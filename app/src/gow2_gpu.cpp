// XMOW (from gears2-nx) - the game's graphics system on the Switch.
//
// Copied from halo3/src/halo3_gpu.cpp: nothing in it is about Halo 3. Once a second game uses it, it belongs in sdk/.
//
// What it does
//   Stands in for the Xbox 360 GPU, so the game runs past its first frame. The game's own Direct3D keeps
//   running unchanged and writes its list of GPU commands (the PM4 ring). A thread reads that list and does
//   what the game waits for:
//     1. MMIO registers at 0x7FC80000, with the fixed values the SDK's emulation returns;
//     2. the ring read pointer, written back where the game watches it (it spins on it to know how much
//        room is left);
//     3. interrupts: vblank, and PM4_INTERRUPT packets (the GPU reached that point of the ring);
//     4. GPU writes to memory (MEM_WRITE, COND_WRITE, REG_TO_MEM, EVENT_WRITE_*, scratch registers), as in
//        the SDK's graphics/command_processor.cpp. Occlusion queries report 1000 samples, like the SDK.
//   5. (deko3d build) the minimal renderer: IM_LOAD / IM_LOAD_IMMEDIATE tell which translated shader is loaded
//      (gow2_shaders.h), and each draw goes to the SDK's deko3d backend (rex::x360::RenderDk) as a PeticionDibujo
//      with the register mirror; resolves and clears go to its Copiar, and each Swap presents the frontbuffer
//      that fetch constant 0 describes. A draw whose VS or PS is not identified is skipped and counted.
//
// Presentation (the deko3d build): the SDK's deko3d provider and presenter (rex/ui/deko3d/presentador.h). ReXApp
// sees them and sets up the window and the ImGui overlay as usual; until the renderer refreshes the guest output,
// the presenter paints black under the overlay.
//
// Without it the runtime drops the game's Vd* calls ("no GPU emulation loaded") and the render thread waits
// forever for a GPU that never reads its commands.

#include "gow2_gpu.h"
#include "xmow_gpu_boost.h"
#include "xmow_perf.h"
#include "xmow_cpu33.h"  // 0.1.33 (CPU line)

void gow2_log_main_thread();  // src/gow2_guards.cpp

#if defined(__SWITCH__)
#include <switch.h>
#endif

#include <algorithm>
#include <bit>
#include <cctype>
#include <cmath>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <deque>
#include <cstring>
#include <iterator>
#include <map>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <rex/graphics/registers.h>
#include <rex/graphics/xenos.h>
#include <rex/kernel/xboxkrnl/video.h>
#include <rex/logging.h>
#include <rex/memory/utils.h>
#include <rex/system/function_dispatcher.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xmemory.h>
#include <rex/system/xobject.h>
#include <rex/system/xthread.h>
#include <rex/system/xtypes.h>
#include <rex/system/xvideo.h>
#include <rex/thread.h>
#include <rex/types.h>
#include <rex/ui/graphics_provider.h>
#include <rex/ui/presenter.h>
#include <rex/ui/windowed_app_context.h>

#if GOW2_DEKO3D
#include <unordered_map>
#include <unordered_set>
#include <cstdio>
#include <string>
#include <string_view>
#include <filesystem>

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/ui/deko3d/presentador.h>
#include <rex/x360/decodificador.h>
#include <rex/x360/deko/destinos.h>
#include <rex/x360/destinos.h>
#include <rex/x360/peticion.h>

#include "gow2_shaders.h"
#include "xmow_bench.h"

// xxHash reads with memcpy under GCC's strict aliasing (see shaders/ds_shader_library.cpp).
#undef XXH_FORCE_MEMORY_ACCESS
#define XXH_FORCE_MEMORY_ACCESS 0
#define XXH_INLINE_ALL
#include <xxhash.h>

REXCVAR_DECLARE(bool, switch_captura_pedida);  // ZL + ZR + Down (sdk/src/input/switch/switch_input_driver.cpp)
REXCVAR_DECLARE(bool, switch_volcado_pedido);  // ZL + ZR + Left: every resolved texture of the next frame
REXCVAR_DECLARE(bool, nfsmw_dk_todas_las_salidas);  // skip mode 9 (SDK patch 0033)
REXCVAR_DECLARE(bool, nfsmw_dk_gamma_lineal);       // skip mode 10 (SDK patch 0033)
REXCVAR_DECLARE(std::string, rex_archivos_ocultos);  // SDK patch 0048: files the game does not find
REXCVAR_DECLARE(bool, switch_omitir_pedido);   // ZL + ZR + Right: next draw-skipping mode (SkipMode)
// The SDK renderer's shadow-pass shortcuts (sdk/src/x360/deko/destinos.cpp; the SDK keeps their nfsmw_ names): both
// drop colorless draws that alpha-test or discard, which here are ordinary depth-only draws. Off for Dead Space 2.
REXCVAR_DECLARE(bool, nfsmw_sombras_sin_vegetacion);
REXCVAR_DECLARE(bool, nfsmw_nativo_ps_solo_alfa);
// The renderer's shadow-map depth bias, given when the game sets none: Dead Space 2's shadow maps are 960 wide.
REXCVAR_DECLARE(int32_t, nfsmw_dk_sombras_sesgo_pitch);
// The renderer's sampled change checks of geometry and textures stay at the SDK's defaults (sampled). Full fingerprints
// were tried against the streaks (which were the vertex index and the computed-index fetches, not stale vertices):
// they hashed 14-20 MB of vertices a frame and the ring thread went from 9 % to 40 % of a core.
REXCVAR_DECLARE(bool, nfsmw_dk_geometria_muestreo);
REXCVAR_DECLARE(int32_t, nfsmw_dk_perfil_gpu_s);  // the renderer's periodic GPU profile (telemetry only)
// Safe mode (ZL + ZR + Right, mode 1): every shortcut the renderer took for NFS Most Wanted off at once, to tell whether
// one of them breaks Dead Space 2 (each changes what is drawn when its guess about the game is wrong). The SDK reads
// them on every use, so they switch while the game runs.
REXCVAR_DECLARE(bool, nfsmw_nativo_saltar_invisibles);
REXCVAR_DECLARE(bool, nfsmw_nativo_sin_ps_sin_color);
REXCVAR_DECLARE(bool, nfsmw_nativo_sin_desenfoque);
REXCVAR_DECLARE(bool, nfsmw_nativo_pcf_barato);
REXCVAR_DECLARE(bool, nfsmw_nativo_sombra_minimo);
REXCVAR_DECLARE(bool, nfsmw_nativo_z_temprana);
REXCVAR_DECLARE(bool, nfsmw_nativo_cielo_aplazado);
REXCVAR_DECLARE(bool, nfsmw_dk_constantes_por_diferencias);
REXCVAR_DECLARE(bool, nfsmw_nativo_saltar_borrados_repetidos);
REXCVAR_DECLARE(bool, nfsmw_nativo_resolver_sin_copia);
REXCVAR_DECLARE(bool, nfsmw_nativo_intercambiar_sin_borrado);
REXCVAR_DECLARE(bool, nfsmw_nativo_frontal_perezoso);
REXCVAR_DECLARE(bool, nfsmw_dk_borrar_area_util);
REXCVAR_DECLARE(bool, nfsmw_dk_borrado_color_aplazado);
REXCVAR_DECLARE(bool, nfsmw_dk_borrado_z_aplazado);

// Automatic data collection while the renderer is being brought up: nothing to press on the console.
// Set by the GetData hook (xmow_perf_hooks.cpp, real occlusion only) while Direct3D answers "not yet": Unreal's rendering thread is waiting
// for a query, so OcclusionPoll answers the ones still being counted instead of letting it wait for the GPU.
std::atomic<bool> g_gow2_query_waited{false};

REXCVAR_DEFINE_BOOL(xmow_borrado_rectangulo_del_juego, true, "XMOW",
                    "Los borrados emulados de listas de rectangulos cubren solo los rectangulos del juego (sus vertices por el viewport) dentro de su tijera, no toda la superficie: UE3 marca el stencil de cada luz asi, y a toda pantalla cada luz tenia la pantalla entera");
REXCVAR_DEFINE_BOOL(xmow_msaa2x_como_1x, true, "XMOW",
                    "Superficies MSAA 2X (la escena, en dos teselas) dibujadas como 1X de la misma altura, sin el alias "
                    "de altura doble de Dead Space 2 (con el, la mitad de abajo salia negra y la de arriba estirada)");
REXCVAR_DEFINE_BOOL(xmow_clear_color_vertice, true, "XMOW",
                    "Color de los borrados de Direct3D por rectangulo: el color del vertice (el PS n4567 lo escribe), "
                    "no c0 del PS (que guarda el (8 0 0 0) del motor)");
REXCVAR_DEFINE_BOOL(xmow_rectangulos_como_borrado, true, "XMOW",
                    "Toda lista de rectangulos (primitiva 8) se hace como borrado emulado aunque su VS este en la "
                    "biblioteca: en Wolverine son los shaders de Clear del propio Direct3D (VS n2367 / PS n4567, sacados "
                    "del XEX), y el renderizador no dibuja listas de rectangulos (causa 108)");
REXCVAR_DEFINE_BOOL(xmow_captura_ab, false, "XMOW",
                    "Diagnostico de las manchas blancas (teste 15): tras cada captura automatica, 4 cuadros sin un grupo "
                    "de dibujos y otra captura, por turnos: auto_NNNN_ta = sin los translucidos con mezcla alfa "
                    "(SRC_ALPHA x 1-SRC_ALPHA), _to = sin los demas translucidos (7e3, con mezcla), (grupos viejos: _tr = "
                    "todos los translucidos, _sm = sombras moduladas, _ps = PS de xmow_ab_ps) "
                    "(XMOW: padrao = xmow.toml entregue)");
REXCVAR_DEFINE_STRING(xmow_ab_ps, "10127,1121,1917,2639,6971", "XMOW",
                      "Numeros de PS (de la biblioteca) que la captura _ps no dibuja; sus 3 primeros dibujos de la "
                      "sesion van al log con texturas y constantes");
REXCVAR_DEFINE_STRING(xmow_omitir_ps, "", "XMOW",
                      "Pixel shaders (numeros del dslib, separados por coma) que nunca se dibujan. 2639: la niebla por "
                      "altura/profundidad de pantalla entera, que lavaba toda la imagen en blanco azulado (teste 17/18). "
                      "Desde 0.1.19 la niebla la decide xmow_niebla_altura (AUTO); 2639 aqui la quita siempre");
REXCVAR_DEFINE_INT32(xmow_niebla_altura, 0, "XMOW",
                     "Full screen height fog (PS n2639; quick menu HEIGHT FOG): 0 = AUTO (drawn everywhere, the frontend "
                     "too since 0.1.20), 1 = always, 2 = off in gameplay only (since 0.1.32: the title and menus keep "
                     "their fog; the frontend is told by the fog's own constants and by the map package, as LIGHTING)");
REXCVAR_DEFINE_INT32(gow2_auto_captura_s, 0, "XMOW",
                     "Cada cuantos segundos se guarda una captura reducida (640x360) en capturas/auto_NNNN.bmp; 0 = nunca");
REXCVAR_DEFINE_INT32(gow2_auto_volcado_primero_s, 120, "XMOW",
                     "Segundo de juego del primer volcado automatico de las texturas resueltas de un fotograma");
REXCVAR_DEFINE_INT32(gow2_auto_volcado_cada_s, 300, "XMOW", "Segundos entre volcados automaticos; 0 = solo el primero");
REXCVAR_DEFINE_BOOL(gow2_sin_prepaso_enmascarado, true, "XMOW",
                    "Sin el pase previo enmascarado (alpha test): dibujos color+profundidad en la profundidad de la escena "
                    "(EDRAM 000) con todos los canales de color apagados y escritura de profundidad. Como en singularity-nx: "
                    "dibujado, el pase base (GEQUAL) no coincidia por redondeo y el suelo salia negro (teste 16)");
REXCVAR_DEFINE_BOOL(gow2_sin_prepaso_z, true, "XMOW",
                    "Sin el pase previo de solo profundidad de la escena: el pase base escribe su propia profundidad. "
                    "Con el pase previo, la profundidad de dos vertex shaders compilados aparte no coincide bit a bit y "
                    "el pase base (GEQUAL) se descartaba: el mundo salia negro (XMOW teste 16: true, el suelo salia negro)");
// Gears of War 2 specifics of gears2-nx, all OFF / neutral by default in the XMOW build (what each one assumed is a
// Gears shader hash or the Gears EDRAM layout; Wolverine's are different or unknown):
REXCVAR_DEFINE_BOOL(gow2_ajustes_gears2, false, "XMOW",
                    "Activa los ajustes por hash de shader de Gears of War 2 (VS de arboles 38234053372F8F2F, VS de "
                    "resplandor/corona, PS del buffer de velocidades 1929EAB6BE60FA77). En Wolverine no significan nada: "
                    "apagado. Prueba/diagnostico solamente");
REXCVAR_DEFINE_INT32(gow2_edram_base_escena, -1, "XMOW",
                     "Base EDRAM (0..0xFFF) del color 0 del pase base de la escena, para los modos de busqueda y la sonda. "
                     "-1 = cualquiera (Gears of War 2 usaba 0x2D0, que no se sabe para Wolverine)");
REXCVAR_DEFINE_INT32(gow2_formato_escena, -1, "XMOW",
                     "Formato de color (0..15) del color 0 del pase base de la escena, para los modos de busqueda y la "
                     "sonda. -1 = cualquiera (Gears of War 2: 3)");
REXCVAR_DEFINE_INT32(gow2_formato_luces, -1, "XMOW",
                     "Formato de color del buffer de luces para el modo de omision 3 (dibujos a gow2_edram_base_escena "
                     "con este formato). -1 = ninguno (Gears of War 2: 12)");
REXCVAR_DEFINE_STRING(gow2_logos_ocultos, "", "XMOW",
                      "Con gow2_saltar_logos: partes de ruta (separadas por comas) de los videos de arranque que el juego "
                      "no debe encontrar (rex_archivos_ocultos). Vacio = ninguno");
REXCVAR_DEFINE_BOOL(gow2_consultas_siempre_visibles, false, "XMOW",
                    "Consultas de oclusion: cada segundo ZPD cuenta como el final de una consulta visible, aunque no lleve la marca de D3D (prueba)");
REXCVAR_DEFINE_BOOL(gow2_oclusion_real, false, "XMOW",
                    "Consultas de oclusion reales: la GPU cuenta los pixeles de la caja de cada consulta que pasan la "
                    "profundidad, y el juego deja de dibujar lo que queda tapado. false = todo visible, como antes "
                    "(XMOW: padrao = xmow.toml entregue)");
REXCVAR_DEFINE_INT32(gow2_oclusion_espera_ms, 40, "XMOW",
                     "Consultas de oclusion reales: milisegundos desde el final de una consulta que el juego puede "
                     "esperar su resultado; pasado ese tiempo se le contesta visible");
REXCVAR_DEFINE_INT32(gow2_oclusion_sin_esperar_ms, 8, "XMOW",
                     "Consultas de oclusion reales: cuando el juego se pone a esperar un resultado, las consultas "
                     "terminadas hace al menos estos milisegundos que la GPU aun no conto se contestan visibles al "
                     "momento (sin parar el fotograma). -1 = esperar hasta gow2_oclusion_espera_ms");
REXCVAR_DEFINE_STRING(gow2_sonda, "700,650", "XMOW",
                      "Sonda del fotograma tras la traza (ZL+ZR+Left): x,y de un cuadrado de 8x8 pixeles; cada dibujo "
                      "del pase base se dibuja solo ahi y la GPU cuenta sus pixeles: el ultimo que pinta es el que se ve "
                      "en ese punto (vacio = sin sonda)");
REXCVAR_DEFINE_BOOL(gow2_saltar_logos, false, "XMOW",
                    "Sin los videos de los logotipos del arranque (los de gow2_logos_ocultos). Apagado por defecto: "
                    "en Gears of War 2, sin el archivo el juego leia un puntero nulo (pantalla negra)");
REXCVAR_DEFINE_BOOL(gow2_busqueda_ps, false, "XMOW",
                    "Modos de busqueda (ZL+ZR+Right): cada pulsacion deja sin dibujar un pixel shader del pase base "
                    "(false: un vertex shader de los que escriben la profundidad de la escena)");
REXCVAR_DEFINE_BOOL(gow2_busqueda_directa, false, "XMOW",
                    "ZL+ZR+Right va directo a los modos de busqueda, sin pasar por los modos 1-10");
REXCVAR_DEFINE_BOOL(gow2_saltar_vs_rotos, false, "XMOW",
                    "(Solo con gow2_ajustes_gears2.) No dibujar los vertex shaders que rompen la geometria (Gears nivel 2: el VS 38234053372F8F2F, con "
                    "matrices elegidas por indice, dibuja triangulos gigantes sobre toda la pantalla)");
REXCVAR_DEFINE_INT32(gow2_arboles, 1, "XMOW",
                     "Los arboles del nivel 2 (VS 38234053372F8F2F): 0 = no se dibujan (gow2_saltar_vs_rotos), 1 = tal cual, "
                     "2 = con las matrices de balanceo (c11-c22) cambiadas por la identidad, 3 = identidad y c10 = 0, "
                     "4 = solo c10 = 0");
REXCVAR_DEFINE_BOOL(gow2_velocidad_sin_z, false, "XMOW",
                    "Prueba: los dibujos del buffer de velocidades (PS 1929EAB6BE60FA77) sin test de profundidad contra "
                    "la profundidad reducida (5A0, 640x360)");
REXCVAR_DEFINE_BOOL(gow2_velocidad_prueba, false, "XMOW",
                    "ZL+ZR+Right cambia gow2_velocidad_sin_z con una captura, en vez de los modos de busqueda");
REXCVAR_DEFINE_BOOL(gow2_arboles_prueba, false, "XMOW",
                    "ZL+ZR+Right cambia el modo de los arboles (0, 1, 2) con una captura, en vez de los modos de busqueda");
REXCVAR_DEFINE_INT32(gow2_auto_volcado_dibujos, 800, "XMOW",
                     "Dibujos que debe tener el fotograma anterior para un volcado automatico (la escena 3D, no un menu)");
REXCVAR_DEFINE_INT32(gow2_auto_volcados_max, 0, "XMOW",
                     "Volcados automaticos como mucho (0 = ninguno) (XMOW: padrao = xmow.toml entregue)");
REXCVAR_DEFINE_INT32(gow2_traza_cada_s, 0, "XMOW", "Segundos entre trazas de la estructura de un fotograma pesado");
REXCVAR_DEFINE_INT32(gow2_trazas_max, 0, "XMOW",
                     "Trazas de fotograma como mucho (XMOW: padrao = xmow.toml entregue)");
REXCVAR_DEFINE_BOOL(gow2_perfil_pilas, false, "XMOW",
                    "Crea logs/rex/perfil_pilas.flag al arrancar: el perfil de CPU muestrea las pilas de cada hilo "
                    "(rex_perfil.log), algo mas lento");
#endif
REXCVAR_DEFINE_INT32(xmow_vblank_hz, 60, "XMOW",
                     "Vblank rate given to the game. The game waits for 2 vblanks per frame (30 fps). 120 lets it reach 60 "
                     "fps (EXPERIMENTAL: anything tied to the vblank runs faster).");
// CPU cuts on the ring thread (test 22: the ring thread 48 % busy, the rendering thread waiting for it).
REXCVAR_DEFINE_BOOL(xmow_anillo_rptr_rapido, true, "XMOW",
                    "Ring: the read pointer goes back to the game before the ring waits (WAIT_REG_MEM) and at the end "
                    "of each batch before waiting for the renderer's uploads, and each publication wakes the threads "
                    "waiting in Direct3D's spin (xmow_spin_cede)");
REXCVAR_DEFINE_BOOL(xmow_ring_wake_once, false, "XMOW",
                    "0.1.33 (test): a CP_RB_WPTR write (Direct3D's kick) that finds the ring thread asleep takes the "
                    "'asleep' flag before waking it, so the kicks that follow while the ring thread is still waking up "
                    "do not take the ring mutex (Horizon hands it to the woken thread at the signal: the render thread "
                    "slept in svcArbitrateLock, 2 % of its time in test 34). false = every such kick locks and "
                    "notifies, as 0.1.32. Live");
REXCVAR_DEFINE_BOOL(xmow_imload_rapido, true, "XMOW",
                    "IM_LOAD_IMMEDIATE: a shader load whose raw microcode (hash of the packet's words) was identified "
                    "before reuses that answer, without the byte swap, the library lock and the identification");
REXCVAR_DEFINE_BOOL(xmow_diag_solo_activo, true, "XMOW",
                    "Per-draw diagnostics (A/B capture details, search mode candidates, format/primitive counters of "
                    "the 10 s report) only in frames where a diagnostic is active (trace, probe, A/B capture, skip or "
                    "search mode); false = every frame, as before. The search modes take their candidates from the "
                    "frame before the press: with true they need one of those on, or false here");
// 0.1.22: stack sampling of the SDK profiler (switch_perf.cpp), switched on and off during play from the menu.
extern "C" bool RexSwitchPerfStacksActive(void);
extern "C" bool RexSwitchPerfToggleStacks(void);
// 0.1.26 (SDK 1032): the two busiest threads are sampled every millisecond and the raw samples go to
// logs/rex/rex_pilhas.bin through the low-priority SD writer. 0.1.28: off by default (test 34: on for the whole test).
REXCVAR_DEFINE_BOOL(xmow_amostragem_pilas, false, "XMOW",
                    "Samples where every thread spends its time (every millisecond, with call stacks; the two busiest "
                    "threads on every millisecond) into the profile log, and the raw samples into rex_pilhas.bin. The "
                    "game runs a little slower while it is on. Live: checked once per game frame");
extern "C" void RexSwitchPerfFijarVolcadoCrudo(bool (*escribir)(const char* ruta, void* datos, size_t bytes),
                                               unsigned tope_mb);
REXCVAR_DEFINE_INT32(xmow_amostragem_volcado_mb, 192, "XMOW",
                     "Cap per boot, in MB, of the raw stack samples written to logs/rex/rex_pilhas.bin while stack "
                     "sampling is on (about 1-2 MB per 10 s; written by the low-priority SD writer, in pieces). 0 = no "
                     "raw dump")
    .range(0, 2048);
REXCVAR_DEFINE_BOOL(xmow_pular_caixas_consulta, true, "XMOW",
                    "With gow2_oclusion_real = false: the occlusion query boxes (draws between a query's begin and end "
                    "with every color channel masked off and no depth or stencil write) are not sent to the renderer; "
                    "the query is still answered visible. They paint nothing, about a third of the draws in open views");
// 0.1.21 (diagnostico-0.1.20.md item 6): conservative real occlusion. The game is answered at once (never waits), from
// a history kept per primitive; the GPU counts the boxes in the background only to feed that history.
REXCVAR_DEFINE_BOOL(xmow_oclusao_conservadora, false, "XMOW",
                    "With gow2_oclusion_real = false: occlusion queries are answered at once from the history of their "
                    "box (keyed by a hash of the box vertices): hidden only after two valid GPU counts of 0 in a row "
                    "(depth target without a pending clear or contenido_invalido), visible otherwise. The boxes are "
                    "drawn and counted on the GPU without the game waiting for it: every frame for a box last counted 0, "
                    "every xmow_oclusao_reteste_quadros frames for a visible one (skipped the other frames, as "
                    "xmow_pular_caixas_consulta). false = as 0.1.20");
REXCVAR_DEFINE_INT32(xmow_oclusao_reteste_quadros, 4, "XMOW",
                     "xmow_oclusao_conservadora: a box whose last count was visible is drawn and counted once every this "
                     "many frames (spread over the frames by its hash); 1 = every frame");
REXCVAR_DEFINE_INT32(xmow_log_hitch_ms, 50, "XMOW",
                     "Frames (Swap to Swap on the ring thread) longer than this many ms are collected as [xmow hitch] "
                     "lines (frame time, draws, query boxes skipped, textures uploaded, waits) and written in the 10 s "
                     "summary, at most 20 per 10 s (the log is written synchronously to the SD). 0 = off");
REXCVAR_DECLARE(bool, xmow_spin_cede);           // src/xmow_perf_hooks.cpp
REXCVAR_DECLARE(int32_t, xmow_spin_cede_us);     // src/xmow_perf_hooks.cpp
REXCVAR_DECLARE(bool, xmow_consulta_inmediata);  // src/xmow_perf_hooks.cpp
REXCVAR_DECLARE(bool, xmow_precarga_mapa);       // src/gow2_shaders.cpp (0.1.27)
// 0.1.28: logged at boot with the [xmow perf] line.
REXCVAR_DECLARE(int32_t, xmow_d3d_quadros_adiante);  // src/xmow_perf_hooks.cpp
REXCVAR_DECLARE(int32_t, xmow_distancia_corte);      // src/xmow_game_patches.cpp
REXCVAR_DECLARE(int32_t, xmow_detalhe_malha);        // src/xmow_game_patches.cpp
REXCVAR_DECLARE(int32_t, xmow_iluminacao);           // src/xmow_resolucao_interna.cpp
REXCVAR_DECLARE(bool, xmow_dk_constantes_sucias);    // sdk-g2 src/x360/deko/dibujos.cpp
REXCVAR_DECLARE(bool, xmow_registro_barato);         // sdk-g2 src/x360/deko/destinos.cpp
REXCVAR_DECLARE(bool, xmow_gpu_boost);               // src/xmow_gpu_boost.cpp
REXCVAR_DECLARE(int32_t, xmow_corte_pequenos);       // src/xmow_game_patches.cpp (0.1.29)
REXCVAR_DECLARE(bool, xmow_dk_barreira_copia_seletiva);  // sdk-g2 src/x360/deko/destinos.cpp (xmow 1035)
REXCVAR_DECLARE(bool, xmow_dk_menos_estado);             // sdk-g2 src/x360/deko/dibujos.cpp (xmow 1036, 0.1.30)
REXCVAR_DECLARE(bool, xmow_dk_resolve_z_unico);          // sdk-g2 src/x360/deko/destinos.cpp (xmow 1036, 0.1.30)
REXCVAR_DECLARE(bool, xmow_d3d_nativo);                  // src/xmow_perf_hooks.cpp (0.1.30)
REXCVAR_DECLARE(bool, xmow_dk_remapeo_normalizado);      // sdk-g2 src/x360/deko/dibujos.cpp (xmow 1037, 0.1.31)
REXCVAR_DECLARE(bool, xmow_sem_prepasso_motor);
// 0.1.33 (boot line)
REXCVAR_DECLARE(bool, xmow_resolucao_dinamica);  // src/xmow_resolucao_interna.cpp
REXCVAR_DECLARE(bool, xmow_sem_distorcao);
REXCVAR_DECLARE(bool, xmow_sem_bloom);
REXCVAR_DECLARE(bool, xmow_sem_volumes_nevoa);
REXCVAR_DECLARE(bool, xmow_diag_sem_speedtree);
REXCVAR_DECLARE(int32_t, xmow_native_shader_params);  // src/xmow_cpu33_params.cpp (0.1.33; 0.1.33d: 2 = SELF-CHECK)
REXCVAR_DECLARE(bool, xmow_dk_vs_entrada_plegada);     // sdk-g2 dibujos.cpp (xmow 1039, 0.1.33d)
namespace rex::x360 {
extern std::atomic<uint32_t> g_xmow_reinicio_informes;  // sdk-g2 destinos.cpp (0.1.33d): bumped by live menu changes
}
REXCVAR_DECLARE(int32_t, xmow_material_replay);    // src/xmow_cpu33_material.cpp (0.1.33)
REXCVAR_DECLARE(bool, xmow_dk_tiled_cache);             // sdk-g2 destinos.cpp
REXCVAR_DECLARE(int32_t, nfsmw_dk_envio_parcial);       // sdk-g2 dibujos.cpp
// 0.1.29 (item 3): inert draws outside occlusion queries.
REXCVAR_DEFINE_BOOL(xmow_pular_inertes, true, "XMOW",
                    "Outside occlusion queries, a draw that writes no color channel, no depth and no stencil (the "
                    "[xmow census] inert class, about 96 per frame in the jungle) is not sent to the renderer: it "
                    "cannot change the image (the renderer has no memexport). Not with gow2_oclusion_real, nor in the "
                    "probe or the L3 + R3 traced frame. false = sent as in 0.1.28. Live");
// 0.1.29 (item 2): diagnostic of the GPU's fixed cost per draw.
REXCVAR_DEFINE_BOOL(xmow_diag_tijera_1px, false, "XMOW",
                    "Diagnostic, off by default: every game draw is scissored to the single pixel at 0,0, so the GPU "
                    "still does each draw's fixed work (state, vertices, setup) but shades almost no pixels. The GPU "
                    "time that is left ([deko3d] GPU busy, fps) is the per-draw fixed cost. THE IMAGE IS WRONG while "
                    "it is on (the port menu still shows). Live");
// 0.1.30: the same diagnostic with a third state. 0 = off, 1 = 1 PIXEL (as xmow_diag_tijera_1px), 2 = EMPTY: every game
// draw keeps all of its state, uploads and binds but is emitted with 0 vertices (PeticionDibujo::vacio): what is left
// of the GPU time is the cost of the state changes alone. xmow_diag_tijera_1px = true still means 1 PIXEL.
REXCVAR_DEFINE_INT32(xmow_diag_custo_dibujo, 0, "XMOW",
                     "Diagnostic, off by default: 0 = off; 1 = 1 PIXEL (every game draw scissored to the pixel at 0,0, "
                     "as xmow_diag_tijera_1px); 2 = EMPTY (every game draw recorded with all of its state but 0 "
                     "vertices: the GPU pays for the state changes only); 3 = ONE TRIANGLE (all of the state, only the "
                     "first triangle drawn). THE IMAGE IS WRONG while it is on (the port menu still shows). Never saved "
                     "by the menu, always 0 at start (0.1.33c). Live")
    .range(0, 3);
// 0.1.29 (item 5): L3 + R3 takes a screen capture; the full dump only when asked for.
REXCVAR_DEFINE_BOOL(xmow_l3r3_volcado_completo, false, "XMOW",
                    "L3 + R3: false = only a screen capture of the presented frame (capturas/cap_NNN.bmp, about 2.7 MB, "
                    "no stall). true = also the full diagnostic dump of the next frame (every resolved texture as BMP "
                    "in volcado_NN/ and the draw-by-draw trace in the log; tens of MB to the SD card, the game stalls "
                    "while it is written). Live");
// 0.1.29 (item 4): xmow_iluminacao per map (src/xmow_resolucao_interna.cpp).
void XmowIluminacaoPorMapa(const char* paquete, bool jogo);
bool XmowMapaDeJogo();  // 0.1.32: the last map package opened is a gameplay map (xmow_niebla_altura 2)
// 0.1.33d (review S-B): every height fog draw is classified (frontend fog constants or not) and counted for the game
// thread, which decides at the start of a frame (xmow_resolucao_interna.cpp AlInicioDelCuadro) whether the game went
// back to the main menu. Nothing is applied from the ring thread.
void XmowNieblaClasificada(bool frontal);

namespace gow2::gpu {
namespace {

// 0.1.27: the last persistent level the game opened ("jungle1_p"), set on the thread that opens it (the kernel's
// g_xmow_paquete_ue3 hook), read by MapaPorSwap on the ring.
std::mutex& MapaPendienteCandado() {
  static std::mutex m;
  return m;
}
std::string& MapaPendiente() {
  static std::string s;
  return s;
}
void PaqueteUe3Abierto(const char* nombre) {
  // 0.1.34b (review S8): the kernel's "[XMOW mapa]" line (sdk-g2 xboxkrnl_io.cpp, the first open of each package
  // name) is INFO and PLAY MODE keeps WARN and up: here the same line at WARN, so a crash log still shows what loaded.
  if (xmow::bench::ModoJogoEfetivo()) {
    static std::mutex cerrojo;
    static std::unordered_set<std::string> vistos;
    bool nuevo = false;
    {
      std::lock_guard<std::mutex> l(cerrojo);
      nuevo = vistos.size() < 8192 && vistos.insert(nombre).second;
    }
    if (nuevo) REXLOG_WARN("[XMOW mapa] {}", nombre);
  }
  // "<map>_p.xxx", any case: the persistent level of a map (Wolverine's maps stream their zones as other packages).
  std::string n(nombre);
  for (char& c : n) c = char(std::tolower(static_cast<unsigned char>(c)));
  // 0.1.29 (xmow_iluminacao per map): a gameplay map, or the frontend map (WStart.xxx: title and menus), before its
  // actors attach their lights. The name may come with a path.
  {
    const size_t barra = n.find_last_of("\\/:");
    const std::string_view archivo = barra == std::string::npos ? std::string_view(n) : std::string_view(n).substr(barra + 1);
    if (archivo == "wstart.xxx") {
      XmowIluminacaoPorMapa("WStart.xxx", false);
    } else if (archivo.size() >= 7 && archivo.substr(archivo.size() - 6) == "_p.xxx") {
      XmowIluminacaoPorMapa(std::string(archivo).c_str(), true);
    }
  }
  if (n.size() < 7 || n.compare(n.size() - 6, 6, "_p.xxx") != 0) return;
  n.resize(n.size() - 4);
  for (char& c : n) {
    if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_' && c != '-') c = '_';
  }
  std::lock_guard<std::mutex> l(MapaPendienteCandado());
  MapaPendiente() = std::move(n);
}

namespace xenos = rex::graphics::xenos;
using rex::X_STATUS;  // X_STATUS_SUCCESS and X_STATUS_UNSUCCESSFUL need it too
using Clock = std::chrono::steady_clock;

constexpr uint32_t kMmioBase = 0x7FC80000;
constexpr uint32_t kMmioMask = 0xFFFF0000;
constexpr uint32_t kMmioSize = 0x0000FFFF;
constexpr uint32_t kRegisterCount = 0x5003;  // RegisterFile::kRegisterCount

constexpr uint32_t kRegCpRbWptr = rex::graphics::XE_GPU_REG_CP_RB_WPTR;
constexpr uint32_t kRegRbEdramTiming = 0x0F00;
constexpr uint32_t kRegRbBcControl = 0x0F01;
constexpr uint32_t kRegD1ModeVCounter = 0x194C;
constexpr uint32_t kRegD1ModeVblankVlineStatus = 0x1951;
constexpr uint32_t kRegD1ModeViewportSize = 0x1961;

constexpr int kMaxIndirectDepth = 4;
// The real GPU waits as long as it takes; without one some conditions are never met, so the wait is cut
// short and the ring goes on.
constexpr auto kWaitRegMemMax = std::chrono::milliseconds(200);
constexpr uint16_t kMaxExtent = 2048 >> 3;  // xenos::kTexture2DCubeMaxWidthHeight >> 3
constexpr int32_t kFakeSampleCount = 1000;  // the SDK's query_occlusion_fake_sample_count
constexpr auto kReportEvery = std::chrono::seconds(10);

#if GOW2_DEKO3D
// 0.1.26 (SDK 1032): the raw stack samples of each 10 s interval (switch_perf.cpp), appended to rex_pilhas.bin on the
// renderer's SD writer thread (priority 0x3B, in pieces of xmow_volcado_trozo_kb with pauses), never on the ring.
// Refused while the writer has other work queued (an L3 + R3 dump), so a dump is never delayed by more than one block.
std::atomic<uint32_t> g_pilhas_pendentes{0};
bool EscritorPilhas(const char* ruta, void* datos, size_t bytes) {
  // Also refused with 4 blocks (~8 MB) still queued: a slow card loses intervals instead of piling up memory.
  const uint32_t mias = g_pilhas_pendentes.load(std::memory_order_acquire);
  if (rex::x360::EscriturasSdPendientes() > mias || mias >= 4) {
    return false;
  }
  g_pilhas_pendentes.fetch_add(1, std::memory_order_acq_rel);
  std::filesystem::path path(ruta);
  rex::x360::EncolarEscrituraSd([path, datos, bytes]() {
    if (!rex::x360::EscribirArchivoEnTrozos(path, datos, bytes, true)) {
      REXLOG_WARN("[xmow perf] raw stack samples: could not append {} bytes to {}", bytes, path.string());
    }
    std::free(datos);
    g_pilhas_pendentes.fetch_sub(1, std::memory_order_acq_rel);
  });
  return true;
}
#endif

// Reads the ring (a power-of-two buffer of words that wraps) or an indirect buffer (linear).
struct Reader {
  const uint8_t* base = nullptr;
  uint32_t mask = 0;  // words - 1 in the ring; 0 in a linear buffer
  uint32_t pos = 0;
  uint32_t end = 0;

