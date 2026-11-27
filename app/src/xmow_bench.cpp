// XMOW 0.1.34 (b: after the two Opus reviews, revisao-opus-0.1.34-A/B.md; c: after revisao-opus-0.1.34b.md): PLAY
// MODE, the automatic bench and the live frame time line.
//
// PLAY MODE (xmow_modo_jogo, default true, live; LOGS AND DIAGNOSTICS > PLAY MODE):
//  - a log level floor of warn (rex::SetLevelFloor, sdk-g2 logging.cpp: also for the categories registered later; the
//    log_level cvar is not changed, so SAVE never writes warn): WARN and ERROR lines still go to the log. A crash
//    writes logs/rex/rex_crash.log (exception_handler_switch.cpp, independent of the log level) and then waits up to
//    800 ms for the log writer thread to write what is still queued (RexLogDrenarParaCrash, log_async; not on the exit()
//    path). 0.1.34c: the log flushes on ERROR only (rex::SetFlushErrorsOnly), so the WARN context lines below cause no
//    SD write of their own (the writer thread writes them with its buffer);
//  - the renderer's 10 s report (X360_INFORME, render.h) is not logged, the profiler's rex_perfil report is not written
//    (switch_perf.cpp RexSwitchPerfInformes), the [xmow hitch] lines are not collected, stack sampling stays off. The
//    port's own 10 s lines are INFO, dropped by the level before they are formatted (REX_LOG_IMPL);
//  - what stays: the GPU timestamps the dynamic resolution reads (g_xmow_gpu_ocupada_ns), the counters (plain adds),
//    the context lines at WARN (map packages [xmow light] and [XMOW mapa], menu changes, back to the main menu, the
//    bench: XMOW_LOG_CONTEXTO) and the once-a-minute [xmow live] line (frame times, memory, clocks).
//  The bench turns the diagnostics on for its run (and back), unless the test itself changes xmow_modo_jogo.
//
// [xmow live] (always, ring thread): every frame goes into a 0.25 ms histogram; once a minute a WARN line with the
// average fps, the 1 % and 0.1 % lows (the fps of the 99th / 99.9th percentile frame time), the share of frames over
// 40 and 50 ms, the worst whole second, and the process / guest memory, the clocks and the map kind. Gaps over 2 s
// (pause menus of the console, loads, sleep) are left out and counted. 0.1.34c: never inside a bench window (its
// service calls): the line waits for the window's end.
//
// The bench (advanced menu > TESTS; xmow_bench.toml beside the NRO, read each time the page opens):
//  - [[test]] tables: id (unique), name, help, window_s (measured seconds per step, default 30), window_frames (instead
//    of the seconds when > 0), settle_s (seconds after a change before measuring, default 3), capture (a screen capture
//    at the end of every step), repeat (the steps list run this many times; then the first step once more, so the
//    order is ABAB...A, unless end_with_control = false), region = [x0, y0, x1, y1] (the part of the frame the image
//    verdict uses; default the whole frame), live (a played test: the controller is not blocked, no pause check),
//    steps = [{cvar = value, ...}, ...] ({} = nothing changed).
//  - Only live rows of the advanced menu with a value of the cvar's type are accepted. Each step starts from the
//    values the options had when the test started and sets its own; the dynamic resolution is held off during the
//    test (it would trade the measured cost for resolution and change the image).
//  - Not a live test: first two captures 1 s apart; more than 1.5 % of the region's pixels changed (a channel by more
//    than 8 of 1023) = NOT PAUSED (aborted); no capture possible = aborted too (NO PAUSE CHECK POSSIBLE).
//  - Per step: the settle (it starts again after a frame over 250 ms, except the frame after the bench's own capture,
//    and it waits for the SD writes to finish), a
//    report window cut, the measured window, a second cut, "... end:" with fps, frame times (avg, p95, p99, max, the
//    share of frames near 16.7 / 33.3 / 50 ms), GPU busy and scene per frame, draws, the CPU ms per frame of the
//    busiest threads (and the GPU ring, and all threads together), the clocks, the SoC temperature, docked or
//    handheld, the charger. A frame over 250 ms in the window starts the window again once; a second one marks the
//    window INVALID. At the window's end every option of the step is read back: one the port changed by itself marks
//    the window INVALID (and MATERIAL REPLAY ON that an audit switched off aborts the test: its ON windows would be OFF).
//  - The capture (capturas/bench_<id>_<time>_<run>_<step>.bmp) is compared with the test's first one (a control
//    capture): the differing pixels inside the region are kept (up to 5 % of the frame). At the end, the pixels that
//    changed between the control's own captures (dilated 2 pixels) form a mask; over 5 % of the region = SCENE NOT
//    STATIC (no image result). Every other capture is then IDENTICAL (nothing differs outside the mask), NEAR (at most
//    1 of 1023 in any channel) or DIFFERENT. Whole frame numbers are logged too. 0.1.34c: the compare and the region
//    hash use the presented RGB10A2 texels as they are (rex::ui::PedirCapturaCruda), not the 8-bit image (4 codes of
//    1023 per step of 255); without them the run compares AT 8 BITS, says so, and has no NEAR.
//  - At the end every option goes back; per distinct step: the means, then the paired result against the control (each
//    window of the step minus the mean of the control windows next to it): every pair, mean +- SD, t = mean / (SD /
//    sqrt(n)) with n - 1 degrees of freedom and its two-sided p (Student), the signs; REAL when p < 0.05 (|t| over
//    12.71 / 4.30 / 3.18 / 2.78 for 2 / 3 / 4 / 5 pairs) with the same sign in every pair, LIKELY when p < 0.20 with
//    the same signs, else NOT RESOLVED (next pairs share a control window, so p is somewhat optimistic), for fps, frame
//    ms, scene, GPU busy, the busiest thread, the GPU ring and all threads; the control's drift (last - first). The
//    control is the first step's label even when its first window is INVALID.
//  - The bench lines are WARN (they must survive PLAY MODE, test 7 measures it).
//  - While a blocking test runs the game sees an idle controller (and nothing held when it ends: every button must be
//    released first), the SDK's shortcuts are off, B aborts. Losing the focus (HOME, sleep), a map package, or no frame
//    for 5 s abort too (0.1.34c: at 5 s the controller and the shortcuts go back at once, the options at the next
//    frame). A corner notification shows the test, the step and the phase, then DONE / ABORTED and, for a capture test,
//    the image verdict.
//  - Nothing is saved to xmow.toml: a crash in the middle comes back with the saved values.
//
// Threads: Listar / Iniciar / Abortar / Bloqueante / AnilloParado on the menu's input thread; PorSwap / QuiereCaptura /
// Captura / AplicarModoJogo on the ring thread (the Swap). The test is handed over under g_mutex; the run itself is the
// ring thread's only.

#include "xmow_bench.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <fmt/format.h>
#include <toml++/toml.hpp>
// xxHash reads with memcpy under GCC's strict aliasing (as gow2_gpu.cpp).
#undef XXH_FORCE_MEMORY_ACCESS
#define XXH_FORCE_MEMORY_ACCESS 0
#define XXH_INLINE_ALL
#include <xxhash.h>

#include <rex/cvar.h>
#include <rex/input/filtro_mando.h>
#include <rex/logging.h>
#include <rex/ui/presenter.h>
#include <rex/ui/superposicion.h>
#include <rex/x360/destinos.h>

#if defined(__SWITCH__)
#include <switch.h>
#endif

REXCVAR_DEFINE_BOOL(xmow_modo_jogo, true, "XMOW",
                    "PLAY MODE (0.1.34): true = normal play: the log keeps WARN and ERROR lines only, the 10 s reports, "
                    "the profile report, the slow frame lines and stack sampling are off; crashes are still written. "
                    "false = full diagnostics for tests (log_level applies). The bench (menu TESTS) turns diagnostics "
                    "on for its run by itself. Live");

namespace rex::x360 {
extern std::atomic<uint32_t> g_xmow_reinicio_informes;  // destinos.cpp (0.1.33d)
extern std::atomic<uint64_t> g_xmow_gpu_ocupada_ns;     // destinos.cpp (xmow 1039)
extern std::atomic<uint64_t> g_xmow_gpu_escena_ns;      // destinos.cpp (0.1.34)
extern std::atomic<uint64_t> g_xmow_gpu_trabajos;       // destinos.cpp (0.1.34)
}  // namespace rex::x360
namespace xmow::cpu33 {
void EstadoMaterialReplay(uint64_t* auditadas, uint64_t* diferencias, bool* apagado);  // xmow_cpu33_material.cpp
}
bool XmowMapaDeJogo();  // xmow_resolucao_interna.cpp
#if defined(__SWITCH__)
extern "C" void RexSwitchPerfReiniciarVentana(void);
extern "C" void RexSwitchPerfInformes(bool activos);
extern "C" void RexSwitchPerfEstadoConsola(uint32_t mhz[3], float* soc_c, uint64_t mb[6], int* energia);
extern "C" size_t RexSwitchPerfTicksHilos(uint32_t* handles, char (*nombres)[32], uint64_t* ticks, size_t max);
#endif

