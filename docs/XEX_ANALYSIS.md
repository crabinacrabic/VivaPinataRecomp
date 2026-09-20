# Анализ `game_files/default.xex` — Viva Piñata (2006)

> Дата: 2026-09-20. Источник: разбор заголовка XEX2 (скрипт `xexinfo.ps1`, поля по xenia `xex2_info.h`).
> Образ зашифрован и сжат — тело PE не анализировалось; всё ниже взято из незашифрованной части заголовка.

## 1. Идентификация

| Поле | Значение | Комментарий |
|---|---|---|
| Title ID | **`0x4D5307F2`** | В ТЗ указан `4D5307F1` — это **Fable II** (MS-2033). Опечатка, проект ведём под `4D5307F2` |
| Media ID | `0x690B3287` | |
| Регионы | `0x00FF00FF` | Все регионы (диск «USA, Europe», 16 языков) |
| Версия / базовая | `0.0.0.1` / `0.0.0.1` | Ретейл-диск без title update |
| Диск | 1 из 1 | |
| Имя PE | `PinataParadiseRO.pe` | Внутреннее кодовое имя Rare — «Piñata Paradise», RO = retail optimised |
| Дата линковки | 2006-10-21 00:14:36 UTC | Релиз 2006-11-09 → это финальная мастер-сборка |
| ISO (Redump) | CRC `f3ddf9d3`, MD5 `3902321dfe15d7d2510a96114dba625a` | `Vimm's Lair.txt` |

## 2. Раскладка образа

| Поле | Значение |
|---|---|
| `image_base` / `load_address` | `0x82000000` |
| `image_size` | `0x00B90000` (11,6 МБ) → образ `0x82000000..0x82B90000` |
| `entry_point` | `0x826B8B48` |
| `image_flags` | `0x00000008` |
| TLS | 64 слота, raw `@0x82B09000`, data/raw size `0x0C` |
| Стек по умолчанию | `0x40000` (256 КБ) |
| `system_flags` | `0x600` |
| `title_workspace` | `0x02000001` |
| Формат файла | encryption = **normal (1)**, compression = **normal LZX (2)**, window `0x8000`, block `0xF800` |
| PE data offset | `0x3000`, размер файла 3 244 032 байт |

### 2.1. Результат первого codegen (2026-09-20, SDK 0.10.0.8-dev.g1406e1b)

| Поле | Значение |
|---|---|
| `REX_CODE_BASE` / `REX_CODE_SIZE` | `0x820E0000` / `0x5FFBB8` → код `0x820E0000..0x826DFBB8` (6,0 МБ) |
| Функций | **19 430** (`vivapinata_funcs.h`) |
| Юнитов | 99 × `vivapinata_recomp.N.cpp` (+ 99 `_funcs.N.h`, init/register/pch), 205 файлов, 6,6 с |
| Фазы | Register → Scan → Discover → GapFill → Merge → Validate → Write |
| Validate, прогон №1 | 2 × `UnresolvedCall` (`0x82481D70`, `0x822F12C0`) → добавлены в `config/vivapinata_functions.toml` |
| Write, прогон №2 | 2 × «Unresolved b target» → `REX_FATAL` в коде (`0x82630554` в `recomp.80.cpp`, `0x82672F14` в `recomp.32.cpp`) → добавлены туда же |
| Линковка, прогон №2 | `undefined symbol: PPCImageConfig` — `sources.cmake` не существовал при первом конфигурировании, OBJECT-библиотека `vivapinata_recomp` не создана. Исправлено bootstrap-блоком в `CMakeLists.txt` (codegen при конфигурировании, если `sources.cmake` нет, + `CMAKE_CONFIGURE_DEPENDS`) |

### 2.2. Журнал запусков (F5)

