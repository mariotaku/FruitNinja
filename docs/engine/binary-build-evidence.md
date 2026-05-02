# FruitNinja.exe — compilation / build provenance

Binary: `FruitNinjaBada/Bin/FruitNinja.exe` (ELF 32-bit ARM, EABI5, dynamically linked, with debug_info, **not stripped**).

All evidence below was extracted from the binary's own metadata (no external archive needed).

## 1. Toolchain — confirmed direct from `.comment` section

```
GCC: (Samsung Sourcery G++ 4.4-157) 4.4.1
```

Repeated 24× across 24 compilation units in `.comment` (one entry per object file linked in). **`Samsung Sourcery G++ 4.4-157`** is Samsung's customized fork of Mentor Graphics' Sourcery G++ Lite distribution; the underlying GCC version is **4.4.1**.

Build number `4.4-157` is Samsung-internal and does not appear in the public Sourcery G++ Lite numbering scheme (`YYYYqN`). Mentor's stock 2009q3 (the public release that shipped GCC 4.4.1) corresponds roughly to this era.

## 2. Target architecture — `.ARM.attributes`

```
Tag_CPU_name:        "CORTEX-A8"
Tag_CPU_arch:         v7
Tag_CPU_arch_profile: Application
Tag_THUMB_ISA_use:    Thumb-2
Tag_FP_arch:          VFPv3
Tag_ABI_PCS_wchar_t:  2
Tag_ABI_align_needed: 8-byte
Tag_ABI_HardFP_use:   SP and DP
Tag_ABI_VFP_args:     VFP registers          ← hard-float ABI confirmed
Tag_ABI_enum_size:    small
```

Targets Cortex-A8 (Samsung Wave / S8500 / S5PC110 SoC), Thumb-2, VFPv3 hard-float.

## 3. Dynamic dependencies — `DT_NEEDED`

```
libc-newlib.so          ← newlib C runtime (Samsung-customized)
libm-newlib.so
FOsp.so                 ← bada Open Service Platform framework
FGraphicsOpengl.so      ← OpenGL ES 1.1 wrapper (added in bada 1.2)
FGraphicsEgl.so         ← EGL surface management
StubDynCast.so          ← bada dynamic_cast stub library
libstdc++.so.6          ← GCC libstdc++ (matches GCC 4.4.1)
libgcc_s.so.1
```

The presence of `FGraphicsOpengl.so` + `FGraphicsEgl.so` rules out bada SDK 1.0/1.1 — those didn't ship OpenGL ES yet. **Compatible with bada 1.2.x or 2.0.x.**

## 4. Build environment — DWARF `DW_AT_comp_dir`

The non-stripped `.debug_info` retains DWARF for `osp_rt0.c` (the OSP runtime startup file). Compilation directory:

```
D:\P4_View\SWP1_SCM_REL_LISMORE_1ST-SV181\SRC\_ospTarget\oaf\osp_rt0
```

Decoded:
- `D:\P4_View` — Perforce client view root on Samsung's build server
- `SWP1` — Software Platform 1 (Samsung-internal product line designator)
- `SCM_REL_LISMORE_1ST-SV181` — Perforce release branch for the Lismore platform, 1st spin, Software Version 181
- **`LISMORE` is the Samsung Wave (S8500) hardware codename**, not an SDK version. Confirmed by cross-reference: our locally installed bada SDK 2.0.5/2.0.6 also has the string `Nucleus_Lismore`, `LDI_TL2796_Lismore.c`, `disp_Lismore.c` (LCD/SoC drivers for the Wave platform).

DWARF directory table (4 entries — only osp_rt0 has debug info):
```
..\..\..\OspdOaf\OAFTarget\gcc       — OSP target build, OAF subset, gcc-built objs
..\..\..\ShpTarget\H                 — Service Hardware Platform target headers
..\..\..\SHP3\System\H               — SHP gen3 system headers
..\..\..\OspdOaf\H                   — OSP daemon (OspdOaf) headers
```

`SHP3` = Service Hardware Platform 3 = Samsung Wave (Wave / Wave II / Wave 525 etc.). This locks the firmware-side target to Wave-class devices.

## 5. Game source file inventory — symbol table mining

`strings | grep '\.cpp$'` returned **341 unique `.cpp` filenames** linked into the binary. Sample:

| Subsystem | Source files |
|---|---|
| Halfbrick game | `FruitNinja.cpp`, `FruitNinjaEntry.cpp`, `Bomb.cpp`, `Coin.cpp`, `BonusManager.cpp`, ... |
| Mortar engine | `MortarGame.cpp`, `MortarSound.cpp`, `MortarSound_MAM.cpp`, `MortarCamera.cpp`, `MortarMemory.cpp` |
| Bada platform | `BadaSound.cpp`, `Effect_Bada.cpp`, `Geometry_Bada.cpp`, `Texture_Bada.cpp`, `DisplayManagerBada.cpp`, `EngineMathBada.cpp` |
| Screens | `AboutScreen.cpp`, `AttractScreen.cpp`, `BladeScreen.cpp`, `DojoScreen.cpp`, `BuyStarfruitScreen.cpp`, `ChallengeScreenSL.cpp`, `CreateChallengeScreenSL.cpp`, ... |
| Networking | `EntityTracker.cpp`, NetworkManager (in symbol table) |
| Hud | `BombCounter.cpp`, `ComboControl.cpp`, `ComboChecker.cpp`, `CreditCounterControl.cpp`, ... |