  uint32_t Pending() const { return mask ? ((end - pos) & mask) : (end - pos); }
  uint32_t Peek() const { return rex::memory::load_and_swap<uint32_t>(base + size_t(pos) * 4); }
  uint32_t Read() {
    const uint32_t value = Peek();
    Skip(1);
    return value;
  }
  void Skip(uint32_t words) { pos = mask ? ((pos + words) & mask) : (pos + words); }
};

bool Compare(uint32_t info, uint32_t value, uint32_t reference) {
  switch (info & 0x7) {
    case 0x0: return false;
    case 0x1: return value < reference;
    case 0x2: return value <= reference;
    case 0x3: return value == reference;
    case 0x4: return value != reference;
    case 0x5: return value >= reference;
    case 0x6: return value > reference;
    default: return true;
  }
}

// Register base of each SET_CONSTANT / LOAD_ALU_CONSTANT type, or UINT32_MAX.
uint32_t ConstantBase(uint32_t type_index) {
  switch ((type_index >> 16) & 0xFF) {
    case 0: return 0x4000;  // ALU
    case 1: return 0x4800;  // fetch
    case 2: return 0x4900;  // bool
    case 3: return 0x4908;  // loop
    case 4: return 0x2000;  // registers
    default: return UINT32_MAX;
  }
}

class RingGraphicsSystem final : public rex::system::IGraphicsSystem {
 public:
  RingGraphicsSystem() : registers_(kRegisterCount, 0) {}
  ~RingGraphicsSystem() override { Shutdown(); }

  X_STATUS SetupPresentation(rex::ui::WindowedAppContext* app_context) override {
#if GOW2_DEKO3D
    if (presenter_) {
      return X_STATUS_SUCCESS;
    }
    app_context_ = app_context;
    if (!provider_) {
      provider_ = rex::deko3d::ProveedorDeko3d::Crear();  // a deko3d failure ends the process, logged
      if (!provider_) {
        REXLOG_ERROR("[gpu] could not create the deko3d device");
        return X_STATUS_UNSUCCESSFUL;
      }
    }
    auto create = [this]() { presenter_ = provider_->CreatePresenter(); };
    if (app_context_) {
      app_context_->CallInUIThreadSynchronous(create);
    } else {
      create();
    }
    if (!presenter_) {
      REXLOG_ERROR("[gpu] could not create the deko3d presenter");
      return X_STATUS_UNSUCCESSFUL;
    }
    REXLOG_INFO("[gpu] deko3d presenter up (the game's frames come with the renderer)");
#else
    (void)app_context;
#endif
    return X_STATUS_SUCCESS;
  }

  X_STATUS SetupGuestGpu(rex::runtime::FunctionDispatcher* dispatcher,
                         rex::system::KernelState* kernel_state) override {
    dispatcher_ = dispatcher;
    kernel_state_ = kernel_state;
    memory_ = dispatcher->memory();
    if (!mmio_registered_) {
      mmio_registered_ =
          memory_->AddVirtualMappedRange(kMmioBase, kMmioMask, kMmioSize, this, &ReadMmio, &WriteMmio);
      if (!mmio_registered_) {
        REXLOG_ERROR("[gpu] could not register the GPU's MMIO range");
        return X_STATUS_UNSUCCESSFUL;
      }
    }
    if (active_.exchange(true)) {
      return X_STATUS_SUCCESS;
    }
#if GOW2_DEKO3D
    REXCVAR_SET(nfsmw_sombras_sin_vegetacion, false);
    REXCVAR_SET(nfsmw_nativo_ps_solo_alfa, false);
    REXCVAR_SET(nfsmw_dk_sombras_sesgo_pitch, 960);
    // 0.1.33c (review F3): the draw cost probe breaks the image; it never starts on (the menu no longer saves it).
    if (REXCVAR_GET(xmow_diag_custo_dibujo) != 0) {
      REXLOG_INFO("[xmow perf] xmow_diag_custo_dibujo={} from the config ignored: the draw cost probe starts off",
                  REXCVAR_GET(xmow_diag_custo_dibujo));
      REXCVAR_SET(xmow_diag_custo_dibujo, 0);
    }
    // Telemetry (automatic captures, frame dumps, per-draw traces of light frames, CPU stack sampling) only with
    // telemetria.flag next to the NRO: it costs frames (the stack sampler stops every thread each millisecond).
    {
      std::error_code ec;
      telemetry_ = std::filesystem::exists(rex::filesystem::GetExecutableFolder() / "telemetria.flag", ec);
      const auto folder = rex::filesystem::GetExecutableFolder() / "logs" / "rex";
      const auto stack_flag = folder / "perfil_pilas.flag";
      if (telemetry_ && REXCVAR_GET(gow2_perfil_pilas)) {
        std::filesystem::create_directories(folder, ec);
        if (FILE* flag = std::fopen(stack_flag.string().c_str(), "a")) std::fclose(flag);
      } else {
        std::filesystem::remove(stack_flag, ec);  // left by an earlier telemetry run
      }
      REXLOG_INFO("[gpu] telemetry {}", telemetry_ ? "on (telemetria.flag)" : "off");
      REXLOG_INFO("[xmow perf] xmow_spin_cede={} (xmow_spin_cede_us={}) xmow_consulta_inmediata={} "
                  "(gow2_oclusion_real={}) xmow_anillo_rptr_rapido={} xmow_imload_rapido={} xmow_diag_solo_activo={}",
                  REXCVAR_GET(xmow_spin_cede), REXCVAR_GET(xmow_spin_cede_us), REXCVAR_GET(xmow_consulta_inmediata),
                  REXCVAR_GET(gow2_oclusion_real), REXCVAR_GET(xmow_anillo_rptr_rapido),
                  REXCVAR_GET(xmow_imload_rapido), REXCVAR_GET(xmow_diag_solo_activo));
      REXLOG_INFO("[xmow 0.1.28] render ahead xmow_d3d_quadros_adiante={} ({}); view distance xmow_distancia_corte={} % "
                  "(live); mesh detail xmow_detalhe_malha={} % LOD distance (live); lighting xmow_iluminacao={} ({}, per map since 0.1.29); constants by written range "
                  "xmow_dk_constantes_sucias={}; cheap clear/resolve logs xmow_registro_barato={}; stack sampling "
                  "xmow_amostragem_pilas={}; automatic dumps gow2_auto_volcados_max={}, traces gow2_trazas_max={}; "
                  "CPU BOOST / PEAK BOOST removed (no clock is set by the port except the official GPU boost profile, "
                  "xmow_gpu_boost={})",
                  REXCVAR_GET(xmow_d3d_quadros_adiante),
                  REXCVAR_GET(xmow_d3d_quadros_adiante) >= 2 ? "2 frames, test" : "1 frame, the game's own",
                  REXCVAR_GET(xmow_distancia_corte), REXCVAR_GET(xmow_detalhe_malha), REXCVAR_GET(xmow_iluminacao),
                  REXCVAR_GET(xmow_iluminacao) >= 2 ? "NONE: DynamicLights off" : "STANDARD",
                  REXCVAR_GET(xmow_dk_constantes_sucias), REXCVAR_GET(xmow_registro_barato),
                  REXCVAR_GET(xmow_amostragem_pilas), REXCVAR_GET(gow2_auto_volcados_max), REXCVAR_GET(gow2_trazas_max),
                  REXCVAR_GET(xmow_gpu_boost));
      REXLOG_INFO("[xmow 0.1.29] small object cull xmow_corte_pequenos={} ({}, live); lighting xmow_iluminacao={} "
                  "({}, gameplay maps only, applied at each map load); skip inert draws xmow_pular_inertes={}; "
                  "selective copy barrier xmow_dk_barreira_copia_seletiva={}; 1 pixel draws diagnostic "
                  "xmow_diag_tijera_1px={}; L3 + R3 {} (xmow_l3r3_volcado_completo={}); internal resolution shown "
                  "after the FPS counter",
                  REXCVAR_GET(xmow_corte_pequenos),
                  REXCVAR_GET(xmow_corte_pequenos) <= 0   ? "OFF"
                  : REXCVAR_GET(xmow_corte_pequenos) == 1 ? "LOW"
                                                          : "HIGH",
                  REXCVAR_GET(xmow_iluminacao), REXCVAR_GET(xmow_iluminacao) >= 2 ? "NONE" : "STANDARD",
                  REXCVAR_GET(xmow_pular_inertes), REXCVAR_GET(xmow_dk_barreira_copia_seletiva),
                  REXCVAR_GET(xmow_diag_tijera_1px),
                  REXCVAR_GET(xmow_l3r3_volcado_completo) ? "screen capture + full dump" : "screen capture only",
                  REXCVAR_GET(xmow_l3r3_volcado_completo));
      REXLOG_INFO("[xmow 0.1.32] engine depth prepass without its draw lists xmow_sem_prepasso_motor={} (live, "
                  "only with gow2_sin_prepaso_z={} and gow2_sin_prepaso_enmascarado={} on and no occlusion queries); "
                  "native Direct3D state copies xmow_d3d_nativo={} (default true since 0.1.32); height fog "
                  "xmow_niebla_altura={} ({}); draws by primitive type in \"[deko3d] xmow 1038\"",
                  REXCVAR_GET(xmow_sem_prepasso_motor), REXCVAR_GET(gow2_sin_prepaso_z),
                  REXCVAR_GET(gow2_sin_prepaso_enmascarado), REXCVAR_GET(xmow_d3d_nativo),
                  REXCVAR_GET(xmow_niebla_altura),
                  REXCVAR_GET(xmow_niebla_altura) == 2   ? "NO: off on gameplay maps only, the frontend keeps it"
                  : REXCVAR_GET(xmow_niebla_altura) == 1 ? "YES"
                                                         : "AUTO");
      REXLOG_INFO("[xmow 0.1.33] dynamic resolution xmow_resolucao_dinamica={}; gameplay maps without distortion "
                  "xmow_sem_distorcao={}, bloom xmow_sem_bloom={}, fog volumes xmow_sem_volumes_nevoa={}; SpeedTree "
                  "leaves and fronds off xmow_diag_sem_speedtree={}; partial submit every nfsmw_dk_envio_parcial={} draws; "
                  "tiled cache xmow_dk_tiled_cache={}; stack sampling xmow_amostragem_pilas={} (menu, no "
                  "telemetria.flag needed); native shader constant setters xmow_native_shader_params={}; material "
                  "parameter replay xmow_material_replay={} (0 off, 1 VERIFY, 2 ON); ring wake once "
                  "xmow_ring_wake_once={}; folded vertex shaders xmow_dk_vs_entrada_plegada={} (0.1.33d, needs "
                  "shadercache_dksh_vs.bin); native shader parameters 0 off, 1 on, 2 SELF-CHECK",
                  REXCVAR_GET(xmow_resolucao_dinamica), REXCVAR_GET(xmow_sem_distorcao), REXCVAR_GET(xmow_sem_bloom),
                  REXCVAR_GET(xmow_sem_volumes_nevoa), REXCVAR_GET(xmow_diag_sem_speedtree),
                  REXCVAR_GET(nfsmw_dk_envio_parcial), REXCVAR_GET(xmow_dk_tiled_cache),
                  REXCVAR_GET(xmow_amostragem_pilas), REXCVAR_GET(xmow_native_shader_params),
                  REXCVAR_GET(xmow_material_replay), REXCVAR_GET(xmow_ring_wake_once),
                  REXCVAR_GET(xmow_dk_vs_entrada_plegada));
      REXLOG_INFO("[xmow 0.1.31] identity vertex remap sent as identity xmow_dk_remapeo_normalizado={} (live); new "
                  "per frame counters in \"[deko3d] xmow 1037\" (remap, manual fetches, GPU vertex shader invocations "
                  "and input primitives, why resolves are copied) and the depth prepass draws in [xmow census]",
                  REXCVAR_GET(xmow_dk_remapeo_normalizado));
      REXLOG_INFO("[xmow 0.1.30] native Direct3D state copies xmow_d3d_nativo={} (live); fewer state submissions "
                  "xmow_dk_menos_estado={} (live); skip repeated depth resolves xmow_dk_resolve_z_unico={} (live); "
                  "draw cost diagnostic xmow_diag_custo_dibujo={} ({}); view distance xmow_distancia_corte={} %",
                  REXCVAR_GET(xmow_d3d_nativo), REXCVAR_GET(xmow_dk_menos_estado), REXCVAR_GET(xmow_dk_resolve_z_unico),
                  REXCVAR_GET(xmow_diag_custo_dibujo),
                  REXCVAR_GET(xmow_diag_custo_dibujo) == 3   ? "ONE TRIANGLE"
                  : REXCVAR_GET(xmow_diag_custo_dibujo) == 2 ? "EMPTY"
                  : REXCVAR_GET(xmow_diag_custo_dibujo) == 1 ? "1 PIXEL"
                                                             : "NO",
                  REXCVAR_GET(xmow_distancia_corte));
      REXLOG_INFO("[xmow occlusion] xmow_oclusao_conservadora={} xmow_oclusao_reteste_quadros={} "
                  "xmow_pular_caixas_consulta={} (gow2_oclusion_real={} wins when on)",
                  REXCVAR_GET(xmow_oclusao_conservadora), REXCVAR_GET(xmow_oclusao_reteste_quadros),
                  REXCVAR_GET(xmow_pular_caixas_consulta), REXCVAR_GET(gow2_oclusion_real));
      // The renderer's GPU profile of a frame every 20 s (a barrier and two timestamps around each of ~1300 draws, the
      // first draw of each pass drawn twice) and the frame structure traces (hundreds of log lines to the SD card once
      // a minute) are telemetry too: each was a visible hitch.
      if (!telemetry_) {
        REXCVAR_SET(nfsmw_dk_perfil_gpu_s, 0);
      }
      hide_glow_ = std::filesystem::exists(rex::filesystem::GetExecutableFolder() / "sin_resplandor.flag", ec);
      if (hide_glow_) REXLOG_INFO("[gpu] sin_resplandor.flag: lamp glows and coronas are not drawn");
    }
    // Before the game creates its shaders (the create hooks look the containers up in it).
    gow2::shaders::Global().Cargar(rex::filesystem::GetExecutableFolder() / "xmow_shaders.dslib");
    rex::x360::g_xmow_paquete_ue3.store(&PaqueteUe3Abierto, std::memory_order_release);  // 0.1.27 (MapaPorSwap)
#endif
    last_report_ = Clock::now();
    vblank_thread_ = rex::system::object_ref<rex::system::XHostThread>(new rex::system::XHostThread(
        kernel_state_, 128 * 1024, 0, [this]() { return VblankLoop(); }));
    vblank_thread_->set_name("GPU vblank");
    vblank_thread_->Create();
    ring_thread_ = rex::system::object_ref<rex::system::XHostThread>(new rex::system::XHostThread(
        kernel_state_, 128 * 1024, 0, [this]() { return RingLoop(); }));
    ring_thread_->set_name("GPU ring");
    ring_thread_->Create();
    REXLOG_INFO("[gpu] ring reader started");
    // The startup logos: the game skips a movie it cannot open (as it does with the _RUS ones it looks for first).
    if (REXCVAR_GET(gow2_saltar_logos) && REXCVAR_GET(rex_archivos_ocultos).empty() &&
        !REXCVAR_GET(gow2_logos_ocultos).empty()) {
      REXCVAR_SET(rex_archivos_ocultos, REXCVAR_GET(gow2_logos_ocultos));
      REXLOG_INFO("[gpu] startup logos skipped (gow2_saltar_logos)");
    }
    return X_STATUS_SUCCESS;
  }

  bool has_presentation() const override { return presenter_ != nullptr; }
  rex::ui::GraphicsProvider* provider() const override { return provider_.get(); }
  rex::ui::Presenter* presenter() const override { return presenter_.get(); }

  void SetInterruptCallback(uint32_t callback, uint32_t user_data) override {
    callback_data_.store(user_data, std::memory_order_release);
    callback_.store(callback, std::memory_order_release);
    REXLOG_INFO("[gpu] interrupt callback {:08X} ({:08X})", callback, user_data);
  }

  void InitializeRingBuffer(uint32_t ptr, uint32_t size_log2) override {
    // As in CommandProcessor::InitializeRingBuffer: (1 << (size_log2 + 3)) bytes.
    ring_words_.store(uint32_t(1) << (size_log2 + 1), std::memory_order_release);
    ring_base_.store(ptr, std::memory_order_release);
    ring_generation_.fetch_add(1, std::memory_order_acq_rel);
    REXLOG_INFO("[gpu] ring at {:08X}, {} words", ptr, uint32_t(1) << (size_log2 + 1));
  }

  void EnableReadPointerWriteBack(uint32_t ptr, uint32_t block_size_log2) override {
    (void)block_size_log2;
    read_pointer_writeback_.store(ptr, std::memory_order_release);
  }

  void Shutdown() override {
    if (!active_.exchange(false)) {
      DestroyPresentation();
      return;
    }
    {
      std::lock_guard<std::mutex> lock(ring_mutex_);
    }
    ring_cv_.notify_all();
    if (ring_thread_) {
      ring_thread_->Wait(0, 0, 0, nullptr);
      ring_thread_.reset();
    }
    if (vblank_thread_) {
      vblank_thread_->Wait(0, 0, 0, nullptr);
      vblank_thread_.reset();
    }
    DestroyPresentation();
  }

  // After the threads: the presenter goes before its provider, on the UI thread.
  void DestroyPresentation() {
    destinos_.reset();  // frees its deko3d memory before the device goes
    if (presenter_) {
      if (app_context_) {
        app_context_->CallInUIThreadSynchronous([this]() { presenter_.reset(); });
      }
      presenter_.reset();
    }
    provider_.reset();
  }

 private:
  // --- MMIO registers ---------------------------------------------------------

  static uint32_t ReadMmio(void* ppc_context, void* context, uint32_t address) {
    (void)ppc_context;
    return static_cast<RingGraphicsSystem*>(context)->ReadMmioRegister(address);
  }

  static void WriteMmio(void* ppc_context, void* context, uint32_t address, uint32_t value) {
    (void)ppc_context;
    static_cast<RingGraphicsSystem*>(context)->WriteMmioRegister(address, value);
  }

  // The same fixed values as the SDK's emulation (graphics/graphics_system.cpp).
  uint32_t ReadMmioRegister(uint32_t address) {
    const uint32_t r = (address & 0xFFFF) / 4;
    switch (r) {
      case kRegRbEdramTiming:
        return 0x08100748;
      case kRegRbBcControl:
        return 0x0000200E;
      case kRegD1ModeVCounter: {
        rex::system::X_VIDEO_MODE mode;
        rex::kernel::xboxkrnl::VdQueryVideoMode(&mode);
        return std::min(uint32_t(mode.display_height), uint32_t(0x0FFF));
      }
      case kRegD1ModeVblankVlineStatus:
        return 1;  // in vblank
      case kRegD1ModeViewportSize: {
        rex::system::X_VIDEO_MODE mode;
        rex::kernel::xboxkrnl::VdQueryVideoMode(&mode);
        const uint32_t width = std::min(uint32_t(mode.display_width), uint32_t(0x0FFF));
        const uint32_t height = std::min(uint32_t(mode.display_height), uint32_t(0x0FFF));
        return (width << 16) | height;
      }
      default:
        return Register(r);
    }
  }

  void WriteMmioRegister(uint32_t address, uint32_t value) {
    const uint32_t r = (address & 0xFFFF) / 4;
    if (r < kRegisterCount) {
      registers_[r] = value;  // no side effects through MMIO (graphics_system.cpp)
#if GOW2_DEKO3D
      if (r >= 0x4000 && r < 0x4800) MarcarConstante(r);  // xmow 1034: never seen, but then the renderer compares it
#endif
    }
    if (r == kRegCpRbWptr) {
      write_pointer_.store(value, std::memory_order_seq_cst);
      wptr_escritas_.store(wptr_escritas_.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);
      // Only a sleeping ring thread needs the lock and the notify (each KickOff paid both, 3-5 % of the rendering
      // thread). seq_cst pairs with ring_dormido_ in RingLoop's wait predicate: either the ring's check sees this write
      // pointer, or this sees the flag and wakes it; taking the lock for an instant keeps the wakeup from being lost.
      // 0.1.33 (xmow_ring_wake_once): the kick takes the flag, so only the first kick after the ring went to sleep
      // locks and notifies (the predicate sets the flag again before every sleep and reads the pointer after it).
      const bool dormido = REXCVAR_GET(xmow_ring_wake_once)
                               ? ring_dormido_.exchange(false, std::memory_order_seq_cst)
                               : ring_dormido_.load(std::memory_order_seq_cst);
      if (dormido) {
        wptr_despertares_.store(wptr_despertares_.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);
        // 0.1.33d (Fable review 3.2): the time the kick spends taking the ring's lock and notifying, measured only
        // here (a sleeping ring), so the ring wake cost is a number in the report instead of an A/B by fps.
        const uint64_t t0 = NsAnillo();
        {
          std::lock_guard<std::mutex> lock(ring_mutex_);
        }
        const uint64_t t1 = NsAnillo();
        ring_cv_.notify_one();
        const uint64_t t2 = NsAnillo();
        kick_ns_cerrojo_.store(kick_ns_cerrojo_.load(std::memory_order_relaxed) + (t1 - t0), std::memory_order_relaxed);
        kick_ns_aviso_.store(kick_ns_aviso_.load(std::memory_order_relaxed) + (t2 - t1), std::memory_order_relaxed);
        if (t1 - t0 > kick_ns_max_.load(std::memory_order_relaxed)) kick_ns_max_.store(t1 - t0, std::memory_order_relaxed);
      }
    }
  }

  uint32_t Register(uint32_t index) const { return index < kRegisterCount ? registers_[index] : 0; }

#if GOW2_DEKO3D
  // xmow 1034: the 16-byte constant register of a register index 0x4000-0x47FF, marked changed for the next draw.
  void MarcarConstante(uint32_t index) {
    const uint32_t c = ((index - 0x4000) >> 2) & 0xFF;
    (index < 0x4400 ? sucias_vs_ : sucias_ps_)[c >> 6] |= uint64_t(1) << (c & 63);
  }
#endif

  // Register writes from the ring, with the side effects the game relies on.
  void WriteRegister(uint32_t index, uint32_t value) {
    if (index >= kRegisterCount) {
      return;
    }
    if (index == rex::graphics::XE_GPU_REG_COHER_STATUS_HOST) {
      value |= UINT32_C(0x80000000);
    }
#if GOW2_DEKO3D
    // What the renderer caches per generation (PeticionDibujo): bumped only when a value really changes.
    if (registers_[index] != value) {
      if (index >= 0x4000 && index < 0x4800) {
        ++(index < 0x4400 ? generacion_constantes_vs_ : generacion_constantes_ps_);
        MarcarConstante(index);  // xmow 1034
      } else if (index >= 0x4800 && index <= 0x48BF) {
        ++generacion_fetch_;
      } else if (EsRegistroDeEncuadre(index)) {
        ++generacion_encuadre_;
      }
    }
#endif
    registers_[index] = value;
    // Scratch registers are mirrored to memory (SCRATCH_ADDR) for the ones SCRATCH_UMSK selects: the game
    // waits on those values to know where the GPU is.
    if (index >= rex::graphics::XE_GPU_REG_SCRATCH_REG0 && index <= rex::graphics::XE_GPU_REG_SCRATCH_REG7) {
      const uint32_t n = index - rex::graphics::XE_GPU_REG_SCRATCH_REG0;
      if ((UINT32_C(1) << n) & registers_[rex::graphics::XE_GPU_REG_SCRATCH_UMSK]) {
        const uint32_t address = registers_[rex::graphics::XE_GPU_REG_SCRATCH_ADDR] + n * 4;
        rex::memory::store_and_swap<uint32_t>(memory_->TranslatePhysical(address), value);
        GuestSeesGpuProgress();
      }
    }
  }

  // The guest can see the GPU move on (a fence value written to memory, a scratch register mirrored, an interrupt):
  // from there it may rewrite vertex and index memory the GPU already drew from. Dead Space 2 refills one dynamic
  // vertex buffer for its translucent draws this way, several times a frame at the same address. The renderer keeps
  // copies of ranges it already uploaded (in the work slot, and per epoch in the geometry store) and forgets them
  // when this counter moves; without it every translucent batch after the first was drawn with the first one's
  // vertices: the huge triangles across the screen.
  static void GuestSeesGpuProgress() {
#if GOW2_DEKO3D
    rex::x360::g_sincronizaciones_anillo.fetch_add(1, std::memory_order_relaxed);
#endif
    xmow::perf::AvisarProgreso();  // a fence the Direct3D spin may be waiting for (xmow_spin_cede)
  }

  // The ring read pointer back to the game (EnableReadPointerWriteBack), and the spinning threads woken: Direct3D
  // waits for ring space on it. Only what is behind `rptr` has been read.
  void PublicarRptr(uint32_t rptr) {
    const uint32_t writeback = read_pointer_writeback_.load(std::memory_order_acquire);
    if (!writeback) return;
    rex::memory::store_and_swap<uint32_t>(memory_->TranslatePhysical(writeback), rptr);
    xmow::perf::AvisarProgreso();
  }

  // --- GPU memory (the two low bits of the address are the byte order) --------

  uint32_t ReadMemory(uint32_t address) const {
    uint32_t value;
    std::memcpy(&value, memory_->TranslatePhysical(address & ~uint32_t(0x3)), sizeof(value));
    return xenos::GpuSwap(value, static_cast<xenos::Endian>(address & 0x3));
  }

  void WriteMemory(uint32_t address, uint32_t value) {
    GuestSeesGpuProgress();
    const uint32_t swapped = xenos::GpuSwap(value, static_cast<xenos::Endian>(address & 0x3));
    std::memcpy(memory_->TranslatePhysical(address & ~uint32_t(0x3)), &swapped, sizeof(swapped));
  }

  // --- Threads ----------------------------------------------------------------

  // Creates the deko renderer backend on first use (on the ring thread, as the device allows). The game plugs in no
  // effects, so it uses the default hooks.
  bool EnsureRenderer() {
    if (destinos_) {
      return true;
    }
    if (!provider_ || !presenter_) {
      return false;
    }
    destinos_ = rex::x360::CrearDestinosDeko3d(
        static_cast<rex::deko3d::ProveedorDeko3d*>(provider_.get()), memory_);
    if (destinos_) {
      REXLOG_INFO("[gpu] deko3d renderer backend up (minimal renderer: draws, resolves and the Swap's frontbuffer)");
    }
    return destinos_ != nullptr;
  }

  // Runs the game's interrupt callback on the calling thread (one of ours, an XHostThread).
  void Interrupt(uint32_t source, uint32_t cpu) {
    const uint32_t callback = callback_.load(std::memory_order_acquire);
    if (!callback || !dispatcher_) {
      return;
    }
    auto* thread = rex::system::XThread::GetCurrentThread();
    if (!thread) {
      return;
    }
    thread->SetActiveCpu(uint8_t(cpu));
    uint64_t args[] = {source, callback_data_.load(std::memory_order_acquire)};
    dispatcher_->ExecuteInterrupt(thread->thread_state(), callback, args, 2);
  }

  int VblankLoop() {
    rex::system::X_VIDEO_MODE mode;
    rex::kernel::xboxkrnl::VdQueryVideoMode(&mode);
    const double modo_hz = std::max(1.0, double(float(mode.refresh_rate)));
    // The rate comes from xmow_vblank_hz (clamped 30..240); 60 means the video mode's own refresh, as before.
    auto leer_hz = [&]() {
      const int pedido = std::clamp(int(REXCVAR_GET(xmow_vblank_hz)), 30, 240);
      return pedido == 60 ? modo_hz : double(pedido);
    };
    double hz = leer_hz();
    auto interval = std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(1.0 / hz));
    REXLOG_INFO("[xmow quadro] vblank {} Hz", hz);
    auto next = Clock::now() + interval;
    while (active_.load(std::memory_order_acquire)) {
      const double nuevo_hz = leer_hz();
      if (nuevo_hz != hz) {
        hz = nuevo_hz;
        interval = std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(1.0 / hz));
        REXLOG_INFO("[xmow quadro] vblank {} Hz", hz);
      }
      const auto now = Clock::now();
      // After a long pause (loading, a debugger) the missed vblanks are not replayed.
      if (now - next > std::chrono::milliseconds(250)) {
        next = now;
      }
      while (now >= next) {
        counter_.fetch_add(1, std::memory_order_relaxed);
        vblanks_.fetch_add(1, std::memory_order_relaxed);
        Interrupt(0, 2);
        next += interval;
      }
      rex::thread::Sleep(std::chrono::milliseconds(1));
    }
    return 0;
  }

  int RingLoop() {
    uint32_t read = 0;
    uint32_t generation = 0;
    while (active_.load(std::memory_order_acquire)) {
      xmow::perf::AfinarHilo(xmow::perf::Papel::Anillo);  // xmow_afinidade_threads: core 2
      {
        const uint64_t t_ocioso = NsAnillo();  // xmow 1031: ring time per frame ([xmow hitch])
        std::unique_lock<std::mutex> lock(ring_mutex_);
        // 0.1.33: the flag is set by the predicate, only when there is nothing to do, and the write pointer is read
        // again after setting it (with xmow_ring_wake_once a kick takes the flag, so every new wait sets it again).
        const auto hay_trabajo = [&] {
          return !active_.load(std::memory_order_acquire) ||
                 write_pointer_.load(std::memory_order_seq_cst) != read ||
                 ring_generation_.load(std::memory_order_acquire) != generation;
        };
        ring_cv_.wait_for(lock, std::chrono::milliseconds(pending_queries_.empty() ? 4 : 1), [&] {
          if (hay_trabajo()) return true;
          ring_dormido_.store(true, std::memory_order_seq_cst);
          return hay_trabajo();
        });
        ring_dormido_.store(false, std::memory_order_seq_cst);
        anillo_.ocioso += NsAnillo() - t_ocioso;
      }
      if (!active_.load(std::memory_order_acquire)) {
        break;
      }
      {
        const uint64_t t = NsAnillo();
        Report();
        const uint64_t t2 = NsAnillo();
        anillo_.informe += t2 - t;
        OcclusionPoll();
        OclusionConservadoraPoll();  // xmow_oclusao_conservadora: never waits
        ProbePoll();
        anillo_.sondeos += NsAnillo() - t2;
      }
      const uint32_t gen = ring_generation_.load(std::memory_order_acquire);
      if (gen != generation) {
        generation = gen;
        read = 0;  // InitializeRingBuffer leaves the read pointer at zero
      }
      const uint32_t base = ring_base_.load(std::memory_order_acquire);
      const uint32_t words = ring_words_.load(std::memory_order_acquire);
      if (!base || !words) {
        continue;
      }
      const uint32_t written = write_pointer_.load(std::memory_order_acquire) & (words - 1);
      if (written == read) {
        continue;
      }
      ring_activity_ = std::chrono::steady_clock::now();
      Reader reader;
      reader.base = memory_->TranslatePhysical(base);
      reader.mask = words - 1;
      reader.pos = read;
      reader.end = written;
      // The read pointer goes back to the game during a batch too, every kRptrPaso words, as the real command
      // processor does: Direct3D waits for ring space on it (a Gears of War 2 function, kept as a comment only), and a whole batch (with its WAIT_REG_MEMs)
      // used to pass before any space came back. What is behind reader.pos has been read already.
      const uint32_t writeback_lote = read_pointer_writeback_.load(std::memory_order_acquire);
      uint32_t publicado = read;
      // xmow_anillo_rptr_rapido: WAIT_REG_MEM publishes this reader's position before it waits (PacketType3).
      const bool rptr_rapido = REXCVAR_GET(xmow_anillo_rptr_rapido);
      lector_anillo_ = rptr_rapido ? &reader : nullptr;
      const uint64_t t_paquetes = NsAnillo();  // xmow 1031
      while (reader.Pending()) {
        if (!Packet(reader, 0)) {
          break;  // partial packet: the rest comes with the next CP_RB_WPTR
        }
        if (writeback_lote && ((reader.pos - publicado) & reader.mask) >= kRptrPaso) {
          PublicarRptr(reader.pos);
          publicado = reader.pos;
        }
      }
      anillo_.paquetes += NsAnillo() - t_paquetes;
      lector_anillo_ = nullptr;
      read = reader.pos;
      // Not a GuestSeesGpuProgress: it is written after every batch of the ring, and each one made the renderer's
      // geometry store check every range again (an epoch). Ring memory comes back only after a wrap, frames later.
      // With xmow_anillo_rptr_rapido the batch's end goes back before the wait for the renderer's uploads (as the
      // writebacks inside the batch already did): the ring words are consumed, the uploads are of vertex memory.
      if (rptr_rapido && read != publicado) PublicarRptr(read);
#if GOW2_DEKO3D
      if (destinos_) {
        const uint64_t t = NsAnillo();
        destinos_->EsperarSubidas();
        anillo_.subidas += NsAnillo() - t;
      }
#endif
      if (!rptr_rapido) PublicarRptr(read);
    }
    return 0;
  }

