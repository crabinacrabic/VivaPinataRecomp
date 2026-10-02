/**
 * @file        vp_tools/gpu_state.h
 * @brief       Statistical sampler of the guest GPU render-backend state
 *
 * @remarks     Research aid for making ROV fast on AMD (Knowlage_BASE/ROV_AMD_RESEARCH.md).
 *              The GPU plugin keeps the guest register file in GraphicsSystem; the command
 *              processor thread writes it while it walks the PM4 stream. A sampler thread
 *              reads the render-backend registers (MSAA, EDRAM mode, render target formats,
 *              depth test, blending) as fast as it can for a few seconds and counts:
 *              - samples: share of time the command processor sat in a state;
 *              - entries: how many times the state was entered (roughly passes / batches).
 *              The reads race with the writer; single aligned dwords are atomic on x64,
 *              so each register value is real, only combinations can mix two states.
 */

#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <tuple>
#include <vector>

#include <fmt/format.h>

#include <rex/graphics/graphics_system.h>
#include <rex/graphics/registers.h>
#include <rex/runtime.h>

namespace vp_tools
{
  struct GpuState
  {
    uint32_t pitch = 0;       // RB_SURFACE_INFO.surface_pitch, pixels
    uint32_t msaa = 0;        // 0 = 1x, 1 = 2x, 2 = 4x
    uint32_t edram_mode = 0;  // xenos::EdramMode
    uint32_t rt_mask = 0;     // bit i: RB_COLOR_MASK nibble i is not zero
    uint32_t formats[4] = {}; // xenos::ColorRenderTargetFormat, for RTs in rt_mask
    uint32_t depth_format = 0;
    uint32_t depth_control = 0;  // RB_DEPTHCONTROL
    uint32_t blend0 = 0;         // RB_BLENDCONTROL0

    auto Key() const
    {
      return std::make_tuple(pitch, msaa, edram_mode, rt_mask, formats[0], formats[1], formats[2], formats[3],
                             depth_format, depth_control, blend0);
    }
    bool operator<(const GpuState &o) const { return Key() < o.Key(); }
    bool operator==(const GpuState &o) const { return Key() == o.Key(); }

    bool z_enable() const { return (depth_control >> 1) & 1; }
    bool z_write() const { return (depth_control >> 2) & 1; }
    uint32_t zfunc() const { return (depth_control >> 4) & 7; }
    bool stencil() const { return depth_control & 1; }
    // ONE * src + ZERO * dst for both color and alpha.
    bool blending() const { return (blend0 & 0x1FFF1FFF) != 0x00010001; }
  };

  struct GpuStateCount
  {
    uint64_t samples = 0;
    uint64_t entries = 0;
  };

  inline const char *ColorFormatName(uint32_t f)
  {
    switch (f)
    {
    case 0: return "8888";
    case 1: return "8888_GAMMA";
    case 2: return "2_10_10_10";
    case 3: return "7e3";
    case 4: return "16_16";
    case 5: return "16x4";
    case 6: return "16_16_F";
    case 7: return "16x4_F";
    case 10: return "2_10_10_10_AS_10x4";
    case 12: return "7e3_AS_16x4";
    case 14: return "32_F";
    case 15: return "32_32_F";
    default: return "?";
    }
  }

  inline const char *EdramModeName(uint32_t m)
  {
    switch (m)
    {
    case 0: return "none";
    case 4: return "color+depth";
    case 5: return "depth only";
    case 6: return "copy";
    default: return "?";
    }
  }

  inline const char *CompareName(uint32_t f)
  {
    static const char *const kNames[8] = {"never", "less", "equal", "lequal", "greater", "notequal", "gequal", "always"};
    return kNames[f & 7];
  }

  class GpuStateSampler
  {
  public:
    static GpuStateSampler &Get()
    {
      static GpuStateSampler s;
      return s;
    }

    ~GpuStateSampler()
    {
      if (thread_.joinable())
      {
        thread_.join();
      }
    }

    bool running() const { return running_.load(); }
    bool available() const { return Registers() != nullptr; }

    // Samples for `seconds` on a background thread; the previous result is replaced.
    void Start(double seconds)
    {
      if (running_.exchange(true))
      {
        return;
      }
      if (thread_.joinable())
      {
        thread_.join();
      }
      thread_ = std::thread([this, seconds] { Run(seconds); });
    }

    struct Result
    {
      std::vector<std::pair<GpuState, GpuStateCount>> states;  // most samples first
      uint64_t samples = 0;
      double seconds = 0;
    };

    Result Snapshot() const
    {
      std::lock_guard lock(mutex_);
      return result_;
    }

