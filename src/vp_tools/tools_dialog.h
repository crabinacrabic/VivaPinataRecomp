// vp_tools/tools_dialog.h - the VP Tools menu (ImGui over the game)
//
// Opens with F1 (game window focused) or by holding Back on a controller for
// 0.5 s; F1, Esc, B, a second Back hold or the window's X close it. While it
// is open the game gets no pad input (VivapinataApp::OnPostSetup gates the
// InputSystem on g_menu_open) and the mouse cursor is shown.
//
// Stage 1 tabs:
//   Garden - game tick rate, the garden scene seen in spawn calls, the last
//            spawn, and the player fields behind the credits-function candidate;
//   Memory - value scanner (vp_tools/memory_scan.h) and a watch list;
//   Trace  - calls of the hooked engine functions (vp_tools/hooks.h), with a
//            "Copy report" button that also writes the report to the log.
//
// Layout and the Back-hold gesture follow ReTiP's TiP Tools menu
// (SolarCookies/TiP-Recomp, used with the author's permission).
#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <Xinput.h>
#pragma comment(lib, "xinput.lib")
#endif

#include <imgui.h>

#include <fmt/format.h>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/runtime.h>
#include <rex/ui/imgui_dialog.h>
#include <rex/ui/window.h>

#include "game_cvars.h"
#include "launcher.h"
#include "vp_tools/memory_scan.h"
#include "vp_tools/state.h"

namespace vp_tools
{
  // Menu strings, EN / RU, same scheme as vp_launcher::UiText. "###id" keeps
  // ImGui state when the language changes.
  struct ToolsText
  {
    const char *window_title;
    const char *tab_garden;
    const char *tab_memory;
    const char *tab_trace;
    const char *hint;

    const char *ticks_per_second;
    const char *ticks_total;
    const char *ticks_none;
    const char *scene;
    const char *scene_none;
    const char *last_spawn;
    const char *spawn_none;
    const char *player_header;
    const char *player_unverified;
    const char *player_none;
    const char *coins;
    const char *experience;
    const char *level;
    const char *set;

    const char *value;
    const char *first_scan;
    const char *next_scan;
    const char *reset;
    const char *found;
    const char *truncated;
    const char *scan_help;
    const char *too_many;
    const char *address;
    const char *current;
    const char *watch;
    const char *watch_list;
    const char *add_address;
    const char *add;
    const char *write;
    const char *remove;
    const char *unreadable;
    const char *write_failed;

    const char *trace_log;
    const char *copy_report;
    const char *copied;
    const char *functions;
    const char *function;
    const char *calls;
    const char *caller;
    const char *spawns;
    const char *spawns_none;
    const char *trace_help;
  };

  inline constexpr ToolsText kToolsEn = {
      "VP Tools###vp_tools",
      "Garden###garden",
      "Memory###memory",
      "Trace###trace",
      "F1 / Esc / B: close",

      "Game ticks per second",
      "ticks total",
      "no game ticks yet",
      "Garden scene",
      "not seen yet (it is captured when the game spawns a pinata)",
      "Last spawn",
      "none yet",
      "Player (credits-function candidate)",
      "Unverified: compare the coins below with the game before changing anything.",
      "The candidate function has not been called yet (buy or sell something).",
      "Coins (+4)",
      "Experience (+16)",
      "Level (+20)",
      "Set",

      "Value",
      "First scan",
      "Next scan",
      "Reset",
      "Addresses found",
      "Result list is full (200 000): use a less common value.",
      "Enter the current value (for example, your coins) and press First scan. Change it in the game, "
      "enter the new value, press Next scan. Repeat until a few addresses are left.",
      "Too many addresses to list; keep scanning.",
      "Address",
      "Value now",
      "Watch",
      "Watch list",
      "Address (hex)",
      "Add",
      "Write",
      "Remove",
      "not readable",
      "write failed (page is not writable)",

      "Write the first calls to the log (vp_tools_trace)",
      "Copy report",
      "Copied to the clipboard and written to the log.",
      "Engine functions",
      "Function",
      "Calls",
      "Caller",
      "Spawns (supportPinataCreateGeneralEx)",
      "No spawns yet.",
      "? marks a candidate that is not confirmed yet. A function that keeps 0 calls in the garden is "
      "probably a wrong match.",
  };

