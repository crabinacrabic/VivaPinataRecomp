// vp_tools/tools_dialog.h - the VP Tools menu (ImGui over the game)
//
// Opens with F1 (game window focused) or by holding Back on a controller for
// 0.5 s; F1, Esc, B, a second Back hold or the window's X close it. While it
// is open the game gets no pad input (VivapinataApp::OnPostSetup gates the
// InputSystem on g_menu_open) and the mouse cursor is shown.
//
// Tabs:
//   Garden  - game tick rate, the garden scene (garden-slot table), the cursor
//             position (cursorCameraTick), the last spawn, and the player
//             fields behind the credits-function candidate;
//   Spawn   - supportPinataCreateGeneralEx at the cursor, by tag; the tag
//             classes come from the game's own class-range table;
//   Memory  - value scanner (vp_tools/memory_scan.h), watch list, and "what
//             writes here" (vp_tools/write_watch.h);
//   Trace   - calls of the hooked engine functions (vp_tools/hooks.h), with a
//             "Copy report" button that also writes the report to the log.
//
// Layout, the Back-hold gesture and the tag-class names follow ReTiP's TiP
// Tools menu (SolarCookies/TiP-Recomp, used with the author's permission).
#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <map>
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
#include "vp_tools/write_watch.h"

namespace vp_tools
{
  // Menu strings, EN / RU, same scheme as vp_launcher::UiText. "###id" keeps
  // ImGui state when the language changes.
  struct ToolsText
  {
    const char *window_title;
    const char *tab_garden;
    const char *tab_spawn;
    const char *tab_memory;
    const char *tab_trace;
    const char *hint;

    const char *ticks_per_second;
    const char *ticks_total;
    const char *ticks_none;
    const char *tick_graph;
    const char *slow_ticks;
    const char *worst;
    const char *scene;
    const char *scene_none;
    const char *cursor;
    const char *cursor_none;
    const char *last_spawn;
    const char *spawn_none;
    const char *player_header;
    const char *player_unverified;
    const char *player_none;
    const char *coins;
    const char *experience;
    const char *level;
    const char *set;

    const char *spawn_help;
    const char *tag_class;
    const char *tag;
    const char *tag_ranges;
    const char *tag_table_missing;
    const char *class_names_note;
    const char *scale;
    const char *age;
    const char *count;
    const char *at_cursor;
    const char *position;
    const char *spawn;
    const char *spawn_no_scene;
    const char *spawn_queued;
    const char *recent_spawns;
    const char *by_tools;

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
    const char *who_writes;
    const char *watching;
    const char *threads;
    const char *stop;
    const char *no_writes;
    const char *watch_help;
    const char *watch_failed;

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
      .window_title = "VP Tools###vp_tools",
      .tab_garden = "Garden###garden",
      .tab_spawn = "Spawn###spawn",
      .tab_memory = "Memory###memory",
      .tab_trace = "Trace###trace",
      .hint = "F1 / Esc / B: close",

      .ticks_per_second = "Game ticks per second",
      .ticks_total = "ticks total",
      .ticks_none = "no game ticks yet",
      .tick_graph = "ms per game tick, last 10 s (33 ms = 30 per second)",
      .slow_ticks = "Slow ticks (over 50 ms)",
      .worst = "worst",
      .scene = "Garden scene",
      .scene_none = "none (not in a garden yet)",
      .cursor = "Cursor",
      .cursor_none = "not seen yet",
      .last_spawn = "Last spawn",
      .spawn_none = "none yet",
      .player_header = "Player (credits-function candidate)",
      .player_unverified = "Unverified: compare the coins below with the game before changing anything.",
      .player_none = "The candidate function has not been called yet (buy or sell something).",
      .coins = "Coins (+4)",
      .experience = "Experience (+16)",
      .level = "Level (+20)",
      .set = "Set",

