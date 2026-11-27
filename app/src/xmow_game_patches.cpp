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
#include <rex/logging.h>

#include <bit>
#include <chrono>
#include <cstdint>

REXCVAR_DEFINE_BOOL(xmow_patch_sem_tiling, true, "XMOW",
                    "GUseTilingMode = FALSE: a cena e desenhada de uma vez, sem as duas teselas do predicated tiling "
                    "(patch 1 do jogo, como o singularity-nx)");

extern "C" int XmowGamePatch(int id) {
  switch (id) {
    case 1: return REXCVAR_GET(xmow_patch_sem_tiling) ? 1 : 0;
    default: return 0;
  }
}

// 0.1.28. 2 and 3 do not ask XmowGamePatch: they read a factor that is 1.0 (bit-exact, the game's compare) unless the
// option is changed. Both sit in FSceneRenderer::InitViews (sub_82606A08, xmow_recomp.158.cpp), on the render thread;
// XmowGamePatchesPorVista refreshes them from the cvars at each InitViews (xmow_perf_hooks.cpp), so they apply live.
//   2  View distance: the per-primitive MaxDrawDistance cull compares DistSq > MaxDrawDistance^2 * g_xmow_corte_k2
//      (only for 0 < MaxDrawDistance < 262144). k2 = (xmow_distancia_corte / 100)^2.
//   3  Mesh detail: the static mesh LOD pick compares LODDistanceFactor^2 * DistSq * g_xmow_lod_k2 against each LOD's
//      [MinDrawDistanceSq, MaxDrawDistanceSq). k2 = (xmow_detalhe_malha / 100)^2: 150 = every static mesh takes the
//      LOD it would have at 1.5x the distance (fewer triangles; fewer draws where the low LOD has fewer sections).
REXCVAR_DEFINE_INT32(xmow_distancia_corte, 75, "XMOW",  // 0.1.30: default 75 (test 36: 75 and 50 near the same)
                     "View distance, in % of the game's: primitives with a cull distance (MaxDrawDistance) disappear at "
                     "this share of it (100 = the game, 75 = the default since 0.1.30, 50). Primitives without one are not "
                     "touched. Live")
    .range(25, 100);
REXCVAR_DEFINE_INT32(xmow_detalhe_malha, 100, "XMOW",
                     "Mesh detail: static meshes pick their LOD as if this many % farther away (100 = the game, 150 = "
                     "medium, 200 = low). Live")
    .range(100, 400);

//   4  (0.1.29) Small object cull: in the same function, just before the frustum test (loc_826077E0, `bl 0x82896378`
//      with View+736, the bounds origin r21 and the sphere radius at compact+40), a primitive whose bounding sphere
//      radius is under a share of its distance to the view origin (View+720/724/728, perspective views only:
//      View+732 > 0) goes to the cull branch (0x82608104), as the distance cull does. The codegen patch calls
//      XmowCortePequeno only while g_xmow_pequeno_k2 > 0 (OFF = 0: the game's code path, nothing else read).
REXCVAR_DEFINE_INT32(xmow_corte_pequenos, 0, "XMOW",
                     "Small object cull: objects whose bounding sphere looks smaller than a few pixels are not drawn. "
                     "0 = off (the game), 1 = LOW (radius under 0.4 % of the distance, about 4 pixels across at "
                     "960x540), 2 = HIGH (under 1 %, about 10 pixels). Live")
    .range(0, 2);

extern "C" {
double g_xmow_corte_k2 = 1.0;
double g_xmow_lod_k2 = 1.0;
double g_xmow_pequeno_k2 = 0.0;  // 0.1.29 patch 4: (radius / distance)^2 under which a primitive is cut; 0 = off
}

namespace {
// Patch 4 counters, only touched on the render thread (InitViews) while the cull is on.
uint64_t g_pequenos_probados = 0, g_pequenos_cortados = 0, g_pequenas_vistas = 0;
}  // namespace

extern "C" int XmowCortePequeno(uint32_t radio, uint32_t ox, uint32_t oy, uint32_t oz, uint32_t vx, uint32_t vy,
                                uint32_t vz, uint32_t vw) {
  const float w = std::bit_cast<float>(vw);
  if (!(w > 0.0f)) return 0;  // not a perspective view: the game computes no distance there either
  const double r = double(std::bit_cast<float>(radio));
  if (!(r > 0.0)) return 0;
  const double dx = double(std::bit_cast<float>(ox)) - double(std::bit_cast<float>(vx));
  const double dy = double(std::bit_cast<float>(oy)) - double(std::bit_cast<float>(vy));
  const double dz = double(std::bit_cast<float>(oz)) - double(std::bit_cast<float>(vz));
  ++g_pequenos_probados;
  if (r * r < g_xmow_pequeno_k2 * (dx * dx + dy * dy + dz * dz)) {
    ++g_pequenos_cortados;
    return 1;
  }
  return 0;
}

void XmowGamePatchesPorVista() {
  const int32_t c = REXCVAR_GET(xmow_distancia_corte);
  const int32_t l = REXCVAR_GET(xmow_detalhe_malha);
  g_xmow_corte_k2 = c >= 100 ? 1.0 : double(c) * double(c) / 10000.0;
  g_xmow_lod_k2 = l <= 100 ? 1.0 : double(l) * double(l) / 10000.0;
  // 0.1.29 patch 4: LOW 0.004, HIGH 0.010 of the distance (squared).
  const int32_t p = REXCVAR_GET(xmow_corte_pequenos);
  g_xmow_pequeno_k2 = p <= 0 ? 0.0 : p == 1 ? 0.004 * 0.004 : 0.010 * 0.010;
  // Every 10 s while it is on, and when the level changes: what it cut, per InitViews.
  using reloj = std::chrono::steady_clock;
  static reloj::time_point desde{};
  static int32_t nivel_previo = 0;
  const reloj::time_point ahora = reloj::now();
  if (desde == reloj::time_point{}) desde = ahora;
  const double s = std::chrono::duration<double>(ahora - desde).count();
  if (nivel_previo > 0 && (s >= 10.0 || p != nivel_previo) && g_pequenas_vistas) {
    const double v = double(g_pequenas_vistas);
    REXLOG_INFO("[xmow cull] {:.1f} s: small object cull {} (xmow_corte_pequenos {}): {} of {} primitives cut "
                "({:.0f} of {:.0f} per InitViews, {} InitViews)",
                s, nivel_previo == 1 ? "LOW" : "HIGH", nivel_previo, g_pequenos_cortados, g_pequenos_probados,
                double(g_pequenos_cortados) / v, double(g_pequenos_probados) / v, g_pequenas_vistas);
  }
  if (p != nivel_previo) {
    REXLOG_INFO("[xmow cull] small object cull now {} (xmow_corte_pequenos {})",
                p <= 0 ? "OFF" : p == 1 ? "LOW" : "HIGH", p);
  }
  if (p != nivel_previo || s >= 10.0) {
    g_pequenos_probados = g_pequenos_cortados = g_pequenas_vistas = 0;
    desde = ahora;
  }
  if (p > 0) ++g_pequenas_vistas;  // this InitViews, counted with what it cuts
  nivel_previo = p;
}
