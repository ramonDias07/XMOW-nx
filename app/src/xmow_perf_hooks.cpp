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
//                 With xmow_oclusao_conservadora (0.1.21) the ring writes 1000 or 0 from its history of that box, so
//                 the original runs first and its answer is kept; only its "not yet" becomes the visible answer.
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
#include <cstring>
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
REXCVAR_DEFINE_BOOL(xmow_afinidade_threads, false, "XMOW",
                    "Hot threads pinned to their own core: the UE3 rendering thread on core 1 and the GPU ring thread on "
                    "core 2, both at their own priority (0x3B, the only band Horizon time-slices); the game's main "
                    "thread is left unpinned. Off by default until measured on the console (A/B with rex_perfil). Each "
                    "thread applies it to itself (checked at every call), so turning it off releases them");
REXCVAR_DEFINE_BOOL(xmow_sem_consultas_oclusao, true, "XMOW",
                    "UE3 issues no occlusion queries: while FSceneRenderer::InitViews (sub_82606A08) runs, "
                    "GIgnoreAllOcclusionQueries (0x835E8C08, the TOGGLEOCCLUSION flag) reads 1, so every view gets "
                    "bIgnoreExistingQueries + bDisableQuerySubmissions: primitives and shadows are visible, no query "
                    "box, no GetData. The depth prepass is kept (Render reads the flag again after InitViews, restored "
                    "by then). Off by itself with gow2_oclusion_real or xmow_oclusao_conservadora");
// 0.1.28 (test 34, ANALISE.md 2.1: the render thread waits 10.1 % of its time in 82B3DB18 -> 82B45838). See the
// comment above REX_HOOK_RAW(sub_82B3CFD0) at the end of this file.
REXCVAR_DEFINE_INT32(xmow_d3d_quadros_adiante, 1, "XMOW",
                     "How many frames Direct3D's render thread may run ahead of the GPU ring: 1 = the game's own "
                     "throttle (the first command buffer segment of frame N+1 waits until the ring has finished frame N), "
                     "2 = one frame more (it waits for frame N-1 instead). The fences themselves stay exact; only the "
                     "throttle's depth changes (EXPERIMENTAL: +1 frame of input delay). Live")
    .range(1, 2);
// 0.1.30 (test 35 item 7, render thread CPU): see the comment above REX_HOOK_RAW(sub_82B43C28) at the end of this file.
REXCVAR_DEFINE_BOOL(xmow_d3d_nativo, true, "XMOW",  // 0.1.32: default true (test 37: no fault in ~5 min, -0.7 to -1 ms)
                    "Direct3D's copies of its dirty shader constants (sub_82B43C28) and register blocks (sub_82B43890) "
                    "into its command buffer are done by native code: the same words at the same places (padding, "
                    "headers, data), Direct3D's own code when the segment is full. Less render thread time per draw. "
                    "false = the recompiled code, as in 0.1.29. On by default since 0.1.32. Live");
// 0.1.32 (test 38: with the identity remap the render thread is the limit, 98-100 %): see the comment above
// REX_HOOK_RAW(sub_82608DB0) at the end of this file.
REXCVAR_DEFINE_BOOL(xmow_sem_prepasso_motor, true, "XMOW",
                    "UE3's depth prepass (RenderPrePass, sub_82608DB0) runs without its draw lists (sub_82608FE8): "
                    "the render thread no longer builds the ~800 prepass draws per frame that gow2_sin_prepaso_z and "
                    "gow2_sin_prepaso_enmascarado drop at the ring anyway; every state call of the prepass still runs. "
                    "Only with both of those on, no occlusion queries (xmow_sem_consultas_oclusao on, gow2_oclusion_real "
                    "and xmow_oclusao_conservadora off) and the single pass scene (GUseTilingCode 0); otherwise the "
                    "prepass runs as in 0.1.31. On by default since 0.1.33 (test 39: same image, about +1 fps). Live");
REXCVAR_DECLARE(bool, gow2_oclusion_real);  // src/gow2_gpu.cpp
REXCVAR_DECLARE(bool, xmow_oclusao_conservadora);  // src/gow2_gpu.cpp
REXCVAR_DECLARE(bool, gow2_sin_prepaso_z);            // src/gow2_gpu.cpp (0.1.32: xmow_sem_prepasso_motor)
REXCVAR_DECLARE(bool, gow2_sin_prepaso_enmascarado);  // src/gow2_gpu.cpp (0.1.32: xmow_sem_prepasso_motor)
extern std::atomic<bool> g_gow2_query_waited;  // src/gow2_gpu.cpp (OcclusionPoll)