  // One line every kReportEvery, on the ring thread, so the log shows whether the game is advancing.
  void Report() {
    const auto now = Clock::now();
    // 0.1.33d (Fable review 5.2): a live menu change closes the window now, so the next window starts at the change
    // and each side of an A/B made with the menu open has whole 10 s reports.
    const uint32_t reinicio = rex::x360::g_xmow_reinicio_informes.load(std::memory_order_relaxed);
    const bool cortado = reinicio != reinicio_visto_;
    reinicio_visto_ = reinicio;
    if (!cortado && now - last_report_ < kReportEvery) {
      return;
    }
    const double seconds = std::chrono::duration<double>(now - last_report_).count();
    last_report_ = now;
    if (cortado) {
      REXLOG_INFO("[gpu] report window cut by a menu change: this report covers {:.1f} s", seconds);
    }
    const uint64_t quadros_intervalo = swaps_ - reported_swaps_;  // 0.1.26 ([xmow census])
    REXLOG_INFO("[gpu] {:.1f} s: {} swaps ({:.1f}/s), {} draws, {} packets, {} vblanks, {} waits cut short",
                seconds, swaps_ - reported_swaps_, double(swaps_ - reported_swaps_) / seconds,
                draws_ - reported_draws_, packets_ - reported_packets_,
                vblanks_.load(std::memory_order_relaxed), waits_cut_short_);
    reported_swaps_ = swaps_;
    reported_draws_ = draws_;
    reported_packets_ = packets_;
    REXLOG_INFO("[gpu] ring waits: {} WAIT_REG_MEM waited {:.1f} ms in total", reg_mem_waits_,
                double(reg_mem_wait_ns_) / 1e6);
    {
      const uint64_t despertares = wptr_despertares_.exchange(0, std::memory_order_relaxed);
      const uint64_t escritas = wptr_escritas_.exchange(0, std::memory_order_relaxed);
      const uint64_t ns_cerrojo = kick_ns_cerrojo_.exchange(0, std::memory_order_relaxed);
      const uint64_t ns_aviso = kick_ns_aviso_.exchange(0, std::memory_order_relaxed);
      const uint64_t ns_max = kick_ns_max_.exchange(0, std::memory_order_relaxed);
      REXLOG_INFO("[gpu] ring wakes: {} of {} write pointer writes found the ring asleep and woke it "
                  "(xmow_ring_wake_once {}); kick lock wait {:.2f} ms in total (longest {:.1f} us), notify {:.2f} ms",
                  despertares, escritas, REXCVAR_GET(xmow_ring_wake_once), double(ns_cerrojo) / 1e6,
                  double(ns_max) / 1e3, double(ns_aviso) / 1e6);
    }
    reg_mem_waits_ = 0;
    reg_mem_wait_ns_ = 0;
#if GOW2_DEKO3D
    hitch_espera_anillo_prev_ = 0;
    const gow2::shaders::Estadisticas s = gow2::shaders::Global().estadisticas();
    REXLOG_INFO("[gpu] shaders: {} loads (address {}, address refused {}, learned {}, microcode {}, similar {}, "
                "unknown {}); objects {} ({} not in the library, {} without a microcode address)",
                s.cargas, s.por_direccion, s.direccion_rechazada, s.por_aprendizaje, s.por_microcodigo,
                s.por_parecido, s.desconocidas,
                s.objetos, s.objetos_sin_entrada, s.objetos_sin_direccion);
    {
      const xmow::perf::Contadores p = xmow::perf::LeerContadores();
      REXLOG_INFO("[xmow perf] {:.1f} s: {} shader loads reused (xmow_imload_rapido, not in the loads above); {} "
                  "Direct3D spin waits yielded ({:.1f} ms); {} occlusion GetData answered at once; per-draw "
                  "diagnostics {}; query boxes skipped {} (xmow_pular_caixas_consulta); log lines dropped so far {} "
                  "(log_async {}); frame throttle (Direct3D sub_82B3CFD0, xmow_d3d_quadros_adiante {}): {} waits, "
                  "{:.1f} ms",
                  seconds, imload_rapidas_, p.esperas, double(p.esperas_us) / 1000.0, p.consultas,
                  diag_activo_ ? "on" : "off (the draw targets / primitive types / indices below are not counted)",
                  caixas_omitidas_ - reported_caixas_, rex::LogLineasDescartadas(),
                  rex::LogIsAsync() ? "on" : "off", REXCVAR_GET(xmow_d3d_quadros_adiante), p.freno,
                  double(p.freno_us) / 1000.0);
      // 0.1.30 (xmow_d3d_nativo)
      REXLOG_INFO("[xmow perf] {:.1f} s: Direct3D state copies done natively (xmow_d3d_nativo {}): shader constants {} "
                  "calls, {} registers; register blocks {} calls, {} registers; runs through Direct3D's slow path {}",
                  seconds, REXCVAR_GET(xmow_d3d_nativo), p.nativo_constantes, p.nativo_constantes_regs,
                  p.nativo_bloques, p.nativo_bloques_regs, p.nativo_lentos);
      // 0.1.32 (xmow_sem_prepasso_motor)
      if (REXCVAR_GET(xmow_sem_prepasso_motor) || p.prepaso_llamadas) {
        std::string motivos;
        if (p.prepaso_motivo & 1) motivos += " gow2_sin_prepaso_z off;";
        if (p.prepaso_motivo & 2) motivos += " gow2_sin_prepaso_enmascarado off;";
        if (p.prepaso_motivo & 4) motivos += " occlusion queries issued;";
        if (p.prepaso_motivo & 8) motivos += " GUseTilingCode set;";
        REXLOG_INFO("[xmow perf] {:.1f} s: engine depth prepass (xmow_sem_prepasso_motor {}): {} RenderPrePass calls, "
                    "{} view draw lists left out, {} calls run whole because:{}",
                    seconds, REXCVAR_GET(xmow_sem_prepasso_motor), p.prepaso_llamadas, p.prepaso_listas_omitidas,
                    p.prepaso_bloqueados, motivos.empty() ? std::string(" -") : motivos);
      }
      xmow::cpu33::Informe(seconds);  // 0.1.33: "[xmow cpu33]" lines
      {
        // 0.1.26 (item 1c): the draws sent to the renderer per frame by what they write and how they blend (ClaseDelDibujo).
        const double q = double(std::max<uint64_t>(quadros_intervalo, 1));
        uint64_t enviados = 0;
        for (const uint64_t n : censo_) enviados += n;
        REXLOG_INFO("[xmow census] {:.1f} s, {} frames, per frame: {:.0f} draws sent to the renderer: opaque {:.0f}, "
                    "additive {:.0f}, alpha blend {:.0f}, modulated {:.0f}, other blend {:.0f}, depth/stencil only "
                    "{:.0f}, inert (no color, depth or stencil write) {:.0f} ({:.0f} of them drawn by the renderer); "
                    "query boxes skipped before it {:.0f}; inert draws skipped before it {:.0f} (xmow_pular_inertes "
                    "{}); draws scissored to 1 pixel {:.0f}, draws emptied {:.0f} (xmow_diag_custo_dibujo {}, "
                    "xmow_diag_tijera_1px {}); depth prepass draws not sent {:.0f} (gow2_sin_prepaso_z {}, "
                    "gow2_sin_prepaso_enmascarado {})",
                    seconds, quadros_intervalo, double(enviados) / q, double(censo_[kClaseOpaco]) / q,
                    double(censo_[kClaseAditivo]) / q, double(censo_[kClaseAlfa]) / q,
                    double(censo_[kClaseModulado]) / q, double(censo_[kClaseOtraMezcla]) / q,
                    double(censo_[kClaseSoloProfundidad]) / q, double(censo_[kClaseInerte]) / q,
                    double(censo_inertes_dibujados_) / q, double(caixas_omitidas_ - reported_caixas_) / q,
                    double(inertes_omitidos_) / q, REXCVAR_GET(xmow_pular_inertes),
                    double(tijera_1px_dibujos_) / q, double(vacios_dibujos_) / q,
                    REXCVAR_GET(xmow_diag_custo_dibujo), REXCVAR_GET(xmow_diag_tijera_1px),
                    double(prepass_skipped_ - reported_prepass_skipped_) / q, REXCVAR_GET(gow2_sin_prepaso_z),
                    REXCVAR_GET(gow2_sin_prepaso_enmascarado));
        reported_prepass_skipped_ = prepass_skipped_;  // 0.1.31: counted since 0.1.x, printed now
        inertes_omitidos_ = 0;
        tijera_1px_dibujos_ = 0;
        vacios_dibujos_ = 0;
        censo_ = {};
        censo_inertes_dibujados_ = 0;
      }
      imload_rapidas_ = 0;
      reported_caixas_ = caixas_omitidas_;
      // xmow_oclusao_conservadora: what the history answered and what the GPU counted in this interval.
      if (REXCVAR_GET(xmow_oclusao_conservadora) || oc_.ocultas || oc_.visibles || !oc_historial_.empty()) {
        REXLOG_INFO("[xmow occlusion] {:.1f} s: conservative {} (retest every {} frames); queries answered {} hidden, {} "
                    "visible ({} with no box, {} with no key, {} with a draw that paints); GetData {} read from the "
                    "ring, {} answered visible at once (the ring was not there yet); query boxes {} drawn and counted, "
                    "{} skipped; GPU counts {} back ({} late, 2+ frames), {} discarded for an invalid depth (pending "
                    "clear or contenido_invalido), {} given up, {} still out; history {} keys, {} clears, {} prunes",
                    seconds, REXCVAR_GET(xmow_oclusao_conservadora) ? "on" : "off",
                    REXCVAR_GET(xmow_oclusao_reteste_quadros), oc_.ocultas, oc_.visibles, oc_.sin_caja, oc_.sin_clave,
                    oc_.pinta, p.consultas_anillo, p.consultas_tarde, oc_.dibujadas, oc_.saltadas, oc_.cuentas,
                    oc_.atrasadas, oc_.z_invalidas, oc_.perdidas, oc_pendientes_.size(), oc_historial_.size(),
                    oc_.limpiezas, oc_.podas);
      }
      const uint64_t zpd_fotograma = oc_.zpd_fotograma;
      oc_ = {};
      oc_.zpd_fotograma = zpd_fotograma;
      // xmow_log_hitch_ms: the slow frames of this interval, collected by HitchPorSwap (on this thread), written here.
      for (const std::string& linha : hitch_linhas_) REXLOG_INFO("{}", linha);
      if (hitch_suprimidas_) {
        REXLOG_INFO("[xmow hitch] {} more slow frames in this interval not listed (20 lines per 10 s)", hitch_suprimidas_);
      }
      hitch_linhas_.clear();
      hitch_suprimidas_ = 0;
    }
    // What the draws sent to the renderer target and draw (ColorRenderTargetFormat of each written color
    // target, DepthRenderTargetFormat when depth is on, primitive type): the formats it does not handle yet show
    // up here with how many draws they cost.
    std::string formats;
    for (uint32_t f = 0; f < 16; ++f) {
      if (color_formats_[f]) formats += fmt::format(" c{}:{}", f, color_formats_[f]);
    }
    for (uint32_t f = 0; f < 2; ++f) {
      if (depth_formats_[f]) formats += fmt::format(" z{}:{}", f, depth_formats_[f]);
    }
    std::string primitives;
    for (uint32_t t = 0; t < 32; ++t) {
      if (primitive_types_[t]) primitives += fmt::format(" {}:{}", t, primitive_types_[t]);
    }
    std::string orders;
    static constexpr const char* kOrder[4] = {"none", "8in16", "8in32", "16in32"};
    for (uint32_t k = 0; k < 8; ++k) {
      if (index_orders_[k]) orders += fmt::format(" {}-bit {}:{}", k < 4 ? 16 : 32, kOrder[k & 3], index_orders_[k]);
    }
    index_orders_ = {};
#if GOW2_DEKO3D
    const uint32_t syncs = rex::x360::g_sincronizaciones_anillo.load(std::memory_order_relaxed);
    const uint32_t syncs_interval = syncs - syncs_reported_;
    syncs_reported_ = syncs;
#else
    const uint32_t syncs_interval = 0;
#endif
    REXLOG_INFO("[gpu] draw targets:{}; primitive types:{}; indices:{}; {} emulated clears ({} limited to the game rectangle), {} multisampled draws drawn "
                "at 1x; {} guest-visible GPU progress points (renderer copies forgotten)", formats, primitives, orders,
                clears_, game_rect_clears_, msaa_draws_, syncs_interval);
    color_formats_ = {};
    depth_formats_ = {};
    primitive_types_ = {};
    uint64_t dibujados = 0, rechazados = 0;
    if (destinos_) {
      const rex::x360::EstadisticasDibujos e = destinos_->EstadisticasDeDibujos();
      dibujados = e.dibujados;
      rechazados = e.rechazados;
    }
#if defined(__SWITCH__)
    {
      // The process's memory (title takeover gives ~3.2 GB): Gears of War 2 ran out of it loading its first level.
      u64 used = 0, total = 0;
      svcGetInfo(&used, InfoType_UsedMemorySize, CUR_PROCESS_HANDLE, 0);
      svcGetInfo(&total, InfoType_TotalMemorySize, CUR_PROCESS_HANDLE, 0);
      REXLOG_INFO("[mem] process memory: {} MB used of {} MB", used >> 20, total >> 20);
    }
#endif
    gow2_log_main_thread();
    REXLOG_INFO("[gpu] draws: {} geometry, {} sent to the renderer: {} drawn, {} rejected by it; skipped here: "
                "{} without VS, {} without PS (the renderer logs each rejection cause once); {} left out by "
                "xmow_omitir_ps, {} height fog draws left out by xmow_niebla_altura",
                geometria_, enviados_, dibujados, rechazados, sin_vs_, sin_ps_, omitidos_ps_, omitidos_niebla_);
    {
      const gow2::shaders::Biblioteca::Lecturas lec = gow2::shaders::Global().lecturas();
      REXLOG_INFO("[shaders] SPIR-V reads (since boot): reader thread {} ({} of them preloaded for the map, {:.0f} ms), "
                  "ring or create hooks {:.0f} ms; {} draws skipped while their shader was being read",
                  lec.fondo, lec.precargadas, double(lec.ns_fondo) / 1e6, double(lec.ns_en_el_acto) / 1e6,
                  spirv_no_lista_);
    }
    if (destinos_) {
      uint64_t q[5];
      destinos_->EstadisticasOclusion(q);
      REXLOG_INFO("[gpu] occlusion: {} queries counted on the GPU, {} without room; {} answered with their count ({} "
                  "of them hidden, {:.1f} ms after their end on average, {} samples, max {}), {} answered visible when "
                  "the game waited for them, {} after waiting too long, {} waiting",
                  q[0], q[1], queries_answered_, queries_hidden_,
                  queries_answered_ ? double(queries_wait_us_) / 1000.0 / double(queries_answered_) : 0.0, q[3], q[4],
                  queries_not_waited_, queries_timed_out_, pending_queries_.size());
    }
    // Which unknown vertex shaders the skipped draws had, the most frequent first, with one draw's state each.
    std::vector<std::pair<uint64_t, uint64_t>> skips(unknown_vs_skips_.begin(), unknown_vs_skips_.end());
    std::sort(skips.begin(), skips.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
    for (size_t i = 0; i < skips.size() && i < 4; ++i) {
      REXLOG_INFO("[gpu] skipped without VS: {} draws with microcode {:016X}: {}", skips[i].second, skips[i].first,
                  unknown_vs_sample_[skips[i].first]);
    }
#endif
  }

  // --- PM4 packets ------------------------------------------------------------

  // Consumes a whole packet, or leaves the position untouched if it is not complete yet.
  bool Packet(Reader& reader, int depth) {
    const uint32_t packet = reader.Peek();
    uint32_t count = 0;
    switch (packet >> 30) {
      case 0: count = packet ? ((packet >> 16) & 0x3FFF) + 1 : 0; break;
      case 1: count = 2; break;
      case 2: count = 0; break;
      default: count = ((packet >> 16) & 0x3FFF) + 1; break;
    }
    if (reader.Pending() < count + 1) {
      return false;
    }
    reader.Skip(1);
    ++packets_;
    Reader data = reader;
    reader.Skip(count);
    if (!packet) {
      return true;
    }
    switch (packet >> 30) {
      case 0: {
        const uint32_t index = packet & 0x7FFF;
        const bool one_register = (packet >> 15) & 0x1;
        for (uint32_t i = 0; i < count; ++i) {
          WriteRegister(one_register ? index : index + i, data.Read());
        }
        break;
      }
      case 1: {
        const uint32_t value1 = data.Read();
        const uint32_t value2 = data.Read();
        WriteRegister(packet & 0x7FF, value1);
        WriteRegister((packet >> 11) & 0x7FF, value2);
        break;
      }
      case 2:
        break;
      default:
        PacketType3(data, packet, count, depth);
        break;
    }
    return true;
  }

  void PacketType3(Reader& data, uint32_t packet, uint32_t count, int depth) {
    const uint32_t opcode = (packet >> 8) & 0x7F;
    // The traced frame (ZL + ZR + Left, automatic dumps): every type 3 packet but the register writes, with its
    // predicate and the bins, to see where a frame's commands go (Gears of War 2's scene never reached a draw).
    if (draw_tracing_ && packet_trace_n_ < 6000 && opcode != xenos::PM4_SET_CONSTANT &&
        opcode != xenos::PM4_SET_SHADER_CONSTANTS) {
      ++packet_trace_n_;
      const uint32_t w0 = count > 0 ? data.Peek() : 0;
      REXLOG_DEBUG("[pm4] d{} op {:02X} n {} pred {} sel {:X} mask {:X} w0 {:08X}{}", depth, opcode, count,
                  packet & 0x1, bin_select_, bin_mask_, w0,
                  (packet & 0x1) && !(bin_select_ & bin_mask_) ? " SKIPPED" : "");
    }
    if ((packet & 0x1) && (!(bin_select_ & bin_mask_) || opcode == xenos::PM4_XE_SWAP)) {
#if GOW2_DEKO3D
      ++frame_.predicated_skipped;
#endif
      return;  // predicate fails; predicated Swaps are always dropped
    }
    switch (opcode) {
      case xenos::PM4_INTERRUPT: {
        if (count < 1) break;
        const uint32_t cpus = data.Read();
        GuestSeesGpuProgress();
        for (uint32_t cpu = 0; cpu < 6; ++cpu) {
          if (cpus & (1u << cpu)) {
            Interrupt(1, cpu);
          }
        }
        break;
      }
      case xenos::PM4_XE_SWAP:
#if GOW2_DEKO3D
        FrameTraceSwap();
        HitchPorSwap();
        query_aberta_ = false;  // a query left open (its end never seen) must not drop the next frame's draws
        composicao_final_logada_ = false;
        OclusionConservadoraSwap();  // xmow_oclusao_conservadora: history upkeep (before swaps_ moves on)
#endif
        // VdSwap writes "SWAP", the frontbuffer's physical address and its size. The renderer presents whatever is
        // resolved (clears and, once the ring decoder feeds draws, the frame). Handheld 1280x720.
        ++swaps_;
        counter_.fetch_add(1, std::memory_order_relaxed);
#if defined(__SWITCH__)
        if (swaps_ % 30 == 1) xmow::gpu_boost::Sondear();  // GPU boost: cvar / dock change re-check (xmow_gpu_boost.cpp)
#endif
        if (!depth_vs_frame_.empty()) {
          depth_vs_last_.swap(depth_vs_frame_);
          depth_vs_frame_.clear();
        }
        if (EnsureRenderer()) {
          // VdSwap describes the frontbuffer in fetch constant 0; an empty TexturaSwap finds nothing (black).
          {
            const uint64_t t = NsAnillo();  // xmow 1031
            destinos_->Presentar(presenter_.get(), rex::x360::LeerTexturaSwap(registers_.data()), 1280, 720);
            anillo_.presentar += NsAnillo() - t;
          }
          if (REXCVAR_GET(switch_omitir_pedido) && REXCVAR_GET(gow2_velocidad_prueba)) {
            REXCVAR_SET(switch_omitir_pedido, false);
            REXCVAR_SET(gow2_velocidad_sin_z, !REXCVAR_GET(gow2_velocidad_sin_z));
            REXLOG_INFO("[gpu] velocity draws {} (the next capture shows it)",
                        REXCVAR_GET(gow2_velocidad_sin_z) ? "WITHOUT depth test" : "with their depth test");
            tree_capture_after_ = 3;
          }
          if (REXCVAR_GET(switch_omitir_pedido) && REXCVAR_GET(gow2_arboles_prueba)) {
            REXCVAR_SET(switch_omitir_pedido, false);
            REXCVAR_SET(gow2_arboles, (REXCVAR_GET(gow2_arboles) + 1) % 5);
            static constexpr const char* kModos[5] = {"not drawn", "drawn as they are", "drawn with identity sway matrices",
                                                      "identity sway matrices and c10 = 0", "c10 = 0 only"};
            REXLOG_INFO("[gpu] trees mode {}: {} (the next capture shows it)", REXCVAR_GET(gow2_arboles),
                        kModos[REXCVAR_GET(gow2_arboles)]);
            tree_capture_after_ = 3;
          }
          if (tree_capture_after_ && --tree_capture_after_ == 0) {
            SaveCapture();
            REXLOG_INFO("[gpu] capture above: trees mode {}, velocity depth test {}", REXCVAR_GET(gow2_arboles),
                        REXCVAR_GET(gow2_velocidad_sin_z) ? "off" : "on");
          }
          if (REXCVAR_GET(switch_omitir_pedido)) {
            REXCVAR_SET(switch_omitir_pedido, false);
            skip_mode_ = ((skip_mode_ ? skip_mode_ : skip_resume_) + 1) % kSkipModes;
            skip_resume_ = 0;
            if (REXCVAR_GET(gow2_busqueda_directa) && skip_mode_ > 0 && skip_mode_ < kSkipModesFixed) {
              skip_mode_ = kSkipModesFixed;
            }
            if (skip_mode_ == kSkipModesFixed) {
              // The candidates: last frame's depth-writing vertex shaders, the most frequent first.
              std::vector<std::pair<uint32_t, uint32_t>> list(depth_vs_last_.begin(), depth_vs_last_.end());
              std::sort(list.begin(), list.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
              search_vs_.clear();
              std::string names;
              for (const auto& [vs, draws] : list) {
                if (search_vs_.size() == kSearchModes) break;
                search_vs_.push_back(vs);
                names += fmt::format(" n{} ({})", vs, draws);
              }
              search_ps_ = REXCVAR_GET(gow2_busqueda_ps);
              REXLOG_INFO("[gpu] search: {} {} shaders:{}", search_vs_.size(),
                          search_ps_ ? "base-pass pixel" : "depth-writing vertex", names);
            }
            if (skip_mode_ >= kSkipModesFixed && skip_mode_ - kSkipModesFixed >= search_vs_.size()) {
              skip_mode_ = 0;
            }
            SetNfsShortcuts(skip_mode_ != 1);
            REXCVAR_SET(nfsmw_dk_todas_las_salidas, skip_mode_ == 9);
            REXCVAR_SET(nfsmw_dk_gamma_lineal, skip_mode_ == 10);
            REXLOG_INFO("[gpu] skip mode {}: {} (the next capture shows it)", skip_mode_, SkipModeName());
            capture_after_skip_ = 3;  // a few frames later, when the change is on screen
          }
          if (capture_after_skip_ && --capture_after_skip_ == 0) {
            SaveCapture();
            REXLOG_INFO("[gpu] capture above: skip mode {} ({})", skip_mode_, SkipModeName());
            // Gears of War 2: the mode stays on screen until the next press (one frame was too short to see it).
          }
          // 0.1.34: PLAY MODE (xmow_bench.cpp) first: it decides whether stack sampling may be on.
          xmow::bench::AplicarModoJogo();
          if ((REXCVAR_GET(xmow_amostragem_pilas) && !xmow::bench::ModoJogoEfetivo()) != RexSwitchPerfStacksActive()) {
            RexSwitchPerfToggleStacks();
            REXLOG_INFO("[xmow perf] stack sampling {}", RexSwitchPerfStacksActive() ? "on" : "off");
          }
          // 0.1.26: the raw samples' writer and cap (switch_perf.cpp), handed over again when the cap changes.
          if (const int32_t mb = REXCVAR_GET(xmow_amostragem_volcado_mb); mb != volcado_pilhas_mb_) {
            volcado_pilhas_mb_ = mb;
            RexSwitchPerfFijarVolcadoCrudo(&EscritorPilhas, unsigned(std::max(mb, 0)));
            REXLOG_INFO("[xmow perf] raw stack samples to logs/rex/rex_pilhas.bin: cap {} MB per boot (while stack "
                        "sampling is on; the two busiest threads sampled every millisecond)", mb);
          }
          // 0.1.29 (xmow_l3r3_volcado_completo off, the default): L3 + R3 is a screen capture only (cap_NNN.bmp, as
          // switch_captura_pedida), no dump and no trace (test 35: a dump with the level streaming froze it 11 s).
          if (REXCVAR_GET(switch_volcado_pedido) && !REXCVAR_GET(xmow_l3r3_volcado_completo)) {
            REXCVAR_SET(switch_volcado_pedido, false);
            REXLOG_INFO("[xmow capture] L3 + R3: screen capture of the presented frame (xmow_l3r3_volcado_completo "
                        "off: no dump)");
            SaveCapture();
          }
          if (REXCVAR_GET(switch_volcado_pedido) &&
              rex::x360::EscriturasSdPendientes() > g_pilhas_pendentes.load(std::memory_order_acquire)) {
            // xmow 1031: a new L3 + R3 while the previous dump is still being written to the SD card is ignored (test
            // 32: presses 3 s apart doubled the card's load while the game streamed the helicopter area).
            REXCVAR_SET(switch_volcado_pedido, false);
            REXLOG_INFO("[xmow dump] L3 + R3 ignored: the previous dump is still being written ({} SD writes pending)",
                        rex::x360::EscriturasSdPendientes());
          }
          if (REXCVAR_GET(switch_volcado_pedido)) {
            REXCVAR_SET(switch_volcado_pedido, false);
            destinos_->PedirVolcado();
            // And every draw of that frame in the log, whatever its size (FrameTraceSwap ends it at the next Swap).
            draw_tracing_ = true;
            draw_trace_n_ = 0;
            draw_sum_on_ = true;  // 0.1.22: aggregate of every draw of the frame, logged when it ends
            draw_sum_.clear();
            draw_sum_total_ = 0;
            draw_trace_cap_ = 1200;  // XMOW: 3000 draws with their details took ~35 s of a menu frame on the SD
            packet_trace_n_ = 0;
            REXLOG_INFO("[draws] full trace of the next frame (ZL + ZR + Left)");
            captura_final_en_ = swaps_ + 1;  // the presented frame is saved at the Swap that ends the dumped one
          }
          AutomaticCollection();
          // L3 + R3: the final presented frame (capturas/cap_NNN.bmp), taken at the Swap that ends the dumped frame,
          // after its Presentar and outside any draw (the capture is written to the SD on the writer thread).
          if (captura_final_en_ && swaps_ >= captura_final_en_) {
            captura_final_en_ = 0;
            REXLOG_INFO("[xmow dump] presented final frame of the dumped one (PS 6971 inputs are in the [draws] trace)");
            SaveCapture();
          }
          if (REXCVAR_GET(switch_captura_pedida)) {
            REXCVAR_SET(switch_captura_pedida, false);
            SaveCapture();
          }
          // 0.1.34: the bench (advanced menu > TESTS): its steps, its windows and its captures of the presented frame.
          xmow::bench::PorSwap(draws_);
          if (xmow::bench::QuiereCaptura()) {
            rex::ui::RawImage imagen;
            std::vector<uint32_t> crudo;  // 0.1.34c: the RGB10A2 texels too, for the exact compare
#if GOW2_DEKO3D
            rex::ui::PedirCapturaCruda(&crudo);
#endif
            const bool ok = presenter_ && presenter_->CaptureGuestOutput(imagen);
#if GOW2_DEKO3D
            rex::ui::PedirCapturaCruda(nullptr);
#endif
            xmow::bench::Captura(ok ? &imagen : nullptr, ok ? &crudo : nullptr);
          }
        }
#if GOW2_DEKO3D
        ActualizarDiag();  // after FrameTraceSwap and AutomaticCollection: for the frame that starts here
#endif
        break;
      case xenos::PM4_INDIRECT_BUFFER:
      case xenos::PM4_INDIRECT_BUFFER_PFD: {
        if (count < 2) break;
        const uint32_t address = data.Read() & 0x1FFFFFFF;
        const uint32_t length = data.Read() & 0xFFFFF;
        if (depth < kMaxIndirectDepth) {
          Reader indirect;
          indirect.base = memory_->TranslatePhysical(address);
          indirect.end = length;
          while (indirect.Pending() && Packet(indirect, depth + 1)) {
          }
        }
        break;
      }
      case xenos::PM4_WAIT_REG_MEM: {
        if (count < 5) break;
        const uint32_t info = data.Read();
        const uint32_t poll = data.Read();
        const uint32_t reference = data.Read();
        const uint32_t mask = data.Read();
        const auto start = Clock::now();
        const auto limit = start + kWaitRegMemMax;
        const auto yield_until = start + std::chrono::milliseconds(1);
        bool waited = false;
        for (;;) {
          if (!(info & 0x10) && poll == rex::graphics::XE_GPU_REG_COHER_STATUS_HOST &&
              (Register(poll) & UINT32_C(0x80000000))) {
            registers_[poll] = 0;  // MakeCoherent: there is no GPU cache to flush
          }
          const uint32_t value = (info & 0x10) ? ReadMemory(poll) : Register(poll);
          if (Compare(info, value & mask, reference)) {
            break;
          }
          if (Clock::now() >= limit || !active_.load(std::memory_order_acquire)) {
            if (waits_cut_short_++ == 0) {
              REXLOG_WARN("[gpu] WAIT_REG_MEM not met ({} {:08X} ref {:08X} mask {:08X}), going on",
                          (info & 0x10) ? "memory" : "register", poll, reference, mask);
            }
            break;
          }
          if (!waited && lector_anillo_) {
            // xmow_anillo_rptr_rapido: what the ring has read so far goes back before it waits, or a guest waiting for
            // ring space (while the ring waits for that guest) would only see it after this wait is cut short.
            PublicarRptr(lector_anillo_->pos);
          }
          waited = true;
          // The first millisecond yielding instead of sleeping: a sleep on Horizon often lasts far longer than asked,
          // and the game's main thread waits on the GPU fences behind this wait (D3D BlockOnFence in its profile).
          if (Clock::now() < yield_until) {
            rex::thread::MaybeYield();
          } else {
            rex::thread::Sleep(std::chrono::microseconds(100));
          }
        }
        if (waited) {
          ++reg_mem_waits_;
          reg_mem_wait_ns_ += uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count());
        }
        break;
      }
      case xenos::PM4_REG_RMW: {
        if (count < 3) break;
        const uint32_t info = data.Read();
        const uint32_t and_value = data.Read();
        const uint32_t or_value = data.Read();
        uint32_t value = Register(info & 0x1FFF);
        value &= ((info >> 31) & 0x1) ? Register(and_value & 0x1FFF) : and_value;
        value |= ((info >> 30) & 0x1) ? Register(or_value & 0x1FFF) : or_value;
        WriteRegister(info & 0x1FFF, value);
        break;
      }
      case xenos::PM4_REG_TO_MEM: {
        if (count < 2) break;
        const uint32_t reg = data.Read();
        const uint32_t address = data.Read();
        WriteMemory(address, Register(reg));
        break;
      }
      case xenos::PM4_MEM_WRITE: {
        if (count < 1) break;
        uint32_t address = data.Read();
        for (uint32_t i = 1; i < count; ++i) {
          WriteMemory(address, data.Read());
          address += 4;
        }
        break;
      }
      case xenos::PM4_COND_WRITE: {
        if (count < 6) break;
        const uint32_t info = data.Read();
        const uint32_t poll = data.Read();
        const uint32_t reference = data.Read();
        const uint32_t mask = data.Read();
        const uint32_t target = data.Read();
        const uint32_t value_to_write = data.Read();
        const uint32_t value = (info & 0x10) ? ReadMemory(poll) : Register(poll);
        if (Compare(info, value & mask, reference)) {
          if (info & 0x100) {
            WriteMemory(target, value_to_write);
          } else {
            WriteRegister(target, value_to_write);
          }
        }
        break;
      }
      case xenos::PM4_EVENT_WRITE:
        if (count < 1) break;
        WriteRegister(rex::graphics::XE_GPU_REG_VGT_EVENT_INITIATOR, data.Read() & 0x3F);
        break;
      case xenos::PM4_EVENT_WRITE_SHD: {
        if (count < 3) break;
        const uint32_t initiator = data.Read();
        const uint32_t address = data.Read();
        const uint32_t value = data.Read();
        WriteRegister(rex::graphics::XE_GPU_REG_VGT_EVENT_INITIATOR, initiator & 0x3F);
        WriteMemory(address, ((initiator >> 31) & 0x1) ? counter_.load(std::memory_order_relaxed) : value);
        break;
      }
      case xenos::PM4_EVENT_WRITE_EXT: {
        if (count < 2) break;
        const uint32_t initiator = data.Read();
        const uint32_t address = data.Read();
        WriteRegister(rex::graphics::XE_GPU_REG_VGT_EVENT_INITIATOR, initiator & 0x3F);
        // Screen extent of the previous draw: the maximum, as the SDK does.
        const uint16_t extent[] = {0, kMaxExtent, 0, kMaxExtent, 0, 1};
        uint8_t* target = memory_->TranslatePhysical(address & ~uint32_t(0x3));
        for (size_t i = 0; i < std::size(extent); ++i) {
          rex::memory::store_and_swap<uint16_t>(target + i * 2, extent[i]);
        }
        break;
      }
      case xenos::PM4_EVENT_WRITE_ZPD: {
        if (count < 1) break;
        WriteRegister(rex::graphics::XE_GPU_REG_VGT_EVENT_INITIATOR, data.Read() & 0x3F);
        OcclusionQuery(Register(rex::graphics::XE_GPU_REG_RB_SAMPLE_COUNT_ADDR));
        break;
      }
      case xenos::PM4_SET_CONSTANT: {
        if (count < 1) break;
        const uint32_t type_index = data.Read();
        const uint32_t base = ConstantBase(type_index);
        if (base != UINT32_MAX) {
          const uint32_t index = base + (type_index & 0x7FF);
          for (uint32_t i = 1; i < count; ++i) {
            WriteRegister(index + i - 1, data.Read());
          }
        }
        break;
      }
      case xenos::PM4_SET_CONSTANT2:
      case xenos::PM4_SET_SHADER_CONSTANTS: {
        // First word: the register index; the rest is written consecutively.
        if (count < 1) break;
        const uint32_t index = data.Read() & 0xFFFF;
        for (uint32_t i = 1; i < count; ++i) {
          WriteRegister(index + i - 1, data.Read());
        }
        break;
      }
      case xenos::PM4_LOAD_ALU_CONSTANT: {
        // Registers loaded from guest memory instead of from the ring.
        if (count < 3) break;
        const uint32_t address = data.Read() & 0x3FFFFFFF;
        const uint32_t type_index = data.Read();
        const uint32_t n = data.Read() & 0xFFF;
        const uint32_t base = ConstantBase(type_index);
        if (base != UINT32_MAX) {
          const uint8_t* source = memory_->TranslatePhysical(address);
          const uint32_t index = base + (type_index & 0x7FF);
          for (uint32_t i = 0; i < n; ++i) {
            WriteRegister(index + i, rex::memory::load_and_swap<uint32_t>(source + size_t(i) * 4));
          }
        }
        break;
      }
      case xenos::PM4_VIZ_QUERY: {
        // When a query ends it is reported as visible, in case the game reads the result (as the SDK does).
        if (count < 1) break;
        const uint32_t word = data.Read();
        const uint32_t id = word & 0x3F;
        if (!(word & 0x100)) {
          WriteRegister(rex::graphics::XE_GPU_REG_VGT_EVENT_INITIATOR, xenos::VIZQUERY_START);
        } else {
          WriteRegister(rex::graphics::XE_GPU_REG_VGT_EVENT_INITIATOR, xenos::VIZQUERY_END);
          const uint32_t reg = id < 32 ? uint32_t(rex::graphics::XE_GPU_REG_PA_SC_VIZ_QUERY_STATUS_0)
                                       : uint32_t(rex::graphics::XE_GPU_REG_PA_SC_VIZ_QUERY_STATUS_1);
          registers_[reg] |= UINT32_C(1) << (id < 32 ? id : id - 32);
        }
        break;
      }
      case xenos::PM4_SET_BIN_MASK_LO:
        if (count < 1) break;
        bin_mask_ = (bin_mask_ & 0xFFFFFFFF00000000ull) | data.Read();
        break;
      case xenos::PM4_SET_BIN_MASK_HI:
        if (count < 1) break;
        bin_mask_ = (bin_mask_ & 0xFFFFFFFFull) | (uint64_t(data.Read()) << 32);
        break;
      case xenos::PM4_SET_BIN_SELECT_LO:
        if (count < 1) break;
        bin_select_ = (bin_select_ & 0xFFFFFFFF00000000ull) | data.Read();
        break;
      case xenos::PM4_SET_BIN_SELECT_HI:
        if (count < 1) break;
        bin_select_ = (bin_select_ & 0xFFFFFFFFull) | (uint64_t(data.Read()) << 32);
        break;
      case xenos::PM4_SET_BIN_MASK: {
        if (count < 2) break;
        const uint64_t high = data.Read();
        bin_mask_ = (high << 32) | data.Read();
        break;
      }
      case xenos::PM4_SET_BIN_SELECT: {
        if (count < 2) break;
        const uint64_t high = data.Read();
        bin_select_ = (high << 32) | data.Read();
        break;
      }
      case xenos::PM4_DRAW_INDX:
      case xenos::PM4_DRAW_INDX_2: {
        ++draws_;
#if GOW2_DEKO3D
        // DRAW_INDX starts with the visibility query word. Then the initiator and, for indices in memory (DMA),
        // their base and size, kept in their registers as the SDK's command processor does: the renderer reads
        // everything from the register mirror.
        const uint32_t skip = opcode == xenos::PM4_DRAW_INDX ? 1 : 0;
        if (count < skip + 1) break;
        if (skip) data.Read();
        const uint32_t initiator = data.Read();
        WriteRegister(rex::graphics::XE_GPU_REG_VGT_DRAW_INITIATOR, initiator);
        if (((initiator >> 6) & 0x3) == uint32_t(xenos::SourceSelect::kDMA)) {
          if (count < skip + 3) break;
          WriteRegister(rex::graphics::XE_GPU_REG_VGT_DMA_BASE, data.Read());
          WriteRegister(rex::graphics::XE_GPU_REG_VGT_DMA_SIZE, data.Read());
        }
        if (!EnsureRenderer()) break;
        // A copy-mode draw is a resolve and/or a clear.
        if (rex::x360::EsCopia(registers_.data())) {
          ResolveCatalog();
          FrameTraceCopy();
          if (draw_tracing_ && draw_trace_n_ < draw_trace_cap_) {
            namespace g = rex::graphics;
            const uint32_t control = Register(g::XE_GPU_REG_RB_COPY_CONTROL);
            REXLOG_DEBUG("[draws]   copy: source {} (c0 {:03X} z {:03X}) pitch {} clear color {} depth {} command {} -> "
                        "{:08X} pitch {} height {} format {}",
                        control & 0x7, Register(g::XE_GPU_REG_RB_COLOR_INFO) & 0xFFF,
                        Register(g::XE_GPU_REG_RB_DEPTH_INFO) & 0xFFF,
                        Register(g::XE_GPU_REG_RB_SURFACE_INFO) & 0x3FFF, (control >> 8) & 1, (control >> 9) & 1,
                        (control >> 20) & 3, Register(g::XE_GPU_REG_RB_COPY_DEST_BASE),
                        Register(g::XE_GPU_REG_RB_COPY_DEST_PITCH) & 0x3FFF,
                        (Register(g::XE_GPU_REG_RB_COPY_DEST_PITCH) >> 16) & 0x3FFF,
                        (Register(g::XE_GPU_REG_RB_COPY_DEST_INFO) >> 7) & 0x3F);
          }
          CopiarMedido(rex::x360::LeerRegistrosCopia(registers_.data()));
          break;
        }
        Draw();
#else
        (void)data;
#endif
        break;
      }
#if GOW2_DEKO3D
      case xenos::PM4_IM_LOAD: {
        // Microcode in GPU memory; the type (0 VS, 1 PS) in the two low bits of the address.
        if (count < 2) break;
        const uint32_t address_type = data.Read();
        const uint32_t words = data.Read() & 0xFFFF;
        const uint32_t type = address_type & 0x3;
        if (type > 1 || !words) break;
        const uint8_t* source = memory_->TranslatePhysical(address_type & ~uint32_t(0x3));
        microcode_.resize(words);
        for (uint32_t i = 0; i < words; ++i) {
          microcode_[i] = rex::memory::load_and_swap<uint32_t>(source + size_t(i) * 4);
        }
        ShaderLoaded(type, address_type & ~uint32_t(0x3));
        if (type == 0) vs_clave_ = 0;  // the VS state is no longer an IM_LOAD_IMMEDIATE's (CargaInmediataCacheada)
        break;
      }
      case xenos::PM4_IM_LOAD_IMMEDIATE: {
        // Microcode inside the packet (D3D's copy path, patched there).
        if (count < 2) break;
        const uint32_t type = data.Read() & 0x3;
        const uint32_t words = data.Read() & 0xFFFF;
        if (type > 1 || !words || words > count - 2) break;
        // xmow_imload_rapido: ~3200 of these a frame in test 22, nearly all repeats.
        const uint64_t clave = REXCVAR_GET(xmow_imload_rapido) ? ClaveInmediata(data, words) : 0;
        if (clave && CargaInmediataCacheada(type, clave, data, words)) break;
        microcode_.resize(words);
        for (uint32_t i = 0; i < words; ++i) {
          microcode_[i] = data.Read();
        }
        ShaderLoaded(type, 0);
        GuardarCargaInmediata(type, clave);
        break;
      }
#endif
      default:
        // IM_LOAD, IM_LOAD_IMMEDIATE, INVALIDATE_STATE, NOP...: nothing the game waits for.
        break;
    }
  }

#if GOW2_DEKO3D
  // PA_SC_WINDOW_OFFSET..SCISSOR_BR, PA_CL_VPORT_*, PA_CL_CLIP_CNTL, PA_SU_SC_MODE_CNTL, PA_CL_VTE_CNTL and
  // PA_SU_VTX_CNTL: the viewport/scissor state the renderer caches (as the SDK's command processor).
  static bool EsRegistroDeEncuadre(uint32_t index) {
    if (index - 0x2080 > 0x2302 - 0x2080) return false;
    return index <= 0x2082 || (index >= 0x210F && index <= 0x2114) || (index >= 0x2204 && index <= 0x2206) ||
           index == 0x2302;
  }

  // --- Draw-skipping modes (ZL + ZR + Right), to find which kind of draw makes an artifact: each press goes to the next
  // mode, logs it and takes a capture a few frames later. Classified by the blend of target 0 (RB_BLENDCONTROL0) and the
  // primitive, as the frame traces show Dead Space 2's draws.
  static constexpr uint32_t kSkipModesFixed = 11;
  // Then the search modes: kSkipModesFixed + i skips only the i-th vertex shader of the scene's depth-writing draws
  // (the most frequent first, as counted in the frame before the first search mode), to find which one draws the
  // giant triangles of level 2. After the last one, back to normal rendering.
  static constexpr uint32_t kSearchModes = 128;
  static constexpr uint32_t kSkipModes = kSkipModesFixed + kSearchModes;
  static constexpr const char* kSkipModeNames[kSkipModesFixed] = {
      "none (normal rendering)",
      "SAFE MODE: the renderer's NFS shortcuts off, nothing skipped",
      "modulated shadows and other multiplying draws (source factor DST_COLOR, dest ZERO)",
      "lights (draws into the base target with gow2_formato_luces, if set) and the multiplying draws",
      "everything after the base pass (draws not into the gow2_edram_base_escena / gow2_formato_escena target with blend 00010001)",
      "additive effects (SRC_ALPHA+ONE 00010106, ONE+ONE 01010101)",
      "everything blended (blend other than 00010001)",
      "alpha-tested (RB_COLORCONTROL alpha test on)",
      "no depth prepass (gow2_sin_prepaso_z; here regardless of it)",
      "vertex shaders with every interpolator (nfsmw_dk_todas_las_salidas)",
      "gamma textures read as linear (nfsmw_dk_gamma_lineal; textures loaded from now on)",
  };
  // The renderer's NFS shortcuts (see the declarations at the top): on as the SDK defaults, off in safe mode.
  static void SetNfsShortcuts(bool on) {
    REXCVAR_SET(nfsmw_nativo_saltar_invisibles, on);
    REXCVAR_SET(nfsmw_nativo_sin_ps_sin_color, on);
    REXCVAR_SET(nfsmw_nativo_sin_desenfoque, on);
    REXCVAR_SET(nfsmw_nativo_pcf_barato, on);
    REXCVAR_SET(nfsmw_nativo_sombra_minimo, on);
    REXCVAR_SET(nfsmw_nativo_z_temprana, on);
    REXCVAR_SET(nfsmw_nativo_cielo_aplazado, on);
    REXCVAR_SET(nfsmw_dk_constantes_por_diferencias, on);
    REXCVAR_SET(nfsmw_nativo_saltar_borrados_repetidos, on);
    REXCVAR_SET(nfsmw_nativo_resolver_sin_copia, on);
    REXCVAR_SET(nfsmw_nativo_intercambiar_sin_borrado, on);
    REXCVAR_SET(nfsmw_nativo_frontal_perezoso, on);
    REXCVAR_SET(nfsmw_dk_borrar_area_util, on);
    REXCVAR_SET(nfsmw_dk_borrado_color_aplazado, on);
    REXCVAR_SET(nfsmw_dk_borrado_z_aplazado, on);
  }

  std::string SkipModeName() const {
    if (skip_mode_ < kSkipModesFixed) return kSkipModeNames[skip_mode_];
    return fmt::format("search {} of {}: {} shader n{} not drawn", skip_mode_ - kSkipModesFixed + 1,
                       search_vs_.size(), search_ps_ ? "pixel" : "vertex", search_vs_[skip_mode_ - kSkipModesFixed]);
  }

  // Whether the draw's color 0 is the scene's base pass (gow2_edram_base_escena / gow2_formato_escena; -1 = any).
  bool IsBasePassTarget() const {
    namespace g = rex::graphics;
    const uint32_t info = Register(g::XE_GPU_REG_RB_COLOR_INFO);
    const int32_t base = REXCVAR_GET(gow2_edram_base_escena), format = REXCVAR_GET(gow2_formato_escena);
    return (base < 0 || (info & 0xFFF) == uint32_t(base)) && (format < 0 || ((info >> 16) & 0xF) == uint32_t(format));
  }
  // xmow_ab_ps, read once: the pixel shader numbers of the "_ps" capture and of the detailed log lines.
  bool EnListaAb(uint32_t numero) {
    if (!ab_ps_leidos_) {
      ab_ps_leidos_ = true;
      LeerListaNumeros(REXCVAR_GET(xmow_ab_ps), ab_ps_);
      REXLOG_INFO("[gpu] xmow_ab_ps: {} pixel shaders", ab_ps_.size());
    }
    return std::find(ab_ps_.begin(), ab_ps_.end(), numero) != ab_ps_.end();
  }
  // xmow_omitir_ps: pixel shaders that are never drawn. Read at the first draw and again whenever the cvar changes
  // (the port menu's HEIGHT FOG row and PORT FIXES / SKIPPED SHADERS change it live, from the input thread): a change
  // callback bumps a counter, and the list is re-read through the locked registry (GetFlagByName), never with an
  // unlocked REXCVAR_GET of the std::string while the menu may be writing it.
  bool EnListaOmitir(uint32_t numero) {
    static std::atomic<uint32_t> cambios{0};
    if (!omitir_ps_leidos_) {
      rex::cvar::RegisterChangeCallback("xmow_omitir_ps", [](std::string_view, std::string_view) {
        cambios.fetch_add(1, std::memory_order_relaxed);
      });
    }
    const uint32_t version = cambios.load(std::memory_order_relaxed);
    if (!omitir_ps_leidos_ || version != omitir_ps_version_) {
      omitir_ps_leidos_ = true;
      omitir_ps_version_ = version;
      omitir_ps_.clear();
      LeerListaNumeros(rex::cvar::GetFlagByName("xmow_omitir_ps"), omitir_ps_);
      // A bitset by shader number for the per-draw check (numbers above 1 << 20 are not library numbers: ignored).
      omitir_ps_bits_.clear();
      for (const uint32_t n : omitir_ps_) {
        if (n >= (1u << 20)) continue;
        if (omitir_ps_bits_.size() <= n / 64) omitir_ps_bits_.resize(n / 64 + 1, 0);
        omitir_ps_bits_[n / 64] |= uint64_t(1) << (n % 64);
      }
      REXLOG_INFO("[gpu] xmow_omitir_ps: {} pixel shaders", omitir_ps_.size());
    }
    return numero / 64 < omitir_ps_bits_.size() && ((omitir_ps_bits_[numero / 64] >> (numero % 64)) & 1);
  }
  // xmow_niebla_altura (quick menu HEIGHT FOG): whether this draw of the full-screen height fog (PS n2639, UE3's
  // HeightFogPixelShader: rgb = (1 - T) * FogInScattering * 8, alpha = T, blended ONE x SRC_ALPHA into the 7e3 scene)
  // is left out. 1 = always drawn, 2 = never, 0 = AUTO: drawn everywhere, the frontend too (0.1.20: the 360's main menu
// is the blue water look of this fog); the frontend detection below is kept for the log only.
  // The frontend is told by the fog's own pixel shader constants, read at this draw (free: they are in registers_):
  // the main menu's fog (WStart, tests 12-22) is c3.x = FogDistanceScale -0.0218, c4.x = FogExtinctionDistance 500,
  // c6.x = FogStartDistance 0 and c7.x = 0 (the transmittance beyond the extinction distance: fully fogged); every
  // gameplay fog logged is about 1500 times thinner (c3.x -1.44e-05 / -7.2e-05), with c4.x 50000, c6.x 1000 and
  // c7.x 1. With the camera inside the fog band (VS c7 - c4.z = 307 units over it), the menu's fog covers ~70% of the
  // pixels with its blue (simulated on the depth of test 25's dump) and the frames with and without a usable depth
  // alternate: the flash. The map log ([XMOW mapa], SDK) only names a package the first time it is opened, so it
  // cannot tell a return to the main menu.
  static constexpr uint32_t kPsNieblaAltura = 2639;
  float ConstantePs(uint32_t c, uint32_t componente) const {
    return std::bit_cast<float>(registers_[0x4000 + (256 + c) * 4 + componente]);
  }
  // 0.1.32: 2 (NO) leaves the fog out only on gameplay maps: drawn while the fog's constants are the frontend's or the
  // last map package opened is not a gameplay one (before the first map, WStart.xxx), so the title and menus keep
  // their blue water look (test 38: NO everywhere ruined the main menu).
  bool NieblaAlturaOmitida() {
    const int32_t modo = REXCVAR_GET(xmow_niebla_altura);
    // 0.1.33d (review S-B): classified at every fog draw, with any xmow_niebla_altura (1 used to return here, so a
    // return to the main menu was never seen with YES).
    const float densidad = ConstantePs(3, 0), extincion = ConstantePs(4, 0), inicio = ConstantePs(6, 0),
                mas_alla = ConstantePs(7, 0);
    // Thresholds far from both sets of values: a fog 70 times denser than any gameplay one, fully opaque beyond an
    // extinction distance under 2000 units.
    const bool frontal = densidad < -0.001f && extincion > 0.0f && extincion < 2000.0f && mas_alla == 0.0f;
    rex::x360::g_xmow_frontend.store(frontal ? 1 : 2, std::memory_order_relaxed);  // xmow 1031 ([xmow composite])
    // 0.1.33c (review S2) / 0.1.33d (S-B): WStart.xxx is not known to be opened again on a return to the main menu
    // (the package may stay loaded); the main menu's fog constants say it. Only counted here: the game thread puts the
    // per map settings back to the frontend's at the start of a frame, after enough frontend fog frames in a row.
    XmowNieblaClasificada(frontal);
    if (frontal != niebla_frontal_) {
      niebla_frontal_ = frontal;
      if (frontal) {
        REXLOG_INFO("[xmow fog] frontend detected (height fog drawn, xmow_niebla_altura {})", modo);
      } else {
        REXLOG_INFO("[xmow fog] frontend left (height fog {}, xmow_niebla_altura {})",
                    modo == 2 && XmowMapaDeJogo() ? "left out: gameplay map" : "drawn", modo);
      }
      REXLOG_INFO("[xmow fog] fog constants: distance scale {:g}, extinction distance {:g}, start distance {:g}, "
                  "transmittance beyond extinction {:g}",
                  densidad, extincion, inicio, mas_alla);
    }
    // YES (1) and AUTO (0) draw the fog everywhere; NO (2) leaves it out on gameplay maps only (0.1.32).
    return modo == 2 && !frontal && XmowMapaDeJogo();
  }
  // xmow_pular_caixas_consulta: the draw about to be sent writes nothing: no color channel (depth only mode 5, or
  // RB_COLOR_MASK 0 for every target), no depth (RB_DEPTHCONTROL bit 2, Z write enable) and no stencil (stencil test
  // enabled, bit 0, with a non-zero write mask, RB_STENCILREFMASK bits 16-23: kept to be safe, as it may be a mask).
  // 0.1.33c (review S5): stencil written by the front faces (RB_STENCILREFMASK write mask, bits 16-23) or, with two
  // sided stencil (RB_DEPTHCONTROL bit 7, backface enable), by the back faces (RB_STENCILREFMASK_BF).
  bool EscreveEstencil(uint32_t profundidade) const {
    namespace g = rex::graphics;
    if ((profundidade & 0x1) == 0) return false;
    if (((Register(g::XE_GPU_REG_RB_STENCILREFMASK) >> 16) & 0xFF) != 0) return true;
    return (profundidade & 0x80) != 0 && ((Register(g::XE_GPU_REG_RB_STENCILREFMASK_BF) >> 16) & 0xFF) != 0;
  }
  bool CaixaDeConsulta() const {
    namespace g = rex::graphics;
    const uint32_t modo = Register(g::XE_GPU_REG_RB_MODECONTROL) & 0x7;
    const uint32_t profundidade = Register(g::XE_GPU_REG_RB_DEPTHCONTROL);
    const bool sem_cor = modo == 5 || (Register(g::XE_GPU_REG_RB_COLOR_MASK) & 0xFFFF) == 0;
    const bool escreve_z = (profundidade & 0x4) != 0;
    return sem_cor && !escreve_z && !EscreveEstencil(profundidade);
  }
  // 0.1.26 (pesquisa-fps-picos.md item 1c): what a draw sent to the renderer writes and how it blends into color target 0
  // (RB_BLENDCONTROL0: source factor bits 0-4, destination bits 8-12; Xenos factors 0 ZERO, 1 ONE, 4 SRC_COLOR,
  // 6 SRC_ALPHA, 7 ONE_MINUS_SRC_ALPHA, 8 DST_COLOR). Inert = no color channel, no depth write and no stencil write (the
  // renut skip_inert_draws idea; memexport is not checked: the shader entries do not record it). The same test as
  // CaixaDeConsulta, outside a query.
  enum ClaseDibujo : uint8_t {
    kClaseOpaco, kClaseAditivo, kClaseAlfa, kClaseModulado, kClaseOtraMezcla, kClaseSoloProfundidad, kClaseInerte,
    kClases
  };
  static constexpr const char* kNombresClase[kClases] = {"opaque", "additive", "alpha", "modulated", "other blend",
                                                         "depth/stencil only", "inert"};
  uint8_t ClaseDelDibujo() const {
    namespace g = rex::graphics;
    const uint32_t modo = Register(g::XE_GPU_REG_RB_MODECONTROL) & 0x7;
    const uint32_t profundidade = Register(g::XE_GPU_REG_RB_DEPTHCONTROL);
    const bool cor = modo != 5 && (Register(g::XE_GPU_REG_RB_COLOR_MASK) & 0xFFFF) != 0;
    if (!cor) {
      const bool escreve_z = (profundidade & 0x4) != 0;
      return escreve_z || EscreveEstencil(profundidade) ? kClaseSoloProfundidad : kClaseInerte;
    }
    const uint32_t mezcla = Register(g::XE_GPU_REG_RB_BLENDCONTROL0);
    const uint32_t src = mezcla & 0x1F, dst = (mezcla >> 8) & 0x1F;
    if (src == 1 && dst == 0) return kClaseOpaco;
    if (dst == 1) return kClaseAditivo;  // ONE x ONE, SRC_ALPHA x ONE: per-light passes, glows
    if (src == 6 && dst == 7) return kClaseAlfa;
    if ((src == 8 && dst == 0) || (src == 0 && dst == 4)) return kClaseModulado;  // DST_COLOR x ZERO, ZERO x SRC_COLOR
    return kClaseOtraMezcla;
  }
  static void LeerListaNumeros(const std::string& lista, std::vector<uint32_t>& salida) {
    uint32_t v = 0;
    bool hay = false;
    for (const char c : lista + ",") {
      if (c >= '0' && c <= '9') {
        v = v * 10 + uint32_t(c - '0');
        hay = true;
      } else if (hay) {
        salida.push_back(v);
        v = 0;
        hay = false;
      }
    }
  }

  // xmow_captura_ab: whether this draw belongs to the group left out for the next capture. Only color draws (mode 4)
  // with some channel written; 1, 2 and 4 only into a 7e3 target (formats 3 and 12, the scene). Group 4 = additive
  // translucents (ONE x ONE). The rotation now alternates 5 (translucent alpha blends: SRC_ALPHA x ONE_MINUS_SRC_ALPHA,
  // suffix "ta") and 6 (the other translucents, suffix "to").
  bool OmitidoAb() {
    namespace g = rex::graphics;
    if ((Register(g::XE_GPU_REG_RB_MODECONTROL) & 0x7) != 4) return false;
    if (ab_grupo_ == 3) return ps_ && ps_->numero == ab_ps_actual_;
    const uint32_t formato = (Register(g::XE_GPU_REG_RB_COLOR_INFO) >> 16) & 0xF;
    if (!(Register(g::XE_GPU_REG_RB_COLOR_MASK) & 0xF) || (formato != 3 && formato != 12)) return false;
    const uint32_t mezcla = Register(g::XE_GPU_REG_RB_BLENDCONTROL0);
    const uint32_t src = mezcla & 0x1F, dst = (mezcla >> 8) & 0x1F;
    const bool modulada = src == 8 && dst == 0;   // DST_COLOR x ZERO: the scene times the shadow
    const bool opaca = src == 1 && dst == 0;      // ONE x ZERO: no blending
    if (ab_grupo_ == 2) return modulada;
    if (ab_grupo_ == 4) return src == 1 && dst == 1;
    const bool alfa = src == 6 && dst == 7;       // SRC_ALPHA x ONE_MINUS_SRC_ALPHA: classic alpha blend
    if (ab_grupo_ == 5) return !modulada && !opaca && alfa;
    if (ab_grupo_ == 6) return !modulada && !opaca && !alfa;
    return !modulada && !opaca;
  }

  bool IsBasePassTargetBase() const {
    const int32_t base = REXCVAR_GET(gow2_edram_base_escena);
    return base < 0 || (Register(rex::graphics::XE_GPU_REG_RB_COLOR_INFO) & 0xFFF) == uint32_t(base);
  }

  bool SkippedByMode() const {
    namespace g = rex::graphics;
    if (skip_mode_ >= kSkipModesFixed) {
      const auto* shader = search_ps_ ? ps_ : vs_;
      return shader && shader->numero == search_vs_[skip_mode_ - kSkipModesFixed];
    }
    const uint32_t blend = Register(g::XE_GPU_REG_RB_BLENDCONTROL0);
    const uint32_t mask = Register(g::XE_GPU_REG_RB_COLOR_MASK);
    const uint32_t prim = Register(g::XE_GPU_REG_VGT_DRAW_INITIATOR) & 0x3F;
    const uint32_t colorctl = Register(g::XE_GPU_REG_RB_COLORCONTROL);
    const uint32_t color0_format = (Register(g::XE_GPU_REG_RB_COLOR_INFO) >> 16) & 0xF;
    // Color = source * destination: Gears of War 2's modulated shadows (01000008) multiply the scene by the shadow.
    const bool multiplying = (mask & 0xF) && (blend & 0x1F) == 8 && ((blend >> 8) & 0x1F) == 0;
    (void)prim;
    switch (skip_mode_ - 1) {  // mode 1 is the safe mode, which skips nothing
      case 1: return multiplying;
      case 2: return multiplying || (REXCVAR_GET(gow2_formato_luces) >= 0 && IsBasePassTargetBase() &&
                                     color0_format == uint32_t(REXCVAR_GET(gow2_formato_luces)));
      case 3: return (mask & 0xF) && !(IsBasePassTarget() && blend == 0x00010001);
      case 4: return (mask & 0xF) && (blend == 0x00010106 || blend == 0x01010101);
      case 5: return (mask & 0xF) && blend != 0x00010001;
      case 6: return ((colorctl >> 3) & 0x1) && (colorctl & 0x7) != 7;
      case 7:
        return (Register(g::XE_GPU_REG_RB_MODECONTROL) & 0x7) == 5 &&
               (Register(g::XE_GPU_REG_RB_DEPTH_INFO) & 0xFFF) == 0 && (Register(g::XE_GPU_REG_RB_DEPTHCONTROL) & 0x4);
      default: return false;
    }
  }

  // Level 2's giant triangles (found with the search modes): this vertex shader picks its matrices from the float
  // constants by an index it computes from a per-vertex value (fetch r1, 32_32_32_32_FLOAT at offset 32 of stream 31.1:
  // a0 = ((r1.x + c10.x) mod 3) * 4, then c11..c14 + a0). What it is fed, for the first draws.
  static constexpr uint64_t kBrokenSkinVs = UINT64_C(0x38234053372F8F2F);
  void LogBrokenSkinDraw() {
    if (broken_vs_logged_ >= 6) return;
    ++broken_vs_logged_;
    namespace g = rex::graphics;
    const uint32_t v = 0x4800 + 31 * 6;
    std::string text = fmt::format("[gpu] skin VS n{} draw: fetch 31 {:08X} {:08X} | {:08X} {:08X} | {:08X} {:08X}; "
                                   "index offset {} prim {:08X}; constants",
                                   vs_->numero, Register(v), Register(v + 1), Register(v + 2), Register(v + 3),
                                   Register(v + 4), Register(v + 5), Register(g::XE_GPU_REG_VGT_INDX_OFFSET),
                                   Register(g::XE_GPU_REG_VGT_DRAW_INITIATOR));
    for (uint32_t c = 10; c <= 26; ++c) {
      const uint32_t r = 0x4000 + c * 4;
      text += fmt::format(" c{}=({:g} {:g} {:g} {:g})", c, std::bit_cast<float>(Register(r)),
                          std::bit_cast<float>(Register(r + 1)), std::bit_cast<float>(Register(r + 2)),
                          std::bit_cast<float>(Register(r + 3)));
    }
    // Stream 31.1's first vertices, bytes 32-47 (the index and the two weights), as stored (big-endian floats).
    const uint32_t base = Register(v + 2) & ~0x3u;
    if (const uint8_t* m = base ? memory_->TranslatePhysical(base) : nullptr) {
      text += "; stream 31.1 at +32:";
      for (uint32_t i = 0; i < 6; ++i) {
        text += " (";
        for (uint32_t k = 0; k < 4; ++k) {
          text += fmt::format("{}{:g}", k ? " " : "",
                              std::bit_cast<float>(rex::memory::load_and_swap<uint32_t>(m + i * 64 + 32 + k * 4)));
        }
        text += ")";
      }
    }
    REXLOG_INFO("{}", text);
    // The first vertices the index buffer names: position (stream 31.2, float3) and the index/weights (31.1 +32), with
    // c0-c9 and c23-c26, to redo the shader's math on the PC.
    {
      const uint32_t init = Register(g::XE_GPU_REG_VGT_DRAW_INITIATOR);
      const uint32_t dma = Register(g::XE_GPU_REG_VGT_DMA_BASE);
      const bool idx32 = (init >> 11) & 1;
      const uint32_t pos_base = Register(v + 4) & ~0x3u, w_base = Register(v + 2) & ~0x3u;
      const uint8_t* ib = dma ? memory_->TranslatePhysical(dma & ~0x3u) : nullptr;
      const uint8_t* pb = pos_base ? memory_->TranslatePhysical(pos_base) : nullptr;
      const uint8_t* wb = w_base ? memory_->TranslatePhysical(w_base) : nullptr;
      auto f32 = [](const uint8_t* m) { return std::bit_cast<float>(rex::memory::load_and_swap<uint32_t>(m)); };
      std::string verts;
      for (uint32_t k = 0; ib && pb && wb && k < 6; ++k) {
        const uint32_t i = idx32 ? rex::memory::load_and_swap<uint32_t>(ib + k * 4)
                                 : rex::memory::load_and_swap<uint16_t>(ib + k * 2);
        verts += fmt::format(" [i{} p({:g} {:g} {:g}) w({:g} {:g} {:g} {:g})]", i, f32(pb + i * 12), f32(pb + i * 12 + 4),
                             f32(pb + i * 12 + 8), f32(wb + i * 64 + 32), f32(wb + i * 64 + 36), f32(wb + i * 64 + 40),
                             f32(wb + i * 64 + 44));
      }
      std::string cs;
      for (uint32_t c : {0u, 1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u, 9u, 27u, 28u, 29u, 30u}) {
        const uint32_t r = 0x4000 + c * 4;
        cs += fmt::format(" c{}=({:g} {:g} {:g} {:g})", c, std::bit_cast<float>(Register(r)),
                          std::bit_cast<float>(Register(r + 1)), std::bit_cast<float>(Register(r + 2)),
                          std::bit_cast<float>(Register(r + 3)));
      }
      REXLOG_INFO("[gpu] skin VS n{} vertices (index base {:08X}, {}-bit, endian {}):{};{}", vs_->numero, dma,
                  idx32 ? 32 : 16, Register(g::XE_GPU_REG_VGT_DMA_SIZE) >> 30, verts, cs);
    }
  }

  // The lamp glow (gVFGlowInfo) and corona (modulate_by_corona_info_vs) vertex shaders: both read the lamp's
  // visibility from a texture in the vertex shader (tfetch2DVertice) and build their quads from it. Suspects for the
  // streaks next to flickering lamps; sin_resplandor.flag next to the NRO leaves them out to tell.
  static bool IsGlowShader(const rex::x360::EntradaShader* entry) {
    return entry && REXCVAR_GET(gow2_ajustes_gears2) && (entry->huella_contenedor == UINT64_C(0x36929751F8A50138) ||
                     entry->huella_contenedor == UINT64_C(0xC94E83E0DB28EA74));
  }

  // Once per patched variant: the vertex fetches D3D wrote for the declaration (format, stride, offset), which the
  // container does not have.
  void LogGlowFetches(const rex::x360::EntradaShader* entry) {
    const uint64_t key = XXH3_64bits(microcode_.data(), microcode_.size() * sizeof(uint32_t));
    if (glow_logged_.size() >= 16 || !glow_logged_.insert(key).second) return;
    std::string fetches;
    for (size_t i = 0; i + 2 < microcode_.size(); i += 3) {
      const uint32_t w0 = microcode_[i], w1 = microcode_[i + 1], w2 = microcode_[i + 2];
      if ((w0 & 0x1F) != 0 || !((w0 >> 19) & 1)) continue;  // vertex fetch: opcode 0, must-be-one bit
      fetches += fmt::format(" [slot {} r{} const {}.{} format {} signed {} int {} stride {} offset {} swz {:03X}]",
                             i / 3, (w0 >> 12) & 0x3F, (w0 >> 20) & 0x1F, (w0 >> 25) & 3, (w1 >> 16) & 0x3F,
                             (w1 >> 14) & 1, (w1 >> 13) & 1, w2 & 0xFF, (w2 >> 8) & 0x7FFFFF, w1 & 0xFFF);
    }
    REXLOG_INFO("[gpu] glow VS {:016X} (n{}): {} words, fetches:{}", entry->huella_contenedor, entry->numero,
                microcode_.size(), fetches);
  }

  // An IM_LOAD / IM_LOAD_IMMEDIATE of type 0 (VS) or 1 (PS), its microcode in microcode_ (host order).
  void ShaderLoaded(uint32_t type, uint32_t physical) {
    const rex::x360::EntradaShader* entry = gow2::shaders::Global().Identificar(type == 0, physical, microcode_);
    if (type == 1) {
      ps_ = entry;
      return;
    }
    // The same vertex shader loaded again from the same address (D3D reloads it for every draw that sets it): its
    // patched microcode and generation are the ones already here. Checked by length and three words, as the library
    // does, instead of copying and hashing it again.
    const size_t n = microcode_.size();
    if (physical && entry && entry == vs_ && physical == vs_physical_ && n && n == vs_microcode_.size() &&
        microcode_[0] == vs_microcode_[0] && microcode_[n / 2] == vs_microcode_[n / 2] &&
        microcode_[n - 1] == vs_microcode_[n - 1]) {
      return;
    }
    vs_physical_ = physical;
    vs_ = entry;
    vs_microcode_ = microcode_;  // patched by D3D: the vertex input is read from here
    if (IsGlowShader(entry)) LogGlowFetches(entry);
    // The same microcode keeps its generation, so the renderer's per-generation caches stay valid.
    const uint64_t key = XXH3_64bits(vs_microcode_.data(), vs_microcode_.size() * sizeof(uint32_t)) ^
                         vs_microcode_.size();
    auto [it, inserted] = vs_generations_.try_emplace(key, vs_generations_.size() + 1);
    generacion_vs_ = it->second;
  }

  // xmow_imload_rapido. An IM_LOAD_IMMEDIATE's key: the hash of its raw (guest order) words and their count, or 0 when
  // they wrap around the end of the ring (then the full path). Without a physical address the library's answer
  // depends only on the microcode (and the same microcode keeps its VS generation), so a load seen before reuses the
  // answer: no byte swap into microcode_, no library lock, no identification, no generation lookup. Only identified
  // shaders are kept (the unknown ones still go through the library, which counts and dumps them), and not the Gears
  // glow shaders (their load logs their fetches).
  static uint64_t ClaveInmediata(const Reader& data, uint32_t words) {
    if (data.mask && data.pos + words > data.mask + 1) return 0;
    const uint64_t h = XXH3_64bits(data.base + size_t(data.pos) * 4, size_t(words) * 4) ^ (uint64_t(words) << 40);
    return h ? h : 1;
  }

  bool CargaInmediataCacheada(uint32_t type, uint64_t clave, const Reader& data, uint32_t words) {
    auto& cache = cargas_inmediatas_[type & 1];
    const auto it = cache.find(clave);
    if (it == cache.end()) return false;
    ++imload_rapidas_;
    if (type == 1) {
      ps_ = it->second.entrada;
      return true;
    }
    if (clave == vs_clave_) return true;  // the VS state (vs_, vs_microcode_, generation) is this load's already
    vs_ = it->second.entrada;
    vs_physical_ = 0;
    vs_microcode_.resize(words);  // patched by D3D: the vertex input is read from here (ShaderLoaded)
    Reader r = data;
    for (uint32_t i = 0; i < words; ++i) vs_microcode_[i] = r.Read();
    generacion_vs_ = it->second.generacion;
    vs_clave_ = clave;
    return true;
  }

  // After the full path of an IM_LOAD_IMMEDIATE (ShaderLoaded): its answer for the next time.
  void GuardarCargaInmediata(uint32_t type, uint64_t clave) {
    const rex::x360::EntradaShader* entry = type == 0 ? vs_ : ps_;
    const bool guardar = clave && entry && !(type == 0 && IsGlowShader(entry));
    if (type == 0) vs_clave_ = guardar ? clave : 0;
    if (!guardar) return;
    auto& cache = cargas_inmediatas_[type & 1];
    if (cache.size() >= 16384) cache.clear();  // bounded; the state of the current VS does not live here
    cache[clave] = {entry, type == 0 ? generacion_vs_ : 0};
  }

  // xmow_diag_solo_activo: whether this frame's draws feed the diagnostics (Draw), once per frame at the Swap.
  // Active with a draw or frame trace, the probe, an A/B capture under way or enabled, or a skip/search mode.
  void ActualizarDiag() {
    diag_activo_ = !REXCVAR_GET(xmow_diag_solo_activo) || draw_tracing_ || tracing_ || probing_ || ab_grupo_ != 0 ||
                   ab_cuenta_ != 0 || REXCVAR_GET(xmow_captura_ab) || skip_mode_ != 0 || capture_after_skip_ != 0;
  }

  void Draw() {
    ++geometria_;
    FrameTraceDraw();
    namespace g = rex::graphics;
    // Depth only (RB_MODECONTROL 5): the GPU runs no pixel shader, so the VS is enough.
    const bool depth_only = (Register(g::XE_GPU_REG_RB_MODECONTROL) & 0x7) == 5;
    // XMOW: Wolverine draws its scene into a real 2X multisampled surface (pitch 1280, two predicated tiles, resolved in
    // pixels). The renderer has no multisampling: drawn as the 1X surface of the same size (xmow_msaa2x_como_1x), not
    // through Dead Space 2's double-height alias, which stretched the top tile and put the bottom one in rows 512-927
    // while its resolve read rows 0-207 (test 14).
    const uint32_t msaa_guest = (Register(g::XE_GPU_REG_RB_SURFACE_INFO) >> 16) & 0x3;
    const uint32_t msaa = msaa_guest == 1 && REXCVAR_GET(xmow_msaa2x_como_1x) ? 0 : msaa_guest;
    // Which way the scene's depth test goes, for the depth value of the emulated clears.
    if (!msaa && (Register(g::XE_GPU_REG_RB_DEPTHCONTROL) & 0x2)) {
      const uint32_t zfunc = (Register(g::XE_GPU_REG_RB_DEPTHCONTROL) >> 4) & 0x7;
      if (zfunc == 4 || zfunc == 6) ++depth_greater_; else if (zfunc == 1 || zfunc == 3) ++depth_less_;
      // And per depth buffer (its EDRAM base): Gears of War 2's scene goes GREATER (far = 0), its shadow maps LESS
      // (far = 1). With one direction for all, the shadow maps were cleared to 0, nothing could be drawn into them,
      // every pixel came out in shadow and the modulated shadows darkened the whole world to black.
      auto& [mayor, menor] =
          depth_direction_[DepthKey(Register(g::XE_GPU_REG_RB_DEPTH_INFO), Register(g::XE_GPU_REG_RB_SURFACE_INFO))];
      if (zfunc == 4 || zfunc == 6) ++mayor; else if (zfunc == 1 || zfunc == 3) ++menor;
    }
    // Direct3D clears a target by drawing over it with its own internal shaders (not created by the game, so not in
    // the library), through the same EDRAM seen as a multisampled surface of half the pitch. Done as the renderer's
    // clears instead.
    // Only full-surface ones (window scissor to 8192): Dead Space 2's light stencil masks are multisampled draws too,
    // and one whose VS was not identified, taken for a clear, wiped the stencil and let a light flood the screen.
    // A single-sampled full-surface draw without an identified VS is usually a real draw (the lamps' 64x8 visibility
    // pass is one, and clearing for it wiped the scene's depth in the middle of the frame). Single-sampled, only
    // Direct3D's own clear shape is taken: one rectangle (rect list, 3 vertices) with all four targets' mask. The
    // decompression scene clears its quarter-size particle buffer that way every frame; left as it was, the smoke and
    // debris of every frame piled up into grey smears. And only onto an 8-bit color target with a color in 0..1 in the
    // clear shader's c0: the same shape also fills the 80-wide float exposure target with game data (c0 like 0.027 0
    // -18.4 1), which is not a clear.
    const uint32_t initiator = Register(g::XE_GPU_REG_VGT_DRAW_INITIATOR);
    // Also onto a k_16_16 target (fixed point, -32..32): Gears of War 2 clears its motion blur velocity buffer (EDRAM
    // 658, 640x360) that way before writing the moving objects' velocities with MAX blending. Never cleared, it kept
    // the largest velocity ever written, and the hero riding a vehicle stayed smeared though he does not move.
    const uint32_t clear_format = (Register(g::XE_GPU_REG_RB_COLOR_INFO) >> 16) & 0xF;  // (its color: EmulateClear)
    bool rect_clear = !msaa && (initiator & 0x3F) == 8 && (initiator >> 16) == 3 &&
                      Register(g::XE_GPU_REG_RB_COLOR_MASK) == 0xFFFF && (clear_format <= 1 || clear_format == 4);
    for (uint32_t j = 0; j < 4 && rect_clear; ++j) {
      float v;
      std::memcpy(&v, &registers_[0x4400 + j], 4);
      rect_clear = clear_format == 4 ? (std::isfinite(v) && v >= -32.0f && v <= 32.0f) : (v >= 0.0f && v <= 1.0f);
    }
    // Gears of War 2 fills each shadow map with the far plane before drawing it: one rectangle, depth-only, depth
    // written with ALWAYS (depthcontrol 00000076), through a vertex shader of its own that is not in the library.
    // Skipped, the 800x800 shadow map kept 0 everywhere, every pixel came out shadowed and the modulated shadows
    // blacked out characters and walls. Done as a depth clear (to the far value of that depth buffer).
    const uint32_t depthcontrol = Register(g::XE_GPU_REG_RB_DEPTHCONTROL);
    const bool depth_fill = !msaa && (initiator & 0x3F) == 8 && (initiator >> 16) == 3 &&
                            (Register(g::XE_GPU_REG_RB_MODECONTROL) & 0x7) == 5 && (depthcontrol & 0x6) == 0x6 &&
                            ((depthcontrol >> 4) & 0x7) == 7;
    // XMOW: Wolverine's library has Direct3D's own clear shaders (taken from the XEX), so vs_ is set for its clear
    // rectangles. Only a rectangle whose pixel shader reads no texture and writes without blending (or writes no
    // color) is taken for a clear. Test 12: the game's own full-screen rectangles (VS n3807 with PS n12752 / n8672,
    // which copy the resolved scene color and depth back into the EDRAM before the lights) were emulated as clears
    // with their c0 = (8 0 0 0) as the color: the whole scene turned red. Those are drawn (ExpandRectList).
    // 0.1.27 (xmow_spirv_lectura_fondo): a shader met for the first time has its SPIR-V read on the library's reader
    // thread; until then its samplers (below, also for a depth-only rectangle) and SPIR-V must not be read, and the
    // draw is skipped, as with a DKSH being read. The vertex shader is checked after the clears (they do not need it).
    if (ps_ && !gow2::shaders::Global().Lista(ps_)) {
      ++spirv_no_lista_;
      if (draw_tracing_) DrawTraceLine(false, "PS SPIR-V still being read");
      return;
    }
    const uint32_t rect_mask = Register(g::XE_GPU_REG_RB_COLOR_MASK);
    const bool rect_copy_blend = (Register(g::XE_GPU_REG_RB_BLENDCONTROL0) & 0x1FFF1FFF) == 0x00010001;
    const bool d3d_rect = (initiator & 0x3F) == 8 && REXCVAR_GET(xmow_rectangulos_como_borrado) &&
                          (!ps_ || ps_->samplers.empty()) && (depth_only || !rect_mask || rect_copy_blend);
    if (d3d_rect || (!vs_ && ((msaa && (Register(g::XE_GPU_REG_PA_SC_WINDOW_SCISSOR_BR) & 0x3FFF) >= 0x2000) ||
                              rect_clear || depth_fill))) {
      if (draw_tracing_) DrawTraceLine(true, "Direct3D clear, emulated");
      EmulateClear(msaa, d3d_rect && !msaa && REXCVAR_GET(xmow_borrado_rectangulo_del_juego));
      return;
    }
    if (!vs_) {
      ++sin_vs_;
      {
        const uint64_t key = XXH3_64bits(vs_microcode_.data(), vs_microcode_.size() * sizeof(uint32_t));
        if (unknown_vs_skips_[key]++ == 0) {
          unknown_vs_sample_[key] = fmt::format(
              "{} words, prim {} count {}, source {}, color0 {:08X} mask {:04X}, depth {:08X} control {:08X}, mode {}, "
              "msaa {}, PS n{}",
              vs_microcode_.size(), initiator & 0x3F, initiator >> 16, (initiator >> 6) & 0x3,
              Register(g::XE_GPU_REG_RB_COLOR_INFO), Register(g::XE_GPU_REG_RB_COLOR_MASK),
              Register(g::XE_GPU_REG_RB_DEPTH_INFO), Register(g::XE_GPU_REG_RB_DEPTHCONTROL),
              Register(g::XE_GPU_REG_RB_MODECONTROL) & 0x7, msaa, ps_ ? int(ps_->numero) : -1);
        }
      }
      // A real draw whose VS was not identified (not a Direct3D clear): which microcode, once each.
      if (!msaa && unknown_vs_logged_.size() < 16) {
        const uint64_t key = XXH3_64bits(vs_microcode_.data(), vs_microcode_.size() * sizeof(uint32_t));
        if (unknown_vs_logged_.insert(key).second) {
          REXLOG_WARN("[gpu] draw skipped, VS not identified: microcode {:016X} ({} words, first {:08X} {:08X} "
                      "{:08X}), target c0 {:08X} pitch {}, PS n{}",
                      key, vs_microcode_.size(), vs_microcode_.size() > 0 ? vs_microcode_[0] : 0,
                      vs_microcode_.size() > 1 ? vs_microcode_[1] : 0, vs_microcode_.size() > 2 ? vs_microcode_[2] : 0,
                      Register(g::XE_GPU_REG_RB_COLOR_INFO), Register(g::XE_GPU_REG_RB_SURFACE_INFO) & 0x3FFF,
                      ps_ ? int(ps_->numero) : -1);
        }
      }
      if (draw_tracing_) DrawTraceLine(false, "no VS");
      return;
    }
    if (!ps_ && !depth_only) {
      ++sin_ps_;
      return;
    }
    if (!gow2::shaders::Global().Lista(vs_)) {  // 0.1.27, see above
      ++spirv_no_lista_;
      if (draw_tracing_) DrawTraceLine(false, "VS SPIR-V still being read");
      return;
    }
    // gow2_sin_prepaso_z: the scene's depth prepass (depth-only draws writing the depth buffer at EDRAM 000) is not
    // drawn; the base pass that follows tests GEQUAL and writes depth itself, so it ends up the same.
    if (REXCVAR_GET(gow2_sin_prepaso_z) && skip_mode_ != 8 && (Register(g::XE_GPU_REG_RB_MODECONTROL) & 0x7) == 5 &&
        (Register(g::XE_GPU_REG_RB_DEPTH_INFO) & 0xFFF) == 0 && (Register(g::XE_GPU_REG_RB_DEPTHCONTROL) & 0x4)) {
      ++prepass_skipped_;
      return;
    }
    // gow2_sin_prepaso_enmascarado (singularity-nx): the masked (alpha-tested) depth prepass, colour+depth mode with
    // every colour channel masked off and depth writes on into the scene depth (EDRAM 000).
    if (REXCVAR_GET(gow2_sin_prepaso_enmascarado) && skip_mode_ != 8 && (Register(g::XE_GPU_REG_RB_MODECONTROL) & 0x7) == 4 &&
        (Register(g::XE_GPU_REG_RB_COLOR_MASK) & 0xFFFF) == 0 && (Register(g::XE_GPU_REG_RB_DEPTH_INFO) & 0xFFF) == 0 &&
        (Register(g::XE_GPU_REG_RB_DEPTHCONTROL) & 0x6) == 0x6) {
      ++prepass_skipped_;
      return;
    }
    if (ps_ && ps_->numero == kPsNieblaAltura && NieblaAlturaOmitida()) {
      ++omitidos_niebla_;
      return;
    }
    if (ps_ && EnListaOmitir(ps_->numero)) {
      ++omitidos_ps_;
      return;
    }
    // xmow_pular_caixas_consulta: inside an occlusion query (begin seen, end not yet), a draw that writes no color, no
    // depth and no stencil is the query's box: it cannot change the image and its result is faked visible anyway.
    // Real occlusion (gow2_oclusion_real) never gets here with the flag set (OcclusionQuery).
    // xmow_oclusao_conservadora: the box is drawn (and counted) only when the query's history asks for a count
    // (CajaConservadora); a draw inside the query that is not a box keeps the query out of the history.
    if (query_aberta_ && oc_abierta_) {
      if (CaixaDeConsulta()) {
        if (!CajaConservadora()) {
          ++caixas_omitidas_;
          ++frame_caixas_;
          return;
        }
      } else {
        oc_pinta_ = true;
      }
    } else if (query_aberta_ && REXCVAR_GET(xmow_pular_caixas_consulta) && CaixaDeConsulta()) {
      ++caixas_omitidas_;
      ++frame_caixas_;
      return;
    }
    // 0.1.29 (xmow_pular_inertes): outside any occlusion query, a draw that writes no color, no depth and no stencil
    // (CaixaDeConsulta, the census' inert class: ~96 per frame in test 35, all drawn by the renderer, whose own
    // nfsmw_nativo_saltar_invisibles keeps draws with a depth or stencil test) cannot change the image: the renderer
    // has no memexport. Real occlusion counts draws; the probe frame and the traced frame keep everything.
    if (REXCVAR_GET(xmow_pular_inertes) && !query_aberta_ && !oc_abierta_ && !probing_ && !draw_tracing_ &&
        !REXCVAR_GET(gow2_oclusion_real) && CaixaDeConsulta()) {
      ++inertes_omitidos_;
      return;
    }
    // L3 + R3 dump frame: the final composite's (PS 6971) textures and constants c0-c15, in full, once.
    if (draw_tracing_ && ps_ && ps_->numero == 6971 && !composicao_final_logada_) {
      composicao_final_logada_ = true;
      const uint32_t n = draw_trace_n_, cap = draw_trace_cap_;
      draw_trace_n_ = 0;
      draw_trace_cap_ = 1;
      DrawTraceLine(true, "XMOW final composite (PS 6971) inputs");
      draw_trace_n_ = n;
      draw_trace_cap_ = cap;
    }
    if (skip_mode_ > 1 && SkippedByMode()) {
      ++skipped_by_mode_;
      return;
    }
    // xmow_ab_ps: the first draws of each listed pixel shader with their textures and constants in the log.
    // (This, the search candidates and the report counters below: only with diag_activo_, ActualizarDiag.)
    if (diag_activo_ && ps_ && EnListaAb(ps_->numero) && ab_detalles_[ps_->numero]++ < 3) {
      const uint32_t n = draw_trace_n_, cap = draw_trace_cap_;
      draw_trace_n_ = 0;
      draw_trace_cap_ = 1;
      DrawTraceLine(true, "XMOW detalle (xmow_ab_ps)");
      draw_trace_n_ = n;
      draw_trace_cap_ = cap;
    } else if (diag_activo_ && ps_ && (Register(g::XE_GPU_REG_RB_MODECONTROL) & 0x7) == 4 &&
               (Register(g::XE_GPU_REG_RB_COLOR_MASK) & 0xF) != 0 &&
               (((Register(g::XE_GPU_REG_RB_COLOR_INFO) >> 16) & 0xF) == 3 ||
                ((Register(g::XE_GPU_REG_RB_COLOR_INFO) >> 16) & 0xF) == 12) &&
               (Register(g::XE_GPU_REG_VGT_DRAW_INITIATOR) >> 16) <= 6) {
      // Full-screen translucent effects (haze, glow): the first draws of each pixel shader, in detail.
      const uint32_t mezcla = Register(g::XE_GPU_REG_RB_BLENDCONTROL0);
      const uint32_t src = mezcla & 0x1F, dst = (mezcla >> 8) & 0x1F;
      if (!(src == 1 && dst == 0) && !(src == 8 && dst == 0) && tela_detalles_[ps_->numero]++ < 3) {
        const uint32_t n = draw_trace_n_, cap = draw_trace_cap_;
        draw_trace_n_ = 0;
        draw_trace_cap_ = 1;
        DrawTraceLine(true, "XMOW detalle (pantalla translucida)");
        draw_trace_n_ = n;
        draw_trace_cap_ = cap;
      }
    }
    if (ab_grupo_ && OmitidoAb()) {
      ++ab_omitidos_;
      return;
    }
    // The search modes' candidates: vertex shaders writing depth while drawing color and depth.
    // (pixel search: the base pass, color into gow2_edram_base_escena / gow2_formato_escena with depth on)
    if (!diag_activo_) {
      // no search candidates this frame (xmow_diag_solo_activo)
    } else if (REXCVAR_GET(gow2_busqueda_ps)) {
      if (ps_ && (Register(g::XE_GPU_REG_RB_MODECONTROL) & 0x7) == 4 &&
          IsBasePassTarget() && depth_vs_frame_.size() < 256) {
        // Weighted by vertices: the big meshes (the ground) are one or two draws each.
        depth_vs_frame_[ps_->numero] += std::max<uint32_t>(Register(g::XE_GPU_REG_VGT_DRAW_INITIATOR) >> 16, 1);
      }
    } else if ((Register(g::XE_GPU_REG_RB_MODECONTROL) & 0x7) == 4 &&
               (Register(g::XE_GPU_REG_RB_DEPTHCONTROL) & 0x4) && depth_vs_frame_.size() < 256) {
      ++depth_vs_frame_[vs_->numero];
    }
    bool tree_identity = false, tree_c10_zero = false;
    if (REXCVAR_GET(gow2_ajustes_gears2) && vs_->huella_contenedor == kBrokenSkinVs) {
      LogBrokenSkinDraw();
      const int32_t mode = REXCVAR_GET(gow2_arboles);
      if (mode == 0 && REXCVAR_GET(gow2_saltar_vs_rotos)) {
        ++skipped_broken_vs_;
        return;
      }
      tree_identity = mode == 2 || mode == 3;
      tree_c10_zero = mode == 3 || mode == 4;
    }
    if (IsGlowShader(vs_)) {
      if (glow_draws_logged_ < 12) {
        ++glow_draws_logged_;
        // Fetch constant 17 (vertex texture register 1): the visibility texture the shader reads.
        const uint32_t f = 0x4800 + 17 * 6;
        // Vertex fetch constants 31.0-31.2 (where the containers point their fetches) share fetch slot 31's words.
        const uint32_t v = 0x4800 + 31 * 6;
        REXLOG_INFO("[gpu] glow draw (VS n{}, PS n{}): texture fetch 17 {:08X} {:08X} {:08X} {:08X} {:08X} {:08X}; "
                    "vertex fetch 31.x {:08X} {:08X} {:08X} {:08X} {:08X} {:08X}",
                    vs_->numero, ps_ ? ps_->numero : 0, Register(f), Register(f + 1), Register(f + 2),
                    Register(f + 3), Register(f + 4), Register(f + 5), Register(v), Register(v + 1), Register(v + 2),
                    Register(v + 3), Register(v + 4), Register(v + 5));
      }
      if (hide_glow_) return;
    }
    // Diagnostics (Report): formats and primitive types of what is sent.
    if (diag_activo_) {
      const uint32_t mask = Register(rex::graphics::XE_GPU_REG_RB_COLOR_MASK);
      static constexpr uint32_t kColorInfo[4] = {
          rex::graphics::XE_GPU_REG_RB_COLOR_INFO, rex::graphics::XE_GPU_REG_RB_COLOR1_INFO,
          rex::graphics::XE_GPU_REG_RB_COLOR2_INFO, rex::graphics::XE_GPU_REG_RB_COLOR3_INFO};
      for (uint32_t i = 0; i < 4 && ps_ && !depth_only; ++i) {
        if (((mask >> (i * 4)) & 0xF) && ((ps_->salidas >> i) & 0x1)) {
          ++color_formats_[(Register(kColorInfo[i]) >> 16) & 0xF];
        }
      }
      if (Register(rex::graphics::XE_GPU_REG_RB_DEPTHCONTROL) & 0x6) {  // Z test or Z write
        ++depth_formats_[(Register(rex::graphics::XE_GPU_REG_RB_DEPTH_INFO) >> 16) & 0x1];
      }
      ++primitive_types_[Register(rex::graphics::XE_GPU_REG_VGT_DRAW_INITIATOR) & 0x1F];
      // Indexed draws by index size and byte order (VGT_DMA_SIZE bits 30-31): 16-bit indices in k8in32 or k16in32
      // were read unswapped by the SDK.
      const uint32_t init = Register(rex::graphics::XE_GPU_REG_VGT_DRAW_INITIATOR);
      if (((init >> 6) & 0x3) == 0) {  // source select: DMA (an index buffer)
        ++index_orders_[((init >> 11) & 0x1) * 4 + (Register(rex::graphics::XE_GPU_REG_VGT_DMA_SIZE) >> 30)];
      }
    }

    rex::x360::PeticionDibujo request;
    request.registros = registers_.data();
    AliasScope alias(*this);
    if (msaa) {
      // Direct3D's trick for quarter-rate passes (Dead Space 2's light stencil masks): the 1280x720 depth buffer
      // drawn as a 640x360 4x-multisampled surface over the same EDRAM, each pixel's four samples landing on 2x2
      // pixels. The renderer has no multisampling and keys targets by pitch, so the draw goes to the 1x surface
      // with everything in pixels scaled up instead (same pixels covered).
      request.registros = alias.Apply(msaa);
      ++generacion_encuadre_;  // the scaled viewport and scissor are not the mirror's
    }
    const bool rect_list = ExpandRectList(alias);
    if (rect_list) ++generacion_fetch_;  // the fetch constant points at our quads
    request.vs = vs_;
    request.ps = ps_;
    request.vs_microcodigo = vs_microcode_;
    request.generacion_vs = generacion_vs_;
    request.generacion_constantes_vs = generacion_constantes_vs_;
    request.generacion_constantes_ps = generacion_constantes_ps_;
    // xmow 1034: the constant registers changed since the last draw handed to the renderer (reset after Dibujar).
    request.constantes_sucias_validas = true;
    std::memcpy(request.sucias_vs, sucias_vs_, sizeof(sucias_vs_));
    std::memcpy(request.sucias_ps, sucias_ps_, sizeof(sucias_ps_));
    request.generacion_fetch = generacion_fetch_;
    request.generacion_encuadre = generacion_encuadre_;
    ++enviados_;
    // The probe frame: a base-pass draw only into the probe square, its samples counted on the GPU.
    const bool probe = probing_ && !msaa && ps_ && (Register(g::XE_GPU_REG_RB_MODECONTROL) & 0x7) == 4 &&
                       IsBasePassTarget() && probes_.size() < 3000;
    uint32_t saved_tl = 0, saved_br = 0;
    const uint32_t probe_key = 0xFFF00000u + uint32_t(probes_.size());
    if (probe) {
      saved_tl = registers_[g::XE_GPU_REG_PA_SC_WINDOW_SCISSOR_TL];
      saved_br = registers_[g::XE_GPU_REG_PA_SC_WINDOW_SCISSOR_BR];
      registers_[g::XE_GPU_REG_PA_SC_WINDOW_SCISSOR_TL] = 0x80000000u | (probe_y_ << 16) | probe_x_;
      registers_[g::XE_GPU_REG_PA_SC_WINDOW_SCISSOR_BR] = 0x80000000u | ((probe_y_ + 8) << 16) | (probe_x_ + 8);
      ++generacion_encuadre_;
      request.generacion_encuadre = generacion_encuadre_;
      destinos_->EmpezarOclusion(probe_key);
    }
    // 0.1.29 (xmow_diag_tijera_1px, diagnostic): this draw only into the pixel at 0,0 (window scissor 0,0-1,1), as the
    // probe frame does with its square: the GPU keeps each draw's fixed work and shades almost nothing.
    // 0.1.30 (xmow_diag_custo_dibujo): 1 = this pixel scissor, 2 = EMPTY (all of the state, 0 vertices; the SDK
    // honours PeticionDibujo::vacio where it emits the draw).
    const int32_t custo_dibujo = REXCVAR_GET(xmow_diag_custo_dibujo);
    const bool tijera_1px = !probe && (custo_dibujo == 1 || (custo_dibujo <= 0 && REXCVAR_GET(xmow_diag_tijera_1px)));
    if (!probe && custo_dibujo == 2) {
      request.vacio = true;
      ++vacios_dibujos_;
    }
    if (!probe && custo_dibujo == 3) request.un_triangulo = true;  // 0.1.33c: ONE TRIANGLE (first triangle only)
    uint32_t tijera_tl = 0, tijera_br = 0;
    if (tijera_1px) {
      tijera_tl = registers_[g::XE_GPU_REG_PA_SC_WINDOW_SCISSOR_TL];
      tijera_br = registers_[g::XE_GPU_REG_PA_SC_WINDOW_SCISSOR_BR];
      registers_[g::XE_GPU_REG_PA_SC_WINDOW_SCISSOR_TL] = 0x80000000u;
      registers_[g::XE_GPU_REG_PA_SC_WINDOW_SCISSOR_BR] = 0x80000000u | (1u << 16) | 1u;
      ++generacion_encuadre_;
      request.generacion_encuadre = generacion_encuadre_;
      ++tijera_1px_dibujos_;
    }
    // gow2_velocidad_sin_z: the velocity buffer's draws without their depth test (z enable off for this draw).
    const bool velocity_no_z = REXCVAR_GET(gow2_ajustes_gears2) && REXCVAR_GET(gow2_velocidad_sin_z) && ps_ &&
                               ps_->huella_contenedor == UINT64_C(0x1929EAB6BE60FA77);
    const uint32_t saved_depthcontrol = registers_[g::XE_GPU_REG_RB_DEPTHCONTROL];
    if (velocity_no_z) {
      registers_[g::XE_GPU_REG_RB_DEPTHCONTROL] = saved_depthcontrol & ~0x2u;
      ++generacion_encuadre_;
      request.generacion_encuadre = generacion_encuadre_;
    }
    // gow2_arboles = 2: the trees' three sway matrices (c11-c22) replaced by the identity for this draw.
    std::array<uint32_t, 48> saved_sway{};
    std::array<uint32_t, 4> saved_c10{};
    if (tree_c10_zero) {
      // The index offset c10.x: with it 0 the matrix index is r1.x mod 3 of a 1 (no truncation of a 2/3 involved).
      for (uint32_t i = 0; i < 4; ++i) {
        saved_c10[i] = registers_[0x4000 + 10 * 4 + i];
        registers_[0x4000 + 10 * 4 + i] = 0;
      }
      ++generacion_constantes_vs_;
      request.generacion_constantes_vs = generacion_constantes_vs_;
      request.sucias_vs[0] |= uint64_t(1) << 10;  // c10
    }
    if (tree_identity) {
      for (uint32_t i = 0; i < 48; ++i) {
        saved_sway[i] = registers_[0x4000 + 11 * 4 + i];
        registers_[0x4000 + 11 * 4 + i] = (i % 16) % 5 == 0 ? 0x3F800000u : 0u;  // rows 0-3 of each 4x4: 1 on the diagonal
      }
      ++generacion_constantes_vs_;
      request.generacion_constantes_vs = generacion_constantes_vs_;
      request.sucias_vs[0] |= ((uint64_t(1) << 12) - 1) << 11;  // c11-c22
    }
    const uint8_t clase = ClaseDelDibujo();  // 0.1.26: census of the 10 s report ([xmow census])
    ++censo_[clase];
    const uint64_t t_dibujar = NsAnillo();  // xmow 1031
    const bool drawn = destinos_->Dibujar(request);
    anillo_.dibujar += NsAnillo() - t_dibujar;
    std::memset(sucias_vs_, 0, sizeof(sucias_vs_));  // xmow 1034: handed over
    std::memset(sucias_ps_, 0, sizeof(sucias_ps_));
    if (clase == kClaseInerte && drawn) ++censo_inertes_dibujados_;
    if (tree_identity) {
      for (uint32_t i = 0; i < 48; ++i) registers_[0x4000 + 11 * 4 + i] = saved_sway[i];
      ++generacion_constantes_vs_;
      sucias_vs_[0] |= ((uint64_t(1) << 12) - 1) << 11;  // xmow 1034: c11-c22 back to the game's
    }
    if (tree_c10_zero) {
      for (uint32_t i = 0; i < 4; ++i) registers_[0x4000 + 10 * 4 + i] = saved_c10[i];
      ++generacion_constantes_vs_;
      sucias_vs_[0] |= uint64_t(1) << 10;  // xmow 1034: c10 back to the game's
    }
    if (velocity_no_z) {
      registers_[g::XE_GPU_REG_RB_DEPTHCONTROL] = saved_depthcontrol;
      ++generacion_encuadre_;
    }
    if (tijera_1px) {
      registers_[g::XE_GPU_REG_PA_SC_WINDOW_SCISSOR_TL] = tijera_tl;
      registers_[g::XE_GPU_REG_PA_SC_WINDOW_SCISSOR_BR] = tijera_br;
      ++generacion_encuadre_;
    }
    if (probe) {
      const uint64_t ticket = destinos_->TerminarOclusionAsincrona(probe_key);
      registers_[g::XE_GPU_REG_PA_SC_WINDOW_SCISSOR_TL] = saved_tl;
      registers_[g::XE_GPU_REG_PA_SC_WINDOW_SCISSOR_BR] = saved_br;
      ++generacion_encuadre_;
      probes_.push_back({uint32_t(probes_.size()), vs_ ? vs_->numero : 0, ps_->numero,
                         Register(g::XE_GPU_REG_VGT_DRAW_INITIATOR) >> 16, ticket, UINT64_MAX});
    }
    if (draw_sum_on_) DrawSumAdd(clase);
    if (draw_tracing_) DrawTraceLine(drawn);
    if (msaa) {
      ++generacion_encuadre_;  // back to the mirror's for the next draw
      ++msaa_draws_;
    }
    if (rect_list) ++generacion_fetch_;  // back to the game's fetch constant
  }


  // The register mirror with a multisampled surface turned into the 1x surface it aliases: 4X doubles width and
  // height (pitch x2), 2X doubles the height (samples stacked vertically). Viewport and scissors scale the same way.
  // Done in place on the ten registers involved and undone when the scope ends (copying the whole 80 KB mirror for
  // each of the ~30 multisampled draws per frame cost ~2.4 MB of copies per frame on the ring thread).
  class AliasScope {
   public:
    explicit AliasScope(RingGraphicsSystem& system) : system_(system) {}
    ~AliasScope() {
      for (uint32_t i = 0; i < count_; ++i) system_.registers_[saved_[i].first] = saved_[i].second;
    }
    const uint32_t* Apply(uint32_t msaa) { return system_.AliasedRegisters(msaa, *this); }
    void Save(uint32_t index) { saved_[count_++] = {index, system_.registers_[index]}; }

   private:
    RingGraphicsSystem& system_;
    std::pair<uint32_t, uint32_t> saved_[16];
    uint32_t count_ = 0;
  };

  const uint32_t* AliasedRegisters(uint32_t msaa, AliasScope& scope) {
    namespace g = rex::graphics;
    for (uint32_t index : {uint32_t(g::XE_GPU_REG_RB_SURFACE_INFO), uint32_t(g::XE_GPU_REG_PA_CL_VPORT_XSCALE),
                           uint32_t(g::XE_GPU_REG_PA_CL_VPORT_XSCALE + 1), uint32_t(g::XE_GPU_REG_PA_CL_VPORT_XSCALE + 2),
                           uint32_t(g::XE_GPU_REG_PA_CL_VPORT_XSCALE + 3), uint32_t(g::XE_GPU_REG_PA_SC_WINDOW_SCISSOR_TL),
                           uint32_t(g::XE_GPU_REG_PA_SC_WINDOW_SCISSOR_BR), uint32_t(g::XE_GPU_REG_PA_SC_SCREEN_SCISSOR_TL),
                           uint32_t(g::XE_GPU_REG_PA_SC_SCREEN_SCISSOR_BR)}) {
      scope.Save(index);
    }
    uint32_t* r = registers_.data();
    const uint32_t sx = msaa == 2 ? 2 : 1, sy = 2;
    const uint32_t surface = r[g::XE_GPU_REG_RB_SURFACE_INFO];
    r[g::XE_GPU_REG_RB_SURFACE_INFO] = (surface & ~uint32_t(0x33FFF)) | std::min<uint32_t>((surface & 0x3FFF) * sx, 0x3FFF);
    auto scale_float = [&](uint32_t index, uint32_t factor) {
      float v;
      std::memcpy(&v, &r[index], 4);
      v *= float(factor);
      std::memcpy(&r[index], &v, 4);
    };
    scale_float(g::XE_GPU_REG_PA_CL_VPORT_XSCALE, sx);
    scale_float(g::XE_GPU_REG_PA_CL_VPORT_XSCALE + 1, sx);  // XOFFSET
    scale_float(g::XE_GPU_REG_PA_CL_VPORT_XSCALE + 2, sy);  // YSCALE
    scale_float(g::XE_GPU_REG_PA_CL_VPORT_XSCALE + 3, sy);  // YOFFSET
    auto scale_xy = [&](uint32_t index, uint32_t bits) {
      const uint32_t v = r[index];
      const uint32_t mask = (1u << bits) - 1;
      const uint32_t x = std::min((v & mask) * sx, mask), y = std::min(((v >> 16) & mask) * sy, mask);
      r[index] = (v & ~(mask | (mask << 16))) | x | (y << 16);
    };
    scale_xy(g::XE_GPU_REG_PA_SC_WINDOW_SCISSOR_TL, 14);
    scale_xy(g::XE_GPU_REG_PA_SC_WINDOW_SCISSOR_BR, 14);
    scale_xy(g::XE_GPU_REG_PA_SC_SCREEN_SCISSOR_TL, 15);
    scale_xy(g::XE_GPU_REG_PA_SC_SCREEN_SCISSOR_BR, 15);
    return r;
  }

  // Rectangle lists (primitive type 8): Unreal Engine 3 draws its full-screen passes (post-processing, the final copy
  // to the back buffer, UI quads) as rectangles of three vertices, the fourth implied, which deko3d has no primitive
  // for. Each rectangle becomes a quad (deko3d draws quad lists natively): the corner is the vertex opposite the
  // longest edge (by POSITION0's x and y), the missing vertex is a + b - corner for every component (a rectangle's
  // attributes are affine across it), written to a guest buffer of ours that the fetch constant points at for this
  // draw. Only what such quads use: auto-indexed draws, one vertex stream, 32-bit float elements, in k8in32 or no
  // swap. The registers are restored with the alias scope.
  bool ExpandRectList(AliasScope& scope) {
    namespace g = rex::graphics;
    const uint32_t init = Register(g::XE_GPU_REG_VGT_DRAW_INITIATOR);
    if ((init & 0x3F) != 8 || ((init >> 6) & 0x3) != 2 || !vs_) return false;  // rect list, auto index
    const uint32_t count = init >> 16;
    if (!count || count % 3 || count > 3 * 4096) return Unexpanded("vertex count");
    int32_t slot = -1;
    uint32_t stride = 0, position = UINT32_MAX;
    for (const rex::x360::ElementoVertice& el : vs_->elementos) {
      const size_t i = size_t(el.instruccion) * 3;
      if (i + 2 >= vs_microcode_.size()) return Unexpanded("fetch outside the microcode");
      const uint32_t w0 = vs_microcode_[i], w1 = vs_microcode_[i + 1], w2 = vs_microcode_[i + 2];
      const int32_t s = int32_t(((w0 >> 20) & 0x1F) * 3 + ((w0 >> 25) & 0x3));
      if (slot >= 0 && s != slot) return Unexpanded("more than one stream");
      slot = s;
      const uint32_t format = (w1 >> 16) & 0x3F;
      if (format != 36 && format != 37 && format != 57 && format != 38) return Unexpanded("not a float format");
      if (stride && (w2 & 0xFF) != stride) return Unexpanded("strides differ");
      stride = w2 & 0xFF;
      if (el.uso == 0 && el.indice_uso == 0) position = (w2 >> 8) & 0x7FFFFF;
    }
    if (slot < 0 || !stride || position == UINT32_MAX || position + 2 > stride) return Unexpanded("no position");
    const uint32_t reg = 0x4800 + uint32_t(slot) * 2;
    const uint32_t f0 = registers_[reg], f1 = registers_[reg + 1];
    const uint32_t endian = f1 & 0x3;
    if ((f0 & 0x3) != 3 || (endian != 0 && endian != 2)) return Unexpanded("fetch constant");
    const uint32_t first = Register(g::XE_GPU_REG_VGT_INDX_OFFSET) & 0xFFFFFF;
    const uint32_t available = (f1 >> 2) & 0xFFFFFF;  // words
    if (uint64_t(first + count) * stride > available) return Unexpanded("past the buffer");
    const uint32_t* src = memory_->TranslatePhysical<const uint32_t*>((f0 & 0x1FFFFFFC) + first * stride * 4);
    const uint32_t rects = count / 3;
    const uint32_t words = rects * 4 * stride;
    if (words * 4 > rect_buffer_size_) {
      if (rect_buffer_) memory_->SystemHeapFree(rect_buffer_);
      rect_buffer_size_ = std::max<uint32_t>(words * 4, 4096);
      rect_buffer_ = memory_->SystemHeapAlloc(rect_buffer_size_, 256, rex::memory::kSystemHeapPhysical);
      if (!rect_buffer_) {
        rect_buffer_size_ = 0;
        return Unexpanded("no guest memory");
      }
    }
    uint32_t* dst = memory_->TranslateVirtual<uint32_t*>(rect_buffer_);
    auto load = [&](uint32_t w) {
      float f;
      const uint32_t v = endian ? __builtin_bswap32(w) : w;
      std::memcpy(&f, &v, 4);
      return f;
    };
    auto store = [&](float f) {
      uint32_t v;
      std::memcpy(&v, &f, 4);
      return endian ? __builtin_bswap32(v) : v;
    };
    for (uint32_t r = 0; r < rects; ++r) {
      const uint32_t* v[3] = {src + (r * 3) * stride, src + (r * 3 + 1) * stride, src + (r * 3 + 2) * stride};
      auto edge = [&](int a, int b) {
        const float dx = load(v[a][position]) - load(v[b][position]);
        const float dy = load(v[a][position + 1]) - load(v[b][position + 1]);
        return dx * dx + dy * dy;
      };
      const float e12 = edge(1, 2), e20 = edge(2, 0), e01 = edge(0, 1);
      const int c = e12 >= e20 && e12 >= e01 ? 0 : (e20 >= e01 ? 1 : 2);  // opposite the longest edge
      const uint32_t* corner = v[c];
      const uint32_t* a = v[(c + 1) % 3];
      const uint32_t* b = v[(c + 2) % 3];
      uint32_t* q = dst + r * 4 * stride;
      std::memcpy(q, corner, stride * 4);
      std::memcpy(q + stride, a, stride * 4);
      for (uint32_t w = 0; w < stride; ++w) q[2 * stride + w] = store(load(a[w]) + load(b[w]) - load(corner[w]));
      std::memcpy(q + 3 * stride, b, stride * 4);
    }
    scope.Save(g::XE_GPU_REG_VGT_DRAW_INITIATOR);
    scope.Save(g::XE_GPU_REG_VGT_INDX_OFFSET);
    scope.Save(reg);
    scope.Save(reg + 1);
    registers_[g::XE_GPU_REG_VGT_DRAW_INITIATOR] = (init & 0xFFC0) | 13 | ((rects * 4) << 16);  // quad list
    registers_[g::XE_GPU_REG_VGT_INDX_OFFSET] = 0;
    registers_[reg] = (memory_->GetPhysicalAddress(rect_buffer_) & 0x1FFFFFFC) | (f0 & 0x3);
    registers_[reg + 1] = (f1 & ~uint32_t(0x03FFFFFC)) | ((words & 0xFFFFFF) << 2);
    if (rect_lists_++ < 4) {
      REXLOG_INFO("[gpu] rect list as quads: {} rectangle(s), stride {} words, position at {} (VS n{})", rects, stride,
                  position, vs_->numero);
    }
    return true;
  }

  bool Unexpanded(const char* why) {
    if (rect_unexpanded_logged_ < 8) {
      ++rect_unexpanded_logged_;
      REXLOG_WARN("[gpu] rect list not expanded ({}): VS n{}", why, vs_ ? int(vs_->numero) : -1);
    }
    return false;
  }

  // A Direct3D clear draw, as the renderer's clears. Direct3D's fast clear binds c0 (the color to clear) and, for the
  // depth buffer, c1-c3 over the depth buffer's EDRAM written as color, with a placeholder depth target that may even
  // sit on c0's base (Dead Space 2's light buffer clear: c0 2D0, c1-c3 000, depth 2D0). So: c0 is cleared as color
  // (to 0); c1-c3 on another base than c0 clear the depth buffer at their base (far plane, stencil 0); the bound
  // depth target is cleared only when depth or stencil is written and it is not on c0's base; depth-only mode
  // clears no color. The first few are logged with what the game had set, to check.
  void EmulateClear(uint32_t msaa, bool game_rect = false) {
    namespace g = rex::graphics;
    AliasScope alias(*this);
    const uint32_t* r = msaa ? alias.Apply(msaa) : registers_.data();
    rex::x360::RegistrosCopia base = rex::x360::LeerRegistrosCopia(r);
    // Direct3D's clear rectangle (VS 6D38CFA5): 7 dwords per vertex, POSITION float3 then COLOR float4, in fetch
    // constant 0. A color clear done through the depth path carries the color's EDRAM bits as depth24 << 8 | stencil,
    // so the depth it writes (z of the first vertex, through the viewport) is the color the target gets. Read before
    // fetch constant 0 is replaced with the renderer's own rectangle below.
    float game_z = 0.0f;
    float game_color[4] = {};
    bool game_vertices = false;
    if ((base.fetch_vertices[0] & 0x3) == uint32_t(xenos::FetchConstantType::kVertex) &&
        ((base.fetch_vertices[1] >> 2) & 0xFFFFFF) >= 7) {
      const auto endian = static_cast<xenos::Endian>(base.fetch_vertices[1] & 0x3);
      const uint8_t* v = memory_->TranslatePhysical(base.fetch_vertices[0] & ~uint32_t(0x3));
      float z;
      std::memcpy(&z, v + 2 * 4, 4);
      z = xenos::GpuSwap(z, endian);
      for (uint32_t j = 0; j < 4; ++j) {  // COLOR, after POSITION
        std::memcpy(&game_color[j], v + (3 + j) * 4, 4);
        game_color[j] = xenos::GpuSwap(game_color[j], endian);
      }
      float zs, zo;
      std::memcpy(&zs, &r[g::XE_GPU_REG_PA_CL_VPORT_ZSCALE], 4);
      std::memcpy(&zo, &r[g::XE_GPU_REG_PA_CL_VPORT_ZOFFSET], 4);
      const uint32_t vte = r[g::XE_GPU_REG_PA_CL_VTE_CNTL];
      if (vte & (1u << 4)) z *= zs;
      if (vte & (1u << 5)) z += zo;
      game_z = z;
      game_vertices = std::isfinite(z);
    }
    // The whole surface: the renderer takes a copy's rectangle from three vertices in fetch constant 0 (where
    // Direct3D puts a resolve's), clipped by the window scissor. Ours, in a small guest buffer made once.
    if (!clear_rect_) {
      clear_rect_ = memory_->SystemHeapAlloc(32, 32, rex::memory::kSystemHeapPhysical);
    }
    if (!clear_rect_) return;
    const float w = float(r[g::XE_GPU_REG_RB_SURFACE_INFO] & 0x3FFF), h = 8192.0f;
    float rect[6] = {0.0f, 0.0f, w, 0.0f, 0.0f, h};  // host order, read with Endian::kNone
    // XMOW: the game's own rectangles (game_rect): the bounds of its vertices through the viewport, kept inside the
    // game's window scissor and window offset. Unreal Engine 3 marks each light's screen area in the stencil with one
    // of these, inside the light's scissor; as a full-surface clear every light lit the whole screen.
    bool own_rect = false;
    if (game_rect && (base.fetch_vertices[0] & 0x3) == uint32_t(xenos::FetchConstantType::kVertex)) {
      const uint32_t count = r[g::XE_GPU_REG_VGT_DRAW_INITIATOR] >> 16;
      const uint32_t dwords = (base.fetch_vertices[1] >> 2) & 0xFFFFFF;
      if (count >= 3 && count <= 96 && dwords >= count * 7) {
        const auto endian = static_cast<xenos::Endian>(base.fetch_vertices[1] & 0x3);
        const uint8_t* v = memory_->TranslatePhysical(base.fetch_vertices[0] & ~uint32_t(0x3));
        const uint32_t vte = r[g::XE_GPU_REG_PA_CL_VTE_CNTL];
        auto reg_f = [&](uint32_t reg) { float f; std::memcpy(&f, &r[reg], 4); return f; };
        const float xs = (vte & 0x1) ? reg_f(g::XE_GPU_REG_PA_CL_VPORT_XSCALE) : 1.0f;
        const float xo = (vte & 0x2) ? reg_f(g::XE_GPU_REG_PA_CL_VPORT_XOFFSET) : 0.0f;
        const float ys = (vte & 0x4) ? reg_f(g::XE_GPU_REG_PA_CL_VPORT_YSCALE) : 1.0f;
        const float yo = (vte & 0x8) ? reg_f(g::XE_GPU_REG_PA_CL_VPORT_YOFFSET) : 0.0f;
        float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
        bool finite = true;
        for (uint32_t i = 0; i < count; ++i) {
          float px, py;  // POSITION x, y (7 dwords per vertex; Direct3D's clear quad has w = 1)
          std::memcpy(&px, v + i * 28, 4);
          std::memcpy(&py, v + i * 28 + 4, 4);
          px = xenos::GpuSwap(px, endian);
          py = xenos::GpuSwap(py, endian);
          const float sx = px * xs + xo, sy = py * ys + yo;
          finite = finite && std::isfinite(sx) && std::isfinite(sy);
          x0 = std::min(x0, sx); x1 = std::max(x1, sx);
          y0 = std::min(y0, sy); y1 = std::max(y1, sy);
        }
        if (finite && x1 > x0 && y1 > y0) {
          x0 = std::max(x0, 0.0f); y0 = std::max(y0, 0.0f);
          x1 = std::min(x1, 8192.0f); y1 = std::min(y1, 8192.0f);
          // Rectangulo takes min/max of three vertices: (x0,y0), (x1,y0), (x0,y1) give the whole box.
          rect[0] = x0; rect[1] = y0; rect[2] = x1; rect[3] = y0; rect[4] = x0; rect[5] = y1;
          own_rect = x1 > x0 && y1 > y0;
        }
      }
    }
    std::memcpy(memory_->TranslateVirtual<uint8_t*>(clear_rect_), rect, sizeof(rect));
    const uint32_t physical = memory_->GetPhysicalAddress(clear_rect_);
    base.fetch_vertices[0] = (physical & ~uint32_t(0x3)) | uint32_t(xenos::FetchConstantType::kVertex);
    base.fetch_vertices[1] = (6u << 2) | uint32_t(xenos::Endian::kNone);
    if (own_rect) {
      ++game_rect_clears_;
    } else {
      base.pa_sc_window_offset = 0;
      base.pa_sc_window_scissor_tl = 0x80000000u;  // window_offset_disable, from 0,0
      base.pa_sc_window_scissor_br = 0x3FFF3FFFu;
      base.pa_su_sc_mode_cntl &= ~(1u << 16);       // no vtx_window_offset
    }
    const uint32_t mask = r[g::XE_GPU_REG_RB_COLOR_MASK];
    const uint32_t depth_info = r[g::XE_GPU_REG_RB_DEPTH_INFO];
    const uint32_t depth_control = r[g::XE_GPU_REG_RB_DEPTHCONTROL];
    const bool depth_only = (r[g::XE_GPU_REG_RB_MODECONTROL] & 0x7) == 5;
    const bool reversed = depth_greater_ > depth_less_;
    // The stencil: Direct3D's clear quad writes the stencil reference with a REPLACE (depthcontrol 00008777: stencil on,
    // ALWAYS, z-pass REPLACE), not 0. Dead Space 2 clears it to its own value before the menu's Scaleform masks, whose
    // frames test LESS against it; written as 0, every frame and backing panel of the menus failed and only the text
    // (drawn without stencil) was left.
    uint32_t stencil = 0;
    if ((depth_control & 0x1) && ((depth_control >> 14) & 0x7) == 2) {
      const uint32_t ref_mask = r[g::XE_GPU_REG_RB_STENCILREFMASK];
      stencil = ref_mask & (ref_mask >> 16) & 0xFF;
    }
    constexpr uint32_t kNullCommand = 3u << 20;  // CopyCommand::kNull: clear only, copy nothing
    // Direct3D's clear quad with depth writes off and the stencil on (depthcontrol 00008701): a stencil-only clear.
    // Gears of War 2 does one after each light (through the scene's own depth buffer); as a full clear it wiped the
    // scene's depth in the middle of the frame. Bit 30 of RB_COPY_CONTROL (unused by the Xenos) tells the renderer
    // (SDK patch 0030).
    const bool stencil_only = !(depth_control & 0x4) && (depth_control & 0x1);
    // xmow 1034 (xmow_registro_barato): `what` is only for the two logs below; once both are done (32 shapes seen, the
    // first 12 + 8 clear lines written) it is not built, nor kept in clear_shapes_. Same log text.
    const bool describir = !REXCVAR_GET(xmow_registro_barato) || clear_shapes_.size() < 32 ||
                           (msaa ? clears_logged_ < 12 : clears_logged_1x_ < 8);
    auto clear_depth_at = [&](uint32_t edram_base, std::string& what, bool only_stencil = false) {
      // The direction of the draws into this depth buffer when it has any, the frame's otherwise.
      bool reversed_here = reversed;
      if (auto it = depth_direction_.find(
              DepthKey((depth_info & ~uint32_t(0xFFF)) | (edram_base & 0xFFF), r[g::XE_GPU_REG_RB_SURFACE_INFO]));
          it != depth_direction_.end() && it->second.first != it->second.second) {
        reversed_here = it->second.first > it->second.second;
      }
      const uint32_t depth_value = (reversed_here ? 0u : 0xFFFFFF00u) | stencil;
      rex::x360::RegistrosCopia c = base;
      c.rb_depth_info = (depth_info & ~uint32_t(0xFFF)) | (edram_base & 0xFFF);
      c.rb_copy_control = 4u | (1u << 9) | kNullCommand | (only_stencil ? 1u << 30 : 0u);
      c.rb_depth_clear = depth_value;
      CopiarMedido(c);
      if (describir) {
        what += fmt::format(" z={:03X}->{:08X}{}", edram_base & 0xFFF, depth_value, only_stencil ? " (stencil only)" : "");
      }
    };
    std::string what;
    const uint32_t c0_base = base.rb_color_info[0] & 0xFFF;
    const bool c0_written = !depth_only && (mask & 0xF);
    // The color: the multisampled ones were all seen clearing to 0. A single-sampled one takes what Direct3D's clear
    // shader writes, the pixel shader's c0 (RGBA8 for the renderer's clear).
    uint32_t color = 0;
    // XMOW: Wolverine's Direct3D clear pixel shader (n4567, from the XEX) writes the rectangle's vertex color, not c0:
    // its c0 keeps the engine's (8 0 0 0) and the clears painted red (000000FF) into the 352x182 bloom target and the
    // scene's 8-bit targets (test 13). The vertex color when the game's own rectangle is used (xmow_clear_color_vertice).
    const bool vertex_color = game_rect && game_vertices && REXCVAR_GET(xmow_clear_color_vertice);
    if (!msaa) {
      for (uint32_t j = 0; j < 4; ++j) {
        float v;
        std::memcpy(&v, &registers_[0x4400 + j], 4);
        if (vertex_color) v = std::isfinite(game_color[j]) ? game_color[j] : 0.0f;
        color |= uint32_t(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f)) << (j * 8);
      }
    }
    // A k_16_16 target: the clear rectangle's vertex color (its pixel shader's c0 holds 8, not the color: as the color,
    // the velocity buffer was filled with "a moving object everywhere" and the whole screen smeared) as the target's
    // EDRAM bits (16-bit fixed point, value / 32 * 32767), passed as such (bit 31 of RB_COPY_CONTROL, SDK patch 0040).
    bool color_as_bits = false;
    if (!msaa && ((base.rb_color_info[0] >> 16) & 0xF) == 4) {
      color = 0;
      for (uint32_t j = 0; j < 2; ++j) {
        const float v = game_vertices && std::isfinite(game_color[j]) ? game_color[j] : 0.0f;
        const int32_t q = int32_t(std::lround(std::clamp(v / 32.0f, -1.0f, 1.0f) * 32767.0f));
        color |= uint32_t(uint16_t(int16_t(q))) << (j * 16);
      }
      color_as_bits = true;
    }
    if (c0_written) {
      rex::x360::RegistrosCopia c = base;
      c.rb_copy_control = 0u | (1u << 8) | kNullCommand | (color_as_bits ? 1u << 31 : 0u);
      c.rb_color_clear = color;
      c.rb_color_clear_lo = 0;
      CopiarMedido(c);
      if (describir) {
        what += fmt::format(" c0={:03X}", c0_base);
        if (color) what += fmt::format("={:08X}", color);
      }
    }
    uint32_t aliased_depth[3] = {};
    uint32_t n_aliased = 0;
    for (uint32_t i = 1; i < 4 && !depth_only; ++i) {
      const uint32_t b = base.rb_color_info[i] & 0xFFF;
      if (!((mask >> (i * 4)) & 0xF) || (c0_written && b == c0_base)) continue;
      if (std::find(aliased_depth, aliased_depth + n_aliased, b) != aliased_depth + n_aliased) continue;
      aliased_depth[n_aliased++] = b;
      clear_depth_at(b, what);
    }
    const uint32_t z_base = depth_info & 0xFFF;
    const bool z_written = depth_only || (depth_control & 0x4) || (depth_control & 0x1);
    if (z_written && !(c0_written && z_base == c0_base) &&
        std::find(aliased_depth, aliased_depth + n_aliased, z_base) == aliased_depth + n_aliased) {
      clear_depth_at(z_base, what, stencil_only);
      // Depth written over c0's EDRAM (Dead Space 2's light buffer clear: depth-only mode, depth target on 2D0 where
      // the light buffer is): on the console the same EDRAM bytes are the color target's, so the color is cleared by
      // it (depth 0 and stencil 0 there: black). The renderer keeps depth and color in separate images, so the color
      // image is cleared too; without it the lights were added on top of the normals left there by the G-buffer.
      if (z_base == c0_base && (mask & 0xF) && !stencil_only) {
        // The color those EDRAM bits are (Gears of War 2 clears its ambient occlusion buffer, 16_16_FLOAT, this way:
        // with 0 the pixels it masks out, the player's character among them, were multiplied to black, and the
        // occlusion's temporal filter trusted that black history and dragged it behind moving things).
        uint32_t bits = 0;
        if (game_vertices) {
          const bool d24fs = (depth_info >> 16) & 0x1;
          const float z = std::clamp(game_z, 0.0f, d24fs ? 1.99999988f : 1.0f);
          const uint32_t d24 = d24fs ? xenos::Float32To20e4(z, true) : uint32_t(z * 16777215.0f + 0.5f);
          bits = (d24 << 8) | (stencil & 0xFF);
        }
        rex::x360::RegistrosCopia c = base;
        // Bit 31 (unused by the Xenos, like bit 30 in SDK patch 0030): RB_COLOR_CLEAR holds the target's EDRAM bits.
        c.rb_copy_control = 0u | (1u << 8) | kNullCommand | (1u << 31);
        c.rb_color_clear = bits;
        c.rb_color_clear_lo = bits;
        CopiarMedido(c);
        if (describir) what += fmt::format(" c0={:03X}={:08X} (by the depth over it)", c0_base, bits);
      }
    }
    ++clears_;
    if (describir && clear_shapes_.insert(what).second && clear_shapes_.size() <= 32) {
      REXLOG_INFO("[gpu] clear shape: msaa {}, mode {}:{}", msaa, r[g::XE_GPU_REG_RB_MODECONTROL] & 0x7, what);
    }
    // Single-sampled ones counted apart: they are new, and their game clear color (PS c0) tells whether 0 is right.
    if (msaa ? clears_logged_ < 12 : clears_logged_1x_ < 8) {
      ++(msaa ? clears_logged_ : clears_logged_1x_);
      auto f = [&](uint32_t index) {
        float v;
        std::memcpy(&v, &registers_[index], 4);
        return v;
      };
      REXLOG_INFO("[gpu] clear draw (msaa {}, pitch {}):{}; game had: PS c0 ({:.3f} {:.3f} {:.3f} {:.3f}) VS c0 "
                  "({:.3f} {:.3f} {:.3f} {:.3f}) RB_COLOR_CLEAR {:08X} RB_DEPTH_CLEAR {:08X} depthcontrol {:08X} "
                  "mask {:04X} vertex z {:.4f} color ({:.3f} {:.3f} {:.3f} {:.3f}){}",
                  msaa, Register(g::XE_GPU_REG_RB_SURFACE_INFO) & 0x3FFF, what, f(0x4400), f(0x4401), f(0x4402),
                  f(0x4403), f(0x4000), f(0x4001), f(0x4002), f(0x4003), Register(g::XE_GPU_REG_RB_COLOR_CLEAR),
                  Register(g::XE_GPU_REG_RB_DEPTH_CLEAR), depth_control, mask, game_z, game_color[0], game_color[1],
                  game_color[2], game_color[3], vertex_color ? " (vertex color used)" : "");
    }
  }

