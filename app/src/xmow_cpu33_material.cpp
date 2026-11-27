// XMOW 0.1.33 - material shader parameters set once per frame and replayed (xmow_material_replay).
//
// Test 34 stack profile (render thread): sub_828C3270 is 14.7 % inclusive (13.7 % outside the depth prepass): 4.0 %
// self, 6.4 % in the virtual calls that evaluate the material's uniform expressions (sub_828C6B10 2.6 %, sub_828C6CD0
// 1.6 %, sub_828C6F48 1.5 %, sub_828C7438, sub_828C7AB8), the rest in the RHI setters. It is UE3's
// FMaterialShaderParameters::Set (this = r3, the shader = r4, FMaterialRenderContext* = r5 {MaterialRenderProxy +0,
// CurrentTime +4, CurrentRealTime +8, View +12}); xmow_recomp.230.cpp. This UE3 has no uniform expression cache: every
// DrawShared of every drawing policy (one per static draw list link, sub_8256BD00 / sub_8256D0D0 / sub_8256CD10 /
// sub_8256D320, and one per dynamic mesh, sub_826264F0) evaluates every expression of the material again, although
// the result only depends on the material proxy, the times and the view. Many links share one material (a link is one
// vertex factory + light map + material), so most calls of a frame repeat an earlier one.
//
// What the function does outside its own frame, all of it read from the recompiled code:
//   - RHI constant setters sub_82371840 (vertex) / sub_82371960 (pixel): (r3 offset, r4 bytes, r5 source). The source is
//     either in the function's frame (an expression's value, a matrix it computed) or a persistent object (the view:
//     View+80 / +384 / +656 / +720; the proxy's data returned by proxy->vtbl[20]: +0 and +2048).
//   - RHI texture setters sub_82371700 (vertex, return address 0x828C37A0) / sub_82371590 (pixel, 0x828C3D14):
//     (r3 sampler, r4 = [tex+24], r5 = [tex+20], f1 = a mip bias from the floats at tex+40..+55 and the global floats
//     0x835EAA20 and 0x8352D89C). Just before each, the 8 bytes of the global double 0x835E0018 are stored at tex+28
//     (the texture's last render time). tex is the word at r1+96 of the function's frame at the call.
//   - sub_8260B560(this+60, View, 0) at the end of the pixel path (scene texture parameters; return address
//     0x828C3EEC).
//   - the path for a proxy without a material (GetMaterial = 0; return addresses 0x828C3388, 0x828C3400, 0x828C3488,
//     0x828C352C) is never replayed.
//   Everything else is its own stack frame, virtual calls that compute values (the expressions, proxy->vtbl[0] /
//   vtbl[20], material->vtbl[120]) and sub_822F32F0 (a matrix into the frame).
//
// xmow_material_replay 2: inside FSceneRenderer::Render (sub_826083C8), the first call for a key (this, shader, proxy,
// both times, view) runs the recompiled function while the hooks of the five functions above record the calls it makes
// directly (told by the return address; a call made from anywhere else while recording, except under sub_8260B560,
// marks the key as never replayed). Every later call with the same key in the same Render replays the record: the same
// calls, in the same order, with the same arguments; frame sources are replayed from the recorded bytes (copied into
// the same guest frame the function would use), persistent sources are passed as the same pointers (so the setter
// reads their current bytes, as the function would), each texture's last render time is stored with the current global
// value, r4 / r5 of the texture calls are read again from the texture, and sub_8260B560 runs live. A replay is refused
// (and the function runs and records again) unless all of these still hold: the first 1088 bytes of the view are what
// they were (every view field the function reads: +80..+143, +384..+447, +448..+511, +656..+735, +1056), the first 32
// bytes of the proxy are what they were (a stack or frame-memory proxy reused at the same address), the proxy is not in
// the thread's stack, the two default textures (0x83602500 / 0x83602504) are the same, and for each texture the 16
// bytes at tex+40 and the two global floats are the same. The record lives for one Render call only.
// What this rests on (not provable from this function alone): a uniform expression gives the same value for the same
// proxy, times and view during one Render call, i.e. nothing changes a material instance's parameters while the scene
// is being drawn (in UE3 they change through render commands, which run between Render calls) and the expression
// classes are pure. xmow_material_replay 1 measures exactly that: it never replays; at every call that would have been
// a replay it runs the function recording again and compares the two records (calls, arguments, bytes), counting and
// logging the differences. Run 1 first: "0 differences" over a play session is the evidence for 2.
// Registers on return: r1, lr and the non-volatile registers as at entry; the volatile ones differ from the original's
// leftovers (callers are compiled C++, standard ABI). The dead stack below the caller's frame holds different garbage.
//
// 0.1.33d (reviews: Opus S-A / S-C / N-2..N-4, Fable 2.2-2.3):
//   - persistent sources are compared by CONTENT, not only by pointer: each record keeps a hash of the bytes the setter
//     read at the call (proxy->vtbl[20] might fill a shared buffer and return the same pointer). Mode 2 refuses a
//     replay whose persistent sources do not hash as recorded (counted with the guards).
//   - VERIFY exercises the replay itself: at every would-be replay it hashes what a replay would pass from the
//     persistent sources, saves Direct3D's constant shadow and dirty masks (device+0..15, device+1920..10111: both
//     shader types, xmow_cpu33_params.cpp), REPLAYS, keeps the shadow the replay left, puts the saved one back, runs the
//     game's function recording, and compares the calls, the bytes, the persistent contents and the shadow. The game's
//     run is what stays: VERIFY still changes nothing.
//   - mode 2 audits 1 replay in 64 the same way; any difference logs it, sets xmow_material_replay back to 0 and keeps
//     mode 2 off for the rest of the boot (0.1.33e: and for the rest of that Render, review M-2).
//   - 0.1.33e (review M-1): an audit's replay runs only the constant setter calls before the scene call, with the
//     shadow, masks and the setters' scratch array saved first and put back after, whatever the result: nothing of it
//     stays. Textures and the scene call are compared through the records; the game's shadow is taken just before its
//     own scene call.
//   - the records (about 3.2 MB) are allocated only with at least 64 MB of heap headroom (newlib: the unclaimed top of
//     the heap plus malloc's free blocks; svcGetInfo counts the whole heap as used from the start), and freed when the
//     mode goes back to 0.

