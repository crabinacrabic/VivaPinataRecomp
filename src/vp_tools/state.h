// vp_tools/state.h - state shared by the VP Tools guest hooks and the menu
//
// The hooks (vp_tools/hooks.h) run on guest threads; the menu
// (vp_tools/tools_dialog.h) runs on the UI thread. Everything here is either
// atomic or behind Mutex().
#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <vector>

namespace vp_tools
{
  inline std::mutex &Mutex()
  {
    static std::mutex m;
    return m;
  }

  // Menu open: guest pad input is blocked (VivapinataApp::OnPostSetup).
  inline std::atomic<bool> g_menu_open{false};

  // Cheap filter for values that may be guest pointers (reads still go through
  // MemoryScanner::Read32, which checks the page).
  inline bool PlausibleGuestPointer(uint32_t addr)
  {
    return addr >= 0x00010000u && addr < 0xFFFF0000u;
  }

  // --- spawn trace (supportPinataCreateGeneralEx) -------------------------------

  // Caller recorded for spawns made by the menu (the hook sets lr to it).
  inline constexpr uint32_t kToolsCaller = 0;

  struct SpawnCall
  {
    uint32_t caller = 0;   // LR: guest address after the calling bl
    uint32_t scene = 0;    // r3
    uint32_t pos_ptr = 0;  // r4 -> 3 floats
    uint32_t rot_ptr = 0;  // r5 (0 is allowed: the game passes 0 itself)
    uint32_t tag = 0;      // r7
    uint32_t r9 = 0;
    uint32_t r10 = 0;
    double scale = 0.0;    // f1
    double age = 0.0;      // f2 (TiP: 1 = adult, 0 = baby)
    float pos[3] = {};
    uint32_t result = 0;   // r3 on return: the new entity
    uint64_t index = 0;    // running call number
  };

  struct SpawnLog
  {
    std::array<SpawnCall, 32> ring{};
    uint64_t count = 0;
    uint32_t last_scene = 0;
  };

  inline SpawnLog &Spawns()
  {
    static SpawnLog log;
    return log;
  }

  // --- spawn requests (menu -> game thread) -------------------------------------

  // Queued by the menu, carried out by the appMainTickPreDraw hook on the game
  // thread (ReTiP serviced its spawn request the same way, from a game hook).
  struct SpawnRequest
  {
    uint32_t scene = 0;
    uint32_t tag = 0;
    float pos[3] = {};
    float scale = 1.0f;
    float age = 1.0f;
    uint32_t r10 = 0;
  };

  inline std::vector<SpawnRequest> &SpawnQueue()
  {
    static std::vector<SpawnRequest> queue;
    return queue;
  }

  // --- cursor (cursorCameraTick: camera r3, controls r4, pos r5, rot r6) --------

  struct CursorState
  {
    uint64_t calls = 0;
    uint32_t camera = 0;
    uint32_t pos_ptr = 0;
    uint32_t rot_ptr = 0;
    float pos[3] = {};
  };

  inline CursorState &Cursor()
  {
    static CursorState c;
    return c;
  }

  // --- garden table (read by sub_82106E40, the VP1 gardenMainGetGardenScene) ---

  // Four 200-byte garden slots at 0x82A27CC0 + 140; slot +16 is the garden id
  // (the game asks for id 1, as TiP's gardenMainGetGardenScene does), slot +4
  // the scene. Plain reads, valid from any thread.
  inline constexpr uint32_t kGardenSlots = 0x82A27CC0u + 140u;
  inline constexpr uint32_t kGardenSlotsEnd = 0x82A27CC0u + 940u;
  inline constexpr uint32_t kGardenSlotSize = 200u;

  // --- per-function call counters (verification of docs/NAMES_FROM_TIP.md) -----

  struct FnTrace
  {
    const char *name;
    const char *symbol;
    const char *grade;
    uint64_t calls = 0;
    uint32_t last_lr = 0;
    uint32_t last_args[4] = {};  // r3..r6
    uint32_t last_result = 0;
    uint32_t extra[3] = {};      // function-specific (e.g. fields behind r3)
  };

  enum TraceId
  {
    kTraceTick,
    kTraceRequirements,
    kTraceCursorCam,
    kTraceGardenScene,
    kTraceCredits,
    kTraceCount
  };

  inline std::array<FnTrace, kTraceCount> &Traces()
  {
    static std::array<FnTrace, kTraceCount> t = {{
        {"appMainTickPreDraw", "sub_82105528", "confirmed"},
        {"requirementsMet", "sub_823B09A8", "high"},
        {"cursorCameraTick", "sub_821DF590", "call graph"},
        {"gardenMainGetGardenScene(id)", "sub_82106E40", "code"},
        {"playerMainUpdateHighestAndLowestCredits?", "sub_82429D50", "low"},
    }};
    return t;
  }

  // --- tick timing (stutter measurement) -----------------------------------------

  // Time between appMainTickPreDraw calls, recorded on the game thread. The
  // game targets 30 ticks/s (33 ms); a "slow" tick is one over 50 ms.
  struct TickTimes
  {
    static constexpr size_t kSize = 300;  // 10 s at 30 ticks/s
    static constexpr float kSlowMs = 50.0f;
    std::array<float, kSize> interval_ms{};
    uint64_t count = 0;
    uint64_t slow = 0;
    float worst_ms = 0.0f;
    std::chrono::steady_clock::time_point last{};
  };

  inline TickTimes &Ticks()
  {
    static TickTimes t;
    return t;
  }

  // Game ticks per second, measured on the UI thread from Traces()[kTraceTick].
  struct TickRate
  {
    uint64_t last_calls = 0;
    std::chrono::steady_clock::time_point last_time = std::chrono::steady_clock::now();
    double per_second = 0.0;
  };
}  // namespace vp_tools