  // The frame just presented as a 24-bit BMP in capturas/ next to the NRO (ZL + ZR + Down): what is on screen, to look
  // at rendering problems without describing them.
  // Captures every gow2_auto_captura_s and frame dumps on their schedule (seconds since the first Swap).
  void AutomaticCollection() {
    if (!telemetry_) return;
    const auto now = Clock::now();
    if (first_swap_ == Clock::time_point{}) return;
    const double t = std::chrono::duration<double>(now - first_swap_).count();
    const int32_t every = REXCVAR_GET(gow2_auto_captura_s);
    if (ab_cuenta_) {
      // xmow_captura_ab: the frame without the group, a few frames after it was left out (then drawn again).
      if (--ab_cuenta_ == 0) {
        const std::string sufijo = ab_grupo_ == 3   ? "p" + std::to_string(ab_ps_actual_)
                                   : ab_grupo_ == 5 ? std::string("ta")
                                   : ab_grupo_ == 6 ? std::string("to")
                                                    : std::string("tr");
        SaveCapture(true, sufijo.c_str());
        if (auto_captures_ % 12 == 1 || ab_turno_ <= 3) {
          REXLOG_INFO("[gpu] capture ab: group {} left out ({} draws so far); xmow_omitir_ps: {} draws left out",
                      sufijo, ab_omitidos_, omitidos_ps_);
        }
        ab_grupo_ = 0;
      }
    } else if (every > 0 && t >= next_auto_capture_s_) {
      next_auto_capture_s_ = t + every;
      SaveCapture(true);
      if (REXCVAR_GET(xmow_captura_ab) && last_frame_draws_ >= 300) {
        ab_grupo_ = (ab_turno_++ % 2) ? 6 : 5;  // alternates alpha-blended (ta) and other translucents (to)
        if (ab_grupo_ == 3) {
          EnListaAb(0);
          if (ab_ps_.empty()) {
            ab_grupo_ = 1;
          } else {
            ab_ps_actual_ = ab_ps_[ab_ps_turno_++ % ab_ps_.size()];
          }
        }
        ab_cuenta_ = 4;
      }
    }
    const int32_t max_dumps = REXCVAR_GET(gow2_auto_volcados_max);
    if (auto_dumps_ < uint32_t(std::max(max_dumps, 0))) {
      if (next_auto_dump_s_ < 0) next_auto_dump_s_ = REXCVAR_GET(gow2_auto_volcado_primero_s);
      // Only a frame of the game itself (the 3D scene: hundreds of draws), not a menu or loading screen: dumps by
      // time alone caught Gears of War 2's menus (270 draws) while the problem was in the level.
      if (t >= next_auto_dump_s_ && last_frame_draws_ >= uint64_t(std::max(REXCVAR_GET(gow2_auto_volcado_dibujos), 0))) {
        ++auto_dumps_;
        const int32_t gap = REXCVAR_GET(gow2_auto_volcado_cada_s);
        next_auto_dump_s_ = gap > 0 ? t + gap : 1e30;
        REXLOG_INFO("[gpu] automatic frame dump {} at {:.0f} s ({} draws in the previous frame)", auto_dumps_, t,
                    last_frame_draws_);
        destinos_->PedirVolcado();
        // XMOW: only the textures and the pass trace; the draw-by-draw trace (L3 + R3) froze the game for ~35 s each
        // time on the SD, and the automatic ones followed each other in the loading screens.
      }
    }
  }