      .spawn_help = "Creates an object in the current garden through the game's own supportPinataCreateGeneralEx, "
                    "on the next game tick. Experimental: save your garden first.",
      .tag_class = "Class",
      .tag = "Tag",
      .tag_ranges = "Tags of this class",
      .tag_table_missing = "The game's tag-class table is not readable yet.",
      .class_names_note = "Class names are TiP's (same table layout, 3 classes fewer).",
      .scale = "Scale",
      .age = "Age (0 baby - 1 adult)",
      .count = "Count",
      .at_cursor = "At the cursor",
      .position = "Position",
      .spawn = "Spawn",
      .spawn_no_scene = "No garden scene: open your garden first.",
      .spawn_queued = "Queued for the next game tick.",
      .recent_spawns = "Recent spawns",
      .by_tools = "VP Tools",

      .value = "Value",
      .first_scan = "First scan",
      .next_scan = "Next scan",
      .reset = "Reset",
      .found = "Addresses found",
      .truncated = "Result list is full (200 000): use a less common value.",
      .scan_help = "Enter the current value (for example, your coins) and press First scan. Change it in the game, "
                   "enter the new value, press Next scan. Repeat until a few addresses are left.",
      .too_many = "Too many addresses to list; keep scanning.",
      .address = "Address",
      .current = "Value now",
      .watch = "Watch",
      .watch_list = "Watch list",
      .add_address = "Address (hex)",
      .add = "Add",
      .write = "Write",
      .remove = "Remove",
      .unreadable = "not readable",
      .write_failed = "write failed (page is not writable)",
      .who_writes = "What writes?",
      .watching = "Watching writes to",
      .threads = "threads",
      .stop = "Stop",
      .no_writes = "No writes yet: change the value in the game.",
      .watch_help = "Hardware breakpoint: every write is counted by the code that made it. "
                    "Does not work under the Visual Studio debugger (F5).",
      .watch_failed = "Could not arm the breakpoint.",

      .trace_log = "Write the first calls to the log (vp_tools_trace)",
      .copy_report = "Copy report",
      .copied = "Copied to the clipboard and written to the log.",
      .functions = "Engine functions",
      .function = "Function",
      .calls = "Calls",
      .caller = "Caller",
      .spawns = "Spawns (supportPinataCreateGeneralEx)",
      .spawns_none = "No spawns yet.",
      .trace_help = "? marks a candidate that is not confirmed yet. A function that keeps 0 calls in the garden is "
                    "probably a wrong match.",
  };

