// XMOW - the guest's Direct3D waits (src/xmow_perf_hooks.cpp) and the GPU ring thread (src/gow2_gpu.cpp).
#pragma once

#include <cstdint>

namespace xmow::perf {

// The ring thread made progress the guest can see: a read pointer writeback, a fence or scratch value written to guest
// memory, an interrupt. Wakes the threads that xmow_spin_cede put to wait in Direct3D's spin. Cheap when nobody waits
// (an atomic increment and a load).
void AvisarProgreso();

// The counters of the last interval, reset: spin waits that yielded and their total time, and occlusion query GetData
// calls answered at once (xmow_consulta_inmediata).
struct Contadores {
  uint64_t esperas = 0, esperas_us = 0, consultas = 0;
};
Contadores LeerContadores();

}  // namespace xmow::perf
