// XMOW - internal resolution through UE3's own ScreenPercentage, and the shadow cut, both set in the engine's system
// settings (FSystemSettings) right after the game reads them from its ini (plan 0.1.17, items 1a and 2b), and changed
// LIVE from the menu since 0.1.18, at the start of a game frame.
//
// What the recompiled code shows (addresses of the retail default.xex, no title update):
//   GSystemSettings          0x836015DC (vtable at +0; constructor sub_82584670, xmow_recomp.245.cpp). Its settings data
//                            (FSystemSettingsData, 512 bytes) is at +4 = 0x836015E0; GSystemSettings+544 (0x836017FC) is a
//                            UBOOL the constructor and Initialize clear; GSystemSettings+548 holds 5 more copies of the
//                            data (the compat level buckets, 512 bytes each).
//   sub_825838A0             FSystemSettingsData::LoadFromIni(this r3, section r4, ini r5): GConfig GetBool / GetInt /
//                            GetFloat over a table of (name, this + offset). THIS is the loader that runs at start:
//                            sub_82584728 (FSystemSettings::Initialize, called once by sub_831EF9E8) calls it for the 5
//                            buckets and then for the data at 0x836015E0, then copies the scene size globals 0x83532BA4 /
//                            0x83532BA8 (sub_82374488) into ResX / ResY and calls sub_82585940. The console command path
//                            sub_82584910 ("SCALE ...") calls it again, but into a temporary on its stack.
//   sub_82583E18             NOT a loader: the same table written back (GConfig set + appSprintf, then a flush): the
//                            SaveToIni of the "SCALE" console path (sub_82584910, only when GSystemSettings+544 == 0). It
//                            never runs at start, so it is not hooked (the plan named it; the tables only look alike).
//   Offsets in the data (both tables agree, names are UTF-16BE strings at 0x821136DC..0x82113F68):
//     +396 float ScreenPercentage (ini 100.0)      +400 UBOOL UpscaleScreenPercentage (ini True)
//     +404 int   ResX (ini 1280)                   +408 int   ResY (ini 720)
//     +424 UBOOL DynamicShadows                    +428 UBOOL LightEnvironmentShadows
//     +432 int   ShadowFilterQualityBias           +436 int   MinShadowResolution      +440 int MaxShadowResolution
//     +476 int   ShadowFadeResolution              +444 float ShadowTexelsPerPixel     +472 float ShadowDepthBias
//   Readers of ScreenPercentage: sub_82585758 / sub_82585650 (the scale / unscale of screen coordinates:
//   clamp(ScreenPercentage * 0.01, 0, 1)) and the renderer (sub_82604768, sub_826C36F8, sub_8280C350, sub_825CF650), all
//   with the same test: UpscaleScreenPercentage && ScreenPercentage < 100.0 (0x8223D38C) && GSystemSettings+544 == 0.
//   So the engine draws the 3D view in a smaller rectangle and stretches it to the full 1280x720 itself; the HUD and
//   text are drawn afterwards at 720.
//
// Why the change can be live (0.1.18):
//   - Every reader of ScreenPercentage loads it from GSystemSettings+400 on each call (lfs f0,400(rGSS) in all six;
//     sub_825CF650 is the viewport's draw on the game thread, sub_82604768 the scene pass on the rendering thread, both
//     once per frame). No render target is sized by it: the scene targets stay 1280x720 and only the view rectangle
//     shrinks. The "SCALE" console command (sub_82584910) writes the field and recreates nothing for it: its only
//     follow-up is a FGlobalComponentReattachContext (sub_826CFEF0 / sub_824471B0) when data+0 changed.
//   - DynamicShadows (+424), MinShadowResolution (+436) and MaxShadowResolution (+440) are not read by the renderer from
//     here: sub_82585940 (FSystemSettings::UpdateRenderThreadSettings) copies them, with data+32, +40, +64 and +420, into
//     a render command (or runs sub_82585AD8 at once without a rendering thread), and sub_82585AD8 stores them in the
//     rendering thread's copy at GSystemSettings+516..540 and, when one changed, calls sub_824E7DE8(0x8360392C) - an
//     UpdateRHI (Release / Init through the vtable) of the resource the shadow passes size from +540. FEngineLoop::Tick
//     (sub_831EE170) calls sub_82585940 EVERY frame, right after GEngine->Tick. So a value written on the game thread
//     reaches the renderer at the next frame, through the engine's own path, shadow buffers included. The renderer
//     reads the copies per frame (+524 in sub_826083C8 / sub_8260A3A8 / sub_82606A08, +540 in sub_8260A3A8 / sub_82615188
//     and the sub_824EA6F8.. family). ShadowFilterQualityBias (+432) is read in place by its users (lwz 436(rGSS)).
//   - When: the menu runs on the input thread, so it only changes the cvars. The hook on FEngineLoop::Tick
//     (sub_831EE170, game thread, once per frame, before GEngine->Tick) sees the cvar differ from what was applied,
//     calls the engine's FlushRenderingCommands (sub_824E7B50, the first thing sub_826CFEF0 does) so the rendering thread
//     has finished the previous frame, writes the fields, and lets the tick run: the whole next frame, game and
//     rendering thread, uses the new values. Never in the middle of a frame.
//
// Hooked through the weak alias of each function (REX_HOOK_RAW, as xmow_perf_hooks.cpp): sub_825838A0 (defined in a
// different file than all its callers) runs the original, then, on a data block whose ResX / ResY read 1280 / 720,
// writes ScreenPercentage = xmow_escala_interna (and UpscaleScreenPercentage = True) and the xmow_sombras cut. Every load
// is patched again. sub_82584728 (Initialize) only checks, after the original, that the main block still has the values
// written and logs its final state (re-writing them if something put them back). sub_831EE170 (defined in
// xmow_recomp.116.cpp, its only caller in xmow_recomp.13.cpp) applies the live changes.

#include "xmow_pch.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>

#include <fmt/format.h>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/memory/utils.h>

#include "xmow_bench.h"
#include "xmow_perf.h"

// 0.1.29: the internal resolution shown after the FPS counter (sdk-g2 src/ui/deko3d/presentador.cpp).
namespace rex::ui {
void FijarResolucionContador(uint32_t ancho, uint32_t alto);
}
namespace rex::x360 {
extern std::atomic<uint64_t> g_xmow_gpu_ocupada_ns;  // sdk-g2 destinos.cpp (xmow 1039): GPU busy ns since start
}
bool XmowMapaDeJogo();  // below
void XmowIluminacaoPorMapa(const char* paquete, bool jogo);  // below

// 0.1.33d (review S-B): the return to the main menu told by the height fog's constants. The ring thread classifies
// every fog draw (gow2_gpu.cpp NieblaAlturaOmitida -> XmowNieblaClasificada); the game thread decides at the start of
// a frame (AlInicioDelCuadro). Reset when a gameplay map package opens (XmowIluminacaoPorMapa).
// 0.1.33e (review F-1): only totals here; the game thread turns them into FRAMES (with and without frontend fog).
std::atomic<uint64_t> g_niebla_frontal_total{0};     // frontend fog draws since start
std::atomic<uint64_t> g_niebla_otra_total{0};        // other (gameplay) fog draws since start
std::atomic<uint32_t> g_paquete_jogo_gen{0};         // gameplay map packages opened
std::atomic<int64_t> g_paquete_jogo_ns{0};           // steady clock ns of the last one
inline int64_t AhoraNsSteady() {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch())
      .count();
}
void XmowNieblaClasificada(bool frontal) {
  std::atomic<uint64_t>& t = frontal ? g_niebla_frontal_total : g_niebla_otra_total;
  t.store(t.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);  // one writer (the ring thread)
}

