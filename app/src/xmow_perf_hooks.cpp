// XMOW - CPU time the console has no use for: two of Direct3D's waits on the GPU (test 22, rex_perfil.log: the
// rendering thread XThread830F7380 at 90 % of a core, 16 % of it in the spin below and 8 % in the query GetData).
//
//   sub_82B45838  Direct3D's spin callback (a CBlocker-style check; xmow_recomp.20.cpp): 4 x 8 db16cyc delays, then a
//                 look at the GPU's progress (the value device+10896 points at) and a 5 s hang timeout. It answers 1
//                 while still waiting; its callers (BlockOnFence sub_82B3DB18 and the ring-space waits) call it again
//                 and re-check their own condition. On the Xbox 360 the delays let the other hardware thread of the core
//                 run; here they take the core from the GPU ring thread the guest is waiting for. xmow_spin_cede: when
//                 it answers 1, the thread waits up to xmow_spin_cede_us for the ring to publish progress (read
//                 pointer, fence) or yields the core (svcSleepThread(0)) with xmow_spin_cede_us = 0. Same semantics: the
//                 original runs first and its answer is kept; only the time between two polls changes.
//
//   sub_82B3EB08  Direct3D's query GetData (xmow_recomp.54.cpp). For an occlusion query (type 9, query+4) it answers
//                 S_FALSE (r3 = 1, *data = 1) while the query's fence (query+24) is ahead of the GPU or the end
//                 structure still holds D3D's 0xFFFFFEED mark, and otherwise sums over the query's tiles (query+148,
//                 at least one; their structures' addresses from query+28) end.ZPass_A + end.ZPass_B - begin.ZPass_A -
//                 begin.ZPass_B, stores it at *data, sets bit 0x80 of query+20 and answers S_OK (0). With
//                 gow2_oclusion_real = false the ring (gow2_gpu.cpp OcclusionQuery) writes ZPass_A = 1000 into each end
//                 structure and zeroes the begin ones, so the answer is always 1000 per tile; the game just has to
//                 wait for the ring to get there. xmow_consulta_inmediata gives that answer at once, without the wait.
//                 Not touched: queries of other types (8 = event, the frame throttling), a query not issued (flag 0x40
//                 of query+20 clear: the original's D3DERR_INVALIDCALL), a lost device (device+10941 bit 0x2) and real
//                 occlusion (gow2_oclusion_real = true): those go to the recompiled code.
//
// Hooked through the weak alias of each function (REX_HOOK_RAW, as gears2-nx's gow2_perf_hooks.cpp did): no call to
// either is inlined (patch_inline_calls only inlines same-file calls; both are called from other files and through
// the function table), so the generated code does not need to be regenerated. A later tools/codegen.ps1 sees these hooks in
// src/ and keeps their calls as calls.

#include "xmow_pch.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/memory/utils.h>

#if defined(__SWITCH__)
#include <switch.h>
#endif

#include "xmow_perf.h"

REXCVAR_DEFINE_BOOL(xmow_spin_cede, true, "XMOW",
                    "Direct3D's GPU spin (sub_82B45838): while it answers 'still waiting', the thread waits for the "
                    "ring to publish progress (up to xmow_spin_cede_us) instead of spinning on the core");
REXCVAR_DEFINE_INT32(xmow_spin_cede_us, 200, "XMOW",
                     "xmow_spin_cede: longest wait in microseconds for ring progress (the caller polls again "
                     "afterwards); 0 = only yield the core (svcSleepThread(0))");
REXCVAR_DEFINE_BOOL(xmow_consulta_inmediata, true, "XMOW",
                    "With gow2_oclusion_real = false: occlusion query GetData (sub_82B3EB08) answers 'visible' (what "
                    "the ring writes, 1000 samples per tile) at once instead of polling until the ring gets there");
REXCVAR_DECLARE(bool, gow2_oclusion_real);  // src/gow2_gpu.cpp
extern std::atomic<bool> g_gow2_query_waited;  // src/gow2_gpu.cpp (OcclusionPoll)

