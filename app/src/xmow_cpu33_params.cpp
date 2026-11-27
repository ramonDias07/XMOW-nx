// XMOW 0.1.33 - native RHI shader constant setters (xmow_native_shader_params).
//
// Test 34 stack profile (0.1.27, render thread, 61779 samples at 1800-2800 draws): sub_82371840 3.35 % inclusive
// (1.02 % self + 2.34 % in sub_82B30EC8) and sub_82371960 about 1.0 % (with sub_82B30FA0): the two functions every
// shader parameter of every draw goes through (callers: the per-mesh setters sub_828AB9B8 / sub_828C3F00 /
// sub_8254C0F0 and the material parameters sub_828C3270). What they do, read from the recompiled code
// (xmow_recomp.137.cpp / .148.cpp; the Direct3D leaves in .38.cpp / .151.cpp):
//
//   sub_82371840(r3 = byte offset of the first constant (register * 16), r4 = NumBytes, r5 = source)
//     rounded = (NumBytes + 15) & ~15. When rounded != NumBytes the source is first copied into a function-static
//     scratch TArray<BYTE> (0x8360C000: Data +0, ArrayNum +4, ArrayMax +8; "constructed" flag = bit 0 of 0x8360BFFC),
//     grown when ArrayNum < rounded, and the tail [NumBytes, rounded) is zeroed; the source becomes the scratch.
//     Then register = offset >> 4, count = (NumBytes + 15) >> 4, and a dirty mask with one bit per group of 4
//     registers (bit 63 - n = registers 4n..4n+3) for first = register >> 2 .. last = (register + count - 1) >> 2:
//     ((int64)0x8000000000000000 >> (last - first)) >> first. Calls
//   sub_82B30EC8(device = *(0x8357F01C), register, source, count, mask): copies count * 16 bytes (lvlx/lvrx pairs = an
//     unaligned 16 byte read; stvx = an aligned 16 byte write) to device + (register + 120) * 16 (the vertex shader
//     constant shadow, device+1920) and ORs the mask into the 64-bit word at device+0.
//   sub_82371960 / sub_82B30FA0: the same with device + (register + 376) * 16 (the pixel shader constant shadow,
//     device+6016) and the mask word at device+8. Both share the same scratch array.
//
// The recompiled lvlx/lvrx/vor/stvx sequence is what costs (each one a byte shuffle through the vector register
// emulation); the work itself is a memcpy and a 64-bit OR. Native: exactly the same guest memory writes (the scratch
// array included), in the same order, for the cases below; anything else goes to the recompiled function:
//   NumBytes 1..4096, register + count <= 256 (the shadow has 256 registers per shader type: first/last <= 63, so
//   both shifts are below 64), a non-null device, source and destination not overlapping, and for an unrounded size
//   the scratch array already constructed and large enough (the first calls with a new largest size grow it through
//   the recompiled function; afterwards ArrayNum never shrinks).
// Registers on return: the callers are compiled C++ (UE3) using the standard ABI, which treats r0, r3-r12, f0-f13,
// ctr, xer and cr0/1/5/6/7 as dead after a call; r1, lr and r14-r31 are not touched here. (The generated callers copy
// ctx back into their locals after the call, but only what the PPC code then reads matters, and by the ABI it reads
// none of the volatile ones before writing them.) The hook leaves r3-r12 as they were at entry.
// With xmow_material_replay recording (xmow_cpu33_material.cpp) each call is first reported to it, unchanged.
// 0.1.33d (Fable review 1.3), xmow_native_shader_params 2 = SELF-CHECK: the native plan is worked out (the same tests
// as 1) but not done; the recompiled function runs, and then the shadow bytes it wrote, the dirty mask word and, for
// an unrounded size, the scratch array are compared with what the native code would have written. Differences are
// counted in the 10 s line and the first 8 logged. Slower than 0 (a copy of the source per call); the image is the
// recompiled code's.
// No same-file caller exists for either function (so no call was inlined by patch_inline_calls): every call goes
// through the weak alias or the function table, both of which resolve to these hooks.

