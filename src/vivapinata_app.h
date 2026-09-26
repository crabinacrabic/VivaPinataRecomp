// vivapinata_app.h - ReXApp lifecycle for Viva Pinata (2006, 4D5307F2)
//
// Lifecycle (rex/rex_app.h):
//   SetupEnvironment   OnConfigurePaths -> config TOML -> logging -> OnPostInitLogging
//   SetupPresentation  OnPreSetup(RuntimeConfig&) -> window/ImGui (OnConfigureFonts)
//                      -> OnCreateDialogs
//   OnFinalizePaths    launcher (src/launcher.h); runtime is built after "ИГРАТЬ"
//   ConstructRuntime   OnLoadXexImage -> LoadXexImage -> OnPostLoadXexImage -> OnPostSetup
//   LaunchModule       OnPreLaunchModule -> OnPostLaunchModule(XThread*) -> OnGuestThreadExit
//   OnShutdown
#pragma once

#include <cstdlib>
#include <memory>

#include <rex/cvar.h>
#include <rex/input/flags.h>
#include <rex/logging.h>
#include <rex/memory/utils.h>
#include <rex/rex_app.h>
#include <rex/runtime.h>
#include <rex/system/gpu_plugin.h>

#include "debug_tools.h"
#include "game_cvars.h"
#include "game_timing.h"
#include "launcher.h"
#include "utils.h"

class VivapinataApp : public rex::ReXApp
{
public:
  using rex::ReXApp::ReXApp;

  static std::unique_ptr<rex::ui::WindowedApp> Create(rex::ui::WindowedAppContext &ctx)
  {
    return std::unique_ptr<VivapinataApp>(new VivapinataApp(ctx, "vivapinata", PPCImageConfig));
  }

  void OnConfigurePaths(rex::PathConfig &paths) override
  {
    // --game_data_root / REX_GAME_DATA_ROOT are read before any config file,
    // so if they were given the path is already non-empty here.
    // game_files/ is the extracted disc: default.xex + Beta\ (Rare's data
    // root: bundles_packages, movie, font, xwavebank...). The SDK mounts it
    // as game: and d:, so guest paths like game:\Beta\... resolve directly.
    if (paths.game_data_root.empty())
    {
      paths.game_data_root = utils::RepoRoot() / "game_files";
    }

    // settings/hardware.toml becomes the SDK's primary config file (also the
    // target of the in-game Settings overlay "Save to config").
    paths.config_path = utils::SettingsDir() / "hardware.toml";

    // First pass: window/input cvars exist already. GPU cvars register later
    // and are picked up by the second pass in OnPostSetup.
    utils::LoadSettingsFiles();
  }

  void OnPreSetup(rex::RuntimeConfig &config) override
  {
    // Native XInput avoids SDL controller-mapping surprises on Windows;
    // settings/mapping.toml can still override input_backend.
    REXCVAR_SET(input_backend, "xinput");

    std::string backend = REXCVAR_GET(graphics_backend);
    if (backend != "any" && !config.gpu_plugin.empty())
    {
      config.graphics = rex::system::LoadGpuPlugin(config.gpu_plugin, backend);
      if (!config.graphics)
      {
        REXLOG_WARN("graphics_backend '{}' unavailable, falling back to automatic selection",
                    backend);
      }
    }
  }

  // Pre-commit the physical aliases (0xA0000000 uncached, 0xC0000000,
  // 0xE0000000) so a GPU ring-buffer write through an alias never faults
  // on an uncommitted page (AO2 sub_82A50FF0 incident, KB ERROR_LOOKUP_TABLE).
  // Cheap insurance for a Xenos title; remove if profiling shows RSS pressure.
  static void PrecommitMemoryAliases()
  {
    auto *rt = rex::Runtime::instance();
    uint8_t *base = rt ? rt->virtual_membase() : nullptr;
    if (!base)
    {
      return;
    }

#if defined(_WIN32)
    VirtualAlloc(base + 0xA0000000ull, 0x20000000ull, MEM_COMMIT, PAGE_READWRITE);
    VirtualAlloc(base + 0xC0000000ull, 0x20000000ull, MEM_COMMIT, PAGE_READWRITE);
    VirtualAlloc(base + 0xE0000000ull, 0x1FD00000ull, MEM_COMMIT, PAGE_READWRITE);
#else
    rex::memory::AllocFixed(base + 0xA0000000ull, 0x20000000ull, rex::memory::AllocationType::kCommit, rex::memory::PageAccess::kReadWrite);
    rex::memory::AllocFixed(base + 0xC0000000ull, 0x20000000ull, rex::memory::AllocationType::kCommit, rex::memory::PageAccess::kReadWrite);
    rex::memory::AllocFixed(base + 0xE0000000ull, 0x1FD00000ull, rex::memory::AllocationType::kCommit, rex::memory::PageAccess::kReadWrite);
#endif
    REXLOG_INFO("Pre-committed guest physical aliases (0xA0000000, 0xC0000000, 0xE0000000)");
  }