// Live (kHotReload): the menu changes them at once, SAVE AND CLOSE keeps them in xmow.toml.
REXCVAR_DEFINE_INT32(xmow_escala_interna, 100, "XMOW",
                     "Internal resolution in percent of 1280x720 (UE3 ScreenPercentage, the engine upscales the 3D "
                     "view; HUD stays at 720): 100 = 1280x720, 85 = 1088x612, 75 = 960x540, 67 = 854x480, 50 = 640x360. "
                     "Live: applied at the start of the next game frame")
    .range(50, 100);
REXCVAR_DEFINE_INT32(xmow_sombras, 0, "XMOW",
                     "Dynamic shadows: 0 = all (the game's settings), 1 = reduced (MaxShadowResolution halved, shadow "
                     "filter quality bias -1), 2 = off (DynamicShadows = False). Applied at the next start "
                     "(latched at the first settings load; a change in the menu is only saved)")
    .range(0, 2);
// 0.1.20: engine detail that cuts draws (offsets verified in LoadFromIni sub_825838A0, see kCamposDetalle below).
REXCVAR_DEFINE_INT32(xmow_detalle, 0, "XMOW",
                     "Engine detail (UE3 system settings, cuts draws in open views): 0 = as the game, 1 = reduced "
                     "(DetailMode -1, particle and skeletal mesh LOD bias +1, static and dynamic decals off, foliage "
                     "draw radius x0.75), 2 = low (DetailMode 0 = low, LOD biases +2, decals off, motion blur, depth of "
                     "field and lens flares off, foliage radius x0.5). Applied at the next start "
                     "(latched at the first settings load; a change in the menu is only saved)")
    .range(0, 2);
// 0.1.28: LIGHTING. 0 = STANDARD (the game), 2 = NONE (FSystemSettings +20 DynamicLights = FALSE: lights without
// static lighting are not attached to the scene). 1 is kept for a REDUCED level, not done: this build's FSystemSettings
// has no light count or radius cap (kTodosCampos), and thinning the per-light passes would need the light loop of the
// renderer found and patched; 1 reads as STANDARD.
// 0.1.29: applied per map, when the game opens a map package (XmowIluminacaoPorMapa, from gow2_gpu.cpp's package hook):
// a gameplay map ("<map>_p.xxx") gets NONE, the frontend (before the first map, and WStart.xxx) keeps the game's
// value. Test 35: the title and menus are lit only by movable lights, NONE left them dark.
REXCVAR_DEFINE_INT32(xmow_iluminacao, 0, "XMOW",
                     "Lighting: 0 = standard (the game's), 2 = none (DynamicLights = False: no dynamic lights or their "
                     "passes) on gameplay maps only; the title and menus always keep the game's lights. Applied when "
                     "the next map loads")
    .range(0, 2);
// 0.1.33 (quality options, all off by default): FSystemSettings UBOOLs written False on gameplay maps only (the title and
// menus keep the game's values: the main menu's water look uses distortion, bloom and fog volumes). Written at every
// settings load, at each map package open (XmowIluminacaoPorMapa) and at once when changed in the menu.
REXCVAR_DEFINE_BOOL(xmow_sem_distorcao, false, "XMOW",
                    "Distortion = False on gameplay maps (FSystemSettings +52): no heat haze or refraction pass and no "
                    "scene copy for it. The title and menus keep it. Live");
REXCVAR_DEFINE_BOOL(xmow_sem_bloom, false, "XMOW",
                    "Bloom = False on gameplay maps (FSystemSettings +44): no glow around bright areas, no bloom "
                    "resolve and blur. The title and menus keep it. Live");
REXCVAR_DEFINE_BOOL(xmow_sem_volumes_nevoa, false, "XMOW",
                    "FogVolumes = False on gameplay maps (FSystemSettings +64): no fog volume meshes. The title and "
                    "menus keep them. Live");
REXCVAR_DEFINE_BOOL(xmow_diag_sem_speedtree, false, "XMOW",
                    "Diagnostic: SpeedTreeLeaves and SpeedTreeFronds = False on gameplay maps (FSystemSettings +4, +8): "
                    "trees without leaves and fronds. Shows whether the jungle's draws are SpeedTree. Read when the "
                    "trees attach: applies at the next map load");
// 0.1.33: dynamic resolution at the peaks, off by default.
REXCVAR_DEFINE_BOOL(xmow_resolucao_dinamica, false, "XMOW",
                    "Dynamic resolution on gameplay maps: when the GPU alone is the limit (GPU busy over 31.7 ms per "
                    "frame and under 29 fps for 2 s) the internal resolution goes ONE step below xmow_escala_interna "
                    "(100 -> 85, 85 -> 75, 75 -> 67), and back when the GPU has room again (at least 5 s later). "
                    "Nothing with xmow_escala_interna at 67 or below (logged once). Live");

REXCVAR_DEFINE_BOOL(xmow_detalle_reanexar, true, "XMOW",
                    "Unused since 0.1.22 (xmow_detalle is no longer live). Was: when xmow_detalle changed live, run the engine's FGlobalComponentReattachContext (what the SCALE "
                    "console command does when DetailMode changes) so components attached under the old DetailMode / "
                    "decal settings are re-evaluated. false = only new components see the change (until a level load)");

