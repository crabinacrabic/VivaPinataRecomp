// vp_tools/hooks.h - guest-function hooks of VP Tools
//
// Included once, from src/main.cpp, after game_fixes.h (strong extern "C"
// sub_X symbols replace the weak generated aliases; see game_fixes.h).
//
// Every hook records its arguments and calls the original body, so the game
// behaves as before. The records feed the menu (Garden, Trace tabs) and, with
// --vp_tools_trace=true, the log. One hook also acts: appMainTickPreDraw
// carries out the spawn requests queued by the menu, on the game thread.
//
// Matches checked in game (2026-10-01 trace): appMainTickPreDraw ~30 calls/s,
// supportPinataCreateGeneralEx with TiP's arguments. Ruled out: sub_82106ED0
// and sub_82171680 (gardenMainGetGardenScene / avatarPosGet by code only).
#pragma once

#include <algorithm>
#include <chrono>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>

#include "game_cvars.h"
#include "vp_names.h"
#include "vp_tools/memory_scan.h"
#include "vp_tools/state.h"

// Found by hand (not graded by tools/port_names.py):
//   cursorCameraTick       same call sequence as TiP 0x822C1E88 (meCursorCam*
//                          calls, then the same four matched functions);
//                          (camera r3, controls r4, pos r5, rot r6)
//   gardenMainGetGardenScene(id)  the garden-slot lookup; its only caller
//                          passes id 1, TiP's version hard-codes 1
//   credits candidate      code only, verify (TiP playerMain +4/+16/+20)
#define VP_FN_cursorCameraTick sub_821DF590
#define VP_IMP_cursorCameraTick __imp__sub_821DF590
#define VP_FN_gardenMainGetGardenSceneById sub_82106E40
#define VP_IMP_gardenMainGetGardenSceneById __imp__sub_82106E40
#define VP_CAND_playerMainUpdateHighestAndLowestCredits sub_82429D50
#define VP_CAND_IMP_playerMainUpdateHighestAndLowestCredits __imp__sub_82429D50

namespace vp_tools
{
  inline constexpr uint64_t kLogFirstCalls = 8;

  // Arguments as they were on entry; the original body clobbers r3..r6 and lr.
  struct Entry
  {
    uint32_t lr;
    uint32_t args[4];  // r3..r6
  };

  inline Entry OnEntry(const PPCContext &ctx)
  {
    return {static_cast<uint32_t>(ctx.lr), {ctx.r3.u32, ctx.r4.u32, ctx.r5.u32, ctx.r6.u32}};
  }

  // Guest reads in hooks go through VirtualQuery: a wrong candidate may pass
  // something that is not a pointer, and a fault here would end the game.
  inline uint32_t SafeLoad32(uint32_t addr)
  {
    int32_t v = 0;
    MemoryScanner::Read32(addr, v);
    return static_cast<uint32_t>(v);
  }

  inline float SafeLoadFloat(uint32_t addr)
  {
    const uint32_t v = SafeLoad32(addr);
    float f;
    std::memcpy(&f, &v, sizeof(f));
    return f;
  }

  inline void StoreBE32(uint8_t *base, uint32_t addr, uint32_t value)
  {
    const uint32_t v = __builtin_bswap32(value);
    std::memcpy(base + addr, &v, sizeof(v));
  }

  inline void StoreBEFloat(uint8_t *base, uint32_t addr, float value)
  {
    uint32_t v;
    std::memcpy(&v, &value, sizeof(v));
    StoreBE32(base, addr, v);
  }

  inline void RecordCall(TraceId id, const Entry &entry, uint32_t result, const uint32_t *extra = nullptr)
  {
    uint64_t n;
    {
      std::lock_guard lock(Mutex());
      FnTrace &t = Traces()[id];
      n = ++t.calls;
      t.last_lr = entry.lr;
      std::memcpy(t.last_args, entry.args, sizeof(t.last_args));
      t.last_result = result;
      if (extra)
      {
        std::memcpy(t.extra, extra, sizeof(t.extra));
      }
    }
    if (n <= kLogFirstCalls && REXCVAR_GET(vp_tools_trace))
    {
      const FnTrace &t = Traces()[id];
      REXLOG_INFO("[vp_tools] {} ({}) call {} from 0x{:08X}: r3=0x{:08X} r4=0x{:08X} r5=0x{:08X} "
                  "-> 0x{:08X} extra {:08X} {:08X} {:08X}",
                  t.name, t.symbol, n, entry.lr, entry.args[0], entry.args[1], entry.args[2], result,
                  extra ? extra[0] : 0, extra ? extra[1] : 0, extra ? extra[2] : 0);
    }
  }

