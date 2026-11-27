// XMOW 0.1.34: PLAY MODE (xmow_modo_jogo), the automatic bench (advanced menu > TESTS, xmow_bench.toml beside the
// NRO) and (0.1.34b) the once-a-minute live frame time line. See xmow_bench.cpp.
#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include <rex/logging.h>

namespace rex::ui {
struct RawImage;
// sdk-g2 presentador.cpp (0.1.34c): while set, CaptureGuestOutput on this thread also copies the RGB10A2 texels as they
// are (width * height) into *destino.
void PedirCapturaCruda(std::vector<uint32_t>* destino);
}  // namespace rex::ui
namespace rex::x360 {
extern std::atomic<bool> g_xmow_diagnosticos;  // sdk-g2 destinos.cpp: false = PLAY MODE (periodic reports off)
}

namespace xmow::bench {

// ---- PLAY MODE ----
// Once per game frame on the ring thread (gow2_gpu.cpp, at the Swap): applies xmow_modo_jogo (or the bench's forced
// diagnostics) to the log level floor, the 10 s reports of the port, the renderer and the profiler.
void AplicarModoJogo();
// True while PLAY MODE is in effect (diagnostics off).
inline bool ModoJogoEfetivo() { return !rex::x360::g_xmow_diagnosticos.load(std::memory_order_relaxed); }

// ---- The bench ----
struct Entrada {
  int id = 0;
  std::string nombre;   // as in the file
  std::string ayuda;    // the test's "help" text (menu marquee)
  double segundos = 0;  // estimated length
  bool vivo = false;    // live test: played, the controller is not blocked
  std::string error;    // why it cannot run (empty: runnable)
};
// The tests of the file, for the menu (input thread). `error` gets a file-level error (missing, does not parse).
// `validar(cvar, tipo)` returns an empty string for a cvar the bench may set with a TOML value of that type ('b'
// boolean, 'i' integer, 'f' float, 's' string), else the reason it may not.
using Validador = std::string (*)(const std::string& cvar, char tipo);
std::vector<Entrada> Listar(const std::filesystem::path& archivo, Validador validar, std::string* error);
// Starts test `id` of the file (input thread). False with `error` set when it cannot.
bool Iniciar(const std::filesystem::path& archivo, int id, Validador validar, std::string* error);
// A test is queued or running.
bool Activo();
// A test that blocks the game's controller (every test but a live one) is queued or running.
bool Bloqueante();
// A blocking test is running but the ring has given no frame for 5 s: the menu gives the controller back. The first
// time (0.1.34c) the test is aborted (restored at the next frame) and the SDK's shortcuts come back at once.
bool AnilloParado();
// Asks the running test to stop (any thread); a test not started yet is dropped at once, a running one is restored at
// the next frame. `tambien_vivo` false: a live test goes on (a map package while playing is normal).
void Abortar(const char* motivo, bool tambien_vivo = true);
// Ring thread, at each Swap after the frame is presented. `draws` = the ring's draw counter since start.
void PorSwap(uint64_t draws);
// Ring thread, right after PorSwap: the bench wants the presented frame now.
bool QuiereCaptura();
// `imagen` null: no guest output to capture. `crudo` (0.1.34c): the same frame's RGB10A2 texels (PedirCapturaCruda), for
// the exact 10-bit compare and hash; null or the wrong size: the 8-bit image is used and the verdicts say so.
void Captura(const rex::ui::RawImage* imagen, const std::vector<uint32_t>* crudo);

}  // namespace xmow::bench

// Context lines (map packages, menu changes, the bench): INFO normally, WARN in PLAY MODE (whose log keeps WARN and up),
// so a crash report keeps them. 0.1.34c: in PLAY MODE the log flushes on ERROR only (rex::SetFlushErrorsOnly): these
// lines cause no SD write of their own; a crash drains the queue (RexLogDrenarParaCrash).
#define XMOW_LOG_CONTEXTO(...)              \
  do {                                      \
    if (::xmow::bench::ModoJogoEfetivo()) { \
      REXLOG_WARN(__VA_ARGS__);             \
    } else {                                \
      REXLOG_INFO(__VA_ARGS__);             \
    }                                       \
  } while (0)
