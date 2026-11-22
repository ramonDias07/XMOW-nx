// XMOW - official GPU boost. See xmow_gpu_boost.h. Ported from Budokai3-nx (app/src/graphics.cpp: AplicarGpuBoost and
// TickGpuBoost).

#include "xmow_gpu_boost.h"

#include <atomic>

#include <pthread.h>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ui/switch_apm.h>
#include <rex/ui/switch_saltynx.h>

#include <switch.h>

REXCVAR_DEFINE_BOOL(xmow_gpu_boost, false, "XMOW",
                    "Official GPU profile of 460.8 MHz (apm), handheld only; docked it does nothing. Turning it off "
                    "restores the previous profile. Live.");
REXCVAR_DEFINE_BOOL(xmow_gpu_boost_ram1600, true, "XMOW",
                    "The GPU boost uses the official handheld mode GPU 460 + RAM 1600 (fixes audio crackle at stock "
                    "CPU). Live.");

namespace xmow::gpu_boost {
namespace {

std::atomic<bool> g_ocupado{false};  // an apply or a restore is running on its pthread
bool g_estado = false;               // what Sondear believes is applied (the ring thread only)
bool g_ram_aplicada = false;         // xmow_gpu_boost_ram1600 at the time of the last apply

// true = the request was taken (or there was nothing to do); false = try again at the next Sondear.
bool Aplicar(bool ligar) {
  const bool dock = rex::ui::switch_saltynx::ModoBase(appletGetOperationMode() == AppletOperationMode_Console);
  if (!ligar) {
    // A request to turn it on is still being applied in the background: "off" now would find nothing to restore and
    // the thread would turn it on afterwards, stuck until restart. Return false so Sondear tries again later.
    if (g_ocupado.load()) {
      return false;
    }
    if (!RexSwitchApmAtivo()) {
      return true;  // nothing to restore
    }
    if (g_ocupado.exchange(true)) {
      return false;
    }
    // Restaurar sleeps ~0.5 s and reads clkrst: keep it off the game's threads (same pthread mechanism as the on path).
    pthread_t hilo_off;
    const int rc_off = pthread_create(&hilo_off, nullptr, [](void*) -> void* {
      if (RexSwitchApmRestaurar() == 1) {
        REXLOG_INFO("[xmow gpu boost] off: previous profile restored");
      } else {
        REXLOG_WARN("[xmow gpu boost] off, but apm REJECTED the previous profile (see [apm] in the log)");
      }
      g_ocupado.store(false);
      return nullptr;
    }, nullptr);
    if (rc_off != 0) {
      REXLOG_WARN("[xmow gpu boost] pthread_create failed ({}): boost not restored", rc_off);
      g_ocupado.store(false);
      return false;
    }
    pthread_detach(hilo_off);
    return true;
  }
  if (dock) {
    REXLOG_INFO("[xmow gpu boost] ignored: console is docked (handheld only)");
    return false;
  }
  if (g_ocupado.load()) {
    return false;  // an apply or a restore is still running in the background; retry next tick
  }
  if (RexSwitchApmAtivo()) {
    return true;
  }
  if (g_ocupado.exchange(true)) {
    return false;
  }
  // Off the game's threads: RexSwitchApmAplicar waits for the clocks (up to a few seconds) and checks the memory clock
  // did not move; if it did, it falls back to the original profile (see switch_apm.cpp).
  // pthread, not std::thread: std::thread throws std::system_error "Function not implemented" on this toolchain
  // (the Budokai 3 0.1.43 crash: std::terminate in AplicarGpuBoost).
  pthread_t hilo;
  RexSwitchApmPermitirRam1600(REXCVAR_GET(xmow_gpu_boost_ram1600) ? 1 : 0);
  const int rc = pthread_create(&hilo, nullptr, [](void*) -> void* {
    RexSwitchApmPedirGpuMhz(460);
    RexSwitchApmAplicar();
    REXLOG_INFO("[xmow gpu boost] on: {}", RexSwitchApmAtivo() ? "GPU 460.8 MHz active"
                                                                : "not applied (see [apm] in the log: no safe profile)");
    if (!REXCVAR_GET(xmow_gpu_boost) && RexSwitchApmAtivo()) {
      // Turned off from the menu while this was being applied.
      RexSwitchApmRestaurar();
      REXLOG_INFO("[xmow gpu boost] turned off right after applying (asked during the apply): previous profile restored");
    }
    g_ocupado.store(false);
    return nullptr;
  }, nullptr);
  if (rc != 0) {
    REXLOG_WARN("[xmow gpu boost] pthread_create failed ({}): boost not applied", rc);
    g_ocupado.store(false);
    return false;
  }
  pthread_detach(hilo);
  return true;
}

}  // namespace

void Sondear() {
  const bool dock = rex::ui::switch_saltynx::ModoBase(appletGetOperationMode() == AppletOperationMode_Console);
  const bool quer = REXCVAR_GET(xmow_gpu_boost) && !dock;  // docked it turns itself off, and returns on undocking
  const bool ram = REXCVAR_GET(xmow_gpu_boost_ram1600);
  if (quer && g_estado && RexSwitchApmAtivo() && ram != g_ram_aplicada) {
    // The RAM 1600 cvar changed while the boost is on: restore now, and the next call applies it again.
    if (Aplicar(false)) {
      REXLOG_INFO("[xmow gpu boost] xmow_gpu_boost_ram1600 changed ({}): boost reapplied on the next cycle", ram);
      g_estado = false;
    }
    return;
  }
  if (quer == g_estado) return;
  if (Aplicar(quer)) {
    g_estado = quer;
    if (quer) g_ram_aplicada = ram;
  }
}

}  // namespace xmow::gpu_boost
