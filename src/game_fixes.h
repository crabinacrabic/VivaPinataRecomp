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

#include "game_constants.h"
#include "game_cvars.h"
#include "game_timing.h"

// ---------------------------------------------------------------------------
// 0. Status
// ---------------------------------------------------------------------------
// First codegen: no guest overrides. Nothing is known about this XEX beyond
// its header; every address must come from generated/default/*_recomp.N.cpp
// or from a runtime log, never from TiP-Recomp (different XEX).
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