  inline constexpr ToolsText kToolsRu = {
      "VP Tools###vp_tools",
      "Сад###garden",
      "Память###memory",
      "Трассировка###trace",
      "F1 / Esc / B: закрыть",

      "Игровых тиков в секунду",
      "тиков всего",
      "игра ещё не тикала",
      "Сцена сада",
      "пока не видна (запоминается, когда игра создаёт пиньяту)",
      "Последнее создание",
      "пока нет",
      "Игрок (кандидат функции монет)",
      "Не проверено: сравните монеты ниже с игрой, прежде чем что-то менять.",
      "Функция-кандидат ещё не вызывалась (купите или продайте что-нибудь).",
      "Монеты (+4)",
      "Опыт (+16)",
      "Уровень (+20)",
      "Задать",

      "Значение",
      "Первый поиск",
      "Отсеять",
      "Сброс",
      "Найдено адресов",
      "Список заполнен (200 000): возьмите менее частое значение.",
      "Введите текущее значение (например, монеты) и нажмите «Первый поиск». Измените его в игре, "
      "введите новое значение и нажмите «Отсеять». Повторяйте, пока не останется несколько адресов.",
      "Адресов слишком много для списка; продолжайте отсеивать.",
      "Адрес",
      "Сейчас",
      "Следить",
      "Список наблюдения",
      "Адрес (hex)",
      "Добавить",
      "Записать",
      "Убрать",
      "не читается",
      "не записалось (страница только для чтения)",

      "Писать первые вызовы в лог (vp_tools_trace)",
      "Скопировать отчёт",
      "Скопировано в буфер обмена и записано в лог.",
      "Функции движка",
      "Функция",
      "Вызовы",
      "Откуда",
      "Создание пиньят (supportPinataCreateGeneralEx)",
      "Пиньяты ещё не создавались.",
      "? — кандидат, ещё не подтверждён. Если в саду у функции так и остаётся 0 вызовов, это, скорее "
      "всего, неверное совпадение.",
  };

  inline const ToolsText &T() { return vp_launcher::UiRussian() ? kToolsRu : kToolsEn; }

  class ToolsDialog : public rex::ui::ImGuiDialog
  {
  public:
    ToolsDialog(rex::ui::ImGuiDrawer *drawer, rex::ui::Window *window)
        : rex::ui::ImGuiDialog(drawer), window_(window)
    {
    }

    ~ToolsDialog() override { g_menu_open = false; }

  protected:
    void OnDraw(ImGuiIO &io) override
    {
      (void)io;
      const bool enabled = REXCVAR_GET(vp_tools) && rex::Runtime::instance() != nullptr;
      if (!enabled)
      {
        SetOpen(false);
        return;
      }

      UpdateTickRate();
      PollToggle();
      if (!open_)
      {
        return;
      }

      vp_launcher::FontScope font(vp_launcher::GetFonts().caption);
      const ToolsText &t = T();
      ImGui::SetNextWindowPos(ImVec2(40, 40), ImGuiCond_FirstUseEver);
      ImGui::SetNextWindowSize(ImVec2(720, 520), ImGuiCond_FirstUseEver);
      bool keep_open = true;
      if (ImGui::Begin(t.window_title, &keep_open, ImGuiWindowFlags_NoCollapse))
      {
        ImGui::TextDisabled("%s", t.hint);
        if (ImGui::BeginTabBar("vp_tools_tabs"))
        {
          if (ImGui::BeginTabItem(t.tab_garden))
          {
            DrawGarden(t);
            ImGui::EndTabItem();
          }
          if (ImGui::BeginTabItem(t.tab_memory))
          {
            DrawMemory(t);
            ImGui::EndTabItem();
          }
          if (ImGui::BeginTabItem(t.tab_trace))
          {
            DrawTrace(t);
            ImGui::EndTabItem();
          }
          ImGui::EndTabBar();
        }
      }
      ImGui::End();
      if (!keep_open)
      {
        SetOpen(false);
      }
    }