  inline void ServiceSpawnRequests(PPCContext &ctx, uint8_t *base);
}  // namespace vp_tools

// supportPinataCreateGeneralEx(scene r3, pos r4, rot r5, tag r7, r9, r10,
// scale f1, age f2) -> entity. Same arguments as in TiP (checked by comparing
// the code of both games); r6 and r8 are not inputs (overwritten on entry).
REX_HOOK_RAW(VP_FN_supportPinataCreateGeneralEx)
{
  vp_tools::SpawnCall call;
  call.caller = static_cast<uint32_t>(ctx.lr);
  call.scene = ctx.r3.u32;
  call.pos_ptr = ctx.r4.u32;
  call.rot_ptr = ctx.r5.u32;
  call.tag = ctx.r7.u32;
  call.r9 = ctx.r9.u32;
  call.r10 = ctx.r10.u32;
  call.scale = ctx.f1.f64;
  call.age = ctx.f2.f64;
  if (vp_tools::PlausibleGuestPointer(call.pos_ptr))
  {
    for (int i = 0; i < 3; ++i)
    {
      call.pos[i] = vp_tools::SafeLoadFloat(call.pos_ptr + 4 * i);
    }
  }

  VP_IMP_supportPinataCreateGeneralEx(ctx, base);
  call.result = ctx.r3.u32;

  uint64_t n;
  {
    std::lock_guard lock(vp_tools::Mutex());
    vp_tools::SpawnLog &log = vp_tools::Spawns();
    call.index = ++log.count;
    log.ring[(call.index - 1) % log.ring.size()] = call;
    if (call.scene)
    {
      log.last_scene = call.scene;
    }
    n = log.count;
  }
  if ((n <= 20 || call.caller == vp_tools::kToolsCaller) && REXCVAR_GET(vp_tools_trace))
  {
    REXLOG_INFO("[vp_tools] supportPinataCreateGeneralEx call {} from 0x{:08X}: scene=0x{:08X} "
                "pos=({:.1f}, {:.1f}, {:.1f}) rot=0x{:08X} tag={} r9={} r10={} scale={:.2f} age={:.2f} "
                "-> 0x{:08X}",
                n, call.caller, call.scene, call.pos[0], call.pos[1], call.pos[2], call.rot_ptr, call.tag,
                call.r9, call.r10, call.scale, call.age, call.result);
  }
}

// Once per game frame, on the game thread: the safe point for menu requests.
REX_HOOK_RAW(VP_FN_appMainTickPreDraw)
{
  const vp_tools::Entry entry = vp_tools::OnEntry(ctx);
  {
    const auto now = std::chrono::steady_clock::now();
    std::lock_guard lock(vp_tools::Mutex());
    vp_tools::TickTimes &t = vp_tools::Ticks();
    if (t.last != std::chrono::steady_clock::time_point{})
    {
      const float ms = std::chrono::duration<float, std::milli>(now - t.last).count();
      t.interval_ms[t.count % t.interval_ms.size()] = ms;
      ++t.count;
      if (ms > vp_tools::TickTimes::kSlowMs)
      {
        ++t.slow;
      }
      t.worst_ms = std::max(t.worst_ms, ms);
    }
    t.last = now;
  }
  VP_IMP_appMainTickPreDraw(ctx, base);
  vp_tools::RecordCall(vp_tools::kTraceTick, entry, ctx.r3.u32);
  vp_tools::ServiceSpawnRequests(ctx, base);
}

REX_HOOK_RAW(VP_FN_requirementsMet)
{
  const vp_tools::Entry entry = vp_tools::OnEntry(ctx);
  VP_IMP_requirementsMet(ctx, base);
  vp_tools::RecordCall(vp_tools::kTraceRequirements, entry, ctx.r3.u32);
}