namespace xmow::perf {
namespace {
std::atomic<uint32_t> g_progreso{0};
std::atomic<uint32_t> g_esperando{0};
std::mutex g_mutex;
std::condition_variable g_cv;
std::atomic<uint64_t> g_esperas{0}, g_esperas_ns{0}, g_consultas{0};

constexpr int32_t kEsperaMaxUs = 5000;
constexpr uint32_t kMuestrasPorTesela = 1000;  // gow2_gpu.cpp kFakeSampleCount
constexpr uint32_t kTeselasMax = 16;           // more is not a real query: the recompiled code decides

void Ceder() {
  const int32_t us = std::min(REXCVAR_GET(xmow_spin_cede_us), kEsperaMaxUs);
  const auto inicio = std::chrono::steady_clock::now();
  if (us <= 0) {
#if defined(__SWITCH__)
    svcSleepThread(0);
#else
    std::this_thread::yield();
#endif
  } else {
    // Either AvisarProgreso sees this thread counted and notifies under the lock, or the predicate sees its increment.
    const uint32_t visto = g_progreso.load(std::memory_order_seq_cst);
    g_esperando.fetch_add(1, std::memory_order_seq_cst);
    {
      std::unique_lock<std::mutex> lock(g_mutex);
      g_cv.wait_for(lock, std::chrono::microseconds(us),
                    [&] { return g_progreso.load(std::memory_order_seq_cst) != visto; });
    }
    g_esperando.fetch_sub(1, std::memory_order_seq_cst);
  }
  g_esperas.fetch_add(1, std::memory_order_relaxed);
  g_esperas_ns.fetch_add(uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                      std::chrono::steady_clock::now() - inicio)
                                      .count()),
                         std::memory_order_relaxed);
}
}  // namespace

void AvisarProgreso() {
  g_progreso.fetch_add(1, std::memory_order_seq_cst);
  if (g_esperando.load(std::memory_order_seq_cst)) {
    {
      std::lock_guard<std::mutex> lock(g_mutex);
    }
    g_cv.notify_all();
  }
}

Contadores LeerContadores() {
  Contadores c;
  c.esperas = g_esperas.exchange(0, std::memory_order_relaxed);
  c.esperas_us = g_esperas_ns.exchange(0, std::memory_order_relaxed) / 1000;
  c.consultas = g_consultas.exchange(0, std::memory_order_relaxed);
  return c;
}

}  // namespace xmow::perf

REX_EXTERN(__imp__sub_82B45838);
REX_HOOK_RAW(sub_82B45838) {
  __imp__sub_82B45838(ctx, base);
  if (ctx.r3.u32 == 1 && REXCVAR_GET(xmow_spin_cede)) xmow::perf::Ceder();
}

REX_EXTERN(__imp__sub_82B3EB08);
REX_HOOK_RAW(sub_82B3EB08) {
  // GetData(query r3, data r4): see the comment at the top for what the original computes.
  if (REXCVAR_GET(xmow_consulta_inmediata) && !REXCVAR_GET(gow2_oclusion_real)) {
    // The codegen's own accessors (xmow_pch.h), so an address in the 0xE0000000 physical range is read as the
    // recompiled code reads it.
    const uint32_t query = ctx.r3.u32, data = ctx.r4.u32;
    if (query && data) {
      const uint8_t flags = REX_LOAD_U8(query + 20);
      const uint32_t device = REX_LOAD_U32(query);
      const uint32_t teselas = std::max<uint32_t>(REX_LOAD_U32(query + 148), 1);  // the loop runs at least once
      if ((flags & 0x40) && REX_LOAD_U32(query + 4) == 9 && device && !(REX_LOAD_U8(device + 10941) & 0x2) &&
          teselas <= xmow::perf::kTeselasMax) {
        REX_STORE_U32(data, teselas * xmow::perf::kMuestrasPorTesela);
        REX_STORE_U8(query + 20, uint8_t(flags | 0x80));  // rlwimi r29,r11(=1),7,0,24: the result is in
        xmow::perf::g_consultas.fetch_add(1, std::memory_order_relaxed);
        ctx.r3.u64 = 0;  // S_OK
        return;
      }
    }
  }
  const uint32_t query = ctx.r3.u32;
  const bool oclusion = query && REX_LOAD_U32(query + 4) == 9;
  __imp__sub_82B3EB08(ctx, base);
  // S_FALSE for an occlusion query with real occlusion: the game is waiting for a result, so the ring (gow2_gpu.cpp
  // OcclusionPoll) may answer the queries ended at least gow2_oclusion_sin_esperar_ms ago (gears2-nx's GetData hook).
  if (oclusion && ctx.r3.u32 == 1 && REXCVAR_GET(gow2_oclusion_real)) {
    g_gow2_query_waited.store(true, std::memory_order_release);
  }
}
