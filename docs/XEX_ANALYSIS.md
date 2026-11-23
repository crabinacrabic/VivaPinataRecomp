# Analysis of `game_files/default.xex`: Viva Piñata (2006)

> Date: 2026-09-20. Source: the XEX2 header (script `xexinfo.ps1`, fields after Xenia's `xex2_info.h`).
> The image is encrypted and compressed, so the PE body was not analysed; everything below comes from the unencrypted part of the header.

## 1. Identification

| Field | Value | Comment |
|---|---|---|
| Title ID | **`0x4D5307F2`** | The task description said `4D5307F1`, which is **Fable II** (MS-2033): a typo; the project uses `4D5307F2` |
| Media ID | `0x690B3287` | |
| Regions | `0x00FF00FF` | All regions (disc "USA, Europe", 16 languages) |
| Version / base | `0.0.0.1` / `0.0.0.1` | Retail disc without a title update |
| Disc | 1 of 1 | |
| PE name | `PinataParadiseRO.pe` | Rare's internal code name "Piñata Paradise"; RO = retail optimised |
| Link date | 2006-10-21 00:14:36 UTC | Released 2006-11-09, so this is the final master build |
| ISO (Redump) | CRC `f3ddf9d3`, MD5 `3902321dfe15d7d2510a96114dba625a` | `Vimm's Lair.txt` |

## 2. Image layout

| Field | Value |
|---|---|
| `image_base` / `load_address` | `0x82000000` |
| `image_size` | `0x00B90000` (11.6 MB) → image `0x82000000..0x82B90000` |
| `entry_point` | `0x826B8B48` |
| `image_flags` | `0x00000008` |
| TLS | 64 slots, raw `@0x82B09000`, data/raw size `0x0C` |
| Default stack | `0x40000` (256 KB) |
| `system_flags` | `0x600` |
| `title_workspace` | `0x02000001` |
| File format | encryption = **normal (1)**, compression = **normal LZX (2)**, window `0x8000`, block `0xF800` |
| PE data offset | `0x3000`, file size 3,244,032 bytes |

### 2.1 First codegen (2026-09-20, SDK 0.10.0.8-dev.g1406e1b)

| Field | Value |
|---|---|
| `REX_CODE_BASE` / `REX_CODE_SIZE` | `0x820E0000` / `0x5FFBB8` → code `0x820E0000..0x826DFBB8` (6.0 MB) |
| Functions | **19,430** (`vivapinata_funcs.h`) |
| Units | 99 × `vivapinata_recomp.N.cpp` (+ 99 `_funcs.N.h`, init/register/pch), 205 files, 6.6 s |
| Phases | Register → Scan → Discover → GapFill → Merge → Validate → Write |
| Validate, run 1 | 2 × `UnresolvedCall` (`0x82481D70`, `0x822F12C0`) → added to `config/vivapinata_functions.toml` |
| Write, run 2 | 2 × "Unresolved b target" → `REX_FATAL` in the code (`0x82630554` in `recomp.80.cpp`, `0x82672F14` in `recomp.32.cpp`) → added there too |
| Link, run 2 | `undefined symbol: PPCImageConfig`: `sources.cmake` did not exist at the first configure, so the OBJECT library `vivapinata_recomp` was not created. Fixed with a bootstrap block in `CMakeLists.txt` (codegen at configure time if `sources.cmake` is missing, + `CMAKE_CONFIGURE_DEPENDS`) |

### 2.2 Launch log (F5)

| # | Got to | Symptom | Cause | Fix |
|---|---|---|---|---|
| 1 | XEX loaded, D3D12 (RX 6600, ROV yes), audio endpoint, `SetInterruptCallback(82464688, 40001880)` | `[FATAL] Call to invalid or unregistered function at guest address 0x82482EF0` | `0x82482EF0` is an 8-byte MSVC adjustor thunk (`addi r3,r3,-16; b Method`) in the packed block `0x82482E90..0x82482F08`; only a vtable the scanner did not find refers to it | `tools/find_thunk_holes.py` found **42** such holes in 7 blocks → all in `config/vivapinata_functions.toml` |
| 2 | same | `[FATAL] … 0x82527D18`; stack `sub_82528080 → sub_8252C400 → sub_8252E0C8` (recomp.7.cpp:26578, `lwz r11,0(r28); mtctr; bctrl` = vtable[0]) | `sub_82527C90` ends exactly at `0x82527D18`, a 16-byte hole before `sub_82527D28` (a `lis/ori/blr` getter): another method of an unfound vtable | address added; the project switched to **discovery mode**: `dev_debug_runtime = true` → the stub sweep writes all missed addresses to `logs/stub_sweep.txt`, converter `tools/stub_sweep_to_toml.py` |
| 3 | + 2 shaders, 1 pipeline; stub sweep: **0 hits** | `0xC0000005` **first-chance** write to `0x1_FB126000` in `sub_8269A200` (= CRT `memcpy`), called from `sub_82538BF0` (D3D Lock → memcpy 120 → Unlock); after Continue the next page `0x1FAED9000`, `0x1FAF00000`, … | **Not a bug.** The page is readable (`vsdbg_memory`) and only write-protected: the SDK's GPU write-watch, handled by its own VEH. VS stops on every first-chance `0xC0000005` by default | Untick `0xC0000005` in VS Exception Settings (Ctrl+Alt+E → Win32 Exceptions) or run with Ctrl+F5. KB: `ERROR_LOOKUP_TABLE.md` #7/#8, `ROOT_INDEX.json` |

Found on the way, for `[rexcrt]`: **`memcpy = 0x8269A200`** (signature `std r3,-8(r1); dcbt r0,r4; … ldu/stdu`), `__savegprlr_*` at `0x8269A040/48`.

`src/game_constants.h` takes `kCodeBase`/`kCodeEnd` from the pch macros automatically. For comparison: AO2 (UE3, 2008) has 62,467 functions and code size 0xE8C604; Viva Piñata is three times more compact.

Army of Two's XEX is also encrypted + LZX: the SDK loader decrypts it itself, and codegen worked on it (62,467 functions, 0 errors). Viva Piñata needs no extra tools.

## 3. Imports and static libraries

Import libraries (2): `xboxkrnl.exe` with **276** imports, `xam.xex` with **170** imports, both version `2.0.3529.0`. No game DLLs → no `[[modules]]` section in the manifest, no `--scan-dll`.

XDK static libraries **2.0.3529** (one of the earliest retail XDKs, autumn 2006):

```
D3DX9  XGRAPHC  XONLINE  XUIHTM  XUIVIDE(16385)  XHV  XMEDIA  LIBCPMT
XAPILIB  XBOXKRNL  D3D9LTCG  XAUDLTCG  XUIRUNL  XUIRNDRL  X3DAUDL  XACTLTCG
```

What this means for the recompilation:

- **D3D9LTCG:** the D3D layer is linked statically (LTCG). The whole Xenos driver lives in the image; "ring buffer / interrupt callback" class bugs (AO2 `sub_82A4FCE0`, `sub_82A50FF0`) are possible, with their own addresses.
- **XUIRUNL / XUIRNDRL / XUIHTM / XUIVIDE:** the UI is XUI (as in TiP: hooks `XuiProcessInput`, `CXuiModule::ProcessInput`).
- **XHV / XONLINE:** voice chat and Live; without a network, XAM stubs are expected.
- **XMEDIA / XUIVIDE:** the video player (`Beta\movie\`, 67 MB files = intro/cutscenes). Candidate no. 1 for a skip hook if video rendering does not work.
- **XACTLTCG / XAUDLTCG / X3DAUDL:** XACT audio (`Beta\xwavebank\`, `xwavebankloc\`).
- **LIBCPMT:** the C++ runtime with exceptions: `generate_exception_handlers = false` on the first run; turn it on only on EH validation errors.
- XDK 2.0.3529 is older than AO2's (2008) and TiP's (2008): CRT/XAPI addresses from neighbouring projects cannot be reused, not even approximately. For Ghidra: `tomcl7/ghidra-fidb-xenonsdk` has FIDBs for early XDKs, the only quick way to get the names `memcpy`/`RtlAllocateHeap`/`Sleep`.

## 4. Game data (extracted ISO → `game_files/`)

```
game_files/
├── default.xex                  3.2 MB
├── gardensave.png, settingssave.png   save icons
├── $SystemUpdate/               su20076000 (not needed)
└── Beta/                        Rare's data root (guest path game:\Beta\...)
    ├── bundles/                 <lang>.bnl, localisation (17 languages)
    ├── bundles_packages/        615 × N.pkg + packageLut.plt (package LUT), packageLut_dlc_1.plt
    ├── debug/                   db_index.txt (39,290 lines "aid_<asset> <hash> <ver>"), debug_pack.bin 775 MB, debug_hash.bin
    ├── font/, fontcacheabc/, fontcachesbm/   fonts and glyph caches (hex names)
    ├── movie/                   videos (hex names, up to 153 MB)
    ├── vincedata/               metafont_<lang>.ini
    └── xwavebank/, xwavebankloc/  XACT wave banks
```

`db_index.txt` maps asset names to hashes. Not function symbols, but useful for asset manager hooks (in TiP: `assetManOpen`, `meLoadAsset`, `meInitModel`): an asset name can be recovered from a hash in the log.

## 5. The TiP-Recomp reference: what applies and what does not

`C:\Recompiles\reference_repos\viva_pinata_recomp` = **SolarCookies/TiP-Recomp**, a recomp of **Viva Piñata: Trouble in Paradise (2008)** on SDK 0.8.1. The same Rare engine and data layout, but **a different XEX**.

| Applies (architecture) | Does NOT apply (addresses) |
|---|---|
| Separate `config/*.toml` (ctx / hooks / midasm / crt / variables): reproduced | `setjmp_address = 0x82AE2EA0`, `longjmp_address = 0x82AE2A60` |
| Hook naming `rex_<subsystem><Func>_<addr>` | All 30 `#CTX.CTR.U32` entries in `retip_ctx.toml` |
| `timeBeginPeriod(1)` in `OnPostSetup`: reproduced (`vp_high_res_timer`) | All `[rexcrt]` addresses (`RtlAllocateHeap = 0x82B089F0`, …) |
| The hybrid Sleep idea (`SleepHooks.h`): ported to `src/game_timing.h` | All `[[midasm_hook]]` addresses (`vsync_hook @0x8229B71C`, …) |
| The list of subsystems to hook: appMainDraw, camMainGetAspectRatio, XUsbcam*, XuiProcessInput | `retip_hooks.toml` addresses |
| `XUsbcam*` stubs (Live Vision camera) | |

## 6. Risks and blockers before the first codegen

1. **`ñ` in the project path is a blocker.** `rexglue.exe` (v0.10.0.8) exits with `0xC0000409` (STATUS_STACK_BUFFER_OVERRUN) in any path with a non-ASCII character (checked in isolation: `…\vp_ñ_test\` fails, `…\vp_init\` with the same XEX works). Codegen from Visual Studio uses the same binary and fails the same way. The folder must be renamed to ASCII (for example `C:\Recompiles\VivaPinata_xbox360`). All project files are path-independent, so renaming is safe.
2. **Title ID:** use `4D5307F2` everywhere (Xenia patches, achievements, save ID).
3. **Early XDK 2.0.3529:** the SDK's XAM/kernel exports target later ordinals; `unimplemented export` lines at start are possible. Not a codegen error: stubs in `src/game_fixes.h`.
4. **Huge videos in `Beta\movie\`:** if the SDK's XMEDIA decoder rejects them, the first visible frame comes after an intro skip hook (like `SkipIntroVideos_hook` in TiP).
5. The project root held the ISO (7.8 GB) and a 7z (5.8 GB): in `.gitignore`, but they slow down VS indexing; better moved out of the root.