namespace xmow::bench {
namespace {

using Clock = std::chrono::steady_clock;
constexpr double kParon_ms = 250.0;        // a frame this long restarts the settle (and the window, once)
constexpr double kSinQuadro_ms = 15000.0;  // no frame this long aborts a blocking test
constexpr double kPausaPct = 1.5;          // NOT PAUSED over this share of the region's pixels changed in 1 s
constexpr int kPausaDelta10 = 8;           // a pixel changed: a channel by more than this (of 1023; 2 of 255 at 8 bits)
constexpr double kMascaraMaxPct = 5.0;     // SCENE NOT STATIC over this mask (share of the region)

int64_t AhoraNs() {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count();
}

struct Paso {
  std::vector<std::pair<std::string, std::string>> valores;
  std::string etiqueta;  // "cvar=value, ..." or "(no change)"
};

struct Prueba {
  int id = 0;
  std::string nombre, ayuda;
  double ventana_s = 30.0, asentar_s = 3.0;
  uint32_t ventana_quadros = 0;
  bool captura = false, vivo = false;
  int repetir = 1;
  std::array<int, 4> region{0, 0, 0, 0};
  bool con_region = false;
  std::vector<Paso> pasos;  // expanded (repeat, final control)
  bool toca_modo_jogo = false, toca_material = false;
  std::filesystem::path carpeta;  // the NRO's folder
};

std::string ValorTexto(const toml::node& n, char* tipo) {
  if (const auto* b = n.as_boolean()) {
    *tipo = 'b';
    return b->get() ? "true" : "false";
  }
  if (const auto* i = n.as_integer()) {
    *tipo = 'i';
    return std::to_string(i->get());
  }
  if (const auto* f = n.as_floating_point()) {
    *tipo = 'f';
    return fmt::format("{}", f->get());
  }
  if (const auto* s = n.as_string()) {
    *tipo = 's';
    return s->get();
  }
  *tipo = '?';
  return {};
}

// A number key: absent = `defecto`; present, it must be a finite number inside [lo, hi].
bool Numero(const toml::table& t, const char* clave, double defecto, double lo, double hi, double* out) {
  const toml::node* n = t.get(clave);
  if (!n) {
    *out = defecto;
    return true;
  }
  std::optional<double> v;
  if (const auto* f = n->as_floating_point()) v = f->get();
  if (const auto* i = n->as_integer()) v = double(i->get());
  if (!v || !std::isfinite(*v) || *v < lo || *v > hi) return false;
  *out = *v;
  return true;
}

// The whole file; `error` for the file itself, each test's first problem in `errores`.
bool Leer(const std::filesystem::path& archivo, Validador validar, std::vector<Prueba>& pruebas,
          std::vector<std::string>& errores, std::string* error) {
  std::error_code ec;
  if (!std::filesystem::exists(archivo, ec)) {
    *error = "NO XMOW BENCH.TOML BESIDE XMOW.NRO";
    return false;
  }
  toml::table raiz;
  try {
    raiz = toml::parse_file(archivo.string());
  } catch (const toml::parse_error& e) {
    *error = fmt::format("XMOW BENCH.TOML DOES NOT PARSE: LINE {}", e.source().begin.line);
    REXLOG_WARN("[xmow bench] {} does not parse: {}", archivo.string(), e.description());
    return false;
  }
  const toml::array* lista = raiz["test"].as_array();
  if (!lista) {
    *error = "NO [[TEST]] TABLES IN XMOW BENCH.TOML";
    return false;
  }
  for (const toml::node& nodo : *lista) {
    const toml::table* t = nodo.as_table();
    if (!t) continue;
    Prueba p;
    std::string problema;
    auto falla = [&problema](std::string s) {
      if (problema.empty()) problema = std::move(s);
    };
    p.carpeta = archivo.parent_path();
    p.id = int((*t)["id"].value_or(int64_t(0)));
    p.nombre = (*t)["name"].value_or(std::string("TEST"));
    p.ayuda = (*t)["help"].value_or(std::string());
    double v = 0;
    if (Numero(*t, "window_s", 30.0, 1.0, 600.0, &v)) p.ventana_s = v; else falla("BAD WINDOW S");
    if (Numero(*t, "settle_s", 3.0, 0.0, 60.0, &v)) p.asentar_s = v; else falla("BAD SETTLE S");
    if (Numero(*t, "window_frames", 0.0, 0.0, 36000.0, &v) && v == std::floor(v)) p.ventana_quadros = uint32_t(v);
    else falla("BAD WINDOW FRAMES");
    if (Numero(*t, "repeat", 1.0, 1.0, 10.0, &v) && v == std::floor(v)) p.repetir = int(v); else falla("BAD REPEAT");
    p.captura = (*t)["capture"].value_or(false);
    p.vivo = (*t)["live"].value_or(false);
    const bool final_control = (*t)["end_with_control"].value_or(true);
    if (const toml::node* r = t->get("region")) {
      const toml::array* a = r->as_array();
      if (!a || a->size() != 4) {
        falla("BAD REGION");
      } else {
        for (size_t k = 0; k < 4; ++k) p.region[k] = int((*a)[k].value_or(int64_t(-1)));
        p.con_region = p.region[0] >= 0 && p.region[1] >= 0 && p.region[2] > p.region[0] && p.region[3] > p.region[1];
        if (!p.con_region) falla("BAD REGION");
      }
    }
    std::vector<Paso> base;
    if (const toml::array* s = (*t)["steps"].as_array()) {
      for (const toml::node& sn : *s) {
        const toml::table* st = sn.as_table();
        if (!st) {
          falla("A STEP IS NOT A TABLE");
          continue;
        }
        Paso paso;
        for (const auto& [clave, valor] : *st) {
          const std::string nombre(clave.str());
          char tipo = '?';
          const std::string texto = ValorTexto(valor, &tipo);
          if (tipo == '?') falla("BAD VALUE FOR " + nombre);
          if (const std::string motivo = validar ? validar(nombre, tipo) : std::string(); !motivo.empty()) falla(motivo);
          if (nombre == "xmow_modo_jogo") p.toca_modo_jogo = true;
          if (nombre == "xmow_material_replay") p.toca_material = true;
          paso.valores.emplace_back(nombre, texto);
          paso.etiqueta += (paso.etiqueta.empty() ? "" : ", ") + nombre + "=" + texto;
        }
        if (paso.etiqueta.empty()) paso.etiqueta = "(no change)";
        base.push_back(std::move(paso));
      }
    }
    if (base.empty()) falla("NO STEPS");
    for (int r = 0; r < p.repetir; ++r) p.pasos.insert(p.pasos.end(), base.begin(), base.end());
    if (p.repetir > 1 && final_control && !base.empty()) p.pasos.push_back(base.front());
    if (p.id <= 0) falla("NO ID");
    errores.push_back(problema);
    pruebas.push_back(std::move(p));
  }
  for (size_t i = 0; i < pruebas.size(); ++i) {
    for (size_t j = 0; j < pruebas.size(); ++j) {
      if (i != j && pruebas[i].id == pruebas[j].id && errores[i].empty()) {
        errores[i] = "DUPLICATE ID " + std::to_string(pruebas[i].id);
      }
    }
  }
  return true;
}

double Duracion(const Prueba& p) {
  const double ventana = p.ventana_quadros ? p.ventana_quadros / 27.0 : p.ventana_s;
  return double(p.pasos.size()) * (p.asentar_s + 0.5 + ventana + (p.captura ? 0.5 : 0.0)) + (p.vivo ? 1.0 : 3.0);
}

// ---- Shared between the input thread and the ring thread ----
std::mutex g_mutex;
std::unique_ptr<Prueba> g_pendiente;  // handed over by Iniciar, taken by PorSwap
std::string g_motivo;                 // why Abortar was called
std::atomic<bool> g_hay_pendiente{false};
std::atomic<bool> g_activo{false};
std::atomic<bool> g_bloqueante{false};
std::atomic<bool> g_abortar{false};
std::atomic<bool> g_forzar_diag{false};
std::atomic<int64_t> g_ultimo_swap_ns{0};
std::atomic<bool> g_aviso_parado{false};

// ---- Ring thread only ----
struct Imagen {
  bool hay = false, desbordada = false;
  bool comparada = false;  // compared with the reference (not the reference itself, not one taken before it)
  uint64_t hash = 0;
  uint64_t dif_region = 0, dif_total = 0;
  int max_delta = 0;
  std::vector<uint32_t> idx;     // differing pixels inside the region (y * width + x), at most kMaxDif
  std::vector<uint16_t> delta;   // their largest channel difference (of 1023, or of 255 at 8 bits)
};

struct Hilo {
  std::string nombre;
  double ms = 0;  // CPU ms per frame
};

struct Medida {
  std::string etiqueta;
  bool control = false;  // the test's first step label
  bool invalida = false;
  std::string motivo;
  double segundos = 0, fps = 0, ms = 0, p95 = 0, p99 = 0, max_ms = 0, gpu_ms = 0, escena_ms = 0, draws = 0;
  double pct16 = 0, pct33 = 0, pct50 = 0;
  double hilo_max_ms = 0, anillo_ms = 0, total_cpu_ms = 0;
  uint64_t quadros = 0;
  bool gpu = false;  // GPU works were read back in the window
  Imagen img;
};

enum class Fase { kPausa1, kPausa2, kAsentar, kMedir, kCaptura };

struct Instantanea {
  std::vector<uint32_t> h;
  std::vector<std::string> n;
  std::vector<uint64_t> t;
};

Instantanea TomarHilos() {
  Instantanea s;
#if defined(__SWITCH__)
  constexpr size_t kMax = 96;
  uint32_t h[kMax];
  char n[kMax][32];
  uint64_t t[kMax];
  const size_t k = RexSwitchPerfTicksHilos(h, n, t, kMax);
  for (size_t i = 0; i < k; ++i) {
    s.h.push_back(h[i]);
    s.n.emplace_back(n[i]);
    s.t.push_back(t[i]);
  }
#endif
  return s;
}

std::string EstadoConsola() {
#if defined(__SWITCH__)
  uint32_t mhz[3] = {0, 0, 0};
  float soc = 0;
  uint64_t mb[6] = {0, 0, 0, 0, 0, 0};
  int energia = 0;
  RexSwitchPerfEstadoConsola(mhz, &soc, mb, &energia);
  return fmt::format("clocks CPU {} GPU {} memory {} MHz, SoC {:.1f} C, {}, charger {}", mhz[0], mhz[1], mhz[2], soc,
                     energia & 1 ? "docked" : "handheld", energia & 4 ? (energia & 2 ? "connected" : "not connected") : "unknown");
#else
  return "clocks unknown";
#endif
}

std::string MemoriaConsola() {
#if defined(__SWITCH__)
  uint32_t mhz[3] = {0, 0, 0};
  float soc = 0;
  uint64_t mb[6] = {0, 0, 0, 0, 0, 0};
  int energia = 0;
  RexSwitchPerfEstadoConsola(mhz, &soc, mb, &energia);
  return fmt::format("process {}/{} MB, mapping limit {}/{} MB, guest {}/{} MB (committed/mapped); clocks CPU {} GPU {} "
                     "memory {} MHz, SoC {:.1f} C, {}",
                     mb[0], mb[1], mb[2], mb[3], mb[4], mb[5], mhz[0], mhz[1], mhz[2], soc,
                     energia & 1 ? "docked" : "handheld");
#else
  return "memory unknown";
#endif
}

struct Corrida {
  Prueba p;
  size_t paso = 0;
  Fase fase = Fase::kAsentar;
  Clock::time_point t_fase{}, ultimo_swap{}, ultimo_estado{};
  uint64_t quadros_fase = 0;
  std::map<std::string, std::string> originales;  // every option the test touches, before it
  std::map<std::string, std::string> forzados;    // held for the whole test (the dynamic resolution)
  std::map<std::string, std::string> objetivo;    // what the current step set
  bool reintentado = false;                       // the current step's window started again after a stall
  bool sd_avisado = false;
  bool quiere_captura = false;
  // the measured window
  uint64_t n = 0;
  double suma_ms = 0, max_ms = 0;
  std::array<uint32_t, 801> hist{};  // 0.25 ms bins, the last one = 200 ms and more
  uint64_t gpu0 = 0, escena0 = 0, trabajos0 = 0, draws0 = 0, draws_ahora = 0;
  uint64_t auditadas0 = 0, difs0 = 0;
  Instantanea hilos0;
  Clock::time_point t_medir{};
  bool invalida = false;
  std::string motivo;
  std::vector<Medida> resultados;
  // the control image (the test's first control capture) and the pause check's first capture, as packed texels (Texeles)
  std::vector<uint32_t> control, pausa;
  size_t ref = SIZE_MAX;  // the result whose capture is the reference
  uint32_t cw = 0, ch = 0;
  int bits = 0;               // the run's image compare: 10 (the presented texels) or 8 (the 8-bit image); 0 = not yet
  bool tras_captura = false;  // the bench captured at the last Swap: the next frame time includes the capture
  std::string sello;  // HHMMSS of the start, and the run number, in the capture names
  std::string veredicto_imagen;  // for the corner at the end
};
Corrida* g_corrida = nullptr;
Clock::time_point g_aviso_hasta{}, g_aviso_ultimo{};
std::vector<std::string> g_aviso_final;
unsigned g_corridas = 0;

void CortarVentanas() {
  rex::x360::g_xmow_reinicio_informes.fetch_add(1, std::memory_order_relaxed);
#if defined(__SWITCH__)
  RexSwitchPerfReiniciarVentana();
#endif
}

// Overlay text: upper case, digits and a few signs only (rex/ui/superposicion.h); the rest becomes a space.
std::string TextoAviso(std::string_view s, size_t maximo = 40) {
  std::string out;
  for (char c : s) {
    if (out.size() >= maximo) break;
    if (c >= 'a' && c <= 'z') c = char(c - 'a' + 'A');
    const bool ok = c != '\0' && ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || std::strchr(" .:/()-+>!,%'=", c));
    out += ok ? c : ' ';
  }
  return out;
}

// The corner: refreshed every 3 s (it lasts 4 s) with a text that only changes with the phase (no countdown: the same
// overlay work in every step).
void MostrarEstado(Corrida& c, Clock::time_point ahora, bool forzar = false) {
  if (!forzar && ahora - c.ultimo_estado < std::chrono::seconds(3)) return;
  c.ultimo_estado = ahora;
  std::string fase;
  switch (c.fase) {
    case Fase::kPausa1:
    case Fase::kPausa2: fase = "PAUSE CHECK"; break;
    case Fase::kAsentar: fase = "SETTLING"; break;
    case Fase::kMedir:
      fase = c.p.ventana_quadros ? fmt::format("MEASURING {} FRAMES", c.p.ventana_quadros)
                                 : fmt::format("MEASURING {:.0f} S", c.p.ventana_s);
      break;
    case Fase::kCaptura: fase = "CAPTURE"; break;
  }
  rex::ui::AvisarSuperposicion({TextoAviso(fmt::format("BENCH {}: {}", c.p.id, c.p.nombre)),
                                TextoAviso(fmt::format("STEP {}/{}  {}", c.paso + 1, c.p.pasos.size(), fase)),
                                c.p.vivo ? "LIVE: PLAY NORMALLY" : "B = ABORT"});
}

// Sets every option the test touches to the original value overlaid with the held ones and `paso`'s values.
bool AplicarValores(Corrida& c, const Paso* paso, std::string* fallo) {
  std::map<std::string, std::string> objetivo = c.originales;
  if (paso) {
    for (const auto& [k, v] : c.forzados) objetivo[k] = v;
    for (const auto& [k, v] : paso->valores) objetivo[k] = v;
  }
  bool ok = true;
  for (const auto& [k, v] : objetivo) {
    if (rex::cvar::GetFlagByName(k) == v) continue;
    if (!rex::cvar::SetFlagByName(k, v)) {
      ok = false;
      if (fallo) *fallo = fmt::format("{}={} rejected by the cvar", k, v);
    }
  }
  c.objetivo = std::move(objetivo);
  return ok;
}

void EmpezarPaso(Corrida& c, Clock::time_point ahora);
void Terminar(bool completo, const std::string& motivo);

double Percentil(const std::array<uint32_t, 801>& hist, uint64_t n, double q) {
  if (!n) return 0;
  const uint64_t objetivo = std::max<uint64_t>(1, uint64_t(std::ceil(q * double(n))));
  uint64_t acumulado = 0;
  for (size_t i = 0; i < hist.size(); ++i) {
    acumulado += hist[i];
    if (acumulado >= objetivo) return double(i + 1) * 0.25;
  }
  return 200.0;
}

double PctEntre(const std::array<uint32_t, 801>& hist, uint64_t n, double lo, double hi) {
  if (!n) return 0;
  uint64_t s = 0;
  for (size_t i = size_t(lo / 0.25); i < size_t(hi / 0.25) && i < hist.size(); ++i) s += hist[i];
  return double(s) * 100.0 / double(n);
}

void EmpezarMedida(Corrida& c, Clock::time_point ahora) {
  CortarVentanas();  // the reports close here: their cut report is the settle
  c.fase = Fase::kMedir;
  c.t_fase = c.t_medir = ahora;
  c.quadros_fase = 0;
  c.n = 0;
  c.suma_ms = c.max_ms = 0;
  c.hist.fill(0);
  c.invalida = false;
  c.motivo.clear();
  c.gpu0 = rex::x360::g_xmow_gpu_ocupada_ns.load(std::memory_order_relaxed);
  c.escena0 = rex::x360::g_xmow_gpu_escena_ns.load(std::memory_order_relaxed);
  c.trabajos0 = rex::x360::g_xmow_gpu_trabajos.load(std::memory_order_relaxed);
  c.draws0 = c.draws_ahora;
  bool apagado = false;
  xmow::cpu33::EstadoMaterialReplay(&c.auditadas0, &c.difs0, &apagado);
  c.hilos0 = TomarHilos();
  MostrarEstado(c, ahora, true);
}

void TerminarMedida(Corrida& c, Clock::time_point ahora) {
  CortarVentanas();  // the reports close here: their cut report is exactly the measured window
  const Instantanea hilos1 = TomarHilos();
  Medida m;
  m.etiqueta = c.p.pasos[c.paso].etiqueta;
  m.control = m.etiqueta == c.p.pasos.front().etiqueta;
  m.segundos = std::chrono::duration<double>(ahora - c.t_medir).count();
  m.quadros = c.n;
  m.fps = m.segundos > 0 ? double(c.n) / m.segundos : 0;
  m.ms = c.n ? c.suma_ms / double(c.n) : 0;
  m.p95 = Percentil(c.hist, c.n, 0.95);
  m.p99 = Percentil(c.hist, c.n, 0.99);
  m.max_ms = c.max_ms;
  m.pct16 = PctEntre(c.hist, c.n, 15.5, 17.5);
  m.pct33 = PctEntre(c.hist, c.n, 32.5, 34.5);
  m.pct50 = PctEntre(c.hist, c.n, 48.5, 51.5);
  const uint64_t trabajos = rex::x360::g_xmow_gpu_trabajos.load(std::memory_order_relaxed) - c.trabajos0;
  const double q = double(std::max<uint64_t>(c.n, 1));
  m.gpu = trabajos > 0;
  m.gpu_ms = double(rex::x360::g_xmow_gpu_ocupada_ns.load(std::memory_order_relaxed) - c.gpu0) / 1e6 / q;
  m.escena_ms = double(rex::x360::g_xmow_gpu_escena_ns.load(std::memory_order_relaxed) - c.escena0) / 1e6 / q;
  m.draws = double(c.draws_ahora - c.draws0) / q;
  // The threads' CPU in the window, per frame.
  std::vector<Hilo> hilos;
#if defined(__SWITCH__)
  const double tick_ms = 1000.0 / double(armGetSystemTickFreq());
#else
  const double tick_ms = 0;
#endif
  for (size_t i = 0; i < hilos1.h.size(); ++i) {
    for (size_t j = 0; j < c.hilos0.h.size(); ++j) {
      if (c.hilos0.h[j] != hilos1.h[i] || hilos1.t[i] < c.hilos0.t[j]) continue;
      const double ms = double(hilos1.t[i] - c.hilos0.t[j]) * tick_ms / q;
      hilos.push_back({hilos1.n[i], ms});
      m.total_cpu_ms += ms;
      if (hilos1.n[i].rfind("GPU ring", 0) == 0) m.anillo_ms += ms;
      break;
    }
  }
  std::sort(hilos.begin(), hilos.end(), [](const Hilo& a, const Hilo& b) { return a.ms > b.ms; });
  m.hilo_max_ms = hilos.empty() ? 0 : hilos.front().ms;
  std::string texto_hilos;
  for (size_t i = 0; i < hilos.size() && i < 4; ++i) {
    texto_hilos += fmt::format("{}{} {:.2f}", i ? ", " : "", hilos[i].nombre, hilos[i].ms);
  }
  // Every option of the step read back: the port may have changed one by itself.
  for (const auto& [k, v] : c.objetivo) {
    const std::string ahora_v = rex::cvar::GetFlagByName(k);
    if (ahora_v != v) {
      c.invalida = true;
      c.motivo += fmt::format("{}{} changed by itself to {} during the window", c.motivo.empty() ? "" : "; ", k, ahora_v);
    }
  }
  uint64_t auditadas = 0, difs = 0;
  bool apagado = false;
  xmow::cpu33::EstadoMaterialReplay(&auditadas, &difs, &apagado);
  std::string material;
  if (c.p.toca_material) {
    material = fmt::format("; material replay ON audits {} in the window, {} differences{}", auditadas - c.auditadas0,
                           difs - c.difs0, apagado ? ", mode 2 is OFF for this boot" : "");
  }
  bool abortar_material = false;
  if (apagado) {
    for (const auto& [k, v] : c.objetivo) {
      if (k == "xmow_material_replay" && v == "2") {
        c.invalida = true;
        c.motivo += std::string(c.motivo.empty() ? "" : "; ") +
                    "MATERIAL REPLAY ON was switched off by an audit difference: this window measured OFF";
        abortar_material = true;
      }
    }
  }
  m.invalida = c.invalida;
  m.motivo = c.motivo;
  REXLOG_WARN("[xmow bench] test {} step {}/{} {} end{}: {:.1f} s, {} frames, {:.2f} fps, frame ms avg {:.2f} p95 {:.2f} "
              "p99 {:.2f} max {:.1f}, near 16.7/33.3/50 ms {:.0f}/{:.0f}/{:.0f} %; GPU busy {:.2f} ms/frame, scene "
              "{:.2f} ms/frame{}; {:.0f} draws/frame; CPU ms/frame: {} (GPU ring {:.2f}, all threads {:.2f}); {}{}",
              c.p.id, c.paso + 1, c.p.pasos.size(), m.etiqueta, m.invalida ? " INVALID (" + m.motivo + ")" : "",
              m.segundos, m.quadros, m.fps, m.ms, m.p95, m.p99, m.max_ms, m.pct16, m.pct33, m.pct50, m.gpu_ms,
              m.escena_ms, m.gpu ? "" : " (no GPU works read back)", m.draws,
              texto_hilos.empty() ? std::string("unknown") : texto_hilos, m.anillo_ms, m.total_cpu_ms, EstadoConsola(),
              material);
  c.resultados.push_back(std::move(m));
  if (abortar_material) {
    Terminar(false, "MATERIAL REPLAY ON switched itself off (an audit found a difference): the ON windows would measure OFF");
    return;
  }
  if (c.p.captura) {
    c.fase = Fase::kCaptura;
    c.t_fase = ahora;
    c.quadros_fase = 0;
    c.quiere_captura = true;
  } else {
    ++c.paso;
    EmpezarPaso(c, ahora);
  }
}

void EmpezarPaso(Corrida& c, Clock::time_point ahora) {
  if (c.paso >= c.p.pasos.size()) {
    Terminar(true, {});
    return;
  }
  const Paso& paso = c.p.pasos[c.paso];
  std::string fallo;
  if (!AplicarValores(c, &paso, &fallo)) {
    Terminar(false, fallo);
    return;
  }
  REXLOG_WARN("[xmow bench] test {} step {}/{} {} start (settle {:.0f} s, then {})", c.p.id, c.paso + 1, c.p.pasos.size(),
              paso.etiqueta, c.p.asentar_s,
              c.p.ventana_quadros ? fmt::format("{} frames", c.p.ventana_quadros) : fmt::format("{:.0f} s", c.p.ventana_s));
  CortarVentanas();
  c.fase = Fase::kAsentar;
  c.t_fase = ahora;
  c.quadros_fase = 0;
  c.reintentado = false;
  c.sd_avisado = false;
  MostrarEstado(c, ahora, true);
}

std::string Mas(double v, int decimales = 2) { return fmt::format("{}{:.{}f}", v >= 0 ? "+" : "", v, decimales); }

// The regularized incomplete beta's continued fraction (Numerical Recipes betacf), for PStudent.
double BetaCf(double a, double b, double x) {
  constexpr double kMin = 1e-300;
  const double qab = a + b, qap = a + 1, qam = a - 1;
  double c = 1, d = 1 - qab * x / qap;
  if (std::fabs(d) < kMin) d = kMin;
  d = 1 / d;
  double h = d;
  for (int m = 1; m <= 200; ++m) {
    const double m2 = 2.0 * m;
    double aa = m * (b - m) * x / ((qam + m2) * (a + m2));
    d = 1 + aa * d;
    if (std::fabs(d) < kMin) d = kMin;
    c = 1 + aa / c;
    if (std::fabs(c) < kMin) c = kMin;
    d = 1 / d;
    h *= d * c;
    aa = -(a + m) * (qab + m) * x / ((a + m2) * (qap + m2));
    d = 1 + aa * d;
    if (std::fabs(d) < kMin) d = kMin;
    c = 1 + aa / c;
    if (std::fabs(c) < kMin) c = kMin;
    d = 1 / d;
    const double del = d * c;
    h *= del;
    if (std::fabs(del - 1) < 1e-12) break;
  }
  return h;
}

// The two-sided p of Student's t with `gl` degrees of freedom: I_x(gl / 2, 1 / 2), x = gl / (gl + t^2).
double PStudent(double t, double gl) {
  if (!std::isfinite(t)) return 0;
  const double x = gl / (gl + t * t);
  if (x >= 1) return 1;
  const double a = gl / 2, b = 0.5;
  const double ln_beta = std::lgamma(a) + std::lgamma(b) - std::lgamma(a + b);
  const double frente = std::exp(std::log(x) * a + std::log1p(-x) * b - ln_beta);
  const double i = x < (a + 1) / (a + b + 2) ? frente * BetaCf(a, b, x) / a : 1 - frente * BetaCf(b, a, 1 - x) / b;
  return std::clamp(i, 0.0, 1.0);
}

// The paired result of one metric: each window of the step minus the mean of the control windows next to it.
std::string Pareado(const char* nombre, const std::vector<double>& d, int decimales = 2) {
  if (d.empty()) return fmt::format("{}: no pair", nombre);
  double media = 0;
  for (double x : d) media += x;
  media /= double(d.size());
  std::string lista;
  for (size_t i = 0; i < d.size(); ++i) lista += (i ? ", " : "") + Mas(d[i], decimales);
  if (d.size() < 2) return fmt::format("{} {} (one pair: no decision)", nombre, lista);
  double var = 0;
  size_t mismo_signo = 0;
  for (double x : d) {
    var += (x - media) * (x - media);
    if ((x > 0) == (media > 0) && x != 0) ++mismo_signo;
  }
  const double sd = std::sqrt(var / double(d.size() - 1));
  const double t = sd > 0 ? media / (sd / std::sqrt(double(d.size()))) : media != 0 ? INFINITY : 0.0;
  const double p = PStudent(t, double(d.size() - 1));
  const bool signos = mismo_signo == d.size();
  const char* veredicto = signos && p < 0.05 ? "REAL" : signos && p < 0.20 ? "LIKELY" : "NOT RESOLVED";
  return fmt::format("{} {} = {} +- {:.{}f}, t {} ({} df), p ~ {}, signs {}/{}: {}", nombre, lista,
                     Mas(media, decimales), sd, decimales, std::isfinite(t) ? fmt::format("{:.2f}", t) : std::string("inf"), d.size() - 1,
                     p < 0.001 ? std::string("<0.001") : fmt::format("{:.3f}", p), mismo_signo, d.size(), veredicto);
}

// The image region inside a w x h frame: x0, y0, x1, y1 (the whole frame without a region).
std::array<int, 4> RegionEn(const Prueba& p, uint32_t w, uint32_t h) {
  if (!p.con_region) return {0, 0, int(w), int(h)};
  const int x0 = std::clamp(p.region[0], 0, int(w)), y0 = std::clamp(p.region[1], 0, int(h));
  return {x0, y0, std::clamp(p.region[2], x0, int(w)), std::clamp(p.region[3], y0, int(h))};
}

// The image verdict (see the header comment) and the corner's short text.
void VeredictoImagen(Corrida& c) {
  if (!c.p.captura || c.control.empty()) return;
  const uint32_t w = c.cw, h = c.ch;
  const auto [x0, y0, x1, y1] = RegionEn(c.p, w, h);
  const double region_px = double(std::max(1, (x1 - x0) * (y1 - y0)));
  const bool ocho = c.bits != 10;
  const char* bits_txt = ocho ? " AT 8 BITS" : " (10-bit texels)";
  // The mask: what changed between the control's own captures (the first one is the reference), dilated 2 pixels.
  std::vector<uint8_t> mascara(size_t(w) * h, 0);
  bool desbordada = false;
  size_t repeticiones = 0;
  for (size_t i = 0; i < c.resultados.size(); ++i) {
    const Medida& m = c.resultados[i];
    if (!m.control || !m.img.comparada || i == c.ref) continue;  // control repeats compared, not the reference
    ++repeticiones;
    if (m.img.desbordada) desbordada = true;
    for (uint32_t i : m.img.idx) {
      const int px = int(i % w), py = int(i / w);
      for (int yy = std::max(py - 2, y0); yy <= std::min(py + 2, y1 - 1); ++yy) {
        for (int xx = std::max(px - 2, x0); xx <= std::min(px + 2, x1 - 1); ++xx) mascara[size_t(yy) * w + xx] = 1;
      }
    }
  }
  uint64_t enmascarados = 0;
  for (int y = y0; y < y1; ++y) {
    for (int x = x0; x < x1; ++x) enmascarados += mascara[size_t(y) * w + x];
  }
  const double mascara_pct = double(enmascarados) * 100.0 / region_px;
  const bool estatica = !desbordada && mascara_pct <= kMascaraMaxPct;
  const std::string region_txt = fmt::format("region {},{}-{},{}{}", x0, y0, x1, y1, bits_txt);
  if (!repeticiones) {
    REXLOG_WARN("[xmow bench] test {} image check: a single control capture, no mask ({})", c.p.id, region_txt);
  } else if (!estatica) {
    REXLOG_WARN("[xmow bench] test {} image check: SCENE NOT STATIC: the control's own captures differ in {} ({:.3f} % "
                "of the {}){}: no image result",
                c.p.id, desbordada ? "more than 5 % of the frame" : fmt::format("{} pixels", enmascarados), mascara_pct,
                region_txt, desbordada ? " or more" : "");
    const bool solo_control = std::all_of(c.p.pasos.begin(), c.p.pasos.end(), [&c](const Paso& s) {
      return s.etiqueta == c.p.pasos.front().etiqueta;
    });
    c.veredicto_imagen = solo_control ? "NOT STATIC: PAUSE AGAIN, RETRY" : "IMAGE: SCENE NOT STATIC";
    return;
  } else {
    REXLOG_WARN("[xmow bench] test {} image check: scene static ({}): {} control repeats, mask {} pixels ({:.3f} % of the "
                "region: what changed between them, left out of the comparison)",
                c.p.id, region_txt, repeticiones, enmascarados, mascara_pct);
  }
  // Per distinct non-control step: every capture against the reference, outside the mask.
  std::vector<std::string> orden;
  for (const Medida& m : c.resultados) {
    if (!m.control && std::find(orden.begin(), orden.end(), m.etiqueta) == orden.end()) orden.push_back(m.etiqueta);
  }
  int peor_global = 0;  // 0 identical, 1 near, 2 different
  for (const std::string& e : orden) {
    size_t n = 0, iguales = 0, cerca = 0, sin_comparar = 0;
    uint64_t peor_px = 0;
    int peor_delta = 0;
    std::vector<uint64_t> hashes;
    for (const Medida& m : c.resultados) {
      if (m.etiqueta != e) continue;
      if (!m.img.comparada) {
        ++sin_comparar;
        continue;
      }
      ++n;
      if (std::find(hashes.begin(), hashes.end(), m.img.hash) == hashes.end()) hashes.push_back(m.img.hash);
      uint64_t fuera = 0;
      int delta = 0;
      for (size_t k = 0; k < m.img.idx.size(); ++k) {
        if (mascara[m.img.idx[k]]) continue;
        ++fuera;
        delta = std::max(delta, int(m.img.delta[k]));
      }
      if (m.img.desbordada) {
        fuera = std::max<uint64_t>(fuera, m.img.dif_region);
        delta = std::max(delta, ocho ? 255 : 1023);
      }
      peor_px = std::max(peor_px, fuera);
      peor_delta = std::max(peor_delta, delta);
      if (fuera == 0) {
        ++iguales;
      } else if (!ocho && delta <= 1) {  // 1 of 1023; at 8 bits one step of 255 may hide 5 of 1023: no NEAR there
        ++cerca;
      }
    }
    if (sin_comparar) {
      REXLOG_WARN("[xmow bench] test {} image {}: {} captures not compared (none taken, no reference or no 10-bit "
                  "texels)", c.p.id, e, sin_comparar);
    }
    if (!n) continue;
    const int nivel = iguales == n ? 0 : iguales + cerca == n ? 1 : 2;
    peor_global = std::max(peor_global, nivel);
    REXLOG_WARN("[xmow bench] test {} image {}{}: {} of {} captures IDENTICAL, {} NEAR (at most 1 of 1023), {} DIFFERENT; "
                "worst {} pixels outside the mask ({:.4f} % of the region), max channel delta {} of {}; {} distinct "
                "hashes: {}",
                c.p.id, e, bits_txt, iguales, n, cerca, n - iguales - cerca, peor_px,
                double(peor_px) * 100.0 / region_px, peor_delta, ocho ? 255 : 1023, hashes.size(),
                nivel == 0 ? (ocho ? "IDENTICAL AT 8 BITS" : "IDENTICAL") : nivel == 1 ? "NEAR" : "DIFFERENT");
  }
  if (orden.empty()) {
    c.veredicto_imagen = enmascarados == 0 ? "SCENE STATIC" : "SCENE STATIC (SMALL MASK)";
  } else {
    c.veredicto_imagen = peor_global == 0 ? "IMAGE: IDENTICAL" : peor_global == 1 ? "IMAGE: NEAR" : "IMAGE: DIFFERENT";
  }
  if (ocho) c.veredicto_imagen += " AT 8 BITS";
}

void Resumen(Corrida& c) {
  // Groups by step label: the first step's label (the control, A) first, even with no valid window, then the others in
  // first-seen order. INVALID windows are left out.
  std::vector<std::string> orden;
  std::map<std::string, std::vector<const Medida*>> grupos;
  if (!c.p.pasos.empty()) orden.push_back(c.p.pasos.front().etiqueta);
  size_t invalidas = 0;
  for (const Medida& m : c.resultados) {
    if (m.invalida) {
      ++invalidas;
      continue;
    }
    if (std::find(orden.begin(), orden.end(), m.etiqueta) == orden.end()) orden.push_back(m.etiqueta);
    grupos[m.etiqueta].push_back(&m);
  }
  if (invalidas) REXLOG_WARN("[xmow bench] test {}: {} INVALID windows left out of the results", c.p.id, invalidas);
  for (size_t g = 0; g < orden.size(); ++g) {
    if (grupos[orden[g]].empty()) {
      REXLOG_WARN("[xmow bench] test {} result {} {}: no valid window", c.p.id, char('A' + std::min<size_t>(g, 25)),
                  orden[g]);
      continue;
    }
    double fps = 0, ms = 0, p95 = 0, max_ms = 0, gpu = 0, escena = 0, draws = 0, hilo = 0, anillo = 0, total = 0;
    double fps_min = 1e9, fps_max = 0;
    for (const Medida* m : grupos[orden[g]]) {
      fps += m->fps;
      ms += m->ms;
      p95 += m->p95;
      max_ms = std::max(max_ms, m->max_ms);
      gpu += m->gpu_ms;
      escena += m->escena_ms;
      draws += m->draws;
      hilo += m->hilo_max_ms;
      anillo += m->anillo_ms;
      total += m->total_cpu_ms;
      fps_min = std::min(fps_min, m->fps);
      fps_max = std::max(fps_max, m->fps);
    }
    const double n = double(grupos[orden[g]].size());
    REXLOG_WARN("[xmow bench] test {} result {} {} ({} windows): fps {:.2f} ({:.2f}-{:.2f}), frame {:.2f} ms, p95 {:.2f}, "
                "max {:.1f}, GPU busy {:.2f} ms, scene {:.2f} ms, {:.0f} draws, busiest thread {:.2f} ms, GPU ring {:.2f} "
                "ms, all threads {:.2f} ms per frame",
                c.p.id, char('A' + std::min<size_t>(g, 25)), orden[g], size_t(n), fps / n, fps_min, fps_max, ms / n,
                p95 / n, max_ms, gpu / n, escena / n, draws / n, hilo / n, anillo / n, total / n);
    if (g == 0 && draws / n < 2000) {
      REXLOG_WARN("[xmow bench] test {}: {:.0f} draws per frame in the control: not the jungle bench spot (about 2750)?",
                  c.p.id, draws / n);
    }
  }
  // Paired against the control windows next to each window of the other steps.
  const std::vector<Medida>& r = c.resultados;
  for (size_t g = 1; g < orden.size(); ++g) {
    std::vector<double> d_fps, d_ms, d_escena, d_gpu, d_hilo, d_anillo, d_total;
    for (size_t i = 0; i < r.size(); ++i) {
      if (r[i].invalida || r[i].control || r[i].etiqueta != orden[g]) continue;  // never a control window itself
      const Medida* antes = nullptr;
      const Medida* despues = nullptr;
      for (size_t j = i; j-- > 0;) {
        if (r[j].control && !r[j].invalida) {
          antes = &r[j];
          break;
        }
      }
      for (size_t j = i + 1; j < r.size(); ++j) {
        if (r[j].control && !r[j].invalida) {
          despues = &r[j];
          break;
        }
      }
      if (!antes && !despues) continue;
      auto vecino = [&](double Medida::*campo) {
        const double a = antes ? antes->*campo : despues->*campo;
        const double b = despues ? despues->*campo : antes->*campo;
        return r[i].*campo - (a + b) / 2.0;
      };
      d_fps.push_back(vecino(&Medida::fps));
      d_ms.push_back(vecino(&Medida::ms));
      d_escena.push_back(vecino(&Medida::escena_ms));
      d_gpu.push_back(vecino(&Medida::gpu_ms));
      d_hilo.push_back(vecino(&Medida::hilo_max_ms));
      d_anillo.push_back(vecino(&Medida::anillo_ms));
      d_total.push_back(vecino(&Medida::total_cpu_ms));
    }
    REXLOG_WARN("[xmow bench] test {} paired {} {} - neighbouring A: {}; {}; {}; {}; {}; {}; {}", c.p.id,
                char('A' + std::min<size_t>(g, 25)), orden[g], Pareado("fps", d_fps), Pareado("frame ms", d_ms),
                Pareado("scene ms", d_escena), Pareado("GPU busy ms", d_gpu), Pareado("busiest thread ms", d_hilo),
                Pareado("GPU ring ms", d_anillo), Pareado("all threads ms", d_total));
  }
  if (!orden.empty() && grupos[orden[0]].size() >= 2) {
    const Medida* a0 = grupos[orden[0]].front();
    const Medida* a1 = grupos[orden[0]].back();
    REXLOG_WARN("[xmow bench] test {} control drift (last - first A): fps {}, scene {} ms, busiest thread {} ms", c.p.id,
                Mas(a1->fps - a0->fps), Mas(a1->escena_ms - a0->escena_ms), Mas(a1->hilo_max_ms - a0->hilo_max_ms));
  }
  VeredictoImagen(c);
}

void Terminar(bool completo, const std::string& motivo) {
  Corrida* c = g_corrida;
  if (!c) return;
  std::string fallo;
  AplicarValores(*c, nullptr, &fallo);  // every option (and the held ones) back to its value before the test
  CortarVentanas();
  if (!fallo.empty()) REXLOG_WARN("[xmow bench] test {}: could not restore {}", c->p.id, fallo);
  const std::string titulo = TextoAviso(fmt::format("BENCH {}: {}", c->p.id, c->p.nombre));
  if (completo) {
    Resumen(*c);
    REXLOG_WARN("[xmow bench] test {} DONE: {} steps, options restored", c->p.id, c->p.pasos.size());
    g_aviso_final = {titulo, "DONE"};
  } else {
    if (!c->resultados.empty()) Resumen(*c);
    REXLOG_WARN("[xmow bench] test {} ABORTED at step {}/{}: {}; options restored", c->p.id, c->paso + 1,
                c->p.pasos.size(), motivo);
    g_aviso_final = {titulo, TextoAviso("ABORTED: " + motivo)};
    if (motivo.rfind("NOT PAUSED", 0) == 0) g_aviso_final.push_back("PAUSE THE GAME FIRST, WAIT 5 S");
  }
  if (!c->veredicto_imagen.empty()) g_aviso_final.push_back(TextoAviso(c->veredicto_imagen));
  g_aviso_hasta = Clock::now() + std::chrono::seconds(15);
  g_aviso_ultimo = {};
  g_corrida = nullptr;
  delete c;
  g_forzar_diag.store(false, std::memory_order_relaxed);
  g_abortar.store(false, std::memory_order_relaxed);
  rex::input::BloquearAtajos(false);
  g_bloqueante.store(false, std::memory_order_release);
  g_activo.store(false, std::memory_order_release);
}

// ---- [xmow live] ----
struct Vivo {
  std::array<uint32_t, 801> hist{};
  uint64_t n = 0, huecos = 0, sobre40 = 0, sobre50 = 0;
  double suma_ms = 0;
  Clock::time_point inicio{}, segundo{};
  uint32_t quadros_segundo = 0;
  double peor_fps = 1e9, peor_en = 0;
  Clock::time_point arranque{};
};
Vivo g_vivo;

void PorSwapVivo(Clock::time_point ahora, double dt_ms, bool primero) {
  Vivo& v = g_vivo;
  if (v.arranque == Clock::time_point{}) v.arranque = ahora;
  if (v.inicio == Clock::time_point{}) v.inicio = v.segundo = ahora;
  if (primero) return;
  if (dt_ms > 2000.0) {  // a console menu, a load, sleep: not a frame time
    ++v.huecos;
    v.segundo = ahora;
    v.quadros_segundo = 0;
  } else {
    ++v.n;
    v.suma_ms += dt_ms;
    v.hist[std::min<size_t>(size_t(dt_ms / 0.25), v.hist.size() - 1)]++;
    if (dt_ms > 40.0) ++v.sobre40;
    if (dt_ms > 50.0) ++v.sobre50;
    ++v.quadros_segundo;
    const double s = std::chrono::duration<double>(ahora - v.segundo).count();
    if (s >= 1.0) {
      const double fps = double(v.quadros_segundo) / s;
      if (fps < v.peor_fps) {
        v.peor_fps = fps;
        v.peor_en = std::chrono::duration<double>(ahora - v.arranque).count();
      }
      v.segundo = ahora;
      v.quadros_segundo = 0;
    }
  }
  const double desde = std::chrono::duration<double>(ahora - v.inicio).count();
  if (desde < 60.0) return;
  if (g_corrida && g_corrida->fase == Fase::kMedir) return;  // not inside a bench window (the service calls): after it
  const double p99 = Percentil(v.hist, v.n, 0.99), p999 = Percentil(v.hist, v.n, 0.999);
  REXLOG_WARN("[xmow live] {:.0f} s: {} frames, {:.2f} fps, 1 % low {:.1f} fps, 0.1 % low {:.1f} fps, frames over 40 ms "
              "{:.1f} %, over 50 ms {:.1f} %, worst second {} at {:.0f} s, {} gaps over 2 s left out; {} map{}; {}",
              std::chrono::duration<double>(ahora - v.arranque).count(), v.n,
              v.suma_ms > 0 ? double(v.n) * 1000.0 / v.suma_ms : 0.0, p99 > 0 ? 1000.0 / p99 : 0.0,
              p999 > 0 ? 1000.0 / p999 : 0.0, v.n ? double(v.sobre40) * 100.0 / double(v.n) : 0.0,
              v.n ? double(v.sobre50) * 100.0 / double(v.n) : 0.0,
              v.peor_fps < 1e8 ? fmt::format("{:.1f} fps", v.peor_fps) : std::string("none"), v.peor_en, v.huecos,
              XmowMapaDeJogo() ? "gameplay" : "frontend", g_corrida ? " (a bench test is running)" : "",
              MemoriaConsola());
  const Clock::time_point arranque = v.arranque;
  v = Vivo{};
  v.arranque = arranque;
  v.inicio = v.segundo = ahora;
}

}  // namespace

