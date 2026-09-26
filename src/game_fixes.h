// game_fixes.h - ALL guest-function overrides and hooks for Viva Pinata
//
// Included exactly once, from src/main.cpp, after the generated init header.
// Every definition here is a STRONG `extern "C"` symbol that replaces the
// weak alias codegen emits for the same name (DEFINE_REX_FUNC in
// generated/default/vivapinata_pch.h):
//
//   __imp__sub_82XXXXXX  strong, the recompiled body   (always callable)
//   sub_82XXXXXX         weak alias -> __imp__          (what call sites and the
//                                                       dispatch table reference)
//
// A strong sub_82XXXXXX in main.obj wins at link time; generated/ is never
// touched. Recipes (all from <rex/hook.h>):
//
//   (a) wrapper      REX_HOOK_RAW(sub_82XXXXXX) {
//                      if (ctx.lr == 0x82YYYYYY) { ctx.r3.s64 = 0; return; }
//                      __imp__sub_82XXXXXX(ctx, base);
//                    }
//   (b) replacement  copy the body from generated/default/vivapinata_recomp.N.cpp,
//                    keep REX_FUNC_PROLOGUE() and the loc_ labels, mark every
//                    changed line with  // [VivaPinata Fix]
//   (c) typed hook   REX_HOOK(sub_82XXXXXX, NativeFn)   r3..r10 -> args, ret -> r3/f1
//   (d) stubs        REX_STUB(sub_82XXXXXX) / REX_STUB_RETURN(sub_82XXXXXX, 0)
//   (e) call-in      REX_IMPORT(__imp__sub_82XXXXXX, g_fn, u32(u32, u32));
//   (f) mid-asm      bool name() / void name(PPCRegister& rN)  for [[midasm_hook]]
//                    entries in config/vivapinata_midasm.toml
//
// One override per guest function (duplicate strong symbols = link error).
// Named functions (config/vivapinata_hooks.toml) appear as rex_<name> instead
// of sub_82XXXXXX; the __imp__ prefix rule is the same.
#pragma once

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include "generated/default/vivapinata_pch.h"
#include "generated/default/vivapinata_funcs.h"

#include <rex/hook.h>

#include <cstdint>
#include <cstring>

#include "game_constants.h"
#include "game_cvars.h"
#include "game_timing.h"

// ---------------------------------------------------------------------------
// 0. Status
// ---------------------------------------------------------------------------
// Guest overrides so far: section 1 (vpkd3d128 codegen workaround).
// Every address must come from generated/default/*_recomp.N.cpp or from a
// runtime log, never from TiP-Recomp (different XEX).
//
// Expected first candidates (order in which TiP-Recomp needed them):
//   1. XUsbcam* (Xbox Live Vision camera, xam.xex imports) - if the SDK's XAM
//      layer reports them as unimplemented, stub the game-side thunks here.
//      TiP-Recomp (SDK 0.8.1): REX_STUB(__imp__XUsbcamCreate) etc. In SDK 0.10
//      unimplemented exports resolve at runtime through the ordinal registry,
//      so only add stubs if the log shows a hard failure.
//   2. Intro movie skip (vp_skip_intro_videos) - midasm jump hooks.
//   3. Presentation interval / vsync (vp_fps_unlock).
//   4. Sleep() thunk -> vp_timing::PreciseSleep (hybrid sleep) if cores peg.
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// 1. vpkd3d128 FLOAT16_4 in-place sign fix (white garden ground)
// ---------------------------------------------------------------------------
// SDK codegen (src/codegen/builders/vector.cpp, vpkd3d128 case 5) packs lane by
// lane: it writes vD.u16[7] and only then ORs in the sign read from vB.u32[3].
// When vD == vB, u16[7] is the upper half of u32[3], so the sign of guest
// lane x is read back after it was overwritten and every negative half comes
// out positive. The game's float->half idiom
//   lvlx v0 / vpkd3d128 v0,v0,5,2,2 / vsplth v0,v0,0 / stvehx
// always packs in place and splats lane x, so all of its negative halves were
// lost. sub_82207E38 builds the terrain patch grid (-256..256) with it; the
// grid folded into the +X+Z quadrant and 3/4 of the garden rendered white.
//
// Every in-place `vpkd3d128 v0,v0,5,2,2` is hooked BEFORE the instruction in
// config/vivapinata_midasm.toml; the hook does the pack with the source
// lanes snapshotted and jumps over the generated code (jump_address = +4).
// Non-in-place forms (vD != vB) are correct and not hooked.

// Same float->half conversion as the generated code (Xenos: no inf, NaN and
// overflow saturate to 0x7FFF, small values denormalize), sign included.
static inline uint16_t VpF32ToF16Xenos(uint32_t bits) {
  const uint32_t abs_bits = bits & 0x7FFFFFFF;
  float f;
  std::memcpy(&f, &abs_bits, sizeof(f));
  const uint8_t e = (f != f || f > 65504.0f) ? 0xFF : uint8_t((bits & 0x7F800000) >> 23);
  const uint16_t m = e != 0xFF ? uint16_t((bits & 0x7FE000) >> 13) : 0;
  const uint16_t h =
      e != 0xFF ? (e > 0x70 ? uint16_t(((e - 0x70) << 10) + m)
                            : (0x71 - e > 31 ? uint16_t(0) : uint16_t((0x400 + m) >> (0x71 - e))))
                : uint16_t(0x7FFF);
  return uint16_t(h | ((bits & 0x80000000) >> 16));
}

// [[midasm_hook]] VpFixVpkd3d128Float16x4, registers = ["v0", "fpscr"].
// Guest words 0..3 (x,y,z,w) are host u32[3..0]; the packed halves go to
// guest halfwords 0..3 = host u16[7..4]. Guest words 2,3 (host u32[1..0])
// keep the source floats, exactly as the generated code leaves them.
void VpFixVpkd3d128Float16x4(PPCVRegister& v0, PPCFPSCRRegister& fpscr) {
  fpscr.enableFlushMode();
  const uint32_t x = v0.u32[3], y = v0.u32[2], z = v0.u32[1], w = v0.u32[0];
  v0.u16[7] = VpF32ToF16Xenos(x);
  v0.u16[6] = VpF32ToF16Xenos(y);
  v0.u16[5] = VpF32ToF16Xenos(z);
  v0.u16[4] = VpF32ToF16Xenos(w);
}
