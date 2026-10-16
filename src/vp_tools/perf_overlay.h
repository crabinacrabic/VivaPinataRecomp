// vp_tools/perf_overlay.h - performance overlay, F2 (game window focused):
// game ticks per second, CPU and GPU load of the game process, memory and the
// render mode, in the top left corner. The idea comes from Army of Two's
// hardware_overlay.h; here the GPU load is the 3D engine utilization of the
// process from the Windows GPU engine counters (what Task Manager and the ROV
// research benchmarks use), and the game tick rate is the one of the
// "[vp_tools] ticks" log lines. The system queries run on a worker thread once
// per second, so drawing never waits for them. pdh.dll and dxgi.dll are loaded
// at run time (no extra link libraries).
#pragma once

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cwchar>
#include <iterator>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dxgi1_4.h>
#include <pdh.h>
#include <pdhmsg.h>
#include <psapi.h>
#endif

#include <imgui.h>

#include <rex/cvar.h>
#include <rex/ui/imgui_dialog.h>
#include <rex/ui/window.h>

#include "game_cvars.h"
#include "launcher.h"
#include "vp_tools/state.h"

namespace vp_tools
{
  // System numbers of the game process, refreshed once per second.
  struct PerfSample
  {
    double cpu_percent = -1.0;      // share of all logical CPUs
    double gpu_percent = -1.0;      // 3D engine utilization
    uint64_t vram_used = 0;         // local video memory of this process, bytes
    uint64_t vram_budget = 0;
    uint64_t ram_used = 0;          // working set, bytes
    uint32_t system_ram_load = 0;   // percent
    uint64_t system_ram_total = 0;  // bytes
    std::string gpu_name;
  };

#if defined(_WIN32)
  class PerfSampler
  {
  public:
    PerfSampler() : thread_([this] { Run(); }) {}

    ~PerfSampler()
    {
      {
        std::lock_guard lock(mutex_);
        stop_ = true;
      }
      wake_.notify_all();
      thread_.join();
    }

    PerfSample Get() const
    {
      std::lock_guard lock(mutex_);
      return sample_;
    }

  private:
    void Run()
    {
      OpenDxgi();
      while (true)
      {
        PerfSample sample;
        sample.gpu_name = gpu_name_;
        sample.cpu_percent = SampleCpu();
        sample.gpu_percent = SampleGpu();
        SampleMemory(sample);
        {
          std::unique_lock lock(mutex_);
          sample_ = std::move(sample);
          if (wake_.wait_for(lock, std::chrono::seconds(1), [this] { return stop_; }))
          {
            break;
          }
        }
      }
      ClosePdh();
      if (adapter_)
      {
        adapter_->Release();
      }
    }

