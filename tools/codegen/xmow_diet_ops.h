// Lote C: as funcoes de rex/ppc/diet_ops.h do fork do LoS1-nx (sdk/include/rex/ppc/diet_ops.h, BSD-3-Clause, de
// sa2-nx/sasrt-nx/banjo-nx) que o codigo das opcoes fctiwz_inline, fcti_inline, div_inline e fp_copy_pairs /
// fp_copy_forwarding chama. O sdk-g2 nao tem esse header; tools/make_nx_pch.py cola este texto no PCH do Switch
// (ponto 8), entao nada muda no SDK. So a parte que o lote 1 usa: as formas any-mode (fp_*_modeless,
// fp_mode_lookahead) ficam de fora ate essas opcoes entrarem. Texto das funcoes igual ao do fork.
#ifndef XMOW_DIET_OPS_H
#define XMOW_DIET_OPS_H
#include <bit>
#include <cstdint>

#include <simde/x86/sse2.h>  // fctid/fctiw fora do aarch64

namespace rex::ppc {

/// fctiwz: truncate toward zero to int32 with PPC saturation, NaN -> 0x80000000 (s64 register image: NaN zero
/// extended, everything else sign extended). AArch64 fcvtzs saturates as the PPC does; only NaN needs the select.
inline int64_t fctiwz(double v) noexcept {
#if defined(__aarch64__)
  int32_t r;
  asm("fcvtzs %w0, %d1" : "=r"(r) : "w"(v));
  return __builtin_isnan(v) ? int64_t(0x80000000U) : int64_t(r);
#else
  if (__builtin_isnan(v))
    return int64_t(0x80000000U);
  if (v >= 2147483647.0)
    return INT32_MAX;
  return (v > -2147483648.0) ? int64_t(int32_t(v)) : int64_t(INT32_MIN);
#endif
}

/// fctidz: truncate toward zero to int64 with PPC saturation, NaN -> INT64_MIN.
inline int64_t fctidz(double v) noexcept {
#if defined(__aarch64__)
  int64_t r;
  asm("fcvtzs %x0, %d1" : "=r"(r) : "w"(v));
  return __builtin_isnan(v) ? INT64_MIN : r;
#else
  if (__builtin_isnan(v))
    return INT64_MIN;
  if (v >= 9223372036854775808.0)
    return INT64_MAX;
  return (v >= -9223372036854775808.0) ? int64_t(v) : INT64_MIN;
#endif
}

/// fctid: round to int64 in the current rounding mode with PPC saturation, NaN -> INT64_MIN. On AArch64 frinti +
/// fcvtzs round halves to even as the 360 does (simde's portable path rounded halves away from zero).
inline int64_t fctid(double v) noexcept {
  if (__builtin_isnan(v)) [[unlikely]]
    return INT64_MIN;
#if defined(__aarch64__)
  double integral;
  asm("frinti %d0, %d1" : "=w"(integral) : "w"(v));
  int64_t r;
  asm("fcvtzs %x0, %d1" : "=r"(r) : "w"(integral));
  return r;
#else
  if (v >= 9223372036854775808.0)
    return INT64_MAX;
  return simde_mm_cvtsd_si64(simde_mm_set_sd(v));
#endif
}

/// fctiw: the same to int32, as the s64 register image (NaN -> 0x0000000080000000, everything else sign extended).
inline int64_t fctiw(double v) noexcept {
  if (__builtin_isnan(v)) [[unlikely]]
    return int64_t(0x80000000U);
#if defined(__aarch64__)
  double integral;
  asm("frinti %d0, %d1" : "=w"(integral) : "w"(v));
  int32_t r;
  asm("fcvtzs %w0, %d1" : "=r"(r) : "w"(integral));
  return int64_t(r);
#else
  if (v >= 2147483647.0)
    return INT32_MAX;
  return int64_t(simde_mm_cvtsd_si32(simde_mm_set_sd(v)));
#endif
}

/// div_inline: 0 for a zero divisor, 0 for the signed overflow (INT_MIN / -1), the C quotient otherwise. AArch64
/// udiv/sdiv answer 0 for a zero divisor and never trap. A divisor known at compile time keeps the C expression.
inline uint32_t divwu(uint32_t n, uint32_t d) noexcept {
#if defined(__aarch64__)
  if (!__builtin_constant_p(d)) {
    uint32_t q;
    asm("udiv %w0, %w1, %w2" : "=r"(q) : "r"(n), "r"(d));
    return q;
  }
#endif
  return d ? n / d : 0;
}
inline uint64_t divdu(uint64_t n, uint64_t d) noexcept {
#if defined(__aarch64__)
  if (!__builtin_constant_p(d)) {
    uint64_t q;
    asm("udiv %x0, %x1, %x2" : "=r"(q) : "r"(n), "r"(d));
    return q;
  }
#endif
  return d ? n / d : 0;
}
/// The result as the zero-extended word the old expression assigned to the 64-bit register.
inline uint32_t divw(int32_t n, int32_t d) noexcept {
#if defined(__aarch64__)
  if (!__builtin_constant_p(d)) {
    int32_t q;
    asm("sdiv %w0, %w1, %w2" : "=r"(q) : "r"(n), "r"(d));
    return uint32_t((n == INT32_MIN && d == -1) ? 0 : q);
  }
#endif
  return uint32_t((d && !(n == INT32_MIN && d == -1)) ? n / d : 0);
}
inline int64_t divd(int64_t n, int64_t d) noexcept {
#if defined(__aarch64__)
  if (!__builtin_constant_p(d)) {
    int64_t q;
    asm("sdiv %x0, %x1, %x2" : "=r"(q) : "r"(n), "r"(d));
    return (n == INT64_MIN && d == -1) ? 0 : q;
  }
#endif
  return (d && !(n == INT64_MIN && d == -1)) ? n / d : 0;
}

/// fp_copy_pairs: lfs without a mode, the value double(float) gives with the flush mode off; a denormal single is
/// converted with integer arithmetic, so the function is pure and drops out when the register is not read again.
inline double SingleToDoublePure(uint32_t bits) noexcept {
  const uint32_t fraction = bits & 0x007FFFFFu;
  if ((bits & 0x7F800000u) == 0 && fraction != 0) [[unlikely]] {
    const int shift = __builtin_clz(fraction) - 8;  // 1..23: brings the leading one to bit 23
    const uint64_t exponent = uint64_t(1023 - 126 - shift);
    const uint64_t mantissa = uint64_t((fraction << shift) & 0x007FFFFFu) << 29;
    return std::bit_cast<double>((uint64_t(bits >> 31) << 63) | (exponent << 52) | mantissa);
  }
  return double(std::bit_cast<float>(bits));
}

/// stfs of the value such an lfs loaded: float(double(single)) is the single again, a signalling NaN quieted.
inline uint32_t QuietSingleBits(uint32_t bits) noexcept {
  return (bits & 0x7FFFFFFFu) > 0x7F800000u ? (bits | 0x00400000u) : bits;
}

}  // namespace rex::ppc
#endif  // XMOW_DIET_OPS_H