namespace xmow::resolucao {
namespace {

constexpr uint32_t kGSystemSettings = 0x836015DC;
constexpr uint32_t kDatosPrincipal = kGSystemSettings + 4;  // the FSystemSettingsData the renderer reads
constexpr uint32_t kBloqueoEscala = kGSystemSettings + 544;  // != 0 turns the engine's upscale off

// FSystemSettingsData offsets (see the top of the file).
constexpr uint32_t kScreenPercentage = 396;        // float
constexpr uint32_t kUpscaleScreenPercentage = 400;  // UBOOL (32 bit)
constexpr uint32_t kResX = 404;
constexpr uint32_t kResY = 408;
constexpr uint32_t kDynamicShadows = 424;           // UBOOL
constexpr uint32_t kShadowFilterQualityBias = 432;  // int
constexpr uint32_t kMinShadowResolution = 436;      // int
constexpr uint32_t kMaxShadowResolution = 440;      // int

// 0.1.20 (xmow_detalle): the detail fields. Mapped from LoadFromIni (sub_825838A0): it builds three tables of
// (UTF-16BE name, this + offset) on its stack - ints at r1+80..172 (GetInt), floats at r1+176..276 (GetFloat), UBOOLs
// at r1+288..540 (GetBool) - and the names were read from the decompressed image (xmow-ult\out\renut_image.bin). The
// same table confirms the offsets already used above (ScreenPercentage 396, ResX 404, DynamicShadows 424 ...).
//   int   +0 DetailMode (r31 itself = this: the SCALE path reattaches all components when data+0 changes)
//   int   +76 SkeletalMeshLODBias, +80 ParticleLODBias, +380 MaxAnisotropy, +420 MaxMultisamples
//   UBOOL +4 SpeedTreeLeaves, +8 SpeedTreeFronds, +12 StaticDecals, +16 DynamicDecals, +20 DynamicLights,
//         +24 CompositeDynamicLights, +28 DirectionalLightmaps, +32 MotionBlur, +36 DepthOfField, +40 AmbientOcclusion,
//         +44 Bloom, +48 UseHighQualityBloom, +52 Distortion, +56 DropParticleDistortion, +60 LensFlares,
//         +64 FogVolumes, +68 FloatingPointRenderTargets, +72 OneFrameThreadLag, +84 AllowD3D10, +376 OnlyStreamInTextures
//   float +384 SceneCaptureStreamingMultiplier, +388 FoliageDrawRadiusMultiplier
// There is no LightShafts / bAllowLightShafts key in this build's table (UE3 of 2008-2009) and no particle
// CullDistance here (that is a per-component property: only reachable with a SET / ini override hook, not done).
// Bloom, Distortion and FogVolumes are left alone: the main menu's water look depends on them.
enum class TipoCampo : uint8_t { kEntero, kBool, kFloat };
struct CampoDetalle {
  uint32_t offset;
  TipoCampo tipo;
  const char* nombre;
};
constexpr CampoDetalle kCamposDetalle[] = {
    {0, TipoCampo::kEntero, "DetailMode"},
    {76, TipoCampo::kEntero, "SkeletalMeshLODBias"},
    {80, TipoCampo::kEntero, "ParticleLODBias"},
    {12, TipoCampo::kBool, "StaticDecals"},
    {16, TipoCampo::kBool, "DynamicDecals"},
    {32, TipoCampo::kBool, "MotionBlur"},
    {36, TipoCampo::kBool, "DepthOfField"},
    {60, TipoCampo::kBool, "LensFlares"},
    {388, TipoCampo::kFloat, "FoliageDrawRadiusMultiplier"},
};
constexpr size_t kNumCamposDetalle = std::size(kCamposDetalle);
// 0.1.26 (pesquisa-fps-picos.md item 1b): EVERY field of LoadFromIni's tables (sub_825838A0, xmow_recomp.143.cpp),
// extracted from its stack tables (UTF-16BE names in the image, this + offset) by scratchpad tabela_ini.py: 11 ints,
// 13 floats, 32 UBOOLs. Logged at FSystemSettings init and checked again on the game thread (RevisarCampos).
constexpr CampoDetalle kTodosCampos[] = {
    {0, TipoCampo::kEntero, "DetailMode"},
    {76, TipoCampo::kEntero, "SkeletalMeshLODBias"},
    {80, TipoCampo::kEntero, "ParticleLODBias"},
    {380, TipoCampo::kEntero, "MaxAnisotropy"},
    {404, TipoCampo::kEntero, "ResX"},
    {408, TipoCampo::kEntero, "ResY"},
    {420, TipoCampo::kEntero, "MaxMultisamples"},
    {432, TipoCampo::kEntero, "ShadowFilterQualityBias"},
    {436, TipoCampo::kEntero, "MinShadowResolution"},
    {440, TipoCampo::kEntero, "MaxShadowResolution"},
    {476, TipoCampo::kEntero, "ShadowFadeResolution"},
    {384, TipoCampo::kFloat, "SceneCaptureStreamingMultiplier"},
    {388, TipoCampo::kFloat, "FoliageDrawRadiusMultiplier"},
    {396, TipoCampo::kFloat, "ScreenPercentage"},
    {444, TipoCampo::kFloat, "ShadowTexelsPerPixel"},
    {468, TipoCampo::kFloat, "ShadowFilterRadius"},
    {472, TipoCampo::kFloat, "ShadowDepthBias"},
    {480, TipoCampo::kFloat, "ShadowFadeExponent"},
    {484, TipoCampo::kFloat, "ShadowVolumeLightRadiusThreshold"},
    {488, TipoCampo::kFloat, "ShadowVolumePrimitiveScreenSpacePercentageThreshold"},
    {496, TipoCampo::kFloat, "NumFracturedPartsScale"},
    {500, TipoCampo::kFloat, "FractureDirectSpawnChanceScale"},
    {504, TipoCampo::kFloat, "FractureRadialSpawnChanceScale"},
    {508, TipoCampo::kFloat, "FractureCullDistanceScale"},
    {4, TipoCampo::kBool, "SpeedTreeLeaves"},
    {8, TipoCampo::kBool, "SpeedTreeFronds"},
    {12, TipoCampo::kBool, "StaticDecals"},
    {16, TipoCampo::kBool, "DynamicDecals"},
    {20, TipoCampo::kBool, "DynamicLights"},
    {24, TipoCampo::kBool, "CompositeDynamicLights"},
    {28, TipoCampo::kBool, "DirectionalLightmaps"},
    {32, TipoCampo::kBool, "MotionBlur"},
    {36, TipoCampo::kBool, "DepthOfField"},
    {40, TipoCampo::kBool, "AmbientOcclusion"},
    {44, TipoCampo::kBool, "Bloom"},
    {48, TipoCampo::kBool, "UseHighQualityBloom"},
    {52, TipoCampo::kBool, "Distortion"},
    {56, TipoCampo::kBool, "DropParticleDistortion"},
    {60, TipoCampo::kBool, "LensFlares"},
    {64, TipoCampo::kBool, "FogVolumes"},
    {68, TipoCampo::kBool, "FloatingPointRenderTargets"},
    {72, TipoCampo::kBool, "OneFrameThreadLag"},
    {84, TipoCampo::kBool, "AllowD3D10"},
    {376, TipoCampo::kBool, "OnlyStreamInTextures"},
    {392, TipoCampo::kBool, "UseVsync"},
    {400, TipoCampo::kBool, "UpscaleScreenPercentage"},
    {412, TipoCampo::kBool, "Fullscreen"},
    {416, TipoCampo::kBool, "UserSelectedResolution"},
    {424, TipoCampo::kBool, "DynamicShadows"},
    {428, TipoCampo::kBool, "LightEnvironmentShadows"},
    {448, TipoCampo::kBool, "bEnableVSMShadows"},
    {452, TipoCampo::kBool, "bEnableBranchingPCFShadows"},
    {456, TipoCampo::kBool, "bAllowBetterModulatedShadows"},
    {460, TipoCampo::kBool, "bEnableForegroundShadowsOnWorld"},
    {464, TipoCampo::kBool, "bEnableForegroundSelfShadowing"},
    {492, TipoCampo::kBool, "bAllowFracturedDamage"},
};
constexpr size_t kNumTodosCampos = std::size(kTodosCampos);
// Fields whose change needs the components reattached (read when a component attaches).
constexpr bool CampoReanexa(uint32_t offset) { return offset == 0 || offset == 12 || offset == 16; }

// The value written for a field at detail level `modo`, from the game's own value `orig` (raw 32-bit word).
uint32_t ValorDetalle(const CampoDetalle& c, uint32_t orig, int32_t modo) {
  if (modo <= 0) return orig;
  const int32_t paso = modo >= 2 ? 2 : 1;
  switch (c.offset) {
    case 0: {  // DetailMode: 0 low, 1 medium, 2 high
      const int32_t v = int32_t(orig);
      return uint32_t(modo >= 2 ? 0 : std::max(0, v - 1));
    }
    case 76:
    case 80:
      return uint32_t(int32_t(orig) + paso);
    case 12:
    case 16:
      return 0u;
    case 32:
    case 36:
    case 60:
      return modo >= 2 ? 0u : orig;
    case 388: {
      const float f = std::bit_cast<float>(orig);
      if (!(f > 0.0f)) return orig;
      return std::bit_cast<uint32_t>(f * (modo >= 2 ? 0.5f : 0.75f));
    }
    default:
      return orig;
  }
}

std::string TextoCampo(const CampoDetalle& c, uint32_t v) {
  if (c.tipo == TipoCampo::kFloat) return fmt::format("{}", std::bit_cast<float>(v));
  if (c.tipo == TipoCampo::kBool) return v ? "True" : "False";
  return fmt::format("{}", int32_t(v));
}

// The float written for each menu value. The engine takes trunc(size * float(ScreenPercentage * 0.01f)); 85 * 0.01f
// rounds to 0.84999996 and would give 1087 columns, so the values sit a hair above the nominal one. 67 is written as
// 66.67 so the height is exactly 480 (853x480). 50 is exact (50 * 0.01f rounds to 0.5: 640x360).
float PorcentajeReal(int32_t pedido) {
  switch (pedido) {
    case 100: return 100.0f;
    case 85: return 85.001f;
    case 75: return 75.001f;
    case 67: return 66.67f;
    case 50: return 50.0f;
    default: return float(pedido) + 0.001f;
  }
}

float LeerFloat(uint8_t* base, uint32_t direccion) { return std::bit_cast<float>(uint32_t(REX_LOAD_U32(direccion))); }
void EscribirFloat(uint8_t* base, uint32_t direccion, float valor) {
  REX_STORE_U32(direccion, std::bit_cast<uint32_t>(valor));
}

// One data block we patched. The shadow fields are always computed from the game's own values (`orig_*`), so the
// cut can go back and forth live, and a later load of a section without those keys (GConfig leaves the value as it
// was, i.e. ours) does not halve the shadow resolution or lower the bias a second time.
struct Bloque {
  uint32_t direccion = 0;
  bool registrado = false;  // logged once
  bool aplicado = false;    // ResX/ResY were 1280x720 and the values were written: live changes go here too
  bool escrito = false;     // the escrito_* fields hold what was written last
  uint32_t orig_dinamicas = 0, escrito_dinamicas = 0;
  int32_t orig_max = 0, escrito_max = 0;
  int32_t orig_sesgo = 0, escrito_sesgo = 0;
  // 0.1.20 (xmow_detalle): the game's value and what was written last, per kCamposDetalle field (raw words).
  bool escrito_detalle = false;
  std::array<uint32_t, kNumCamposDetalle> orig_detalle{}, escrito_detalle_v{};
  // 0.1.29 (xmow_iluminacao per map): the game's DynamicLights and what was written last.
  bool luz_tomada = false;
  uint32_t orig_luz = 0, escrito_luz = 0;
  // 0.1.33 (kCamposMapa): the game's values and what was written last.
  std::array<bool, 5> mapa_tomado{};
  std::array<uint32_t, 5> mapa_orig{}, mapa_escrito{};
};
std::mutex g_cerrojo;
std::array<Bloque, 8> g_bloques{};
bool g_principal_aplicado = false;
float g_principal_porcentaje = 100.0f;
// What the main block holds, as cvar values (-1 = nothing applied yet). Read by the game thread every frame.
std::atomic<int32_t> g_escala_aplicada{-1};
std::atomic<int32_t> g_sombras_aplicadas{-1};
std::atomic<int32_t> g_detalle_aplicado{-1};
// 0.1.22: startup values of xmow_sombras / xmow_detalle (-1 = not latched yet), guarded by g_cerrojo.
int32_t g_sombras_inicio = -1;
int32_t g_detalle_inicio = -1;
// 0.1.29 (xmow_iluminacao per map): a gameplay map is loaded (set by XmowIluminacaoPorMapa), the guest memory base
// seen by the first settings load (the package hook runs outside any guest call). Guarded by g_cerrojo.
bool g_mapa_jogo = false;
uint8_t* g_base_luz = nullptr;
constexpr uint32_t kDynamicLights = 20;  // UBOOL

// xmow_detalle on one block: the game's values taken first (a field that differs from what we wrote is the game's),
// then each field written from them. Returns the log text; `reanexar` is set when a field read at attach changed.
std::string AplicarDetalle(uint8_t* base, uint32_t datos, Bloque& b, int32_t modo, bool* reanexar) {
  std::string texto = fmt::format("detail {}:", modo);
  for (size_t i = 0; i < kNumCamposDetalle; ++i) {
    const CampoDetalle& c = kCamposDetalle[i];
    const uint32_t actual = REX_LOAD_U32(datos + c.offset);
    if (!b.escrito_detalle || actual != b.escrito_detalle_v[i]) b.orig_detalle[i] = actual;
    const uint32_t nuevo = ValorDetalle(c, b.orig_detalle[i], modo);
    REX_STORE_U32(datos + c.offset, nuevo);
    b.escrito_detalle_v[i] = nuevo;
    if (nuevo != actual && reanexar && CampoReanexa(c.offset)) *reanexar = true;
    texto += fmt::format(" {} (+{}) {} -> {} (game {});", c.nombre, c.offset, TextoCampo(c, actual),
                         TextoCampo(c, nuevo), TextoCampo(c, b.orig_detalle[i]));
  }
  b.escrito_detalle = true;
  return texto;
}

// More than 8 blocks: from the values in place (once per load).
std::string AplicarDetalleSinRegistro(uint8_t* base, uint32_t datos, int32_t modo) {
  if (modo <= 0) return "detail 0 (game settings)";
  for (const CampoDetalle& c : kCamposDetalle) {
    REX_STORE_U32(datos + c.offset, ValorDetalle(c, REX_LOAD_U32(datos + c.offset), modo));
  }
  return fmt::format("detail {}", modo);
}

Bloque* BuscarBloque(uint32_t direccion) {
  for (Bloque& b : g_bloques) {
    if (b.direccion == direccion) return &b;
  }
  for (Bloque& b : g_bloques) {
    if (b.direccion == 0) {
      b.direccion = direccion;
      return &b;
    }
  }
  return nullptr;  // more than 8 blocks: still applied, never logged, not changed live
}

const char* NombreBloque(uint32_t datos) {
  if (datos == kDatosPrincipal) return "GSystemSettings";
  const uint32_t primero = kGSystemSettings + 548;
  if (datos >= primero && datos < primero + 5 * 512 && (datos - primero) % 512 == 0) return "bucket";
  return "other";
}

const char* NombreSombras(int32_t modo) { return modo <= 0 ? "ALL" : modo >= 2 ? "OFF" : "REDUCED"; }

// After a load: the shadow fields that differ from what we wrote last are the game's (new) values.
void TomarOriginales(uint8_t* base, uint32_t datos, Bloque& b) {
  const uint32_t dinamicas = REX_LOAD_U32(datos + kDynamicShadows);
  const int32_t max_res = int32_t(REX_LOAD_U32(datos + kMaxShadowResolution));
  const int32_t sesgo = int32_t(REX_LOAD_U32(datos + kShadowFilterQualityBias));
  if (!b.escrito || dinamicas != b.escrito_dinamicas) b.orig_dinamicas = dinamicas;
  if (!b.escrito || max_res != b.escrito_max) b.orig_max = max_res;
  if (!b.escrito || sesgo != b.escrito_sesgo) b.orig_sesgo = sesgo;
}

// The shadow cut on one block, from the game's values in `b`. Returns the text for the log.
std::string AplicarSombras(uint8_t* base, uint32_t datos, Bloque& b, int32_t modo) {
  const int32_t min_res = int32_t(REX_LOAD_U32(datos + kMinShadowResolution));
  const uint32_t antes_dinamicas = REX_LOAD_U32(datos + kDynamicShadows);
  const int32_t antes_max = int32_t(REX_LOAD_U32(datos + kMaxShadowResolution));
  const int32_t antes_sesgo = int32_t(REX_LOAD_U32(datos + kShadowFilterQualityBias));
  uint32_t dinamicas = b.orig_dinamicas;
  int32_t max_res = b.orig_max;
  int32_t sesgo = b.orig_sesgo;
  if (modo >= 2) {
    dinamicas = 0;  // DynamicShadows exists in this build's table (+424), so OFF is real
  } else if (modo == 1) {
    max_res = std::max(min_res, b.orig_max / 2);
    sesgo = b.orig_sesgo - 1;
  }
  REX_STORE_U32(datos + kDynamicShadows, dinamicas);
  REX_STORE_U32(datos + kMaxShadowResolution, uint32_t(max_res));
  REX_STORE_U32(datos + kShadowFilterQualityBias, uint32_t(sesgo));
  b.escrito = true;
  b.escrito_dinamicas = dinamicas;
  b.escrito_max = max_res;
  b.escrito_sesgo = sesgo;
  return fmt::format("shadows {}: DynamicShadows {} -> {}, MaxShadowResolution {} -> {} (min {}, game {}), "
                     "ShadowFilterQualityBias {} -> {} (game {})",
                     NombreSombras(modo), antes_dinamicas, dinamicas, antes_max, max_res, min_res, b.orig_max,
                     antes_sesgo, sesgo, b.orig_sesgo);
}

// The old cut, for a block with no Bloque (more than 8): from the values in place, so only once per load.
std::string AplicarSombrasSinRegistro(uint8_t* base, uint32_t datos, int32_t modo) {
  if (modo <= 0) return "shadows ALL (game settings)";
  if (modo >= 2) {
    REX_STORE_U32(datos + kDynamicShadows, 0u);
    return "shadows OFF";
  }
  const int32_t min_res = int32_t(REX_LOAD_U32(datos + kMinShadowResolution));
  const int32_t max_res = int32_t(REX_LOAD_U32(datos + kMaxShadowResolution));
  REX_STORE_U32(datos + kMaxShadowResolution, uint32_t(std::max(min_res, max_res / 2)));
  REX_STORE_U32(datos + kShadowFilterQualityBias,
                uint32_t(int32_t(REX_LOAD_U32(datos + kShadowFilterQualityBias)) - 1));
  return "shadows REDUCED";
}

// ScreenPercentage (and the upscale flag) on one block. Returns the value written.
float AplicarEscala(uint8_t* base, uint32_t datos, int32_t pedido) {
  const float nuevo = PorcentajeReal(pedido);
  EscribirFloat(base, datos + kScreenPercentage, nuevo);
  if (nuevo < 100.0f && REX_LOAD_U32(datos + kUpscaleScreenPercentage) == 0) {
    REX_STORE_U32(datos + kUpscaleScreenPercentage, 1u);
  }
  return nuevo;
}

// 0.1.29 (xmow_iluminacao per map) on one block, under g_cerrojo: DynamicLights = 0 with NONE on a gameplay map, the
// game's value otherwise (a value that differs from what we wrote last is the game's).
std::string AplicarLuz(uint8_t* base, uint32_t datos, Bloque* b, int32_t modo) {
  const uint32_t actual = REX_LOAD_U32(datos + kDynamicLights);
  uint32_t juego = actual;
  if (b) {
    if (!b->luz_tomada || actual != b->escrito_luz) b->orig_luz = actual;
    b->luz_tomada = true;
    juego = b->orig_luz;
  }
  const bool ninguna = modo >= 2 && g_mapa_jogo;
  const uint32_t nuevo = ninguna ? 0u : juego;
  if (nuevo != actual) REX_STORE_U32(datos + kDynamicLights, nuevo);
  if (b) b->escrito_luz = nuevo;
  return fmt::format("lighting {} (xmow_iluminacao {}, {} map): DynamicLights {} -> {}", ninguna ? "NONE" : "STANDARD",
                     modo, g_mapa_jogo ? "gameplay" : "frontend", actual, nuevo);
}

// 0.1.33: the quality toggles written per map (gameplay maps only), under g_cerrojo, as AplicarLuz.
struct CampoMapa {
  uint32_t offset;
  const char* nombre;
};
constexpr CampoMapa kCamposMapa[] = {
    {44, "Bloom"}, {52, "Distortion"}, {64, "FogVolumes"}, {4, "SpeedTreeLeaves"}, {8, "SpeedTreeFronds"}};
static_assert(std::size(kCamposMapa) == 5);
bool CampoMapaApagado(size_t i) {
  switch (i) {
    case 0: return REXCVAR_GET(xmow_sem_bloom);
    case 1: return REXCVAR_GET(xmow_sem_distorcao);
    case 2: return REXCVAR_GET(xmow_sem_volumes_nevoa);
    default: return REXCVAR_GET(xmow_diag_sem_speedtree);
  }
}
uint32_t FirmaCamposMapa() {
  uint32_t f = 0;
  for (size_t i = 0; i < std::size(kCamposMapa); ++i) f |= uint32_t(CampoMapaApagado(i)) << i;
  return f;
}
std::string AplicarCamposMapa(uint8_t* base, uint32_t datos, Bloque* b) {
  std::string texto;
  for (size_t i = 0; i < std::size(kCamposMapa); ++i) {
    const uint32_t direccion = datos + kCamposMapa[i].offset;
    const uint32_t actual = REX_LOAD_U32(direccion);
    uint32_t juego = actual;
    if (b) {
      if (!b->mapa_tomado[i] || actual != b->mapa_escrito[i]) b->mapa_orig[i] = actual;
      b->mapa_tomado[i] = true;
      juego = b->mapa_orig[i];
    }
    const bool apagar = g_mapa_jogo && CampoMapaApagado(i);
    const uint32_t nuevo = apagar ? 0u : juego;
    if (nuevo != actual) REX_STORE_U32(direccion, nuevo);
    if (b) b->mapa_escrito[i] = nuevo;
    if (apagar || nuevo != actual) {
      texto += fmt::format("{}{} {} -> {}", texto.empty() ? "" : ", ", kCamposMapa[i].nombre, actual, nuevo);
    }
  }
  return "map toggles (" + std::string(g_mapa_jogo ? "gameplay" : "frontend") + " map): " +
         (texto.empty() ? std::string("game's values") : texto);
}

// After a LoadFromIni of the block at `datos`.
void Aplicar(uint8_t* base, uint32_t datos) {
  std::lock_guard<std::mutex> cerrojo(g_cerrojo);
  // Only the main block and the 5 buckets are kept for live changes (review): the SCALE console command loads into a
  // temporary on its stack, whose address must never be written again later. Others are patched once, unregistered.
  Bloque* b = std::string_view(NombreBloque(datos)) != "other" ? BuscarBloque(datos) : nullptr;
  const bool registrar = b && !b->registrado;
  if (b) b->registrado = true;
  const uint32_t res_x = REX_LOAD_U32(datos + kResX);
  const uint32_t res_y = REX_LOAD_U32(datos + kResY);
  const float antes = LeerFloat(base, datos + kScreenPercentage);
  const uint32_t upscale = REX_LOAD_U32(datos + kUpscaleScreenPercentage);
  if (res_x != 1280 || res_y != 720) {
    if (registrar) {
      REXLOG_INFO("[xmow res] {:08X} ({}): ResX/ResY {}x{} (not 1280x720): nothing applied (ScreenPercentage {}, "
                  "Upscale {})",
                  datos, NombreBloque(datos), res_x, res_y, antes, upscale);
    }
    return;
  }
  const int32_t pedido = REXCVAR_GET(xmow_escala_interna);
  // 0.1.22: shadows and detail are latched at the first load; a menu change only applies at the next start (changing
  // them live freed guest heap on the render thread and crashed).
  if (g_sombras_inicio < 0) g_sombras_inicio = REXCVAR_GET(xmow_sombras);
  if (g_detalle_inicio < 0) g_detalle_inicio = REXCVAR_GET(xmow_detalle);
  g_base_luz = base;
  const int32_t modo = g_sombras_inicio;
  const float nuevo = AplicarEscala(base, datos, pedido);
  std::string sombras;
  if (b) {
    TomarOriginales(base, datos, *b);
    sombras = AplicarSombras(base, datos, *b, modo);
    b->aplicado = true;
  } else {
    sombras = AplicarSombrasSinRegistro(base, datos, modo);
  }
  // 0.1.20 (xmow_detalle): before any level is loaded, so no reattach is needed here.
  const int32_t detalle = g_detalle_inicio;
  sombras += "; " + (b ? AplicarDetalle(base, datos, *b, detalle, nullptr) : AplicarDetalleSinRegistro(base, datos, detalle));
  // 0.1.29 (xmow_iluminacao): NONE writes DynamicLights = 0 only while a gameplay map is loaded (none is at the first
  // load: the frontend); otherwise the game's value.
  sombras += "; " + AplicarLuz(base, datos, b, REXCVAR_GET(xmow_iluminacao));
  sombras += "; " + AplicarCamposMapa(base, datos, b);  // 0.1.33
  if (datos == kDatosPrincipal) {
    g_principal_aplicado = true;
    g_principal_porcentaje = nuevo;
    g_escala_aplicada.store(pedido, std::memory_order_release);
    rex::ui::FijarResolucionContador(uint32_t(1280 * (nuevo * 0.01f)), uint32_t(720 * (nuevo * 0.01f)));  // 0.1.29
    g_sombras_aplicadas.store(modo, std::memory_order_release);
    g_detalle_aplicado.store(detalle, std::memory_order_release);
  }
  if (registrar) {
    REXLOG_INFO("[xmow res] {:08X} ({}): ResX/ResY {}x{}; ScreenPercentage {} -> {} (xmow_escala_interna {}, view "
                "{}x{}); UpscaleScreenPercentage {} -> {}; {}",
                datos, NombreBloque(datos), res_x, res_y, antes, nuevo, pedido, int(1280 * (nuevo * 0.01f)),
                int(720 * (nuevo * 0.01f)), upscale, REX_LOAD_U32(datos + kUpscaleScreenPercentage), sombras);
  }
}

// 0.1.26 (pesquisa-fps-picos.md item 1b): every field of the main block (kTodosCampos), as last logged; the game thread
// compares against it (VigilarCampos) and logs what changed. Guarded by g_cerrojo.
std::array<uint32_t, kNumTodosCampos> g_instantanea{};
bool g_instantanea_tomada = false;

void RegistrarTodosCampos(uint8_t* base, const char* cuando) {
  std::string texto[3];
  for (size_t i = 0; i < kNumTodosCampos; ++i) {
    const CampoDetalle& c = kTodosCampos[i];
    const uint32_t v = REX_LOAD_U32(kDatosPrincipal + c.offset);
    g_instantanea[i] = v;
    texto[size_t(c.tipo)] += fmt::format(" {} {};", c.nombre, TextoCampo(c, v));
  }
  g_instantanea_tomada = true;
  REXLOG_INFO("[xmow settings] {}: all {} FSystemSettings fields of the block the renderer reads (0x{:08X})", cuando,
              kNumTodosCampos, kDatosPrincipal);
  REXLOG_INFO("[xmow settings] ints:{}", texto[size_t(TipoCampo::kEntero)]);
  REXLOG_INFO("[xmow settings] floats:{}", texto[size_t(TipoCampo::kFloat)]);
  REXLOG_INFO("[xmow settings] bools:{}", texto[size_t(TipoCampo::kBool)]);
}

// Whether the main block still holds what xmow_detalle wrote into it (kCamposDetalle). Under g_cerrojo.
std::string ComprobarDetalle(uint8_t* base) {
  const int32_t modo = g_detalle_aplicado.load(std::memory_order_acquire);
  if (modo < 0) {
    return fmt::format("xmow_detalle {} NOT applied: the main block was never patched (see the [xmow res] lines)",
                       REXCVAR_GET(xmow_detalle));
  }
  const Bloque* b = nullptr;
  for (const Bloque& x : g_bloques) {
    if (x.direccion == kDatosPrincipal) b = &x;
  }
  if (!b || !b->escrito_detalle) return fmt::format("xmow_detalle {}: no record of the written values", modo);
  std::string diferentes;
  for (size_t i = 0; i < kNumCamposDetalle; ++i) {
    const CampoDetalle& c = kCamposDetalle[i];
    const uint32_t actual = REX_LOAD_U32(kDatosPrincipal + c.offset);
    if (actual != b->escrito_detalle_v[i]) {
      diferentes += fmt::format(" {} is {} (written {}, game {});", c.nombre, TextoCampo(c, actual),
                                TextoCampo(c, b->escrito_detalle_v[i]), TextoCampo(c, b->orig_detalle[i]));
    }
  }
  if (diferentes.empty()) {
    return fmt::format("xmow_detalle {} applied: the {} detail fields hold the values written at the ini load{}", modo,
                       kNumCamposDetalle, modo == 0 ? " (0 = the game's own values)" : "");
  }
  return fmt::format("xmow_detalle {} NOT holding, changed after it was written:{}", modo, diferentes);
}

// After FSystemSettings::Initialize: the final state of the block the renderer reads.
void RevisarPrincipal(uint8_t* base) {
  std::lock_guard<std::mutex> cerrojo(g_cerrojo);
  const uint32_t datos = kDatosPrincipal;
  float porcentaje = LeerFloat(base, datos + kScreenPercentage);
  const char* nota = "";
  if (g_principal_aplicado && porcentaje != g_principal_porcentaje) {
    EscribirFloat(base, datos + kScreenPercentage, g_principal_porcentaje);
    if (g_principal_porcentaje < 100.0f) REX_STORE_U32(datos + kUpscaleScreenPercentage, 1u);
    nota = " (changed after the load: written again)";
    porcentaje = g_principal_porcentaje;
  }
  const uint32_t bloqueo = REX_LOAD_U32(kBloqueoEscala);
  REXLOG_INFO("[xmow res] after FSystemSettings init: {}; ScreenPercentage {}{}, UpscaleScreenPercentage {}, "
              "ResX/ResY {}x{} (scene globals), GSystemSettings+544 = {}{}, DynamicShadows {}, MaxShadowResolution {}, "
              "ShadowFilterQualityBias {}",
              g_principal_aplicado ? "applied" : "NOT applied (see the lines above)", porcentaje, nota,
              REX_LOAD_U32(datos + kUpscaleScreenPercentage), REX_LOAD_U32(datos + kResX), REX_LOAD_U32(datos + kResY),
              bloqueo, bloqueo ? " (the engine's upscale is OFF while it is not 0)" : "",
              REX_LOAD_U32(datos + kDynamicShadows), int32_t(REX_LOAD_U32(datos + kMaxShadowResolution)),
              int32_t(REX_LOAD_U32(datos + kShadowFilterQualityBias)));
  std::string detalle;
  for (const CampoDetalle& c : kCamposDetalle) {
    detalle += fmt::format(" {} {};", c.nombre, TextoCampo(c, REX_LOAD_U32(datos + c.offset)));
  }
  REXLOG_INFO("[xmow res] after FSystemSettings init: detail fields (xmow_detalle {}):{}", REXCVAR_GET(xmow_detalle),
              detalle);
  // 0.1.26: Initialize runs once in engine init (sub_831EF9E8), before the engine loads its first map; xmow_detalle is
  // the value xmow.toml had when the app started (it is latched at the first ini load, see Aplicar).
  RegistrarTodosCampos(base, "after FSystemSettings init (engine init, before the first map)");
  REXLOG_INFO("[xmow settings] detail at FSystemSettings init: {}", ComprobarDetalle(base));
}

// 0.1.26: on the game thread, at the first engine tick (the first map is loaded by then) and then every 30 s: the
// fields that changed since the last log, and the detail check again when one of them did. Reads only (56 words).
void VigilarCampos(uint8_t* base) {
  using reloj = std::chrono::steady_clock;
  static bool primera = true;
  static reloj::time_point proxima{};
  const reloj::time_point ahora = reloj::now();
  if (!primera && ahora < proxima) return;
  proxima = ahora + std::chrono::seconds(30);
  std::lock_guard<std::mutex> cerrojo(g_cerrojo);
  if (!g_instantanea_tomada) {
    RegistrarTodosCampos(base, primera ? "first engine tick (no FSystemSettings init seen)" : "game thread");
    REXLOG_INFO("[xmow settings] detail at the first engine tick: {}", ComprobarDetalle(base));
    primera = false;
    return;
  }
  std::string cambios;
  for (size_t i = 0; i < kNumTodosCampos; ++i) {
    const CampoDetalle& c = kTodosCampos[i];
    const uint32_t v = REX_LOAD_U32(kDatosPrincipal + c.offset);
    if (v != g_instantanea[i]) {
      cambios += fmt::format(" {} {} -> {};", c.nombre, TextoCampo(c, g_instantanea[i]), TextoCampo(c, v));
      g_instantanea[i] = v;
    }
  }
  if (primera) {
    REXLOG_INFO("[xmow settings] first engine tick (first map loaded): {}; fields changed since FSystemSettings "
                "init:{}",
                ComprobarDetalle(base), cambios.empty() ? " none" : cambios);
  } else if (!cambios.empty()) {
    REXLOG_INFO("[xmow settings] fields changed in the last 30 s:{}; {}", cambios, ComprobarDetalle(base));
  }
  primera = false;
}

}  // namespace
}  // namespace xmow::resolucao

