// launcher.h - start-up launcher (ImGui page, English / Russian) shown before the Runtime exists
//
// VivapinataApp::OnFinalizePaths returns std::nullopt while the launcher is
// open, so ConstructRuntime (GPU init, XEX load) only runs after PLAY.
// Settings picked here therefore apply on this launch: render_target_path_d3d12
// (kInitOnly) and resolution_scale (kRequiresRestart) are read when the GPU
// system is set up, which has not happened yet.
//
// Choices go to settings/launcher.toml (flat key = value), which
// utils::LoadSettingsFiles loads after hardware.toml so they win over it.
// Not rex::cvar::SaveConfig: it rewrites the whole file and would drop the
// comments in hardware.toml.
//
// Layout follows TiP-Recomp's LaunchMenu (full-window page + options window),
// adapted to SDK 0.10: ImGuiDialog registers itself in its constructor, the
// texture comes from ReXApp::immediate_drawer(), and the SDK uploads the font
// atlas once (legacy backend), so Cyrillic is baked in LoadFonts().
#pragma once

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>
#pragma comment(lib, "bcrypt.lib")
#endif

#include <imgui.h>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ui/image_decode.h>
#include <rex/ui/imgui_dialog.h>
#include <rex/ui/immediate_drawer.h>
#include <rex/ui/window.h>
#include <rex/version.h>

#include "game_cvars.h"

namespace vp_launcher
{
  // SHA-1 of game_files/default.xex from the tested disc:
  // Viva Pinata (USA, Europe) (En,Ja,Fr,De,Es,It,Nl,Pt,Sv,No,Zh,Ko,Pl,Cs,Hu,Sk),
  // Title ID 4D5307F2, Media ID 690B3287 (README "Какая версия игры нужна").
  inline constexpr const char *kExpectedXexSha1 = "130dbbe05328eeca23ab830bc8ed3337064eddd3";

  // ---------------------------------------------------------------------------
  // Fonts
  // ---------------------------------------------------------------------------

  struct Fonts
  {
    ImFont *body = nullptr;
    ImFont *caption = nullptr;
    ImFont *button = nullptr;
    ImFont *title = nullptr;
  };

  inline Fonts &GetFonts()
  {
    static Fonts fonts;
    return fonts;
  }

  // Called from ReXApp::OnConfigureFonts, before the atlas is built. The SDK's
  // default font (ProggyTiny, Latin-1 only) stays the default for its own
  // overlays; the launcher pushes these explicitly.
  inline void LoadFonts(ImFontAtlas *atlas)
  {
#if defined(_WIN32)
    // Must outlive the atlas build.
    static const ImWchar kRanges[] = {
        0x0020, 0x00FF,  // Basic Latin + Latin-1 (ñ, ×, ·)
        0x0400, 0x04FF,  // Cyrillic
        0x2013, 0x2026,  // dashes, quotes, bullet, ellipsis
        0,
    };
    wchar_t windir[MAX_PATH] = {};
    if (GetWindowsDirectoryW(windir, MAX_PATH) == 0)
    {
      return;
    }
    const std::filesystem::path font_dir = std::filesystem::path(windir) / "Fonts";
    auto add = [&](const char *file, float size) -> ImFont * {
      const std::filesystem::path path = font_dir / file;
      std::error_code ec;
      if (!std::filesystem::exists(path, ec))
      {
        REXLOG_WARN("launcher: font {} not found, Cyrillic will not render", path.string());
        return nullptr;
      }
      ImFontConfig config;
      config.OversampleH = 2;
      config.OversampleV = 1;
      return atlas->AddFontFromFileTTF(path.string().c_str(), size, &config, kRanges);
    };
    Fonts &fonts = GetFonts();
    fonts.body = add("segoeui.ttf", 20.0f);
    fonts.caption = add("segoeui.ttf", 16.0f);
    fonts.button = add("seguisb.ttf", 26.0f);
    fonts.title = add("segoeuib.ttf", 40.0f);
#else
    (void)atlas;
#endif
  }

  class FontScope
  {
  public:
    explicit FontScope(ImFont *font) : pushed_(font != nullptr)
    {
      if (pushed_)
      {
        ImGui::PushFont(font, font->LegacySize);
      }
    }
    ~FontScope()
    {
      if (pushed_)
      {
        ImGui::PopFont();
      }
    }
    FontScope(const FontScope &) = delete;
    FontScope &operator=(const FontScope &) = delete;