    static uint64_t FileTime64(const FILETIME &ft)
    {
      return (uint64_t(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
    }

    // Process CPU time over the time of all logical CPUs since the last call.
    double SampleCpu()
    {
      FILETIME idle, kernel, user, creation, exit, process_kernel, process_user;
      if (!GetSystemTimes(&idle, &kernel, &user) ||
          !GetProcessTimes(GetCurrentProcess(), &creation, &exit, &process_kernel, &process_user))
      {
        return -1.0;
      }
      // The system kernel time includes the idle time.
      const uint64_t system_time = FileTime64(kernel) + FileTime64(user);
      const uint64_t process_time = FileTime64(process_kernel) + FileTime64(process_user);
      double percent = -1.0;
      if (last_system_time_ && system_time > last_system_time_)
      {
        percent = std::min(100.0, 100.0 * double(process_time - last_process_time_) /
                                      double(system_time - last_system_time_));
      }
      last_system_time_ = system_time;
      last_process_time_ = process_time;
      return percent;
    }

    // \GPU Engine(pid_<pid>_*engtype_3D)\Utilization Percentage, summed over
    // the instances (one per adapter and 3D engine).
    double SampleGpu()
    {
      if (!pdh_query_ && !OpenPdh())
      {
        return -1.0;
      }
      if (pdh_.collect(pdh_query_) != ERROR_SUCCESS)
      {
        return -1.0;
      }
      DWORD size = 0, count = 0;
      if (pdh_.get_array(pdh_counter_, PDH_FMT_DOUBLE, &size, &count, nullptr) != PDH_MORE_DATA)
      {
        // No instances yet (the engines appear with the first GPU work): ask
        // again with a new query a bit later.
        if (++empty_samples_ >= 5)
        {
          ClosePdh();
          empty_samples_ = 0;
        }
        return -1.0;
      }
      std::vector<uint8_t> buffer(size);
      auto *items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W *>(buffer.data());
      if (pdh_.get_array(pdh_counter_, PDH_FMT_DOUBLE, &size, &count, items) != ERROR_SUCCESS)
      {
        return -1.0;
      }
      double sum = 0.0;
      for (DWORD i = 0; i < count; ++i)
      {
        if (items[i].FmtValue.CStatus == PDH_CSTATUS_VALID_DATA ||
            items[i].FmtValue.CStatus == PDH_CSTATUS_NEW_DATA)
        {
          sum += items[i].FmtValue.doubleValue;
        }
      }
      empty_samples_ = 0;
      // The first collection of a rate counter has no previous value.
      if (!pdh_primed_)
      {
        pdh_primed_ = true;
        return -1.0;
      }
      return std::min(100.0, sum);
    }

    bool OpenPdh()
    {
      if (!pdh_.module)
      {
        pdh_.module = LoadLibraryW(L"pdh.dll");
        if (!pdh_.module)
        {
          return false;
        }
        pdh_.open_query = reinterpret_cast<decltype(pdh_.open_query)>(
            GetProcAddress(pdh_.module, "PdhOpenQueryW"));
        pdh_.add_counter = reinterpret_cast<decltype(pdh_.add_counter)>(
            GetProcAddress(pdh_.module, "PdhAddEnglishCounterW"));
        pdh_.collect = reinterpret_cast<decltype(pdh_.collect)>(
            GetProcAddress(pdh_.module, "PdhCollectQueryData"));
        pdh_.get_array = reinterpret_cast<decltype(pdh_.get_array)>(
            GetProcAddress(pdh_.module, "PdhGetFormattedCounterArrayW"));
        pdh_.close_query = reinterpret_cast<decltype(pdh_.close_query)>(
            GetProcAddress(pdh_.module, "PdhCloseQuery"));
      }
      if (!pdh_.open_query || !pdh_.add_counter || !pdh_.collect || !pdh_.get_array ||
          !pdh_.close_query)
      {
        return false;
      }
      if (pdh_.open_query(nullptr, 0, &pdh_query_) != ERROR_SUCCESS)
      {
        pdh_query_ = nullptr;
        return false;
      }
      wchar_t path[128];
      std::swprintf(path, std::size(path), L"\\GPU Engine(pid_%lu_*engtype_3D)\\Utilization Percentage",
                    GetCurrentProcessId());
      if (pdh_.add_counter(pdh_query_, path, 0, &pdh_counter_) != ERROR_SUCCESS)
      {
        ClosePdh();
        return false;
      }
      pdh_primed_ = false;
      return true;
    }

    void ClosePdh()
    {
      if (pdh_query_)
      {
        pdh_.close_query(pdh_query_);
        pdh_query_ = nullptr;
        pdh_counter_ = nullptr;
      }
    }

    // The adapter with the most dedicated video memory (the discrete GPU on
    // laptops with two).
    void OpenDxgi()
    {
      HMODULE dxgi = LoadLibraryW(L"dxgi.dll");
      if (!dxgi)
      {
        return;
      }
      using CreateFactory = HRESULT(WINAPI *)(REFIID, void **);
      auto create = reinterpret_cast<CreateFactory>(GetProcAddress(dxgi, "CreateDXGIFactory1"));
      IDXGIFactory1 *factory = nullptr;
      if (!create || FAILED(create(IID_PPV_ARGS(&factory))))
      {
        return;
      }
      SIZE_T best_memory = 0;
      IDXGIAdapter1 *adapter = nullptr;
      for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i)
      {
        DXGI_ADAPTER_DESC1 desc{};
        if (SUCCEEDED(adapter->GetDesc1(&desc)) && !(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) &&
            desc.DedicatedVideoMemory > best_memory)
        {
          IDXGIAdapter3 *adapter3 = nullptr;
          if (SUCCEEDED(adapter->QueryInterface(IID_PPV_ARGS(&adapter3))))
          {
            if (adapter_)
            {
              adapter_->Release();
            }
            adapter_ = adapter3;
            best_memory = desc.DedicatedVideoMemory;
            char name[128];
            WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, name, sizeof(name), nullptr, nullptr);
            gpu_name_ = name;
          }
        }
        adapter->Release();
      }
      factory->Release();
    }

