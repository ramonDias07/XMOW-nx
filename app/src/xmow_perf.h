// XMOW - the guest's Direct3D waits (src/xmow_perf_hooks.cpp) and the GPU ring thread (src/gow2_gpu.cpp).
#pragma once

#include <cstdint>

namespace xmow::perf {

// The ring thread made progress the guest can see: a read pointer writeback, a fence or scratch value written to guest
// memory, an interrupt. Wakes the threads that xmow_spin_cede put to wait in Direct3D's spin. Cheap when nobody waits
// (an atomic increment and a load).
void AvisarProgreso();

// The counters of the last interval, reset: spin waits that yielded and their total time, and occlusion query GetData
// calls answered at once (xmow_consulta_inmediata). With xmow_oclusao_conservadora: GetData answered by Direct3D from
// what the ring wrote (the history's answer), and those answered visible at once because the ring had not got there.
struct Contadores {
  uint64_t esperas = 0, esperas_us = 0, consultas = 0;
  uint64_t consultas_anillo = 0, consultas_tarde = 0;
  // 0.1.28: Direct3D's frame throttle (sub_82B3CFD0 with a frame fence pending): calls and time inside, waits included.
  uint64_t freno = 0, freno_us = 0;
  // 0.1.30 (xmow_d3d_nativo): Direct3D's dirty-state copies into its command buffer done natively (sub_82B43C28 shader
  // constants: calls and registers; sub_82B43890 register blocks: calls and registers), and the runs that went to
  // Direct3D's own slow path (sub_82B43248, the segment is full).
  uint64_t nativo_constantes = 0, nativo_constantes_regs = 0, nativo_bloques = 0, nativo_bloques_regs = 0;
  uint64_t nativo_lentos = 0;
  // 0.1.32 (xmow_sem_prepasso_motor): RenderPrePass calls with the option on, prepass draw lists left out, calls run as
  // before because a condition failed, and the conditions that failed (bits: 1 gow2_sin_prepaso_z off,
  // 2 gow2_sin_prepaso_enmascarado off, 4 occlusion queries issued, 8 GUseTilingCode set).
  uint64_t prepaso_llamadas = 0, prepaso_listas_omitidas = 0, prepaso_bloqueados = 0;
  uint32_t prepaso_motivo = 0;
};
Contadores LeerContadores();

// Total time (us) the guest spent in Direct3D's yielded spin since the start, never reset (for the [xmow hitch] deltas).
uint64_t EsperaD3dTotalUs();

// xmow_afinidade_threads: pins the CALLING thread for its role and remembers it, or lets it go back to the process
// cores when the cvar is off (checked at every call, cheap). Roles: 0 = game main thread (core 0), 1 = UE3 render
// thread (core 1, priority kept), 2 = GPU ring thread (core 2); the game thread (0) is never pinned (0.1.20 review). Switch only.
enum class Papel { Juego = 0, Render = 1, Anillo = 2 };
void AfinarHilo(Papel papel);

}  // namespace xmow::perf
