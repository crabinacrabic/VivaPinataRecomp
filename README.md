# vivapinata — статическая рекомпиляция Viva Piñata (Xbox 360, 2006)

| | |
|---|---|
| Title ID | `4D5307F2` (US/EU мультирегион; `4D5307F1` из ТЗ — это Fable II) |
| XEX | `game_files/default.xex`, `PinataParadiseRO.pe`, XDK 2.0.3529, образ `0x82000000..0x82B90000`, entry `0x826B8B48` |
| SDK | ReXGlue **v0.10.0.8-dev.g1406e1b** (`rexglue/win-amd64`, копия из ArmyOfTwoRE2008) |
| Toolchain | Visual Studio 18 2026, ClangCL, C++23 (как AO2) |
| База знаний | `C:\Recompiles\Knowlage_BASE` (`SKILL.md`, `ROOT_INDEX.json`, `CROSS_INSPECTION_AO2.md`) |
| Эталон | `C:\Recompiles\reference_repos\viva_pinata_recomp` = TiP-Recomp (**сиквел 2008**, адреса не переносятся) |

Подробное техзаключение по XEX: [docs/XEX_ANALYSIS.md](docs/XEX_ANALYSIS.md).

## Железные правила

1. **Никаких сборок из терминала** — ни `cmake --build`, ни `ninja`, ни `cl`, ни `rexglue codegen`. Сборку (и codegen, который она запускает) делает человек в Visual Studio: **F7** — сборка, **F5** — запуск.
2. **`generated/` — только чтение.** Codegen перезапускается сборкой при изменении манифеста, любого `config/*.toml` или XEX и стирает каталог. Все хуки — в `src/game_fixes.h` (сильные символы поверх слабых `sub_*`) или в таблицах `config/*.toml`.
3. Отчёты и общение — по-русски, код и комментарии — по-английски.
4. Retro-стандарт: 30 FPS, `resolution_scale = 1`, ROV D3D12 + readback resolve, MSAA off, XInput.

## Структура

```
vivapinata_manifest.toml     [project]/[entrypoint] + includes → config/*.toml
config/
  vivapinata_ctx.toml        флаги локализации регистров (все false) + [analysis]
  vivapinata_functions.toml  ручные границы функций (из UnresolvedCall)
  vivapinata_hooks.toml      [functions] с name → символы rex_* для хуков
  vivapinata_midasm.toml     [[midasm_hook]]
  vivapinata_crt.toml        [rexcrt] нативный CRT (пусто на первом прогоне)
CMakeLists.txt               project(vivapinata), fetch SDK, rexglue_setup_target(... GPU_PLUGINS xenos)
CMakePresets.json            от `rexglue init` (Ninja + clang)
CMakeUserPresets.json        CMAKE_PREFIX_PATH = rexglue/win-amd64
cmake/fetch-rexglue-sdk.cmake
generated/rexglue.cmake      SDK-glue от `rexglue init` — READ-ONLY
generated/default/           появится после первого codegen — READ-ONLY
src/
  main.cpp                   roundeven-шимы, init.h, game_fixes.h, app, REX_DEFINE_APP
  vivapinata_app.h           ReXApp: пути, XInput, precommit алиасов, timeBeginPeriod, debug tools
  game_fixes.h               ВСЕ переопределения гостевых функций (включается один раз)
  game_cvars.h               vp_* cvars + graphics_backend + dev_debug_runtime
  game_constants.h           Title/Media ID, image base/size/entry, kCodeBase из pch
  game_timing.h              timeBeginPeriod, SpinBackoff, PreciseSleep (порт TiP SleepHooks)
  debug_tools.h              stub sweep / missing-function scan
  utils.h                    RepoRoot(), SettingsDir(), LoadSettingsFiles()
  vivapinata.rc / .ico / winresrc.h
settings/hardware.toml       основной конфиг SDK (GPU, окно, retro-настройки, vp_* cvars)
settings/mapping.toml        ввод (XInput + клавиатура/мышь)
game_files/                  извлечённый ISO: default.xex + Beta\ (5,2 ГБ, в .gitignore)
docs/XEX_ANALYSIS.md         заключение по XEX
tools/validate_manifest.py   pre-flight проверка цепочки манифеста (запускать перед F7)
tools/find_thunk_holes.py    пропущенные сканером MSVC adjustor-thunk-и (после каждого codegen; --toml для вставки)
```