  private:
    using Clock = std::chrono::steady_clock;

    struct Watch
    {
      uint32_t addr;
      int32_t new_value;
    };

    // --- open / close --------------------------------------------------------------

    void SetOpen(bool open)
    {
      if (open_ == open)
      {
        return;
      }
      open_ = open;
      g_menu_open = open;
      if (!window_)
      {
        return;
      }
      if (open)
      {
        saved_cursor_ = window_->GetCursorVisibility();
        window_->SetCursorVisibility(rex::ui::Window::CursorVisibility::kVisible);
      }
      else
      {
        window_->SetCursorVisibility(saved_cursor_);
      }
    }

    void PollToggle()
    {
#if defined(_WIN32)
      const bool focused = window_ && window_->HasFocus();
      const bool f1 = focused && (GetAsyncKeyState(VK_F1) & 0x8000) != 0;
      if (f1 && !f1_down_)
      {
        SetOpen(!open_);
      }
      f1_down_ = f1;

      bool back = false;
      bool b = false;
      for (DWORD user = 0; user < XUSER_MAX_COUNT; ++user)
      {
        XINPUT_STATE state{};
        if (XInputGetState(user, &state) == ERROR_SUCCESS)
        {
          back |= (state.Gamepad.wButtons & XINPUT_GAMEPAD_BACK) != 0;
          b |= (state.Gamepad.wButtons & XINPUT_GAMEPAD_B) != 0;
        }
      }
      const auto now = Clock::now();
      if (!back)
      {
        back_held_ = false;
        back_used_ = false;
      }
      else if (!back_held_)
      {
        back_held_ = true;
        back_since_ = now;
      }
      else if (!back_used_ && now - back_since_ >= std::chrono::milliseconds(500))
      {
        back_used_ = true;  // one toggle per hold
        SetOpen(!open_);
      }
      if (open_ && b && !b_down_)
      {
        SetOpen(false);
      }
      b_down_ = b;
#endif
      if (open_ && ImGui::IsKeyPressed(ImGuiKey_Escape, false))
      {
        SetOpen(false);
      }
    }

    // --- Garden --------------------------------------------------------------------

    void UpdateTickRate()
    {
      const auto now = Clock::now();
      const double dt = std::chrono::duration<double>(now - tick_rate_.last_time).count();
      if (dt < 0.5)
      {
        return;
      }
      uint64_t calls;
      {
        std::lock_guard lock(Mutex());
        calls = Traces()[kTraceTick].calls;
      }
      tick_rate_.per_second = static_cast<double>(calls - tick_rate_.last_calls) / dt;
      tick_rate_.last_calls = calls;
      tick_rate_.last_time = now;
    }

