// xmow_setup - XDVDFS reader (port of extract_iso.py) and XEX2 image loader.
// Copyright (C) 2026 xmow-nx contributors. GPL-3.0-or-later (see LICENSE).
#include "disco.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <set>

#include "cripto.h"

namespace disco {
namespace {
constexpr uint64_t kSetor = 2048;
const char kMagia[] = "MICROSOFT*XBOX*MEDIA";  // 20 bytes
constexpr size_t kMagiaN = 20;
constexpr uint32_t kMaxPasta = 256u << 20;

uint32_t le32(const uint8_t* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }
uint16_t le16(const uint8_t* p) { return uint16_t(p[0] | p[1] << 8); }
uint32_t be32(const uint8_t* p) { return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3]; }
uint16_t be16(const uint8_t* p) { return uint16_t(p[0] << 8 | p[1]); }
std::string Minusc(std::string s) {
  for (char& c : s) c = char(std::tolower((unsigned char)c));
  return s;
}
}  // namespace

Iso::~Iso() {
  if (f_) std::fclose(f_);
}

bool Iso::LerEm(uint64_t off, void* buf, size_t n) {
  if (_fseeki64(f_, int64_t(off), SEEK_SET) != 0) return false;
  return std::fread(buf, 1, n, f_) == n;
}

bool Iso::Abrir(const std::filesystem::path& p, std::string& erro) {
  f_ = _wfopen(p.wstring().c_str(), L"rb");
  if (!f_) { erro = "cannot open the image"; return false; }
  auto magia_em = [&](uint64_t base) {
    char b[kMagiaN];
    return LerEm(base + 32 * kSetor, b, kMagiaN) && std::memcmp(b, kMagia, kMagiaN) == 0;
  };
  const struct { uint64_t base; const char* tipo; } conhecidas[] = {
      {0x00000000, "raw partition / trimmed image"}, {0x0FD90000, "XGD2"}, {0x02080000, "XGD3"}, {0x18300000, "XGD1"}};
  bool achou = false;
  for (const auto& k : conhecidas)
    if (magia_em(k.base)) { base_ = k.base; tipo_ = k.tipo; achou = true; break; }
  if (!achou) {  // scan the first GiB, sector aligned
    _fseeki64(f_, 0, SEEK_END);
    const uint64_t lim = std::min<uint64_t>(uint64_t(_ftelli64(f_)), 1ull << 30);
    std::vector<char> b(kSetor);
    for (uint64_t pos = 32 * kSetor; pos < lim && !achou; pos += kSetor)
      if (LerEm(pos, b.data(), kMagiaN) && std::memcmp(b.data(), kMagia, kMagiaN) == 0 && magia_em(pos - 32 * kSetor)) {
        base_ = pos - 32 * kSetor; tipo_ = "found by scanning (non-standard offset)"; achou = true;
      }
  }
  if (!achou) { erro = "no XDVDFS file system in the image (compressed CCI/GOD/ZAR images must be converted to .iso)"; return false; }
  uint8_t vd[kSetor];
  if (!LerEm(base_ + 32 * kSetor, vd, kSetor)) { erro = "volume descriptor unreadable"; return false; }
  Andar(le32(vd + 0x14), le32(vd + 0x18), "", 0);
  if (entradas_.empty()) { erro = "empty file system"; return false; }
  return true;
}

void Iso::Andar(uint32_t setor, uint32_t tamanho, const std::string& prefixo, int nivel) {
  if (tamanho == 0 || tamanho > kMaxPasta || nivel > 64) return;
  std::vector<uint8_t> t(tamanho);
  if (!LerEm(base_ + setor * kSetor, t.data(), tamanho)) return;
  std::vector<Entrada> aqui;
  std::set<uint32_t> vistos;
  std::vector<uint32_t> pilha{0};
  while (!pilha.empty()) {
    const uint32_t off = pilha.back();
    pilha.pop_back();
    if (!vistos.insert(off).second || off + 14 > tamanho) continue;
    const uint8_t* e = t.data() + off;
    for (uint16_t filho : {le16(e), le16(e + 2)})
      if (filho != 0 && filho != 0xFFFF) pilha.push_back(uint32_t(filho) * 4);
    const uint32_t nl = e[13];
    if (nl == 0 || off + 14 + nl > tamanho) continue;
    Entrada x;
    const std::string nome(reinterpret_cast<const char*>(e + 14), nl);
    x.caminho = prefixo.empty() ? nome : prefixo + "/" + nome;
    x.setor = le32(e + 4);
    x.tamanho = le32(e + 8);
    x.pasta = (e[12] & 0x10) != 0;
    aqui.push_back(x);
  }
  std::sort(aqui.begin(), aqui.end(), [](const Entrada& a, const Entrada& b) { return Minusc(a.caminho) < Minusc(b.caminho); });
  for (const auto& x : aqui) {
    entradas_.push_back(x);
    if (x.pasta) Andar(x.setor, x.tamanho, x.caminho, nivel + 1);
  }
}

const Entrada* Iso::Achar(const std::string& caminho) const {
  const std::string c = Minusc(caminho);
  for (const auto& e : entradas_)
    if (Minusc(e.caminho) == c) return &e;
  return nullptr;
}