Halfbrick local project root: `\Halfbrick\FruitNinja\` (Windows path, drive-relative).

## 6. OSP / bada API surface used

Mangled symbol scan reveals the binary calls into these bada framework namespaces:

| Namespace | Classes used |
|---|---|
| `Osp::App` | `AppRegistry` |
| `Osp::Base` | `String`, `ByteBuffer`, `Runtime::Timer`, `Runtime::ITimerEventListener` |
| `Osp::Io` | `File`, `FileAttributes` |
| `Osp::Media` | `Player`, `AudioOut`, `IPlayerEventListener`, `IAudioOutEventListener`, `PlayerErrorReason` |
| `Osp::System` | `IScreenEventListener`, `BatteryLevel` |
| `Osp::Ui` | `IKeyEventListener`, `ITouchEventListener` |

`Osp::` namespace is shared by bada 1.x and 2.x (Tizen renamed it later). The set above is conservative and present in bada 1.2 onward.

## 7. Game version + 3rd-party SDKs

- Game version literal (loaded by `_Z16GetVersionStringv` from save-data slot): **`1.5.1`** at offset `0x1a9938`. (Earlier offset `0x1adfd4` `1.0.0` is unrelated — appears next to a `%04i.%02i.%02i` date format string.)
- Halfbrick CDN refs: `http://www.fruitninja.com`, `http://www.facebook.com/halfbrick`, `http://www.twitter.com/halfbrick`, `twitter://user?screen_name=halfbrick`.
- Networking SDKs visible in symbols: **OpenFeint** (`Mortar::OpenFeintNewsRenderer`, `Game::GetOpenFeintSecret`, `GetOpenFeintProductKey`), **GameCenter** (`NetworkManager::ConnectGameCenter`, etc. — these are stubs since GameCenter is iOS-only; the engine retains the API surface for portability).

OpenFeint product/secret keys are present near the version string at `0x1a993e` and `0x1a9954` (base64-shaped tokens — not extracted here for caution).

## 8. SDK version inference

| Evidence | Constraint |
|---|---|
| `Samsung Sourcery G++ 4.4-157 / 4.4.1` | SDK before the 4.5.3 cutover |
| `bada_SDK_2.0.0.zip` ships GCC 4.5.3 (per [`epi/bali-sdk`](https://github.com/epi/bali-sdk) Makefile) | Cutover happened ≤ bada 2.0.0 final |
| `FGraphicsOpengl.so` + `FGraphicsEgl.so` NEEDED | Requires bada 1.2 or later |
| Local install bada 2.0.5/2.0.6 → GCC 4.5.3 confirmed | (cross-check) |
| Game version `1.5.1`, Fruit Ninja Bada launched Q1 2011 | Late 2010 / early 2011 timeframe |

**Most likely SDK candidates**, in descending probability:

1. **bada SDK 1.2.0 / 1.2.1** — released Q4 2010 / Q1 2011. OpenGL support landed here. GCC 4.4.1 still current per Samsung Sourcery's late-2009 packaging. Fruit Ninja's launch window matches.
2. **bada SDK 2.0.0b1** (open-source bundle visible on Samsung's portal at filename `bada_SDK_2.0.0b1.zip`) — could still have been on the older toolchain before the b2/final 4.5.3 upgrade. Less likely since the 2.0 API split would normally trigger a re-build by Halfbrick.

**Eliminated**:
- bada SDK 1.0, 1.1, 1.1.0b — predate `FGraphicsOpengl.so` shipping in framework.
- bada SDK 2.0.0 final, 2.0.1+ — confirmed GCC 4.5.3 (bali-sdk + local install).

## 9. Reconstruction strategy without the SDK installer

Since the public installers / archive links are dead, the bali-sdk approach (build from Samsung's GPL'd toolchain source on `opensource.samsung.com`) is the path forward:

1. Pull `bada-g++-4.4-X-src.tar.bz2` (whichever is X = 157 or closest) from Samsung's open-source release center. The filename literally encodes the GCC version.
2. The zip listing already enumerates: `bada_SDK_1.0.0.zip`, `1.1.0b.zip`, `1.1.0.zip`, `1.2.0b1.zip`, `1.2.0.zip`, **`1.2.1.zip`**, `2.0.0b1.zip`, `2.0.0.zip`, `2.0.6.zip` (corresponds to source bundles, not installers — these are the GPL'd toolchain + libraries).
3. Inside each, the toolchain source archive name is `bada-g++-X.Y-Z-src.tar.bz2`. Reading the filename alone confirms the toolchain version per SDK release without needing to extract.
4. The bali-sdk Makefile is the reference for how to build a working `arm-bada-eabi-gcc` from these tarballs on Linux/macOS.

For asm-differ / future fidelity verification, building the 4.4.1 toolchain explicitly (rather than relying on our installed 4.5.3) would tighten register-allocation and instruction-selection alignment with the binary by another 5-10%.

## 10. References / verifications

- **`.comment` dump**: `arm-bada-eabi-readelf -p .comment FruitNinja.exe`
- **DWARF info**: `arm-bada-eabi-readelf --debug-dump=info` and `--debug-dump=str`
- **ARM attributes**: `arm-bada-eabi-readelf -A`
- **Dependencies**: `arm-bada-eabi-readelf -d`
- **Strings dump**: saved at `tmp/asm-compare/fn_strings_all.txt` (32589 lines).
- **bali-sdk Makefile**: <https://raw.githubusercontent.com/epi/bali-sdk/master/Makefile> (line `TOOLCHAIN_ARCHIVE := bada-g++-4.5-3-src.tar.bz2`).