  inline constexpr ToolsText kToolsRu = {
      .window_title = "VP Tools###vp_tools",
      .tab_garden = "Сад###garden",
      .tab_spawn = "Пиньяты###spawn",
      .tab_memory = "Память###memory",
      .tab_trace = "Трассировка###trace",
      .hint = "F1 / Esc / B: закрыть",

      .ticks_per_second = "Игровых тиков в секунду",
      .ticks_total = "тиков всего",
      .ticks_none = "игра ещё не тикала",
      .tick_graph = "мс на игровой тик, последние 10 с (33 мс = 30 в секунду)",
      .slow_ticks = "Медленных тиков (дольше 50 мс)",
      .worst = "худший",
      .scene = "Сцена сада",
      .scene_none = "нет (вы ещё не в саду)",
      .cursor = "Курсор",
      .cursor_none = "пока не виден",
      .last_spawn = "Последнее создание",
      .spawn_none = "пока нет",
      .player_header = "Игрок (кандидат функции монет)",
      .player_unverified = "Не проверено: сравните монеты ниже с игрой, прежде чем что-то менять.",
      .player_none = "Функция-кандидат ещё не вызывалась (купите или продайте что-нибудь).",
      .coins = "Монеты (+4)",
      .experience = "Опыт (+16)",
      .level = "Уровень (+20)",
      .set = "Задать",

      .spawn_help = "Создаёт объект в текущем саду собственной функцией игры supportPinataCreateGeneralEx "
                    "на следующем игровом тике. Экспериментально: сначала сохраните сад.",
      .tag_class = "Класс",
      .tag = "Тег",
      .tag_ranges = "Теги этого класса",
      .tag_table_missing = "Таблица классов тегов игры пока не читается.",
      .class_names_note = "Названия классов взяты из TiP (таблица устроена так же, классов на 3 меньше).",
      .scale = "Размер",
      .age = "Возраст (0 малыш - 1 взрослый)",
      .count = "Сколько",
      .at_cursor = "У курсора",
      .position = "Позиция",
      .spawn = "Создать",
      .spawn_no_scene = "Нет сцены сада: сначала откройте свой сад.",
      .spawn_queued = "Поставлено в очередь на следующий тик.",
      .recent_spawns = "Последние созданные",
      .by_tools = "VP Tools",

      .value = "Значение",
      .first_scan = "Первый поиск",
      .next_scan = "Отсеять",
      .reset = "Сброс",
      .found = "Найдено адресов",
      .truncated = "Список заполнен (200 000): возьмите менее частое значение.",
      .scan_help = "Введите текущее значение (например, монеты) и нажмите «Первый поиск». Измените его в игре, "
                   "введите новое значение и нажмите «Отсеять». Повторяйте, пока не останется несколько адресов.",
      .too_many = "Адресов слишком много для списка; продолжайте отсеивать.",
      .address = "Адрес",
      .current = "Сейчас",
      .watch = "Следить",
      .watch_list = "Список наблюдения",
      .add_address = "Адрес (hex)",
      .add = "Добавить",
      .write = "Записать",
      .remove = "Убрать",
      .unreadable = "не читается",
      .write_failed = "не записалось (страница только для чтения)",
      .who_writes = "Кто пишет?",
      .watching = "Слежу за записью в",
      .threads = "потоков",
      .stop = "Стоп",
      .no_writes = "Записей пока нет: измените значение в игре.",
      .watch_help = "Аппаратная точка останова: каждая запись засчитывается коду, который её сделал. "
                    "Не работает под отладчиком Visual Studio (F5).",
      .watch_failed = "Не удалось поставить точку останова.",

      .trace_log = "Писать первые вызовы в лог (vp_tools_trace)",
      .copy_report = "Скопировать отчёт",
      .copied = "Скопировано в буфер обмена и записано в лог.",
      .functions = "Функции движка",
      .function = "Функция",
      .calls = "Вызовы",
      .caller = "Откуда",
      .spawns = "Создание объектов (supportPinataCreateGeneralEx)",
      .spawns_none = "Объекты ещё не создавались.",
      .trace_help = "? — кандидат, ещё не подтверждён. Если в саду у функции так и остаётся 0 вызовов, это, скорее "
                    "всего, неверное совпадение.",
  };

  inline const ToolsText &T() { return vp_launcher::UiRussian() ? kToolsRu : kToolsEn; }

  // --- game data read by the menu -------------------------------------------------

  // The scene of garden id 1, as sub_82106E40 (gardenMainGetGardenScene) finds it.
  inline uint32_t CurrentGardenScene()
  {
    for (uint32_t slot = kGardenSlots; slot < kGardenSlotsEnd; slot += kGardenSlotSize)
    {
      int32_t id = 0;
      int32_t scene = 0;
      if (MemoryScanner::Read32(slot + 16, id) && id == 1 && MemoryScanner::Read32(slot + 4, scene))
      {
        return static_cast<uint32_t>(scene);
      }
    }
    return 0;
  }