  // Segoe UI with Cyrillic for the launcher (the SDK font is Latin-1 only).
  void OnConfigureFonts(ImFontAtlas *atlas) override
  {
    vp_launcher::LoadFonts(atlas);
  }

  // Launcher: keep the runtime unbuilt until "ИГРАТЬ", so the GPU settings
  // chosen there (render path, resolution scale) apply to this launch.
  // --vp_show_launcher=false (or unticking it in the launcher) starts directly.
  std::optional<rex::PathConfig> OnFinalizePaths(const rex::PathConfig &defaults,
                                                 std::function<void(rex::PathConfig)> resume) override
  {
    if (!REXCVAR_GET(vp_show_launcher) || !imgui_drawer())
    {
      return defaults;
    }
    launcher_ = std::make_unique<vp_launcher::LauncherDialog>(
        imgui_drawer(), immediate_drawer(), utils::RepoRoot(), defaults.game_data_root,
        [this, defaults, resume]() {
          // Called inside the launcher's ImGui frame: leave it first, then drop
          // the dialog and build the runtime.
          app_context().CallInUIThreadDeferred([this, defaults, resume]() {
            launcher_.reset();
            resume(defaults);
          });
        },
        [this]() {
          app_context().CallInUIThreadDeferred([this]() {
            if (window())
            {
              window()->RequestClose();
            }
          });
        });
    return std::nullopt;
  }

  void OnPostLoadXexImage() override
  {
    PrecommitMemoryAliases();
    // Byte patches to the loaded image go here (game_patches::* once any
    // address is verified against this XEX).
  }

  void OnPostSetup() override
  {
    // Second config pass: GPU-backend cvars (vsync, resolution_scale,
    // render_target_path_d3d12...) are registered by now.
    utils::LoadSettingsFiles();

    if (REXCVAR_GET(vp_high_res_timer))
    {
      vp_timing::EnableHighResTimer();  // TiP-Recomp: timeBeginPeriod(1)
    }

    if (REXCVAR_GET(dev_debug_runtime))
    {
      // Discovery mode. Order matters: the pointer scan asks the dispatcher
      // which targets are registered, the sweep then registers a stub on
      // every remaining address. (PerformMissingFunctionScan is available
      // too but dumps ~1.5M interior addresses - not useful by default.)
      debug_tools::PerformDataPointerScan();
      debug_tools::PerformStubSweep();
    }
  }

  void OnShutdown() override
  {
    launcher_.reset();
    if (REXCVAR_GET(vp_high_res_timer))
    {
      vp_timing::DisableHighResTimer();
    }
  }

  // Available for later phases:
  // void OnPostInitLogging() override {}
  // void OnLoadXexImage(std::string& xex_image) override {}   // default: game:\default.xex
  // void OnCreateDialogs(rex::ui::ImGuiDrawer* drawer) override {}
  // void OnPreLaunchModule() override {}                        // last chance to patch guest memory
  // void OnPostLaunchModule(rex::system::XThread* thread) override {}
  // std::unique_ptr<rex::ui::ImGuiDialog> CreateAchievementsOverlay() override;
  // std::unique_ptr<rex::ui::AchievementNotificationDialog> CreateAchievementNotificationDialog() override;

private:
  std::unique_ptr<vp_launcher::LauncherDialog> launcher_;
};