namespace xmow::perf {
namespace {
std::atomic<uint32_t> g_progreso{0};
std::atomic<uint32_t> g_esperando{0};
std::mutex g_mutex;
std::condition_variable g_cv;
std::atomic<uint64_t> g_esperas{0}, g_esperas_ns{0}, g_consultas{0};
std::atomic<uint64_t> g_consultas_anillo{0}, g_consultas_tarde{0};  // xmow_oclusao_conservadora
std::atomic<uint64_t> g_esperas_total_ns{0};  // like g_esperas_ns, never reset (xmow::perf::EsperaD3dTotalUs)
// 0.1.28 (xmow_d3d_quadros_adiante): the frame throttle's waits (sub_82B3CFD0 with a frame fence pending) and their time.
std::atomic<uint64_t> g_freno{0}, g_freno_ns{0};
// 0.1.30 (xmow_d3d_nativo): see the comment above REX_HOOK_RAW(sub_82B43C28) at the end of this file.
std::atomic<uint64_t> g_nativo_constantes{0}, g_nativo_constantes_regs{0}, g_nativo_bloques{0};
std::atomic<uint64_t> g_nativo_bloques_regs{0}, g_nativo_lentos{0};
// 0.1.33c (review F2): ~750 thousand counts per second from the render thread: a plain load and store (no atomic
// read-modify-write). 0.1.33d (review N-1): a reset by the report between the writer's load and store is undone by
// that store, so the next report shows that whole interval again (one line reads about double). Statistics only:
// nothing decides on these counters.
inline void Sumar1(std::atomic<uint64_t>& a, uint64_t n) {
  a.store(a.load(std::memory_order_relaxed) + n, std::memory_order_relaxed);
}
// 0.1.32 (xmow_sem_prepasso_motor): RenderPrePass calls with the option on, prepass draw lists left out, calls run as
// before because a condition failed, and the conditions that failed at those calls (MotivoPrepasoMotor bits).
std::atomic<uint64_t> g_prepaso_llamadas{0}, g_prepaso_listas_omitidas{0}, g_prepaso_bloqueados{0};
std::atomic<uint32_t> g_prepaso_motivo{0};

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
  const uint64_t ns = uint64_t(
      std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - inicio).count());
  g_esperas.fetch_add(1, std::memory_order_relaxed);
  g_esperas_ns.fetch_add(ns, std::memory_order_relaxed);
  g_esperas_total_ns.fetch_add(ns, std::memory_order_relaxed);
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

uint64_t EsperaD3dTotalUs() { return g_esperas_total_ns.load(std::memory_order_relaxed) / 1000; }

// xmow_afinidade_threads. Horizon: svcSetThreadCoreMask(CUR_THREAD_HANDLE, preferred core, mask) with the mask cut to that
// one core is a real pin (see sdk-g2 threading_switch.cpp RexSwitchPinCurrentThreadToCore: a preferred core alone is
// ignored). The calling thread does it to itself, so no thread registry or handle is needed; each role is told by who
// calls: the game thread's Tick hook (xmow_resolucao_interna.cpp), the GetData hook for occlusion queries (only the UE3
// rendering thread asks for them) and the ring loop (gow2_gpu.cpp). With the cvar turned off the thread gets back the
// mask and priority it had.
void AfinarHilo(Papel papel) {
#if defined(__SWITCH__)
  struct Estado {
    bool aplicado = false;
    bool intentado = false;
    s32 preferido = 0;
    u64 mascara = 0;
    s32 prioridad = 0;
  };
  thread_local Estado e;
  static std::atomic<uint32_t> g_avisado{0};  // one log line per role and direction
  const bool quiere = REXCVAR_GET(xmow_afinidade_threads);
  if (quiere == e.aplicado) return;
  const uint32_t indice = uint32_t(papel);
  const char* nombre = papel == Papel::Juego ? "game thread" : papel == Papel::Render ? "render thread" : "GPU ring thread";
  // 0.1.20 review: the game thread is not pinned (it shares core 0 with the shader compiler otherwise).
  if (quiere && papel == Papel::Juego) return;
  if (quiere) {
    if (!e.intentado) {
      e.intentado = true;
      if (R_FAILED(svcGetThreadCoreMask(&e.preferido, &e.mascara, CUR_THREAD_HANDLE)) ||
          R_FAILED(svcGetThreadPriority(&e.prioridad, CUR_THREAD_HANDLE))) {
        REXLOG_WARN("[xmow affinity] {}: cannot read its core mask or priority, left alone", nombre);
        e.mascara = 0;
      }
    }
    if (!e.mascara) {
      e.aplicado = true;
      return;
    }
    const s32 nucleo = int(papel);  // 0 game, 1 render, 2 ring
    if (!(e.mascara & (u64(1) << nucleo))) {
      if (!(g_avisado.fetch_or(1u << indice) & (1u << indice))) {
        REXLOG_WARN("[xmow affinity] {}: core {} is not in the process mask {:#x}, not pinned", nombre, nucleo, e.mascara);
      }
      e.aplicado = true;  // do not try again every call
      return;
    }
    const Result r1 = svcSetThreadCoreMask(CUR_THREAD_HANDLE, nucleo, u64(1) << nucleo);
    // 0.1.20 review: the priority is left as it is (0x3A would take the render thread out of the time-sliced 0x3B
    // band: a guest spin in it would starve the other 0x3B threads of core 1).
    e.aplicado = true;
    if (!(g_avisado.fetch_or(1u << indice) & (1u << indice))) {
      REXLOG_INFO("[xmow affinity] {} pinned to core {} (rc {:#x}), priority {:#x} kept; process mask {:#x}", nombre,
                  nucleo, uint32_t(r1), e.prioridad, e.mascara);
    }
  } else if (e.mascara) {
    svcSetThreadCoreMask(CUR_THREAD_HANDLE, e.preferido, e.mascara);
    e.aplicado = false;
    REXLOG_INFO("[xmow affinity] {} released (core mask {:#x}, priority {:#x})", nombre, e.mascara, e.prioridad);
  } else {
    e.aplicado = false;
  }
#else
  (void)papel;
#endif
}

Contadores LeerContadores() {
  Contadores c;
  c.esperas = g_esperas.exchange(0, std::memory_order_relaxed);
  c.esperas_us = g_esperas_ns.exchange(0, std::memory_order_relaxed) / 1000;
  c.consultas = g_consultas.exchange(0, std::memory_order_relaxed);
  c.consultas_anillo = g_consultas_anillo.exchange(0, std::memory_order_relaxed);
  c.consultas_tarde = g_consultas_tarde.exchange(0, std::memory_order_relaxed);
  c.freno = g_freno.exchange(0, std::memory_order_relaxed);
  c.freno_us = g_freno_ns.exchange(0, std::memory_order_relaxed) / 1000;
  c.nativo_constantes = g_nativo_constantes.exchange(0, std::memory_order_relaxed);
  c.nativo_constantes_regs = g_nativo_constantes_regs.exchange(0, std::memory_order_relaxed);
  c.nativo_bloques = g_nativo_bloques.exchange(0, std::memory_order_relaxed);
  c.nativo_bloques_regs = g_nativo_bloques_regs.exchange(0, std::memory_order_relaxed);
  c.nativo_lentos = g_nativo_lentos.exchange(0, std::memory_order_relaxed);
  c.prepaso_llamadas = g_prepaso_llamadas.exchange(0, std::memory_order_relaxed);
  c.prepaso_listas_omitidas = g_prepaso_listas_omitidas.exchange(0, std::memory_order_relaxed);
  c.prepaso_bloqueados = g_prepaso_bloqueados.exchange(0, std::memory_order_relaxed);
  c.prepaso_motivo = g_prepaso_motivo.exchange(0, std::memory_order_relaxed);
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
  // Only the UE3 rendering thread asks for occlusion queries: it pins itself here (xmow_afinidade_threads).
  if (ctx.r3.u32 && REX_LOAD_U32(ctx.r3.u32 + 4) == 9) xmow::perf::AfinarHilo(xmow::perf::Papel::Render);
  if (REXCVAR_GET(xmow_consulta_inmediata) && !REXCVAR_GET(gow2_oclusion_real)) {
    // The codegen's own accessors (xmow_pch.h), so an address in the 0xE0000000 physical range is read as the
    // recompiled code reads it.
    const uint32_t query = ctx.r3.u32, data = ctx.r4.u32;
    if (query && data) {
      const uint8_t flags = REX_LOAD_U8(query + 20);
      const uint32_t device = REX_LOAD_U32(query);
      const uint32_t teselas = std::max<uint32_t>(REX_LOAD_U32(query + 148), 1);  // the loop runs at least once
      const bool contestable = (flags & 0x40) && REX_LOAD_U32(query + 4) == 9 && device &&
                               !(REX_LOAD_U8(device + 10941) & 0x2) && teselas <= xmow::perf::kTeselasMax;
      // xmow_oclusao_conservadora: the ring writes each end structure at once with the history's answer (1000 or 0,
      // gow2_gpu.cpp OclusionConservadora), so Direct3D's own GetData reads it when the ring is past the query. Only
      // while it still answers "not yet" (the ring has not got there) is the query answered visible at once, as below:
      // the game never waits either way.
      if (contestable && REXCVAR_GET(xmow_oclusao_conservadora)) {
        __imp__sub_82B3EB08(ctx, base);
        if (ctx.r3.u32 != 1) {
          if (ctx.r3.u32 == 0) xmow::perf::g_consultas_anillo.fetch_add(1, std::memory_order_relaxed);
          return;
        }
        REX_STORE_U32(data, teselas * xmow::perf::kMuestrasPorTesela);
        REX_STORE_U8(query + 20, uint8_t(REX_LOAD_U8(query + 20) | 0x80));
        xmow::perf::g_consultas.fetch_add(1, std::memory_order_relaxed);
        xmow::perf::g_consultas_tarde.fetch_add(1, std::memory_order_relaxed);
        ctx.r3.u64 = 0;  // S_OK
        return;
      }
      if (contestable) {
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

// xmow 0.1.23 (xmow_sem_consultas_oclusao). FSceneRenderer::InitViews (this r3),
// render thread only, called only by FDeferredShadingSceneRenderer::Render (sub_826083C8). It reads
// GIgnoreAllOcclusionQueries (lis r19,-31905 / lwz -29688 = 0x835E8C08, at 82606ABC) to set the views'
// bIgnoreExistingQueries | bDisableQuerySubmissions (+0xE0C |= 0x18000000); Render reads it once more (826085D0) to
// decide the depth prepass and BeginOcclusionTests, after the value is restored here. The GetData hook is no longer
// reached with this on, so the render thread's affinity (xmow_afinidade_threads) is applied here too.
namespace xmow::perf {
constexpr uint32_t kGIgnoreAllOcclusionQueries = 0x835E8C08;
}

void XmowGamePatchesPorVista();  // src/xmow_game_patches.cpp (0.1.28: game patches 2 and 3)
REX_EXTERN(__imp__sub_82606A08);
REX_HOOK_RAW(sub_82606A08) {
  xmow::perf::AfinarHilo(xmow::perf::Papel::Render);
  XmowGamePatchesPorVista();
  if (!REXCVAR_GET(xmow_sem_consultas_oclusao) || REXCVAR_GET(gow2_oclusion_real) ||
      REXCVAR_GET(xmow_oclusao_conservadora)) {
    __imp__sub_82606A08(ctx, base);
    return;
  }
  static std::atomic<bool> g_avisado{false};
  if (!g_avisado.exchange(true, std::memory_order_relaxed)) {
    REXLOG_INFO("[xmow occlusion] xmow_sem_consultas_oclusao: UE3 InitViews runs with GIgnoreAllOcclusionQueries = 1 "
                "(no occlusion queries, every primitive and shadow visible, depth prepass kept)");
  }
  const uint32_t antes = REX_LOAD_U32(xmow::perf::kGIgnoreAllOcclusionQueries);
  REX_STORE_U32(xmow::perf::kGIgnoreAllOcclusionQueries, 1u);
  __imp__sub_82606A08(ctx, base);
  REX_STORE_U32(xmow::perf::kGIgnoreAllOcclusionQueries, antes);
}

// 0.1.28 (xmow_d3d_quadros_adiante). What the render thread's 10 % wait of test 34 is (read from the recompiled code):
//   sub_82B3DB18 BlockOnFence(device r3, fence r4): device+10896 points at the fence value the GPU wrote last and
//               device+10908 is the newest fence issued; it spins in sub_82B45838 while (newest - fence) < (newest -
//               *completed). The ring writes those values when it PARSES the EVENT_WRITE_SHD / MEM_WRITE after each
//               segment (gow2_gpu.cpp PacketType3), i.e. as soon as it has recorded the draws before them: the fence is
//               already published at the earliest point that keeps the guest's resources safe.
//   Swap        sub_82B42AA8 ends with `stw r30,14940(r31)`: device+14940 = the fence of the frame just submitted.
//   sub_82B3CFD0 allocates a new command buffer segment (called by the kick sub_82B3D7C0 when the segment is full, and
//               by Swap); at its end, when device+14940 != 0 (and device+23980 bit 0 is clear) it calls
//               BlockOnFence(device+14940) and clears it. Its own segment memory is guarded apart (sub_82B3CC00 checks
//               the ring's consumed position, device+13232/13236), not by this fence.
//   So it is Direct3D's frame throttle: the second segment of frame N+1 waits until the ring has finished ALL of frame
//   N, the frame's tail included (resolves, post, Present). With 2 the hook hands Direct3D the fence of the frame
//   before (N-1) and keeps N for the next frame: the render thread may record a whole frame while the ring finishes the
//   previous one. Fence values are never faked: any other wait (a resource lock, a query) still waits for the real
//   one. The risk is in what the game assumes about a frame being done (buffers reused by frame count): an A/B test.
// Hooked through the weak alias (its callers sub_82B3D7C0, sub_82B42AA8 and one in xmow_recomp.235.cpp are in other files).
namespace xmow::perf {
namespace {
std::atomic<uint32_t> g_fence_retenida{0};  // the frame fence handed to Direct3D at the next frame (2 only)
}  // namespace
}  // namespace xmow::perf

REX_EXTERN(__imp__sub_82B3CFD0);
REX_HOOK_RAW(sub_82B3CFD0) {
  const uint32_t device = ctx.r3.u32;
  uint32_t pendiente = device ? REX_LOAD_U32(device + 14940) : 0;
  if (pendiente) {
    if (REXCVAR_GET(xmow_d3d_quadros_adiante) >= 2) {
      // Fences only grow: an older one is always a valid (earlier) wait. 0 = nothing older yet (the first frame).
      const uint32_t antes = xmow::perf::g_fence_retenida.exchange(pendiente, std::memory_order_relaxed);
      REX_STORE_U32(device + 14940, antes);
      pendiente = antes;
    } else {
      xmow::perf::g_fence_retenida.store(0, std::memory_order_relaxed);
    }
  }
  if (!pendiente) {
    __imp__sub_82B3CFD0(ctx, base);
    return;
  }
  const auto inicio = std::chrono::steady_clock::now();
  __imp__sub_82B3CFD0(ctx, base);
  xmow::perf::g_freno.fetch_add(1, std::memory_order_relaxed);
  xmow::perf::g_freno_ns.fetch_add(uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                                std::chrono::steady_clock::now() - inicio).count()),
                                   std::memory_order_relaxed);
}

// 0.1.30 (xmow_d3d_nativo; test 34/35 profile: the guest D3D draw sub_82B419E0 20.5 % of the render thread, of it 6.7 %
// in sub_82B43C28 and 1.8 % in sub_82B43890, both called by it for every draw with dirty state). Direct3D keeps its
// state in the device (r3) and a 64-bit dirty mask per group; at the draw it copies every dirty run into its command
// buffer (cursor at device+48: the address of the last word written; end at device+52) as type-0 PM4 packets: a header
// ((count - 1) << 16 | first register) and the registers. Both functions are leaves except their slow path:
//   sub_82B43C28(device r3, mask r4, first register r5, source r6): shader constants (0x4000 vertex from device+1920,
//     0x4400 pixel from device+6016). Bit 63 - n = 4 float4 constants (16 registers, 64 bytes) from register r5 +
//     16 n. A run of k bits: if cursor + ((16 k + 3) << 2) is below the end, type-2 NOPs (0x80000000) until the header
//     lands at 12 mod 16, the header, then 64 k bytes at the next 16-byte boundary (lvx128/stvx128 of the source
//     rounded down to 16: a byte-exact copy); otherwise sub_82B43248(device, cursor, first register, source, 16 k, 16)
//     writes the run and returns the new cursor.
//   sub_82B43890(device r3, mask r4, first register r5, source r6): register blocks (render state 0x2000-0x2380 and
//     others). Bit 63 - n = register r5 + n (one word). A run of k: if cursor + 4 k is below the end, the header and the
//     k words; otherwise sub_82B43248(..., k, 1).
// Native: the same arithmetic (cntlzd, shifts, the running first register, the 32-bit compare against the end, read
// again for every run), the same stores (memcpy of guest bytes: the command buffer and the device are plain guest
// memory), the cursor stored at the end, never before the slow path (which reads it from r4); the slow path is
// Direct3D's own function with the same registers and return address. A zero mask (never passed: the draw tests the
// mask first) goes to the recompiled code. Hooked through the weak alias: their callers that matter (sub_82B419E0,
// xmow_recomp.205.cpp) are in other files, so those calls are not inlined.
namespace xmow::perf {
namespace {
constexpr uint32_t kNopTipo2 = 0x80000000u;
inline uint64_t Desplazar(uint64_t v, uint64_t n) { return (n & 0x40) ? 0 : (v << (n & 0x3F)); }  // sld
inline uint64_t CerosDelante(uint64_t v) { return v == 0 ? 64 : uint64_t(__builtin_clzll(v)); }   // cntlzd
}  // namespace
}  // namespace xmow::perf

REX_EXTERN(sub_82B43248);
REX_EXTERN(__imp__sub_82B43C28);
REX_HOOK_RAW(sub_82B43C28) {
  using namespace xmow::perf;
  if (!REXCVAR_GET(xmow_d3d_nativo) || ctx.r4.u64 == 0) {
    __imp__sub_82B43C28(ctx, base);
    return;
  }
  const uint64_t lr = ctx.lr;
  const uint64_t device = ctx.r3.u64;  // r26
  const uint32_t dev = ctx.r3.u32;
  uint64_t mascara = ctx.r4.u64;       // r30
  uint64_t registro = ctx.r5.u64;      // r5: the first register of what is left
  uint64_t fuente = ctx.r6.u64 - 4;    // r31 = r6 - 4
  uint64_t cursor = REX_LOAD_U32(dev + 48);  // r4 (lwz: zero-extended)
  uint64_t registros = 0, lentos = 0;
  do {
    const uint64_t delante = CerosDelante(mascara);                                   // r10
    const uint32_t fin = REX_LOAD_U32(dev + 52);                                      // r9
    fuente += uint32_t(uint32_t(delante) << 6);                                       // r31 += r10 * 64
    mascara = Desplazar(mascara, delante);
    const uint64_t corrida = CerosDelante(~mascara);                                  // r28: bits in this run (1..64)
    const uint64_t palabras = uint32_t(uint32_t(corrida) << 4);                       // r29: 16 registers per bit
    const uint64_t primero = uint64_t(uint32_t(uint32_t(delante) << 4)) + registro;  // r27
    const uint32_t hasta = uint32_t(uint32_t(palabras + 3) << 2) + uint32_t(cursor);
    if (hasta < fin) {
      uint32_t c = uint32_t(cursor);
      switch (c & 0xF) {  // NOPs until the header lands at 12 mod 16 (the cursor is the last word written)
        case 0:
          REX_STORE_U32(c + 4, kNopTipo2);
          REX_STORE_U32(c + 8, kNopTipo2);
          c += 8;
          break;
        case 4:
          REX_STORE_U32(c + 4, kNopTipo2);
          c += 4;
          break;
        case 12:
          REX_STORE_U32(c + 4, kNopTipo2);
          REX_STORE_U32(c + 8, kNopTipo2);
          REX_STORE_U32(c + 12, kNopTipo2);
          c += 12;
          break;
        default:
          break;
      }
      REX_STORE_U32(c + 4, uint32_t(uint32_t(palabras - 1) << 16) | uint32_t(primero));
      c += 4;
      registro = palabras + primero;                              // r5 = r29 + r27
      const uint32_t desde = (uint32_t(fuente) + 4) & ~0xFu;      // lvx128 rounds down; 64 contiguous bytes per bit
      const uint32_t hacia = (c + 4) & ~0xFu;                     // stvx128: = c + 4 after the NOPs
      const uint32_t bytes = uint32_t(corrida) * 64;
      std::memcpy(base + hacia, base + desde, bytes);
      c += bytes;
      fuente += bytes;
      mascara = Desplazar(mascara, corrida);                      // rldicr r30,r30,1,62 once per bit
      cursor = (cursor & ~uint64_t(0xFFFFFFFFu)) | c;             // stwu / addi only change the low word here
      registros += palabras;
    } else {
      ctx.r8.s64 = 16;
      ctx.r7.u64 = palabras;
      ctx.r6.u64 = fuente;
      ctx.r5.u64 = primero;
      ctx.r4.u64 = cursor;
      ctx.r3.u64 = device;
      ctx.lr = 0x82B43CAC;
      sub_82B43248(ctx, base);
      cursor = ctx.r3.u64;                                        // mr r4,r3
      fuente += uint32_t(uint32_t(palabras) << 2);                // r31 += r29 * 4
      registro = palabras + primero;                              // r5 = r29 + r27
      mascara = Desplazar(mascara, corrida & 0xFFFFFFFFu);        // sld r30,r30,r28
      ++lentos;
    }
  } while (mascara != 0);
  REX_STORE_U32(dev + 48, uint32_t(cursor));
  ctx.r4.u64 = cursor;
  ctx.r5.u64 = registro;
  ctx.lr = lr;
  Sumar1(g_nativo_constantes, 1);
  Sumar1(g_nativo_constantes_regs, registros);
  if (lentos) Sumar1(g_nativo_lentos, lentos);
}

REX_EXTERN(__imp__sub_82B43890);
REX_HOOK_RAW(sub_82B43890) {
  using namespace xmow::perf;
  if (!REXCVAR_GET(xmow_d3d_nativo) || ctx.r4.u64 == 0) {
    __imp__sub_82B43890(ctx, base);
    return;
  }
  const uint64_t lr = ctx.lr;
  const uint64_t device = ctx.r3.u64;  // r26
  const uint32_t dev = ctx.r3.u32;
  uint64_t mascara = ctx.r4.u64;       // r31
  uint64_t registro = ctx.r5.u64;      // r5
  uint64_t fuente = ctx.r6.u64 - 4;    // r29 = r6 - 4
  uint64_t cursor = REX_LOAD_U32(dev + 48);  // r4
  uint64_t registros = 0, lentos = 0;
  do {
    const uint64_t delante = CerosDelante(mascara);                 // r11
    const uint32_t fin = REX_LOAD_U32(dev + 52);                    // r9
    mascara = Desplazar(mascara, delante);
    const uint64_t corrida = CerosDelante(~mascara);                // r30: registers in this run (1..64)
    fuente += uint32_t(uint32_t(delante) << 2);                     // r29 += r11 * 4
    const uint64_t primero = delante + registro;                    // r28 = r11 + r5
    const uint64_t bytes = uint32_t(uint32_t(corrida) << 2);        // r27
    if (uint32_t(bytes + cursor) < fin) {
      uint32_t c = uint32_t(cursor);
      REX_STORE_U32(c + 4, uint32_t(uint32_t(corrida - 1) << 16) | uint32_t(primero));
      c += 4;
      registro = corrida + primero;                                 // r5 = r30 + r28
      std::memcpy(base + uint32_t(c + 4), base + uint32_t(uint32_t(fuente) + 4), size_t(bytes));  // lwzu / stwu
      c += uint32_t(bytes);
      fuente += bytes;
      mascara = Desplazar(mascara, corrida);
      cursor = (cursor & ~uint64_t(0xFFFFFFFFu)) | c;
      registros += corrida;
    } else {
      ctx.r8.s64 = 1;
      ctx.r7.u64 = corrida;
      ctx.r6.u64 = fuente;
      ctx.r5.u64 = primero;
      ctx.r4.u64 = cursor;
      ctx.r3.u64 = device;
      ctx.lr = 0x82B438FC;
      sub_82B43248(ctx, base);
      cursor = ctx.r3.u64;                                          // mr r4,r3
      fuente += bytes;                                              // r29 += r27
      registro = corrida + primero;                                 // r5 = r30 + r28
      mascara = Desplazar(mascara, corrida & 0xFFFFFFFFu);          // sld r31,r31,r30
      ++lentos;
    }
  } while (mascara != 0);
  REX_STORE_U32(dev + 48, uint32_t(cursor));
  ctx.r4.u64 = cursor;
  ctx.r5.u64 = registro;
  ctx.lr = lr;
  Sumar1(g_nativo_bloques, 1);
  Sumar1(g_nativo_bloques_regs, registros);
  if (lentos) Sumar1(g_nativo_lentos, lentos);
}

// 0.1.32 (xmow_sem_prepasso_motor). UE3's FDeferredShadingSceneRenderer::RenderPrePass (sub_82608DB0,
// xmow_recomp.217.cpp; this r3, DPG r4, r5 passed on, view r6 or -1 for all): unless GUseTilingCode (0x835E88E8) is set
// with DPG 1 (world), it sets the prepass depth state (sub_82372010), clears the 4 color write bits at device+10460 with
// dirty bit 37 (RHISetColorWriteEnable(FALSE) inlined) and stores 0 at device+11868; then RenderPrePassView (sub_82608EC0)
// for each view (this+76 views) or the one asked, ORing their bDirty; then, with the same condition, sub_82372540(1)
// (color writes on), and returns bDirty. Its two callers drop that value: Render (sub_826083C8, xmow_recomp.194.cpp)
// overwrites r3 before any read and sets the base pass depth state itself right after (sub_82373550 or sub_82372010);
// the tiled path (sub_826091A8, xmow_recomp.237.cpp) only calls it with 0x835E88EC set, which needs GUseTilingCode.
// RenderPrePassView (xmow_recomp.78.cpp): view parameters (sub_82371A80 with View+384 / +720), depth bias
// (sub_82373048, with DPG 1 or 3 and not 2), rasterizer and blend states (sub_82371EF8 / sub_82371BE8); then, only when
// its r5 is set (Render: DPG world with GIgnoreAllOcclusionQueries 0) and this+172: the viewport (sub_82606920) and the
// draw lists (sub_82608FE8: the position-only and depth draw lists and the dynamic primitives, the prepass draws; its
// bDirty is what the view returns). With the option on, only sub_82608FE8 is left out while RenderPrePass runs: every
// state call above runs as before and in the same order, so the base pass starts from the same device state except what
// the prepass draws set for themselves (each base pass draw sets its own shaders, streams and per-mesh constants). It
// answers 0 (nothing drawn; unused by both callers). Applied only where the ring drops those draws anyway: depth only
// prepass (gow2_sin_prepaso_z) and masked prepass (gow2_sin_prepaso_enmascarado) both left out, no occlusion queries
// (the InitViews condition above: their boxes would test against that depth) and GUseTilingCode 0 (the single pass
// scene patch). Check on the console: [xmow census] "depth prepass draws not sent" drops to ~0 while the draws sent to
// the renderer stay the same at a paused point. sub_82608FE8 is also called by sub_826091A8 outside RenderPrePass: that
// call is never touched (the flag is only set inside RenderPrePass, on the render thread). Hooked through the weak
// alias: the callers are in other files (sub_82608DB0 from xmow_recomp.194/.237, sub_82608FE8 from .78/.237; it is in
// .41).
namespace xmow::perf {
namespace {
constexpr uint32_t kGUseTilingCode = 0x835E88E8;
std::atomic<bool> g_en_prepaso{false};  // inside RenderPrePass with the option applied (render thread only)
// Bits: 1 gow2_sin_prepaso_z off, 2 gow2_sin_prepaso_enmascarado off, 4 occlusion queries issued, 8 GUseTilingCode set.
uint32_t MotivoPrepasoMotor(uint8_t* base) {
  uint32_t m = 0;
  if (!REXCVAR_GET(gow2_sin_prepaso_z)) m |= 1;
  if (!REXCVAR_GET(gow2_sin_prepaso_enmascarado)) m |= 2;
  if (!REXCVAR_GET(xmow_sem_consultas_oclusao) || REXCVAR_GET(gow2_oclusion_real) ||
      REXCVAR_GET(xmow_oclusao_conservadora)) {
    m |= 4;
  }
  if (REX_LOAD_U32(kGUseTilingCode) != 0) m |= 8;
  return m;
}
}  // namespace
}  // namespace xmow::perf

REX_EXTERN(__imp__sub_82608DB0);
REX_HOOK_RAW(sub_82608DB0) {
  using namespace xmow::perf;
  if (!REXCVAR_GET(xmow_sem_prepasso_motor)) {
    __imp__sub_82608DB0(ctx, base);
    return;
  }
  g_prepaso_llamadas.fetch_add(1, std::memory_order_relaxed);
  const uint32_t motivo = MotivoPrepasoMotor(base);
  if (motivo) {
    g_prepaso_bloqueados.fetch_add(1, std::memory_order_relaxed);
    g_prepaso_motivo.fetch_or(motivo, std::memory_order_relaxed);
    __imp__sub_82608DB0(ctx, base);
    return;
  }
  static std::atomic<bool> g_avisado{false};
  if (!g_avisado.exchange(true, std::memory_order_relaxed)) {
    REXLOG_INFO("[xmow prepass] xmow_sem_prepasso_motor: UE3's RenderPrePass runs its state calls without its draw "
                "lists (GUseTilingCode 0, gow2_sin_prepaso_z and gow2_sin_prepaso_enmascarado on, no occlusion "
                "queries)");
  }
  g_en_prepaso.store(true, std::memory_order_relaxed);
  // 0.1.33c (review N4): cleared even if the guest call leaves by an exception.
  struct FinDelPrepaso {
    ~FinDelPrepaso() { g_en_prepaso.store(false, std::memory_order_relaxed); }
  } fin;
  __imp__sub_82608DB0(ctx, base);
}

REX_EXTERN(__imp__sub_82608FE8);
REX_HOOK_RAW(sub_82608FE8) {
  using namespace xmow::perf;
  if (g_en_prepaso.load(std::memory_order_relaxed)) {
    g_prepaso_listas_omitidas.fetch_add(1, std::memory_order_relaxed);
    ctx.r3.u64 = 0;  // bDirty: nothing drawn
    return;
  }
  __imp__sub_82608FE8(ctx, base);
}
