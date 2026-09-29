<div align="center">

<img src="docs/images/icon.png" width="96" alt="Иконка Viva Piñata">

# Viva Piñata Recomp

**Viva Piñata (Xbox 360, 2006) как обычная программа для Windows**

[English](README.md) · **Русский**

[![Platform](https://img.shields.io/badge/platform-Windows%20x64-0078D6?logo=windows&logoColor=white)](#что-нужно)
[![Title ID](https://img.shields.io/badge/Title%20ID-4D5307F2-107C10?logo=xbox&logoColor=white)](#какая-версия-игры-нужна)
[![ReXGlue SDK](https://img.shields.io/badge/ReXGlue%20SDK-v0.10.0.8-8A2BE2)](https://github.com/rexglue/rexglue-sdk)
[![Renderer](https://img.shields.io/badge/render-Direct3D%2012-blue)](#настройки)
[![Status](https://img.shields.io/badge/status-playable-2ea44f)](#что-работает)
[![Languages](https://img.shields.io/badge/text-English%20%7C%20Русский-orange)](#русский-язык)

<img src="docs/images/title_screen.jpg" width="49%" alt="Титульный экран"> <img src="docs/images/garden.jpg" width="49%" alt="Сад">

</div>

---

## Что это

Оригинальная игра с Xbox 360, превращённая в обычную программу для Windows. Это **не эмулятор**: код игры переведён в C++ с помощью [ReXGlue SDK](https://github.com/rexglue/rexglue-sdk) («статическая рекомпиляция»). Есть лаунчер и, по желанию, русский язык.

> [!IMPORTANT]
> **Файлов игры здесь нет.** Нужна своя копия игры, причём строго определённой версии (см. ниже).

## Что нужно

- Компьютер с Windows 10 или 11 (64-бит) и видеокартой с DirectX 12.
- Около **25 ГБ** свободного места: Visual Studio ~12 ГБ, игра ~5 ГБ, образ диска ~8 ГБ на время распаковки.
- Свой образ диска (`.iso`) **Viva Pinata (USA, Europe)** для Xbox 360, именно той версии, что указана ниже.

## Какая версия игры нужна

| | |
| :-- | :-- |
| **Диск (название по Redump)** | `Viva Pinata (USA, Europe) (En,Ja,Fr,De,Es,It,Nl,Pt,Sv,No,Zh,Ko,Pl,Cs,Hu,Sk)` |
| **Title ID / Media ID** | `4D5307F2` / `690B3287` |
| **Версия** | `0.0.0.1`, оригинальный диск, обновление не нужно |
| **Контрольная сумма ISO** | MD5 `3902321dfe15d7d2510a96114dba625a`, CRC32 `f3ddf9d3` |

Другие издания не проверялись и, скорее всего, не запустятся. *Viva Piñata: Trouble in Paradise* и *Viva Piñata: Party Animals* — это другие игры. Бонусный диск не нужен.

## Самый простой способ: пусть установит ИИ-агент

1. Поставьте ИИ-помощника, который умеет выполнять команды на компьютере, например [Claude Code](https://claude.com/claude-code), OpenAI Codex или Cursor.
2. Вставьте ему это сообщение, подставив настоящий путь к своему ISO:

   > Установи Viva Piñata Recomp на этот компьютер: https://github.com/crabinacrabic/VivaPinataRecomp — действуй по файлу AGENTS.md из этого репозитория. Образ диска игры лежит тут: `C:\путь\к\Viva Pinata.iso`. Русский язык: не нужен / нужен.

3. Агент сам всё скачает и проверит. Когда он попросит, пройдите установщик Visual Studio и нажмите **Сборка** в Visual Studio.
4. Когда он закончит, запустите **`run_game.bat`** в папке проекта.

## Сам по шагам (6 шагов)

1. **Установите программы:**
   - [Visual Studio 2026 Community](https://visualstudio.microsoft.com/) (бесплатная) с нагрузкой **«Разработка классических приложений на C++»** и компонентом **C++ Clang tools for Windows**;
   - [Git](https://git-scm.com/).
2. **Скачайте проект.** Путь к папке должен состоять только из латинских букв:
   ```bash
   git clone https://github.com/crabinacrabic/VivaPinataRecomp.git C:\Games\VivaPinataRecomp
   ```
3. **Распакуйте игру** программой [extract-xiso](https://github.com/XboxDev/extract-xiso/releases/latest) (файл `extract-xiso-Win64_Release.zip`):
   ```bash
   extract-xiso -x -d C:\Games\VivaPinataRecomp\game_files "C:\путь\к\Viva Pinata (USA, Europe).iso"
   ```
   После этого в `game_files` должны быть `default.xex` и папка `Beta`.
4. **Соберите игру.**
   - В Visual Studio: **Файл → Открыть → Папка…** → выберите `C:\Games\VivaPinataRecomp`.
   - Дождитесь в окне «Вывод» сообщения о завершении генерации CMake. В первый раз это займёт несколько минут: скачивается SDK и переводится код игры.
   - На панели инструментов выберите конфигурацию **`local-win-relwithdebinfo`**.
   - Нажмите **Сборка → Собрать все** (`F7`).
5. **Запустите:** двойной щелчок по **`run_game.bat`**.
6. **В лаунчере** нажмите **ИГРАТЬ** (или `Enter`). Зелёная строка статуса значит, что игра найдена и версия подходит.
   - В **Настройках** выбираются язык текста игры, полноэкранный режим, вертикальная синхронизация, разрешение и режим рендера, а также язык лаунчера.
   - Лаунчер бывает на русском и английском и по умолчанию следует языку Windows. Кнопка **EN / RU** в правом верхнем углу переключает его.

## Русский язык

На диске Xbox 360 русского нет. Проект переносит на него любительский перевод **ПК-версии** от **ZoG Team** ([zoneofgames.ru](https://www.zoneofgames.ru/)) с разрешения команды. Сам перевод в репозитории не хранится.

1. Скачайте **[VivaPinata_Russian_v1.zip](https://disk.yandex.ru/d/9lgjQVp7fArEjw)** (Яндекс Диск, 255 КБ). В нём только русский текст, файлов игры нет.
2. Распакуйте его в папку проекта, чтобы появился файл `translation\vp_russian.json`.
3. Установите [Python 3](https://www.python.org/) и [7-Zip](https://www.7-zip.org/), затем выполните в папке проекта:
   ```bash
   python tools/make_russian_bnl.py
   ```
4. В лаунчере выберите **Настройки → Язык текста → Русский**.

Если у вас есть ПК-версия с установленным переводом ZoG, тот же файл собирается и из неё: `python tools/make_russian_bnl.py --pc-ru "<ПК-игра>/bundles/english.bnl" --pc-en "<ПК-игра>/Install_Rus/backup/bundles/english.bnl"`.

Переведено 12 446 строк из 12 460, на английском остаются только титры. Вернуться к английскому можно в любой момент.

## Управление

Игра рассчитана на геймпад Xbox, он работает сразу. Можно играть и с клавиатуры и мыши:

| Геймпад | Клавиатура | | Геймпад | Клавиатура |
| :-- | :-- | :-- | :-- | :-- |
| Левый стик (курсор) | `W` `A` `S` `D` | | **A** | `Space` |
| Правый стик (камера) | стрелки, мышь | | **B** | `Backspace` |
| Крестовина | `Shift` + стрелки | | **X** | `L` |
| **LT / RT** | `Q` / `E` | | **Y** | `P` |
| **LB / RB** | `1` / `3` | | **Start** | `Enter` |
| **L3 / R3** | `F` / `K` | | **Back** | `Tab` |

Клавиши меняются в [`settings/mapping.toml`](settings/mapping.toml).

## Настройки

Основное есть в лаунчере, остальное в [`settings/hardware.toml`](settings/hardware.toml). Лаунчер сохраняет свой выбор в `settings/launcher.toml`; удалите этот файл, чтобы сбросить настройки лаунчера.

## Что работает

| | |
| :-- | :-- |
| ✅ | Меню, титульный экран, сад и вся игра |
| ✅ | Звук, геймпад Xbox, клавиатура и мышь |
| ✅ | Лаунчер на русском и английском, с проверкой версии игры и настройками графики |
| ✅ | Русский текст (перевод ZoG Team) |
| 🚧 | Пропуск роликов и снятие ограничения 30 FPS пока не сделаны |
| 🚧 | Только Direct3D 12 |

## Если что-то пошло не так

| Проблема | Что делать |
| :-- | :-- |
| В лаунчере красная строка | Нет файлов игры или версия не та: проверьте `game_files\default.xex` и таблицу версий выше |
| Ошибка сборки `Microsoft Visual C/C++ Version differs in precompiled file` | Visual Studio обновилась. Удалите `out\build\local-win-relwithdebinfo\CMakeFiles\vivapinata_recomp.dir\cmake_pch.hxx.pch` и соберите заново |
| Генерация кода падает с `0xC0000409` | В пути к проекту есть русские или другие не английские буквы. Перенесите проект, например в `C:\Games\VivaPinataRecomp` |
| При запуске через `F5` Visual Studio останавливается на «Access violation» в `memcpy` | Это не падение. Отключите остановку на `0xC0000005` (Отладка → Окна → Параметры исключений) или запускайте через `Ctrl+F5` / `run_game.bat` |

Логи пишутся в `out\build\local-win-relwithdebinfo\logs\`. Подробная техническая инструкция: **[AGENTS.md](AGENTS.md)** (на английском, рассчитана на ИИ-агента).

## Для разработчиков

Устройство проекта, правила разработки и важные исправления: [AGENTS.md](AGENTS.md), раздел 2. Два главных исправления:
- ошибка генератора кода ReXGlue, из-за которой земля была белой (`vpkd3d128` терял знак чисел, [`src/game_fixes.h`](src/game_fixes.h));
- распаковка файлов CAFF от Rare «на месте», под которую пришлось подстроить русский текстовый бандл ([`tools/make_russian_bnl.py`](tools/make_russian_bnl.py)).

## Благодарности

- [ReXGlue SDK](https://github.com/rexglue/rexglue-sdk): рекомпилятор и среда выполнения
- [Xenia](https://github.com/xenia-project/xenia) и [Xenia Canary](https://github.com/xenia-canary/xenia-canary): графический бэкенд и эталон для сравнения
- [TiP-Recomp](https://github.com/SolarCookies/TiP-Recomp) (SolarCookies): образец архитектуры и имена функций движка (Viva Piñata: Trouble in Paradise), используются с разрешения автора
- ZoG Team ([Zone of Games](https://www.zoneofgames.ru/)): русский перевод ПК-версии
- Rare: за замечательную игру

## Правовая информация

Проект не связан с Microsoft и Rare и не одобрен ими. Файлов игры здесь нет, нужна собственная законно полученная копия. Viva Piñata является товарным знаком Microsoft Corporation.
