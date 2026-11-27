// xmow_setup - SHA-256 and AES-128 (decryption only), written for this tool (FIPS 180-4 / FIPS 197).
// Copyright (C) 2026 xmow-nx contributors. GPL-3.0-or-later (see LICENSE).
#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <string>

namespace cripto {

class Sha256 {
 public:
  Sha256() { Reset(); }
  void Reset() {
    static const uint32_t k0[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                   0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    std::memcpy(h_, k0, sizeof(h_));
    len_ = 0;
    used_ = 0;
  }
  void Update(const void* data, size_t n) {
    const uint8_t* p = static_cast<const uint8_t*>(data);
    len_ += n;
    while (n) {
      const size_t t = std::min<size_t>(64 - used_, n);
      std::memcpy(buf_ + used_, p, t);
      used_ += t; p += t; n -= t;
      if (used_ == 64) { Block(buf_); used_ = 0; }
    }
  }
  std::array<uint8_t, 32> Final() {
    const uint64_t bits = len_ * 8;
    const uint8_t pad = 0x80, zero = 0;
    Update(&pad, 1);
    while (used_ != 56) Update(&zero, 1);
    uint8_t l[8];
    for (int i = 0; i < 8; ++i) l[i] = uint8_t(bits >> (56 - 8 * i));
    Update(l, 8);
    std::array<uint8_t, 32> out;
    for (int i = 0; i < 8; ++i)
      for (int j = 0; j < 4; ++j) out[4 * i + j] = uint8_t(h_[i] >> (24 - 8 * j));
    return out;
  }
  static std::string Hex(const std::array<uint8_t, 32>& d) {
    static const char* x = "0123456789abcdef";
    std::string s;
    for (uint8_t b : d) { s += x[b >> 4]; s += x[b & 15]; }
    return s;
  }
  static std::string Of(const void* data, size_t n) {
    Sha256 s;
    s.Update(data, n);
    return Hex(s.Final());
  }

 private:
  static uint32_t R(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }
  void Block(const uint8_t* b) {
    static const uint32_t k[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
        0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
        0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
        0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
        0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
    uint32_t w[64];
    for (int i = 0; i < 16; ++i) w[i] = uint32_t(b[4 * i]) << 24 | uint32_t(b[4 * i + 1]) << 16 | uint32_t(b[4 * i + 2]) << 8 | b[4 * i + 3];
    for (int i = 16; i < 64; ++i) {
      const uint32_t s0 = R(w[i - 15], 7) ^ R(w[i - 15], 18) ^ (w[i - 15] >> 3);
      const uint32_t s1 = R(w[i - 2], 17) ^ R(w[i - 2], 19) ^ (w[i - 2] >> 10);
      w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = h_[0], bb = h_[1], c = h_[2], d = h_[3], e = h_[4], f = h_[5], g = h_[6], h = h_[7];
    for (int i = 0; i < 64; ++i) {
      const uint32_t t1 = h + (R(e, 6) ^ R(e, 11) ^ R(e, 25)) + ((e & f) ^ (~e & g)) + k[i] + w[i];
      const uint32_t t2 = (R(a, 2) ^ R(a, 13) ^ R(a, 22)) + ((a & bb) ^ (a & c) ^ (bb & c));
      h = g; g = f; f = e; e = d + t1; d = c; c = bb; bb = a; a = t1 + t2;
    }
    h_[0] += a; h_[1] += bb; h_[2] += c; h_[3] += d; h_[4] += e; h_[5] += f; h_[6] += g; h_[7] += h;
  }
  uint32_t h_[8];
  uint64_t len_;
  uint8_t buf_[64];
  size_t used_;
};

// AES-128 decryption (ECB block + CBC), tables built at start from GF(2^8).
class Aes128Dec {
 public:
  explicit Aes128Dec(const uint8_t key[16]) {
    Tables();
    uint8_t* rk = rk_;
    std::memcpy(rk, key, 16);
    uint8_t rcon = 1;
    for (int i = 16; i < 176; i += 4) {
      uint8_t t[4] = {rk[i - 4], rk[i - 3], rk[i - 2], rk[i - 1]};
      if (i % 16 == 0) {
        const uint8_t u = t[0];
        t[0] = uint8_t(sbox()[t[1]] ^ rcon); t[1] = sbox()[t[2]]; t[2] = sbox()[t[3]]; t[3] = sbox()[u];
        rcon = Xt(rcon);
      }
      for (int j = 0; j < 4; ++j) rk[i + j] = rk[i - 16 + j] ^ t[j];
    }
  }
  void DecryptBlock(const uint8_t in[16], uint8_t out[16]) const {
    uint8_t s[16];
    for (int i = 0; i < 16; ++i) s[i] = in[i] ^ rk_[160 + i];
    for (int r = 9; r >= 0; --r) {
      uint8_t t[16];
      for (int c = 0; c < 4; ++c)          // inverse shift rows + inverse sub bytes
        for (int row = 0; row < 4; ++row) t[4 * c + row] = inv()[s[4 * ((c - row + 4) & 3) + row]];
      for (int i = 0; i < 16; ++i) t[i] ^= rk_[16 * r + i];
      if (r) {
        for (int c = 0; c < 4; ++c) {
          const uint8_t a0 = t[4 * c], a1 = t[4 * c + 1], a2 = t[4 * c + 2], a3 = t[4 * c + 3];
          s[4 * c] = M(a0, 14) ^ M(a1, 11) ^ M(a2, 13) ^ M(a3, 9);
          s[4 * c + 1] = M(a0, 9) ^ M(a1, 14) ^ M(a2, 11) ^ M(a3, 13);
          s[4 * c + 2] = M(a0, 13) ^ M(a1, 9) ^ M(a2, 14) ^ M(a3, 11);
          s[4 * c + 3] = M(a0, 11) ^ M(a1, 13) ^ M(a2, 9) ^ M(a3, 14);
        }
      } else {
        std::memcpy(s, t, 16);
      }
    }
    std::memcpy(out, s, 16);
  }
  // CBC in place; iv is updated so the call can be chained over consecutive chunks.
  void DecryptCbc(uint8_t* data, size_t n, uint8_t iv[16]) const {
    for (size_t o = 0; o + 16 <= n; o += 16) {
      uint8_t c[16], p[16];
      std::memcpy(c, data + o, 16);
      DecryptBlock(c, p);
      for (int i = 0; i < 16; ++i) data[o + i] = p[i] ^ iv[i];
      std::memcpy(iv, c, 16);
    }
  }
  static bool SelfTest() {  // FIPS-197 appendix C.1
    uint8_t key[16], ct[16] = {0x69, 0xc4, 0xe0, 0xd8, 0x6a, 0x7b, 0x04, 0x30, 0xd8, 0xcd, 0xb7, 0x80, 0x70, 0xb4, 0xc5, 0x5a}, pt[16];
    for (int i = 0; i < 16; ++i) key[i] = uint8_t(i);
    Aes128Dec(key).DecryptBlock(ct, pt);
    for (int i = 0; i < 16; ++i)
      if (pt[i] != uint8_t(i * 0x11)) return false;
    return true;
  }

 private:
  static uint8_t Xt(uint8_t x) { return uint8_t((x << 1) ^ ((x & 0x80) ? 0x1B : 0)); }
  static uint8_t M(uint8_t a, uint8_t b) {
    uint8_t r = 0;
    while (b) { if (b & 1) r ^= a; a = Xt(a); b >>= 1; }
    return r;
  }
  static uint8_t* sbox() { static uint8_t t[256]; return t; }
  static uint8_t* inv() { static uint8_t t[256]; return t; }
  static void Tables() {
    static bool done = false;
    if (done) return;
    auto rotl = [](uint8_t x, int s) { return uint8_t((x << s) | (x >> (8 - s))); };
    uint8_t p = 1, q = 1;
    do {
      p = uint8_t(p ^ (p << 1) ^ ((p & 0x80) ? 0x1B : 0));
      q ^= uint8_t(q << 1); q ^= uint8_t(q << 2); q ^= uint8_t(q << 4);
      if (q & 0x80) q ^= 0x09;
      sbox()[p] = uint8_t(q ^ rotl(q, 1) ^ rotl(q, 2) ^ rotl(q, 3) ^ rotl(q, 4) ^ 0x63);
    } while (p != 1);
    sbox()[0] = 0x63;
    for (int i = 0; i < 256; ++i) inv()[sbox()[i]] = uint8_t(i);
    done = true;
  }
  uint8_t rk_[176];
};

}  // namespace cripto
