// XMOW - game patches in the generated code (tools/codegen/patch_xmow_jogo.py, run by tools/codegen.ps1).
// Each patched site asks XmowGamePatch(id) whether it is on: a cvar each, so it can be turned off from xmow.toml
// without a rebuild. Modelled on singularity-nx's singularity_game_patches.cpp (panalexeu/singularity-nx, GPL-3).
//
//   1  GUseTilingMode = FALSE: the game draws the scene at once instead of in two predicated tiles. The renderer
//      init sub_82374410 sets GUseTilingMode (0x836088E8) to 1 with `stw r11,-30488(r6)` (xmow_recomp.8.cpp); found
//      from the TILING console command (sub_8236FBB0, strings "TILING"/"TILINGMODE" at 0x820BEC44/54), which
//      flips that variable. Wolverine tiles its 2X MSAA scene (rows 0-511 and 512-719); tests 13-15 showed the
//      split, the stretched top half and the white overexposure around it.
#include <rex/cvar.h>

REXCVAR_DEFINE_BOOL(xmow_patch_sem_tiling, true, "XMOW",
                    "GUseTilingMode = FALSE: a cena e desenhada de uma vez, sem as duas teselas do predicated tiling "
                    "(patch 1 do jogo, como o singularity-nx)");

extern "C" int XmowGamePatch(int id) {
  switch (id) {
    case 1: return REXCVAR_GET(xmow_patch_sem_tiling) ? 1 : 0;
    default: return 0;
  }
}
