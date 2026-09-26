<div align="center">

<img src="docs/images/icon.png" width="96" alt="Viva Piñata icon">

# Viva Piñata Recomp

**Статическая рекомпиляция Viva Piñata (Xbox 360, 2006) в нативную программу для Windows**

[![Platform](https://img.shields.io/badge/platform-Windows%20x64-0078D6?logo=windows&logoColor=white)](#требования)
[![Title ID](https://img.shields.io/badge/Title%20ID-4D5307F2-107C10?logo=xbox&logoColor=white)](docs/XEX_ANALYSIS.md)
[![ReXGlue SDK](https://img.shields.io/badge/ReXGlue%20SDK-v0.10.0.8-8A2BE2)](https://github.com/rexglue/rexglue-sdk)
[![Renderer](https://img.shields.io/badge/render-Direct3D%2012-blue)](#настройки)
[![C++23](https://img.shields.io/badge/C%2B%2B-23-00599C?logo=cplusplus&logoColor=white)](CMakeLists.txt)
[![Status](https://img.shields.io/badge/status-playable-2ea44f)](#статус)

<img src="docs/images/title_screen.jpg" width="49%" alt="Титульный экран"> <img src="docs/images/garden.jpg" width="49%" alt="Сад">

</div>

---

## О проекте

Это не эмулятор. Код игры для процессора Xbox 360 (PowerPC, Xenon) переведён в C++ с помощью [ReXGlue SDK](https://github.com/rexglue/rexglue-sdk) и собран как обычная 64-битная программа для Windows. Процессорный код выполняется напрямую, а графику Xenos отрисовывает бэкенд Direct3D 12, унаследованный от Xenia.

Файлы игры в репозиторий не входят. Для запуска нужна собственная копия игры.

| | |
| :-- | :-- |
| **Игра** | Viva Piñata (USA, Europe), Rare / Microsoft, 2006 |
| **Title ID** | `4D5307F2` |
| **XEX** | `default.xex`, XDK 2.0.3529, образ `0x82000000..0x82B90000` |
| **Рекомпилировано** | 19 714 функций в 99 файлах C++ |
| **SDK** | ReXGlue v0.10.0.8-dev.g1406e1b |
| **Сборка** | Visual Studio 2026, Clang (ClangCL), C++23, CMake + Ninja |

Подробный разбор XEX: [docs/XEX_ANALYSIS.md](docs/XEX_ANALYSIS.md).

## Статус

| | |
| :-- | :-- |
| ✅ | Запуск, меню, титульный экран |
| ✅ | Сад и игровой процесс, земля и трава отрисовываются так же, как в Xenia Canary |
| ✅ | Звук, геймпад XInput |
| ✅ | Лаунчер на русском: проверка версии игры, настройки графики, кнопка «Играть» |
| 🚧 | Пропуск вступительных роликов и снятие ограничения 30 FPS: настройки `vp_skip_intro_videos` и `vp_fps_unlock` заведены, хуков пока нет |
| 🚧 | Только Direct3D 12: в готовой сборке SDK Vulkan выключен |

## Главные исправления

| Проблема | Причина | Решение |
| :-- | :-- | :-- |
| **Белая земля** на титульном экране и в саду | Ошибка генератора кода ReXGlue: `vpkd3d128` (упаковка float → float16), если регистр назначения совпадает с источником, читает знак из уже затёртого места. Все отрицательные координаты сетки ландшафта становились положительными, и 3/4 сада не рисовались | 30 mid-asm хуков с правильной упаковкой: [`src/game_fixes.h`](src/game_fixes.h), [`config/vivapinata_midasm.toml`](config/vivapinata_midasm.toml). Ошибка передана авторам SDK |
| **Вылеты** `Call to invalid or unregistered function` | Сканер SDK не находит методы, доступные только через таблицы виртуальных функций (adjustor thunks, маленькие геттеры) | Границы функций вручную в [`config/vivapinata_functions.toml`](config/vivapinata_functions.toml), поиск: [`tools/find_thunk_holes.py`](tools/find_thunk_holes.py) |

## Требования

- Windows 10 или 11, x64
- Видеокарта с поддержкой Direct3D 12 (проверено на AMD Radeon RX 6600)
- [Visual Studio 2026](https://visualstudio.microsoft.com/) с нагрузкой «Разработка классических приложений на C++» и компонентом **C++ Clang tools for Windows**; CMake и Ninja входят в Visual Studio
- Своя копия **Viva Piñata (USA, Europe)** для Xbox 360, образ диска (точная версия ниже, в разделе [Какая версия игры нужна](#какая-версия-игры-нужна)), и [extract-xiso](https://github.com/XboxDev/extract-xiso)
- Python 3, только для вспомогательных скриптов из `tools/`

## Какая версия игры нужна

Проект собран и проверен **только** на этой версии диска:

| | |
| :-- | :-- |
| **Диск (Redump)** | `Viva Pinata (USA, Europe) (En,Ja,Fr,De,Es,It,Nl,Pt,Sv,No,Zh,Ko,Pl,Cs,Hu,Sk)` |
| **Title ID** | `4D5307F2` |
| **Media ID** | `690B3287` |
| **Версия** | `0.0.0.1`, оригинальный диск; обновление (title update) не нужно |
| **ISO** | CRC32 `f3ddf9d3`, MD5 `3902321dfe15d7d2510a96114dba625a` |
| **`default.xex`** | SHA-1 `130dbbe05328eeca23ab830bc8ed3337064eddd3` |

Проверить распакованный `default.xex` можно встроенной командой Windows:

```bash
certutil -hashfile game_files\default.xex SHA1
```

> [!IMPORTANT]
> Рекомпилированный код привязан к адресам именно этого `default.xex`. Другие издания (переиздания, цифровая версия Games on Demand) не проверялись и, скорее всего, не запустятся.
>
> Это другие игры, они не подойдут: **Viva Piñata: Trouble in Paradise** (для неё есть свой проект [TiP-Recomp](https://github.com/SolarCookies/TiP-Recomp)) и **Viva Piñata: Party Animals**. Бонусный диск не нужен.

## Сборка

1. **Клонируйте репозиторий** в папку, путь к которой состоит только из латиницы. `rexglue.exe` падает на путях с символами вроде `ñ`.

   ```bash
   git clone https://github.com/crabinacrabic/VivaPinataRecomp.git C:/Recompiles/VivaPinata_xbox360
   ```

2. **Распакуйте игру** в `game_files/`. Должны появиться `game_files/default.xex` и папка `game_files/Beta/`.

   ```bash
   extract-xiso -x -d game_files "Viva Pinata (USA, Europe).iso"
   ```

3. **SDK.** Если папки `rexglue/win-amd64/` нет, [`cmake/fetch-rexglue-sdk.cmake`](cmake/fetch-rexglue-sdk.cmake) скачает SDK с GitHub при первой настройке CMake.

4. **Откройте папку в Visual Studio** («Открыть локальную папку»), выберите конфигурацию **`local-win-relwithdebinfo`** и нажмите **F7**. Первая сборка сначала запускает генерацию кода (`rexglue codegen`, несколько минут), затем компилирует около сотни сгенерированных файлов.

## Запуск

Двойной щелчок по **`run_game.bat`**, или запустите `out/build/local-win-relwithdebinfo/vivapinata.exe`.

При запуске открывается **лаунчер**:
- Он проверяет, что файлы игры на месте и версия `default.xex` подходит.
- Кнопка **Настройки** открывает полноэкранный режим, вертикальную синхронизацию, разрешение рендера и режим рендера. Всё выбранное применяется сразу при нажатии **Играть**.
- Кнопка **Играть** (или `Enter`) запускает игру.
- Если снять галку «Показывать при запуске», игра будет стартовать сразу. Вернуть лаунчер можно так: запустите `vivapinata.exe --vp_show_launcher=true` один раз и включите галку снова, или удалите `settings/launcher.toml`.

- Настройки читаются из `settings/`, файлы игры из `game_files/`.
- Логи пишутся в `logs/` рядом с exe.

> [!TIP]
> Запускаете из Visual Studio под отладчиком (**F5**)? Один раз отключите остановку на `0xC0000005`: **Отладка → Окна → Параметры исключений** (Ctrl+Alt+E) → Win32 Exceptions → снимите галку **Access violation**.
>
> Это не падения. SDK защищает от записи память, переданную видеокарте, и сам обрабатывает такие обращения. Без отладчика (**Ctrl+F5**) этого делать не нужно.

## Управление

Игра рассчитана на геймпад: курсор на левом стике, действия на кнопках. Геймпад Xbox работает сразу. Клавиатура и мышь эмулируют геймпад, мышь управляет камерой (правый стик).

| Геймпад | Клавиатура | | Геймпад | Клавиатура |
| :-- | :-- | :-- | :-- | :-- |
| Левый стик (курсор) | `W` `A` `S` `D` | | **A** | `Space` |
| Правый стик (камера) | стрелки, мышь | | **B** | `Backspace` |
| Крестовина | `Shift` + стрелки | | **X** | `L` |
| **LT / RT** | `Q` / `E` | | **Y** | `P` |
| **LB / RB** | `1` / `3` | | **Start** | `Enter` |
| **L3 / R3** | `F` / `K` | | **Back** | `Tab` |

Клавиши переназначаются в [`settings/mapping.toml`](settings/mapping.toml).

## Настройки

Основной файл: [`settings/hardware.toml`](settings/hardware.toml). Там собраны настройки графики, окна и игры.

Лаунчер сохраняет свой выбор в `settings/launcher.toml`. Этот файл загружается после `hardware.toml`, поэтому его значения важнее. Удалите его, чтобы вернуться к `hardware.toml`.

| Ключ | Что делает |
| :-- | :-- |
| `window_width`, `window_height`, `fullscreen` | Размер окна и полноэкранный режим (родное разрешение игры 1280×720) |
| `resolution_scale` | Масштаб внутреннего разрешения |
| `vsync` | Вертикальная синхронизация |
| `render_target_path_d3d12` | Путь рендер-таргетов: `rov` (точнее) или `rtv` (быстрее) |
| `vp_high_res_timer` | Точный системный таймер (1 мс), включён по умолчанию |
| `vp_show_launcher` | Показывать лаунчер при запуске |
| `vp_skip_intro_videos`, `vp_fps_unlock` | Зарезервированы, пока не действуют |

## Структура проекта

```
vivapinata_manifest.toml   манифест codegen, подключает config/*.toml
config/                    границы функций, хуки, mid-asm хуки, нативный CRT
src/
  main.cpp                 точка входа
  vivapinata_app.h         приложение ReXApp: пути, ввод, таймер, запуск лаунчера
  launcher.h               лаунчер: проверка игры, настройки, кнопка «Играть»
  game_fixes.h             все исправления гостевого кода
  game_cvars.h             настройки vp_*
settings/                  hardware.toml, mapping.toml, база геймпадов SDL
assets/launcher/           фон и иконка лаунчера
tools/                     вспомогательные скрипты на Python
docs/                      разбор XEX, картинки для README
game_files/                файлы игры (не в репозитории)
rexglue/                   ReXGlue SDK (не в репозитории)
generated/                 код от codegen (не в репозитории, только чтение)
```

## Для разработчиков

**Правила проекта (для людей и ИИ-агентов):**

1. **`generated/` только для чтения.** Сборка перезапускает codegen при изменении манифеста, любого `config/*.toml` или XEX и перезаписывает каталог. Исправления вносятся только двумя способами:
   - сильными символами в `src/game_fixes.h` поверх слабых `sub_*`;
   - записями в `config/*.toml`, включая `[[midasm_hook]]`.
2. **Сборка и codegen запускаются только из Visual Studio** (F7 / F5), не из терминала.
3. Общение и отчёты на русском, код и комментарии на английском.
4. Ретро-стандарт по умолчанию: 30 FPS, `resolution_scale = 1`, D3D12 ROV + readback resolve, без MSAA, XInput.

**Перед F7**, если менялись манифест или `config/*.toml`:

```bash
python tools/validate_manifest.py
```

Скрипт повторяет правила слияния конфигов SDK и проверяет ключи, адреса и выравнивание. Код выхода: 0 — всё чисто, 2 — только предупреждения, 1 — ошибки (codegen упадёт).

| Скрипт | Назначение |
| :-- | :-- |
| `tools/validate_manifest.py` | Проверка манифеста и `config/*.toml` перед сборкой |
| `tools/find_thunk_holes.py` | Поиск пропущенных сканером adjustor thunks (после каждого codegen) |
| `tools/stub_sweep_to_toml.py` | Разбор `stub_sweep.txt` (режим `dev_debug_runtime = true`) в записи для `config/` |
| `tools/data_pointers_to_toml.py` | Функции из таблиц указателей (vtables) в `[functions]` |

<details>
<summary><b>Решение проблем при сборке</b></summary>

- **`Microsoft Visual C/C++ Version differs in precompiled file`** после обновления Visual Studio: удалите `out/build/<конфигурация>/CMakeFiles/vivapinata_recomp.dir/cmake_pch.hxx.pch` и соберите заново.
- **Codegen падает с `0xC0000409`**: в пути к проекту есть не-ASCII символы.
- **`Call to invalid or unregistered function at 0x82XXXXXX`**: добавьте `0x82XXXXXX = {}` в `config/vivapinata_functions.toml` и соберите заново. Codegen перезапустится сам.

</details>

## Благодарности

- [ReXGlue SDK](https://github.com/rexglue/rexglue-sdk): рекомпилятор и среда выполнения
- [Xenia](https://github.com/xenia-project/xenia) и [Xenia Canary](https://github.com/xenia-canary/xenia-canary): графический бэкенд и эталон для сравнения
- [TiP-Recomp](https://github.com/SolarCookies/TiP-Recomp) (Viva Piñata: Trouble in Paradise): образец архитектуры проекта
- Rare: за замечательную игру

## Правовая информация

Проект не связан с Microsoft и Rare и не одобрен ими. Репозиторий не содержит файлов игры. Для запуска нужна собственная законно полученная копия. Viva Piñata является товарным знаком Microsoft Corporation.