  void SaveCapture(bool small = false, const char* sufijo = nullptr) {
    rex::ui::RawImage image;
    if (!presenter_ || !presenter_->CaptureGuestOutput(image) || !image.width || !image.height) {
      REXLOG_WARN("[gpu] capture: no guest output to capture");
      return;
    }
    const auto folder = rex::filesystem::GetExecutableFolder() / "capturas";
    std::error_code ec;
    std::filesystem::create_directories(folder, ec);
    char name[32];
    std::filesystem::path path;
    if (small) {
      if (sufijo && *sufijo) {
        // The A/B partner of the last automatic capture: the same number.
        std::snprintf(name, sizeof(name), "auto_%04u_%s.bmp", auto_captures_ ? auto_captures_ - 1 : 0, sufijo);
      } else {
        std::snprintf(name, sizeof(name), "auto_%04u.bmp", auto_captures_++);
      }
      path = folder / name;
      // Half size: each 2x2 block averaged into the top-left texel, then compacted (0.7 MB instead of 2.7 MB).
      const uint32_t w = image.width / 2, h = image.height / 2;
      std::vector<uint8_t> half(size_t(w) * h * 4);
      for (uint32_t y = 0; y < h; ++y) {
        for (uint32_t x = 0; x < w; ++x) {
          const uint8_t* a = image.data.data() + size_t(2 * y) * image.stride + size_t(2 * x) * 4;
          const uint8_t* b = a + image.stride;
          for (int k = 0; k < 4; ++k) half[(size_t(y) * w + x) * 4 + k] = uint8_t((a[k] + a[k + 4] + b[k] + b[k + 4] + 2) / 4);
        }
      }
      image.data = std::move(half);
      image.width = w;
      image.height = h;
      image.stride = size_t(w) * 4;
    } else {
      // The first free number once, then counted: the file is written later, so looking for it again found the same
      // free name and captures taken close together overwrote each other.
      if (next_capture_ == UINT32_MAX) {
        for (next_capture_ = 0; next_capture_ < 1000; ++next_capture_) {
          std::snprintf(name, sizeof(name), "cap_%03u.bmp", next_capture_);
          if (!std::filesystem::exists(folder / name, ec)) break;
        }
      }
      std::snprintf(name, sizeof(name), "cap_%03u.bmp", next_capture_++);
      path = folder / name;
    }
    const uint32_t row = image.width * 3;  // 1280 * 3 is a multiple of 4; others are padded below
    const uint32_t padded = (row + 3) & ~3u;
    const uint32_t pixels = padded * image.height;
    uint8_t header[54] = {'B', 'M'};
    auto put32 = [&](size_t at, uint32_t v) { std::memcpy(header + at, &v, 4); };
    put32(2, 54 + pixels);
    put32(10, 54);
    put32(14, 40);
    put32(18, image.width);
    put32(22, image.height);
    header[26] = 1;
    header[28] = 24;
    put32(34, pixels);
    std::vector<uint8_t> data(pixels, 0);
    for (uint32_t y = 0; y < image.height; ++y) {
      const uint8_t* s = image.data.data() + size_t(image.height - 1 - y) * image.stride;  // BMP rows go bottom-up
      uint8_t* d = data.data() + size_t(y) * padded;
      for (uint32_t x = 0; x < image.width; ++x, s += 4, d += 3) {
        d[0] = s[2];
        d[1] = s[1];
        d[2] = s[0];
      }
    }
    // One write, on a thread of its own: small writes to the SD card take 10-50 ms each, and the ring thread must not
    // wait for them (the game stalls with it).
    std::vector<uint8_t> file(header, header + sizeof(header));
    file.insert(file.end(), data.begin(), data.end());
    const bool log = !small || auto_captures_ % 12 == 1;  // the automatic ones once a minute in the log
    rex::x360::EncolarEscrituraSd([path, file = std::move(file), log, w = image.width, h = image.height]() {
      // xmow 1031: in pieces with pauses (xmow_volcado_trozo_kb / xmow_volcado_pausa_ms), as the dumps.
      if (rex::x360::EscribirArchivoEnTrozos(path, file.data(), file.size())) {
        if (log) REXLOG_INFO("[gpu] capture: {} ({}x{})", path.string(), w, h);
      } else {
        REXLOG_WARN("[gpu] capture: could not write {}", path.string());
      }
    });
  }

