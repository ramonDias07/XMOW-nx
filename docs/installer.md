# Shader installer (xmow_setup)

The port needs four shader files next to `XMOW.nro`. They are built from the game's own shaders, so the release does
not ship them. `xmow_setup.exe` builds them on Windows from the user's own disc, and checks each one against the
SHA-256 of the release.

| file | bytes | SHA-256 |
|---|---|---|
| `xmow_shaders.dslib` | 270,376,072 | `c0003d1c6fbfe12b916b913fded655f21f722178f823601555ca4f87febe4d22` |
| `shadercache_dksh.bin` | 569,797,952 | `14a0b9743b14264f2e213982dc03cb44bfeb9a8cef6c9e56c491ab3536175319` |
| `shadercache_dksh_fix.bin` | 77,952 | `241a25fbdf83a4fe8a171ac872a8c70ca811249bdf10e617d7319a736f28bc61` |
| `shadercache_dksh_vs.bin` | 7,170,792 | `07194928844c5e385b783f945cdb2ef1b8eaceef94db6591e17cd60cfbd608bf` |

## Using it

Download `XMOW-nx-installer-v0.1.34f.zip`, unzip it, and either double-click `xmow_setup.exe` (it asks for the paths)
or run:

```
xmow_setup.exe --iso "D:\path\to\your game.iso" --out "D:\XMOW-SD"
```

Then copy `D:\XMOW-SD\switch` to the root of the SD card, merging it with the release (`switch\XMOW\XMOW.nro`,
`xmow.toml`). The output folder holds `switch\XMOW\game_root\` (`default.xex` + `WGame\`) and the four files.

Input: a plain Xbox 360 `.iso` (XGD2/XGD3/raw XDVDFS) or `--disc <folder>` with `default.xex` and `WGame\`.
Requirements: Windows 10/11 x64, about 10 GB free, 8 GB of RAM or more. On an 8-thread PC with 4 workers it took
33 minutes.

Options: `--no-game-root` (only read the game files), `--jobs N` (default: hardware threads limited by free RAM;
1 GB per DXC process, 2.5 GB per uam worker), `--keep-work`, `--start S` / `--until S` with
S = `entrada|hlsl|dxc|dslib|fix|vs|dksh`.

## What it does

1. **entrada**: reads the image (XDVDFS), copies `default.xex` + `WGame\` to `game_root`, then:
   - scans `WGame\CookedXenon\*.xxx` with `xmow_extract_shaders` and gets 16,372 shader containers;
   - decrypts `default.xex` (XEX2, AES-128) and undoes its basic compression. It cuts the 18 shaders that the D3D
     runtime keeps inside the executable, placing our own container headers (`data/xex.tsv`) in front of the microcode
     at the recorded offsets;
   - rebuilds the 67 shader variants that were recorded on the console. It patches u32 words of their disc parents
     (`data/variantes.tsv`); 2 variants have no parent on the disc and are shipped whole (336 bytes);
   - checks all 16,457 containers against `data/manifesto.tsv` (SHA-256).
2. **hlsl**: runs `ds_hlsl --saltos-lineales` (XenosRecomp) in parallel slices.
3. **dxc**: runs `dxc -spirv -T ps_6_6|vs_6_6 -E main -HV 2021 -fspv-target-env=vulkan1.2 -fvk-use-dx-layout
   -Werror=parameter-usage`.
4. **dslib**: `ds_empaquetar` builds `xmow_shaders.dslib`.
5. **fix**: puts the 3 pixel shaders that the uam post-factor fix changes into a 3-shader library, then compiles it
   with `ds_dksh_0127` (`--variantes-ps 1,208,209 --variantes data/variantes_consola.tsv`). The result is
   `shadercache_dksh_fix.bin`.
6. **vs**: runs `ds_vs_plegado` over the 2,355 proven folded vertex shader keys (`data/claves_vs.txt`), then packs
   the output sorted as NFDK v2 (`src/nfdk.h`). The result is `shadercache_dksh_vs.bin`.
7. **dksh**: `ds_dksh_0123` (uam without the fix, as in the release) builds `shadercache_dksh.bin`
   (154,028 DKSH; any failed or timed-out variant is an error).

The tool contains no game data except the variant words above (about 4 KB). The `data/` files are hashes, offsets,
our own header bytes, shader names and integers.

## Building it

Everything is in `tools/installer/`:

| path | what |
|---|---|
| `src/` | xmow_setup (C++20): XDVDFS, XEX2 + AES, SHA-256, NFDK writer, the stage runner |
| `tool-src/shaders-g2/` | ds_hlsl, ds_empaquetar, ds_dksh (+ ds_validar), xmow_extract_shaders, XenosRecomp (MIT), sdk-min (the SDK deko3d files with the gears2 patches) |
| `tool-src/vs-plegado/` | ds_vs_plegado and the sdk-g2 `glsl.cpp`/`glsl.h` with `PlegarEntradaVs` (0.1.33) |
| `third_party/` | xxHash (e626a72), fmt include (407c905), libmspack `mspack/` (3059077, LGPL-2.1, unmodified) |
| `patches/uam-postfactor-0127.patch` | the XMOW 0.1.27 fix to uam's `nv50_ir_peephole.cpp` |
| `fetch_third_party.ps1` | gets SPIRV-Cross `f11ba9f0` and uam (SwitchWakerHD `2d147aaa`, `runtime/third_party/uam`) |
| `build.ps1` | builds every exe with the original flags and lays out `build\dist` |
| `data/`, `licenses/`, `README.txt` | what the zip ships |
| `gen/` | the one-off scripts that made `data/` from a reference run (they need that run's outputs; not part of the build) |

Toolchain: [llvm-mingw](https://github.com/mstorsjo/llvm-mingw) 20261020 ucrt x86_64, CMake >= 3.20, Ninja, git.
DXC: DirectXShaderCompiler v1.8.2407 x64 (`dxc.exe`, `dxcompiler.dll`; `dxil.dll` is not needed for SPIR-V).

```
powershell -ExecutionPolicy Bypass -File tools\installer\fetch_third_party.ps1
powershell -ExecutionPolicy Bypass -File tools\installer\build.ps1 -Dxc <folder with dxc.exe> -Jobs 8
```

The zip is the contents of `tools\installer\build\dist`. The shipped exes were built this way, and an end-to-end
run with them matches all four hashes. The exes themselves are not byte-reproducible: they embed build paths.
