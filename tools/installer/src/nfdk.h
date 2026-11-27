// xmow_setup - NFDK v2 DKSH cache files (format of sdk-g2 src/ui/deko3d/cache_dksh.cpp): read and sorted write.
// Header 32 bytes: magic "NFDK", version 2, compiler id hash (u64), count (u32), 4 zero bytes, XXH3-64 of everything after
// the header. Then count 24-byte entries (SPIR-V hash u64, specialization u32, variant u32, size u32, 4 zero bytes), then the DKSH data,
// each 4-byte aligned, in entry order. All little-endian. Replaces dksh_lib.write_cache / dksh_fix_0127.py.
// Copyright (C) 2026 xmow-nx contributors. GPL-3.0-or-later (see LICENSE).
#pragma once
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <tuple>
#include <vector>

#define XXH_INLINE_ALL
#include "xxhash.h"  // third_party/xxHash

namespace nfdk {

constexpr uint32_t kMagia = 0x4B44464E, kVersao = 2, kCab = 32, kEnt = 24;

struct Chave {
  uint64_t huella;
  uint32_t espec, variante;
  bool operator<(const Chave& o) const { return std::tie(huella, espec, variante) < std::tie(o.huella, o.espec, o.variante); }
};
using Cache = std::map<Chave, std::vector<uint8_t>>;

inline uint32_t L32(const uint8_t* p) { uint32_t v; std::memcpy(&v, p, 4); return v; }
inline uint64_t L64(const uint8_t* p) { uint64_t v; std::memcpy(&v, p, 8); return v; }

// Parses a whole cache file in memory. Checks magic, version, bounds and the XXH3. Returns "" or the error.
inline std::string Ler(const std::vector<uint8_t>& d, uint64_t& id, Cache* entradas) {
  if (d.size() < kCab || L32(&d[0]) != kMagia || L32(&d[4]) != kVersao) return "not an NFDK v2 cache";
  id = L64(&d[8]);
  const uint32_t n = L32(&d[16]);
  if (XXH3_64bits(d.data() + kCab, d.size() - kCab) != L64(&d[24])) return "XXH3 of the cache does not match";
  if (kCab + uint64_t(n) * kEnt > d.size()) return "entry table out of range";
  if (!entradas) return "";
  uint64_t pos = kCab + uint64_t(n) * kEnt;
  for (uint32_t i = 0; i < n; ++i) {
    const uint8_t* e = &d[kCab + size_t(i) * kEnt];
    const uint32_t tam = L32(e + 16);
    if (pos + tam > d.size()) return "entry data out of range";
    (*entradas)[Chave{L64(e), L32(e + 8), L32(e + 12)}] = std::vector<uint8_t>(d.begin() + pos, d.begin() + pos + tam);
    pos = (pos + tam + 3) & ~uint64_t(3);
  }
  return "";
}

inline std::vector<uint8_t> Montar(uint64_t id, const Cache& c) {  // std::map: written sorted by key
  uint64_t total = kCab + c.size() * kEnt;
  for (const auto& kv : c) total = (total + kv.second.size() + 3) & ~uint64_t(3);
  std::vector<uint8_t> out(total, 0);
  auto p32 = [&](size_t o, uint32_t v) { std::memcpy(&out[o], &v, 4); };
  auto p64 = [&](size_t o, uint64_t v) { std::memcpy(&out[o], &v, 8); };
  p32(0, kMagia); p32(4, kVersao); p64(8, id); p32(16, uint32_t(c.size()));
  size_t e = kCab, d = kCab + c.size() * kEnt;
  for (const auto& kv : c) {
    p64(e, kv.first.huella); p32(e + 8, kv.first.espec); p32(e + 12, kv.first.variante);
    p32(e + 16, uint32_t(kv.second.size()));  // bytes 20..23 stay zero
    if (!kv.second.empty()) std::memcpy(&out[d], kv.second.data(), kv.second.size());
    e += kEnt;
    d = (d + kv.second.size() + 3) & ~size_t(3);
  }
  p64(24, XXH3_64bits(out.data() + kCab, out.size() - kCab));
  return out;
}

}  // namespace nfdk