    void DrawGarden(const ToolsText &t)
    {
      FnTrace tick, credits;
      SpawnLog spawns;
      {
        std::lock_guard lock(Mutex());
        tick = Traces()[kTraceTick];
        credits = Traces()[kTraceCredits];
        spawns = Spawns();
      }

      if (tick.calls)
      {
        ImGui::Text("%s: %.1f  (%llu %s)", t.ticks_per_second, tick_rate_.per_second,
                    static_cast<unsigned long long>(tick.calls), t.ticks_total);
      }
      else
      {
        ImGui::Text("%s: %s", t.ticks_per_second, t.ticks_none);
      }

      if (spawns.last_scene)
      {
        ImGui::Text("%s: 0x%08X", t.scene, spawns.last_scene);
      }
      else
      {
        ImGui::Text("%s: %s", t.scene, t.scene_none);
      }

      if (spawns.count)
      {
        const SpawnCall &s = spawns.ring[(spawns.count - 1) % spawns.ring.size()];
        ImGui::Text("%s: tag %u  pos (%.1f, %.1f, %.1f)  scale %.2f  age %.2f  -> 0x%08X", t.last_spawn,
                    s.tag, s.pos[0], s.pos[1], s.pos[2], s.scale, s.age, s.result);
      }
      else
      {
        ImGui::Text("%s: %s", t.last_spawn, t.spawn_none);
      }

      ImGui::Separator();
      ImGui::TextUnformatted(t.player_header);
      const uint32_t player = credits.last_args[0];
      if (!credits.calls || !PlausibleGuestPointer(player))
      {
        ImGui::TextDisabled("%s", t.player_none);
        return;
      }
      ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "%s", t.player_unverified);
      ImGui::Text("playerMain? = 0x%08X  (%llu calls)", player, static_cast<unsigned long long>(credits.calls));
      const struct
      {
        const char *label;
        uint32_t offset;
      } fields[] = {{t.coins, 4}, {t.experience, 16}, {t.level, 20}};
      for (int i = 0; i < 3; ++i)
      {
        ImGui::PushID(i);
        const uint32_t addr = player + fields[i].offset;
        int32_t value = 0;
        if (MemoryScanner::Read32(addr, value))
        {
          ImGui::Text("%s: %d", fields[i].label, value);
        }
        else
        {
          ImGui::Text("%s: %s", fields[i].label, t.unreadable);
        }
        ImGui::SameLine(260);
        ImGui::SetNextItemWidth(120);
        ImGui::InputInt("##new", &player_edit_[i], 0, 0);
        ImGui::SameLine();
        if (ImGui::Button(t.set))
        {
          MemoryScanner::Write32(addr, player_edit_[i]);
        }
        ImGui::PopID();
      }
    }

    // --- Memory --------------------------------------------------------------------