  // 0.1.27 (xmow_precarga_mapa): at each Swap on the ring thread. The kernel names every UE3 package the game opens
  // (sdk-g2 xboxkrnl_io.cpp, g_xmow_paquete_ue3); a persistent level ("<map>_p.xxx") makes it the current map. Before
  // the first one the map is "frontend". On a change the shader library and the renderer save what they read from the
  // card for the previous map and start reading the new one's list in the background (its loading screen is drawn
  // meanwhile); every 30 s the current map's lists are saved again if they grew.
  void MapaPorSwap(Clock::time_point agora) {
    if (!REXCVAR_GET(xmow_precarga_mapa)) return;
    std::string mapa;
    {
      std::lock_guard<std::mutex> l(MapaPendienteCandado());
      mapa = MapaPendiente();
    }
    if (mapa.empty()) mapa = "frontend";
    const bool cambio = mapa != mapa_actual_;
    if (!cambio && agora - mapa_guardado_ < std::chrono::seconds(30)) return;
    mapa_guardado_ = agora;
    if (cambio) {
      REXLOG_INFO("[xmow map] current map: {} (was {})", mapa, mapa_actual_.empty() ? "none" : mapa_actual_);
      mapa_actual_ = mapa;
    }
    const std::filesystem::path carpeta = rex::filesystem::GetExecutableFolder() / "xmow_precarga";
    gow2::shaders::Global().CambioDeMapa(mapa, carpeta);
    if (destinos_) destinos_->MapaActual(mapa, carpeta);
  }