// =====================================================================================================================

void AplicarModoJogo() {
  static int aplicado = -1;
  const bool diag = !REXCVAR_GET(xmow_modo_jogo) || g_forzar_diag.load(std::memory_order_relaxed);
  if (int(diag) == aplicado) return;
  aplicado = int(diag);
  // The floor is the SDK's (logging.cpp): the categories keep their own levels and those created later get it too.
  rex::SetLevelFloor(diag ? spdlog::level::trace : spdlog::level::warn);
  rex::SetFlushErrorsOnly(!diag);  // 0.1.34c: PLAY MODE's WARN lines cause no SD flush of their own
  rex::x360::g_xmow_diagnosticos.store(diag, std::memory_order_relaxed);
#if defined(__SWITCH__)
  RexSwitchPerfInformes(diag);
#endif
  if (diag) {
    REXLOG_WARN("[xmow play mode] OFF: full diagnostics (log_level {}, 10 s reports, profile report, slow frame lines){}",
                rex::cvar::GetFlagByName("log_level"),
                g_forzar_diag.load(std::memory_order_relaxed) ? ", for the bench" : "");
  } else {
    std::string suprimido;
    if (const std::string v = rex::cvar::GetFlagByName("log_level"); v != "warn" && v != "error" && v != "critical") {
      suprimido += " log_level=" + v + " held at warn;";
    }
    if (rex::cvar::GetFlagByName("xmow_amostragem_pilas") == "true") suprimido += " xmow_amostragem_pilas=true suppressed;";
    if (const std::string v = rex::cvar::GetFlagByName("xmow_log_hitch_ms"); !v.empty() && v != "0") {
      suprimido += " xmow_log_hitch_ms=" + v + " suppressed;";
    }
    REXLOG_WARN("[xmow play mode] ON: the log keeps WARN and ERROR lines (and the context lines), flushed on ERROR only; "
                "10 s reports, profile report, slow frame lines and stack sampling off (LOGS AND DIAGNOSTICS > PLAY MODE "
                "NO for tests).{}",
                suprimido.empty() ? std::string() : " Asked for and not in effect:" + suprimido);
  }
}

