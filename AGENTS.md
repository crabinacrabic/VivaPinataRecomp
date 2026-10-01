# AGENTS.md — instructions for AI agents

This file is written for AI coding agents (Claude Code, Codex, Cursor, …). A person has probably given you the link to this repository and asked you to install the game, or to work on the code.

- **Installing the game for a player** → section 1.
- **Changing the code of this project** → section 2.

Talk to the user in their own language. Many users of this project speak Russian.

---

## 1. Install Viva Piñata Recomp for a player (Windows)

**Goal:** build `out\build\local-win-relwithdebinfo\vivapinata.exe` from this repository and the user's own game disc image, then start it through `run_game.bat`.

### 1.0 Ground rules

- **Never download the game** or any game files from the internet. The game must come from the user's own disc image; ask for the path to their `.iso`.
- Ask before you start installers (Visual Studio, Git, Python, 7-Zip) and before accepting licences.
- The project path must contain **only ASCII characters**, for example `C:\Games\VivaPinataRecomp`. The ReXGlue code generator crashes with `0xC0000409` on paths with non-ASCII characters.
- Do not commit or redistribute `game_files\`, `game_files\Beta\bundles\russian.bnl` or any files from the ZoG translation.
- Visual Studio (`F7`) is the build path the maintainer tests. The command-line build in step 1.5 B should work, but it is not part of the maintainer's routine.

### 1.1 Check the machine

- Windows 10/11 x64, a GPU with Direct3D 12 (`dxdiag`), about 25 GB free: Visual Studio ~12 GB, game files ~5 GB, the ISO ~8 GB (it can be deleted after unpacking).

### 1.2 Install the tools

| Tool | How | Notes |
| :-- | :-- | :-- |
| Git | `winget install -e --id Git.Git` | |
| Visual Studio 2026 (v18) Community | `winget search Microsoft.VisualStudio`, take the 2026 Community package, then add `--override "--passive --wait --add Microsoft.VisualStudio.Workload.NativeDesktop --add Microsoft.VisualStudio.Component.VC.Llvm.Clang --add Microsoft.VisualStudio.Component.VC.Llvm.ClangToolset --includeRecommended"` | Needs the C++ workload, clang 22 (the *C++ Clang tools for Windows* component), CMake and Ninja (come with the workload). VS 2022 is untested. |
| extract-xiso | Download `extract-xiso-Win64_Release.zip` from https://github.com/XboxDev/extract-xiso/releases/latest and unzip it | Unpacks the Xbox 360 ISO |
| Python 3 | `winget install -e --id Python.Python.3.12` | Only for the Russian language and the dev tools |
| 7-Zip | `winget install -e --id 7zip.7zip` | Only for the Russian language |

### 1.3 Get the project

```powershell
git clone https://github.com/crabinacrabic/VivaPinataRecomp.git C:\Games\VivaPinataRecomp
```

### 1.4 Check and unpack the game (before the first build)

The ISO must be **Viva Pinata (USA, Europe) (En,Ja,Fr,De,Es,It,Nl,Pt,Sv,No,Zh,Ko,Pl,Cs,Hu,Sk)**, Title ID `4D5307F2`, version `0.0.0.1`.

```powershell
(Get-FileHash "C:\path\to\game.iso" -Algorithm MD5).Hash      # expected 3902321DFE15D7D2510A96114DBA625A
extract-xiso -x -d C:\Games\VivaPinataRecomp\game_files "C:\path\to\game.iso"
(Get-FileHash C:\Games\VivaPinataRecomp\game_files\default.xex -Algorithm SHA1).Hash
                                                              # expected 130DBBE05328EECA23AB830BC8ED3337064EDDD3
Test-Path C:\Games\VivaPinataRecomp\game_files\Beta\bundles\englishus.bnl   # expected True
```

- `default.xex` and `Beta\` must be directly inside `game_files\`. If extract-xiso created a subfolder, move its contents up.
- A different ISO hash is only a warning. A different `default.xex` SHA-1 means the recompiled code will not match: tell the user this edition is not supported.
- Unpack **before** the first CMake configure: configuring runs the code generator on `game_files\default.xex`.

### 1.5 Build

On the first configure, CMake downloads ReXGlue SDK `0.10.0.8-dev.g1406e1b` into `rexglue\win-amd64\` (`cmake/fetch-rexglue-sdk.cmake`, a GitHub release `nightly-20260915-1406e1b7`). Then it runs `rexglue codegen vivapinata_manifest.toml` once, which writes `generated\default\`. The first build compiles about 100 generated C++ files. Expect 10–40 minutes in total.

**A. Visual Studio (tested; ask the user to do this):**
1. Open Visual Studio 2026 → **File → Open → Folder…** → `C:\Games\VivaPinataRecomp`.
2. Wait for "CMake generation finished" in the Output window.
3. In the toolbar, select the configuration **`local-win-relwithdebinfo`**.
4. **Build → Build All** (`F7`).

**B. Command line (only when the user asks you to build without the IDE):**

```powershell
$vs = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * `
      -requires Microsoft.VisualStudio.Component.VC.Llvm.Clang -property installationPath
Import-Module "$vs\Common7\Tools\Microsoft.VisualStudio.DevShell.dll"
Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments "-arch=x64 -host_arch=x64"
$env:PATH = "$vs\VC\Tools\Llvm\x64\bin;$env:PATH"     # the presets use clang / clang++ from PATH
Set-Location C:\Games\VivaPinataRecomp
cmake --preset local-win-relwithdebinfo
cmake --build out/build/local-win-relwithdebinfo
```