  // xmow_log_hitch_ms: called at each Swap on the ring thread, after FrameTraceSwap (last_frame_draws_ is the frame that
  // ended). The line is only formatted into hitch_linhas_; Report writes them with the 10 s summary.
  void HitchPorSwap() {
    const auto agora = Clock::now();
    const auto anterior = hitch_ultimo_swap_;
    hitch_ultimo_swap_ = agora;
    MapaPorSwap(agora);
    const uint64_t caixas = frame_caixas_;
    frame_caixas_ = 0;
    const int32_t limite_ms = REXCVAR_GET(xmow_log_hitch_ms);
    if (limite_ms <= 0 || !destinos_) {
      return;
    }
    if (xmow::bench::ModoJogoEfetivo()) {  // 0.1.34: PLAY MODE, no slow frame lines (the deltas start again after it)
      hitch_ressincronizar_ = true;
      return;
    }
    const rex::x360::EstadisticasDibujos est = destinos_->EstadisticasDeDibujos();
    const uint64_t subidas = est.subidas_textura;
    // xmow 1031: the ring thread's time in this frame, by what it did (deltas of accumulators since the last Swap).
    const TiempoAnillo a = anillo_;
    TiempoAnillo d;
    d.ocioso = a.ocioso - anillo_previo_.ocioso;
    d.informe = a.informe - anillo_previo_.informe;
    d.sondeos = a.sondeos - anillo_previo_.sondeos;
    d.paquetes = a.paquetes - anillo_previo_.paquetes;
    d.dibujar = a.dibujar - anillo_previo_.dibujar;
    d.copiar = a.copiar - anillo_previo_.copiar;
    d.presentar = a.presentar - anillo_previo_.presentar;
    d.subidas = a.subidas - anillo_previo_.subidas;
    anillo_previo_ = a;
    const uint64_t r_informe = est.ns_informe - hitch_r_previo_[0], r_volcado = est.ns_volcado - hitch_r_previo_[1],
                   r_cache = est.ns_lectura_cache - hitch_r_previo_[2],
                   r_valla = est.ns_espera_valla - hitch_r_previo_[3],
                   r_grabando = est.ns_grabando - hitch_r_previo_[4];
    hitch_r_previo_ = {est.ns_informe, est.ns_volcado, est.ns_lectura_cache, est.ns_espera_valla, est.ns_grabando};
    // 0.1.26 (SDK 1032): GPU busy (works minus waits for the next partial submit) of the works whose fence passed since
    // the last Swap: the GPU work of a frame 2-3 frames back, not this one's.
    const uint64_t gpu_ocupada = est.ns_gpu_trabajos > est.ns_gpu_espera_envio ? est.ns_gpu_trabajos - est.ns_gpu_espera_envio : 0;
    const uint64_t gpu_ocupada_quadro = gpu_ocupada > hitch_gpu_prev_ns_ ? gpu_ocupada - hitch_gpu_prev_ns_ : 0;
    hitch_gpu_prev_ns_ = gpu_ocupada;
    const uint64_t espera_d3d_us = xmow::perf::EsperaD3dTotalUs();
    if (reg_mem_wait_ns_ < hitch_espera_anillo_prev_) hitch_espera_anillo_prev_ = 0;  // Report zeroed it
    const uint64_t texturas = subidas - hitch_subidas_prev_;
    const double espera_anillo_ms = double(reg_mem_wait_ns_ - hitch_espera_anillo_prev_) / 1e6;
    const double espera_d3d_ms = double(espera_d3d_us - hitch_espera_d3d_prev_us_) / 1000.0;
    hitch_subidas_prev_ = subidas;
    hitch_espera_anillo_prev_ = reg_mem_wait_ns_;
    hitch_espera_d3d_prev_us_ = espera_d3d_us;
    // 0.1.27: the shader library's SPIR-V reads (xmow_spirv_lectura_fondo) in this frame.
    const gow2::shaders::Biblioteca::Lecturas lec = gow2::shaders::Global().lecturas();
    const uint64_t spirv_esperas = spirv_no_lista_ - hitch_spirv_prev_[0];
    const uint64_t spirv_en_el_acto = lec.ns_en_el_acto - hitch_spirv_prev_[1];
    const uint64_t spirv_fondo = lec.ns_fondo - hitch_spirv_prev_[2];
    hitch_spirv_prev_ = {spirv_no_lista_, lec.ns_en_el_acto, lec.ns_fondo};
    if (anterior == Clock::time_point{} || std::exchange(hitch_ressincronizar_, false)) {
      return;
    }
    const double ms = std::chrono::duration<double, std::milli>(agora - anterior).count();
    if (ms <= double(limite_ms)) {
      return;
    }
    if (hitch_linhas_.size() >= 20) {
      ++hitch_suprimidas_;
      return;
    }
    const double desde_o_inicio =
        first_swap_ == Clock::time_point{} ? 0.0 : std::chrono::duration<double>(agora - first_swap_).count();
    // xmow 1031: where the ring thread's time went (HELICOPTERO-opus.md section 4). "idle" is ring_cv_ with no PM4 to
    // read (the game gave no work: the stall is elsewhere); "PM4 parse" is the packet loop minus draws, copies,
    // present and WAIT_REG_MEM; "draws" includes the renderer's recording, its DKSH reads from the SD card, its waits for
    // a work slot's fence and its 10 s report when a partial submit ran it; "present" includes the end of a frame dump.
    const auto m = [](uint64_t ns) { return double(ns) / 1e6; };
    const uint64_t reg_mem_ns = uint64_t(std::max(espera_anillo_ms, 0.0) * 1e6);
    const uint64_t dentro = d.dibujar + d.copiar + d.presentar + reg_mem_ns;
    const uint64_t parse = d.paquetes > dentro ? d.paquetes - dentro : 0;
    const uint64_t contado = d.ocioso + d.informe + d.sondeos + d.paquetes + d.subidas;
    const double sin_contar = std::max(0.0, ms - m(contado));
    hitch_linhas_.push_back(fmt::format(
        "[xmow hitch] at {:.1f} s: frame {:.1f} ms, {} draws, {} query boxes skipped, {} textures uploaded, ring wait "
        "{:.1f} ms, game Direct3D spin wait {:.1f} ms; ring: idle {:.1f}, PM4 parse {:.1f}, draws {:.1f} (recording "
        "{:.1f}, DKSH reads from the SD {:.1f}, slot fence waits {:.1f}), copies {:.1f}, present {:.1f} (dump end "
        "{:.1f}), renderer 10 s report and cache save {:.1f}, gpu 10 s report {:.1f}, polls {:.1f}, upload waits {:.1f}, "
        "not accounted {:.1f} ms; GPU busy {:.1f} ms (works read back during this frame); SPIR-V: {} draws skipped "
        "waiting for it, read by the ring or the create hooks {:.1f} ms, by the reader thread {:.1f} ms",
        desde_o_inicio, ms, last_frame_draws_, caixas, texturas, espera_anillo_ms, espera_d3d_ms, m(d.ocioso), m(parse),
        m(d.dibujar), m(r_grabando), m(r_cache), m(r_valla), m(d.copiar), m(d.presentar), m(r_volcado), m(r_informe),
        m(d.informe), m(d.sondeos), m(d.subidas), sin_contar, m(gpu_ocupada_quadro), spirv_esperas, m(spirv_en_el_acto),
        m(spirv_fondo)));
  }

  // 0.1.22: one entry per (vs, ps, primitive, color target 0, depth target) of the L3 + R3 frame, every draw counted
  // (not limited by draw_trace_cap_). Same identifiers as the [draws] debug line.
  // 0.1.26 (item 1c): the key also has target 0's blend (RB_BLENDCONTROL0), the color write mask, the depth/stencil
  // enables (RB_DEPTHCONTROL bits 0-2), the mode and the class (ClaseDelDibujo), and the log gives per-pass totals by
  // class: the same mesh with several pixel shaders is UE3's base pass plus one additive pass per dynamic light, or not.
  void DrawSumAdd(uint8_t clase) {
    namespace g = rex::graphics;
    const uint32_t init = Register(g::XE_GPU_REG_VGT_DRAW_INITIATOR);
    const uint64_t vs = uint64_t(vs_ ? int(vs_->numero) + 1 : 0) & 0xFFFF;
    const uint64_t ps = uint64_t(ps_ ? int(ps_->numero) + 1 : 0) & 0xFFFF;
    const uint64_t c0 = Register(g::XE_GPU_REG_RB_COLOR_INFO) & 0xFFF;
    const uint64_t z = Register(g::XE_GPU_REG_RB_DEPTH_INFO) & 0xFFF;
    const uint64_t chave = vs | (ps << 16) | (uint64_t(init & 0x3F) << 32) | (c0 << 38) | (z << 50);
    const uint64_t modos = uint64_t(Register(g::XE_GPU_REG_RB_BLENDCONTROL0)) |
                           (uint64_t(Register(g::XE_GPU_REG_RB_COLOR_MASK) & 0xFFFF) << 32) |
                           (uint64_t(Register(g::XE_GPU_REG_RB_DEPTHCONTROL) & 0x7) << 48) |
                           (uint64_t(Register(g::XE_GPU_REG_RB_MODECONTROL) & 0x7) << 51) | (uint64_t(clase) << 56);
    DrawSumEntry& e = draw_sum_[{chave, modos}];
    ++e.count;
    e.sum_n += init >> 16;
    ++draw_sum_total_;
  }
  void DrawSumLog() {
    struct Linha { std::pair<uint64_t, uint64_t> chave; DrawSumEntry e; };
    std::vector<Linha> v;
    v.reserve(draw_sum_.size());
    for (const auto& kv : draw_sum_) v.push_back({kv.first, kv.second});
    std::sort(v.begin(), v.end(), [](const Linha& a, const Linha& b) {
      return a.e.count != b.e.count ? a.e.count > b.e.count : a.chave < b.chave;
    });
    REXLOG_INFO("[draws sum] {} draws, {} distinct groups (vs, ps, prim, c0, z, blend, write mask, depth/stencil "
                "enables, mode); top {} by count", draw_sum_total_, v.size(), std::min<size_t>(v.size(), 80));
    for (size_t i = 0; i < v.size() && i < 80; ++i) {
      const uint64_t k = v[i].chave.first, m = v[i].chave.second;
      REXLOG_INFO("[draws sum] {}x vs{} ps{} prim {} c0 {:03X}/z {:03X} n {} | {} blend {:08X} mask {:04X} depthctl "
                  "{:X} mode {}",
                  v[i].e.count, int(k & 0xFFFF) - 1, int((k >> 16) & 0xFFFF) - 1, int((k >> 32) & 0x3F),
                  int((k >> 38) & 0xFFF), int((k >> 50) & 0xFFF), v[i].e.sum_n,
                  kNombresClase[std::min<uint32_t>(uint32_t(m >> 56), kClases - 1)], uint32_t(m), uint32_t(m >> 32) & 0xFFFF,
                  uint32_t(m >> 48) & 0x7, uint32_t(m >> 51) & 0x7);
    }
    // Per pass (c0/z target pair): draws by class.
    std::map<uint32_t, std::array<uint32_t, kClases + 1>> passes;  // (c0, z) -> per class, [kClases] = all
    std::array<uint32_t, kClases> total{};
    for (const auto& kv : draw_sum_) {
      const uint32_t clase = std::min<uint32_t>(uint32_t(kv.first.second >> 56), kClases - 1);
      auto& p = passes[uint32_t((kv.first.first >> 38) & 0xFFFFFF)];
      p[clase] += kv.second.count;
      p[kClases] += kv.second.count;
      total[clase] += kv.second.count;
    }
    const auto por_clase = [](const auto& a) {
      std::string s;
      for (uint32_t c = 0; c < kClases; ++c) {
        if (a[c]) s += fmt::format(" {} {},", kNombresClase[c], a[c]);
      }
      if (!s.empty()) s.pop_back();
      return s;
    };
    REXLOG_INFO("[draws sum] by class (target 0 blend):{}", por_clase(total));
    std::vector<std::pair<uint32_t, std::array<uint32_t, kClases + 1>>> p(passes.begin(), passes.end());
    std::sort(p.begin(), p.end(), [](const auto& a, const auto& b) {
      return a.second[kClases] != b.second[kClases] ? a.second[kClases] > b.second[kClases] : a.first < b.first;
    });
    REXLOG_INFO("[draws sum] draws per pass (c0/z target pair): {} pairs", p.size());
    for (const auto& e : p) {
      REXLOG_INFO("[draws sum] pass c0 {:03X}/z {:03X}: {} draws:{}", e.first & 0xFFF, e.first >> 12, e.second[kClases],
                  por_clase(e.second));
    }
    draw_sum_.clear();
    draw_sum_on_ = false;
  }
  // One line per draw of a traced light frame: what it draws with and what became of it.
  void DrawTraceLine(bool drawn, const char* why = nullptr) {
    namespace g = rex::graphics;
    if (draw_trace_n_ >= draw_trace_cap_) return;
    const uint32_t init = Register(g::XE_GPU_REG_VGT_DRAW_INITIATOR);
    REXLOG_DEBUG("[draws] {:3} prim {} n {} vs {} ps {} c0 {:03X}/f{} z {:03X} mask {:04X} blend0 {:08X} colorctl {:08X} "
                "depthctl {:08X} stencil {:08X} mode {} pitch {} msaa {} -> {}",
                draw_trace_n_++, init & 0x3F, init >> 16, vs_ ? int(vs_->numero) : -1, ps_ ? int(ps_->numero) : -1,
                Register(g::XE_GPU_REG_RB_COLOR_INFO) & 0xFFF, (Register(g::XE_GPU_REG_RB_COLOR_INFO) >> 16) & 0xF,
                Register(g::XE_GPU_REG_RB_DEPTH_INFO) & 0xFFF,
                Register(g::XE_GPU_REG_RB_COLOR_MASK), Register(g::XE_GPU_REG_RB_BLENDCONTROL0),
                Register(g::XE_GPU_REG_RB_COLORCONTROL), Register(g::XE_GPU_REG_RB_DEPTHCONTROL),
                Register(g::XE_GPU_REG_RB_STENCILREFMASK), Register(g::XE_GPU_REG_RB_MODECONTROL) & 0x7,
                Register(g::XE_GPU_REG_RB_SURFACE_INFO) & 0x3FFF, (Register(g::XE_GPU_REG_RB_SURFACE_INFO) >> 16) & 0x3,
                why ? why : drawn ? "drawn" : "REJECTED by the renderer");
    // XMOW: the details below only for the first 250 draws and for rectangles and quads (full-screen passes); each
    // line costs on the SD.
    if (draw_trace_n_ > 250 && (init & 0x3F) != 8 && (init & 0x3F) != 13 && (init & 0x3F) != 6) return;
    // The vertex input as D3D patched it: each fetch's format, swizzle, stride and offset. No vertex bytes: guest
    // memory is committed lazily, and even inside a vertex buffer the pages before the draw's first index may be
    // unmapped (reading vertex 0 crashed in game).
    for (size_t i = 0; i + 2 < vs_microcode_.size() && vs_; i += 3) {
      const uint32_t w0 = vs_microcode_[i], w1 = vs_microcode_[i + 1], w2 = vs_microcode_[i + 2];
      if ((w0 & 0x1F) != 0 || !((w0 >> 19) & 1)) continue;
      REXLOG_DEBUG("[draws]       vfetch r{} const {}.{} format {} swz {:03X} signed {} int {} stride {} offset {}",
                  (w0 >> 12) & 0x3F, (w0 >> 20) & 0x1F, (w0 >> 25) & 3, (w1 >> 16) & 0x3F, w1 & 0xFFF, (w1 >> 12) & 1,
                  (w1 >> 13) & 1, (w2 & 0xFF) * 4, ((w2 >> 8) & 0x7FFFFF) * 4);
    }
    // The pixel shader's textures: fetch constant of each sampler (base, format, size, dimension, swizzle, signs).
    // Every sampler (blended materials use up to 8: S4-S7 were not shown), with the raw words for the mip range.
    for (size_t i = 0; ps_ && i < ps_->samplers.size() && i < 16; ++i) {
      const uint32_t reg = ps_->samplers[i].registro;
      if (reg >= 32) continue;
      const uint32_t* f = &registers_[0x4800 + reg * 6];
      REXLOG_DEBUG("[draws]       s{} tipo {}: base {:08X} fmt {} {}x{} dim {} swz {:03X} signs {:02X} tiled {} type {} "
                  "words {:08X} {:08X} {:08X} {:08X} {:08X} {:08X}",
                  reg, ps_->samplers[i].tipo, f[1] & 0xFFFFF000u, f[1] & 0x3F, (f[2] & 0x1FFF) + 1,
                  ((f[2] >> 13) & 0x1FFF) + 1, (f[5] >> 9) & 0x3, (f[3] >> 1) & 0xFFF, (f[0] >> 2) & 0xFF,
                  (f[0] >> 31) & 1, f[0] & 0x3, f[0], f[1], f[2], f[3], f[4], f[5]);
    }
    // The pixel shader's first float constants (a material without textures shows its colour here).
    if (ps_) {
      std::string pc;
      for (uint32_t c = 0; c < 16; ++c) {
        const uint32_t r = 0x4000 + (256 + c) * 4;
        pc += fmt::format(" c{}=({:g} {:g} {:g} {:g})", c, std::bit_cast<float>(registers_[r]),
                          std::bit_cast<float>(registers_[r + 1]), std::bit_cast<float>(registers_[r + 2]),
                          std::bit_cast<float>(registers_[r + 3]));
      }
      REXLOG_DEBUG("[draws]       ps constants{}", pc);
    }
    // The vertex shader's first float constants (transform / fog parameters).
    if (vs_) {
      std::string vc;
      for (uint32_t c = 0; c < 16; ++c) {
        const uint32_t r = 0x4000 + c * 4;
        vc += fmt::format(" c{}=({:g} {:g} {:g} {:g})", c, std::bit_cast<float>(registers_[r]),
                          std::bit_cast<float>(registers_[r + 1]), std::bit_cast<float>(registers_[r + 2]),
                          std::bit_cast<float>(registers_[r + 3]));
      }
      REXLOG_DEBUG("[draws]       vs constants{}", vc);
    }
  }

  // --- Frame trace -----------------------------------------------------------
  //
  // How the game builds a heavy frame, once a minute up to three times: each pass (a run of draws into the same
  // targets with the same window offset), each resolve and the Swap, one line each. This is what is needed to
  // compare the frame with what the renderer does (multisampling, predicated tiling, EDRAM bases).

  struct FrameState {
    uint64_t draws = 0;              // geometry draws in the frame being counted
    uint64_t predicated_skipped = 0;
  };

  static constexpr uint32_t kTraceMaxLines = 400;

  void FrameTraceSwap() {
    const auto now = Clock::now();
    if (tracing_) {
      FrameTraceClosePass();
      REXLOG_INFO("[frame] end: {} geometry draws, {} predicated packets skipped, {} passes, swap frontbuffer "
                  "fetch0 {:08X} {:08X}",
                  frame_.draws, frame_.predicated_skipped, trace_pass_, Register(0x4800), Register(0x4801));
      tracing_ = false;
      ++traces_done_;
      next_trace_ = now + std::chrono::seconds(std::max(REXCVAR_GET(gow2_traza_cada_s), 1));
    } else if (telemetry_ && traces_done_ < uint32_t(std::max(REXCVAR_GET(gow2_trazas_max), 0)) && frame_.draws >= 800 &&
               now >= next_trace_ && now - first_swap_ >= std::chrono::seconds(60)) {
      tracing_ = true;
      trace_lines_ = 0;
      trace_pass_ = 0;
      trace_key_.fill(0);
      trace_pass_draws_ = 0;
      REXLOG_INFO("[frame] trace {} of a frame ({} draws in the previous one)", traces_done_ + 1, frame_.draws);
    }
    // Per-draw trace of a light frame (loading screens, menus: the interface), with telemetry.
    if (probing_) {
      probing_ = false;  // its draws' counts come back in ProbePoll
      REXLOG_INFO("[probe] end of the probed frame: {} base-pass draws", probes_.size());
    }
    // XMOW 1019: the probe starts two Swaps after the traced frame ends. The frame right after the trace is the one
    // the L3 + R3 dump writes to the SD (PedirVolcado); probed there, every base-pass draw went only into the 8x8 probe
    // square and the dumps of tests 22-24 showed the scene depth empty except that square (700,650)-(708,658).
    if (probe_delay_ && --probe_delay_ == 0) {
      probing_ = true;
      probes_.clear();
      probe_started_ = now;
      REXLOG_INFO("[probe] probed frame starts (square {},{}), after the dumped one", probe_x_, probe_y_);
    }
    if (draw_tracing_) {
      draw_tracing_ = false;
      REXLOG_INFO("[draws] end of the traced frame");
      if (draw_sum_on_) DrawSumLog();
      // The next frame: which base-pass draw paints the probe square (gow2_sonda).
      unsigned px = 0, py = 0;
      if (destinos_ && std::sscanf(REXCVAR_GET(gow2_sonda).c_str(), "%u,%u", &px, &py) == 2) {
        // XMOW 1019: not this next frame (the dumped one): two Swaps later (probe_delay_ above).
        probe_x_ = px;
        probe_y_ = py;
        probe_delay_ = 2;
      }
    } else if (telemetry_ && draw_traces_ < 8 && frame_.draws >= 10 && frame_.draws <= 400 && now >= next_draw_trace_) {
      draw_tracing_ = true;
      ++draw_traces_;
      draw_trace_n_ = 0;
      draw_trace_cap_ = 400;
      next_draw_trace_ = now + std::chrono::seconds(20);
      REXLOG_INFO("[draws] trace {} of a light frame ({} draws in the previous one)", draw_traces_, frame_.draws);
    }
    if (first_swap_ == Clock::time_point{}) first_swap_ = now;
    last_frame_draws_ = frame_.draws;
    frame_ = {};
  }

  void FrameTraceLine(const std::string& line) {
    if (trace_lines_++ < kTraceMaxLines) REXLOG_DEBUG("[frame] {}", line);
  }

  std::array<uint32_t, 9> FrameTraceKey() const {
    namespace g = rex::graphics;
    return {Register(g::XE_GPU_REG_RB_SURFACE_INFO), Register(g::XE_GPU_REG_RB_COLOR_INFO),
            Register(g::XE_GPU_REG_RB_COLOR1_INFO),  Register(g::XE_GPU_REG_RB_COLOR2_INFO),
            Register(g::XE_GPU_REG_RB_COLOR3_INFO),  Register(g::XE_GPU_REG_RB_DEPTH_INFO),
            Register(g::XE_GPU_REG_PA_SC_WINDOW_OFFSET), Register(g::XE_GPU_REG_RB_MODECONTROL),
            Register(g::XE_GPU_REG_RB_COLOR_MASK)};
  }

  void FrameTraceClosePass() {
    if (!trace_pass_draws_) return;
    const auto& k = trace_key_;
    std::string colors;
    for (uint32_t i = 0; i < 4; ++i) {
      if ((k[8] >> (i * 4)) & 0xF) {
        colors += fmt::format(" c{}={:03X}/f{}", i, k[1 + i] & 0xFFF, (k[1 + i] >> 16) & 0xF);
      }
    }
    FrameTraceLine(fmt::format(
        "pass {}: {} draws ({} unknown VS), pitch {} msaa {}{} z={:03X}/f{} window ({},{}) mode {} "
        "scissor {:08X}-{:08X} vport x {:.1f}+{:.1f} y {:.1f}+{:.1f} bins {:X}/{:X} vtxoff {} depthctl {:08X}",
        trace_pass_, trace_pass_draws_, trace_pass_no_vs_, k[0] & 0x3FFF, (k[0] >> 16) & 0x3, colors, k[5] & 0xFFF,
        (k[5] >> 16) & 0x1, int32_t(k[6] << 17) >> 17, int32_t((k[6] >> 16) << 17) >> 17, k[7] & 0x7,
        trace_scissor_tl_, trace_scissor_br_, trace_vport_[0], trace_vport_[1], trace_vport_[2], trace_vport_[3],
        bin_select_, bin_mask_, (trace_sc_mode_ >> 16) & 0x1, trace_depthctl_));
    ++trace_pass_;
    trace_pass_draws_ = 0;
    trace_pass_no_vs_ = 0;
  }

  void FrameTraceDraw() {
    ++frame_.draws;
    if (!tracing_) return;
    const auto key = FrameTraceKey();
    if (key != trace_key_ || !trace_pass_draws_) {
      FrameTraceClosePass();
      trace_key_ = key;
      namespace g = rex::graphics;
      trace_scissor_tl_ = Register(g::XE_GPU_REG_PA_SC_WINDOW_SCISSOR_TL);
      trace_scissor_br_ = Register(g::XE_GPU_REG_PA_SC_WINDOW_SCISSOR_BR);
      trace_sc_mode_ = Register(g::XE_GPU_REG_PA_SU_SC_MODE_CNTL);
      trace_depthctl_ = Register(g::XE_GPU_REG_RB_DEPTHCONTROL);
      for (uint32_t i = 0; i < 4; ++i) {
        // PA_CL_VPORT_XSCALE, XOFFSET, YSCALE, YOFFSET
        const uint32_t v = Register(g::XE_GPU_REG_PA_CL_VPORT_XSCALE + i);
        std::memcpy(&trace_vport_[i], &v, 4);
      }
    }
    ++trace_pass_draws_;
    trace_pass_no_vs_ += vs_ == nullptr;
  }

  // Every distinct resolve, once, when it is first seen (up to 200): the effects (kinesis, hits, screen blood) show up
  // with the time they start, to compare with what is on screen.
  void ResolveCatalog() {
    namespace g = rex::graphics;
    if (resolve_seen_.size() >= 200) return;
    const uint32_t control = Register(g::XE_GPU_REG_RB_COPY_CONTROL);
    const uint32_t source = control & 0x7;
    static constexpr uint32_t kColorInfo[4] = {g::XE_GPU_REG_RB_COLOR_INFO, g::XE_GPU_REG_RB_COLOR1_INFO,
                                               g::XE_GPU_REG_RB_COLOR2_INFO, g::XE_GPU_REG_RB_COLOR3_INFO};
    const uint32_t src_info = source < 4 ? Register(kColorInfo[source]) : Register(g::XE_GPU_REG_RB_DEPTH_INFO);
    const uint32_t surface = Register(g::XE_GPU_REG_RB_SURFACE_INFO);
    const uint32_t tl = Register(g::XE_GPU_REG_PA_SC_WINDOW_SCISSOR_TL), br = Register(g::XE_GPU_REG_PA_SC_WINDOW_SCISSOR_BR);
    const uint32_t dest = Register(g::XE_GPU_REG_RB_COPY_DEST_BASE), pitch = Register(g::XE_GPU_REG_RB_COPY_DEST_PITCH);
    const uint32_t info = Register(g::XE_GPU_REG_RB_COPY_DEST_INFO), offset = Register(g::XE_GPU_REG_PA_SC_WINDOW_OFFSET);
    // The rectangle D3D puts in fetch constant 0 (three vertices), as the renderer reads it.
    const uint32_t f0 = Register(0x4800), f1 = Register(0x4801);
    float rect[6] = {};
    if ((f0 & 0x3) == 3 && ((f1 >> 2) & 0xFFFFFF) == 6) {
      const uint8_t* v = memory_->TranslatePhysical(f0 & ~uint32_t(0x3));
      for (int i = 0; i < 6; ++i) {
        uint32_t w;
        std::memcpy(&w, v + i * 4, 4);
        w = xenos::GpuSwap(w, static_cast<xenos::Endian>(f1 & 0x3));
        std::memcpy(&rect[i], &w, 4);
      }
    }
    const uint64_t key = XXH3_64bits(std::array<uint32_t, 10>{control, src_info, surface, tl, br, dest, pitch, info,
                                                              offset, uint32_t(rect[0]) ^ uint32_t(rect[1]) << 16}
                                         .data(),
                                     40);
    if (!resolve_seen_.insert(key).second) return;
    REXLOG_DEBUG("[resolve] new: {} base {:03X} f{} pitch {} msaa {} scissor {:08X}-{:08X} window ({},{}) rect "
                "({:.0f},{:.0f})-({:.0f},{:.0f})-({:.0f},{:.0f}) -> {:08X} pitch {} height {} fmt {} swap {} control "
                "{:08X}",
                source < 4 ? fmt::format("c{}", source) : std::string("depth"), src_info & 0xFFF,
                (src_info >> 16) & 0xF, surface & 0x3FFF, (surface >> 16) & 0x3, tl, br, int32_t(offset << 17) >> 17,
                int32_t((offset >> 16) << 17) >> 17, rect[0], rect[1], rect[2], rect[3], rect[4], rect[5], dest,
                pitch & 0x3FFF, (pitch >> 16) & 0x3FFF, (info >> 7) & 0x3F, (info >> 24) & 0x1, control);
  }

  void FrameTraceCopy() {
    if (!tracing_) return;
    FrameTraceClosePass();
    namespace g = rex::graphics;
    const uint32_t control = Register(g::XE_GPU_REG_RB_COPY_CONTROL);
    const uint32_t source = control & 0x7;
    const uint32_t surface = Register(g::XE_GPU_REG_RB_SURFACE_INFO);
    static constexpr uint32_t kColorInfo[4] = {g::XE_GPU_REG_RB_COLOR_INFO, g::XE_GPU_REG_RB_COLOR1_INFO,
                                               g::XE_GPU_REG_RB_COLOR2_INFO, g::XE_GPU_REG_RB_COLOR3_INFO};
    const uint32_t src_info = source < 4 ? Register(kColorInfo[source]) : Register(g::XE_GPU_REG_RB_DEPTH_INFO);
    const uint32_t dest_info = Register(g::XE_GPU_REG_RB_COPY_DEST_INFO);
    const uint32_t dest_pitch = Register(g::XE_GPU_REG_RB_COPY_DEST_PITCH);
    const uint32_t offset = Register(g::XE_GPU_REG_PA_SC_WINDOW_OFFSET);
    FrameTraceLine(fmt::format(
        "resolve from {} base {:03X} f{} pitch {} msaa {} rect {:08X}-{:08X} window ({},{}) -> {:08X} pitch {} "
        "height {} fmt {} control {:08X} (copy {}, clear color {} depth {}) depth_clear {:08X} color_clear {:08X} "
        "vtxoff {}",
        source < 4 ? fmt::format("c{}", source) : std::string("depth"), src_info & 0xFFF, (src_info >> 16) & 0xF,
        surface & 0x3FFF, (surface >> 16) & 0x3, Register(g::XE_GPU_REG_PA_SC_WINDOW_SCISSOR_TL),
        Register(g::XE_GPU_REG_PA_SC_WINDOW_SCISSOR_BR), int32_t(offset << 17) >> 17,
        int32_t((offset >> 16) << 17) >> 17, Register(g::XE_GPU_REG_RB_COPY_DEST_BASE), dest_pitch & 0x3FFF,
        (dest_pitch >> 16) & 0x3FFF, (dest_info >> 7) & 0x3F, control, (control >> 20) & 0x3, (control >> 8) & 0x1,
        (control >> 9) & 0x1, Register(g::XE_GPU_REG_RB_DEPTH_CLEAR), Register(g::XE_GPU_REG_RB_COLOR_CLEAR),
        (Register(g::XE_GPU_REG_PA_SU_SC_MODE_CNTL) >> 16) & 0x1));
  }
#endif

  // EVENT_WRITE_ZPD, as the SDK's CommandProcessor::ExecutePacketType3_EVENT_WRITE_ZPD: D3D marks the
  // structure with 0xFFFFFEED on Issue(END) and GetData answers "not yet" while the mark is there. D3D's query is 64
  // bytes, the begin structure at +0x20 and the end one at +0 (the count is end minus begin, the begin one stays
  // zeroed). With gow2_oclusion_real the renderer counts on the GPU the samples drawn between the two; the end keeps
  // its mark until that count is back (OcclusionPoll), or gow2_oclusion_espera_ms passed (then it is faked visible).
  void OcclusionQuery(uint32_t address) {
    if (!address) {
      return;
    }
    const uint32_t kQueryFinished = rex::byte_swap(0xFFFFFEEDu);  // written big-endian, the struct is LE
    auto* counts = memory_->TranslatePhysical<xenos::xe_gpu_depth_sample_counts*>(address);
    const bool end_via_z_pass = counts->ZPass_A == kQueryFinished || counts->ZPass_B == kQueryFinished;
    const bool end_via_z_fail = counts->ZFail_A == kQueryFinished || counts->ZFail_B == kQueryFinished;
    const bool end = end_via_z_pass || end_via_z_fail;
    // What the game left there, for the first queries and every traced frame (Gears of War 2 culled its whole world:
    // whether its occlusion queries come back visible is the first thing to know).
    if (queries_logged_ < 48 || draw_tracing_) {
      ++queries_logged_;
      const uint32_t* w = reinterpret_cast<const uint32_t*>(counts);
      REXLOG_DEBUG("[query] ZPD at {:08X} ctl {:08X}: {:08X} {:08X} {:08X} {:08X} {:08X} {:08X} {:08X} {:08X} -> {}",
                  address, Register(rex::graphics::XE_GPU_REG_RB_SAMPLE_COUNT_CTL), w[0], w[1], w[2], w[3], w[4], w[5],
                  w[6], w[7], end ? "end" : "begin: zeroed");
    }
    const uint32_t query = address & ~0x3Fu;
    // A query issued again: whatever was still waited for at that structure is not written over it any more.
    std::erase_if(pending_queries_, [&](const PendingQuery& q) { return (q.address & ~0x3Fu) == query; });
    const bool real = REXCVAR_GET(gow2_oclusion_real) && destinos_;
    // xmow_pular_caixas_consulta: a query is open between its begin and its end (Draw drops the boxes drawn in
    // between). Real occlusion needs them drawn: they are what the GPU counts.
    query_aberta_ = !end && !real;
    // xmow_oclusao_conservadora: answered here, at once, from the box's history (OclusionConservadora).
    if (!real && destinos_ && REXCVAR_GET(xmow_oclusao_conservadora)) {
      OclusionConservadora(query, end, counts);
      return;
    }
    if (oc_abierta_) OcCerrarSuelta();  // the mode was switched off inside a query
    if (real && !end) {
      std::memset(counts, 0, sizeof(*counts));
      destinos_->EmpezarOclusion(query);
      return;
    }
    if (real) {
      if (const uint64_t ticket = destinos_->TerminarOclusionAsincrona(query)) {
        pending_queries_.push_back({address, ticket, std::chrono::steady_clock::now()});
        return;  // the mark stays: GetData waits
      }
    }
    std::memset(counts, 0, sizeof(*counts));
    // The test option: without D3D's end marker, every second ZPD is taken as a query's end (begin and end come in
    // pairs), so a query is visible even if the marker never shows up where we look.
    const bool end_by_pair = REXCVAR_GET(gow2_consultas_siempre_visibles) && (zpd_count_++ & 1);
    if (end || end_by_pair) {
      counts->ZPass_A = kFakeSampleCount;
      counts->Total_A = kFakeSampleCount;
    }
  }

