# XMOW-nx 0.1.34f

Source code of build **0.1.34f**. The release has the ready NRO and a Windows installer that makes the game files and shaders from your own disc; no game data is included. Build steps are in
[build.md](build.md). Changes since 0.1.16:

## Changes

- **0.1.17-0.1.18:** real internal resolution (100/85/75/67 %) and output resolution, both changeable live; shadow
  quality option; SDK fixes for color-target swaps and cube textures; codegen batches A and B (no-volatile PCH, loop
  barriers, local-register keep list).
- **0.1.19:** codegen batch C (generator from the LoS1-nx fork, `xmow_diet_ops.h`); texture identity probes (fixes
  doors drawn with the wrong leaf textures); height fog option.
- **0.1.20-0.1.22:** occlusion query boxes can be skipped, optional conservative occlusion, hitch log, UE3 detail level;
  geometry caches and per-draw savings in the Deko3D renderer; shadow and detail settings apply at the next start.
- **0.1.23-0.1.25:** exposure/gamma of the final composition aligned with the console's; shader compiles moved off the
  GPU ring thread (hitch fix); achievements menu; asynchronous logging.
- **0.1.26-0.1.28:** raw GPU-busy profile; `-mearly-ra=none` and `-fno-math-errno` build flags (the first guards
  against a GCC miscompile of NEON table lookups); fix in the uam shader compiler (a post-multiply factor was
  overwritten when folding chained multiplies, which over-brightened the title glare); per-map shader cache preload;
  cheaper constant uploads; CPU/peak boost options removed.
- **0.1.29-0.1.31:** small-object culling and lighting options, selective copy barrier, single-depth-resolve
  skipping, normalized vertex remap, per-primitive draw statistics.
- **0.1.32-0.1.33e:** optional engine pre-pass skip; render-thread CPU cuts (native shader parameters, optional
  material replay); folded vertex-input shader cache; review fixes.
- **0.1.34-0.1.34e:** PLAY MODE (quiet logging), a built-in benchmark page (`xmow_bench`), log queue draining on
  crash; the console performance query now opens, reads and closes one service session per call (the previous
  version kept sessions open and could exhaust the system's session memory).
- **0.1.34f:** the game language follows the console language (`language = System`), with a LANGUAGE row in the quick
  menu (SDK patch 1041). The disc has English, French, Spanish and Italian; other console languages fall back to English.

## Known issue

After leaving a level back to the main menu, the menu is darker than on first boot (menu lights attach before the map
change is detected).

## Source layout

- `app/` mirrors the app layer; `patches/rexglue-nx.patch` is the SDK patch against `rexglue-sdk-nx` `f961303`
  (gears2-nx patches 0001-0050 plus XMOW patches 1001-1041); `patches/uam-xmow.patch` is applied after
  `tools/switch_setup.py` fetches uam.
- `tools/prepare_generator.py` builds the code generator (LoS1-nx SDK fork at `3426a45` plus `patches/los1-nx-generator.patch`);
  `tools/codegen.ps1` and `tools/codegen/` are the 0.1.34f codegen recipe (flavor options and the local-register
  exception list are in `app/codegen/`).

## Third-party code added since 0.1.16

- `app/tools/xmow_diet_ops.h` (copy in `tools/codegen/`): PowerPC float/integer helper functions from the LoS1-nx fork's `diet_ops.h`
  (BSD-3-Clause, license in `licenses/LoS1-nx-sdk-BSD.txt`; origin per its header: sa2-nx / sasrt-nx / banjo-nx).
- The code generator itself (LoS1-nx SDK fork, BSD-3-Clause) is fetched at build time, not copied.