#include "xmow_pch.h"

#include <cstdint>
#include <cstring>
#include <new>
#include <vector>

#if defined(__SWITCH__)
#include <malloc.h>
#include <reent.h>
extern "C" char* fake_heap_end;  // libnx / libsysbase: the end of the heap sbrk hands out
#endif

#include <rex/cvar.h>
#include <rex/logging.h>

#include "xmow_cpu33.h"

REXCVAR_DEFINE_INT32(xmow_material_replay, 0, "XMOW",
                     "0.1.33: material shader parameters (UE3 FMaterialShaderParameters::Set, sub_828C3270) are "
                     "evaluated once per material, shader and view in each scene render, and the later draws that use "
                     "the same material replay the same RHI calls instead of evaluating every expression again. 0 = "
                     "off (the game's code, as 0.1.32), 1 = VERIFY (no replay: every would-be replay runs the game's "
                     "code and is compared, differences are counted in the log), 2 = ON. Live (applies at the next "
                     "frame)")
    .range(0, 2);

REX_EXTERN(__imp__sub_828C3270);
REX_EXTERN(__imp__sub_826083C8);
REX_EXTERN(__imp__sub_82371590);
REX_EXTERN(__imp__sub_82371700);
REX_EXTERN(__imp__sub_8260B560);
REX_EXTERN(sub_82371840);  // the hooks of xmow_cpu33_params.cpp (native, or the recompiled code)
REX_EXTERN(sub_82371960);