  private:
    bool pushed_;
  };

  // ---------------------------------------------------------------------------
  // Game files check
  // ---------------------------------------------------------------------------

  enum class GameStatus
  {
    kOk,            // default.xex is the tested build
    kOtherVersion,  // files present, different default.xex
    kMissing,       // no default.xex or no Beta\ data folder
  };

  inline std::string Sha1Hex(const std::filesystem::path &file)
  {
    std::string result;
#if defined(_WIN32)
    std::ifstream in(file, std::ios::binary);
    if (!in)
    {
      return result;
    }
    BCRYPT_ALG_HANDLE alg = nullptr;
    if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA1_ALGORITHM, nullptr, 0)))
    {
      return result;
    }
    BCRYPT_HASH_HANDLE hash = nullptr;
    if (BCRYPT_SUCCESS(BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0)))
    {
      std::vector<char> buffer(1 << 16);
      bool ok = true;
      while (ok && in)
      {
        in.read(buffer.data(), std::streamsize(buffer.size()));
        const std::streamsize n = in.gcount();
        if (n > 0)
        {
          ok = BCRYPT_SUCCESS(BCryptHashData(hash, reinterpret_cast<PUCHAR>(buffer.data()), ULONG(n), 0));
        }
      }
      UCHAR digest[20] = {};
      if (ok && BCRYPT_SUCCESS(BCryptFinishHash(hash, digest, sizeof(digest), 0)))
      {
        static const char kHex[] = "0123456789abcdef";
        for (UCHAR b : digest)
        {
          result += kHex[b >> 4];
          result += kHex[b & 0xF];
        }
      }
      BCryptDestroyHash(hash);
    }
    BCryptCloseAlgorithmProvider(alg, 0);
#else
    (void)file;