  // The probe frame's counts: once all are back (or 3 s passed), the draws that painted the probe square, in order;
  // the last one is what shows there.
  void ProbePoll() {
    if (probes_.empty() || probing_) return;
    bool all = true;
    for (Probe& p : probes_) {
      if (p.samples == UINT64_MAX && p.ticket) {
        uint64_t n = 0;
        if (destinos_->ResultadoOclusion(p.ticket, n)) p.samples = n; else all = false;
      }
    }
    if (!all && std::chrono::steady_clock::now() - probe_started_ < std::chrono::seconds(3)) return;
    std::string text;
    const Probe* last = nullptr;
    for (const Probe& p : probes_) {
      if (p.samples != UINT64_MAX && p.samples > 0) {
        text += fmt::format(" #{} vs{} ps{} n{} ({})", p.index, p.vs, p.ps, p.count, p.samples);
        last = &p;
      }
    }
    REXLOG_INFO("[probe] square {},{}: {} base-pass draws painted it:{}", probe_x_, probe_y_,
                last ? "these" : "no", text);
    if (last) {
      REXLOG_INFO("[probe] the one that shows: #{} vs{} ps{} ({} vertices)", last->index, last->vs, last->ps,
                  last->count);
    }
    probes_.clear();
  }

  // The real queries' counts back to the game, in order; one waited for too long is answered visible.
  void OcclusionPoll() {
    if (pending_queries_.empty()) {
      return;
    }
    const auto now = std::chrono::steady_clock::now();
    const auto limit = std::chrono::milliseconds(std::max(REXCVAR_GET(gow2_oclusion_espera_ms), 0));
    const int32_t no_wait_ms = REXCVAR_GET(gow2_oclusion_sin_esperar_ms);
    const bool game_waits = g_gow2_query_waited.exchange(false, std::memory_order_acq_rel) && no_wait_ms >= 0;
    // The ring has been quiet for a while with a query waited for: the game may be waiting for one whose work is still
    // being recorded (it would never be submitted).
    if (destinos_ && now - pending_queries_.front().ended >= std::chrono::milliseconds(1) &&
        now - ring_activity_ >= std::chrono::milliseconds(1)) {
      destinos_->EnviarOclusiones();
    }
    size_t done = 0;
    for (; done < pending_queries_.size(); ++done) {
      const PendingQuery& q = pending_queries_[done];
      uint64_t samples = 0;
      const bool counted = destinos_ && destinos_->ResultadoOclusion(q.ticket, samples);
      const bool answer_now = game_waits && now - q.ended >= std::chrono::milliseconds(no_wait_ms);
      if (!counted && now - q.ended < limit && !answer_now) {
        break;  // the later ones are in later work
      }
      auto* counts = memory_->TranslatePhysical<xenos::xe_gpu_depth_sample_counts*>(q.address);
      const uint32_t n = counted ? uint32_t(std::min<uint64_t>(samples, INT32_MAX)) : uint32_t(kFakeSampleCount);
      counts->Total_A = n;
      counts->Total_B = 0;
      counts->ZFail_A = 0;
      counts->ZFail_B = 0;
      counts->StencilFail_A = 0;
      counts->StencilFail_B = 0;
      counts->ZPass_B = 0;
      std::atomic_thread_fence(std::memory_order_release);
      counts->ZPass_A = n;  // the mark goes last
      if (!counted) {
        ++(now - q.ended < limit ? queries_not_waited_ : queries_timed_out_);
      } else {
        ++queries_answered_;
        if (n == 0) ++queries_hidden_;
        queries_wait_us_ += uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(now - q.ended).count());
      }
    }
    pending_queries_.erase(pending_queries_.begin(), pending_queries_.begin() + done);
  }

  // --- xmow_oclusao_conservadora (0.1.21) -----------------------------------------------------------------------
  // UE3 recycles its query objects from a pool, so the structure address says nothing about which primitive a query
  // is for. The key is the box itself: a hash of the vertices of the first query box drawn between the begin and the
  // end (UE3's FOcclusionQueryBatcher writes each batch's bounding boxes as world-space corners in one draw, so a
  // static primitive, or an unchanged batch, gives the same bytes every frame; a moving one gets a new key and stays
  // visible). The game is answered at the end ZPD, at once, from the history; the GPU count of a box that was drawn
  // comes back later (OclusionConservadoraPoll) and only updates the history.

  // Begin and end ZPD of a query with the mode on (OcclusionQuery). The begin structure is zeroed as in the fake path;
  // the end one gets 1000 samples (visible) or 0 (hidden) and loses Direct3D's mark.
  void OclusionConservadora(uint32_t query, bool end, xenos::xe_gpu_depth_sample_counts* counts) {
    std::memset(counts, 0, sizeof(*counts));
    ++oc_.zpd_fotograma;
    if (!end) {
      OcCerrarSuelta();  // a query left open (its end never came): its count is read and dropped
      oc_abierta_ = true;
      oc_query_ = query;
      oc_decidida_ = oc_medir_ = oc_empezada_ = oc_pinta_ = false;
      oc_clave_ = 0;
      return;
    }
    uint32_t n = uint32_t(kFakeSampleCount);
    if (oc_abierta_ && oc_query_ == query) {
      const bool con_clave = oc_decidida_ && oc_clave_ != 0;
      const bool elegible = con_clave && !oc_pinta_;
      if (oc_empezada_) {
        if (const uint64_t ticket = destinos_->TerminarOclusionAsincrona(query)) {
          if (elegible) {
            oc_pendientes_.push_back({ticket, oc_clave_, swaps_});
          } else {
            oc_tickets_sueltos_.push_back(ticket);  // read and dropped, so the renderer does not keep it
          }
        }
      }
      if (!oc_decidida_) {
        ++oc_.sin_caja;
      } else if (!con_clave) {
        ++oc_.sin_clave;
      } else if (oc_pinta_) {
        ++oc_.pinta;
      } else if (const auto it = oc_historial_.find(oc_clave_); it != oc_historial_.end()) {
        HistorialOclusion& h = it->second;
        h.visto = swaps_;
        // Hidden: two valid zero counts in consecutive frames, the last one recent (a box seen again after a while,
        // a camera cut, a primitive back in the frustum: its old zeros say nothing about this view).
        if (h.ceros >= 2 && swaps_ - h.cero <= kOcEdadCero) n = 0;
      }
    }
    oc_abierta_ = false;
    ++(n ? oc_.visibles : oc_.ocultas);
    counts->ZPass_A = n;
    counts->Total_A = n;
  }

  // The open conservative query ends without an answer: its GPU count, if one was started, is closed in the renderer
  // (no open count left behind there) and dropped.
  void OcCerrarSuelta() {
    if (oc_abierta_ && oc_empezada_) {
      if (const uint64_t ticket = destinos_->TerminarOclusionAsincrona(oc_query_)) oc_tickets_sueltos_.push_back(ticket);
    }
    oc_abierta_ = oc_empezada_ = false;
  }

  // A query box inside a conservative query (Draw): false = not drawn. The first box of the query decides for all of
  // its boxes whether they are drawn and counted this frame (OcMedir), and starts the GPU count.
  bool CajaConservadora() {
    if (!oc_decidida_) {
      oc_decidida_ = true;
      uint64_t clave = 0;
      if (HuellaCaja(clave)) {
        oc_clave_ = clave;
        oc_medir_ = OcMedir(clave);
      }
      if (oc_medir_) {
        destinos_->EmpezarOclusion(oc_query_);
        oc_empezada_ = true;
      }
    }
    if (oc_medir_ || !REXCVAR_GET(xmow_pular_caixas_consulta)) {
      ++oc_.dibujadas;
      return true;
    }
    ++oc_.saltadas;
    return false;
  }

  // Whether the box with this key is drawn and counted this frame. A key seen for the first time is not (a moving
  // object's box changes every frame: never counted, always visible); one never counted yet, or last counted 0 (or with
  // the depth not valid), is counted every frame, to hide it soon or to show it again at once; a visible one once every
  // xmow_oclusao_reteste_quadros frames, spread by its hash.
  bool OcMedir(uint64_t clave) {
    const bool pular = REXCVAR_GET(xmow_pular_caixas_consulta);
    auto [it, nueva] = oc_historial_.try_emplace(clave);
    HistorialOclusion& h = it->second;
    if (nueva) {
      h.visto = swaps_;
      return !pular;
    }
    if (!pular || !h.medida || h.sospecha) return true;
    const uint64_t periodo = uint64_t(std::max(REXCVAR_GET(xmow_oclusao_reteste_quadros), 1));
    const uint64_t fase = (clave ^ (clave >> 29) ^ (clave >> 47)) % periodo;
    return fase == swaps_ % periodo;
  }

  // The key of the box draw about to be sent: XXH3 of its vertices (the stream of the VS position element, the vertex
  // range the indices use) with the vertex shader, primitive type and index count as the seed. false = no key (the
  // query is answered visible and its boxes are skipped, as in 0.1.20).
  bool HuellaCaja(uint64_t& clave) const {
    namespace g = rex::graphics;
    if (!vs_ || vs_->elementos.empty()) return false;
    const uint32_t init = Register(g::XE_GPU_REG_VGT_DRAW_INITIATOR);
    const uint32_t count = init >> 16;
    if (!count || count > kOcIndicesMax) return false;
    const rex::x360::ElementoVertice* posicion = &vs_->elementos.front();
    for (const rex::x360::ElementoVertice& el : vs_->elementos) {
      if (el.uso == 0 && el.indice_uso == 0) {
        posicion = &el;
        break;
      }
    }
    const size_t i = size_t(posicion->instruccion) * 3;
    if (i + 2 >= vs_microcode_.size()) return false;
    const uint32_t w0 = vs_microcode_[i], w2 = vs_microcode_[i + 2];
    const uint32_t slot = ((w0 >> 20) & 0x1F) * 3 + ((w0 >> 25) & 0x3);  // as ExpandRectList
    const uint32_t stride = w2 & 0xFF;                                    // words
    const uint32_t f0 = registers_[0x4800 + slot * 2], f1 = registers_[0x4800 + slot * 2 + 1];
    if ((f0 & 0x3) != 3 || !stride) return false;
    const uint32_t available = (f1 >> 2) & 0xFFFFFF;  // words
    const uint32_t offset = Register(g::XE_GPU_REG_VGT_INDX_OFFSET) & 0xFFFFFF;
    uint32_t vmin = UINT32_MAX, vmax = 0;
    const uint32_t source = (init >> 6) & 0x3;
    if (source == 2) {  // auto index
      vmin = offset;
      vmax = offset + count - 1;
    } else if (source == 0) {  // DMA: the indices' range, read as the renderer's IndicesDe32 does (whole swapped words)
      const bool idx32 = (init >> 11) & 0x1;
      const uint32_t dma_size = Register(g::XE_GPU_REG_VGT_DMA_SIZE);
      const auto orden = static_cast<xenos::Endian>(dma_size >> 30);
      const uint32_t bytes = idx32 ? 4 : 2;
      const uint32_t dma_base = Register(g::XE_GPU_REG_VGT_DMA_BASE) & ~(bytes - 1) & 0x1FFFFFFF;
      if (count > (dma_size & 0xFFFFFF) || uint64_t(dma_base) + uint64_t(count) * bytes + 4 > kOcMemoriaFisica) {
        return false;
      }
      const uint8_t* datos = memory_->TranslatePhysical(dma_base);
      for (uint32_t k = 0; k < count; ++k) {
        uint32_t v;
        if (idx32) {
          uint32_t crudo;
          std::memcpy(&crudo, datos + size_t(k) * 4, 4);
          v = xenos::GpuSwap(crudo, orden) & 0xFFFFFF;
        } else {
          const uint8_t* indice = datos + size_t(k) * 2;
          const uint8_t* par = reinterpret_cast<const uint8_t*>(uintptr_t(indice) & ~uintptr_t(3));
          const bool alta = (uintptr_t(indice) & 2) != 0;
          uint32_t palabra = 0;
          std::memcpy(&palabra, par, (k + 1 == count && !alta && (uintptr_t(par) & 0xFFF) > 0xFFC) ? 2 : 4);
          palabra = xenos::GpuSwap(palabra, orden);
          v = alta ? (palabra >> 16) : (palabra & 0xFFFF);
          if (v == 0xFFFF) continue;  // a restart index, if the game used one
        }
        v += offset;
        vmin = std::min(vmin, v);
        vmax = std::max(vmax, v);
      }
      if (vmin > vmax) return false;
    } else {
      return false;
    }
    if (vmax - vmin >= kOcVerticesMax || uint64_t(vmax + 1) * stride > available ||
        uint64_t(f0 & 0x1FFFFFFC) + uint64_t(vmax + 1) * stride * 4 > kOcMemoriaFisica) {
      return false;
    }
    const uint32_t direccion = (f0 & 0x1FFFFFFC) + vmin * stride * 4;
    const uint8_t* vertices = memory_->TranslatePhysical(direccion);
    const uint64_t semilla = (uint64_t(vs_->numero) << 40) ^ (uint64_t(init & 0x3F) << 32) ^ count;
    const uint64_t h = XXH3_64bits_withSeed(vertices, size_t(vmax - vmin + 1) * stride * 4, semilla);
    clave = h ? h : 1;
    return true;
  }

  // The GPU counts that came back, in order, into the history; never waits. Called with the other polls (RingLoop).
  void OclusionConservadoraPoll() {
    if (!destinos_) return;
    for (const uint64_t ticket : oc_tickets_sueltos_) {
      uint64_t n = 0;
      bool z = false;
      if (!destinos_->ResultadoOclusionZ(ticket, n, z)) {
        oc_sueltos_espera_.push_back(ticket);
      }
    }
    oc_tickets_sueltos_.clear();
    oc_tickets_sueltos_.swap(oc_sueltos_espera_);
    if (oc_tickets_sueltos_.size() > 4096) oc_tickets_sueltos_.clear();  // the renderer forgets them on its own
    while (!oc_pendientes_.empty()) {
      const PendienteOclusion& p = oc_pendientes_.front();
      uint64_t n = 0;
      bool z_valida = false;
      if (!destinos_->ResultadoOclusionZ(p.ticket, n, z_valida)) {
        if (swaps_ - p.fotograma <= kOcEsperaMaxFotogramas) break;  // in order: the later ones are not back either
        ++oc_.perdidas;  // given up (the renderer forgets old tickets): nothing learnt
        oc_pendientes_.pop_front();
        continue;
      }
      ++oc_.cuentas;
      if (swaps_ - p.fotograma >= 2) ++oc_.atrasadas;
      if (!z_valida) ++oc_.z_invalidas;
      if (const auto it = oc_historial_.find(p.clave); it != oc_historial_.end() && p.fotograma >= it->second.medido) {
        HistorialOclusion& h = it->second;
        const bool cero_seguido = h.ceros > 0 && p.fotograma - h.cero <= 2;  // the frame after (or the same frame)
        h.medida = true;
        h.medido = p.fotograma;
        if (!z_valida) {
          h.ceros = 0;          // the chain of zeros is broken: never hidden on this count
          h.sospecha = n == 0;  // counted again next frame if it said 0
        } else if (n > 0) {
          h.ceros = 0;  // visible again at once
          h.sospecha = false;
        } else {
          if (!cero_seguido) {
            h.ceros = 1;
          } else if (p.fotograma > h.cero) {
            h.ceros = 2;  // two frames in a row (two counts in one frame, the two tiles, are one)
          }
          h.cero = p.fotograma;
          h.sospecha = true;
        }
      }
      oc_pendientes_.pop_front();
    }
  }

  // At each Swap: the history is forgotten after a long frame (a load, a level change), after a run of frames without
  // queries (menus, movies) and when the mode is switched on; old keys go when it grows past kOcHistorialMax.
  void OclusionConservadoraSwap() {
    OclusionConservadoraPoll();
    const auto ahora = Clock::now();
    const bool activa = REXCVAR_GET(xmow_oclusao_conservadora);
    const bool hueco = oc_ultimo_swap_ != Clock::time_point{} && ahora - oc_ultimo_swap_ > std::chrono::seconds(1);
    oc_ultimo_swap_ = ahora;
    oc_sin_consultas_ = oc_.zpd_fotograma ? 0 : oc_sin_consultas_ + 1;
    oc_.zpd_fotograma = 0;
    if (destinos_) OcCerrarSuelta();
    const bool encendida = activa && !oc_activa_previa_;
    oc_activa_previa_ = activa;
    if (!oc_historial_.empty() && (hueco || encendida || !activa || oc_sin_consultas_ >= 30)) {
      oc_historial_.clear();
      ++oc_.limpiezas;
    }
    if (oc_historial_.size() > kOcHistorialMax) {
      std::erase_if(oc_historial_, [&](const auto& e) { return e.second.visto + 8 < swaps_; });
      if (oc_historial_.size() > kOcHistorialMax) oc_historial_.clear();
      ++oc_.podas;
    }
  }

  rex::ui::WindowedAppContext* app_context_ = nullptr;
  std::unique_ptr<rex::ui::GraphicsProvider> provider_;
  std::unique_ptr<rex::ui::Presenter> presenter_;
  std::unique_ptr<rex::x360::DestinosNativos> destinos_;  // the universal deko renderer backend
  rex::runtime::FunctionDispatcher* dispatcher_ = nullptr;
  rex::system::KernelState* kernel_state_ = nullptr;
  rex::memory::Memory* memory_ = nullptr;
  bool mmio_registered_ = false;
  std::atomic<bool> active_{false};

  std::vector<uint32_t> registers_;
  uint64_t bin_mask_ = ~uint64_t(0);
  uint64_t bin_select_ = ~uint64_t(0);

  std::atomic<uint32_t> callback_{0};
  std::atomic<uint32_t> callback_data_{0};
  std::atomic<uint32_t> ring_base_{0};
  std::atomic<uint32_t> ring_words_{0};
  std::atomic<uint32_t> ring_generation_{0};
  std::atomic<uint32_t> write_pointer_{0};
  std::atomic<uint32_t> read_pointer_writeback_{0};
  std::atomic<uint32_t> counter_{0};  // vblanks + swaps, for EVENT_WRITE_SHD
  std::atomic<uint64_t> vblanks_{0};
  std::mutex ring_mutex_;
  std::condition_variable ring_cv_;
  std::atomic<bool> ring_dormido_{false};  // the ring thread is (about to be) waiting on ring_cv_
  // 0.1.33: CP_RB_WPTR writes, and those that woke the ring (one writer each; read and reset by the report).
  // Statistics only: a load and store each (one writer), taken with exchange by the report. A report that runs between
  // a writer's load and store sees that interval's count again in the next report (never a decision input).
  std::atomic<uint64_t> wptr_escritas_{0}, wptr_despertares_{0};
  // 0.1.33d: ns the waking kicks spent taking ring_mutex_ / in notify_one, and the longest lock wait (same rules).
  std::atomic<uint64_t> kick_ns_cerrojo_{0}, kick_ns_aviso_{0}, kick_ns_max_{0};
  uint32_t reinicio_visto_ = 0;  // 0.1.33d: rex::x360::g_xmow_reinicio_informes at the last report
  static constexpr uint32_t kRptrPaso = 256;  // words between the read pointer writebacks inside a batch
  const Reader* lector_anillo_ = nullptr;     // RingLoop's reader during a batch (xmow_anillo_rptr_rapido), else null
  rex::system::object_ref<rex::system::XHostThread> ring_thread_;
  rex::system::object_ref<rex::system::XHostThread> vblank_thread_;

  // Only the ring thread touches these.
#if GOW2_DEKO3D
  std::vector<uint32_t> microcode_;     // the last IM_LOAD, host byte order
  std::vector<uint32_t> vs_microcode_;  // the last VS load, as D3D patched it
  const rex::x360::EntradaShader* vs_ = nullptr;
  const rex::x360::EntradaShader* ps_ = nullptr;
  std::unordered_map<uint64_t, uint64_t> vs_generations_;
  // xmow_imload_rapido (CargaInmediataCacheada): per stage (0 VS, 1 PS), raw microcode key -> the library's answer.
  struct CargaInmediata {
    const rex::x360::EntradaShader* entrada;
    uint64_t generacion;  // VS only
  };
  std::unordered_map<uint64_t, CargaInmediata> cargas_inmediatas_[2];
  uint64_t vs_clave_ = 0;        // key of the IM_LOAD_IMMEDIATE the VS state comes from (0: another kind of load)
  uint64_t imload_rapidas_ = 0;  // loads answered from cargas_inmediatas_ (the 10 s report)
  bool diag_activo_ = true;      // xmow_diag_solo_activo: per-draw diagnostics this frame (ActualizarDiag)
  std::vector<uint64_t> omitir_ps_bits_;  // xmow_omitir_ps as a bitset by shader number (EnListaOmitir)
  uint64_t generacion_vs_ = 0, generacion_constantes_vs_ = 0, generacion_constantes_ps_ = 0;
  uint64_t sucias_vs_[4] = {}, sucias_ps_[4] = {};  // xmow 1034 (MarcarConstante)
  uint64_t generacion_fetch_ = 0, generacion_encuadre_ = 0;
  uint64_t geometria_ = 0, enviados_ = 0, sin_vs_ = 0, sin_ps_ = 0;
  std::unordered_map<uint64_t, uint64_t> unknown_vs_skips_;          // microcode hash -> draws skipped
  std::unordered_map<uint64_t, std::string> unknown_vs_sample_;      // one draw's state each
  std::array<uint64_t, 16> color_formats_{};
  std::array<uint64_t, 2> depth_formats_{};
  std::array<uint64_t, 32> primitive_types_{};
  std::unordered_set<uint64_t> resolve_seen_;  // ResolveCatalog
  std::unordered_set<std::string> clear_shapes_;  // EmulateClear
  uint32_t clear_rect_ = 0;
  uint64_t game_rect_clears_ = 0;  // XMOW: emulated clears limited to the game's rectangle          // guest buffer with the emulated clears' rectangle
  uint32_t rect_buffer_ = 0;         // guest buffer with the rectangle lists made quads (ExpandRectList)
  uint32_t rect_buffer_size_ = 0;
  uint64_t last_frame_draws_ = 0;   // geometry draws of the last finished frame
  uint32_t packet_trace_n_ = 0;     // [pm4] lines of the traced frame
  uint32_t queries_logged_ = 0;     // [query] lines
  uint64_t zpd_count_ = 0;          // gow2_consultas_siempre_visibles
  struct PendingQuery {
    uint32_t address;  // the end structure, still marked
    uint64_t ticket;
    std::chrono::steady_clock::time_point ended;
  };
  std::deque<PendingQuery> pending_queries_;
  std::chrono::steady_clock::time_point ring_activity_;  // the ring thread's last batch of packets  // real occlusion queries the GPU has not counted yet, oldest first
  uint64_t queries_answered_ = 0, queries_hidden_ = 0, queries_timed_out_ = 0, queries_wait_us_ = 0;
  uint64_t queries_not_waited_ = 0;  // answered visible at once because the game was waiting
  uint64_t rect_lists_ = 0;
  uint32_t rect_unexpanded_logged_ = 0;
  double next_auto_capture_s_ = 10, next_auto_dump_s_ = -1;  // AutomaticCollection
  uint32_t auto_captures_ = 0, auto_dumps_ = 0;
  uint64_t depth_greater_ = 0, depth_less_ = 0, clears_ = 0, clears_logged_ = 0, msaa_draws_ = 0;
  // Per depth buffer: draws testing GREATER/GEQUAL and LESS/LEQUAL into it (EmulateClear's far value).
  std::unordered_map<uint32_t, std::pair<uint64_t, uint64_t>> depth_direction_;
  // A depth buffer for depth_direction_: its EDRAM base, its depth format and its pitch. Gears of War 2 keeps its
  // 800-wide D24S8 shadow maps (LEQUAL) and a 640-wide float depth buffer (GEQUAL) at the same base 5A0; keyed by the
  // base alone the GEQUAL draws won, the shadow maps were filled with 0 and characters in shadow came out black.
  static uint32_t DepthKey(uint32_t depth_info, uint32_t surface_info) {
    return (depth_info & 0xFFF) | (((depth_info >> 16) & 0x1) << 12) | ((surface_info & 0x3FFF) << 13);
  }
  FrameState frame_;
  bool telemetry_ = false;
  bool hide_glow_ = false;  // sin_resplandor.flag
  std::array<uint64_t, 8> index_orders_{};
  uint32_t vs_physical_ = 0;
  uint32_t syncs_reported_ = 0;
  uint32_t skip_mode_ = 0, capture_after_skip_ = 0, skip_resume_ = 0;
  std::unordered_map<uint32_t, uint32_t> depth_vs_frame_, depth_vs_last_;  // search modes: VS number -> draws
  std::vector<uint32_t> search_vs_;  // the search modes' shader numbers (vertex or pixel, search_ps_)
  struct Probe {
    uint32_t index, vs, ps, count;
    uint64_t ticket, samples;
  };
  bool probing_ = false;
  uint32_t probe_delay_ = 0;  // XMOW 1019: Swaps left before the probed frame starts (0 = none pending)
  uint32_t probe_x_ = 0, probe_y_ = 0;
  std::vector<Probe> probes_;
  std::chrono::steady_clock::time_point probe_started_;
  bool search_ps_ = false;
  uint32_t broken_vs_logged_ = 0;
  uint32_t tree_capture_after_ = 0;
  uint64_t skipped_broken_vs_ = 0;
  uint64_t prepass_skipped_ = 0;
  uint64_t reported_prepass_skipped_ = 0;  // 0.1.31 ([xmow census])
  uint64_t skipped_by_mode_ = 0;
  // xmow_captura_ab: the group left out until the next capture (0 = none, 1 translucent, 2 modulated shadows, 3 the
  // xmow_ab_ps list, 4 additive, 5 alpha-blended translucents "ta", 6 other translucents "to"), the frames until it, whose turn is next, and the list read once.
  uint32_t ab_grupo_ = 0, ab_cuenta_ = 0, ab_turno_ = 0;
  uint64_t ab_omitidos_ = 0;
  bool ab_ps_leidos_ = false;
  std::vector<uint32_t> ab_ps_;
  uint32_t ab_ps_turno_ = 0, ab_ps_actual_ = 0;
  bool omitir_ps_leidos_ = false;
  uint32_t omitir_ps_version_ = 0;  // EnListaOmitir: change count of xmow_omitir_ps the list was read at
  std::vector<uint32_t> omitir_ps_;
  uint64_t omitidos_ps_ = 0;
  uint64_t omitidos_niebla_ = 0;  // height fog draws left out by xmow_niebla_altura (NieblaAlturaOmitida)
  bool niebla_frontal_ = false;   // NieblaAlturaOmitida: the last fog draw was the frontend's (AUTO)
  // xmow_pular_caixas_consulta
  bool query_aberta_ = false;             // an occlusion query is between its begin and its end (OcclusionQuery)
  uint64_t caixas_omitidas_ = 0;          // query box draws dropped, in total
  uint64_t reported_caixas_ = 0;          // ... at the last 10 s report
  uint64_t frame_caixas_ = 0;             // ... in the frame being counted (for [xmow hitch])
  // xmow_oclusao_conservadora (OclusionConservadora and the functions after it)
  static constexpr uint32_t kOcIndicesMax = 8192;            // index count of a box draw that is still keyed
  static constexpr uint32_t kOcVerticesMax = 4096;           // vertex range of a box draw that is still keyed
  static constexpr uint64_t kOcMemoriaFisica = 0x20000000;   // 512 MB of guest physical memory
  static constexpr size_t kOcHistorialMax = 16384;           // keys kept (pruned at the Swap)
  static constexpr uint64_t kOcEdadCero = 4;                 // frames a zero count is still recent enough to hide
  static constexpr uint64_t kOcEsperaMaxFotogramas = 8;      // frames a GPU count is waited for before giving up
  struct HistorialOclusion {
    uint64_t visto = 0;     // swap of the last query with this key
    uint64_t medido = 0;    // swap of the query of the last count applied
    uint64_t cero = 0;      // swap of the query of the last valid count of 0
    uint8_t ceros = 0;      // valid counts of 0 in consecutive frames (2 = hidden while recent)
    bool medida = false;    // counted at least once
    bool sospecha = false;  // the last count said 0 (valid or not): counted every frame
  };
  struct PendienteOclusion {
    uint64_t ticket, clave, fotograma;
  };
  std::unordered_map<uint64_t, HistorialOclusion> oc_historial_;
  std::deque<PendienteOclusion> oc_pendientes_;                         // counts asked for, oldest first
  std::vector<uint64_t> oc_tickets_sueltos_, oc_sueltos_espera_;        // counts read only to be dropped
  bool oc_abierta_ = false;   // a conservative query is between its begin and its end
  bool oc_decidida_ = false;  // its first box was seen (key and decision made)
  bool oc_medir_ = false;     // its boxes are drawn and counted this time
  bool oc_empezada_ = false;  // EmpezarOclusion was called for it
  bool oc_pinta_ = false;     // a draw that is not a query box went into it: answered visible, not learnt from
  uint32_t oc_query_ = 0;
  uint64_t oc_clave_ = 0;
  bool oc_activa_previa_ = false;
  uint32_t oc_sin_consultas_ = 0;
  Clock::time_point oc_ultimo_swap_{};
  struct ContadoresOclusion {
    uint64_t ocultas = 0, visibles = 0, sin_caja = 0, sin_clave = 0, pinta = 0;
    uint64_t dibujadas = 0, saltadas = 0, cuentas = 0, atrasadas = 0, z_invalidas = 0, perdidas = 0;
    uint64_t limpiezas = 0, podas = 0;
    uint64_t zpd_fotograma = 0;  // ZPDs this frame (not reported)
  } oc_;
  // xmow 1031: the ring thread's time by activity (accumulated ns; HitchPorSwap takes the per-frame deltas), and the
  // renderer's own accumulators at the last Swap (10 s report, dump end, DKSH reads on the ring, fence waits, recording).
  struct TiempoAnillo {
    uint64_t ocioso = 0, informe = 0, sondeos = 0, paquetes = 0, dibujar = 0, copiar = 0, presentar = 0, subidas = 0;
  };
  TiempoAnillo anillo_{}, anillo_previo_{};
  std::array<uint64_t, 5> hitch_r_previo_{};
  static uint64_t NsAnillo() {
    return uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
                        std::chrono::steady_clock::now().time_since_epoch())
                        .count());
  }
  bool CopiarMedido(const rex::x360::RegistrosCopia& c) {
    const uint64_t t = NsAnillo();
    const bool ok = destinos_->Copiar(c);
    anillo_.copiar += NsAnillo() - t;
    return ok;
  }
  // xmow_log_hitch_ms: [xmow hitch] lines wait here for the 10 s report (the logger writes to the SD synchronously)
  Clock::time_point hitch_ultimo_swap_{};
  bool hitch_ressincronizar_ = false;  // 0.1.34: PLAY MODE skipped frames, the deltas are stale
  uint64_t hitch_subidas_prev_ = 0, hitch_espera_anillo_prev_ = 0, hitch_espera_d3d_prev_us_ = 0;
  std::vector<std::string> hitch_linhas_;
  uint32_t hitch_suprimidas_ = 0;
  // L3 + R3: the swap at which the presented frame is saved, and the one-per-dump log of PS 6971's inputs
  uint64_t captura_final_en_ = 0;
  bool composicao_final_logada_ = false;
  std::unordered_map<uint32_t, uint32_t> ab_detalles_;
  std::unordered_map<uint32_t, uint32_t> tela_detalles_;
  uint64_t clears_logged_1x_ = 0;
  std::unordered_set<uint64_t> glow_logged_;
  std::unordered_set<uint64_t> unknown_vs_logged_;
  uint32_t glow_draws_logged_ = 0;
  bool draw_tracing_ = false;
  struct DrawSumEntry { uint32_t count = 0; uint64_t sum_n = 0; };
  bool draw_sum_on_ = false;  // 0.1.22: the L3 + R3 frame is being summed
  uint32_t draw_sum_total_ = 0;
  std::map<std::pair<uint64_t, uint64_t>, DrawSumEntry> draw_sum_;  // 0.1.26: (groups, blend/write modes)
  // 0.1.26: draws sent to the renderer by ClaseDelDibujo since the last 10 s report, and the inert ones it drew
  std::array<uint64_t, kClases> censo_{};
  uint64_t censo_inertes_dibujados_ = 0;
  uint64_t inertes_omitidos_ = 0;    // 0.1.29 xmow_pular_inertes, since the last 10 s report
  uint64_t tijera_1px_dibujos_ = 0;  // 0.1.29 xmow_diag_tijera_1px, since the last 10 s report
  uint64_t vacios_dibujos_ = 0;      // 0.1.30 xmow_diag_custo_dibujo = 2, since the last 10 s report
  int32_t volcado_pilhas_mb_ = -1;  // xmow_amostragem_volcado_mb handed to switch_perf.cpp (-1 = not yet)
  uint64_t hitch_gpu_prev_ns_ = 0;  // [xmow hitch]: GPU busy of the works read back, at the last Swap
  // 0.1.27: draws skipped while their shader's SPIR-V was being read (Draw), and [xmow hitch]'s previous values of
  // them, of the library's SPIR-V reads on the calling threads and on its reader thread (ns).
  uint64_t spirv_no_lista_ = 0;
  std::array<uint64_t, 3> hitch_spirv_prev_{};
  std::string mapa_actual_;              // MapaPorSwap
  Clock::time_point mapa_guardado_{};
  uint32_t draw_traces_ = 0, draw_trace_n_ = 0, draw_trace_cap_ = 400;
  uint32_t next_capture_ = UINT32_MAX;  // SaveCapture
  Clock::time_point next_draw_trace_{};
  bool tracing_ = false;
  uint32_t traces_done_ = 0, trace_lines_ = 0, trace_pass_ = 0;
  uint64_t trace_pass_draws_ = 0, trace_pass_no_vs_ = 0;
  std::array<uint32_t, 9> trace_key_{};
  uint32_t trace_sc_mode_ = 0, trace_depthctl_ = 0;  // XMOW: PA_SU_SC_MODE_CNTL and RB_DEPTHCONTROL at a pass's start
  uint32_t trace_scissor_tl_ = 0, trace_scissor_br_ = 0;
  float trace_vport_[4] = {};
  Clock::time_point next_trace_{}, first_swap_{};
#endif
  uint64_t swaps_ = 0, draws_ = 0, packets_ = 0, waits_cut_short_ = 0;
  uint64_t reg_mem_waits_ = 0, reg_mem_wait_ns_ = 0;  // WAIT_REG_MEM that had to wait, and how long in total
  uint64_t reported_swaps_ = 0, reported_draws_ = 0, reported_packets_ = 0;
  Clock::time_point last_report_;
};

}  // namespace

std::unique_ptr<rex::system::IGraphicsSystem> CreateGraphicsSystem() {
  return std::make_unique<RingGraphicsSystem>();
}

}  // namespace gow2::gpu