| # | Дошло до | Симптом | Причина | Фикс |
|---|---|---|---|---|
| 1 | XEX загружен, D3D12 (RX 6600, ROV yes), аудио-endpoint, `SetInterruptCallback(82464688, 40001880)` | `[FATAL] Call to invalid or unregistered function at guest address 0x82482EF0` | `0x82482EF0` — 8-байтный MSVC adjustor-thunk (`addi r3,r3,-16; b Method`) в упакованном блоке `0x82482E90..0x82482F08`; на него ссылается только vtable, которую сканер не нашёл | `tools/find_thunk_holes.py` нашёл **42** таких дыры в 7 блоках → все в `config/vivapinata_functions.toml` |
| 2 | то же | `[FATAL] … 0x82527D18`; стек `sub_82528080 → sub_8252C400 → sub_8252E0C8` (recomp.7.cpp:26578, `lwz r11,0(r28); mtctr; bctrl` = vtable[0]) | `sub_82527C90` кончается ровно на `0x82527D18`, 16-байтная дыра до `sub_82527D28` (геттер `lis/ori/blr`) — ещё один метод не найденной vtable | адрес добавлен; проект переведён в **discovery mode**: `dev_debug_runtime = true` → stub sweep пишет все пропущенные адреса в `logs/stub_sweep.txt`, конвертер `tools/stub_sweep_to_toml.py` |
| 3 | + 2 шейдера, 1 пайплайн; stub sweep: **0 попаданий** | `0xC0000005` **first-chance** запись `0x1_FB126000` в `sub_8269A200` (= CRT `memcpy`), вызов из `sub_82538BF0` (D3D Lock → memcpy 120 → Unlock); после Continue — следующая страница `0x1FAED9000`, `0x1FAF00000`, … | **Не баг.** Страница читается (`vsdbg_memory`), защищена только от записи: GPU write-watch SDK, обрабатывается его же VEH. VS по умолчанию останавливается на каждом first-chance `0xC0000005` | Снять галку `0xC0000005` в Параметрах исключений VS (Ctrl+Alt+E → Win32 Exceptions) или запускать Ctrl+F5. KB: `ERROR_LOOKUP_TABLE.md` #7/#8, `ROOT_INDEX.json` |

Попутно найдено для `[rexcrt]`: **`memcpy = 0x8269A200`** (сигнатура `std r3,-8(r1); dcbt r0,r4; … ldu/stdu`), `__savegprlr_*` в `0x8269A040/48`.

`src/game_constants.h` берёт `kCodeBase`/`kCodeEnd` из макросов pch автоматически. Для сравнения: AO2 (UE3, 2008) — 62 467 функций, код 0xE8C604; Viva Piñata втрое компактнее.

Для сравнения: XEX Army of Two тоже encrypted+LZX — SDK-загрузчик расшифровывает сам, codegen на нём отработал (62 467 функций, 0 ошибок). Для Viva Piñata дополнительных инструментов не требуется.

## 3. Импорты и статические библиотеки

Импорт-библиотеки (2): `xboxkrnl.exe` — **276** импортов, `xam.xex` — **170** импортов, обе версии `2.0.3529.0`. Игровых DLL нет → секция `[[modules]]` в манифесте не нужна, `--scan-dll` не требуется.

Статические библиотеки XDK **2.0.3529** (одна из самых ранних ретейл-версий XDK, осень 2006):

```
D3DX9  XGRAPHC  XONLINE  XUIHTM  XUIVIDE(16385)  XHV  XMEDIA  LIBCPMT
XAPILIB  XBOXKRNL  D3D9LTCG  XAUDLTCG  XUIRUNL  XUIRNDRL  X3DAUDL  XACTLTCG
```

Что это означает для рекомпиляции:

- **D3D9LTCG** — D3D-слой слинкован статически (LTCG). Весь Xenos-драйвер живёт в образе; ошибки класса «ring buffer / interrupt callback» (AO2 `sub_82A4FCE0`, `sub_82A50FF0`) возможны, но адреса будут свои.
- **XUIRUNL / XUIRNDRL / XUIHTM / XUIVIDE** — UI на XUI (как в TiP: хуки `XuiProcessInput`, `CXuiModule::ProcessInput`).
- **XHV / XONLINE** — голосовой чат и Live; при отсутствии сети ожидаемы «заглушки» в XAM.
- **XMEDIA / XUIVIDE** — видеоплеер (папка `Beta\movie\`, 67 МБ файлы = интро/ролики). Кандидат №1 на skip-хук, если рендер роликов не заведётся.
- **XACTLTCG / XAUDLTCG / X3DAUDL** — XACT-аудио (`Beta\xwavebank\`, `xwavebankloc\`).
- **LIBCPMT** — C++ runtime с исключениями: `generate_exception_handlers = false` на первом прогоне, включать только при ошибках EH-валидации.
- XDK 2.0.3529 старше, чем у AO2 (2008) и TiP (2008): нельзя переносить адреса CRT/XAPI из соседних проектов даже приблизительно. Для Ghidra: `tomcl7/ghidra-fidb-xenonsdk` содержит FIDB под ранние XDK — единственный быстрый способ получить имена `memcpy`/`RtlAllocateHeap`/`Sleep`.

## 4. Данные игры (извлечённый ISO → `game_files/`)

```
game_files/
├── default.xex                  3,2 МБ
├── gardensave.png, settingssave.png   иконки сохранений
├── $SystemUpdate/               su20076000 (не нужен)
└── Beta/                        корень данных Rare (guest-путь game:\Beta\...)
    ├── bundles/                 <lang>.bnl — локализация (17 языков)
    ├── bundles_packages/        615 × N.pkg + packageLut.plt (LUT пакетов), packageLut_dlc_1.plt
    ├── debug/                   db_index.txt (39 290 строк "aid_<asset> <hash> <ver>"), debug_pack.bin 775 МБ, debug_hash.bin
    ├── font/, fontcacheabc/, fontcachesbm/   шрифты и кэши глифов (hex-имена)
    ├── movie/                   ролики (hex-имена, до 153 МБ)
    ├── vincedata/               metafont_<lang>.ini
    └── xwavebank/, xwavebankloc/  XACT wave banks
```

`db_index.txt` — индекс имён ассетов → hash. Это не символы функций, но пригодится для хуков менеджера ассетов (в TiP: `assetManOpen`, `meLoadAsset`, `meInitModel`): по hash из лога можно восстановить имя ассета.

## 5. Эталон TiP-Recomp — что применимо, что нет

`C:\Recompiles\reference_repos\viva_pinata_recomp` = **SolarCookies/TiP-Recomp** — рекомп **Viva Piñata: Trouble in Paradise (2008)**, SDK 0.8.1. Тот же движок Rare, та же структура данных, но **другой XEX**.

| Применимо (архитектура) | НЕ применимо (адреса) |
|---|---|
| Раздельные `config/*.toml` (ctx / hooks / midasm / crt / variables) — воспроизведено | `setjmp_address = 0x82AE2EA0`, `longjmp_address = 0x82AE2A60` |
| Именование хуков `rex_<subsystem><Func>_<addr>` | Все 30 записей `#CTX.CTR.U32` в `retip_ctx.toml` |
| `timeBeginPeriod(1)` в `OnPostSetup` — воспроизведено (`vp_high_res_timer`) | Все `[rexcrt]` адреса (`RtlAllocateHeap = 0x82B089F0`, …) |
| Идея гибридного Sleep (`SleepHooks.h`) — порт в `src/game_timing.h` | Все `[[midasm_hook]]` адреса (`vsync_hook @0x8229B71C`, …) |
| Список подсистем для хуков: appMainDraw, camMainGetAspectRatio, XUsbcam*, XuiProcessInput | Адреса `retip_hooks.toml` |
| Заглушки `XUsbcam*` (Live Vision camera) | |

## 6. Риски и блокеры перед первым codegen

1. **`ñ` в пути проекта — блокер.** `rexglue.exe` (v0.10.0.8) аварийно завершается с `0xC0000409` (STATUS_STACK_BUFFER_OVERRUN) в любом пути с не-ASCII символом (проверено изолированно: `…\vp_ñ_test\` падает, `…\vp_init\` с тем же XEX работает). Codegen из Visual Studio использует тот же бинарь → упадёт так же. Папку нужно переименовать в ASCII (например, `C:\Recompiles\VivaPinata_xbox360`). Все файлы проекта путенезависимы, переименование безопасно.
2. **Title ID** — везде использовать `4D5307F2` (xenia-патчи, achievements, save-ID).
3. **Ранний XDK 2.0.3529** — реализация XAM/kernel-экспортов в SDK ориентирована на более поздние ординалы; возможны `unimplemented export` в логе на старте. Это не ошибка codegen — решается заглушками в `src/game_fixes.h`.
4. **Огромные ролики в `Beta\movie\`** — если XMEDIA-декодер SDK их не примет, первый видимый кадр будет после skip-хука интро (как `SkipIntroVideos_hook` в TiP).
5. В корне проекта лежат ISO (7,8 ГБ) и 7z (5,8 ГБ) — они в `.gitignore`, но замедляют индексацию VS; лучше вынести из корня.