std::vector<Entrada> Listar(const std::filesystem::path& archivo, Validador validar, std::string* error) {
  std::vector<Prueba> pruebas;
  std::vector<std::string> errores;
  std::vector<Entrada> out;
  if (!Leer(archivo, validar, pruebas, errores, error)) return out;
  for (size_t i = 0; i < pruebas.size(); ++i) {
    Entrada e;
    e.id = pruebas[i].id;
    e.nombre = pruebas[i].nombre;
    e.ayuda = pruebas[i].ayuda;
    e.segundos = Duracion(pruebas[i]);
    e.vivo = pruebas[i].vivo;
    e.error = errores[i];
    out.push_back(std::move(e));
  }
  return out;
}

bool Iniciar(const std::filesystem::path& archivo, int id, Validador validar, std::string* error) {
  if (Activo()) {
    *error = "A TEST IS ALREADY RUNNING";
    return false;
  }
  std::vector<Prueba> pruebas;
  std::vector<std::string> errores;
  if (!Leer(archivo, validar, pruebas, errores, error)) return false;
  for (size_t i = 0; i < pruebas.size(); ++i) {
    if (pruebas[i].id != id) continue;
    if (!errores[i].empty()) {
      *error = "TEST " + std::to_string(id) + ": " + errores[i];
      return false;
    }
    const bool vivo = pruebas[i].vivo;
    std::lock_guard<std::mutex> cerrojo(g_mutex);
    g_pendiente = std::make_unique<Prueba>(std::move(pruebas[i]));
    g_abortar.store(false, std::memory_order_relaxed);
    g_aviso_parado.store(false, std::memory_order_relaxed);
    g_ultimo_swap_ns.store(AhoraNs(), std::memory_order_relaxed);
    if (!vivo) rex::input::BloquearAtajos(true);
    g_bloqueante.store(!vivo, std::memory_order_release);
    g_activo.store(true, std::memory_order_release);
    g_hay_pendiente.store(true, std::memory_order_release);
    return true;
  }
  *error = "TEST " + std::to_string(id) + " IS NOT IN THE FILE";
  return false;
}

