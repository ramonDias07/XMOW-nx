// XMOW 0.1.33 - render thread CPU cuts. Shared by the xmow_cpu33_*.cpp files.
#pragma once

#include <atomic>
#include <cstdint>

struct PPCContext;

namespace xmow::cpu33 {

// Statistics only: written by the render thread (the hooks), read and reset by whoever prints the report (the ring
// thread). A plain load, add and store: no LL/SC loop in the hot hooks. 0.1.33d (review N-1): a reset that falls
// between the writer's load and store is undone by that store, so the next report shows that interval's count again
// (one line about double). Nothing decides on these counters; 64-bit aligned values do not tear on AArch64.
struct Contador {
  void Sumar(uint64_t n = 1) noexcept { v_ = v_ + n; }
  uint64_t Tomar() noexcept {
    const uint64_t v = v_;
    v_ = 0;
    return v;
  }
  volatile uint64_t v_ = 0;
};

// One call next to the "[xmow perf]" lines of gow2_gpu.cpp Report(): prints one "[xmow cpu33]" line per feature of the
// xmow_cpu33_*.cpp files that is on or has counted something, with the counters of the interval, and resets them.
void Informe(double seconds);

// Per-file parts of Informe (each file prints its own lines).
void InformeParametros(double seconds);  // xmow_cpu33_params.cpp
void InformeMaterial(double seconds);    // xmow_cpu33_material.cpp

// xmow_material_replay (xmow_cpu33_material.cpp): while one sub_828C3270 call is being recorded, the context of the
// thread running it; the RHI constant setter hooks (xmow_cpu33_params.cpp) then report each call before doing it.
extern std::atomic<PPCContext*> g_cap_ctx;
void TocarConstante(PPCContext& ctx, uint8_t* base, int pixel);

}  // namespace xmow::cpu33
