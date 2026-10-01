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

  struct SpawnCall
  {
    uint32_t caller = 0;   // LR: guest address after the calling bl
    uint32_t scene = 0;    // r3
    uint32_t pos_ptr = 0;  // r4 -> 3 floats
    uint32_t rot_ptr = 0;  // r5
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
    kTraceGardenScene,
    kTraceAvatarPos,
    kTraceCredits,
    kTraceCount
  };

  inline std::array<FnTrace, kTraceCount> &Traces()
  {
    static std::array<FnTrace, kTraceCount> t = {{
        {"appMainTickPreDraw", "sub_82105528", "high"},
        {"requirementsMet", "sub_823B09A8", "high"},
        {"gardenMainGetGardenScene?", "sub_82106ED0", "low"},
        {"avatarPosGet?", "sub_82171680", "medium"},
        {"playerMainUpdateHighestAndLowestCredits?", "sub_82429D50", "low"},
    }};
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