    void DrawMemory(const ToolsText &t)
    {
      ImGui::TextWrapped("%s", t.scan_help);
      ImGui::SetNextItemWidth(160);
      ImGui::InputInt(t.value, &scan_value_, 1, 100);
      if (ImGui::Button(t.first_scan))
      {
        scanner_.FirstScan(scan_value_);
      }
      ImGui::SameLine();
      ImGui::BeginDisabled(!scanner_.scanned());
      if (ImGui::Button(t.next_scan))
      {
        scanner_.NextScan(scan_value_);
      }
      ImGui::SameLine();
      if (ImGui::Button(t.reset))
      {
        scanner_.Reset();
      }
      ImGui::EndDisabled();

      if (scanner_.scanned())
      {
        ImGui::Text("%s: %zu", t.found, scanner_.count());
        if (scanner_.truncated())
        {
          ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.3f, 1.0f), "%s", t.truncated);
        }
        if (scanner_.count() > 100)
        {
          ImGui::TextDisabled("%s", t.too_many);
        }
        else if (scanner_.count() > 0 &&
                 ImGui::BeginTable("results", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollY,
                                   ImVec2(0, 160)))
        {
          ImGui::TableSetupColumn(t.address);
          ImGui::TableSetupColumn(t.current);
          ImGui::TableSetupColumn("");
          ImGui::TableHeadersRow();
          for (uint32_t addr : scanner_.results())
          {
            ImGui::PushID(static_cast<int>(addr));
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Text("0x%08X", addr);
            ImGui::TableNextColumn();
            DrawValue(t, addr);
            ImGui::TableNextColumn();
            if (ImGui::SmallButton(t.watch))
            {
              AddWatch(addr);
            }
            ImGui::PopID();
          }
          ImGui::EndTable();
        }
      }

      ImGui::Separator();
      ImGui::TextUnformatted(t.watch_list);
      ImGui::SetNextItemWidth(120);
      ImGui::InputScalar(t.add_address, ImGuiDataType_U32, &add_address_, nullptr, nullptr, "%08X",
                         ImGuiInputTextFlags_CharsHexadecimal);
      ImGui::SameLine();
      if (ImGui::Button(t.add))
      {
        AddWatch(add_address_ & ~3u);
      }
      if (!watch_error_.empty())
      {
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.3f, 1.0f), "%s", watch_error_.c_str());
      }
      int remove = -1;
      for (int i = 0; i < static_cast<int>(watches_.size()); ++i)
      {
        Watch &w = watches_[i];
        ImGui::PushID(i);
        ImGui::Text("0x%08X", w.addr);
        ImGui::SameLine(110);
        DrawValue(t, w.addr);
        ImGui::SameLine(240);
        ImGui::SetNextItemWidth(120);
        ImGui::InputInt("##new", &w.new_value, 0, 0);
        ImGui::SameLine();
        if (ImGui::Button(t.write))
        {
          watch_error_ = MemoryScanner::Write32(w.addr, w.new_value) ? std::string() : t.write_failed;
        }
        ImGui::SameLine();
        if (ImGui::Button(t.remove))
        {
          remove = i;
        }
        ImGui::PopID();
      }
      if (remove >= 0)
      {
        watches_.erase(watches_.begin() + remove);
      }
    }

    static void DrawValue(const ToolsText &t, uint32_t addr)
    {
      int32_t value = 0;
      if (MemoryScanner::Read32(addr, value))
      {
        ImGui::Text("%d", value);
      }
      else
      {
        ImGui::TextDisabled("%s", t.unreadable);
      }
    }

    void AddWatch(uint32_t addr)
    {
      const bool known = std::any_of(watches_.begin(), watches_.end(), [&](const Watch &w) { return w.addr == addr; });
      if (known)
      {
        return;
      }
      int32_t value = 0;
      MemoryScanner::Read32(addr, value);
      watches_.push_back({addr, value});
    }

    // --- Trace ---------------------------------------------------------------------

    void DrawTrace(const ToolsText &t)
    {
      std::array<FnTrace, kTraceCount> traces;
      SpawnLog spawns;
      {
        std::lock_guard lock(Mutex());
        traces = Traces();
        spawns = Spawns();
      }

      bool log = REXCVAR_GET(vp_tools_trace);
      if (ImGui::Checkbox(t.trace_log, &log))
      {
        REXCVAR_SET(vp_tools_trace, log);
      }
      if (ImGui::Button(t.copy_report))
      {
        const std::string report = Report(traces, spawns);
        ImGui::SetClipboardText(report.c_str());
        REXLOG_INFO("[vp_tools] report:\n{}", report);
        copied_until_ = Clock::now() + std::chrono::seconds(3);
      }
      if (Clock::now() < copied_until_)
      {
        ImGui::SameLine();
        ImGui::TextDisabled("%s", t.copied);
      }
      ImGui::TextWrapped("%s", t.trace_help);

      ImGui::TextUnformatted(t.functions);
      if (ImGui::BeginTable("functions", 7, ImGuiTableFlags_Borders | ImGuiTableFlags_SizingFixedFit))
      {
        ImGui::TableSetupColumn(t.function);
        ImGui::TableSetupColumn("sub_");
        ImGui::TableSetupColumn(t.calls);
        ImGui::TableSetupColumn(t.caller);
        ImGui::TableSetupColumn("r3 / r4");
        ImGui::TableSetupColumn("-> r3");
        ImGui::TableSetupColumn("extra");
        ImGui::TableHeadersRow();
        for (const FnTrace &f : traces)
        {
          ImGui::TableNextRow();
          ImGui::TableNextColumn();
          ImGui::Text("%s [%s]", f.name, f.grade);
          ImGui::TableNextColumn();
          ImGui::TextUnformatted(f.symbol);
          ImGui::TableNextColumn();
          ImGui::Text("%llu", static_cast<unsigned long long>(f.calls));
          ImGui::TableNextColumn();
          ImGui::Text("%08X", f.last_lr);
          ImGui::TableNextColumn();
          ImGui::Text("%08X %08X", f.last_args[0], f.last_args[1]);
          ImGui::TableNextColumn();
          ImGui::Text("%08X", f.last_result);
          ImGui::TableNextColumn();
          ImGui::Text("%d %d %d", static_cast<int32_t>(f.extra[0]), static_cast<int32_t>(f.extra[1]),
                      static_cast<int32_t>(f.extra[2]));
        }
        ImGui::EndTable();
      }

      ImGui::Spacing();
      ImGui::TextUnformatted(t.spawns);
      if (!spawns.count)
      {
        ImGui::TextDisabled("%s", t.spawns_none);
        return;
      }
      if (ImGui::BeginTable("spawns", 8,
                            ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit,
                            ImVec2(0, 180)))
      {
        ImGui::TableSetupColumn("#");
        ImGui::TableSetupColumn(t.caller);
        ImGui::TableSetupColumn("scene");
        ImGui::TableSetupColumn("tag");
        ImGui::TableSetupColumn("pos");
        ImGui::TableSetupColumn("scale / age");
        ImGui::TableSetupColumn("r9 / r10");
        ImGui::TableSetupColumn("-> entity");
        ImGui::TableHeadersRow();
        const uint64_t shown = std::min<uint64_t>(spawns.count, spawns.ring.size());
        for (uint64_t i = 0; i < shown; ++i)
        {
          const SpawnCall &s = spawns.ring[(spawns.count - 1 - i) % spawns.ring.size()];
          ImGui::TableNextRow();
          ImGui::TableNextColumn();
          ImGui::Text("%llu", static_cast<unsigned long long>(s.index));
          ImGui::TableNextColumn();
          ImGui::Text("%08X", s.caller);
          ImGui::TableNextColumn();
          ImGui::Text("%08X", s.scene);
          ImGui::TableNextColumn();
          ImGui::Text("%u", s.tag);
          ImGui::TableNextColumn();
          ImGui::Text("%.1f %.1f %.1f", s.pos[0], s.pos[1], s.pos[2]);
          ImGui::TableNextColumn();
          ImGui::Text("%.2f / %.2f", s.scale, s.age);
          ImGui::TableNextColumn();
          ImGui::Text("%u / %u", s.r9, s.r10);
          ImGui::TableNextColumn();
          ImGui::Text("%08X", s.result);
        }
        ImGui::EndTable();
      }
    }

    // Plain-text dump of the Trace tab, for the clipboard and the log.
    static std::string Report(const std::array<FnTrace, kTraceCount> &traces, const SpawnLog &spawns)
    {
      std::string out = "VP Tools trace report\n";
      for (const FnTrace &f : traces)
      {
        out += fmt::format("{} ({}, {}): calls={} caller={:08X} r3={:08X} r4={:08X} r5={:08X} r6={:08X} "
                           "-> {:08X} extra={} {} {}\n",
                           f.name, f.symbol, f.grade, f.calls, f.last_lr, f.last_args[0], f.last_args[1],
                           f.last_args[2], f.last_args[3], f.last_result, static_cast<int32_t>(f.extra[0]),
                           static_cast<int32_t>(f.extra[1]), static_cast<int32_t>(f.extra[2]));
      }
      out += fmt::format("spawns: {} total, last scene {:08X}\n", spawns.count, spawns.last_scene);
      const uint64_t shown = std::min<uint64_t>(spawns.count, spawns.ring.size());
      for (uint64_t i = 0; i < shown; ++i)
      {
        const SpawnCall &s = spawns.ring[(spawns.count - 1 - i) % spawns.ring.size()];
        out += fmt::format("  #{} caller={:08X} scene={:08X} tag={} pos=({:.1f}, {:.1f}, {:.1f}) rot={:08X} "
                           "scale={:.2f} age={:.2f} r9={} r10={} -> {:08X}\n",
                           s.index, s.caller, s.scene, s.tag, s.pos[0], s.pos[1], s.pos[2], s.rot_ptr, s.scale,
                           s.age, s.r9, s.r10, s.result);
      }
      return out;
    }

    rex::ui::Window *window_ = nullptr;
    rex::ui::Window::CursorVisibility saved_cursor_ = rex::ui::Window::CursorVisibility::kVisible;
    bool open_ = false;
    bool f1_down_ = false;
    bool b_down_ = false;
    bool back_held_ = false;
    bool back_used_ = false;
    Clock::time_point back_since_{};
    Clock::time_point copied_until_{};

    TickRate tick_rate_;
    int player_edit_[3] = {};

    MemoryScanner scanner_;
    int scan_value_ = 0;
    uint32_t add_address_ = 0;
    std::vector<Watch> watches_;
    std::string watch_error_;
  };
}  // namespace vp_tools