  // Printable text at a guest address (TiP's scene objects start with their
  // script name), or "" if there is none.
  inline std::string GuestText(uint32_t addr, size_t max_len = 64)
  {
    std::string out;
    for (uint32_t a = addr & ~3u; out.size() < max_len; a += 4)
    {
      int32_t word = 0;
      if (!MemoryScanner::Read32(a, word))
      {
        break;
      }
      bool end = false;
      for (int shift = 24; shift >= 0; shift -= 8)
      {
        const char ch = static_cast<char>((static_cast<uint32_t>(word) >> shift) & 0xFF);
        if (a + (24 - shift) / 8 < addr)
        {
          continue;
        }
        if (ch == 0)
        {
          end = true;
          break;
        }
        if (ch < 0x20 || ch > 0x7E)
        {
          return {};
        }
        out.push_back(ch);
      }
      if (end)
      {
        break;
      }
    }
    return out.size() >= 3 ? out : std::string();
  }

  // Tag classes: sub_823FCAC8 (TiP supportPinataTagClassify, identical code)
  // binary-searches 39 {class, boundary} pairs at 0x826F7888. A tag below
  // boundary[i] (and above boundary[i-1]) has class[i]; a boundary itself
  // and tag 1 are "unknown" (39).
  struct TagRange
  {
    uint32_t first;
    uint32_t last;
    uint32_t cls;
  };

  inline constexpr uint32_t kTagClassTable = 0x826F7888u;
  inline constexpr int kTagClassEntries = 39;
  inline constexpr uint32_t kTagClassCount = 38;

  // supportPinataTagClass_e from ReTiP's Types/VivaTags.h, without TiP's three
  // ZZ* classes (VP1 returns 39 for "unknown" where TiP returns 42).
  inline constexpr const char *kTagClassNames[kTagClassCount] = {
      "Animal",     "Camera",     "Helper",       "Background", "Accessory",  "BifPlant",      "BifTree",
      "Bud",        "CameraTarget", "Contract",   "Crate",      "Cursor",     "Egg",           "Fence",
      "Fertiliser", "FertiliserPile", "FlowerHead", "Fruit",    "Home",       "HouseBlock",    "Journal",
      "LifeSweet",  "Money",      "Packet",       "Paving",     "Produce",    "Projectile",    "Prop",
      "Seed",       "SeedHole",   "ShopKeeper",   "SlotMachine", "Spade",     "SpadePart",     "Surface",
      "Sweet",      "Vegetable",  "WateringCan",
  };

  inline const char *TagClassName(uint32_t cls)
  {
    return cls < kTagClassCount ? kTagClassNames[cls] : "?";
  }

  inline std::vector<TagRange> ReadTagRanges()
  {
    std::vector<TagRange> ranges;
    uint32_t first = 0;
    for (int i = 0; i < kTagClassEntries; ++i)
    {
      int32_t cls = 0;
      int32_t boundary = 0;
      if (!MemoryScanner::Read32(kTagClassTable + 8 * i, cls) ||
          !MemoryScanner::Read32(kTagClassTable + 8 * i + 4, boundary) || boundary < 0 ||
          static_cast<uint32_t>(boundary) < first || static_cast<uint32_t>(cls) >= kTagClassCount)
      {
        return {};  // not the table we expect
      }
      if (static_cast<uint32_t>(boundary) > first)
      {
        ranges.push_back({first, static_cast<uint32_t>(boundary) - 1, static_cast<uint32_t>(cls)});
      }
      first = static_cast<uint32_t>(boundary) + 1;
    }
    return ranges;
  }

  inline int ClassOfTag(const std::vector<TagRange> &ranges, uint32_t tag)
  {
    for (const TagRange &r : ranges)
    {
      if (tag >= r.first && tag <= r.last)
      {
        return static_cast<int>(r.cls);
      }
    }
    return -1;
  }

  class ToolsDialog : public rex::ui::ImGuiDialog
  {
  public:
    ToolsDialog(rex::ui::ImGuiDrawer *drawer, rex::ui::Window *window)
        : rex::ui::ImGuiDialog(drawer), window_(window)
    {
    }

