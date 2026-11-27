// XMOW - official GPU boost (460.8 MHz handheld profile through apm), ported from the Budokai 3 port.
//
// Cvars (category "XMOW", both live):
//   xmow_gpu_boost         (bool, default false) the 460.8 MHz profile, handheld only; docked it does nothing. Turning
//                          it off gives the console's previous profile back.
//   xmow_gpu_boost_ram1600 (bool, default true) the boost uses the official handheld mode GPU 460 + RAM 1600 (fixes
//                          audio crackle at stock CPU).
//   (0.1.28: xmow_cpu_boost and xmow_pico_boost of 0.1.26 were removed: test 34, ANALISE.md section 3.)
//
// The apply and the restore run on a pthread (std::thread throws "Function not implemented" on this toolchain): they
// sleep and poll the clocks for a second or two, which must not stall the game's threads.

#pragma once

#include <cstdint>

namespace xmow::gpu_boost {

// Call about every 30 frames (gow2_gpu.cpp calls it from the ring thread's Swap). Cheap: it reads two cvars and the
// docked/handheld mode, and only starts the background work when the wanted state differs from the applied one (so a
// dock change or a cvar change is picked up within 30 frames).
void Sondear();

}  // namespace xmow::gpu_boost
