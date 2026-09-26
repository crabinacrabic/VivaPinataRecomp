// utils.h - repository-relative path helpers and settings loading
#pragma once

#include <filesystem>

#include <rex/cvar.h>
#include <rex/filesystem.h>

namespace utils
{
  // Walk up from the exe folder until the project root is found, so the
  // binary works from build/<Config>/, out/build/<preset>/ or next to the
  // manifest.
  static std::filesystem::path RepoRoot()
  {
    namespace fs = std::filesystem;
    fs::path dir = rex::filesystem::GetExecutableFolder();
    for (int i = 0; i < 6 && !dir.empty() && dir.has_parent_path(); ++i)
    {
      if (fs::exists(dir / "vivapinata_manifest.toml") || fs::exists(dir / "game_files"))
      {
        break;
      }
      dir = dir.parent_path();
    }
    return dir;
  }

  static std::filesystem::path SettingsDir() { return RepoRoot() / "settings"; }

  // All files are flat key = value TOML. Loaded twice on purpose: once from
  // OnConfigurePaths (window/input cvars exist already) and again from
  // OnPostSetup (GPU-backend cvars register only after the runtime is up).
  // launcher.toml (written by src/launcher.h) comes last so the launcher's
  // choices win over hardware.toml; config sources of equal priority are
  // last-wins in rex::cvar.
  static void LoadSettingsFiles()
  {
    namespace fs = std::filesystem;
    fs::path dir = SettingsDir();
    for (const char *file : {"hardware.toml", "mapping.toml", "launcher.toml"})
    {
      if (fs::path p = dir / file; fs::exists(p))
      {
        rex::cvar::LoadConfig(p);
      }
    }
  }
} // namespace utils