namespace xmow::cpu33 {

std::atomic<PPCContext*> g_cap_ctx{nullptr};  // xmow_cpu33.h: the thread (its context) whose sub_828C3270 is being recorded

namespace {

constexpr uint32_t kFuncLo = 0x828C3270, kFuncHi = 0x828C3F00;  // sub_828C3270's code
constexpr uint32_t kFrame = 384;                                // stwu r1,-384(r1)
constexpr uint32_t kScratchAt = 96;                             // replayed frame sources go to r1+96 (as its own temporaries)
constexpr uint32_t kMaxInline = 128;
constexpr uint32_t kLrTexVs = 0x828C37A0, kLrTexPs = 0x828C3D14, kLrScene = 0x828C3EEC;
constexpr uint32_t kLrNull[] = {0x828C3388, 0x828C3400, 0x828C3488, 0x828C352C};
constexpr uint32_t kLastRenderTime = 0x835E0018;  // lis -31906 / lfd 24: the global double stored at tex+28
constexpr uint32_t kGlobalF1 = 0x835EAA20;        // lis -31905 / addi -21988 / lfs 4
constexpr uint32_t kGlobalF2 = 0x8352D89C;        // lis -31917 / lfs -10084
constexpr uint32_t kDefaultTex = 0x83602500;      // lis -31904 / lwz 9472 and 9476
constexpr uint32_t kViewBytes = 1088;
// 0.1.33d: Direct3D's constant shadow (xmow_cpu33_params.cpp): the dirty mask words at device+0 / +8, the vertex shader
// constants at device+1920 (register 120) and the pixel shader ones up to device+10112 (register 632).
constexpr uint32_t kDevicePtr = 0x8357F01C;
constexpr uint32_t kSombraLo = 1920, kSombraHi = 10112, kSombraBytes = 16 + (kSombraHi - kSombraLo);
constexpr uint32_t kAuditarCada = 64;              // mode 2: one replay in 64 is audited
// 0.1.33e (review M-1): the setters' scratch TArray<BYTE> (xmow_cpu33_params.cpp: Data +0, ArrayNum +4) and its
// "constructed" flag, saved and put back around an audit's replay with the shadow.
constexpr uint32_t kScratchArr = 0x8360C000, kScratchFlag = 0x8360BFFC, kScratchMax = 4096;
constexpr size_t kHolguraMinima = size_t(64) << 20;  // heap headroom needed to allocate the records
constexpr uint32_t kStackWindow = 0x100000;       // a proxy this close to the stack pointer is taken as a stack object

enum Tipo : uint8_t { kConstVs = 0, kConstPs = 1, kTexPs = 2, kTexVs = 3, kScene = 4 };
// Why a key is not replayed (bits, also the log's "not replayable" reasons).
enum Motivo : uint32_t {
  kNullMaterial = 1,   // the proxy has no material: the function's default path
  kForeign = 2,        // an RHI call from somewhere else while recording
  kBigInline = 4,      // a frame source larger than kMaxInline or leaving the frame
  kTexOdd = 8,         // a texture call that is not one of the two known sites, or the texture is not at r1+96
  kSceneOdd = 16,      // sub_8260B560 with other arguments than (this+60, View, 0)
  kFull = 32,          // no room left in this frame's record
};

struct Registro {
  uint8_t tipo;
  uint8_t en_linea;  // constants: 1 = the source was in the function's frame (bytes in datos_ at a)
  uint16_t bytes;    // constants: r4
  uint32_t r3;
  uint32_t a;        // constants: the source pointer, or the offset in datos_; textures: the texture; scene: r4
  uint32_t lr;       // the return address of the call
  uint64_t f1;       // textures: f1 as bits
  uint8_t guarda[16];  // textures: the bytes at tex+40
  uint32_t g1, g2;     // textures: the two global floats (bits)
  uint64_t huella;     // 0.1.33d: persistent constants: hash of the bytes at a when the setter was called
};

struct Entrada {
  uint32_t epoca = 0;  // the slot is in use when this is the current Render's number
  uint32_t params, shader, proxy, tiempo, tiempo_real, vista, vista_gen;
  uint32_t tex_def[2];
  uint8_t proxy_huella[32];
  uint32_t primero, cuantos;
  uint32_t motivo;  // 0 = replayable
};

struct Vista {
  uint32_t dir = 0, gen = 0;
  uint8_t copia[kViewBytes];
};

constexpr uint32_t kRanuras = 4096, kSondas = 8, kVistas = 8;
constexpr size_t kMaxRegistros = 1u << 15, kMaxDatos = 1u << 20;

struct Estado {
  // The thread inside sub_826083C8 that owns the record (0.1.33d, review N-4: read by every thread entering the hooks).
  std::atomic<PPCContext*> render_ctx{nullptr};
  int modo = 0;
  uint32_t epoca = 1;
  std::vector<Entrada> tabla;
  std::vector<Registro> registros;
  std::vector<uint8_t> datos;
  Vista vistas[kVistas];
  uint32_t n_vistas = 0;
  // The recording in progress.
  uint32_t cap_sp = 0, cap_params = 0, cap_vista = 0, cap_motivo = 0;
  bool cap_en_escena = false;
  // 0.1.33d: audits (VERIFY, and 1 in kAuditarCada replays of mode 2).
  std::vector<uint64_t> pre;      // per record of the kept entry: what a replay would pass from a persistent source
  uint8_t s0[kSombraBytes];       // the shadow before the audit's replay
  uint8_t sa[kSombraBytes];       // the shadow the replay left
  uint8_t scratch0[16 + kScratchMax];  // 0.1.33e: the scratch array (flag, header, data) before the audit's replay
  uint8_t sb[kSombraBytes];            // 0.1.33e: the game's shadow just before its scene call (or at its end)
  uint32_t audit_device = 0;           // 0.1.33e: != 0 while an audit's game run is recording
  bool sb_tomada = false;
  uint32_t cuenta_auditoria = 0;
} g;

Contador g_llamadas, g_fuera, g_repetidas, g_grabadas, g_no_repetibles, g_guardas, g_registros_rep, g_sin_sitio;
Contador g_verificadas, g_diferencias, g_renders, g_vistas_cambiadas, g_pila;
Contador g_auditadas, g_auditoria_difs;  // 0.1.33d: mode 2 audits
std::atomic<bool> g_apagado{false};      // 0.1.33d: an audit found a difference: mode 2 off for this boot
std::atomic<uint64_t> g_auditadas_total{0}, g_difs_total{0};  // 0.1.34b: since start, for the bench
volatile uint32_t g_motivos = 0;
volatile uint32_t g_avisos = 0;  // difference lines logged (the first 12 only)
volatile uint32_t g_cambio_lo = 0xFFFFFFFFu, g_cambio_hi = 0;  // the view bytes seen changing inside a Render

inline uint32_t L32(uint8_t* base, uint32_t a) { return REX_LOAD_U32(a); }

// 0.1.33d: content hash of a persistent source (not cryptographic; a collision would have to hide a real difference).
inline uint64_t Huella(const uint8_t* p, uint32_t n) {
  uint64_t h = 0x9E3779B97F4A7C15ull ^ n;
  uint32_t i = 0;
  for (; i + 8 <= n; i += 8) {
    uint64_t w;
    std::memcpy(&w, p + i, 8);
    h = (h ^ w) * 0xBF58476D1CE4E5B9ull;
    h ^= h >> 31;
  }
  for (; i < n; ++i) h = (h ^ p[i]) * 0x100000001B3ull;
  return h;
}

// 0.1.33d (review N-2): the Render number never wraps onto old slots.
inline void AvanzarEpoca() {
  if (++g.epoca == 0) {
    for (Entrada& e : g.tabla) e.epoca = 0;
    g.epoca = 1;
  }
}

// 0.1.33d (review S-C): heap headroom (see the header).
size_t HolguraHeap() {
#if defined(__SWITCH__)
  const struct mallinfo mi = mallinfo();
  char* const tope = static_cast<char*>(_sbrk_r(_REENT, 0));
  const size_t sin_pedir =
      (tope != reinterpret_cast<char*>(-1) && fake_heap_end > tope) ? size_t(fake_heap_end - tope) : 0;
  return sin_pedir + size_t(mi.fordblks);
#else
  return SIZE_MAX;
#endif
}

void Liberar() {
  std::vector<Entrada>().swap(g.tabla);
  std::vector<Registro>().swap(g.registros);
  std::vector<uint8_t>().swap(g.datos);
  std::vector<uint64_t>().swap(g.pre);
}

inline bool EnNulo(uint32_t lr) {
  for (uint32_t v : kLrNull) {
    if (lr == v) return true;
  }
  return false;
}

inline bool HayRoom(size_t datos_extra) {
  return g.registros.size() < kMaxRegistros && g.datos.size() + datos_extra <= kMaxDatos;
}

// The record of one sub_828C3270 call, appended at the end of the arenas while g_cap_ctx is set.
void GrabarConstante(PPCContext& ctx, uint8_t* base, uint8_t tipo) {
  const uint32_t lr = uint32_t(ctx.lr);
  if (lr < kFuncLo || lr >= kFuncHi) {
    if (!g.cap_en_escena) g.cap_motivo |= kForeign;
    return;
  }
  if (EnNulo(lr)) {
    g.cap_motivo |= kNullMaterial;
    return;
  }
  const uint32_t bytes = ctx.r4.u32, src = ctx.r5.u32;
  Registro r{};
  r.tipo = tipo;
  r.r3 = ctx.r3.u32;
  r.lr = lr;
  if (bytes == 0 || bytes > 0xFFFF) {
    g.cap_motivo |= kBigInline;
    return;
  }
  r.bytes = uint16_t(bytes);
  const uint32_t lo = g.cap_sp - kFrame, hi = g.cap_sp;
  if (src >= lo && src < hi) {
    if (bytes > kMaxInline || src + bytes > hi) {
      g.cap_motivo |= kBigInline;
      return;
    }
    if (!HayRoom(bytes)) {
      g.cap_motivo |= kFull;
      return;
    }
    r.en_linea = 1;
    r.a = uint32_t(g.datos.size());
    g.datos.insert(g.datos.end(), base + src, base + src + bytes);
  } else {
    if (!HayRoom(0)) {
      g.cap_motivo |= kFull;
      return;
    }
    r.a = src;
    r.huella = Huella(base + src, bytes);  // 0.1.33d (review S-A)
  }
  g.registros.push_back(r);
}

void GrabarTextura(PPCContext& ctx, uint8_t* base, uint8_t tipo) {
  const uint32_t lr = uint32_t(ctx.lr);
  if (lr < kFuncLo || lr >= kFuncHi) {
    if (!g.cap_en_escena) g.cap_motivo |= kForeign;
    return;
  }
  if (EnNulo(lr)) {
    g.cap_motivo |= kNullMaterial;
    return;
  }
  if (lr != (tipo == kTexVs ? kLrTexVs : kLrTexPs) || ctx.r1.u32 != g.cap_sp - kFrame) {
    g.cap_motivo |= kTexOdd;
    return;
  }
  // The texture is the word at r1+96 of the function's frame; checked against what the function just did with it:
  // r4 = [tex+24], r5 = [tex+20], [tex+28..35] = the global double.
  const uint32_t tex = L32(base, ctx.r1.u32 + 96);
  if (tex == 0 || tex > 0xFFFFFF00u || L32(base, tex + 24) != ctx.r4.u32 || L32(base, tex + 20) != ctx.r5.u32 ||
      std::memcmp(base + tex + 28, base + kLastRenderTime, 8) != 0) {
    g.cap_motivo |= kTexOdd;
    return;
  }
  if (!HayRoom(0)) {
    g.cap_motivo |= kFull;
    return;
  }
  Registro r{};
  r.tipo = tipo;
  r.r3 = ctx.r3.u32;
  r.a = tex;
  r.lr = lr;
  r.f1 = ctx.f1.u64;
  std::memcpy(r.guarda, base + tex + 40, 16);
  r.g1 = L32(base, kGlobalF1);
  r.g2 = L32(base, kGlobalF2);
  g.registros.push_back(r);
}

// True when the call is the function's own (it then runs under cap_en_escena).
bool GrabarEscena(PPCContext& ctx) {
  const uint32_t lr = uint32_t(ctx.lr);
  if (lr != kLrScene) {
    if (!g.cap_en_escena) g.cap_motivo |= kForeign;
    return false;
  }
  if (ctx.r3.u32 != g.cap_params + 60 || ctx.r4.u32 != g.cap_vista || ctx.r5.u32 != 0) {
    g.cap_motivo |= kSceneOdd;
    return true;
  }
  if (!HayRoom(0)) {
    g.cap_motivo |= kFull;
    return true;
  }
  Registro r{};
  r.tipo = kScene;
  r.r3 = ctx.r3.u32;
  r.a = ctx.r4.u32;
  r.lr = lr;
  g.registros.push_back(r);
  return true;
}

inline uint32_t Mezcla(uint32_t params, uint32_t shader, uint32_t proxy, uint32_t t, uint32_t rt, uint32_t vista) {
  uint64_t h = (uint64_t(params) << 32 | proxy) * 0x9E3779B97F4A7C15ull;
  h ^= (uint64_t(shader) << 32 | vista) * 0xC2B2AE3D27D4EB4Full;
  h ^= (uint64_t(t) << 32 | rt) * 0x165667B19E3779F9ull;
  h ^= h >> 29;
  h *= 0xBF58476D1CE4E5B9ull;
  return uint32_t(h >> 40);
}

// The view's generation: its first kViewBytes compared with the copy taken when it was first seen in this Render; a
// change takes a new copy and a new generation (the records made with the old bytes no longer match). 0 = no room.
uint32_t GeneracionVista(uint8_t* base, uint32_t vista) {
  for (uint32_t i = 0; i < g.n_vistas; ++i) {
    Vista& v = g.vistas[i];
    if (v.dir != vista) continue;
    if (std::memcmp(v.copia, base + vista, kViewBytes) != 0) {
      for (uint32_t b = 0; b < kViewBytes; ++b) {  // rare: which bytes, for the report
        if (v.copia[b] == base[vista + b]) continue;
        if (b < g_cambio_lo) g_cambio_lo = b;
        if (b > g_cambio_hi) g_cambio_hi = b;
      }
      std::memcpy(v.copia, base + vista, kViewBytes);
      ++v.gen;
      g_vistas_cambiadas.Sumar();
    }
    return v.gen;
  }
  if (g.n_vistas >= kVistas) return 0;
  Vista& v = g.vistas[g.n_vistas++];
  v.dir = vista;
  v.gen = 1;
  std::memcpy(v.copia, base + vista, kViewBytes);
  return v.gen;
}

// Still what was recorded? (The view was checked by GeneracionVista.)
bool GuardasValen(uint8_t* base, const Entrada& e) {
  if (std::memcmp(e.proxy_huella, base + e.proxy, 32) != 0) return false;
  if (L32(base, kDefaultTex) != e.tex_def[0] || L32(base, kDefaultTex + 4) != e.tex_def[1]) return false;
  const uint32_t g1 = L32(base, kGlobalF1), g2 = L32(base, kGlobalF2);
  for (uint32_t i = 0; i < e.cuantos; ++i) {
    const Registro& r = g.registros[e.primero + i];
    if ((r.tipo == kConstVs || r.tipo == kConstPs) && !r.en_linea) {
      // 0.1.33d (review S-A): a replay passes the pointer; the bytes there must still be the recorded ones.
      if (Huella(base + r.a, r.bytes) != r.huella) return false;
      continue;
    }
    if (r.tipo != kTexPs && r.tipo != kTexVs) continue;
    if (r.g1 != g1 || r.g2 != g2 || std::memcmp(r.guarda, base + r.a + 40, 16) != 0) return false;
  }
  return true;
}

// 0.1.33e (review M-1): solo_constantes = an audit's replay: only the constant setter calls before the scene call run
// (everything they write is saved and put back by Auditar); the texture and scene calls are compared through the
// records instead, and the game's shadow is taken just before its own scene call (sub_8260B560 hook).
void Reproducir(PPCContext& ctx, uint8_t* base, const Entrada& e, bool solo_constantes = false) {
  const uint64_t lr0 = ctx.lr;
  const uint32_t sp = ctx.r1.u32;
  const uint32_t marco = sp - kFrame;
  REX_STORE_U32(marco, sp);  // stwu r1,-384(r1): the back chain
  ctx.r1.u32 = marco;
  for (uint32_t i = 0; i < e.cuantos; ++i) {
    const Registro& r = g.registros[e.primero + i];
    if (solo_constantes) {
      if (r.tipo == kScene) break;  // the shadow is compared as it is just before the scene call
      if (r.tipo != kConstVs && r.tipo != kConstPs) continue;
    }
    ctx.lr = r.lr;
    switch (r.tipo) {
      case kConstVs:
      case kConstPs: {
        uint32_t src = r.a;
        if (r.en_linea) {
          src = marco + kScratchAt;
          std::memcpy(base + src, g.datos.data() + r.a, r.bytes);
        }
        ctx.r3.u64 = r.r3;
        ctx.r4.u64 = r.bytes;
        ctx.r5.u64 = src;
        if (r.tipo == kConstVs) {
          sub_82371840(ctx, base);
        } else {
          sub_82371960(ctx, base);
        }
        break;
      }
      case kTexPs:
      case kTexVs: {
        const uint32_t tex = r.a;
        ctx.r5.u64 = L32(base, tex + 20);                          // lwz r5,20(r11)
        ctx.r4.u64 = L32(base, tex + 24);                          // lwz r4,24(r11)
        std::memcpy(base + tex + 28, base + kLastRenderTime, 8);   // lfd / stfd / stw 28, stw 32
        ctx.r3.u64 = r.r3;
        ctx.f1.u64 = r.f1;
        if (r.tipo == kTexPs) {
          __imp__sub_82371590(ctx, base);
        } else {
          __imp__sub_82371700(ctx, base);
        }
        break;
      }
      default:  // kScene
        ctx.r3.u64 = r.r3;
        ctx.r4.u64 = r.a;
        ctx.r5.u64 = 0;
        __imp__sub_8260B560(ctx, base);
        break;
    }
  }
  ctx.r1.s64 = ctx.r1.s64 + kFrame;  // addi r1,r1,384
  ctx.lr = lr0;
  g_registros_rep.Sumar(e.cuantos);
}

// Runs the function recording its calls after the current end of the arenas. Returns the reasons it cannot be
// replayed (0 = it can); the records are registros[primero .. size).
uint32_t Grabar(PPCContext& ctx, uint8_t* base, uint32_t params, uint32_t vista, uint32_t* primero) {
  *primero = uint32_t(g.registros.size());
  g.cap_sp = ctx.r1.u32;
  g.cap_params = params;
  g.cap_vista = vista;
  g.cap_motivo = 0;
  g.cap_en_escena = false;
  g_cap_ctx.store(&ctx, std::memory_order_relaxed);
  __imp__sub_828C3270(ctx, base);
  g_cap_ctx.store(nullptr, std::memory_order_relaxed);
  return g.cap_motivo;
}

// xmow_material_replay 1: the fresh record [primero2, size) against the kept one. True when they are the same.
bool Iguales(const Entrada& e, uint32_t primero2, uint32_t motivo2, const char** que) {
  const uint32_t cuantos2 = uint32_t(g.registros.size()) - primero2;
  if (motivo2 != 0) {
    *que = "the second run is not replayable";
    return false;
  }
  if (cuantos2 != e.cuantos) {
    *que = "the number of calls";
    return false;
  }
  for (uint32_t i = 0; i < cuantos2; ++i) {
    const Registro& a = g.registros[e.primero + i];
    const Registro& b = g.registros[primero2 + i];
    if (a.tipo != b.tipo || a.lr != b.lr || a.r3 != b.r3 || a.bytes != b.bytes || a.en_linea != b.en_linea) {
      *que = "a call's kind, site, register or size";
      return false;
    }
    if (a.tipo == kConstVs || a.tipo == kConstPs) {
      if (a.en_linea) {
        if (std::memcmp(g.datos.data() + a.a, g.datos.data() + b.a, a.bytes) != 0) {
          *que = "the bytes of a computed constant";
          return false;
        }
      } else if (a.a != b.a) {
        *que = "the pointer of a persistent constant";
        return false;
      } else if (g.pre[i] != b.huella) {
        // 0.1.33d (review S-A): same pointer, but the game's run read other bytes there than a replay would pass.
        *que = "the content of a persistent constant (what a replay would pass)";
        return false;
      }
    } else if (a.tipo == kScene) {
      if (a.a != b.a) {
        *que = "the scene textures call";
        return false;
      }
    } else if (a.a != b.a || a.f1 != b.f1) {
      *que = a.a != b.a ? "a texture" : "a texture's mip bias";
      return false;
    }
  }
  return true;
}

// 0.1.33d: the audit of one would-be replay (VERIFY: every one; mode 2: one in kAuditarCada). The replay runs, the
// constant shadow it leaves is kept and the shadow before it put back, then the game's function runs recording; the
// two records, the persistent contents and the two shadows are compared. The game's run is what stays.
enum class Auditoria { kIgual, kDiferente, kSinSitio };

void CopiarSombra(uint8_t* base, uint32_t device, uint8_t* a) {
  std::memcpy(a, base + device, 16);
  std::memcpy(a + 16, base + device + kSombraLo, kSombraHi - kSombraLo);
}

Auditoria Auditar(PPCContext& ctx, uint8_t* base, const Entrada& e, uint32_t params, uint32_t vista, const char** que) {
  if (g.pre.size() < e.cuantos) g.pre.resize(e.cuantos);
  for (uint32_t i = 0; i < e.cuantos; ++i) {
    const Registro& r = g.registros[e.primero + i];
    g.pre[i] = ((r.tipo == kConstVs || r.tipo == kConstPs) && !r.en_linea) ? Huella(base + r.a, r.bytes) : 0;
  }
  const uint32_t device = L32(base, kDevicePtr);
  // 0.1.33e (review M-1): the replay runs only when everything it can write is saved first: the constant shadow and
  // masks, and the scratch array (flag, header, up to kScratchMax bytes of data). Afterwards all of it is put back,
  // so whatever the comparison says (or with no room to record), nothing of the replay stays visible to the game.
  const uint32_t datos_scratch = L32(base, kScratchArr), num_scratch = L32(base, kScratchArr + 4);
  const bool scratch_ok = num_scratch <= kScratchMax && (num_scratch == 0 || (datos_scratch != 0 &&
                                                                                datos_scratch <= 0xFFFFFFFFu - kScratchMax));
  const bool sombra = device != 0 && device <= 0xFFFFFFFFu - kSombraHi && scratch_ok;
  if (sombra) {
    CopiarSombra(base, device, g.s0);
    std::memcpy(g.scratch0, base + kScratchFlag, 4);
    std::memcpy(g.scratch0 + 4, base + kScratchArr, 12);
    if (num_scratch) std::memcpy(g.scratch0 + 16, base + datos_scratch, num_scratch);
    const PPCContext guardado = ctx;
    Reproducir(ctx, base, e, true);
    CopiarSombra(base, device, g.sa);
    ctx = guardado;  // the function below starts with the call's own registers
    ctx.fpscr.restoreGuestBits(guardado.fpscr.csr);
    std::memcpy(base + device, g.s0, 16);
    std::memcpy(base + device + kSombraLo, g.s0 + 16, kSombraHi - kSombraLo);
    std::memcpy(base + kScratchFlag, g.scratch0, 4);
    std::memcpy(base + kScratchArr, g.scratch0 + 4, 12);
    if (num_scratch) std::memcpy(base + datos_scratch, g.scratch0 + 16, num_scratch);
  }
  const size_t n_reg = g.registros.size(), n_dat = g.datos.size();
  uint32_t primero2 = 0;
  g.audit_device = sombra ? device : 0;
  g.sb_tomada = false;
  const uint32_t motivo2 = Grabar(ctx, base, params, vista, &primero2);
  g.audit_device = 0;
  if (sombra && !g.sb_tomada) CopiarSombra(base, device, g.sb);  // no scene call: the shadow at the end
  Auditoria res = Auditoria::kIgual;
  if (motivo2 & kFull) {  // review N-3: no room to record the second run: nothing to compare (not a difference)
    res = Auditoria::kSinSitio;
  } else if (!Iguales(e, primero2, motivo2, que)) {
    res = Auditoria::kDiferente;
  } else if (sombra) {
    // The game's shadow just before its scene call, against the replay's (constant calls up to the scene call).
    if (L32(base, kDevicePtr) != device || std::memcmp(g.sb, g.sa, kSombraBytes) != 0) {
      *que = "the constant shadow or dirty masks a replay leaves";
      res = Auditoria::kDiferente;
    }
  }
  g.registros.resize(n_reg);
  g.datos.resize(n_dat);
  return res;
}

}  // namespace

// 0.1.34b (the bench, xmow_bench.cpp): mode 2's audits since start and whether an audit difference switched mode 2
// off for this boot (the cvar can say 2 while it is off).
void EstadoMaterialReplay(uint64_t* auditadas, uint64_t* diferencias, bool* apagado) {
  *auditadas = g_auditadas_total.load(std::memory_order_relaxed);
  *diferencias = g_difs_total.load(std::memory_order_relaxed);
  *apagado = g_apagado.load(std::memory_order_relaxed);
}

void TocarConstante(PPCContext& ctx, uint8_t* base, int pixel) { GrabarConstante(ctx, base, pixel ? kConstPs : kConstVs); }

void InformeMaterial(double seconds) {
  const uint64_t llamadas = g_llamadas.Tomar(), fuera = g_fuera.Tomar(), repetidas = g_repetidas.Tomar();
  const uint64_t grabadas = g_grabadas.Tomar(), no_rep = g_no_repetibles.Tomar(), guardas = g_guardas.Tomar();
  const uint64_t regs = g_registros_rep.Tomar(), sin_sitio = g_sin_sitio.Tomar(), verif = g_verificadas.Tomar();
  const uint64_t difs = g_diferencias.Tomar(), renders = g_renders.Tomar(), vistas = g_vistas_cambiadas.Tomar();
  const uint64_t pila = g_pila.Tomar();
  const uint32_t motivos = g_motivos;
  g_motivos = 0;
  const uint32_t cambio_lo = g_cambio_lo, cambio_hi = g_cambio_hi;
  g_cambio_lo = 0xFFFFFFFFu;
  g_cambio_hi = 0;
  const int modo = REXCVAR_GET(xmow_material_replay);
  if (modo == 0 && !llamadas) return;
  const double r = renders ? double(renders) : 1.0;
  REXLOG_INFO("[xmow cpu33] {:.1f} s: material parameters (xmow_material_replay {}): {} scene renders; per render: "
              "{:.0f} sub_828C3270 calls, {:.0f} replayed ({:.1f} RHI calls each), {:.0f} evaluated and recorded, "
              "{:.0f} evaluated and not replayable (reasons {:#x}: 1 no material, 2 foreign RHI call, 4 big frame "
              "source, 8 odd texture call, 16 odd scene call, 32 record full), {:.0f} evaluated again because a guard "
              "changed, {:.0f} with a stack proxy, {:.0f} without a table slot; view contents changed {} times (bytes "
              "{}..{} of the view); {} calls outside a scene render (never replayed)",
              seconds, modo, renders, double(llamadas) / r, double(repetidas) / r,
              repetidas ? double(regs) / double(repetidas) : 0.0, double(grabadas) / r, double(no_rep) / r, motivos,
              double(guardas) / r, double(pila) / r, double(sin_sitio) / r, vistas, vistas ? cambio_lo : 0u,
              vistas ? cambio_hi : 0u, fuera);
  if (modo == 1 || verif) {
    REXLOG_INFO("[xmow cpu33] {:.1f} s: material parameters VERIFY: {} would-be replays replayed, run and compared "
                "(calls, bytes, persistent contents, constant shadow), {} DIFFERENCES ({})",
                seconds, verif, difs,
                difs ? "do not use xmow_material_replay 2" : "none: replaying would have set the same parameters");
  }
  const uint64_t auditadas = g_auditadas.Tomar(), auditoria_difs = g_auditoria_difs.Tomar();
  if (modo == 2 || auditadas || g_apagado.load(std::memory_order_relaxed)) {
    REXLOG_INFO("[xmow cpu33] {:.1f} s: material parameters ON audit: {} replays audited (1 in {}), {} differences{}",
                seconds, auditadas, kAuditarCada, auditoria_difs,
                g_apagado.load(std::memory_order_relaxed) ? "; mode 2 is OFF for this boot after a difference" : "");
  }
}

}  // namespace xmow::cpu33