// Keeps the cursor position (r5 -> 3 floats) and rotation pointer (r6).
REX_HOOK_RAW(VP_FN_cursorCameraTick)
{
  const vp_tools::Entry entry = vp_tools::OnEntry(ctx);
  float pos[3] = {};
  if (vp_tools::PlausibleGuestPointer(entry.args[2]))
  {
    for (int i = 0; i < 3; ++i)
    {
      pos[i] = vp_tools::SafeLoadFloat(entry.args[2] + 4 * i);
    }
  }
  VP_IMP_cursorCameraTick(ctx, base);
  {
    std::lock_guard lock(vp_tools::Mutex());
    vp_tools::CursorState &c = vp_tools::Cursor();
    ++c.calls;
    c.camera = entry.args[0];
    c.pos_ptr = entry.args[2];
    c.rot_ptr = entry.args[3];
    std::memcpy(c.pos, pos, sizeof(pos));
  }
  vp_tools::RecordCall(vp_tools::kTraceCursorCam, entry, ctx.r3.u32);
}

REX_HOOK_RAW(VP_FN_gardenMainGetGardenSceneById)
{
  const vp_tools::Entry entry = vp_tools::OnEntry(ctx);
  VP_IMP_gardenMainGetGardenSceneById(ctx, base);
  vp_tools::RecordCall(vp_tools::kTraceGardenScene, entry, ctx.r3.u32);
}

// TiP: playerMain in r3, credits at +4, experience at +16, level at +20.
REX_HOOK_RAW(VP_CAND_playerMainUpdateHighestAndLowestCredits)
{
  const vp_tools::Entry entry = vp_tools::OnEntry(ctx);
  uint32_t extra[3] = {};
  if (vp_tools::PlausibleGuestPointer(entry.args[0]))
  {
    extra[0] = vp_tools::SafeLoad32(entry.args[0] + 4);
    extra[1] = vp_tools::SafeLoad32(entry.args[0] + 16);
    extra[2] = vp_tools::SafeLoad32(entry.args[0] + 20);
  }
  VP_CAND_IMP_playerMainUpdateHighestAndLowestCredits(ctx, base);
  vp_tools::RecordCall(vp_tools::kTraceCredits, entry, ctx.r3.u32, extra);
}

namespace vp_tools
{
  // Calls supportPinataCreateGeneralEx for each queued request, from the
  // appMainTickPreDraw hook (game thread, after the tick, before the draw).
  // The position goes into a 0x200-byte block carved below the current guest
  // stack pointer, so the callee's frame lands below it; the whole context is
  // restored afterwards. ReTiP does the same from its gardenMainGetGardenScene
  // hook (saves the context, sets r3/r4/r5/r7/r9/r10/f1/f2, restores).
  inline void ServiceSpawnRequests(PPCContext &ctx, uint8_t *base)
  {
    std::vector<SpawnRequest> requests;
    {
      std::lock_guard lock(Mutex());
      if (SpawnQueue().empty())
      {
        return;
      }
      requests.swap(SpawnQueue());
    }
    for (const SpawnRequest &r : requests)
    {
      if (!PlausibleGuestPointer(r.scene))
      {
        continue;
      }
      const PPCContext saved = ctx;
      const uint32_t sp = (saved.r1.u32 - 0x200u) & ~0xFu;
      const uint32_t pos_addr = sp + 0x180u;
      StoreBE32(base, sp, saved.r1.u32);  // back chain
      for (int i = 0; i < 3; ++i)
      {
        StoreBEFloat(base, pos_addr + 4 * i, r.pos[i]);
      }
      ctx.r1.u64 = sp;
      ctx.lr = kToolsCaller;
      ctx.r3.u64 = r.scene;
      ctx.r4.u64 = pos_addr;
      ctx.r5.u64 = 0;  // no rotation (the game passes 0 itself)
      ctx.r7.u64 = r.tag;
      ctx.r9.u64 = 0;
      ctx.r10.u64 = r.r10;
      ctx.f1.f64 = r.scale;
      ctx.f2.f64 = r.age;
      VP_FN_supportPinataCreateGeneralEx(ctx, base);  // through the hook: shows up in the spawn log
      ctx = saved;
    }
  }
}  // namespace vp_tools