bool Iso::Ler(const Entrada& e, std::vector<uint8_t>& out, std::string& erro) {
  out.resize(e.tamanho);
  if (e.tamanho && !LerEm(base_ + e.setor * kSetor, out.data(), e.tamanho)) {
    erro = "unexpected end of image reading " + e.caminho + " (incomplete image?)";
    return false;
  }
  return true;
}

bool Iso::Extrair(const Entrada& e, const std::filesystem::path& dest, std::string& erro) {
  for (const auto& parte : std::filesystem::path(e.caminho))  // refuse names that would leave the destination
    if (parte == ".." || parte == "." || parte.has_root_name() || parte.has_root_directory()) {
      erro = "unsafe file name in the image: " + e.caminho;
      return false;
    }
  std::filesystem::create_directories(dest.parent_path());
  FILE* o = _wfopen(dest.wstring().c_str(), L"wb");
  if (!o) { erro = "cannot write " + dest.string(); return false; }
  std::vector<uint8_t> b(4u << 20);
  uint64_t pos = base_ + e.setor * kSetor, resta = e.tamanho;
  bool ok = true;
  while (resta && ok) {
    const size_t n = size_t(std::min<uint64_t>(resta, b.size()));
    ok = LerEm(pos, b.data(), n) && std::fwrite(b.data(), 1, n, o) == n;
    pos += n; resta -= n;
  }
  if (std::fclose(o) != 0) ok = false;
  if (!ok) erro = "error extracting " + e.caminho;
  return ok;
}

bool XexImagem(const std::vector<uint8_t>& x, std::vector<uint8_t>& img, std::string& info, std::string& erro) {
  if (x.size() < 24 || std::memcmp(x.data(), "XEX2", 4) != 0) { erro = "default.xex is not XEX2"; return false; }
  const uint32_t pe = be32(&x[8]), seg = be32(&x[16]), n = be32(&x[20]);
  if (pe > x.size() || seg + 0x160 > x.size() || 24 + uint64_t(n) * 8 > x.size()) { erro = "XEX2 header out of range"; return false; }
  uint32_t ffi = 0;
  for (uint32_t i = 0; i < n; ++i)
    if (be32(&x[24 + 8 * i]) == 0x3FF) ffi = be32(&x[28 + 8 * i]);
  if (!ffi || ffi + 8 > x.size()) { erro = "XEX2 without file format info"; return false; }
  const uint32_t ffi_n = be32(&x[ffi]);
  const uint16_t cifra = be16(&x[ffi + 4]), comp = be16(&x[ffi + 6]);
  const uint32_t tam_imagem = be32(&x[seg + 4]);
  info = "encryption " + std::to_string(cifra) + ", compression " + std::to_string(comp) + ", image " + std::to_string(tam_imagem) + " B";
  std::vector<uint8_t> carga(x.begin() + pe, x.end());
  if (cifra == 1) {
    static const uint8_t kVarejo[16] = {0x20, 0xB1, 0x85, 0xA5, 0x9D, 0x28, 0xFD, 0xC3,
                                        0x40, 0x58, 0x3F, 0xBB, 0x08, 0x96, 0xBF, 0x91};
    static const uint8_t kDev[16] = {0};
    bool ok = false;
    for (const uint8_t* chave : {kVarejo, kDev}) {
      uint8_t sess[16];
      cripto::Aes128Dec(chave).DecryptBlock(&x[seg + 0x150], sess);
      cripto::Aes128Dec aes(sess);
      std::vector<uint8_t> tentativa(carga);
      uint8_t iv[16] = {0};
      // decrypt the first block only to check for "MZ"; then the whole payload
      std::vector<uint8_t> prim(tentativa.begin(), tentativa.begin() + std::min<size_t>(16, tentativa.size()));
      uint8_t iv0[16] = {0};
      aes.DecryptCbc(prim.data(), prim.size(), iv0);
      if (prim.size() == 16 && prim[0] == 'M' && prim[1] == 'Z') {
        aes.DecryptCbc(tentativa.data(), tentativa.size() & ~size_t(15), iv);
        carga.swap(tentativa);
        info += chave == kVarejo ? ", retail key" : ", devkit key";
        ok = true;
        break;
      }
    }
    if (!ok) { erro = "XEX2 payload does not decrypt with the retail or devkit key"; return false; }
  } else if (cifra != 0) {
    erro = "unknown XEX2 encryption " + std::to_string(cifra);
    return false;
  }
  img.clear();
  if (comp == 1) {
    size_t p = 0;
    for (uint32_t o = 8; o + 8 <= ffi_n; o += 8) {
      const uint32_t dados = be32(&x[ffi + o]), zeros = be32(&x[ffi + o + 4]);
      if (p + dados > carga.size()) { erro = "XEX2 basic compression block out of range"; return false; }
      img.insert(img.end(), carga.begin() + p, carga.begin() + p + dados);
      img.insert(img.end(), zeros, 0);
      p += dados;
    }
  } else if (comp == 0) {
    img = carga;
  } else {
    erro = "XEX2 compression " + std::to_string(comp) + " not supported (only none/basic)";
    return false;
  }
  if (img.size() < 2 || img[0] != 'M' || img[1] != 'Z') { erro = "decoded image does not start with MZ"; return false; }
  return true;
}

}  // namespace disco