#include "xmow_pch.h"

#include <cstdint>
#include <cstring>

#include <rex/cvar.h>
#include <rex/logging.h>

#include "xmow_cpu33.h"

REXCVAR_DEFINE_INT32(xmow_native_shader_params, 1, "XMOW",
                     "0.1.33: the RHI's shader constant setters (sub_82371840 vertex, sub_82371960 pixel, with "
                     "Direct3D's copies sub_82B30EC8 / sub_82B30FA0) run as native code: the same bytes to the same "
                     "places of Direct3D's constant shadow, the same dirty mask, the same scratch array. Less render "
                     "thread time per shader parameter. 1 (default) = native; 0 = the recompiled code, as in 0.1.32; "
                     "2 = SELF-CHECK (0.1.33d): the recompiled code runs and every call is compared with what the "
                     "native code would have written, differences in the log. Live")
    .range(0, 2);

namespace xmow::cpu33 {
namespace {

constexpr uint32_t kDevicePtr = 0x8357F01C;    // lis r9,-31912 / lwz r3,-4068(r9): the Direct3D device
constexpr uint32_t kScratchFlag = 0x8360BFFC;  // lis r10,-31903 / lwz r11,-16388(r10): bit 0 = scratch constructed
constexpr uint32_t kScratch = 0x8360C000;      // lis r11,-31903 / addi r31,r11,-16384: TArray<BYTE> {Data, Num, Max}
constexpr uint32_t kMaxBytes = 4096;           // 256 registers
constexpr uint32_t kRegisters = 256;

Contador g_vs, g_ps, g_bytes, g_scratch, g_original;
Contador g_comprobadas, g_diferencias;  // 0.1.33d: SELF-CHECK
volatile uint32_t g_avisos = 0;         // SELF-CHECK difference lines logged (the first 8)

// memcpy with the sizes these calls nearly always have as constants (the compiler then copies with a few loads and
// stores instead of calling memcpy).
inline void Copiar(uint8_t* d, const uint8_t* s, uint32_t n) {
  switch (n) {
    case 4: std::memcpy(d, s, 4); break;
    case 12: std::memcpy(d, s, 12); break;
    case 16: std::memcpy(d, s, 16); break;
    case 32: std::memcpy(d, s, 32); break;
    case 48: std::memcpy(d, s, 48); break;
    case 64: std::memcpy(d, s, 64); break;
    default: std::memcpy(d, s, n); break;
  }
}

// What one call writes, worked out from the arguments without writing anything (0.1.33d: shared by the native path
// and SELF-CHECK). False = the recompiled code decides (the cases listed above).
struct Plan {
  uint32_t device, src, dst, n, bytes, rounded, data;  // data = the scratch array's Data (scratch only)
  uint64_t mask;
  uint32_t mask_at;
  bool scratch;
};

inline bool Planear(PPCContext& ctx, uint8_t* base, uint32_t shadow_reg0, uint32_t mask_at, Plan& p) {
  const uint32_t bytes = ctx.r4.u32;
  if (bytes == 0 || bytes > kMaxBytes) return false;
  const uint32_t rounded = (bytes + 15) & ~15u;  // addi r27,r4,15 / rlwinm r30,r27,0,0,27
  const uint32_t reg = (ctx.r3.u32 >> 4);        // rlwinm r4,r26,28,4,31
  const uint32_t count = (bytes + 15) >> 4;      // rlwinm r6,r27,28,4,31
  if (reg >= kRegisters || reg + count > kRegisters) return false;
  const uint32_t device = REX_LOAD_U32(kDevicePtr);
  if (device == 0) return false;
  uint32_t src = ctx.r5.u32;
  const uint32_t dst = (device + ((reg + shadow_reg0) << 4)) & ~15u;  // stvx128: the address rounded down to 16
  const uint32_t n = count * 16;
  // Nothing here may wrap the 4 GB guest window (never the case for real pointers; the recompiled code decides).
  if (src > 0xFFFFFFFFu - kMaxBytes || dst > 0xFFFFFFFFu - kMaxBytes) return false;
  p.data = 0;
  p.scratch = false;
  if (rounded != bytes) {                                    // cmplw cr6,r30,r4 / beq
    if (!(REX_LOAD_U32(kScratchFlag) & 1)) return false;     // not constructed yet: the recompiled code does it
    if (REX_LOAD_U32(kScratch + 4) < rounded) return false;  // cmplw r11,r30 / bge: it would grow the array
    const uint32_t data = REX_LOAD_U32(kScratch);
    if (data == 0 || data > 0xFFFFFFFFu - kMaxBytes) return false;
    // The source must not overlap the scratch (the original's memcpy, sub_82C2A290), nor the scratch the shadow.
    if (src < data + rounded && data < src + bytes) return false;
    if (data < dst + n && dst < data + rounded) return false;
    p.data = data;
    p.scratch = true;
  } else if (src < dst + n && dst < src + n) {
    return false;  // overlapping source and shadow: the original's 4-at-a-time order would matter
  }
  // first = register >> 2, last = (count + register - 1) >> 2, mask = ((1 << 63) sra (last - first)) srd first.
  const uint32_t first = reg >> 2;
  const uint32_t last = (count + reg - 1) >> 2;
  p.mask = uint64_t(int64_t(INT64_MIN) >> (last - first)) >> first;
  p.device = device;
  p.src = src;
  p.dst = dst;
  p.n = n;
  p.bytes = bytes;
  p.rounded = rounded;
  p.mask_at = mask_at;
  return true;
}

// True when it did the whole call. shadow_reg0 = 120 (vertex, device+1920) or 376 (pixel, device+6016); mask_at =
// 0 or 8.
inline bool Poner(PPCContext& ctx, uint8_t* base, uint32_t shadow_reg0, uint32_t mask_at) {
  Plan p;
  if (!Planear(ctx, base, shadow_reg0, mask_at, p)) return false;
  uint32_t src = p.src;
  if (p.scratch) {
    Copiar(base + p.data, base + src, p.bytes);                  // bl 0x82c2a290 (memcpy)
    std::memset(base + p.data + p.bytes, 0, p.rounded - p.bytes);  // bl 0x82c2a1f0 (memset, 0)
    src = p.data;                                                // lwz r5,0(r31)
  }
  Copiar(base + p.dst, base + src, p.n);
  REX_STORE_U64(p.device + p.mask_at, REX_LOAD_U64(p.device + p.mask_at) | p.mask);  // ld / or / std
  g_bytes.Sumar(p.n);
  if (p.scratch) g_scratch.Sumar();
  return true;
}

// 0.1.33d SELF-CHECK: the recompiled function runs; afterwards the shadow, the mask word and the scratch array must
// hold what the native path would have written.
template <void (*Recompilado)(PPCContext&, uint8_t*)>
void Comprobar(PPCContext& ctx, uint8_t* base, uint32_t shadow_reg0, uint32_t mask_at, const char* tipo) {
  Plan p;
  if (!Planear(ctx, base, shadow_reg0, mask_at, p)) {
    g_original.Sumar();
    Recompilado(ctx, base);
    return;
  }
  const uint32_t reg = ctx.r3.u32 >> 4;
  alignas(16) uint8_t esperado[kMaxBytes];
  std::memcpy(esperado, base + p.src, p.bytes);
  std::memset(esperado + p.bytes, 0, p.n - p.bytes);  // n == rounded: the zeroed tail of the scratch path
  const uint64_t mascara_antes = REX_LOAD_U64(p.device + p.mask_at);
  Recompilado(ctx, base);
  g_comprobadas.Sumar();
  const char* que = nullptr;
  if (REX_LOAD_U32(kDevicePtr) != p.device) {
    que = "the device pointer changed";
  } else if (std::memcmp(base + p.dst, esperado, p.n) != 0) {
    que = "the constant shadow bytes";
  } else if (REX_LOAD_U64(p.device + p.mask_at) != (mascara_antes | p.mask)) {
    que = "the dirty mask";
  } else if (p.scratch && (REX_LOAD_U32(kScratch) != p.data || std::memcmp(base + p.data, esperado, p.rounded) != 0)) {
    que = "the scratch array";
  }
  if (que) {
    g_diferencias.Sumar();
    if (g_avisos < 8) {
      g_avisos = g_avisos + 1;
      REXLOG_WARN("[xmow cpu33] native shader constant setters SELF-CHECK: {} call (register {}, {} bytes, source "
                  "{:08X}{}) differs in {}",
                  tipo, reg, p.bytes, ctx.r5.u32, p.scratch ? ", through the scratch array" : "", que);
    }
  }
}

}  // namespace

void InformeParametros(double seconds) {
  const uint64_t vs = g_vs.Tomar(), ps = g_ps.Tomar(), bytes = g_bytes.Tomar(), scratch = g_scratch.Tomar();
  const uint64_t original = g_original.Tomar();
  const uint64_t comprobadas = g_comprobadas.Tomar(), diferencias = g_diferencias.Tomar();
  const int32_t modo = REXCVAR_GET(xmow_native_shader_params);
  if (modo == 0 && !(vs | ps | original | comprobadas)) return;
  REXLOG_INFO("[xmow cpu33] {:.1f} s: native shader constant setters (xmow_native_shader_params {}): {} vertex and {} "
              "pixel calls, {} bytes copied, {} through the scratch array (sizes not a multiple of 16), {} calls left "
              "to the recompiled code",
              seconds, modo, vs, ps, bytes, scratch, original);
  if (modo == 2 || comprobadas) {
    REXLOG_INFO("[xmow cpu33] {:.1f} s: native shader constant setters SELF-CHECK: {} calls run by the recompiled code "
                "and compared, {} DIFFERENCES ({})",
                seconds, comprobadas, diferencias,
                diferencias ? "do not use xmow_native_shader_params 1" : "none: the native code writes the same");
  }
}

}  // namespace xmow::cpu33