Result: `out\build\local-win-relwithdebinfo\vivapinata.exe`.

### 1.6 Run

Start `run_game.bat`. It runs `out\build\local-win-relwithdebinfo\vivapinata.exe`. Settings are read from `settings\`, the game from `game_files\`, and logs go to `out\build\local-win-relwithdebinfo\logs\`.

The launcher UI is in English or Russian. It follows the Windows UI language (`vp_launcher_language = "auto"`); the **EN / RU** button in the top-right corner switches it.

| Launcher (EN / RU) | Meaning |
| :-- | :-- |
| **PLAY** / **ИГРАТЬ**, or `Enter` | Start the game |
| **Settings** / **Настройки** | Game text language, fullscreen, V-Sync, render resolution, render mode (ROV = accurate / RTV = faster), precise timer, launcher language |
| **Quit** / **Выход** | Close |
| **Show at startup** / **Показывать при запуске** | `vp_show_launcher` |
| Status line | green = correct game found; yellow = other `default.xex`; red = game files missing |

Command-line flags:
- `--vp_show_launcher=false` starts the game directly;
- `--vp_language=ru|en` sets the game text language;
- `--vp_launcher_language=auto|en|ru` sets the launcher language.

### 1.7 Optional: Russian language

The translation is the ZoG Team translation of the PC version (zoneofgames.ru), distributed with the team's permission as a pack that holds only the Russian strings: **[VivaPinata_Russian_v1.zip](https://disk.yandex.ru/d/9lgjQVp7fArEjw)** on Yandex Disk (261 269 bytes, MD5 `240c0a660a9673069ca221b569331091`). It is not game data; ask the user before downloading it. Needs Python 3 and 7-Zip.

```powershell
cd C:\Games\VivaPinataRecomp
$api = 'https://cloud-api.yandex.net/v1/disk/public/resources/download?public_key=' + [uri]::EscapeDataString('https://disk.yandex.ru/d/9lgjQVp7fArEjw')
Invoke-WebRequest (Invoke-RestMethod $api).href -OutFile $env:TEMP\VivaPinata_Russian_v1.zip
(Get-FileHash $env:TEMP\VivaPinata_Russian_v1.zip -Algorithm MD5).Hash   # expected 240C0A660A9673069CA221B569331091
Expand-Archive $env:TEMP\VivaPinata_Russian_v1.zip -DestinationPath . -Force   # -> translation\vp_russian.json
python tools\make_russian_bnl.py
# expected: "...russian.bnl: 11415 strings translated, 0 left in English ..." (about 30 s)
```

Alternatively, with the **PC version** of Viva Piñata (2007) and the ZoG translation installed (`VivaPinata_Rus_Setup.exe`): `python tools\make_russian_bnl.py --pc-ru "<PC>\bundles\english.bnl" --pc-en "<PC>\Install_Rus\backup\bundles\english.bnl"` builds the same file.

Then in the launcher choose **Settings → Game text language → Russian** (RU UI: **Настройки → Язык текста → Русский**) and press **PLAY**. The launcher copies `russian.bnl` over `english.bnl` and `englishus.bnl` (the game reads `englishus.bnl`) and keeps SHA-1-checked `.orig` backups. Choosing English restores them.

### 1.8 Verify

- [ ] `game_files\default.xex` has SHA-1 `130DBBE0…4EDDD3`, and `game_files\Beta\` exists.
- [ ] `out\build\local-win-relwithdebinfo\vivapinata.exe` exists.
- [ ] The launcher shows the green status line.
- [ ] After **PLAY**, the title screen shows grass and "Press START" / «Нажми START».

### 1.9 Troubleshooting

| Symptom | Cause | Fix |
| :-- | :-- | :-- |
| CMake: `ReXGlue SDK not found` or `download failed` | No network, or a partial download | Delete `rexglue\win-amd64\` and configure again |
| Codegen exits with `0xC0000409` | Non-ASCII characters in the project path | Move the project to an ASCII path |
| `Entrypoint XEX not found` / the launcher is red | `game_files` layout is wrong | `default.xex` and `Beta\` must be directly in `game_files\` |
| `Microsoft Visual C/C++ Version differs in precompiled file` | Visual Studio was updated | Delete `out\build\local-win-relwithdebinfo\CMakeFiles\vivapinata_recomp.dir\cmake_pch.hxx.pch` and build again |
| CLI build: `clang++` not found | LLVM is not on PATH, or the component is missing | Add `<VS>\VC\Tools\Llvm\x64\bin` to PATH; install the *C++ Clang tools for Windows* component |
| Visual Studio breaks on `0xC0000005` in `memcpy` under `F5` | The SDK's GPU write-watch (a handled first-chance AV) | Not a crash: uncheck Win32 Exceptions → `0xC0000005` in Exception Settings, or run without the debugger |
| `make_russian_bnl.py`: `cannot build a …-byte text stream` | 7-Zip is not installed | Install 7-Zip; zlib alone is too big for Cyrillic |
| Crash on Russian after hand-editing a `.bnl` | Rare CAFF streams are inflated in place | Use only `tools/make_russian_bnl.py` (it builds exact-size streams) |

---

## 2. Working on the code (development rules)

The maintainer builds and runs everything in Visual Studio. The maintainer's working language is Russian; code, comments and commit messages are in English.

### 2.1 Rules

1. **Do not run builds or code generation from the terminal** (`cmake --build`, `ninja`, `rexglue codegen`, compilers) unless the user explicitly asks. The maintainer presses `F7` / `F5`. Launching the already-built exe to test it is fine.
2. **`generated\` is read-only.** Codegen rewrites it whenever the manifest, any `config\*.toml` or the XEX changes. Fix things in one of two places:
   - `src\game_fixes.h`: strong `extern "C"` symbols replace the weak generated `sub_XXXXXXXX`; recipes are in the file header;
   - `config\*.toml`: function boundaries, hooks, `[[midasm_hook]]` instruction-level hooks.
3. After editing the manifest or `config\*.toml`, run `python tools\validate_manifest.py` before building.
4. Push only when the user asks. Never commit game files or `russian.bnl`.
5. Default settings stay authentic: 30 FPS, `resolution_scale = 1`, D3D12 ROV + readback resolve, no MSAA, XInput.

### 2.2 Map

```
vivapinata_manifest.toml     codegen manifest, includes config/*.toml
config/                      functions, hooks, midasm hooks, native CRT
src/main.cpp                 entry point (single translation unit)
src/vivapinata_app.h         ReXApp: paths, input, fonts, launcher gating (async OnFinalizePaths)
src/launcher.h               launcher: game check (SHA-1), settings -> settings/launcher.toml, text language
src/game_fixes.h             all guest-code fixes (vpkd3d128 sign workaround)
src/game_cvars.h             vp_* settings
src/vp_names.h               engine functions identified from TiP (generated by tools/port_names.py)
src/vp_tools/                VP Tools (F1 / hold Back): engine hooks, value scanner, ImGui menu
settings/                    hardware.toml (SDK config), mapping.toml (input), launcher.toml (written by the launcher)
tools/                       validate_manifest, find_thunk_holes, stub_sweep_to_toml, data_pointers_to_toml, make_russian_bnl,
                             port_names (TiP -> Viva Pinata function names by code and call graph)
assets/launcher/             launcher background and icon (PNG: the SDK image decoder does not read JPEG)
docs/XEX_ANALYSIS.md         XEX header analysis
docs/NAMES_FROM_TIP.md       function-name matches from TiP with grades (generated)
game_files/  rexglue/  generated/  out/     not in git
```

### 2.3 Facts worth knowing

- XEX: Title ID `4D5307F2`, image `0x82000000–0x82B90000`, 19 714 functions in 99 generated units.
- With the SDK's defaults (`user_country` 103), the game language is **englishus**, so the game reads `Beta\bundles\englishus.bnl`.
- **SDK codegen bug:** `vpkd3d128` FLOAT16_4 / FLOAT16_2 with `vD == vB` loses signs. It is worked around with 30 midasm hooks (`config\vivapinata_midasm.toml`, `src\game_fixes.h` section 1). Without them the ground renders white.
- **Rare CAFF files are inflated in place.** A recompressed stream must have exactly the compressed size recorded in stream 0 (offset 29), with no zero padding. See `compress_exact()` in `tools\make_russian_bnl.py`.
- The font cache already has all Cyrillic glyphs. Its ABC table is indexed from U+0001.
- VP Tools: `vp_tools = false` hides the menu; `--vp_tools_trace=true` logs the first calls of the hooked engine functions (`[vp_tools]` lines).
- Testing without the launcher: `vivapinata.exe --vp_show_launcher=false --vp_language=en`. The log shows `Unhandled guest access violation` for guest crashes; `--log_level=debug` shows VFS misses.