// The engine's FlushRenderingCommands (FRenderCommandFence begin + wait): called on the game thread before a live write.
REX_EXTERN(sub_824E7B50);

namespace xmow::resolucao {
namespace {

// 0.1.33 (xmow_resolucao_dinamica, game thread, once per frame): the scale to use, `pedido` (the menu's) or one step
// below it while the GPU alone holds the frame rate down. GPU busy = the renderer's works first-to-last timestamp time
// minus their waits for the next partial submit (read back 2-3 frames late), averaged over 1 s windows.
int32_t EscalaDinamica(int32_t pedido) {
  using reloj = std::chrono::steady_clock;
  static bool reducido = false;
  static reloj::time_point inicio{}, cambio{};
  static uint64_t ns_inicio = 0;
  static uint32_t cuadros = 0, altas = 0, bajas = 0;
  const int32_t baja = pedido >= 100 ? 85 : pedido >= 85 ? 75 : pedido > 67 ? 67 : pedido;
  const reloj::time_point ahora = reloj::now();
  const uint64_t ns = rex::x360::g_xmow_gpu_ocupada_ns.load(std::memory_order_relaxed);
  if (REXCVAR_GET(xmow_resolucao_dinamica) && baja == pedido) {
    static int32_t avisado = -1;  // 0.1.33c (review S3)
    if (avisado != pedido) {
      avisado = pedido;
      REXLOG_INFO("[xmow res] dynamic resolution: inactive, nothing below {} % (xmow_escala_interna)", pedido);
    }
  }
  if (!REXCVAR_GET(xmow_resolucao_dinamica) || baja == pedido || !XmowMapaDeJogo()) {
    if (reducido) {
      REXLOG_INFO("[xmow res] dynamic resolution: back to {} % (off, frontend or nothing below)", pedido);
    }
    reducido = false;
    altas = bajas = cuadros = 0;
    inicio = ahora;
    ns_inicio = ns;
    return pedido;
  }
  if (cuadros == 0 && inicio == reloj::time_point{}) {
    inicio = ahora;
    ns_inicio = ns;
  }
  ++cuadros;
  const double segundos = std::chrono::duration<double>(ahora - inicio).count();
  if (segundos < 1.0) return reducido ? baja : pedido;
  const double ms = double(ns - ns_inicio) / 1e6 / cuadros;  // GPU busy per game frame
  const double fps = cuadros / segundos;
  inicio = ahora;
  ns_inicio = ns;
  cuadros = 0;
  if (!reducido) {
    altas = (ms > 31.7 && fps < 29.0) ? altas + 1 : 0;
    if (altas >= 2) {
      reducido = true;
      altas = bajas = 0;
      cambio = ahora;
      REXLOG_INFO("[xmow res] dynamic resolution: {} -> {} % (GPU busy {:.1f} ms per frame at {:.1f} fps)", pedido,
                  baja, ms, fps);
    }
  } else {
    // Back up only when the full scale would still fit: the pixel work grows with the area (k = (pedido/baja)^2),
    // taken as all of the GPU time (conservative).
    const double k = (double(pedido) / baja) * (double(pedido) / baja);
    bajas = (ms * k < 30.6 && ahora - cambio >= std::chrono::seconds(5)) ? bajas + 1 : 0;
    if (bajas >= 3) {
      reducido = false;
      altas = bajas = 0;
      cambio = ahora;
      REXLOG_INFO("[xmow res] dynamic resolution: {} -> {} % (GPU busy {:.1f} ms per frame at {:.1f} fps)", baja,
                  pedido, ms, fps);
    }
  }
  return reducido ? baja : pedido;
}

// 0.1.33d (review S-B) / 0.1.33e (review F-1): back on the main menu? Counted in FRAMES (game thread, start of each
// frame), from the fog draws the ring classified since the last frame. Only on a gameplay map, only after the map was
// really played: kSinFrontalJugado frames in a row without any frontend fog draw since its package opened (a stray
// gameplay fog draw during the load no longer counts). Then kFrontalSeguidos frames in a row with frontend fog and no
// gameplay fog (a frame with neither does not break the run unless kHuecoMax of them follow each other), at least
// kSegundosTrasPaquete after the package opened (test 39: the main menu's fog stays 11-13 s while the level loads).
constexpr uint32_t kFrontalSeguidos = 90;     // about 3 s of frames
constexpr uint32_t kSinFrontalJugado = 300;   // about 10 s of frames
constexpr uint32_t kHuecoMax = 10;
constexpr int64_t kSegundosTrasPaquete = 20;
void VolverAlMenuPorNiebla() {
  static uint32_t gen_vista = UINT32_MAX;
  static uint64_t frontal_visto = 0, otra_vista = 0;
  static uint32_t sin_frontal = 0, seguidos = 0, hueco = 0;
  static bool jugado = false;
  if (!XmowMapaDeJogo()) return;
  const uint32_t gen = g_paquete_jogo_gen.load(std::memory_order_acquire);
  const uint64_t frontal = g_niebla_frontal_total.load(std::memory_order_relaxed);
  const uint64_t otra = g_niebla_otra_total.load(std::memory_order_relaxed);
  if (gen != gen_vista) {
    gen_vista = gen;
    frontal_visto = frontal;
    otra_vista = otra;
    sin_frontal = seguidos = hueco = 0;
    jugado = false;
    return;
  }
  const bool hubo_frontal = frontal != frontal_visto, hubo_otra = otra != otra_vista;
  frontal_visto = frontal;
  otra_vista = otra;
  if (hubo_frontal) {
    sin_frontal = 0;
    hueco = 0;
    seguidos = hubo_otra ? 0 : seguidos + 1;
  } else {
    if (!jugado && ++sin_frontal >= kSinFrontalJugado) jugado = true;
    if (hubo_otra || ++hueco >= kHuecoMax) seguidos = 0;
  }
  if (!jugado || seguidos < kFrontalSeguidos) return;
  const int64_t desde = AhoraNsSteady() - g_paquete_jogo_ns.load(std::memory_order_relaxed);
  if (desde < kSegundosTrasPaquete * 1000000000ll) return;
  XMOW_LOG_CONTEXTO("[xmow fog] back to the main menu: {} frames in a row with the frontend fog, {:.1f} s after the last "
              "gameplay map package (played: {} frames without it before); the per map settings go back to the "
              "frontend's",
              seguidos, double(desde) / 1e9, kSinFrontalJugado);
  XmowIluminacaoPorMapa("main menu (fog constants)", false);
  if (REXCVAR_GET(xmow_iluminacao) >= 2) {
    REXLOG_INFO("[xmow light] main menu (fog constants): DynamicLights is read when a light attaches; the lights the "
                "main menu attached while the gameplay value was in place stay without light until the next map "
                "load (no live reattach: removed in 0.1.22). Bloom, distortion, fog volumes and the resolution apply "
                "now");
  }
}

// Start of FEngineLoop::Tick (game thread, once per frame): the menu's live changes (only the scale is live).
void AlInicioDelCuadro(PPCContext& ctx, uint8_t* base) {
  VigilarCampos(base);  // 0.1.26
  VolverAlMenuPorNiebla();  // 0.1.33d (review S-B)
  // 0.1.33: a per map toggle changed in the menu: every block gets it now.
  {
    static uint32_t firma_vista = UINT32_MAX;
    const uint32_t firma = FirmaCamposMapa();
    if (firma_vista == UINT32_MAX) {
      firma_vista = firma;
    } else if (firma != firma_vista) {
      firma_vista = firma;
      std::lock_guard<std::mutex> cerrojo(g_cerrojo);
      std::string texto;
      for (Bloque& b : g_bloques) {
        if (!b.aplicado || !b.direccion) continue;
        const std::string t = AplicarCamposMapa(base, b.direccion, &b);
        if (texto.empty() || b.direccion == kDatosPrincipal) texto = t;
      }
      REXLOG_INFO("[xmow map] live: {}", texto.empty() ? std::string("no block loaded yet") : texto);
    }
  }
  const int32_t escala_aplicada = g_escala_aplicada.load(std::memory_order_acquire);
  if (escala_aplicada < 0) return;  // the main block was never patched (not loaded yet, or not 1280x720)
  const int32_t pedido = EscalaDinamica(REXCVAR_GET(xmow_escala_interna));  // 0.1.33
  // 0.1.22: shadows / detail never reach the engine mid-game: only logged once per new value.
  {
    static int32_t sombras_avisada = -1, detalle_avisado = -1;
    const int32_t sombras_ini = g_sombras_aplicadas.load(std::memory_order_acquire);
    const int32_t detalle_ini = g_detalle_aplicado.load(std::memory_order_acquire);
    const int32_t modo = REXCVAR_GET(xmow_sombras);
    const int32_t detalle = REXCVAR_GET(xmow_detalle);
    if (modo != sombras_ini && modo != sombras_avisada) {
      REXLOG_INFO("[xmow res] xmow_sombras {} -> {} saved: applies at the next start", sombras_ini, modo);
      sombras_avisada = modo;
    }
    if (detalle != detalle_ini && detalle != detalle_avisado) {
      REXLOG_INFO("[xmow res] xmow_detalle {} -> {} saved: applies at the next start", detalle_ini, detalle);
      detalle_avisado = detalle;
    }
  }
  if (pedido == escala_aplicada) return;

  // The rendering thread finishes everything queued (the previous frame), so no pass of it sees half a change. The
  // call runs on this thread's context: everything it changes in the context is put back, as the SDK's ExecuteTrap
  // does, so the tick starts with its own registers.
  PPCContext guardado = ctx;
  sub_824E7B50(ctx, base);
  ctx = guardado;
  ctx.fpscr.restoreGuestBits(guardado.fpscr.csr);

  std::unique_lock<std::mutex> cerrojo(g_cerrojo);
  const uint32_t datos = kDatosPrincipal;
  const float antes = LeerFloat(base, datos + kScreenPercentage);
  float nuevo = 100.0f;
  for (Bloque& b : g_bloques) {
    if (b.aplicado) nuevo = AplicarEscala(base, b.direccion, pedido);  // the main block and the 5 buckets
  }
  g_principal_porcentaje = nuevo;
  REXLOG_INFO("[xmow res] live: ScreenPercentage {} -> {} (xmow_escala_interna {} -> {}, view {}x{}, "
              "UpscaleScreenPercentage {}, GSystemSettings+544 = {})",
              antes, nuevo, escala_aplicada, pedido, int(1280 * (nuevo * 0.01f)), int(720 * (nuevo * 0.01f)),
              REX_LOAD_U32(datos + kUpscaleScreenPercentage), REX_LOAD_U32(kBloqueoEscala));
  g_escala_aplicada.store(pedido, std::memory_order_release);
  rex::ui::FijarResolucionContador(uint32_t(1280 * (nuevo * 0.01f)), uint32_t(720 * (nuevo * 0.01f)));  // 0.1.29
}
}  // namespace
}  // namespace xmow::resolucao