    void SampleMemory(PerfSample &sample)
    {
      if (adapter_)
      {
        DXGI_QUERY_VIDEO_MEMORY_INFO info{};
        if (SUCCEEDED(adapter_->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &info)))
        {
          sample.vram_used = info.CurrentUsage;
          sample.vram_budget = info.Budget;
        }
      }
      PROCESS_MEMORY_COUNTERS counters{};
      if (K32GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters)))
      {
        sample.ram_used = counters.WorkingSetSize;
      }
      MEMORYSTATUSEX status{};
      status.dwLength = sizeof(status);
      if (GlobalMemoryStatusEx(&status))
      {
        sample.system_ram_load = status.dwMemoryLoad;
        sample.system_ram_total = status.ullTotalPhys;
      }
    }

    struct Pdh
    {
      HMODULE module = nullptr;
      PDH_STATUS(WINAPI *open_query)(LPCWSTR, DWORD_PTR, PDH_HQUERY *) = nullptr;
      PDH_STATUS(WINAPI *add_counter)(PDH_HQUERY, LPCWSTR, DWORD_PTR, PDH_HCOUNTER *) = nullptr;
      PDH_STATUS(WINAPI *collect)(PDH_HQUERY) = nullptr;
      PDH_STATUS(WINAPI *get_array)(PDH_HCOUNTER, DWORD, LPDWORD, LPDWORD,
                                    PPDH_FMT_COUNTERVALUE_ITEM_W) = nullptr;
      PDH_STATUS(WINAPI *close_query)(PDH_HQUERY) = nullptr;
    };

    mutable std::mutex mutex_;
    std::condition_variable wake_;
    bool stop_ = false;
    PerfSample sample_;
    uint64_t last_system_time_ = 0;
    uint64_t last_process_time_ = 0;
    Pdh pdh_;
    PDH_HQUERY pdh_query_ = nullptr;
    PDH_HCOUNTER pdh_counter_ = nullptr;
    bool pdh_primed_ = false;
    int empty_samples_ = 0;
    IDXGIAdapter3 *adapter_ = nullptr;
    std::string gpu_name_;
    // Last: starts after everything above is constructed.
    std::thread thread_;
  };
