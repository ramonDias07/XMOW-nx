// XMOW - the data of the ADVANCED SETTINGS menu (L + R + Right), used by gow2_menu.cpp. Pattern of the shared menu template
// (as in Budokai3-nx): options are DATA, not code. To
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
constexpr SettingChoice kPartialSubmit[] = {{"0", "OFF"}, {"128", "128"}, {"256", "256"}, {"512", "512"}};
constexpr SettingChoice kFirstSubmit[] = {{"0", "OFF"}, {"32", "32"}, {"48", "48"}, {"64", "64"}, {"128", "128"}};
constexpr SettingChoice kUploadMb[] = {{"16", "16 MB"}, {"32", "32 MB"}, {"64", "64 MB"}};
constexpr SettingChoice kTextureMb[] = {{"384", "384 MB"}, {"512", "512 MB"}, {"640", "640 MB"}};
constexpr SettingChoice kGeometryMb[] = {{"0", "OFF"}, {"32", "32 MB"}, {"64", "64 MB"}, {"96", "96 MB"}};
constexpr SettingChoice kAudioQueue[] = {{"6", "6"}, {"8", "8"}, {"10", "10"}, {"12", "12"}, {"14", "14"}, {"16", "16"}};
constexpr SettingChoice kRumble[] = {{"0.25", "25%"}, {"0.5", "50%"}, {"0.75", "75%"}, {"1", "100%"}};
// xmow_vblank_hz: 60 = the video mode's refresh (the game locks to 30 fps), 120 = the game can reach 60 fps.
constexpr SettingChoice kFrameRate[] = {{"60", "30 (ORIGINAL)"}, {"120", "UP TO 60 (EXPERIMENTAL)"}};
constexpr SettingChoice kAudioFrames[] = {{"4", "4"}, {"8", "8"}, {"16", "16"}, {"32", "32"}};
constexpr SettingChoice kXmaThread[] = {{"0", "REQUESTING THREAD"}, {"1", "XMA THREAD"}, {"2", "WORKER"}};
// xmow_omitir_ps as fixed choices: nothing skipped, or the full screen height fog (pixel shader 2639 of the dslib).
constexpr SettingChoice kSkippedShaders[] = {{"", "NONE"}, {"2639", "FOG"}};
// log_level accepts trace, debug, info, warn, error, critical, off (sdk/src/core/logging.cpp); the menu offers three.
constexpr SettingChoice kLogLevel[] = {{"info", "INFO"}, {"warn", "WARNINGS"}, {"error", "ERRORS"}};
constexpr SettingChoice kAutoCapture[] = {{"0", "OFF"}, {"5", "5 S"}, {"10", "10 S"}, {"30", "30 S"}};
constexpr SettingChoice kTraces[] = {{"0", "0"}, {"2", "2"}, {"5", "5"}, {"20", "20"}};
constexpr SettingChoice kDumps[] = {{"0", "0"}, {"2", "2"}, {"4", "4"}};

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
};

// ---- FRAME PACING (this port: gow2_gpu.cpp VblankLoop) ----
constexpr AdvancedOption kPacingOptions[] = {
    {"FRAME RATE", "xmow_vblank_hz",
     "THE GAME LOCKS ITSELF TO 30 FPS BY WAITING FOR TWO SCREEN REFRESHES. UP TO 60 MAKES THOSE REFRESHES TWICE AS FAST. "
     "ONLY HELPS WHERE THE GAME ALREADY HOLDS 30. EXPERIMENTAL: IF ANYTHING RUNS TOO FAST, GO BACK TO 30.",
     kFrameRate},
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
    {"EARLY Z WITH STENCIL", "nfsmw_dk_z_temprana_estencil",
     "EARLY Z ALSO FOR DRAWS THAT ONLY TEST STENCIL AND WRITE NO DEPTH, LIKE THE SHADOW PASSES. NEEDS EARLY Z ON."},
    {"FIRST SUBMIT AFTER", "nfsmw_dk_envio_parcial_primero",
     "DRAWS BEFORE THE FIRST PARTIAL SUBMISSION TO THE GPU AFTER A SWAP, SO THE GPU STARTS SOONER. OFF = AS BEFORE. "
     "NEEDS PARTIAL COMMAND SUBMISSION ON.",
     kFirstSubmit},
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
    {"FAST READ POINTER", "xmow_anillo_rptr_rapido",
     "THE RING GIVES THE READ POINTER BACK TO THE GAME EARLIER (BEFORE WAITING AND AT THE END OF EACH BATCH) AND WAKES THREADS THAT WAIT IN THE GPU SPIN. OFF IS THE OLD TIMING."},
    {"SHADER LOAD CACHE", "xmow_imload_rapido",
     "A SHADER LOAD (IM LOAD IMMEDIATE) ALREADY IDENTIFIED REUSES ITS ANSWER WITHOUT THE BYTE SWAP, THE LOCK AND THE IDENTIFICATION. OFF IDENTIFIES EVERY TIME."},
    {"SAMPLE GEOMETRY", "nfsmw_dk_geometria_muestreo",
     "CHECKS ONLY PARTS OF CACHED GEOMETRY TO DETECT CHANGES. FASTER.", kBoolean, true},
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
    {"REAL OCCLUSION", "gow2_oclusion_real",
     "THE GPU COUNTS WHAT EACH OCCLUSION QUERY SEES SO THE GAME SKIPS HIDDEN OBJECTS. OFF DRAWS EVERYTHING. ON HID THE "
     "HERO AND THE SET IN EARLIER TESTS."},
    {"SKIPPED SHADERS", "xmow_omitir_ps",
     "PIXEL SHADERS NEVER DRAWN. FOG LEAVES OUT THE FULL SCREEN HEIGHT FOG THAT WASHED THE IMAGE BLUE WHITE. SAME AS "
     "HEIGHT FOG IN THE QUICK MENU.",
     kSkippedShaders},
};

// ---- LOGS AND DIAGNOSTICS (SDK and this port; the automatic ones act only with telemetria.flag beside the NRO) ----
constexpr AdvancedOption kLogOptions[] = {
    {"LOG LEVEL", "log_level", "HOW MUCH THE LOG FILE RECORDS. INFO IS BEST FOR BUG REPORTS.", kLogLevel},
    {"DIAGNOSTICS ONLY WHEN ACTIVE", "xmow_diag_solo_activo",
     "PER DRAW DIAGNOSTICS AND REPORT COUNTERS RUN ONLY IN FRAMES WHERE A TRACE, PROBE, A/B CAPTURE OR SKIP IS ACTIVE. OFF "
     "RUNS THEM EVERY FRAME. THE SEARCH MODES NEED ONE OF THOSE ON, OR THIS OFF."},
    {"L3 + R3 CAPTURE", "switch_atajo_volcado",
     "PRESS BOTH STICKS TO DUMP THE NEXT FRAME AND TRACE ITS DRAWS FOR DIAGNOSIS. THE GAME STALLS WHILE IT WRITES TO "
     "THE SD CARD."},
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
    {"GRAPHICS", "IMAGE QUALITY: MIPMAPS, TEXTURE FILTERING AND COLOR.", kGraphicsOptions},
    {"FRAME PACING", "HOW FRAMES REACH THE SCREEN: THE GAME'S FRAME RATE.", kPacingOptions},
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
    "xmow_resolucao_saida", "xmow_gpu_boost", "xmow_omitir_ps", "switch_contador_fps", "audio_mute",
};

}  // namespace gow2::menu::tablas