// 0.1.29 (xmow_iluminacao per map): called by gow2_gpu.cpp's package hook on the thread that opens a map package, before
// the map's actors attach their lights (DynamicLights is read at attach). `jogo` = a gameplay map ("<map>_p.xxx");
// false = the frontend (WStart.xxx). Every block written at a settings load gets the value; quick, never blocks long.
// 0.1.32 (xmow_niebla_altura NO = gameplay maps only, gow2_gpu.cpp): the last map package opened was a gameplay map
// ("<map>_p.xxx"); false before the first one and after WStart.xxx (the frontend). Any thread.
std::atomic<bool> g_mapa_jogo_publico{false};
bool XmowMapaDeJogo() { return g_mapa_jogo_publico.load(std::memory_order_relaxed); }

void XmowIluminacaoPorMapa(const char* paquete, bool jogo) {
  using namespace xmow::resolucao;
  // 0.1.34: nothing to do when no test runs; 0.1.34b: a live (played) test goes on.
  xmow::bench::Abortar("a map package opened (the scene changed)", false);
  std::lock_guard<std::mutex> cerrojo(g_cerrojo);
  const bool antes = g_mapa_jogo;
  g_mapa_jogo = jogo;
  if (jogo) {  // 0.1.33d (review S-B): the main menu detection starts again for this map
    g_paquete_jogo_ns.store(AhoraNsSteady(), std::memory_order_relaxed);
    g_paquete_jogo_gen.fetch_add(1, std::memory_order_release);
  }
  g_mapa_jogo_publico.store(jogo, std::memory_order_relaxed);  // 0.1.32 (XmowMapaDeJogo)
  uint8_t* const base = g_base_luz;
  if (!base) {
    XMOW_LOG_CONTEXTO("[xmow light] map package {} ({} map): no settings block loaded yet, nothing written", paquete,
                jogo ? "gameplay" : "frontend");
    return;
  }
  const int32_t modo = REXCVAR_GET(xmow_iluminacao);
  std::string texto;
  uint32_t n = 0;
  for (Bloque& b : g_bloques) {
    if (!b.aplicado || !b.direccion) continue;
    const std::string t = AplicarLuz(base, b.direccion, &b, modo) + "; " + AplicarCamposMapa(base, b.direccion, &b);
    if (texto.empty() || b.direccion == kDatosPrincipal) texto = t;
    ++n;
  }
  XMOW_LOG_CONTEXTO("[xmow light] map package {} ({} map, was {}): {} blocks; {}", paquete, jogo ? "gameplay" : "frontend",
              antes ? "gameplay" : "frontend", n, texto.empty() ? std::string("no block") : texto);
}

// FSystemSettingsData::LoadFromIni(this r3, section r4, ini r5).
REX_EXTERN(__imp__sub_825838A0);
REX_HOOK_RAW(sub_825838A0) {
  const uint32_t datos = ctx.r3.u32;  // r3 is not kept by the callee
  __imp__sub_825838A0(ctx, base);
  if (datos) xmow::resolucao::Aplicar(base, datos);
}

// FSystemSettings::Initialize (this = GSystemSettings).
REX_EXTERN(__imp__sub_82584728);
REX_HOOK_RAW(sub_82584728) {
  __imp__sub_82584728(ctx, base);
  xmow::resolucao::RevisarPrincipal(base);
}

// FEngineLoop::Tick (this r3): GEngine->Tick, then GSystemSettings.UpdateRenderThreadSettings (sub_82585940) and the
// frame counter. The live changes go in before any of it.
REX_EXTERN(__imp__sub_831EE170);
REX_HOOK_RAW(sub_831EE170) {
  xmow::perf::AfinarHilo(xmow::perf::Papel::Juego);  // xmow_afinidade_threads: the game thread runs the engine tick
  xmow::resolucao::AlInicioDelCuadro(ctx, base);
  __imp__sub_831EE170(ctx, base);
}