#endif

  class PerfOverlay : public rex::ui::ImGuiDialog
  {
  public:
    PerfOverlay(rex::ui::ImGuiDrawer *drawer, rex::ui::Window *window)
        : rex::ui::ImGuiDialog(drawer), window_(window), open_(REXCVAR_GET(vp_perf_overlay))
    {
    }

  protected:
    void OnDraw(ImGuiIO &io) override
    {
      (void)io;
      PollToggle();
      if (!open_)
      {
        return;
      }
#if defined(_WIN32)
      if (!sampler_)
      {
        sampler_ = std::make_unique<PerfSampler>();
      }
      const PerfSample s = sampler_->Get();
#else
      const PerfSample s;
#endif
      const bool ru = vp_launcher::UiRussian();
      if (UpdateTicks() || mode_text_.empty())
      {
        mode_text_ = RenderModeText(ru);
      }
      const ImVec4 good(0.45f, 0.95f, 0.45f, 1.0f), warn(1.0f, 0.85f, 0.3f, 1.0f),
          bad(1.0f, 0.4f, 0.35f, 1.0f), dim(0.7f, 0.74f, 0.72f, 1.0f);

      vp_launcher::FontScope font(vp_launcher::GetFonts().caption);
      ImGui::SetNextWindowPos(ImVec2(10.0f, 10.0f), ImGuiCond_Always);
      ImGui::SetNextWindowBgAlpha(0.6f);
      constexpr ImGuiWindowFlags kFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                                          ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                                          ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs |
                                          ImGuiWindowFlags_NoMove;
      if (ImGui::Begin("##vp_perf_overlay", nullptr, kFlags))
      {
        // Game ticks: the game draws one frame per tick, capped at 30.
        const ImVec4 tick_color = ticks_.per_second >= 29.0 ? good : ticks_.per_second >= 20.0 ? warn : bad;
        ImGui::TextColored(tick_color, ru ? "Тики  %.1f/с" : "Ticks %.1f/s", ticks_.per_second);
        ImGui::SameLine();
        ImGui::TextColored(dim, ru ? "кадр %.1f мс, худший %.0f мс" : "frame %.1f ms, worst %.0f ms",
                           ticks_.average_ms, ticks_.worst_ms);

        Bar("CPU", s.cpu_percent, ru ? "процесс, все ядра" : "process, all cores", good, warn, dim);
        Bar("GPU", s.gpu_percent, ru ? "3D, процесс" : "3D, process", good, warn, dim);

        if (s.vram_budget)
        {
          ImGui::Text("VRAM  %.2f / %.2f GB", Gb(s.vram_used), Gb(s.vram_budget));
        }
        if (s.ram_used)
        {
          ImGui::Text("RAM   %.2f GB", Gb(s.ram_used));
          ImGui::SameLine();
          ImGui::TextColored(dim, ru ? "(система %u%% из %.0f GB)" : "(system %u%% of %.0f GB)",
                             s.system_ram_load, Gb(s.system_ram_total));
        }

        ImGui::Separator();
        ImGui::TextColored(dim, "%s", mode_text_.c_str());
        if (!s.gpu_name.empty())
        {
          ImGui::TextColored(dim, "%s", s.gpu_name.c_str());
        }
      }
      ImGui::End();
    }

  private:
    using Clock = std::chrono::steady_clock;

    static double Gb(uint64_t bytes) { return double(bytes) / (1024.0 * 1024.0 * 1024.0); }

    static void Bar(const char *label, double percent, const char *note, const ImVec4 &good,
                    const ImVec4 &warn, const ImVec4 &dim)
    {
      if (percent < 0.0)
      {
        ImGui::TextColored(dim, "%s   --", label);
        return;
      }
      ImGui::TextColored(percent >= 95.0 ? warn : good, "%s   %3.0f%%", label, percent);
      ImGui::SameLine();
      ImGui::ProgressBar(float(percent / 100.0), ImVec2(90.0f, ImGui::GetTextLineHeight()), "");
      ImGui::SameLine();
      ImGui::TextColored(dim, "%s", note);
    }

    // API, render target path and readback as the GPU plugin was set up.
    static std::string RenderModeText(bool ru)
    {
      const std::string backend = vp_launcher::NormalizedBackend(vp_launcher::ActiveBackend());
      if (vp_launcher::BackendFallback())
      {
        return ru ? "D3D12 (Vulkan недоступен)" : "D3D12 (Vulkan unavailable)";
      }
      std::string text;
      if (backend == "vulkan")
      {
        const std::string path = rex::cvar::Query<std::string>("render_target_path_vulkan");
        text = path == "fsi" ? (ru ? "Vulkan, точная EDRAM (FSI)" : "Vulkan, exact EDRAM (FSI)")
                             : "Vulkan, " + (path.empty() ? std::string("FBO") : path);
      }
      else
      {
        const std::string path = rex::cvar::Query<std::string>("render_target_path_d3d12");
        text = path == "rov" ? (ru ? "D3D12, точная EDRAM (ROV)" : "D3D12, exact EDRAM (ROV)")
                             : "D3D12, " + (path.empty() ? std::string("RTV") : path);
      }
      text += ", readback " + rex::cvar::Query<std::string>("readback_resolve");
      return text;
    }

    // Ticks per second from the appMainTickPreDraw hook (vp_tools/hooks.h),
    // the same count as the "[vp_tools] ticks" log lines; average and worst
    // interval of the last half second or more. True when updated.
    bool UpdateTicks()
    {
      const auto now = Clock::now();
      const double dt = std::chrono::duration<double>(now - ticks_.last_time).count();
      if (dt < 0.5)
      {
        return false;
      }
      uint64_t count;
      float sum = 0.0f, worst = 0.0f;
      size_t n = 0;
      {
        std::lock_guard lock(Mutex());
        const TickTimes &t = Ticks();
        count = t.count;
        const uint64_t recent = std::min<uint64_t>({count - ticks_.last_count, count, TickTimes::kSize});
        for (uint64_t i = 0; i < recent; ++i)
        {
          const float ms = t.interval_ms[(count - 1 - i) % TickTimes::kSize];
          sum += ms;
          worst = std::max(worst, ms);
          ++n;
        }
      }
      if (ticks_.last_time != Clock::time_point{})
      {
        ticks_.per_second = double(count - ticks_.last_count) / dt;
        ticks_.average_ms = n ? sum / float(n) : 0.0f;
        ticks_.worst_ms = worst;
      }
      ticks_.last_count = count;
      ticks_.last_time = now;
      return true;
    }

    void PollToggle()
    {
#if defined(_WIN32)
      const bool focused = window_ && window_->HasFocus();
      const bool f2 = focused && (GetAsyncKeyState(VK_F2) & 0x8000) != 0;
      if (f2 && !f2_down_)
      {
        open_ = !open_;
      }
      f2_down_ = f2;
#endif
    }

    struct TickRate
    {
      Clock::time_point last_time{};
      uint64_t last_count = 0;
      double per_second = 0.0;
      float average_ms = 0.0f;
      float worst_ms = 0.0f;
    };

    rex::ui::Window *window_;
    bool open_;
    bool f2_down_ = false;
    TickRate ticks_;
    std::string mode_text_;
#if defined(_WIN32)
    std::unique_ptr<PerfSampler> sampler_;
#endif
  };
}  // namespace vp_tools
