// rex::deko3d (from nfsmw-nx): the DKSH cache file. See rex/ui/deko3d/cache_dksh.h.

#include <rex/ui/deko3d/cache_dksh.h>

#include <rex/ui/deko3d/glsl.h>

#include <algorithm>
#include <cstring>
// The same xxHash setup as nfsmw_shader_library.cpp (reads through memcpy: see the note there).
#if defined(XXH_IMPLEM_13a8737387)
#error "xxhash.h ya se ha incluido con su implementacion antes de este punto: XXH_FORCE_MEMORY_ACCESS 0 llegaria tarde"
#endif
#undef XXH_FORCE_MEMORY_ACCESS
#define XXH_FORCE_MEMORY_ACCESS 0
#define XXH_INLINE_ALL
#include <xxhash.h>

namespace rex::deko3d {
namespace {

constexpr uint32_t kMagia = 0x4B44464Eu;  // "NFDK"
constexpr uint32_t kVersion = 2;
constexpr size_t kCabecera = 32;
constexpr size_t kEntrada = 24;
// Gears of War 2 ships ~100,000 variants (10,000+ shaders, nine pixel shader variants each).
constexpr uint32_t kMaxEntradas = 1u << 20;  // Gears of War 2, every shader: ~220,000 variants
constexpr uint32_t kMaxDksh = 1u << 20;

uint32_t U32(const uint8_t* p) {
  uint32_t v;
  std::memcpy(&v, p, 4);
  return v;
}
uint64_t U64(const uint8_t* p) {
  uint64_t v;
  std::memcpy(&v, p, 8);
  return v;
}
void Poner(std::vector<uint8_t>& d, size_t at, const void* v, size_t n) { std::memcpy(d.data() + at, v, n); }

uint64_t HuellaId() {
  const std::string id = IdCompilador();
  return XXH3_64bits(id.data(), id.size());
}

}  // namespace

std::string IdCompilador() {
  return "uam 1.1.0 + SwitchWakerHD 2d147aa (patches 1-7); SPIRV-Cross vulkan-sdk-1.4.363.0; GLSL revision " +
         std::to_string(kRevisionGlsl);
}

uint64_t HuellaSpirv(std::span<const uint32_t> espirv) { return XXH3_64bits(espirv.data(), espirv.size_bytes()); }

bool CacheDksh::Cargar(std::span<const uint8_t> archivo, std::string& motivo) {
  entradas_.clear();
  if (archivo.size() < kCabecera || U32(archivo.data()) != kMagia) {
    motivo = "no es una cache de DKSH";
    return false;
  }
  if (U32(archivo.data() + 4) != kVersion) {
    motivo = "otra version del formato";
    return false;
  }
  if (U64(archivo.data() + 8) != HuellaId()) {
    motivo = "hecha por otro compilador (se espera: " + IdCompilador() + ")";
    return false;
  }
  const uint32_t n = U32(archivo.data() + 16);
  if (n > kMaxEntradas || archivo.size() < kCabecera + size_t(n) * kEntrada ||
      XXH3_64bits(archivo.data() + kCabecera, archivo.size() - kCabecera) != U64(archivo.data() + 24)) {
    motivo = "danada";
    return false;
  }
  size_t datos = kCabecera + size_t(n) * kEntrada;
  for (uint32_t i = 0; i < n; ++i) {
    const uint8_t* e = archivo.data() + kCabecera + size_t(i) * kEntrada;
    const uint32_t bytes = U32(e + 16);
    if (bytes > kMaxDksh || datos + bytes > archivo.size()) {
      entradas_.clear();
      motivo = "danada";
      return false;
    }
    entradas_[{U64(e), U32(e + 8), U32(e + 12)}].assign(archivo.data() + datos, archivo.data() + datos + bytes);
    datos = (datos + bytes + 3) & ~size_t(3);
  }
  return true;
}

std::vector<uint8_t> CacheDksh::Serializar() const {
  size_t total = kCabecera + entradas_.size() * kEntrada;
  for (const auto& [clave, dksh] : entradas_) {
    total = (total + dksh.size() + 3) & ~size_t(3);
  }
  std::vector<uint8_t> d(total, 0);
  const uint32_t cabecera[2] = {kMagia, kVersion};
  Poner(d, 0, cabecera, 8);
  const uint64_t id = HuellaId();
  Poner(d, 8, &id, 8);
  const uint32_t n = uint32_t(entradas_.size());
  Poner(d, 16, &n, 4);
  size_t e = kCabecera;
  size_t datos = kCabecera + entradas_.size() * kEntrada;
  for (const auto& [clave, dksh] : entradas_) {
    const uint32_t bytes = uint32_t(dksh.size());
    Poner(d, e, &clave.huella, 8);
    Poner(d, e + 8, &clave.especializacion, 4);
    Poner(d, e + 12, &clave.variante, 4);
    Poner(d, e + 16, &bytes, 4);
    e += kEntrada;
    if (bytes) {
      Poner(d, datos, dksh.data(), bytes);
    }
    datos = (datos + bytes + 3) & ~size_t(3);
  }
  const uint64_t integridad = XXH3_64bits(d.data() + kCabecera, d.size() - kCabecera);
  Poner(d, 24, &integridad, 8);
  return d;
}

CacheDksh::~CacheDksh() {
  if (archivo_) std::fclose(archivo_);
}

bool CacheDksh::CargarIndice(const std::filesystem::path& ruta, std::string& motivo) {
  std::FILE* f = std::fopen(ruta.string().c_str(), "rb");
  if (!f) {
    motivo = "no se puede abrir";
    return false;
  }
  uint8_t cabecera[kCabecera];
  if (std::fread(cabecera, 1, kCabecera, f) != kCabecera || U32(cabecera) != kMagia) {
    std::fclose(f);
    motivo = "no es una cache de DKSH";
    return false;
  }
  if (U32(cabecera + 4) != kVersion || U64(cabecera + 8) != HuellaId()) {
    std::fclose(f);
    motivo = U32(cabecera + 4) != kVersion ? "otra version del formato"
                                           : "hecha por otro compilador (se espera: " + IdCompilador() + ")";
    return false;
  }
  const uint32_t n = U32(cabecera + 16);
  std::vector<uint8_t> tabla(size_t(n) * kEntrada);
  if (n > kMaxEntradas || std::fread(tabla.data(), 1, tabla.size(), f) != tabla.size()) {
    std::fclose(f);
    motivo = "danada";
    return false;
  }
  std::vector<EnArchivo> indice;
  indice.reserve(n);
  uint64_t datos = kCabecera + uint64_t(n) * kEntrada;
  for (uint32_t i = 0; i < n; ++i) {
    const uint8_t* e = tabla.data() + size_t(i) * kEntrada;
    const uint32_t bytes = U32(e + 16);
    if (bytes > kMaxDksh) {
      std::fclose(f);
      motivo = "danada";
      return false;
    }
    indice.push_back({{U64(e), U32(e + 8), U32(e + 12)}, datos, bytes});
    datos = (datos + bytes + 3) & ~uint64_t(3);
  }
  std::sort(indice.begin(), indice.end(), [](const EnArchivo& a, const EnArchivo& b) { return a.clave < b.clave; });
  std::lock_guard<std::mutex> lock(mutex_);
  if (archivo_) std::fclose(archivo_);
  archivo_ = f;
  ruta_ = ruta;
  indice_ = std::move(indice);
  return true;
}

const std::vector<uint8_t>* CacheDksh::Buscar(const Clave& clave) const {
  std::lock_guard<std::mutex> lock(mutex_);
  if (const auto it = entradas_.find(clave); it != entradas_.end()) {
    return &it->second;
  }
  const auto it = std::lower_bound(indice_.begin(), indice_.end(), clave,
                                   [](const EnArchivo& a, const Clave& c) { return a.clave < c; });
  if (it == indice_.end() || it->clave != clave || !archivo_) {
    return nullptr;
  }
  std::vector<uint8_t> dksh(it->bytes);
  if (std::fseek(archivo_, long(it->posicion), SEEK_SET) != 0 ||
      std::fread(dksh.data(), 1, dksh.size(), archivo_) != dksh.size()) {
    return nullptr;
  }
  return &entradas_.emplace(clave, std::move(dksh)).first->second;
}

void CacheDksh::Anadir(const Clave& clave, std::vector<uint8_t> dksh) {
  std::lock_guard<std::mutex> lock(mutex_);
  entradas_[clave] = std::move(dksh);
}

}  // namespace rex::deko3d
