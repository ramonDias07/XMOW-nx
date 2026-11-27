// XMOW - the data of the ADVANCED SETTINGS menu (L + R + Right), used by gow2_menu.cpp. Pattern of the shared menu template
// (from the Budokai 3 deko port): options are DATA, not code. To
// expose one more cvar add one row to a table; rows whose cvar does not exist in this build are hidden at start (and
// logged), so a table can name a cvar another part of the port has not added yet.
//
// Every text here is drawn with the overlay's dot font: only A-Z 0-9 space . : / ( ) - + > ! , x % ' =
// (lower case draws as upper case; anything else, such as an underscore, a semicolon or a question mark, is BLANK).
// Labels are cut at 44 columns with the "> " cursor and the value; help texts longer than 44 scroll in the marquee.
// Options flagged `restart` show " (R)", keep the new value in the menu until SAVE TO CONFIG and apply after a restart.

#pragma once

#include <iterator>
#include <span>
#include <string_view>

namespace gow2::menu::tablas {

struct SettingChoice {
  std::string_view value;  // what is written to the cvar (bools: "true"/"false", the cvar getter's spelling)
  std::string_view label;  // what the menu shows
};

// Choice lists. A cvar holding a value that is not in its list is shown raw; numbers compare numerically.
constexpr SettingChoice kBoolean[] = {{"false", "NO"}, {"true", "YES"}};
constexpr SettingChoice kAnisotropy[] = {{"0", "DEFAULT"}, {"2", "2X"}, {"4", "4X"}, {"8", "8X"}};
constexpr SettingChoice kPartialSubmit[] = {
    {"0", "OFF"}, {"128", "128"}, {"256", "256"}, {"512", "512"}, {"1024", "1024"}};
constexpr SettingChoice kFirstSubmit[] = {{"0", "OFF"}, {"32", "32"}, {"48", "48"}, {"64", "64"}, {"128", "128"}};
constexpr SettingChoice kUploadMb[] = {{"16", "16 MB"}, {"32", "32 MB"}, {"64", "64 MB"}};
constexpr SettingChoice kTextureMb[] = {{"384", "384 MB"}, {"512", "512 MB"}, {"640", "640 MB"}};
constexpr SettingChoice kGeometryMb[] = {{"0", "OFF"}, {"32", "32 MB"}, {"64", "64 MB"}, {"96", "96 MB"}};
constexpr SettingChoice kIoCache[] = {{"0", "OFF"}, {"32", "32MB"}, {"64", "64MB"}, {"128", "128MB"}};
constexpr SettingChoice kAudioQueue[] = {{"6", "6"}, {"8", "8"}, {"10", "10"}, {"12", "12"}, {"14", "14"}, {"16", "16"}};
constexpr SettingChoice kRumble[] = {{"0.25", "25%"}, {"0.5", "50%"}, {"0.75", "75%"}, {"1", "100%"}};
// xmow_vblank_hz: 60 = the video mode's refresh (the game locks to 30 fps), 120 = the game can reach 60 fps.
constexpr SettingChoice kFrameRate[] = {{"60", "30 (ORIGINAL)"}, {"120", "UP TO 60 (EXPERIMENTAL)"}};
constexpr SettingChoice kAudioFrames[] = {{"4", "4"}, {"8", "8"}, {"16", "16"}, {"32", "32"}};
constexpr SettingChoice kXmaThread[] = {{"0", "REQUESTING THREAD"}, {"1", "XMA THREAD"}, {"2", "WORKER"}};
// xmow_omitir_ps as fixed choices: nothing skipped, or the full screen height fog (pixel shader 2639 of the dslib).
constexpr SettingChoice kSkippedShaders[] = {{"", "NONE"}, {"2639", "FOG"}};
// xmow_niebla_altura (gow2_gpu.cpp NieblaAlturaOmitida, live): the full screen height fog. AUTO = drawn everywhere, the
// main menu included (0.1.20: the 360's menu is the fog's blue water look); the frontend is only detected for the log.
constexpr SettingChoice kHeightFog[] = {{"0", "AUTO"}, {"1", "YES"}, {"2", "NO IN GAMEPLAY"}};  // 0.1.32: 2 keeps the menus' fog
// log_level accepts trace, debug, info, warn, error, critical, off (sdk-g2/src/core/logging.cpp); the menu offers three.
constexpr SettingChoice kLogLevel[] = {{"info", "INFO"}, {"warn", "WARNINGS"}, {"error", "ERRORS"}};
constexpr SettingChoice kAutoCapture[] = {{"0", "OFF"}, {"5", "5 S"}, {"10", "10 S"}, {"30", "30 S"}};
constexpr SettingChoice kTraces[] = {{"0", "0"}, {"2", "2"}, {"5", "5"}, {"20", "20"}};
constexpr SettingChoice kDumps[] = {{"0", "0"}, {"2", "2"}, {"4", "4"}};
// xmow_log_hitch_ms (gow2_gpu.cpp HitchPorSwap): frames longer than this go to the log as [xmow hitch] lines.
constexpr SettingChoice kHitch[] = {{"0", "OFF"}, {"33", "33 MS"}, {"50", "50 MS"}, {"100", "100 MS"}};
// xmow_escala_interna (xmow_resolucao_interna.cpp): UE3 ScreenPercentage of 1280x720 (quick menu INTERNAL RESOLUTION,
// live). The same sizes as the output list (gow2_menu.cpp kSalidas*), 1080 and 900 aside.
constexpr SettingChoice kInternalScale[] = {
    {"100", "1280X720"}, {"85", "1088X612"}, {"75", "960X540"}, {"67", "854X480"}, {"50", "640X360"}};
// xmow_sombras (xmow_resolucao_interna.cpp, applies at the next start): 0 = the game's shadows, 1 = reduced, 2 = no dynamic shadows.
constexpr SettingChoice kShadows[] = {{"0", "ALL"}, {"1", "REDUCED"}, {"2", "OFF"}};
// xmow_detalle (xmow_resolucao_interna.cpp, applies at the next start): UE3 DetailMode, LOD biases, decals, post effects.
// xmow 1024 (SDK): texture identity probe period and the geometry frame trust (frames).
constexpr SettingChoice kProbePeriod[] = {{"1", "EVERY FRAME"}, {"2", "2 FRAMES"}, {"4", "4 FRAMES"}, {"8", "8 FRAMES"}};
constexpr SettingChoice kGeometryTrust[] ={{"0", "OFF"}, {"4", "4 FRAMES"}, {"8", "8 FRAMES"}, {"16", "16 FRAMES"}};
constexpr SettingChoice kDetail[] = {{"0", "GAME"}, {"1", "REDUCED"}, {"2", "LOW"}};
// 0.1.28 (xmow_distancia_corte, codegen patch 2 in InitViews sub_82606A08): the scale of every primitive's cull distance.
constexpr SettingChoice kViewDistance[] = {{"100", "100%"}, {"75", "75%"}, {"50", "50%"}};
// 0.1.28 (xmow_iluminacao, applies at the next start): 0 = the game's lights, 2 = DynamicLights off. 1 (REDUCED) is kept
// free: no cheap and safe way to thin the per-light passes was found (see xmow_resolucao_interna.cpp).
// 0.1.28 (xmow_detalhe_malha, codegen patch 3 in InitViews): static meshes pick their LOD as if this % farther away.
constexpr SettingChoice kMeshDetail[] = {{"100", "HIGH"}, {"150", "MEDIUM"}, {"200", "LOW"}};
constexpr const char* kAyudaMeshDetail =
    "STATIC MESHES (ROCKS, PLANTS, BUILDINGS) SWITCH TO THEIR SIMPLER VERSIONS SOONER: MEDIUM AS IF 1.5x FARTHER, LOW "
    "AS IF 2x. FEWER TRIANGLES AND SOMETIMES FEWER DRAWS, MORE VISIBLE SWITCHING. HIGH IS THE GAME. APPLIES AT ONCE.";
constexpr SettingChoice kLighting[] = {{"0", "STANDARD"}, {"2", "NONE"}};
// 0.1.34f (the SDK's language key, xam_user.cpp EffectiveLanguage): the four languages on the disc, or the console's.
constexpr SettingChoice kLanguage[] = {
    {"System", "CONSOLE"}, {"English", "ENGLISH"}, {"French", "FRANCAIS"}, {"Spanish", "ESPANOL"}, {"Italian", "ITALIANO"}};
constexpr const char* kAyudaLanguage =
    "CONSOLE = THE SWITCH'S LANGUAGE, LIKE THE XBOX 360 (ENGLISH WHEN THE GAME DOES NOT HAVE IT). THE GAME HAS ENGLISH, "
    "FRENCH, SPANISH AND ITALIAN. SAVE AND CLOSE, THEN RESTART THE GAME.";
// 0.1.29 (xmow_corte_pequenos, codegen patch 4 in InitViews): objects whose bounding sphere looks a few pixels wide.
constexpr SettingChoice kSmallObjects[] = {{"0", "OFF"}, {"1", "LOW"}, {"2", "HIGH"}};
constexpr const char* kAyudaSmallObjects =
    "OBJECTS THAT LOOK TINY FROM WHERE THE CAMERA IS (SMALL PROPS, DEBRIS, FAR PLANTS) ARE NOT DRAWN. LOW = UNDER ABOUT 4 "
    "PIXELS ACROSS, HIGH = UNDER ABOUT 10, MORE POP IN. FEWER DRAWS IN OPEN VIEWS. OFF IS THE GAME. APPLIES AT ONCE.";
// 0.1.28 (xmow_d3d_quadros_adiante, xmow_perf_hooks.cpp): how far the game's render thread may run ahead of the GPU ring.
constexpr SettingChoice kRenderAhead[] = {{"1", "1 FRAME (ORIGINAL)"}, {"2", "2 FRAMES (TEST)"}};
// 0.1.30 (xmow_diag_custo_dibujo, gow2_gpu.cpp): the per-draw GPU cost diagnostic.
constexpr SettingChoice kDrawCost[] = {{"0", "NO"}, {"1", "1 PIXEL"}, {"2", "EMPTY"}, {"3", "ONE TRIANGLE"}};
constexpr SettingChoice kMaterialReplay[] = {{"0", "OFF"}, {"1", "VERIFY"}, {"2", "ON"}};  // 0.1.33
constexpr SettingChoice kNativeParams[] = {{"0", "NO"}, {"1", "YES"}, {"2", "SELF-CHECK"}};  // 0.1.33d
constexpr const char* kAyudaViewDistance =
    "OBJECTS THAT HAVE A CULL DISTANCE DISAPPEAR SOONER: 75% OR 50% OF THE GAME'S DISTANCE. FEWER DRAWS IN OPEN VIEWS, "
    "MORE POP IN. OBJECTS WITHOUT ONE ARE NOT TOUCHED. 100% IS THE GAME. APPLIES AT ONCE.";
constexpr const char* kAyudaLighting =
    "STANDARD = THE GAME'S LIGHTS. NONE = NO DYNAMIC LIGHTS IN THE LEVELS (THE EXTRA LIGHT PASSES AND THEIR SHADOWS ARE "
    "NOT DRAWN), FLATTER LOOK, A LITTLE FASTER WHERE MANY LIGHTS SHINE. THE TITLE AND MENUS ALWAYS KEEP THEIR LIGHTS. "
    "APPLIES WHEN THE NEXT LEVEL LOADS.";

struct AdvancedOption {
  const char* label;                                  // menu label, short
  const char* cvar;                                   // the cvar this row edits
  const char* help;                                   // marquee text
  std::span<const SettingChoice> choices = kBoolean;  // default: YES/NO
  bool restart = false;                               // true: (R), stored on SAVE TO CONFIG, applies after a restart
};

// ---- GRAPHICS (SDK, deko3d renderer: all read when the renderer starts) ----
constexpr AdvancedOption kGraphicsOptions[] = {
    {"MIPMAPS", "nfsmw_nativo_mipmaps",
     "USES THE GAME'S SMALLER TEXTURE LEVELS FOR DISTANT SURFACES. KEEP ON. OFF IS ONLY FOR TESTS.", kBoolean, true},
    {"ANISOTROPIC FILTERING", "nfsmw_nativo_anisotropico",
     "SHARPENS TEXTURES SEEN AT SHALLOW ANGLES, LIKE FLOORS AND WALLS. 4X IS A GOOD BALANCE, DEFAULT IS FASTEST.",
     kAnisotropy, true},
    {"GAMMA RAMP", "nfsmw_nativo_rampa_gamma",
     "APPLIES THE GAME'S GAMMA CURVE LIKE THE ORIGINAL CONSOLE. OFF MAKES MIDTONES AND SHADOWS DARKER.", kBoolean, true},
    {"SHADOWS", "xmow_sombras",
     "ALL = THE GAME'S SHADOWS. REDUCED = HALF SHADOW RESOLUTION AND SIMPLER FILTERING. OFF = NO DYNAMIC SHADOWS, "
     "FASTEST. APPLIES AT THE NEXT START. SAME AS SHADOWS IN THE QUICK MENU.",
     kShadows},
    {"VIEW DISTANCE", "xmow_distancia_corte", kAyudaViewDistance, kViewDistance},
    {"SMALL OBJECTS", "xmow_corte_pequenos", kAyudaSmallObjects, kSmallObjects},
    {"MESH DETAIL", "xmow_detalhe_malha", kAyudaMeshDetail, kMeshDetail},
    {"LIGHTING", "xmow_iluminacao", kAyudaLighting, kLighting},
    {"DYNAMIC RESOLUTION", "xmow_resolucao_dinamica",
     "ON THE LEVELS, WHEN THE GPU ALONE HOLDS THE FRAME RATE UNDER 30, THE INTERNAL RESOLUTION GOES ONE STEP BELOW "
     "THE ONE CHOSEN (100 TO 85, 85 TO 75, 75 TO 67) AND COMES BACK WHEN THERE IS ROOM. NOTHING AT 67 OR LOWER. SOFTER "
     "IMAGE AT THE PEAKS. OFF BY DEFAULT."},
    {"GAMEPLAY WITHOUT DISTORTION", "xmow_sem_distorcao",
     "YES = NO HEAT HAZE OR REFRACTION ON THE LEVELS, ONE PASS AND ONE SCENE COPY LESS. THE TITLE AND MENUS KEEP IT. "
     "OFF BY DEFAULT. APPLIES AT ONCE."},
    {"GAMEPLAY WITHOUT BLOOM", "xmow_sem_bloom",
     "YES = NO GLOW AROUND BRIGHT AREAS ON THE LEVELS. THE TITLE AND MENUS KEEP IT. OFF BY DEFAULT. APPLIES AT ONCE."},
    {"GAMEPLAY WITHOUT FOG VOLUMES", "xmow_sem_volumes_nevoa",
     "YES = NO FOG VOLUMES ON THE LEVELS. THE TITLE AND MENUS KEEP THEM. OFF BY DEFAULT. APPLIES AT ONCE."},
    {"ENGINE DETAIL", "xmow_detalle",
     "FEWER OBJECTS IN OPEN VIEWS. REDUCED = LOWER DETAIL MODE, SIMPLER PARTICLES AND CHARACTERS, NO DECALS, SHORTER "
     "FOLIAGE RANGE. LOW = ALSO NO MOTION BLUR, DEPTH OF FIELD AND LENS FLARES. APPLIES AT THE NEXT START.",
     kDetail},
};

// ---- FRAME PACING (this port: gow2_gpu.cpp VblankLoop) ----
constexpr AdvancedOption kPacingOptions[] = {
    {"FRAME RATE", "xmow_vblank_hz",
     "THE GAME LOCKS ITSELF TO 30 FPS BY WAITING FOR TWO SCREEN REFRESHES. UP TO 60 MAKES THOSE REFRESHES TWICE AS FAST. "
     "ONLY HELPS WHERE THE GAME ALREADY HOLDS 30. EXPERIMENTAL: IF ANYTHING RUNS TOO FAST, GO BACK TO 30.",
     kFrameRate},
    {"RENDER AHEAD", "xmow_d3d_quadros_adiante",
     "HOW FAR THE GAME'S RENDER THREAD MAY RUN AHEAD OF THE GPU THREAD. 2 FRAMES LETS IT START THE NEXT FRAME WHILE THE "
     "GPU THREAD FINISHES THE LAST ONE: FASTER IN HEAVY SCENES, ONE FRAME MORE INPUT DELAY. TEST: IF ANYTHING FLICKERS OR "
     "BREAKS, GO BACK TO 1. APPLIES AT ONCE.",
     kRenderAhead},
    {"NATIVE SHADER PARAMETERS", "xmow_native_shader_params",
     "THE GAME'S SHADER CONSTANT SETTERS RUN AS NATIVE CODE INSTEAD OF THE TRANSLATED ONE: THE SAME BYTES TO THE SAME "
     "PLACES, LESS RENDER THREAD WORK PER DRAW. YES BY DEFAULT. NO IS AS 0.1.32. SELF-CHECK RUNS THE TRANSLATED CODE AND "
     "WRITES TO THE LOG WHETHER THE NATIVE ONE WOULD HAVE WRITTEN THE SAME (SLOWER). APPLIES AT ONCE.",
     kNativeParams},
    {"RING WAKE ONCE", "xmow_ring_wake_once",
     "TEST. THE RENDER THREAD NO LONGER WAITS ON THE GPU RING'S LOCK WHILE THE RING THREAD WAKES UP. SAME IMAGE. OFF "
     "(DEFAULT) IS AS 0.1.32. APPLIES AT ONCE."},
    {"MATERIAL PARAMETER REPLAY", "xmow_material_replay",
     "TEST. EACH MATERIAL'S SHADER PARAMETERS ARE WORKED OUT ONCE PER FRAME AND REPLAYED FOR THE OTHER OBJECTS THAT USE "
     "IT. VERIFY CHANGES NOTHING AND WRITES TO THE LOG WHETHER REPLAYING WOULD BE EXACT. ON IS THE FAST MODE, ONLY "
     "AFTER A CLEAN VERIFY. OFF (DEFAULT) IS AS 0.1.32. APPLIES AT THE NEXT FRAME.",
     kMaterialReplay},
    {"NATIVE D3D STATE COPY", "xmow_d3d_nativo",
     "TEST. THE GAME'S DIRECT3D COPIES ITS CHANGED SHADER CONSTANTS AND RENDER STATES INTO ITS COMMAND BUFFER WITH NATIVE "
     "CODE INSTEAD OF THE TRANSLATED ONE: THE SAME WORDS AT THE SAME PLACES, LESS RENDER THREAD WORK PER DRAW. ON BY "
     "DEFAULT SINCE 0.1.32. OFF IS AS 0.1.29. APPLIES AT ONCE."},
};

// ---- DEKO RENDERING (SDK) ----
constexpr AdvancedOption kRenderOptions[] = {
    {"RESOLVE BY SWAP", "nfsmw_nativo_resolver_sin_copia",
     "SWAPS IMAGES INSTEAD OF COPYING PIXELS WHEN RESOLVING A FULL TARGET TO A SAME SIZE TEXTURE. KEEP ON.", kBoolean,
     true},
    {"LAZY FRONT BUFFER", "nfsmw_nativo_frontal_perezoso",
     "THE SWAP DRAWS THE FRONT BUFFER FROM THE RENDER TARGET INSTEAD OF COPYING IT FIRST. SAME IMAGE, A LITTLE FASTER."},
    {"COLOR COMPRESSION", "nfsmw_dk_compresion_color", "REDUCES RGBA8 TARGET BANDWIDTH. KEEP ON.", kBoolean, true},
    {"DEPTH COMPRESSION", "nfsmw_dk_compresion_profundidad", "REDUCES DEPTH TARGET BANDWIDTH. KEEP ON.", kBoolean, true},
    {"CLEAR USED AREA", "nfsmw_dk_borrar_area_util", "CLEARS ONLY THE ROWS IN USE INSTEAD OF THE WHOLE TARGET."},
    {"DEFER COLOR CLEARS", "nfsmw_dk_borrado_color_aplazado",
     "WAITS UNTIL THE COLOR TARGET IS USED BEFORE CLEARING IT. SAVES WORK WHEN IT IS NEVER DRAWN."},
    {"DEFER DEPTH CLEARS", "nfsmw_dk_borrado_z_aplazado",
     "WAITS UNTIL THE DEPTH TARGET IS USED BEFORE CLEARING IT. SAVES WORK WHEN IT IS NEVER DRAWN."},
    {"PARTIAL COMMAND SUBMISSION", "nfsmw_dk_envio_parcial",
     "DRAW CALLS PER PARTIAL SUBMISSION TO THE GPU. SMALLER LETS THE GPU START EARLIER, 256 IS BALANCED.",
     kPartialSubmit},
    {"UPLOAD BUFFER", "nfsmw_dk_subida_mb",
     "UPLOAD MEMORY PER COMMAND SLOT. 32 MB IS ENOUGH. USE 16 MB IF MEMORY IS SHORT.", kUploadMb, true},
    {"TARGETS SIZED TO USE", "xmow_dk_alto_por_retangulo",
     "RENDER TARGETS TAKE THE SIZE OF THE LARGEST SCISSOR, VIEWPORT OR RESOLVE RECTANGLE SEEN INSTEAD OF A FIXED 1280 X "
     "1280. SAVES MEMORY AND ALLOWS RESOLVES BY SWAP. OFF GOES BACK TO THE OLD SIZE.",
     kBoolean, true},
    {"COLOR RESOLVE BY SWAP", "xmow_dk_intercambio_color",
     "A COLOR RESOLVE THAT COVERS THE WHOLE TARGET SWAPS IMAGES INSTEAD OF COPYING PIXELS. OFF COPIES AS BEFORE."},
    {"HDR RESOLVE BY SWAP", "xmow_dk_intercambio_color_7e3",
     "EXPERIMENTAL, NOT EXACT, MAY CHANGE BRIGHT AREAS. ALSO SWAPS FOR THE HDR SCENE TARGET (2-10-10-10 FLOAT). THE COPY "
     "CLIPS NEGATIVES AND VALUES ABOVE 31.875, THE SWAP DOES NOT. OFF BY DEFAULT."},
    {"SKIP REPEATED RESOLVES", "xmow_dk_resolve_unico",
     "A COLOR RESOLVE IDENTICAL TO THE PREVIOUS ONE, WITH NOTHING WRITTEN SINCE, IS SKIPPED. OFF DOES EVERY COPY."},
    {"CLEAR ONLY VIEWPORT", "xmow_dk_area_util_viewport",
     "THE USED AREA OF A TARGET IS THE SCISSOR CLIPPED BY THE VIEWPORT, SO CLEARS COVER FEWER ROWS. SAME IMAGE."},
    {"GROUPED BARRIERS", "xmow_dk_barreiras_agrupadas",
     "CONSECUTIVE 3D COPIES SHARE ONE FRAGMENT BARRIER, PLACED ONLY WHEN NEEDED. OFF USES ONE BEFORE AND ONE AFTER EACH "
     "COPY."},
    {"IDENTITY VERTEX REMAP", "xmow_dk_remapeo_normalizado",
     "A VERTEX INPUT THE GAME DID NOT REARRANGE IS SENT AS UNCHANGED, SO THE VERTEX SHADER SKIPS ITS REARRANGING LOOP. "
     "THE SAME VALUES, LESS VERTEX WORK. OFF IS AS 0.1.30. APPLIES AT ONCE."},
    {"FEWER STATE SUBMISSIONS", "xmow_dk_menos_estado",
     "SHADER CONSTANT UPDATES SEPARATED BY AT MOST 2 UNCHANGED ONES GO TO THE GPU AS ONE, AND THE INDEX BUFFER IS NOT "
     "BOUND AGAIN WHEN THE LAST DRAW USED THE SAME. SAME IMAGE. OFF IS AS 0.1.29. APPLIES AT ONCE."},
    // 0.1.32: SKIP REPEATED DEPTH RESOLVES (xmow_dk_resolve_z_unico) left the menu: tests 37 and 38 counted 0 repeated
    // depth resolves per frame. The cvar stays (default false).
    {"SELECTIVE COPY BARRIER", "xmow_dk_barreira_copia_seletiva",
     "TEST. A 3D COPY AFTER DRAWS WAITS FOR THEM ONLY WHEN THEY WROTE WHAT IT READS OR WRITES, OR READ WHAT IT WRITES. "
     "SHOULD BE THE SAME IMAGE. OFF (DEFAULT) WAITS BEFORE EVERY SUCH COPY. NEEDS GROUPED BARRIERS."},
    {"COLOR SWAP ONLY WITH CLEAR", "xmow_dk_intercambio_color_con_borrado",
     "A COLOR RESOLVE SWAPS IMAGES ONLY WHEN THE SAME COPY ALSO CLEARS THE TARGET. OFF SWAPS WITHOUT A CLEAR, AS IN 0.1.16."},
    {"KEEP LAST FRAME IF NO FRONT", "xmow_dk_swap_sin_sustituto",
     "WHEN THE SCREEN SWAP FINDS NO MATCHING RESOLVED IMAGE, THE LAST FRAME STAYS INSTEAD OF SHOWING THE LATEST ONE. OFF IS THE OLD WAY."},
    {"FULL CLEAR AFTER SWAP", "xmow_dk_borrado_entero_tras_intercambio",
     "THE CLEAR THAT ENDS A SWAP COVERS THE WHOLE TARGET, NOT ONLY THE USED AREA. OFF CLEARS THE USED AREA ONLY."},
    {"BARRIER AFTER SWAP", "xmow_dk_barrera_tras_intercambio",
     "THE BARRIER OWED BY A SWAP IS NEVER DEFERRED: THE NEXT DRAW, CLEAR OR 3D COPY GETS IT. OFF DEFERS IT LIKE A COPY'S."},
    {"CUBE FROM 2D TEXTURE", "xmow_dk_cubo_desde_2d",
     "A 2D TEXTURE READ BY A SHADER THAT EXPECTS A CUBE IS SHOWN ON ALL SIX FACES INSTEAD OF AN EMPTY BLACK CUBE."},
    {"EARLY Z WITH STENCIL", "nfsmw_dk_z_temprana_estencil",
     "EARLY Z ALSO FOR DRAWS THAT ONLY TEST STENCIL AND WRITE NO DEPTH, LIKE THE SHADOW PASSES. NEEDS EARLY Z ON."},
    {"FOLDED VERTEX INPUT", "xmow_dk_vs_entrada_plegada",
     "TEST. DRAWS WHOSE VERTEX INPUTS ARE NOT REARRANGED USE VERTEX SHADERS WITH THAT WORK TAKEN OUT, FROM "
     "SHADERCACHE DKSH VS.BIN BESIDE THE NRO (PROVEN THE SAME OUTPUT ON THE PC). LESS VERTEX WORK. NO (DEFAULT) IS AS "
     "0.1.32; WITHOUT THE FILE NOTHING CHANGES. ANY CHANGE IN THE IMAGE MEANS IT MUST STAY OFF. APPLIES AT ONCE."},
    {"TILED CACHE", "xmow_dk_tiled_cache",
     "TEST. THE GPU'S TILED CACHE ON FOR THE GAME'S FRAMES, FLUSHED BEFORE EVERY TARGET CHANGE, COPY AND BARRIER. OFF "
     "(DEFAULT) IS AS ALWAYS. ANY GARBAGE OR OLD IMAGE ON SCREEN MEANS IT MUST STAY OFF."},
    {"FIRST SUBMIT AFTER", "nfsmw_dk_envio_parcial_primero",
     "DRAWS BEFORE THE FIRST PARTIAL SUBMISSION TO THE GPU AFTER A SWAP, SO THE GPU STARTS SOONER. OFF = AS BEFORE. "
     "NEEDS PARTIAL COMMAND SUBMISSION ON.",
     kFirstSubmit},
    {"TEXTURE CHECK PERIOD", "xmow_dk_identidad_periodo",
     "TEXTURES USED EVERY FRAME ARE CHECKED FOR A SWAPPED IMAGE ONLY EVERY N FRAMES. TEXTURES THAT COME BACK AFTER A "
     "PAUSE ARE ALWAYS CHECKED AT ONCE. EVERY FRAME = AS 0.1.19, SLOWER.",
     kProbePeriod},
    {"GEOMETRY FRAME TRUST", "xmow_dk_geometria_por_fotograma",
     "VERTEX DATA UNCHANGED FOR THIS MANY FRAMES IS CHECKED ONCE PER FRAME INSTEAD OF AFTER EVERY GPU FENCE. OFF = AS "
     "0.1.19. TURN OFF IF MODELS SHOW STRETCHED TRIANGLES.",
     kGeometryTrust},
    {"FAST GEOMETRY CACHE", "xmow_dk_geometria_rapida",
     "BIGGER LOOKUP CACHE AND CHEAPER EVICTION FOR THE GEOMETRY CACHE. SAME IMAGE. OFF = AS 0.1.19.", kBoolean, true},
    {"PER DRAW SAVINGS", "xmow_dk_ahorro_por_dibujo",
     "SKIPS REPEATED SHADER LOOKUPS AND VERTEX BUFFER BINDS BETWEEN DRAWS. SAME IMAGE. OFF = AS 0.1.19."},
    {"CONSTANTS BY WRITTEN RANGE", "xmow_dk_constantes_sucias",
     "SHADER CONSTANTS ARE COMPARED ONLY WHERE THE GAME WROTE THEM SINCE THE LAST DRAW, NOT OVER THE WHOLE BLOCK. SAME "
     "IMAGE, LESS GPU THREAD WORK. OFF = AS 0.1.27."},
    {"CHEAP CLEAR AND RESOLVE LOGS", "xmow_registro_barato",
     "CLEARS AND RESOLVES NO LONGER BUILD LOG TEXT EVERY TIME, ONLY WHEN A NEW KIND IS LOGGED OR THE GPU HANGS. SAME "
     "LOG, LESS GPU THREAD WORK. OFF = AS 0.1.27."},
    {"DRAW COST (DIAGNOSTIC)", "xmow_diag_custo_dibujo",
     "DIAGNOSTIC ONLY, THE PICTURE BREAKS WHILE IT IS ON. 1 PIXEL: EVERY GAME DRAW COVERS ONE PIXEL, THE FPS LEFT SHOWS "
     "THE COST EACH DRAW HAS WHATEVER ITS SIZE. EMPTY: EVERY DRAW KEEPS ITS STATE BUT DRAWS NOTHING, THE FPS LEFT SHOWS THE "
     "COST OF THE STATE CHANGES ALONE. PAUSE THE GAME, 20 SECONDS EACH, BACK TO NO. APPLIES AT ONCE.",
     kDrawCost},
    {"HDR OUTPUT CLAMP", "xmow_dk_clamp_7e3",
     "CLAMPS SHADER OUTPUT INTO THE HDR SCENE TO WHAT THE XBOX 360 CAN STORE (COLOR UP TO 31.875, ALPHA 0 TO 1). FOR THE "
     "TOO WHITE MAIN MENU. THE FIRST TIME EACH SHADER IS COMPILED IN THE BACKGROUND."},
};

// ---- GPU AND CACHE (SDK) ----
constexpr AdvancedOption kGpuOptions[] = {
    {"OFFICIAL GPU BOOST", "xmow_gpu_boost",
     "HANDHELD ONLY. ASKS THE SYSTEM FOR ITS OFFICIAL 460 MHZ GPU MODE: MORE SPEED, MORE BATTERY AND HEAT. DOES NOTHING "
     "DOCKED. OFF BY DEFAULT."},
    {"BOOST WITH RAM 1600", "xmow_gpu_boost_ram1600",
     "OFFICIAL HANDHELD MODE GPU 460 + RAM 1600 (USED BY COMMERCIAL GAMES). CLEAN AUDIO AT STOCK CPU. APPLIES LIVE."},
    {"EARLY Z", "nfsmw_nativo_z_temprana", "TESTS DEPTH BEFORE SHADING SO HIDDEN PIXELS COST LESS."},
    {"SKIP INVISIBLE DRAWS", "nfsmw_nativo_saltar_invisibles", "SKIPS DRAWS THAT HAVE NO VISIBLE EFFECT."},
    {"SKIP REPEATED CLEARS", "nfsmw_nativo_saltar_borrados_repetidos", "AVOIDS CLEARING THE SAME AREA TWICE."},
    {"TEXTURE CACHE", "nfsmw_nativo_texturas_mb_max",
     "TEXTURE MEMORY KEPT BEFORE OLD ONES ARE DROPPED. 512 MB IS BALANCED, LOWER IF MEMORY RUNS OUT.", kTextureMb},
    {"GEOMETRY CACHE", "nfsmw_dk_geometria_mb", "REUSES VERTICES AND INDICES BETWEEN FRAMES. OFF SAVES MEMORY.",
     kGeometryMb, true},
    {"YIELD WHILE WAITING FOR GPU", "xmow_spin_cede",
     "WHEN THE GAME SPINS WAITING FOR THE GPU RING, THE THREAD WAITS FOR PROGRESS INSTEAD OF BURNING THE CORE. OFF SPINS "
     "LIKE THE ORIGINAL."},
    {"INSTANT OCCLUSION ANSWER", "xmow_consulta_inmediata",
     "WITH REAL OCCLUSION OFF, OCCLUSION QUERIES ANSWER VISIBLE AT ONCE INSTEAD OF POLLING UNTIL THE RING GETS THERE."},
    {"SKIP QUERY BOXES", "xmow_pular_caixas_consulta",
     "WITH REAL OCCLUSION OFF, THE BOXES THE GAME DRAWS FOR OCCLUSION QUERIES (NO COLOR, DEPTH OR STENCIL WRITE) ARE NOT "
     "DRAWN. THEY PAINT NOTHING AND ARE A THIRD OF THE DRAWS IN OPEN VIEWS. OFF DRAWS THEM."},
    {"NO OCCLUSION QUERIES", "xmow_sem_consultas_oclusao",
     "WITH REAL AND CONSERVATIVE OCCLUSION OFF, THE ENGINE ISSUES NO OCCLUSION QUERIES AT ALL (ITS OWN TOGGLEOCCLUSION "
     "SWITCH, ONLY WHILE IT PREPARES THE VIEWS): NO BOXES, NO WAITS, EVERYTHING DRAWN AS TODAY. LESS RENDER THREAD WORK. "
     "OFF ISSUES THEM AS BEFORE. APPLIES NEXT FRAME."},
    {"THREAD CORES", "xmow_afinidade_threads",
     "EXPERIMENTAL. THE RENDER THREAD ON CORE 1 AND THE GPU RING THREAD ON CORE 2, SAME PRIORITY. THE GAME THREAD STAYS "
     "FREE. OFF (DEFAULT) LETS THE SYSTEM MOVE THEM. APPLIES AT ONCE."},
    {"FAST READ POINTER", "xmow_anillo_rptr_rapido",
     "THE RING GIVES THE READ POINTER BACK TO THE GAME EARLIER (BEFORE WAITING AND AT THE END OF EACH BATCH) AND WAKES THREADS THAT WAIT IN THE GPU SPIN. OFF IS THE OLD TIMING."},
    {"SHADER LOAD CACHE", "xmow_imload_rapido",
     "A SHADER LOAD (IM LOAD IMMEDIATE) ALREADY IDENTIFIED REUSES ITS ANSWER WITHOUT THE BYTE SWAP, THE LOCK AND THE IDENTIFICATION. OFF IDENTIFIES EVERY TIME."},
    {"SAMPLE GEOMETRY", "nfsmw_dk_geometria_muestreo",
     "CHECKS ONLY PARTS OF CACHED GEOMETRY TO DETECT CHANGES. FASTER.", kBoolean, true},
    {"HAZARD CHECK ALL TEXTURES", "xmow_dk_riesgo_todas_texturas",
     "CHECKS A DRAW AGAINST EVERY TEXTURE STILL BOUND, NOT ONLY ITS OWN, FOR WORK A PENDING 3D COPY WROTE. OFF CHECKS THIS "
     "DRAW'S TEXTURES ONLY."},
    {"RESOLVE SKIP FORGETS MOVED", "xmow_dk_resolve_unico_olvidar",
     "WHEN A RESOLVED TEXTURE IS SET ASIDE, THE RESOLVE REMEMBERED FOR IT IS FORGOTTEN SO THE NEXT ONE IS NEVER SKIPPED."},
    {"FULL CHECK SMALL BUFFERS", "xmow_dk_conferir_pequenos_inteiro",
     "SMALL VERTEX AND INDEX RANGES AND SMALL TEXTURES (GLYPHS, UI) ARE ALWAYS CHECKED BYTE BY BYTE. OFF USES THE SAMPLED "
     "CHECK."},
    {"CHEAP SMALL BUFFER CHECK", "xmow_dk_conferir_pequenos_barato",
     "OF THE SMALL BUFFERS CHECKED IN FULL, ONLY THOSE UP TO 2 KB ARE READ WHOLE. THE REST CHECK A ROTATING EIGHTH. OFF CHECKS ALL WHOLE."},
    {"TEXTURE IDENTITY CHECK", "xmow_dk_identidad_texturas",
     "CHECKS EACH FRAME THAT A CACHED TEXTURE STILL HOLDS THE SAME GAME DATA; FIXES DOORS AND LEAVES DRAWN WITH ANOTHER TEXTURE FOR A MOMENT."},
    {"SD READ CACHE", "xmow_io_cache_mb",
     "KEEPS RECENTLY READ BLOCKS OF THE GAME FILES IN MEMORY TO CUT SLOW SD READS. OFF USES NO MEMORY.", kIoCache, true},
    {"SD READ-AHEAD", "xmow_io_leitura_antecipada",
     "READS THE NEXT BLOCK IN THE BACKGROUND WHEN THE GAME READS A FILE IN ORDER. NEEDS SD READ CACHE ON."},
    {"BACKGROUND SHADER COMPILE", "nfsmw_dk_compilar_en_fondo",
     "COMPILES NEW SHADER VARIANTS IN THE BACKGROUND TO AVOID HITCHES. OFF COMPILES ON THE SPOT.", kBoolean, true},
};

// ---- AUDIO (SDK) ----
constexpr AdvancedOption kAudioOptions[] = {
    {"MUTE AUDIO", "audio_mute", "TURNS ALL SOUND OFF."},
    {"AUDIO PUMP", "audio_switch_bomba", "ASKS THE GAME FOR AUDIO AT REGULAR INTERVALS. KEEP ON TO AVOID ROBOTIC SOUND.",
     kBoolean, true},
    {"AUDIO QUEUE", "audio_switch_tramas_en_cola",
     "PCM BLOCKS QUEUED BY THE PUMP. MORE AVOIDS CUTS BUT ADDS DELAY. 10 IS BALANCED.", kAudioQueue, true},
    {"AUDIO FRAME LIMIT", "audio_maxqframes", "MAX BUFFERED AUDIO FRAMES. LOWER REDUCES DELAY BUT MAY CRACKLE.",
     kAudioFrames, true},
    {"XMA DECODE THREAD", "audio_xma_en_trabajador",
     "WHO DECODES XMA MUSIC AND VOICES. CHANGE IT ONLY IF AUDIO STUTTERS.", kXmaThread, true},
};

// ---- CONTROLS (SDK) ----
constexpr AdvancedOption kControlOptions[] = {
    {"BUTTONS BY POSITION", "input_xbox_layout",
     "MATCHES THE PHYSICAL XBOX BUTTON POSITIONS INSTEAD OF THE LETTERS. TURN ON IF BUTTONS FEEL SWAPPED."},
    {"RUMBLE", "input_vibracion", "ENABLES THE GAME'S RUMBLE. OFF SENDS NO VIBRATION."},
    {"RUMBLE STRENGTH", "input_vibracion_fuerza", "SCALES THE GAME'S RUMBLE. THE SWITCH MOTORS FEEL STRONGER.", kRumble},
};

// ---- PORT FIXES (this port: xmow_game_patches.cpp and gow2_gpu.cpp) ----
constexpr AdvancedOption kFixOptions[] = {
    {"SINGLE PASS SCENE", "xmow_patch_sem_tiling",
     "GAME PATCH: THE SCENE IS DRAWN AT ONCE INSTEAD OF IN TWO PREDICATED TILES. KEEP ON.", kBoolean, true},
    {"SKIP DEPTH PREPASS", "gow2_sin_prepaso_z",
     "THE BASE PASS WRITES ITS OWN DEPTH. WITH THE GAME'S DEPTH PREPASS THE WORLD CAME OUT BLACK. KEEP ON."},
    {"SKIP MASKED PREPASS", "gow2_sin_prepaso_enmascarado",
     "LEAVES OUT THE ALPHA TESTED DEPTH PREPASS. DRAWN, IT MADE THE GROUND BLACK. KEEP ON."},
    {"SKIP PREPASS IN ENGINE", "xmow_sem_prepasso_motor",
     "THE GAME NO LONGER BUILDS THE DEPTH PREPASS DRAWS THAT THE TWO OPTIONS ABOVE THROW AWAY: LESS RENDER THREAD "
     "WORK, SAME IMAGE (TEST 39: ABOUT 1 FPS MORE). ONLY WITH BOTH OF THEM ON AND REAL OCCLUSION OFF. ON BY DEFAULT "
     "SINCE 0.1.33. APPLIES AT ONCE."},
    {"REAL OCCLUSION", "gow2_oclusion_real",
     "THE GPU COUNTS WHAT EACH OCCLUSION QUERY SEES SO THE GAME SKIPS HIDDEN OBJECTS. OFF DRAWS EVERYTHING. ON HID THE "
     "HERO AND THE SET IN EARLIER TESTS."},
    {"HEIGHT FOG", "xmow_niebla_altura",
     "AUTO = THE GAME'S HEIGHT FOG, DRAWN EVERYWHERE INCLUDING THE MAIN MENU (ITS BLUE WATER LOOK). YES = ALWAYS "
     "DRAWN. NO IN GAMEPLAY = OFF ON THE LEVELS ONLY, THE TITLE AND MENUS KEEP IT. APPLIES AT ONCE. SAME AS HEIGHT FOG "
     "IN THE QUICK MENU.",
     kHeightFog},
    {"SKIPPED SHADERS", "xmow_omitir_ps",
     "PIXEL SHADERS NEVER DRAWN, FOR TESTS. FOG LEAVES OUT THE FULL SCREEN HEIGHT FOG EVERYWHERE, OVER THE HEIGHT FOG "
     "OPTION. NONE IS THE DEFAULT.",
     kSkippedShaders},
};

// ---- LOGS AND DIAGNOSTICS (SDK and this port; the automatic ones act only with telemetria.flag beside the NRO) ----
constexpr AdvancedOption kLogOptions[] = {
    {"PLAY MODE", "xmow_modo_jogo",
     "YES (DEFAULT) = NORMAL PLAY: THE LOG KEEPS WARNINGS AND ERRORS ONLY, THE 10 SECOND REPORTS, THE PROFILE LOG AND THE "
     "SLOW FRAME LIST ARE OFF. CRASHES ARE STILL WRITTEN. NO = FULL DIAGNOSTICS FOR TESTS. THE TESTS PAGE TURNS "
     "DIAGNOSTICS ON BY ITSELF."},
    {"LOG LEVEL", "log_level",
     "HOW MUCH THE LOG FILE RECORDS. INFO IS BEST FOR BUG REPORTS. IN PLAY MODE THE LOG KEEPS WARNINGS AND ERRORS ONLY.",
     kLogLevel},
    {"DIAGNOSTICS ONLY WHEN ACTIVE", "xmow_diag_solo_activo",
     "PER DRAW DIAGNOSTICS AND REPORT COUNTERS RUN ONLY IN FRAMES WHERE A TRACE, PROBE, A/B CAPTURE OR SKIP IS ACTIVE. OFF "
     "RUNS THEM EVERY FRAME. THE SEARCH MODES NEED ONE OF THOSE ON, OR THIS OFF."},
    {"SLOW FRAME LOG", "xmow_log_hitch_ms",
     "FRAMES LONGER THAN THIS ARE LISTED IN THE LOG (FRAME TIME, DRAWS, QUERY BOXES, TEXTURES, WAITS), AT MOST 20 EVERY 10 "
     "SECONDS. WRITTEN WITH THE 10 SECOND SUMMARY, NOT DURING THE FRAME.",
     kHitch},
    {"STACK SAMPLING", "xmow_amostragem_pilas",
     "SAMPLES WHERE EVERY THREAD SPENDS ITS TIME, EVERY MILLISECOND, INTO THE PROFILE LOG AND REX PILHAS.BIN. THE GAME "
     "RUNS A LITTLE SLOWER WHILE IT IS ON. OFF BY DEFAULT, ON ONLY FOR A PROFILED TEST."},
    {"TREES WITHOUT LEAVES", "xmow_diag_sem_speedtree",
     "DIAGNOSTIC. YES = SPEEDTREE LEAVES AND FRONDS OFF ON THE LEVELS, TO SEE IF THE JUNGLE DRAWS ARE SPEEDTREE. "
     "APPLIES AT THE NEXT LEVEL LOAD. OFF BY DEFAULT."},
    {"L3 + R3 CAPTURE", "switch_atajo_volcado",
     "PRESS BOTH STICKS TO SAVE A SCREEN CAPTURE IN THE CAPTURAS FOLDER. WITH L3 + R3 FULL DUMP ON IT ALSO DUMPS THE "
     "NEXT FRAME FOR DIAGNOSIS."},
    {"L3 + R3 FULL DUMP", "xmow_l3r3_volcado_completo",
     "DIAGNOSTIC. L3 + R3 ALSO DUMPS EVERY TEXTURE OF THE NEXT FRAME AND TRACES ITS DRAWS IN THE LOG. TENS OF MB TO THE "
     "SD CARD, THE GAME STALLS WHILE IT IS WRITTEN. OFF (DEFAULT) = SCREEN CAPTURE ONLY."},
    {"A/B FLASHES", "xmow_captura_ab",
     "AFTER EACH AUTOMATIC CAPTURE, 4 FRAMES LEAVE OUT ONE GROUP OF DRAWS FOR A SECOND CAPTURE, SO THE IMAGE FLASHES. "
     "ONLY WITH THE TELEMETRY FLAG FILE."},
    {"AUTO CAPTURE", "gow2_auto_captura_s",
     "A SMALL SCREEN CAPTURE EVERY N SECONDS IN THE CAPTURES FOLDER. OFF FOR NORMAL PLAY. ONLY WITH THE TELEMETRY FLAG "
     "FILE.",
     kAutoCapture},
    {"FRAME TRACES", "gow2_trazas_max",
     "HOW MANY DRAW TRACES OF A HEAVY FRAME GO TO THE LOG. EACH ONE STALLS THE GAME FOR SECONDS. ONLY WITH THE "
     "TELEMETRY FLAG FILE.",
     kTraces},
    {"AUTO DUMPS", "gow2_auto_volcados_max",
     "HOW MANY AUTOMATIC DUMPS OF A FRAME'S TEXTURES ARE WRITTEN. EACH ONE STALLS THE GAME FOR SECONDS. ONLY WITH THE "
     "TELEMETRY FLAG FILE.",
     kDumps},
};

struct AdvancedCategory {
  const char* name;
  const char* help;
  std::span<const AdvancedOption> options;
};

constexpr AdvancedCategory kCategories[] = {
    {"GRAPHICS", "IMAGE QUALITY: MIPMAPS, TEXTURE FILTERING, COLOR AND SHADOWS.", kGraphicsOptions},
    {"FRAME PACING", "HOW FRAMES REACH THE SCREEN: THE GAME'S FRAME RATE AND HOW FAR THE RENDER THREAD RUNS AHEAD.", kPacingOptions},
    {"DEKO RENDERING", "DEKO3D RENDERER INTERNALS: RESOLVES, COMPRESSION, CLEARS, COMMAND SUBMISSION AND UPLOADS.",
     kRenderOptions},
    {"GPU AND CACHE", "GPU BOOST, DEPTH CULLING AND TEXTURE, GEOMETRY AND SHADER CACHES.", kGpuOptions},
    {"AUDIO", "SOUND OUTPUT, BUFFERING AND DECODING.", kAudioOptions},
    {"CONTROLS", "BUTTON LAYOUT AND RUMBLE.", kControlOptions},
    {"PORT FIXES",
     "WOLVERINE FIXES OF THIS PORT: TILING PATCH, DEPTH PREPASSES, OCCLUSION AND SKIPPED SHADERS. CHANGE THEM ONLY TO "
     "TEST.",
     kFixOptions},
    {"LOGS AND DIAGNOSTICS", "LOG FILE, L3 + R3 CAPTURE AND THE AUTOMATIC CAPTURES OF THE TEST BUILDS.", kLogOptions},
};

// The quick menu's cvars (gow2_menu.cpp, MenuRapido). With the tables above they form the set of keys SAVE writes and
// RESTORE DEFAULTS resets.
constexpr const char* kQuickCvars[] = {
    "xmow_escala_interna", "xmow_sombras", "xmow_resolucao_saida", "xmow_gpu_boost", "xmow_distancia_corte",
    "xmow_corte_pequenos", "xmow_iluminacao", "xmow_niebla_altura",  "switch_contador_fps", "audio_mute",
    "language",  // 0.1.34f
};

}  // namespace gow2::menu::tablas