// FSceneRenderer::Render (called from other files and through the function table only): the life of the record.
REX_HOOK_RAW(sub_826083C8) {
  using namespace xmow::cpu33;
  const int modo = REXCVAR_GET(xmow_material_replay);
  const bool apagado = modo == 0 || (modo == 2 && g_apagado.load(std::memory_order_relaxed));
  static int modo_rechazado = 0;  // a mode whose records could not be allocated (until another mode is chosen)
  if (modo == 0) modo_rechazado = 0;
  if (apagado || g.render_ctx.load(std::memory_order_relaxed) != nullptr) {
    // 0.1.33d (review S-C): back to 0 (or mode 2 turned off by an audit): the records go back to the heap.
    if (apagado && !g.tabla.empty() && g.render_ctx.load(std::memory_order_relaxed) == nullptr) {
      Liberar();
      REXLOG_INFO("[xmow cpu33] material parameters: records freed (xmow_material_replay {})", modo);
    }
    __imp__sub_826083C8(ctx, base);
    return;
  }
  // 0.1.33c/d: the ~3.2 MB of records are taken from the host heap when the mode leaves 0, only with at least 64 MB
  // of headroom (the process runs near its memory ceiling: a reservation that succeeds could make a later deko3d or
  // guest allocation fail). A refusal or a failed allocation keeps the feature off until the mode is chosen again.
  if (modo == modo_rechazado) {
    __imp__sub_826083C8(ctx, base);
    return;
  }
  modo_rechazado = 0;
  if (g.tabla.empty()) {
    const size_t holgura = HolguraHeap();
    bool ok = holgura >= kHolguraMinima;
    if (ok) {
      try {
        g.tabla.resize(kRanuras);
        g.registros.reserve(kMaxRegistros);
        g.datos.reserve(kMaxDatos);
      } catch (const std::bad_alloc&) {
        ok = false;
      }
    }
    if (!ok) {
      Liberar();
      modo_rechazado = modo;
      REXLOG_WARN("[xmow cpu33] material parameters: the records (about {} KB) are not allocated, heap headroom {} MB "
                  "(at least {} MB needed): xmow_material_replay {} stays inactive until it is chosen again",
                  (kRanuras * sizeof(Entrada) + kMaxRegistros * sizeof(Registro) + kMaxDatos) >> 10, holgura >> 20,
                  kHolguraMinima >> 20, modo);
      __imp__sub_826083C8(ctx, base);
      return;
    }
    REXLOG_INFO("[xmow cpu33] material parameters: records allocated ({} slots, {} records, {} KB of data; heap "
                "headroom was {} MB)",
                kRanuras, kMaxRegistros, kMaxDatos >> 10, holgura >> 20);
  }
  AvanzarEpoca();
  g.registros.clear();
  g.datos.clear();
  g.n_vistas = 0;
  g.modo = modo;
  g.render_ctx.store(&ctx, std::memory_order_relaxed);
  g_renders.Sumar();
  // 0.1.33c: the record ends even if the guest call leaves by an exception (as g_en_prepaso, review N4).
  struct FinDelRender {
    ~FinDelRender() {
      g.render_ctx.store(nullptr, std::memory_order_relaxed);
      AvanzarEpoca();
    }
  } fin;
  __imp__sub_826083C8(ctx, base);
}