    ~ToolsDialog() override
    {
      g_menu_open = false;
      WriteWatch::Get().Disarm();
    }

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
      ImGui::SetNextWindowSize(ImVec2(760, 560), ImGuiCond_FirstUseEver);
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
          if (ImGui::BeginTabItem(t.tab_spawn))
          {
            DrawSpawn(t);
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

    // Tick intervals, oldest first; a flat line at 33 ms is a smooth 30 per second.
    static void DrawTickGraph(const ToolsText &t)
    {
      std::array<float, TickTimes::kSize> values{};
      uint64_t count, slow;
      float worst;
      {
        std::lock_guard lock(Mutex());
        const TickTimes &tt = Ticks();
        count = tt.count;
        slow = tt.slow;
        worst = tt.worst_ms;
        const size_t n = std::min<uint64_t>(count, TickTimes::kSize);
        for (size_t i = 0; i < n; ++i)
        {
          values[TickTimes::kSize - n + i] = tt.interval_ms[(count - n + i) % TickTimes::kSize];
        }
      }
      ImGui::PlotLines("##ticks", values.data(), static_cast<int>(values.size()), 0, t.tick_graph, 0.0f, 100.0f,
                       ImVec2(-1, 70));
      ImGui::Text("%s: %llu / %llu  (%s %.0f ms)", t.slow_ticks, static_cast<unsigned long long>(slow),
                  static_cast<unsigned long long>(count), t.worst, worst);
      ImGui::SameLine();
      if (ImGui::SmallButton(t.reset))
      {
        std::lock_guard lock(Mutex());
        TickTimes &tt = Ticks();
        tt.slow = 0;
        tt.worst_ms = 0.0f;
        tt.count = 0;
      }
    }

    static void DrawScene(const ToolsText &t, uint32_t scene)
    {
      if (!scene)
      {
        ImGui::Text("%s: %s", t.scene, t.scene_none);
        return;
      }
      const std::string name = GuestText(scene);
      ImGui::Text("%s: 0x%08X  %s", t.scene, scene, name.c_str());
    }

    static void DrawCursor(const ToolsText &t, const CursorState &c)
    {
      if (!c.calls)
      {
        ImGui::Text("%s: %s", t.cursor, t.cursor_none);
        return;
      }
      ImGui::Text("%s: (%.1f, %.1f, %.1f)  [pos 0x%08X  rot 0x%08X  camera 0x%08X]", t.cursor, c.pos[0], c.pos[1],
                  c.pos[2], c.pos_ptr, c.rot_ptr, c.camera);
    }

    void DrawGarden(const ToolsText &t)
    {
      FnTrace tick, credits;
      SpawnLog spawns;
      CursorState cursor;
      {
        std::lock_guard lock(Mutex());
        tick = Traces()[kTraceTick];
        credits = Traces()[kTraceCredits];
        spawns = Spawns();
        cursor = Cursor();
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
      DrawTickGraph(t);
      DrawScene(t, CurrentGardenScene());
      DrawCursor(t, cursor);

      if (spawns.count)
      {
        const SpawnCall &s = spawns.ring[(spawns.count - 1) % spawns.ring.size()];
        ImGui::Text("%s: tag %u (%s)  pos (%.1f, %.1f, %.1f)  scale %.2f  age %.2f  -> 0x%08X", t.last_spawn, s.tag,
                    TagClassName(static_cast<uint32_t>(ClassOfTag(TagRanges(), s.tag))), s.pos[0], s.pos[1],
                    s.pos[2], s.scale, s.age, s.result);
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

    // --- Spawn ---------------------------------------------------------------------

    const std::vector<TagRange> &TagRanges()
    {
      if (tag_ranges_.empty() && Clock::now() >= tag_retry_)
      {
        tag_ranges_ = ReadTagRanges();
        tag_retry_ = Clock::now() + std::chrono::seconds(2);
      }
      return tag_ranges_;
    }

    void DrawSpawn(const ToolsText &t)
    {
      SpawnLog spawns;
      CursorState cursor;
      {
        std::lock_guard lock(Mutex());
        spawns = Spawns();
        cursor = Cursor();
      }
      ImGui::TextWrapped("%s", t.spawn_help);
      const uint32_t scene = CurrentGardenScene();
      DrawScene(t, scene);
      DrawCursor(t, cursor);
      ImGui::Separator();

      const std::vector<TagRange> &ranges = TagRanges();
      if (ranges.empty())
      {
        ImGui::TextDisabled("%s", t.tag_table_missing);
      }
      else
      {
        ImGui::SetNextItemWidth(260);
        const std::string preview = fmt::format("{}: {}", spawn_class_, TagClassName(spawn_class_));
        if (ImGui::BeginCombo(t.tag_class, preview.c_str()))
        {
          for (uint32_t cls = 0; cls < kTagClassCount; ++cls)
          {
            uint32_t tags = 0;
            for (const TagRange &r : ranges)
            {
              tags += r.cls == cls ? r.last - r.first + 1 : 0;
            }
            if (!tags)
            {
              continue;
            }
            const std::string label = fmt::format("{}: {} ({})", cls, TagClassName(cls), tags);
            if (ImGui::Selectable(label.c_str(), cls == spawn_class_))
            {
              spawn_class_ = cls;
              for (const TagRange &r : ranges)
              {
                if (r.cls == cls)
                {
                  spawn_tag_ = static_cast<int>(r.first);
                  break;
                }
              }
            }
          }
          ImGui::EndCombo();
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered())
        {
          ImGui::SetTooltip("%s", t.class_names_note);
        }
        std::string list;
        for (const TagRange &r : ranges)
        {
          if (r.cls == spawn_class_)
          {
            list += r.first == r.last ? fmt::format("{}  ", r.first) : fmt::format("{}-{}  ", r.first, r.last);
          }
        }
        ImGui::Text("%s: %s", t.tag_ranges, list.c_str());
      }

      ImGui::SetNextItemWidth(160);
      ImGui::InputInt(t.tag, &spawn_tag_, 1, 10);
      spawn_tag_ = std::max(spawn_tag_, 0);
      if (!ranges.empty())
      {
        ImGui::SameLine();
        const int cls = ClassOfTag(ranges, static_cast<uint32_t>(spawn_tag_));
        ImGui::TextDisabled("%s", cls >= 0 ? TagClassName(static_cast<uint32_t>(cls)) : "?");
      }
      ImGui::SetNextItemWidth(220);
      ImGui::SliderFloat(t.scale, &spawn_scale_, 0.1f, 5.0f, "%.2f");
      ImGui::SetNextItemWidth(220);
      ImGui::SliderFloat(t.age, &spawn_age_, 0.0f, 1.0f, "%.2f");
      ImGui::SetNextItemWidth(220);
      ImGui::SliderInt(t.count, &spawn_count_, 1, 10);
      ImGui::SetNextItemWidth(120);
      ImGui::InputInt("r10", &spawn_r10_, 1, 1);
      ImGui::Checkbox(t.at_cursor, &spawn_at_cursor_);
      if (!spawn_at_cursor_)
      {
        ImGui::SetNextItemWidth(300);
        ImGui::InputFloat3(t.position, spawn_pos_, "%.1f");
      }

      ImGui::BeginDisabled(!scene || (spawn_at_cursor_ && !cursor.calls));
      if (ImGui::Button(t.spawn, ImVec2(160, 0)))
      {
        const float *center = spawn_at_cursor_ ? cursor.pos : spawn_pos_;
        std::lock_guard lock(Mutex());
        for (int i = 0; i < spawn_count_; ++i)
        {
          SpawnRequest r;
          r.scene = scene;
          r.tag = static_cast<uint32_t>(spawn_tag_);
          // Several at once: on a small circle around the centre.
          const float angle = 6.2831853f * static_cast<float>(i) / static_cast<float>(spawn_count_);
          const float radius = spawn_count_ > 1 ? 15.0f : 0.0f;
          r.pos[0] = center[0] + radius * std::cos(angle);
          r.pos[1] = center[1];
          r.pos[2] = center[2] + radius * std::sin(angle);
          r.scale = spawn_scale_;
          r.age = spawn_age_;
          r.r10 = static_cast<uint32_t>(spawn_r10_);
          SpawnQueue().push_back(r);
        }
        spawn_feedback_until_ = Clock::now() + std::chrono::seconds(2);
      }
      ImGui::EndDisabled();
      ImGui::SameLine();
      if (!scene)
      {
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.3f, 1.0f), "%s", t.spawn_no_scene);
      }
      else if (Clock::now() < spawn_feedback_until_)
      {
        ImGui::TextDisabled("%s", t.spawn_queued);
      }

      ImGui::Separator();
      ImGui::TextUnformatted(t.recent_spawns);
      const uint64_t shown = std::min<uint64_t>(spawns.count, 8);
      for (uint64_t i = 0; i < shown; ++i)
      {
        const SpawnCall &s = spawns.ring[(spawns.count - 1 - i) % spawns.ring.size()];
        const char *who = s.caller == kToolsCaller ? t.by_tools : "";
        ImGui::Text("#%llu  tag %u (%s)  (%.1f, %.1f, %.1f)  -> 0x%08X  %s", static_cast<unsigned long long>(s.index),
                    s.tag, TagClassName(static_cast<uint32_t>(ClassOfTag(ranges, s.tag))), s.pos[0], s.pos[1],
                    s.pos[2], s.result, who);
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
                                   ImVec2(0, 140)))
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
        ImGui::SameLine(220);
        ImGui::SetNextItemWidth(110);
        ImGui::InputInt("##new", &w.new_value, 0, 0);
        ImGui::SameLine();
        if (ImGui::Button(t.write))
        {
          watch_error_ = MemoryScanner::Write32(w.addr, w.new_value) ? std::string() : t.write_failed;
        }
        ImGui::SameLine();
        if (ImGui::Button(t.who_writes))
        {
          watch_error_ = WriteWatch::Get().Arm(w.addr) ? std::string() : t.watch_failed;
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
      DrawWriteWatch(t);
    }

    void DrawWriteWatch(const ToolsText &t)
    {
      WriteWatch &ww = WriteWatch::Get();
      if (!ww.armed())
      {
        return;
      }
      ImGui::Separator();
      ImGui::Text("%s 0x%08X (%d %s)", t.watching, ww.address(), ww.threads(), t.threads);
      ImGui::SameLine();
      if (ImGui::SmallButton(t.stop))
      {
        ww.Disarm();
        return;
      }
      ImGui::TextDisabled("%s", t.watch_help);
      std::vector<WriteWatch::Hit> hits = ww.Hits();
      if (hits.empty())
      {
        ImGui::TextDisabled("%s", t.no_writes);
        return;
      }
      std::sort(hits.begin(), hits.end(), [](const auto &a, const auto &b) { return a.count > b.count; });
      for (const WriteWatch::Hit &h : hits)
      {
        auto it = symbols_.find(h.rip);
        if (it == symbols_.end())
        {
          it = symbols_.emplace(h.rip, WriteWatch::Describe(h.rip)).first;
        }
        ImGui::Text("%8llu  %s", static_cast<unsigned long long>(h.count), it->second.c_str());
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
          ImGui::Text("%u %s", s.tag, TagClassName(static_cast<uint32_t>(ClassOfTag(TagRanges(), s.tag))));
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
    std::string Report(const std::array<FnTrace, kTraceCount> &traces, const SpawnLog &spawns)
    {
      CursorState cursor;
      {
        std::lock_guard lock(Mutex());
        cursor = Cursor();
      }
      const uint32_t scene = CurrentGardenScene();
      std::string out = "VP Tools trace report\n";
      {
        std::lock_guard lock(Mutex());
        const TickTimes &tt = Ticks();
        out += fmt::format("ticks: {} measured, slow (>50 ms) {}, worst {:.0f} ms\n", tt.count, tt.slow, tt.worst_ms);
      }
      out += fmt::format("garden scene (slot table) {:08X} \"{}\"; cursor calls={} pos=({:.1f}, {:.1f}, {:.1f}) "
                         "pos_ptr={:08X} rot_ptr={:08X} camera={:08X}\n",
                         scene, GuestText(scene), cursor.calls, cursor.pos[0], cursor.pos[1], cursor.pos[2],
                         cursor.pos_ptr, cursor.rot_ptr, cursor.camera);
      for (const FnTrace &f : traces)
      {
        out += fmt::format("{} ({}, {}): calls={} caller={:08X} r3={:08X} r4={:08X} r5={:08X} r6={:08X} "
                           "-> {:08X} extra={} {} {}\n",
                           f.name, f.symbol, f.grade, f.calls, f.last_lr, f.last_args[0], f.last_args[1],
                           f.last_args[2], f.last_args[3], f.last_result, static_cast<int32_t>(f.extra[0]),
                           static_cast<int32_t>(f.extra[1]), static_cast<int32_t>(f.extra[2]));
      }
      const std::vector<TagRange> &ranges = TagRanges();
      out += fmt::format("tag classes ({} ranges):", ranges.size());
      for (const TagRange &r : ranges)
      {
        out += fmt::format(" {}-{}:{}", r.first, r.last, r.cls);
      }
      out += "\n";
      out += fmt::format("spawns: {} total, last scene {:08X}\n", spawns.count, spawns.last_scene);
      const uint64_t shown = std::min<uint64_t>(spawns.count, spawns.ring.size());
      for (uint64_t i = 0; i < shown; ++i)
      {
        const SpawnCall &s = spawns.ring[(spawns.count - 1 - i) % spawns.ring.size()];
        out += fmt::format("  #{} caller={:08X} scene={:08X} tag={} ({}) pos=({:.1f}, {:.1f}, {:.1f}) rot={:08X} "
                           "scale={:.2f} age={:.2f} r9={} r10={} -> {:08X}\n",
                           s.index, s.caller, s.scene, s.tag, TagClassName(static_cast<uint32_t>(ClassOfTag(ranges, s.tag))),
                           s.pos[0], s.pos[1], s.pos[2], s.rot_ptr, s.scale, s.age, s.r9, s.r10, s.result);
      }
      WriteWatch &ww = WriteWatch::Get();
      if (ww.armed())
      {
        out += fmt::format("write watch {:08X} ({} threads):\n", ww.address(), ww.threads());
        for (const WriteWatch::Hit &h : ww.Hits())
        {
          auto it = symbols_.find(h.rip);
          if (it == symbols_.end())
          {
            it = symbols_.emplace(h.rip, WriteWatch::Describe(h.rip)).first;
          }
          out += fmt::format("  {} x{}\n", it->second, h.count);
        }
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

    std::vector<TagRange> tag_ranges_;
    Clock::time_point tag_retry_{};
    uint32_t spawn_class_ = 0;
    int spawn_tag_ = 54;
    float spawn_scale_ = 1.0f;
    float spawn_age_ = 1.0f;
    int spawn_count_ = 1;
    int spawn_r10_ = 0;
    bool spawn_at_cursor_ = true;
    float spawn_pos_[3] = {};
    Clock::time_point spawn_feedback_until_{};

    MemoryScanner scanner_;
    int scan_value_ = 0;
    uint32_t add_address_ = 0;
    std::vector<Watch> watches_;
    std::string watch_error_;
    std::map<uint64_t, std::string> symbols_;
  };
}  // namespace vp_tools