#endif
    return result;
  }

  inline GameStatus CheckGame(const std::filesystem::path &game_root)
  {
    std::error_code ec;
    const std::filesystem::path xex = game_root / "default.xex";
    if (game_root.empty() || !std::filesystem::is_regular_file(xex, ec) ||
        !std::filesystem::is_directory(game_root / "Beta", ec))
    {
      return GameStatus::kMissing;
    }
    const std::string sha1 = Sha1Hex(xex);
    if (sha1 != kExpectedXexSha1)
    {
      REXLOG_WARN("launcher: default.xex SHA-1 {} differs from the tested build {}", sha1,
                  kExpectedXexSha1);
      return GameStatus::kOtherVersion;
    }
    return GameStatus::kOk;
  }

  // ---------------------------------------------------------------------------
  // Game text language
  // ---------------------------------------------------------------------------
  // The game reads its text from Beta\bundles\english.bnl or englishus.bnl
  // (both use the same font cache, which already has Cyrillic). The Russian
  // bundle built by tools/make_russian_bnl.py shares their schema and is
  // copied over both; the disc files are kept as <name>.bnl.orig. A backup is
  // made only from a file whose SHA-1 matches the disc, so a manually replaced
  // file is never mistaken for the original.

  struct BundleFile
  {
    const char *name;
    const char *disc_sha1;
  };
  inline constexpr BundleFile kLanguageBundles[] = {
      {"english.bnl", "093795895fbe38e9faeeca85f5c1532067fd0ae0"},
      {"englishus.bnl", "b4cc7608ce1f3588d8eef83d854be9bf0273063a"},
  };

  inline std::filesystem::path BundleDir(const std::filesystem::path &game_root)
  {
    return game_root / "Beta" / "bundles";
  }

  inline bool RussianAvailable(const std::filesystem::path &game_root)
  {
    std::error_code ec;
    return std::filesystem::is_regular_file(BundleDir(game_root) / "russian.bnl", ec);
  }

  inline void ApplyLanguage(const std::filesystem::path &game_root, bool russian)
  {
    namespace fs = std::filesystem;
    const fs::path dir = BundleDir(game_root);
    const fs::path ru = dir / "russian.bnl";
    russian = russian && RussianAvailable(game_root);
    for (const BundleFile &file : kLanguageBundles)
    {
      std::error_code ec;
      const fs::path current = dir / file.name;
      fs::path orig = current;
      orig += ".orig";
      if (!fs::exists(orig, ec))
      {
        if (!russian)
        {
          continue;  // never switched: the disc file is in place
        }
        if (Sha1Hex(current) != file.disc_sha1)
        {
          REXLOG_WARN("launcher: {} is not the disc file and has no .orig backup; left as is",
                      current.string());
          continue;
        }
        if (!fs::copy_file(current, orig, ec))
        {
          REXLOG_WARN("launcher: cannot back up {}: {}", current.string(), ec.message());
          continue;
        }
      }
      const fs::path &source = russian ? ru : orig;
      if (!fs::copy_file(source, current, fs::copy_options::overwrite_existing, ec))
      {
        REXLOG_WARN("launcher: cannot install {} as {}: {}", source.string(), current.string(),
                    ec.message());
      }
    }
    REXLOG_INFO("launcher: game text language {}", russian ? "Russian (ZoG Team)" : "English");
  }

  // ---------------------------------------------------------------------------
  // settings/launcher.toml
  // ---------------------------------------------------------------------------

  class LauncherSettings
  {
  public:
    explicit LauncherSettings(std::filesystem::path path) : path_(std::move(path)) { Load(); }

    // `toml_value` is a TOML literal: true, 2, "rov". Applies the cvar now
    // (Source::kRuntime, so the second config pass in OnPostSetup cannot undo
    // it) and writes the file straight away, so closing the window keeps it.
    void Set(const std::string &key, const std::string &toml_value)
    {
      std::string raw = toml_value;
      if (raw.size() >= 2 && raw.front() == '"' && raw.back() == '"')
      {
        raw = raw.substr(1, raw.size() - 2);
      }
      if (!rex::cvar::SetFlagByName(key, raw))
      {
        REXLOG_WARN("launcher: {} = {} not applied (set on the command line?)", key, raw);
      }
      auto it = std::find_if(values_.begin(), values_.end(),
                             [&](const auto &kv) { return kv.first == key; });
      if (it != values_.end())
      {
        it->second = toml_value;
      }
      else
      {
        values_.emplace_back(key, toml_value);
      }
      Save();
    }

  private:
    static std::string Trim(const std::string &s)
    {
      const auto first = s.find_first_not_of(" \t\r");
      if (first == std::string::npos)
      {
        return {};
      }
      const auto last = s.find_last_not_of(" \t\r");
      return s.substr(first, last - first + 1);
    }

    // The file is only ever written by Save(): flat `key = value`, `#` comments,
    // no '#' inside values.
    void Load()
    {
      std::ifstream in(path_);
      std::string line;
      while (std::getline(in, line))
      {
        if (const auto hash = line.find('#'); hash != std::string::npos)
        {
          line.erase(hash);
        }
        const auto eq = line.find('=');
        if (eq == std::string::npos)
        {
          continue;
        }
        std::string key = Trim(line.substr(0, eq));
        std::string value = Trim(line.substr(eq + 1));
        if (!key.empty() && !value.empty())
        {
          values_.emplace_back(std::move(key), std::move(value));
        }
      }
    }

    void Save() const
    {
      std::ofstream out(path_, std::ios::trunc);
      if (!out)
      {
        REXLOG_WARN("launcher: cannot write {}", path_.string());
        return;
      }
      out << "# launcher.toml - written by the launcher (src/launcher.h).\n"
             "# Loaded after hardware.toml, so these keys win over it.\n"
             "# Delete this file to go back to hardware.toml.\n";
      for (const auto &[key, value] : values_)
      {
        out << key << " = " << value << "\n";
      }
    }

    std::filesystem::path path_;
    std::vector<std::pair<std::string, std::string>> values_;
  };

  // ---------------------------------------------------------------------------
  // Launcher page
  // ---------------------------------------------------------------------------

  inline constexpr float kPad = 40.0f;
  inline constexpr float kPanelHeight = 250.0f;
  inline constexpr float kFade = 160.0f;
  inline constexpr float kPlayWidth = 290.0f;
  inline constexpr float kPlayHeight = 70.0f;
  inline constexpr float kButtonHeight = 44.0f;
  inline constexpr float kGap = 12.0f;

  inline constexpr ImU32 kPanelColor = IM_COL32(14, 18, 16, 255);
  inline constexpr ImU32 kPanelClear = IM_COL32(14, 18, 16, 0);
  inline constexpr ImU32 kTextColor = IM_COL32(238, 238, 238, 255);
  inline constexpr ImU32 kDimText = IM_COL32(190, 196, 192, 255);
  inline constexpr ImU32 kFaintText = IM_COL32(130, 138, 134, 255);
  inline constexpr ImU32 kStatusOk = IM_COL32(96, 216, 116, 255);
  inline constexpr ImU32 kStatusWarn = IM_COL32(250, 200, 64, 255);
  inline constexpr ImU32 kStatusError = IM_COL32(242, 90, 76, 255);

  inline const ImVec4 kPlayColor(0.18f, 0.64f, 0.31f, 1.0f);
  inline const ImVec4 kSettingsColor(0.22f, 0.45f, 0.60f, 1.0f);
  inline const ImVec4 kQuitColor(0.30f, 0.32f, 0.36f, 1.0f);
  inline const ImVec4 kSwitchColor(0.10f, 0.12f, 0.11f, 0.75f);

  inline constexpr const char *kPathValues[] = {"rov", "rtv", ""};

  // ---------------------------------------------------------------------------
  // UI text (English / Russian)
  // ---------------------------------------------------------------------------
  // vp_launcher_language picks the table; "auto" follows the Windows UI
  // language. This is only the launcher's own language - the game's text
  // language is vp_language.

  struct UiText
  {
    const char *subtitle;
    const char *status_ok;
    const char *status_other;
    const char *status_missing;
    const char *hint_other;
    const char *hint_missing;
    const char *play;
    const char *settings;
    const char *quit;
    const char *show_on_startup;
    const char *switch_label;  // top-right button: the other language
    const char *settings_title;
    const char *section_game;
    const char *section_display;
    const char *section_graphics;
    const char *section_system;
    const char *section_launcher;
    const char *text_language;
    const char *text_english;
    const char *text_russian;
    const char *text_russian_missing;
    const char *fullscreen;
    const char *vsync;
    const char *resolution;
    const char *scale[3];
    const char *render_mode;
    const char *path[3];
    const char *precise_timer;
    const char *launcher_language;
    const char *language_auto;
    const char *saved_to;
    const char *done;
  };

  inline constexpr UiText kTextEn{
      .subtitle = "Native Windows version · recompiled from Xbox 360",
      .status_ok = "Game found: Viva Pinata (USA, Europe), correct version",
      .status_other = "A different default.xex was found: the game may not start",
      .status_missing = "Game files not found",
      .hint_other = "You need the Viva Pinata (USA, Europe) disc, Title ID 4D5307F2. See README",
      .hint_missing = "Unpack the disc into the game_files folder (default.xex and Beta). See README",
      .play = "PLAY",
      .settings = "Settings",
      .quit = "Quit",
      .show_on_startup = "Show at startup",
      .switch_label = "RU",
      .settings_title = "Settings",
      .section_game = "Game",
      .section_display = "Display",
      .section_graphics = "Graphics",
      .section_system = "System",
      .section_launcher = "Launcher",
      .text_language = "Game text language",
      .text_english = "English (original)",
      .text_russian = "Russian (ZoG Team translation)",
      .text_russian_missing = "For Russian, build russian.bnl with tools/make_russian_bnl.py (see README)",
      .fullscreen = "Fullscreen",
      .vsync = "Vertical sync",
      .resolution = "Render resolution",
      .scale = {"×1 — 1280×720, like the Xbox 360", "×2 — 2560×1440", "×3 — 3840×2160"},
      .render_mode = "Render mode",
      .path = {"ROV — accurate, recommended", "RTV — faster", "Auto — chosen by the SDK"},
      .precise_timer = "Precise system timer (1 ms)",
      .launcher_language = "Launcher language",
      .language_auto = "Auto (Windows language)",
      .saved_to = "Saved to settings/launcher.toml, which overrides hardware.toml",
      .done = "Done",
  };

  inline constexpr UiText kTextRu{
      .subtitle = "Нативная версия для Windows · рекомпиляция с Xbox 360",
      .status_ok = "Игра найдена: Viva Pinata (USA, Europe), версия подходит",
      .status_other = "Найдена другая версия default.xex: игра может не запуститься",
      .status_missing = "Файлы игры не найдены",
      .hint_other = "Нужен диск Viva Pinata (USA, Europe), Title ID 4D5307F2. Подробности в README",
      .hint_missing = "Распакуйте диск в папку game_files (default.xex и Beta). Подробности в README",
      .play = "ИГРАТЬ",
      .settings = "Настройки",
      .quit = "Выход",
      .show_on_startup = "Показывать при запуске",
      .switch_label = "EN",
      .settings_title = "Настройки",
      .section_game = "Игра",
      .section_display = "Экран",
      .section_graphics = "Графика",
      .section_system = "Система",
      .section_launcher = "Лаунчер",
      .text_language = "Язык текста",
      .text_english = "English (оригинал)",
      .text_russian = "Русский (перевод ZoG Team)",
      .text_russian_missing = "Для русского соберите russian.bnl: tools/make_russian_bnl.py (README)",
      .fullscreen = "Полноэкранный режим",
      .vsync = "Вертикальная синхронизация",
      .resolution = "Разрешение рендера",
      .scale = {"×1 — 1280×720, как на Xbox 360", "×2 — 2560×1440", "×3 — 3840×2160"},
      .render_mode = "Режим рендера",
      .path = {"ROV — точнее, рекомендуется", "RTV — быстрее", "Авто — выбор SDK"},
      .precise_timer = "Точный системный таймер (1 мс)",
      .launcher_language = "Язык лаунчера",
      .language_auto = "Авто (язык Windows)",
      .saved_to = "Сохраняется в settings/launcher.toml, он важнее hardware.toml",
      .done = "Готово",
  };

  inline bool WindowsPrefersRussian()
  {
#if defined(_WIN32)
    return PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_RUSSIAN;
#else
    return false;
#endif
  }

  inline bool UiRussian()
  {
    const std::string &lang = REXCVAR_GET(vp_launcher_language);
    if (lang == "ru")
    {
      return true;
    }
    if (lang == "en")
    {
      return false;
    }
    static const bool windows_russian = WindowsPrefersRussian();
    return windows_russian;
  }

  inline const UiText &Ui() { return UiRussian() ? kTextRu : kTextEn; }

  inline bool ColorButton(const char *label, const ImVec4 &c, const ImVec2 &size)
  {
    const ImVec4 hovered(std::min(c.x * 1.15f, 1.0f), std::min(c.y * 1.15f, 1.0f),
                         std::min(c.z * 1.15f, 1.0f), c.w);
    const ImVec4 active(c.x * 0.85f, c.y * 0.85f, c.z * 0.85f, c.w);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 10.0f);
    ImGui::PushStyleColor(ImGuiCol_Button, c);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hovered);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, active);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
    const bool clicked = ImGui::Button(label, size);
    ImGui::PopStyleColor(4);
    ImGui::PopStyleVar();
    return clicked;
  }

  inline void PanelText(ImDrawList *dl, ImFont *font, const ImVec2 &pos, ImU32 color, const char *text)
  {
    if (font)
    {
      dl->AddText(font, font->LegacySize, pos, color, text);
    }
    else
    {
      dl->AddText(pos, color, text);
    }
  }

  inline const char *BoolLiteral(bool value) { return value ? "true" : "false"; }

  class LauncherDialog : public rex::ui::ImGuiDialog
  {
  public:
    // on_play / on_quit are invoked from OnDraw (inside the ImGui frame); the
    // app defers the real work (destroying this dialog, building the runtime)
    // with CallInUIThreadDeferred.
    LauncherDialog(rex::ui::ImGuiDrawer *drawer, rex::ui::ImmediateDrawer *immediate,
                   std::filesystem::path repo_root, const std::filesystem::path &game_root,
                   std::function<void()> on_play, std::function<void()> on_quit)
        : rex::ui::ImGuiDialog(drawer),
          immediate_(immediate),
          repo_root_(std::move(repo_root)),
          settings_(repo_root_ / "settings" / "launcher.toml"),
          status_(CheckGame(game_root)),
          russian_available_(RussianAvailable(game_root)),
          on_play_(std::move(on_play)),
          on_quit_(std::move(on_quit))
    {
    }

  protected:
    void OnDraw(ImGuiIO &io) override
    {
      if (done_)
      {
        return;
      }
      const ImVec2 disp = io.DisplaySize;
      if (disp.x <= 0.0f || disp.y <= 0.0f)
      {
        return;
      }
      EnsureTextures();
      const Fonts &fonts = GetFonts();
      const UiText &ui = Ui();

      ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_Always);
      ImGui::SetNextWindowSize(disp, ImGuiCond_Always);
      ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
      ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
      ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
      ImGui::Begin("##vp_launcher", nullptr,
                   ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                       ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings |
                       ImGuiWindowFlags_NoBringToFrontOnFocus |
                       ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoNav);
      ImGui::PopStyleVar(3);

      ImDrawList *dl = ImGui::GetWindowDrawList();
      const float panel_top = std::max(disp.y * 0.5f, disp.y - kPanelHeight);

      // Background: title-screen art on top, fading into the dark panel.
      dl->AddRectFilled(ImVec2(0.0f, 0.0f), disp, kPanelColor);
      if (background_ && background_w_ > 0 && background_h_ > 0)
      {
        const float scale = std::max(disp.x / float(background_w_),
                                     (panel_top + kFade * 0.5f) / float(background_h_));
        const ImVec2 size(background_w_ * scale, background_h_ * scale);
        const ImVec2 pos((disp.x - size.x) * 0.5f, 0.0f);
        dl->AddImage(TextureId(background_), pos, ImVec2(pos.x + size.x, pos.y + size.y));
        dl->AddRectFilledMultiColor(ImVec2(0.0f, size.y - kFade), ImVec2(disp.x, size.y + 1.0f),
                                    kPanelClear, kPanelClear, kPanelColor, kPanelColor);
      }

      // Left: title, subtitle, game status, footer.
      const float top = panel_top + 14.0f;
      float title_x = kPad;
      if (icon_)
      {
        dl->AddImage(TextureId(icon_), ImVec2(kPad, top + 2.0f), ImVec2(kPad + 52.0f, top + 54.0f));
        title_x += 64.0f;
      }
      PanelText(dl, fonts.title, ImVec2(title_x, top), kTextColor, "Viva Piñata Recomp");
      PanelText(dl, fonts.body, ImVec2(kPad, top + 66.0f), kDimText, ui.subtitle);

      const float status_y = top + 108.0f;
      dl->AddCircleFilled(ImVec2(kPad + 7.0f, status_y + 14.0f), 6.0f, StatusColor());
      PanelText(dl, fonts.body, ImVec2(kPad + 22.0f, status_y), kTextColor, StatusText());
      if (const char *hint = StatusHint())
      {
        PanelText(dl, fonts.caption, ImVec2(kPad + 22.0f, status_y + 30.0f), kDimText, hint);
      }

      PanelText(dl, fonts.caption, ImVec2(kPad, disp.y - 34.0f), kFaintText,
               "ReXGlue SDK " REXGLUE_VERSION_STRING
               "  ·  Title ID 4D5307F2  ·  github.com/crabinacrabic/VivaPinataRecomp");

      // Right: play, settings / quit, show-on-startup.
      const bool can_play = status_ != GameStatus::kMissing;
      const float bx = disp.x - kPad - kPlayWidth;
      float by = panel_top + 24.0f;
      bool play = false;
      bool quit = false;
      {
        FontScope font(fonts.button);
        ImGui::SetCursorPos(ImVec2(bx, by));
        ImGui::BeginDisabled(!can_play);
        play = ColorButton(ui.play, kPlayColor, ImVec2(kPlayWidth, kPlayHeight));
        ImGui::EndDisabled();
      }
      by += kPlayHeight + kGap;
      {
        FontScope font(fonts.body);
        const float half = (kPlayWidth - kGap) * 0.5f;
        ImGui::SetCursorPos(ImVec2(bx, by));
        if (ColorButton(ui.settings, kSettingsColor, ImVec2(half, kButtonHeight)))
        {
          options_open_ = !options_open_;
        }
        ImGui::SameLine(0.0f, kGap);
        quit = ColorButton(ui.quit, kQuitColor, ImVec2(half, kButtonHeight));

        by += kButtonHeight + kGap;
        ImGui::SetCursorPos(ImVec2(bx, by));
        bool show = REXCVAR_GET(vp_show_launcher);
        if (ImGui::Checkbox(ui.show_on_startup, &show))
        {
          settings_.Set("vp_show_launcher", BoolLiteral(show));
        }

        // Top right: quick switch of the launcher language (shows the other one).
        const ImVec2 switch_size(64.0f, 36.0f);
        ImGui::SetCursorPos(ImVec2(disp.x - kPad - switch_size.x, 20.0f));
        if (ColorButton(ui.switch_label, kSwitchColor, switch_size))
        {
          settings_.Set("vp_launcher_language", UiRussian() ? "\"en\"" : "\"ru\"");
        }
      }

      ImGui::End();

      DrawOptions(io);

      if (!options_open_ &&
          (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false)))
      {
        play = play || can_play;
      }
      if (play && can_play)
      {
        done_ = true;
        options_open_ = false;
        if (on_play_)
        {
          on_play_();
        }
      }
      else if (quit)
      {
        done_ = true;
        if (on_quit_)
        {
          on_quit_();
        }
      }
    }

  private:
    static ImTextureID TextureId(const std::unique_ptr<rex::ui::ImmediateTexture> &texture)
    {
      // The SDK's ImGui renderer casts the id back to ImmediateTexture*.
      return static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(texture.get()));
    }

    std::unique_ptr<rex::ui::ImmediateTexture> LoadTexture(const std::filesystem::path &path, int &width,
                                                           int &height)
    {
      std::ifstream in(path, std::ios::binary | std::ios::ate);
      if (!in)
      {
        REXLOG_WARN("launcher: {} not found", path.string());
        return nullptr;
      }
      std::vector<uint8_t> bytes(size_t(in.tellg()));
      in.seekg(0);
      in.read(reinterpret_cast<char *>(bytes.data()), std::streamsize(bytes.size()));
      std::vector<uint8_t> pixels = rex::ui::DecodeImageRGBA(bytes.data(), bytes.size(), width, height);
      if (pixels.empty())
      {
        REXLOG_WARN("launcher: cannot decode {}", path.string());
        return nullptr;
      }
      return immediate_->CreateTexture(uint32_t(width), uint32_t(height),
                                       rex::ui::ImmediateTextureFilter::kLinear, false, pixels.data());
    }

    // Lazily, on the UI thread inside the first frame (the presenter is live).
    void EnsureTextures()
    {
      if (textures_loaded_ || !immediate_)
      {
        return;
      }
      textures_loaded_ = true;
      const std::filesystem::path dir = repo_root_ / "assets" / "launcher";
      background_ = LoadTexture(dir / "background.png", background_w_, background_h_);
      int icon_w = 0;
      int icon_h = 0;
      icon_ = LoadTexture(dir / "icon.png", icon_w, icon_h);
    }

    ImU32 StatusColor() const
    {
      switch (status_)
      {
      case GameStatus::kOk:
        return kStatusOk;
      case GameStatus::kOtherVersion:
        return kStatusWarn;
      default:
        return kStatusError;
      }
    }

    const char *StatusText() const
    {
      switch (status_)
      {
      case GameStatus::kOk:
        return Ui().status_ok;
      case GameStatus::kOtherVersion:
        return Ui().status_other;
      default:
        return Ui().status_missing;
      }
    }

    const char *StatusHint() const
    {
      switch (status_)
      {
      case GameStatus::kOk:
        return nullptr;
      case GameStatus::kOtherVersion:
        return Ui().hint_other;
      default:
        return Ui().hint_missing;
      }
    }

    static int PathIndex(const std::string &value)
    {
      if (value == "rov")
      {
        return 0;
      }
      if (value == "rtv")
      {
        return 1;
      }
      return 2;
    }

    void DrawOptions(ImGuiIO &io)
    {
      if (!options_open_)
      {
        return;
      }
      const Fonts &fonts = GetFonts();
      const UiText &ui = Ui();
      FontScope font(fonts.body);
      ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.42f),
                              ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
      ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(20.0f, 16.0f));
      ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10.0f);
      // "###" keeps the window identity when the title changes language.
      const std::string title = std::string(ui.settings_title) + "###vp_launcher_options";
      const bool open = ImGui::Begin(title.c_str(), &options_open_,
                                     ImGuiWindowFlags_AlwaysAutoResize |
                                         ImGuiWindowFlags_NoSavedSettings |
                                         ImGuiWindowFlags_NoCollapse);
      ImGui::PopStyleVar(2);
      if (open)
      {
        constexpr float kItemWidth = 360.0f;

        ImGui::SeparatorText(ui.section_game);
        ImGui::TextUnformatted(ui.text_language);
        const bool russian = russian_available_ && REXCVAR_GET(vp_language) == "ru";
        ImGui::SetNextItemWidth(kItemWidth);
        if (ImGui::BeginCombo("##vp_language", russian ? ui.text_russian : ui.text_english))
        {
          if (ImGui::Selectable(ui.text_english, !russian))
          {
            settings_.Set("vp_language", "\"en\"");
          }
          if (ImGui::Selectable(ui.text_russian, russian,
                                russian_available_ ? 0 : ImGuiSelectableFlags_Disabled))
          {
            settings_.Set("vp_language", "\"ru\"");
          }
          ImGui::EndCombo();
        }
        if (!russian_available_)
        {
          FontScope caption(fonts.caption);
          ImGui::TextDisabled("%s", ui.text_russian_missing);
        }

        ImGui::SeparatorText(ui.section_display);
        bool fullscreen = rex::cvar::Query<bool>("fullscreen");
        if (ImGui::Checkbox(ui.fullscreen, &fullscreen))
        {
          settings_.Set("fullscreen", BoolLiteral(fullscreen));
        }
        bool vsync = rex::cvar::Query<bool>("vsync");
        if (ImGui::Checkbox(ui.vsync, &vsync))
        {
          settings_.Set("vsync", BoolLiteral(vsync));
        }
        ImGui::TextUnformatted(ui.resolution);
        int scale_index = std::clamp(rex::cvar::Query<int32_t>("resolution_scale"), 1, 3) - 1;
        ImGui::SetNextItemWidth(kItemWidth);
        if (ImGui::Combo("##resolution_scale", &scale_index, ui.scale, IM_ARRAYSIZE(ui.scale)))
        {
          settings_.Set("resolution_scale", std::to_string(scale_index + 1));
        }

        ImGui::SeparatorText(ui.section_graphics);
        ImGui::TextUnformatted(ui.render_mode);
        int path_index = PathIndex(rex::cvar::Query<std::string>("render_target_path_d3d12"));
        ImGui::SetNextItemWidth(kItemWidth);
        if (ImGui::Combo("##render_target_path", &path_index, ui.path, IM_ARRAYSIZE(ui.path)))
        {
          settings_.Set("render_target_path_d3d12", std::string("\"") + kPathValues[path_index] + "\"");
        }

        ImGui::SeparatorText(ui.section_system);
        bool timer = REXCVAR_GET(vp_high_res_timer);
        if (ImGui::Checkbox(ui.precise_timer, &timer))
        {
          settings_.Set("vp_high_res_timer", BoolLiteral(timer));
        }

        ImGui::SeparatorText(ui.section_launcher);
        ImGui::TextUnformatted(ui.launcher_language);
        // Language names are shown in their own language; "auto" in the current one.
        const char *const language_names[] = {ui.language_auto, "English", "Русский"};
        static constexpr const char *kLanguageValues[] = {"auto", "en", "ru"};
        const std::string &current = REXCVAR_GET(vp_launcher_language);
        int language_index = current == "en" ? 1 : current == "ru" ? 2 : 0;
        ImGui::SetNextItemWidth(kItemWidth);
        if (ImGui::Combo("##vp_launcher_language", &language_index, language_names,
                         IM_ARRAYSIZE(language_names)))
        {
          settings_.Set("vp_launcher_language", std::string("\"") + kLanguageValues[language_index] + "\"");
        }

        ImGui::Spacing();
        {
          FontScope caption(fonts.caption);
          ImGui::TextDisabled("%s", ui.saved_to);
        }
        ImGui::Spacing();
        if (ColorButton(ui.done, kSettingsColor, ImVec2(150.0f, 40.0f)))
        {
          options_open_ = false;
        }
      }
      ImGui::End();
    }

    rex::ui::ImmediateDrawer *immediate_ = nullptr;
    std::filesystem::path repo_root_;
    LauncherSettings settings_;
    GameStatus status_;
    bool russian_available_;
    std::function<void()> on_play_;
    std::function<void()> on_quit_;

    std::unique_ptr<rex::ui::ImmediateTexture> background_;
    std::unique_ptr<rex::ui::ImmediateTexture> icon_;
    int background_w_ = 0;
    int background_h_ = 0;
    bool textures_loaded_ = false;
    bool options_open_ = false;
    bool done_ = false;
  };
} // namespace vp_launcher
