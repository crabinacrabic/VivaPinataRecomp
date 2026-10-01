// vp_tools/hooks.h - guest-function hooks of VP Tools
//
// Included once, from src/main.cpp, after game_fixes.h (strong extern "C"
// sub_X symbols replace the weak generated aliases; see game_fixes.h).
//
// Stage 0/1: every hook only records its arguments and calls the original
// body, so the game behaves exactly as before. The records feed the menu's
// Trace tab and, with --vp_tools_trace=true, the log. They verify the
// function matches in docs/NAMES_FROM_TIP.md: "high" matches from
// src/vp_names.h, and a few lower-graded candidates (marked with ?).
#pragma once

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>

#include "game_cvars.h"
#include "vp_names.h"
#include "vp_tools/memory_scan.h"
#include "vp_tools/state.h"

// Candidates graded medium/low in docs/NAMES_FROM_TIP.md: traced only.
#define VP_CAND_gardenMainGetGardenScene sub_82106ED0
#define VP_CAND_avatarPosGet sub_82171680
#define VP_CAND_playerMainUpdateHighestAndLowestCredits sub_82429D50

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
}  // namespace vp_tools

// supportPinataCreateGeneralEx(scene r3, pos r4, rot r5, tag r7, r9, r10,
// scale f1, age f2) -> entity. Same arguments as in TiP (checked by comparing
// the code of both games).
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
  if (n <= 20 && REXCVAR_GET(vp_tools_trace))
  {
    REXLOG_INFO("[vp_tools] supportPinataCreateGeneralEx call {} from 0x{:08X}: scene=0x{:08X} "
                "pos=({:.1f}, {:.1f}, {:.1f}) rot=0x{:08X} tag={} r9={} r10={} scale={:.2f} age={:.2f} "
                "-> 0x{:08X}",
                n, call.caller, call.scene, call.pos[0], call.pos[1], call.pos[2], call.rot_ptr, call.tag,
                call.r9, call.r10, call.scale, call.age, call.result);
  }
}

// Once per game frame: the safe point for menu requests (stage 2).
REX_HOOK_RAW(VP_FN_appMainTickPreDraw)
{
  const vp_tools::Entry entry = vp_tools::OnEntry(ctx);
  VP_IMP_appMainTickPreDraw(ctx, base);
  vp_tools::RecordCall(vp_tools::kTraceTick, entry, ctx.r3.u32);
}

REX_HOOK_RAW(VP_FN_requirementsMet)
{
  const vp_tools::Entry entry = vp_tools::OnEntry(ctx);
  VP_IMP_requirementsMet(ctx, base);
  vp_tools::RecordCall(vp_tools::kTraceRequirements, entry, ctx.r3.u32);
}

REX_HOOK_RAW(VP_CAND_gardenMainGetGardenScene)
{
  const vp_tools::Entry entry = vp_tools::OnEntry(ctx);
  __imp__sub_82106ED0(ctx, base);
  vp_tools::RecordCall(vp_tools::kTraceGardenScene, entry, ctx.r3.u32);
}

REX_HOOK_RAW(VP_CAND_avatarPosGet)
{
  const vp_tools::Entry entry = vp_tools::OnEntry(ctx);
  __imp__sub_82171680(ctx, base);
  vp_tools::RecordCall(vp_tools::kTraceAvatarPos, entry, ctx.r3.u32);
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
  __imp__sub_82429D50(ctx, base);
  vp_tools::RecordCall(vp_tools::kTraceCredits, entry, ctx.r3.u32, extra);
}