REX_EXTERN(__imp__sub_82371840);
REX_HOOK_RAW(sub_82371840) {
  using namespace xmow::cpu33;
  if (g_cap_ctx.load(std::memory_order_relaxed) == &ctx) [[unlikely]] TocarConstante(ctx, base, 0);
  const int32_t modo = REXCVAR_GET(xmow_native_shader_params);
  if (modo == 1) {
    if (Poner(ctx, base, 120, 0)) {
      g_vs.Sumar();
      return;
    }
    g_original.Sumar();
  } else if (modo == 2) [[unlikely]] {
    Comprobar<__imp__sub_82371840>(ctx, base, 120, 0, "vertex");
    return;
  }
  __imp__sub_82371840(ctx, base);
}

REX_EXTERN(__imp__sub_82371960);
REX_HOOK_RAW(sub_82371960) {
  using namespace xmow::cpu33;
  if (g_cap_ctx.load(std::memory_order_relaxed) == &ctx) [[unlikely]] TocarConstante(ctx, base, 1);
  const int32_t modo = REXCVAR_GET(xmow_native_shader_params);
  if (modo == 1) {
    if (Poner(ctx, base, 376, 8)) {
      g_ps.Sumar();
      return;
    }
    g_original.Sumar();
  } else if (modo == 2) [[unlikely]] {
    Comprobar<__imp__sub_82371960>(ctx, base, 376, 8, "pixel");
    return;
  }
  __imp__sub_82371960(ctx, base);
}
