// game_cvars.h - project CVar definitions (vp_* namespace)
//
// Registered at static-init time, so they load on the first config pass in
// OnConfigurePaths. Flat `key = value` in settings/hardware.toml, or
// --key=value on the command line / REX_KEY=value in the environment.
#pragma once

#include <rex/cvar.h>

REXCVAR_DEFINE_STRING(graphics_backend, "any", "GPU",
                      "Graphics API backend: any, d3d12, vulkan")
    .allowed({"any", "d3d12", "vulkan"});

// Authentic console pacing: Viva Pinata is a 30 FPS title with simulation
// time coupled to the frame (garden ticks). Keep false until the vsync /
// tick-rate path is understood (TiP-Recomp: midasm vsync_hook on r10 +
// lock_fps cvar).
REXCVAR_DEFINE_BOOL(vp_fps_unlock, false, "Gameplay",
                    "Unlock the 30 FPS presentation interval (experimental, garden sim may speed up)");

REXCVAR_DEFINE_BOOL(vp_skip_intro_videos, false, "Gameplay",
                    "Skip the startup logo/intro movies (requires the SkipIntroVideos hook addresses)");

REXCVAR_DEFINE_BOOL(vp_high_res_timer, true, "Performance",
                    "timeBeginPeriod(1) at startup so guest Sleep(1) is not quantised to 15.6 ms");

// src/launcher.h. The launcher writes its own choice to settings/launcher.toml;
// --vp_show_launcher=false skips it for one run.
REXCVAR_DEFINE_BOOL(vp_show_launcher, true, "UI",
                    "Show the launcher (play / settings) before the game starts");

REXCVAR_DEFINE_BOOL(dev_debug_runtime, false, "Debug",
                    "Enable runtime debug tools (stub sweep, missing function scan)");