REX_HOOK_RAW(sub_828C3270) {
  using namespace xmow::cpu33;
  PPCContext* const dueno = g.render_ctx.load(std::memory_order_relaxed);
  if (dueno != &ctx) {
    if (dueno != nullptr || REXCVAR_GET(xmow_material_replay) != 0) g_fuera.Sumar();
    __imp__sub_828C3270(ctx, base);
    return;
  }
  if (g.modo == 0) {  // 0.1.33e (review M-2): an audit difference in this Render: the game's code from now on
    __imp__sub_828C3270(ctx, base);
    return;
  }
  if (g_cap_ctx.load(std::memory_order_relaxed) == &ctx) {
    // A call from inside the call being recorded (never seen in the code): that record is not replayable.
    g.cap_motivo |= kForeign;
    __imp__sub_828C3270(ctx, base);
    return;
  }
  g_llamadas.Sumar();
  const uint32_t params = ctx.r3.u32, shader = ctx.r4.u32, cx = ctx.r5.u32, sp = ctx.r1.u32;
  const uint32_t proxy = cx ? L32(base, cx) : 0;
  const uint32_t vista = cx ? L32(base, cx + 12) : 0;
  if (cx == 0 || proxy == 0 || vista == 0 || proxy > 0xFFFFFF00u || vista > 0xFFFF0000u || sp < 0x10000) {
    g_no_repetibles.Sumar();
    __imp__sub_828C3270(ctx, base);
    return;
  }
  // A proxy in the thread's stack (a temporary of the caller) may be another object at the next call.
  if (uint64_t(proxy) + kStackWindow > sp && proxy < uint64_t(sp) + kStackWindow) {
    g_pila.Sumar();
    __imp__sub_828C3270(ctx, base);
    return;
  }
  const uint32_t t = L32(base, cx + 4), rt = L32(base, cx + 8);
  const uint32_t vista_gen = GeneracionVista(base, vista);
  if (vista_gen == 0) {
    g_sin_sitio.Sumar();
    __imp__sub_828C3270(ctx, base);
    return;
  }
  // Find the key, or the slot for it.
  const uint32_t h = Mezcla(params, shader, proxy, t, rt, vista);
  Entrada* e = nullptr;
  Entrada* libre = nullptr;
  for (uint32_t i = 0; i < kSondas; ++i) {
    Entrada& c = g.tabla[(h + i) & (kRanuras - 1)];
    if (c.epoca != g.epoca) {
      libre = &c;
      break;
    }
    if (c.params == params && c.shader == shader && c.proxy == proxy && c.tiempo == t && c.tiempo_real == rt &&
        c.vista == vista) {
      e = &c;
      break;
    }
  }
  if (e != nullptr && e->motivo == 0 && e->vista_gen == vista_gen && GuardasValen(base, *e)) {
    if (g.modo == 2 && (++g.cuenta_auditoria % kAuditarCada) != 0) {
      Reproducir(ctx, base, *e);
      g_repetidas.Sumar();
      return;
    }
    // VERIFY (every would-be replay) or a mode 2 audit: replay, run the game's code recording, compare (Auditar).
    const char* que = "";
    const Auditoria a = Auditar(ctx, base, *e, params, vista, &que);
    if (a == Auditoria::kSinSitio) {
      g_sin_sitio.Sumar();
      return;
    }
    if (g.modo == 2) {
      g_auditadas.Sumar();
      g_auditadas_total.fetch_add(1, std::memory_order_relaxed);
      if (a == Auditoria::kDiferente) {
        g_auditoria_difs.Sumar();
        g_difs_total.fetch_add(1, std::memory_order_relaxed);
        g.modo = 0;  // 0.1.33e (review M-2): no more replays in this Render either
        if (!g_apagado.exchange(true, std::memory_order_relaxed)) {
          REXLOG_WARN("[xmow cpu33] material parameters ON audit: a replay differs from the game's run in {} (this "
                      "{:08X}, shader {:08X}, proxy {:08X}, view {:08X}, {} calls recorded): this frame drew with the "
                      "game's run; xmow_material_replay goes back to 0, mode 2 stays off for this boot",
                      que, params, shader, proxy, vista, e->cuantos);
          rex::cvar::SetFlagByName("xmow_material_replay", "0");
        }
      }
      return;
    }
    g_verificadas.Sumar();
    if (a == Auditoria::kDiferente) {
      g_diferencias.Sumar();
      if (g_avisos < 12) {
        g_avisos = g_avisos + 1;
        REXLOG_WARN("[xmow cpu33] material parameters VERIFY: a second sub_828C3270 call in the same scene render "
                    "differs in {} (this {:08X}, shader {:08X}, proxy {:08X}, view {:08X}, {} calls recorded)",
                    que, params, shader, proxy, vista, e->cuantos);
      }
    }
    return;
  }
  // Not replayable as it is: evaluate, and record when there is somewhere to keep it.
  if (e != nullptr && e->motivo != 0) {
    g_no_repetibles.Sumar();
    __imp__sub_828C3270(ctx, base);
    return;
  }
  if (e != nullptr) {
    g_guardas.Sumar();  // a guard or the view changed: recorded again in the same slot
  } else if (libre == nullptr) {
    g_sin_sitio.Sumar();
    __imp__sub_828C3270(ctx, base);
    return;
  } else {
    e = libre;
  }
  const size_t n_dat = g.datos.size();
  uint32_t primero = 0;
  // The proxy's first bytes and the default textures as they are before the call (what the evaluation reads).
  uint8_t huella[32];
  std::memcpy(huella, base + proxy, 32);
  const uint32_t def0 = L32(base, kDefaultTex), def1 = L32(base, kDefaultTex + 4);
  const uint32_t motivo = Grabar(ctx, base, params, vista, &primero);
  e->epoca = g.epoca;
  e->params = params;
  e->shader = shader;
  e->proxy = proxy;
  e->tiempo = t;
  e->tiempo_real = rt;
  e->vista = vista;
  e->vista_gen = vista_gen;
  e->tex_def[0] = def0;
  e->tex_def[1] = def1;
  std::memcpy(e->proxy_huella, huella, 32);
  e->motivo = motivo;
  if (motivo != 0) {
    g.registros.resize(primero);
    g.datos.resize(n_dat);
    e->primero = e->cuantos = 0;
    g_motivos = g_motivos | motivo;
    g_no_repetibles.Sumar();
  } else {
    e->primero = primero;
    e->cuantos = uint32_t(g.registros.size()) - primero;
    g_grabadas.Sumar();
  }
}

REX_HOOK_RAW(sub_82371590) {
  using namespace xmow::cpu33;
  if (g_cap_ctx.load(std::memory_order_relaxed) == &ctx) [[unlikely]] GrabarTextura(ctx, base, kTexPs);
  __imp__sub_82371590(ctx, base);
}

REX_HOOK_RAW(sub_82371700) {
  using namespace xmow::cpu33;
  if (g_cap_ctx.load(std::memory_order_relaxed) == &ctx) [[unlikely]] GrabarTextura(ctx, base, kTexVs);
  __imp__sub_82371700(ctx, base);
}

REX_HOOK_RAW(sub_8260B560) {
  using namespace xmow::cpu33;
  if (g_cap_ctx.load(std::memory_order_relaxed) == &ctx && GrabarEscena(ctx)) {
    if (g.audit_device && !g.sb_tomada) {  // 0.1.33e: an audit's game run, at its scene call (review M-1)
      CopiarSombra(base, g.audit_device, g.sb);
      g.sb_tomada = true;
    }
    g.cap_en_escena = true;
    __imp__sub_8260B560(ctx, base);
    g.cap_en_escena = false;
    return;
  }
  __imp__sub_8260B560(ctx, base);
}