bool Activo() { return g_activo.load(std::memory_order_acquire); }
bool Bloqueante() { return g_bloqueante.load(std::memory_order_acquire); }

bool AnilloParado() {
  if (!Bloqueante()) return false;
  const int64_t sin = AhoraNs() - g_ultimo_swap_ns.load(std::memory_order_relaxed);
  if (sin < 5'000'000'000ll) return false;
  if (!g_aviso_parado.exchange(true, std::memory_order_relaxed)) {
    REXLOG_WARN("[xmow bench] no frame for {:.1f} s while a test runs: the test is aborted, the controller and the SDK "
                "shortcuts go back to the game now (the options are restored at the next frame)",
                double(sin) / 1e9);
    rex::input::BloquearAtajos(false);
    Abortar("no frame for 5 s (the controller went back to the game)");
  }
  return true;
}

void Abortar(const char* motivo, bool tambien_vivo) {
  if (!Activo()) return;
  std::lock_guard<std::mutex> cerrojo(g_mutex);
  if (!tambien_vivo && !g_bloqueante.load(std::memory_order_relaxed)) return;  // a live test goes on
  if (g_pendiente) {  // not started yet (no frame since): dropped here, nothing was changed
    REXLOG_WARN("[xmow bench] test {} dropped before it started: {}", g_pendiente->id, motivo);
    g_pendiente.reset();
    g_hay_pendiente.store(false, std::memory_order_relaxed);
    rex::input::BloquearAtajos(false);
    g_bloqueante.store(false, std::memory_order_release);
    g_activo.store(false, std::memory_order_release);
    return;
  }
  g_motivo = motivo;
  g_abortar.store(true, std::memory_order_release);
}

void PorSwap(uint64_t draws) {
  const Clock::time_point ahora = Clock::now();
  g_ultimo_swap_ns.store(AhoraNs(), std::memory_order_relaxed);
  static Clock::time_point swap_previo{};
  const bool primero = swap_previo == Clock::time_point{};
  const double dt_ms = primero ? 0.0 : std::chrono::duration<double, std::milli>(ahora - swap_previo).count();
  swap_previo = ahora;
  PorSwapVivo(ahora, dt_ms, primero);
  if (!g_corrida) {
    std::unique_ptr<Prueba> nueva;
    if (g_hay_pendiente.load(std::memory_order_acquire)) {
      std::lock_guard<std::mutex> cerrojo(g_mutex);
      nueva = std::move(g_pendiente);
      g_hay_pendiente.store(false, std::memory_order_relaxed);
    }
    if (!nueva) {
      if (!g_aviso_final.empty()) {  // DONE / ABORTED stays for 15 s (the notification lasts 4 s); never cleared here
        if (ahora >= g_aviso_hasta) {
          g_aviso_final.clear();
        } else if (ahora - g_aviso_ultimo > std::chrono::seconds(3)) {
          g_aviso_ultimo = ahora;
          rex::ui::AvisarSuperposicion(g_aviso_final);
        }
      }
      return;
    }
    g_aviso_final.clear();
    auto* c = new Corrida;
    c->p = std::move(*nueva);
    for (const Paso& paso : c->p.pasos) {
      for (const auto& [k, v] : paso.valores) {
        if (!c->originales.count(k)) c->originales[k] = rex::cvar::GetFlagByName(k);
      }
    }
    // The dynamic resolution would trade the measured cost for resolution (and change the image): off for the test.
    const std::string dinamica = rex::cvar::GetFlagByName("xmow_resolucao_dinamica");
    if (dinamica == "true" && !c->originales.count("xmow_resolucao_dinamica")) {
      c->originales["xmow_resolucao_dinamica"] = "true";
      c->forzados["xmow_resolucao_dinamica"] = "false";
    }
    std::time_t t = std::time(nullptr);
    std::tm tm{};
    localtime_r(&t, &tm);
    char sello[32];
    std::snprintf(sello, sizeof(sello), "%02d%02d%02d_%u", tm.tm_hour, tm.tm_min, tm.tm_sec, ++g_corridas);
    c->sello = sello;
    c->ultimo_swap = ahora;
    c->draws_ahora = draws;
    g_corrida = c;
    g_forzar_diag.store(!c->p.toca_modo_jogo, std::memory_order_relaxed);
    AplicarModoJogo();  // the diagnostics on before the first window
    REXLOG_WARN("[xmow bench] test {} '{}' START: {} steps, settle {:.0f} s, window {}, captures {}{}{}; diagnostics {} "
                "for the run; dynamic resolution {}; {}; xmow_bench.toml in {}",
                c->p.id, c->p.nombre, c->p.pasos.size(), c->p.asentar_s,
                c->p.ventana_quadros ? fmt::format("{} frames", c->p.ventana_quadros) : fmt::format("{:.0f} s", c->p.ventana_s),
                c->p.captura ? "yes" : "no",
                c->p.con_region ? fmt::format(" (region {},{}-{},{})", c->p.region[0], c->p.region[1], c->p.region[2],
                                              c->p.region[3])
                                : std::string(),
                c->p.vivo ? ", LIVE (played, the controller is not blocked)" : "",
                c->p.toca_modo_jogo ? "as each step sets them (the test changes PLAY MODE)" : "forced on",
                dinamica == "true" ? "ON in the settings: held OFF during the test" : "off",
                EstadoConsola(), c->p.carpeta.string());
    for (const auto& [k, v] : c->originales) REXLOG_WARN("[xmow bench] test {} original {}={}", c->p.id, k, v);
    if (c->p.toca_material) {
      uint64_t auditadas = 0, difs = 0;
      bool apagado = false;
      xmow::cpu33::EstadoMaterialReplay(&auditadas, &difs, &apagado);
      if (apagado) {
        Terminar(false, "MATERIAL REPLAY mode 2 is already OFF for this boot (an audit difference): restart the game");
        return;
      }
    }
    if (c->p.vivo) {
      EmpezarPaso(*c, ahora);
    } else {  // the pause check first: two captures 1 s apart
      c->fase = Fase::kPausa1;
      c->t_fase = ahora;
      c->quadros_fase = 0;
      c->quiere_captura = true;
      MostrarEstado(*c, ahora, true);
    }
    return;
  }
  Corrida& c = *g_corrida;
  c.ultimo_swap = ahora;
  c.draws_ahora = draws;
  if (g_abortar.load(std::memory_order_acquire)) {
    std::string motivo;
    {
      std::lock_guard<std::mutex> cerrojo(g_mutex);
      motivo = g_motivo;
    }
    Terminar(false, motivo.empty() ? std::string("aborted") : motivo);
    return;
  }
#if defined(__SWITCH__)
  if (appletGetFocusState() != AppletFocusState_InFocus) {
    Terminar(false, "the game lost the focus (HOME menu, sleep or an overlay)");
    return;
  }
#endif
  if (!c.p.vivo && dt_ms > kSinQuadro_ms) {
    Terminar(false, fmt::format("no frame for {:.1f} s", dt_ms / 1000.0));
    return;
  }
  // The frame after the bench's own capture (GPU wait, conversion, copies) is not a stall of the game.
  const bool tras_captura = c.tras_captura;
  c.tras_captura = false;
  const bool paron = !c.p.vivo && dt_ms > kParon_ms && !tras_captura;
  ++c.quadros_fase;
  const double en_fase = std::chrono::duration<double>(ahora - c.t_fase).count();
  switch (c.fase) {
    case Fase::kPausa1:
    case Fase::kPausa2:
      if (c.quiere_captura && c.quadros_fase > 120) {  // Captura is not being called (no renderer)
        Terminar(false, "NO PAUSE CHECK POSSIBLE: no capture of the guest output");
        break;
      }
      if (paron) {  // start the check again after a stall
        REXLOG_WARN("[xmow bench] test {}: a {:.0f} ms frame during the pause check, it starts again", c.p.id, dt_ms);
        c.fase = Fase::kPausa1;
        c.t_fase = ahora;
        c.quadros_fase = 0;
        c.quiere_captura = true;
      } else if (c.fase == Fase::kPausa2 && en_fase >= 1.0 && !c.quiere_captura) {
        c.quiere_captura = true;
      }
      break;
    case Fase::kAsentar: {
      if (paron) {
        REXLOG_WARN("[xmow bench] test {} step {}/{}: a {:.0f} ms frame while settling, the settle starts again", c.p.id,
                    c.paso + 1, c.p.pasos.size(), dt_ms);
        c.t_fase = ahora;
        break;
      }
      if (en_fase < c.p.asentar_s) break;
      const uint32_t sd = rex::x360::EscriturasSdPendientes();
      if (sd > 0 && en_fase < c.p.asentar_s + 20.0) break;  // the SD card's writes (captures, raw samples) first
      if (sd > 0 && !c.sd_avisado) {
        c.sd_avisado = true;
        REXLOG_WARN("[xmow bench] test {} step {}/{}: {} SD writes still pending after 20 s, the window starts anyway",
                    c.p.id, c.paso + 1, c.p.pasos.size(), sd);
      }
      EmpezarMedida(c, ahora);
      break;
    }
    case Fase::kMedir: {
      if (c.quadros_fase > 1) {  // the first Swap after the cut closes the frame that started before it
        if (paron) {
          if (!c.reintentado) {
            c.reintentado = true;
            REXLOG_WARN("[xmow bench] test {} step {}/{}: a {:.0f} ms frame in the window: settle and window again",
                        c.p.id, c.paso + 1, c.p.pasos.size(), dt_ms);
            CortarVentanas();
            c.fase = Fase::kAsentar;
            c.t_fase = ahora;
            c.quadros_fase = 0;
            break;
          }
          if (!c.invalida) {
            c.invalida = true;
            c.motivo += std::string(c.motivo.empty() ? "" : "; ") + fmt::format("a {:.0f} ms frame (second stall)", dt_ms);
          }
        }
        ++c.n;
        c.suma_ms += dt_ms;
        c.max_ms = std::max(c.max_ms, dt_ms);
        c.hist[std::min<size_t>(size_t(dt_ms / 0.25), c.hist.size() - 1)]++;
      } else {
        c.t_medir = ahora;
      }
      const bool fin = c.p.ventana_quadros ? c.n >= c.p.ventana_quadros : en_fase >= c.p.ventana_s;
      if (fin) TerminarMedida(c, ahora);
      break;
    }
    case Fase::kCaptura:
      if (c.quadros_fase > 60) {  // Captura was never called (no renderer): go on without the image
        REXLOG_WARN("[xmow bench] test {} step {}/{}: no capture taken", c.p.id, c.paso + 1, c.p.pasos.size());
        c.quiere_captura = false;
        ++c.paso;
        EmpezarPaso(c, ahora);
      }
      break;
  }
  if (g_corrida) MostrarEstado(*g_corrida, ahora);
}

bool QuiereCaptura() { return g_corrida && g_corrida->quiere_captura; }

namespace {

// The largest channel difference of two packed texels (R | G << 10 | B << 20).
int DeltaCanal(uint32_t a, uint32_t b) {
  int d = 0;
  for (int s = 0; s < 30; s += 10) d = std::max(d, std::abs(int((a >> s) & 0x3FF) - int((b >> s) & 0x3FF)));
  return d;
}

// The capture as packed texels, R | G << 10 | B << 20 (alpha dropped), width * height: the presented RGB10A2 texels when
// the run compares at 10 bits, else the 8-bit image's channels (0-255 each). The run's first capture sets its mode.
// False: this capture has no 10-bit texels but the run compares at 10 bits.
bool Texeles(Corrida& c, const rex::ui::RawImage& img, const std::vector<uint32_t>* crudo, std::vector<uint32_t>& out) {
  const size_t n = size_t(img.width) * img.height;
  const bool hay10 = crudo && crudo->size() == n;
  if (c.bits == 0) {
    c.bits = hay10 ? 10 : 8;
    if (!hay10) {
      REXLOG_WARN("[xmow bench] test {}: no 10-bit texels from the presenter: the image checks compare AT 8 BITS",
                  c.p.id);
    }
  }
  if (c.bits == 10) {
    if (!hay10) return false;
    out.resize(n);
    for (size_t i = 0; i < n; ++i) out[i] = (*crudo)[i] & 0x3FFFFFFFu;
    return true;
  }
  out.resize(n);
  for (uint32_t y = 0; y < img.height; ++y) {
    const uint8_t* s = img.data.data() + size_t(y) * img.stride;
    for (uint32_t x = 0; x < img.width; ++x, s += 4) {
      out[size_t(y) * img.width + x] = uint32_t(s[0]) | uint32_t(s[1]) << 10 | uint32_t(s[2]) << 20;
    }
  }
  return true;
}

// The pause check: the share of the region's pixels whose largest channel difference is over `umbral`.
double PctCambiado(const std::vector<uint32_t>& a, const std::vector<uint32_t>& b, uint32_t w,
                   const std::array<int, 4>& r, int umbral) {
  uint64_t n = 0;
  for (int y = r[1]; y < r[3]; ++y) {
    for (int x = r[0]; x < r[2]; ++x) {
      const size_t i = size_t(y) * w + size_t(x);
      if (a[i] != b[i] && DeltaCanal(a[i], b[i]) > umbral) ++n;
    }
  }
  return double(n) * 100.0 / double(std::max<int64_t>(1, int64_t(r[2] - r[0]) * (r[3] - r[1])));
}

void EscribirBmp(const std::filesystem::path& ruta, const rex::ui::RawImage& imagen) {
  const uint32_t w = imagen.width, h = imagen.height;
  const uint32_t fila_bmp = (w * 3 + 3) & ~3u;
  std::vector<uint8_t> archivo(54 + size_t(fila_bmp) * h, 0);
  auto put32 = [&](size_t at, uint32_t v) { std::memcpy(archivo.data() + at, &v, 4); };
  archivo[0] = 'B';
  archivo[1] = 'M';
  put32(2, uint32_t(archivo.size()));
  put32(10, 54);
  put32(14, 40);
  put32(18, w);
  put32(22, h);
  archivo[26] = 1;
  archivo[28] = 24;
  put32(34, fila_bmp * h);
  for (uint32_t y = 0; y < h; ++y) {
    const uint8_t* s = imagen.data.data() + size_t(h - 1 - y) * imagen.stride;
    uint8_t* d = archivo.data() + 54 + size_t(y) * fila_bmp;
    for (uint32_t x = 0; x < w; ++x, s += 4, d += 3) {
      d[0] = s[2];
      d[1] = s[1];
      d[2] = s[0];
    }
  }
  rex::x360::EncolarEscrituraSd([ruta, archivo = std::move(archivo)]() {
    if (!rex::x360::EscribirArchivoEnTrozos(ruta, archivo.data(), archivo.size())) {
      REXLOG_WARN("[xmow bench] capture: could not write {}", ruta.string());
    }
  });
}

}  // namespace

void Captura(const rex::ui::RawImage* imagen, const std::vector<uint32_t>* crudo) {
  Corrida* cp = g_corrida;
  if (!cp || !cp->quiere_captura) return;
  Corrida& c = *cp;
  c.quiere_captura = false;
  c.tras_captura = true;
  const Clock::time_point ahora = Clock::now();
  const bool valida = imagen && imagen->width && imagen->height &&
                      imagen->data.size() >= size_t(imagen->height - 1) * imagen->stride + size_t(imagen->width) * 4;
  // ---- the pause check ----
  if (c.fase == Fase::kPausa1 || c.fase == Fase::kPausa2) {
    std::vector<uint32_t> texeles;
    if (!valida || !Texeles(c, *imagen, crudo, texeles)) {
      Terminar(false, valida ? "NO PAUSE CHECK POSSIBLE: no 10-bit texels of the guest output"
                             : "NO PAUSE CHECK POSSIBLE: no capture of the guest output");
      return;
    }
    if (c.fase == Fase::kPausa1) {
      c.pausa = std::move(texeles);
      c.cw = imagen->width;
      c.ch = imagen->height;
      c.fase = Fase::kPausa2;
      c.t_fase = ahora;
      c.quadros_fase = 0;
      return;
    }
    if (imagen->width != c.cw || imagen->height != c.ch) {
      c.fase = Fase::kPausa1;  // the output size changed: again
      c.t_fase = ahora;
      c.quiere_captura = true;
      return;
    }
    const std::array<int, 4> r = RegionEn(c.p, c.cw, c.ch);
    const double pct = PctCambiado(c.pausa, texeles, c.cw, r, c.bits == 10 ? kPausaDelta10 : 2);
    c.pausa.clear();
    c.pausa.shrink_to_fit();
    c.cw = c.ch = 0;
    if (pct > kPausaPct) {
      Terminar(false, fmt::format("NOT PAUSED: {:.2f} % of the region's pixels changed in 1 s (pause the game and wait "
                                  "5 s)", pct));
      return;
    }
    REXLOG_WARN("[xmow bench] test {}: pause check passed ({:.3f} % of the pixels of region {},{}-{},{} changed in 1 s{})",
                c.p.id, pct, r[0], r[1], r[2], r[3], c.bits == 10 ? "" : ", AT 8 BITS");
    EmpezarPaso(c, ahora);
    return;
  }
  if (c.fase != Fase::kCaptura || c.resultados.empty()) return;
  Medida& m = c.resultados.back();
  if (!valida) {
    REXLOG_WARN("[xmow bench] test {} step {}/{}: no guest output to capture", c.p.id, c.paso + 1, c.p.pasos.size());
    ++c.paso;
    EmpezarPaso(c, ahora);
    return;
  }
  const uint32_t w = imagen->width, h = imagen->height;
  const auto [x0, y0, x1, y1] = RegionEn(c.p, w, h);
  std::vector<uint32_t> texeles;
  std::string veredicto;
  if (!Texeles(c, *imagen, crudo, texeles)) {
    veredicto = "no 10-bit texels for this capture: not compared";
  } else {
    // The region's hash: the packed texels row by row (10 bits a channel; the 8-bit values at 8 bits).
    XXH3_state_t* estado = XXH3_createState();
    XXH3_64bits_reset(estado);
    for (int y = y0; y < y1; ++y) {
      XXH3_64bits_update(estado, texeles.data() + size_t(y) * w + size_t(x0), size_t(x1 - x0) * 4);
    }
    m.img.hash = XXH3_64bits_digest(estado);
    XXH3_freeState(estado);
    m.img.hay = true;
  }
  // Against the reference (the test's first control capture).
  if (!m.img.hay) {
    // not compared (see above)
  } else if (c.control.empty() && !m.control) {
    veredicto = "no reference (the control capture failed): not compared";
  } else if (c.control.empty()) {
    c.control = std::move(texeles);
    c.cw = w;
    c.ch = h;
    c.ref = c.resultados.size() - 1;
    veredicto = "the reference image";
  } else if (c.cw != w || c.ch != h) {
    m.img.comparada = true;
    m.img.desbordada = true;
    m.img.dif_region = m.img.dif_total = uint64_t(w) * h;
    veredicto = fmt::format("size {}x{} differs from the reference's {}x{}", w, h, c.cw, c.ch);
  } else {
    m.img.comparada = true;
    const size_t kMaxDif = size_t(w) * h / 20;
    uint32_t bx0 = w, by0 = h, bx1 = 0, by1 = 0;  // the box of the differing pixels (HUD or scene)
    for (uint32_t y = 0; y < h; ++y) {
      const uint32_t* a = c.control.data() + size_t(y) * w;
      const uint32_t* b = texeles.data() + size_t(y) * w;
      if (std::memcmp(a, b, size_t(w) * 4) == 0) continue;
      for (uint32_t x = 0; x < w; ++x) {
        if (a[x] == b[x]) continue;
        ++m.img.dif_total;
        bx0 = std::min(bx0, x);
        by0 = std::min(by0, y);
        bx1 = std::max(bx1, x + 1);
        by1 = std::max(by1, y + 1);
        const int d = DeltaCanal(a[x], b[x]);
        m.img.max_delta = std::max(m.img.max_delta, d);
        if (int(x) >= x0 && int(x) < x1 && int(y) >= y0 && int(y) < y1) {
          ++m.img.dif_region;
          if (m.img.idx.size() < kMaxDif) {
            m.img.idx.push_back(y * w + x);
            m.img.delta.push_back(uint16_t(d));
          } else {
            m.img.desbordada = true;
          }
        }
      }
    }
    const double region_px = double(std::max(1, (x1 - x0) * (y1 - y0)));
    veredicto = fmt::format("{} of {} region pixels differ from the reference ({:.4f} %), whole frame {} ({:.4f} %){}, "
                            "max channel delta {} of {}{}",
                            m.img.dif_region, uint64_t(region_px), double(m.img.dif_region) * 100.0 / region_px,
                            m.img.dif_total, double(m.img.dif_total) * 100.0 / double(uint64_t(w) * h),
                            m.img.dif_total ? fmt::format(" inside {},{}-{},{}", bx0, by0, bx1, by1) : std::string(),
                            m.img.max_delta, c.bits == 10 ? 1023 : 255,
                            m.img.desbordada ? " (over 5 % of the frame: not all kept)" : "");
  }
  // The BMP (24-bit, bottom-up), written by the SD writer thread (the next settle waits for it).
  const auto carpeta = c.p.carpeta / "capturas";
  std::error_code ec;
  std::filesystem::create_directories(carpeta, ec);
  const auto ruta = carpeta / fmt::format("bench_{:02}_{}_{:02}.bmp", c.p.id, c.sello, c.paso + 1);
  EscribirBmp(ruta, *imagen);
  REXLOG_WARN("[xmow bench] test {} step {}/{} {} capture {}: hash {:016X} (region {},{}-{},{}, {}); {}", c.p.id,
              c.paso + 1, c.p.pasos.size(), m.etiqueta, ruta.filename().string(), m.img.hash, x0, y0, x1, y1,
              c.bits == 10 ? "10-bit texels" : "AT 8 BITS", veredicto);
  ++c.paso;
  EmpezarPaso(c, ahora);
}

}  // namespace xmow::bench
