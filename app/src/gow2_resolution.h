// XMOW (from gears2-nx) - the launch video mode: 1280x720 (the default) or 960x540.
//
// ONLY the SDK half of gears2-nx's resolution choice is here: the video_mode_width / video_mode_height cvars, which
// the game reads through the OS display mode (VdQueryVideoMode / VdGetCurrentDisplayInformation). The other half, the
// game patch that rewrote the scene size inside Gears of War 2's own code (patch_scene_resolution.py,
// sub_82806A80), does not exist for Wolverine and is not run. 540p is therefore EXPERIMENTAL here: untested, and it
// depends on how Wolverine's engine sizes its scene from the display mode. 720p is the default and the only
// automatic choice (no CPU clock based 540p as in gears2-nx).
//
// What decides, first that is set: xmow_resolucion.txt beside the NRO ("720" or "540"), then gow2_resolucion in
// xmow.toml (720 or 540), else 720. Applies at the next start.

#pragma once

#include <cstdint>
#include <cstdio>
#include <filesystem>

#include <rex/cvar.h>
#include <rex/filesystem.h>

#if defined(__SWITCH__)
#include <switch.h>
#endif

namespace gow2::resolucion {

inline std::filesystem::path Fichero() { return rex::filesystem::GetExecutableFolder() / "xmow_resolucion.txt"; }

// The saved choice (720 or 540), 0 if there is none.
inline uint32_t Guardada() {
  FILE* f = std::fopen(Fichero().string().c_str(), "r");
  if (!f) return 0;
  unsigned valor = 0;
  const bool leido = std::fscanf(f, "%u", &valor) == 1;
  std::fclose(f);
  return leido && (valor == 720 || valor == 540) ? valor : 0;
}

inline bool Guardar(uint32_t valor) {
  FILE* f = std::fopen(Fichero().string().c_str(), "w");
  if (!f) return false;
  const bool escrito = std::fprintf(f, "%u\n", unsigned(valor)) > 0;
  return std::fclose(f) == 0 && escrito;
}

// The CPU clock in MHz (0 if it cannot be read).
inline uint32_t CpuMhz() {
#if defined(__SWITCH__)
  uint32_t hz = 0;
  if (R_SUCCEEDED(clkrstInitialize())) {
    ClkrstSession sesion;
    if (R_SUCCEEDED(clkrstOpenSession(&sesion, PcvModuleId_CpuBus, 3))) {
      clkrstGetClockRate(&sesion, &hz);
      clkrstCloseSession(&sesion);
    }
    clkrstExit();
  }
  return hz / 1000000;
#else
  return 0;
#endif
}

// The choice that applies: the saved one, else the default (PorDefecto, gow2_resolution.cpp).
uint32_t Elegida();

// Set the choice before the game sets up its renderer (gow2_app.h: OnPreLaunchModule).
void Aplicar();

}  // namespace gow2::resolucion