## Перед каждым F7, если менялись манифест или config/*.toml

```bash
python tools/validate_manifest.py
```

Эмулирует merge-семантику SDK (`config.h`: скаляры last-wins, таблицы аддитивно,
`[[midasm_hook]]` дедуп по address), проверяет схему ключей, выравнивание и
попадание адресов в образ, size/end, группу heap в `[rexcrt]`, пару
setjmp/longjmp, уникальность имён. Exit 0 — чисто, 2 — только предупреждения,
1 — ошибки (codegen упадёт).

## Первый codegen — пошагово

0. **Переименовать папку проекта в ASCII** (например `C:\Recompiles\VivaPinata_xbox360`): `rexglue.exe` падает (`0xC0000409`) на `ñ` в пути, codegen из VS упадёт так же. Заодно вынести ISO/7z из корня.
1. Открыть папку проекта в Visual Studio 2026 («Open a local folder» → CMake-проект). Выбрать конфигурацию **`local-win-relwithdebinfo`** (или `local-win-release`) из `CMakeUserPresets.json`. Проверить в Output → CMake строку `Found ReXGlue SDK 0.10.0.8-dev.g1406e1b at .../rexglue/win-amd64/lib/cmake/rexglue`.
2. **F7.** Первый прогон запускает цель `vivapinata_codegen` → `rexglue codegen vivapinata_manifest.toml`. Ожидать несколько минут; в Output смотреть `Validate phase`. Ошибки `UnresolvedCall 0x82XXXXXX` → добавить `0x82XXXXXX = {}` в `config/vivapinata_functions.toml` → снова F7 (codegen перезапустится сам по stamp).
3. После успешного codegen проверить `generated/default/`: `vivapinata_pch.h` (`REX_CODE_BASE`/`REX_CODE_SIZE`), `sources.cmake`, число `vivapinata_recomp.N.cpp`, `codegen.partition.json`. Записать в `docs/XEX_ANALYSIS.md` §2: code base/size, число функций, число unit-файлов.
4. Дать сборке дойти до линковки `vivapinata.exe`. Ошибки линковки вида `unresolved __imp__X…` (XAM-экспорты старого XDK) — фиксировать в `src/game_fixes.h` через `REX_STUB`/`REX_STUB_RETURN`.
5. **Перед первым F5 — один раз:** Отладка → Окна → Параметры исключений (Ctrl+Alt+E) → Win32 Exceptions → снять галку **`0xC0000005 Access violation`**. SDK write-protect'ит физические страницы, залитые в GPU, и штатно ловит запись в них своим обработчиком; с галкой VS останавливается на каждой такой странице (выглядит как падение в `memcpy`). Без отладчика — Ctrl+F5.
6. **F5.** Смотреть `out/build/<preset>/logs/vivapinata_NNN.log` (`ls -t`): загрузка XEX, монтирование `game:`, первые `NtCreateFile game:\Beta\...`, `Call to invalid or unregistered function` → адрес в `vivapinata_functions.toml`. При зависаниях/чёрном экране — сначала конфиг-эксперименты (`render_target_path_d3d12 = "rtv"`, `vsync = false`), потом код.
7. Каждую найденную проблему — в KB: `ERROR_LOOKUP_TABLE.md` + `ROOT_INDEX.json` (новый раздел под `4D5307F2`).

## Куда дальше (после первого запуска)

- Ghidra + `tomcl7/ghidra-fidb-xenonsdk` (XDK 2.0.3529) → имена CRT/XAPI → `config/vivapinata_crt.toml` (memcpy/memset → file I/O → heap-группа целиком) и `setjmp/longjmp` в манифест.
- Хуки по списку TiP-Recomp: `XUsbcam*` заглушки, интро-ролики, `vsync_hook` (interval r10), aspect ratio, cursor/mouse.
- Только затем — флаги `config/vivapinata_ctx.toml` по одному (`skip_lr` → `skip_msr` → …) с полным прогоном после каждого.