    // Plain text for the log and the clipboard.
    static std::string Report(const Result &r)
    {
      std::string out = fmt::format("VP Tools GPU state: {} samples in {:.1f} s, {} states\n", r.samples, r.seconds,
                                    r.states.size());
      uint64_t msaa[3] = {}, drawing = 0;
      for (const auto &[s, c] : r.states)
      {
        if (s.edram_mode == 4 || s.edram_mode == 5)
        {
          msaa[std::min<uint32_t>(s.msaa, 2)] += c.samples;
          drawing += c.samples;
        }
      }
      if (drawing)
      {
        out += fmt::format("MSAA while drawing: 1x {:.1f}%, 2x {:.1f}%, 4x {:.1f}%\n", 100.0 * msaa[0] / drawing,
                           100.0 * msaa[1] / drawing, 100.0 * msaa[2] / drawing);
      }
      for (const auto &[s, c] : r.states)
      {
        out += fmt::format("{:5.1f}% x{:<6} {}\n", r.samples ? 100.0 * c.samples / r.samples : 0.0, c.entries,
                           Describe(s));
      }
      return out;
    }

    static std::string Describe(const GpuState &s)
    {
      std::string rts;
      for (uint32_t i = 0; i < 4; ++i)
      {
        if (s.rt_mask & (1u << i))
        {
          rts += fmt::format("{}{}:{}", rts.empty() ? "" : " ", i, ColorFormatName(s.formats[i]));
        }
      }
      std::string depth = "off";
      if (s.z_enable() || s.stencil())
      {
        depth = fmt::format("{} {}{}{}", s.depth_format ? "D24FS8" : "D24S8", s.z_enable() ? CompareName(s.zfunc()) : "-",
                            s.z_write() ? " write" : "", s.stencil() ? " stencil" : "");
      }
      return fmt::format("pitch {} {}x {} | rt [{}] | depth {} | blend0 {}", s.pitch, 1u << s.msaa,
                         EdramModeName(s.edram_mode), rts.empty() ? "-" : rts, depth,
                         s.blending() ? fmt::format("{:08X}", s.blend0) : std::string("off"));
    }

  private:
    static const uint32_t *Registers()
    {
      rex::Runtime *rt = rex::Runtime::instance();
      if (!rt || !rt->graphics_system())
      {
        return nullptr;
      }
      auto *gs = static_cast<rex::graphics::GraphicsSystem *>(rt->graphics_system());
      return gs->register_file()->values;
    }

    static uint32_t Load(const uint32_t *regs, uint32_t index)
    {
      return *reinterpret_cast<const volatile uint32_t *>(regs + index);
    }

    static GpuState Read(const uint32_t *regs)
    {
      using namespace rex::graphics;
      GpuState s;
      const uint32_t surface = Load(regs, XE_GPU_REG_RB_SURFACE_INFO);
      s.pitch = surface & 0x3FFF;
      s.msaa = (surface >> 16) & 3;
      s.edram_mode = Load(regs, XE_GPU_REG_RB_MODECONTROL) & 7;
      const uint32_t mask = Load(regs, XE_GPU_REG_RB_COLOR_MASK);
      static constexpr uint32_t kColorInfo[4] = {XE_GPU_REG_RB_COLOR_INFO, XE_GPU_REG_RB_COLOR1_INFO,
                                                 XE_GPU_REG_RB_COLOR2_INFO, XE_GPU_REG_RB_COLOR3_INFO};
      if (s.edram_mode == 4)
      {
        for (uint32_t i = 0; i < 4; ++i)
        {
          if ((mask >> (4 * i)) & 0xF)
          {
            s.rt_mask |= 1u << i;
            s.formats[i] = (Load(regs, kColorInfo[i]) >> 16) & 0xF;
          }
        }
        s.blend0 = s.rt_mask & 1 ? Load(regs, XE_GPU_REG_RB_BLENDCONTROL0) : 0;
      }
      if (s.edram_mode == 4 || s.edram_mode == 5)
      {
        s.depth_control = Load(regs, XE_GPU_REG_RB_DEPTHCONTROL);
        s.depth_format = (Load(regs, XE_GPU_REG_RB_DEPTH_INFO) >> 16) & 1;
      }
      return s;
    }

    void Run(double seconds)
    {
      std::map<GpuState, GpuStateCount> counts;
      uint64_t samples = 0;
      const auto start = std::chrono::steady_clock::now();
      const auto end = start + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                                   std::chrono::duration<double>(seconds));
      GpuState last;
      bool have_last = false;
      while (std::chrono::steady_clock::now() < end)
      {
        const uint32_t *regs = Registers();
        if (!regs)
        {
          break;
        }
        // A burst of reads between clock checks keeps the sampling rate high.
        for (int i = 0; i < 64; ++i)
        {
          const GpuState s = Read(regs);
          GpuStateCount &c = counts[s];
          ++c.samples;
          if (!have_last || !(s == last))
          {
            ++c.entries;
            last = s;
            have_last = true;
          }
          ++samples;
        }
        std::this_thread::yield();
      }
      Result r;
      r.samples = samples;
      r.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
      r.states.assign(counts.begin(), counts.end());
      std::sort(r.states.begin(), r.states.end(),
                [](const auto &a, const auto &b) { return a.second.samples > b.second.samples; });
      {
        std::lock_guard lock(mutex_);
        result_ = std::move(r);
      }
      running_ = false;
    }

    mutable std::mutex mutex_;
    Result result_;
    std::atomic<bool> running_{false};
    std::thread thread_;
  };
} // namespace vp_tools
