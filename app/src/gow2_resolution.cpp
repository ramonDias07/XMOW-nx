// XMOW (from gears2-nx) - applying the launch video mode (gow2_resolution.h): only the SDK's video_mode_width /
// video_mode_height. gears2-nx's game patch for the scene size (Gears addresses) is not part of this build.

#include <cstdint>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ui/flags.h>

#include "gow2_resolution.h"

REXCVAR_DEFINE_INT32(gow2_resolucion, 0, "XMOW",
                     "Modo de video al iniciar si xmow_resolucion.txt no existe: 720 (1280x720) o 540 (960x540, "
                     "EXPERIMENTAL: sin parche de escena para este juego). 0 = 720. Al reiniciar")
    .range(0, 720);

namespace gow2::resolucion {

namespace {

uint32_t PorDefecto() {
  const int32_t pedida = REXCVAR_GET(gow2_resolucion);
  return pedida == 540 ? 540 : 720;
}

}  // namespace

uint32_t Elegida() {
  const uint32_t guardada = Guardada();
  return guardada ? guardada : PorDefecto();
}

void Aplicar() {
  if (Elegida() == 540) {
    REXCVAR_SET(video_mode_width, 960);
    REXCVAR_SET(video_mode_height, 540);
  }
  REXLOG_INFO("xmow: modo de video {}p ({}; CPU {} MHz){}", Elegida(),
              Guardada() ? "guardado en xmow_resolucion.txt" : "por defecto", CpuMhz(),
              Elegida() == 540 ? " [EXPERIMENTAL: solo video_mode del SDK]" : "");
}

}  // namespace gow2::resolucion
