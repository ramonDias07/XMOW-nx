// ds_validar: reads xmow_shaders.dslib and shadercache_dksh.bin the way the console runtime does (XMOW; not part of the
// gears2 sources). Library: ds::BibliotecaShaders::CargarIndice + LeerSpirv (gow2_shaders.cpp, gow2_shaders_por_demanda)
// and the whole-file Cargar (integrity hash); each container is also parsed with the bounds checks of
// gow2_shaders.cpp Leer(). Cache: rex::deko3d::CacheDksh::CargarIndice (patch 0045) and the whole-file Cargar.
// Usage: ds_validar <xmow_shaders.dslib> <shadercache_dksh.bin> [informe.tsv]
#include "../XenosRecomp/shader_common.h"
#include <rex/ui/deko3d/cache_dksh.h>
#include <rex/ui/deko3d/glsl.h>
#include "../ds_shader_library.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace fs = std::filesystem;
namespace {
std::vector<uint8_t> LeerTodo(const fs::path& p) {
  std::ifstream f(p, std::ios::binary | std::ios::ate);
  std::vector<uint8_t> d(size_t(f.tellg()));
  f.seekg(0);
  f.read(reinterpret_cast<char*>(d.data()), d.size());
  return d;
}
uint32_t BE(const std::vector<uint8_t>& o, size_t p) {
  return uint32_t(o[p]) << 24 | uint32_t(o[p + 1]) << 16 | uint32_t(o[p + 2]) << 8 | o[p + 3];
}
bool Hay(const std::vector<uint8_t>& o, size_t p, size_t n) { return p <= o.size() && n <= o.size() - p; }

// gow2_shaders.cpp Leer(): the reason it cannot read the container, or nullptr.
const char* LeerContenedor(const ds::Shader& s) {
  const auto& o = s.original;
  if (!Hay(o, 0, 36)) return "contenedor demasiado corto";
  const uint32_t virtuales = BE(o, 4), tabla = BE(o, 16), cabecera = BE(o, 24);
  if (size_t(cabecera) + (s.vertices ? 0x24 : 0x20) > virtuales) return "cabecera fuera de la parte virtual";
  const uint32_t desplazamiento = BE(o, cabecera), bytes = BE(o, cabecera + 4);
  if (!bytes || (bytes % 4) || !Hay(o, size_t(virtuales) + desplazamiento, bytes)) return "microcodigo fuera del contenedor";
  std::vector<uint32_t> micro(bytes / 4);
  for (size_t i = 0; i < micro.size(); ++i) micro[i] = BE(o, size_t(virtuales) + desplazamiento + i * 4);
  if (s.vertices) {
    const uint32_t previos = BE(o, cabecera + 0x18), cantidad = BE(o, cabecera + 0x1C);
    const size_t comienzo = size_t(cabecera) + 0x24 + size_t(previos) * 4;
    if (cantidad > 64 || previos > 1024 || comienzo + size_t(cantidad) * 4 > virtuales) return "elementos de vertices fuera de la parte virtual";
    for (uint32_t i = 0; i < cantidad; ++i) {
      const uint32_t valor = BE(o, comienzo + size_t(i) * 4);
      if ((size_t(valor & 0xFFF) + 1) * 3 > micro.size()) return "elemento de vertices apunta fuera del microcodigo";
    }
  }
  if (!tabla) return nullptr;  // UE3: no constant table
  if (!Hay(o, size_t(tabla) + 4, 28)) return "tabla de constantes fuera";
  return nullptr;
}

// gow2_shaders.cpp ManualesR0DesdeSpirv.
uint32_t ManualesR0(const std::vector<uint32_t>& spirv) {
  uint32_t id = UINT32_MAX;
  for (size_t i = 5; i < spirv.size();) {
    const uint32_t palabras = spirv[i] >> 16, op = spirv[i] & 0xFFFF;
    if (!palabras || i + palabras > spirv.size()) break;
    if (op == 71 && palabras >= 4 && spirv[i + 2] == 1 && spirv[i + 3] == 1) id = spirv[i + 1];
    if (op == 50 && palabras >= 4 && spirv[i + 2] == id) return spirv[i + 3];
    i += palabras;
  }
  return 0;
}
}  // namespace

int main(int argc, char** argv) {
  if (argc < 3) {
    std::fprintf(stderr, "uso: ds_validar <xmow_shaders.dslib> <shadercache_dksh.bin> [informe.tsv]\n");
    return 1;
  }
  const fs::path dslib = argv[1], cache_ruta = argv[2];
  int errores = 0;
  auto fallo = [&](const std::string& m) { std::printf("FALLO: %s\n", m.c_str()); ++errores; };
  constexpr uint64_t kFat32 = 4294967295ull;
  const uint64_t tam_lib = fs::file_size(dslib), tam_cache = fs::file_size(cache_ruta);
  std::printf("tamanos: %s %llu bytes, %s %llu bytes (FAT32 maximo %llu): %s\n", dslib.filename().string().c_str(),
              (unsigned long long)tam_lib, cache_ruta.filename().string().c_str(), (unsigned long long)tam_cache,
              (unsigned long long)kFat32, tam_lib <= kFat32 && tam_cache <= kFat32 ? "caben en FAT32" : "NO CABEN");
  if (tam_lib > kFat32 || tam_cache > kFat32) fallo("archivo mayor que FAT32");
  if (tam_lib > (size_t(2) << 30)) fallo("la biblioteca pasa el limite de 2 GiB del lector (kMaxArchivo)");

  // --- library: the runtime path (index + SPIR-V on demand)
  ds::BibliotecaShaders indice;
  auto t0 = std::chrono::steady_clock::now();
  try {
    indice.CargarIndice(dslib);
  } catch (const std::exception& e) {
    std::printf("CargarIndice: %s\n", e.what());
    return 2;
  }
  std::printf("biblioteca CargarIndice: %zu shaders, %.2f s\n", indice.shaders().size(),
              std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
  size_t vs = 0, ps = 0, sin_spirv = 0, con_manual = 0;
  std::map<std::string, size_t> motivos;
  std::vector<uint64_t> huellas_spirv(indice.shaders().size());
  std::vector<uint32_t> mascaras(indice.shaders().size());
  t0 = std::chrono::steady_clock::now();
  for (size_t i = 0; i < indice.shaders().size(); ++i) {
    const ds::Shader& s = indice.shaders()[i];
    (s.vertices ? vs : ps)++;
    if (const char* m = LeerContenedor(s)) ++motivos[m];
    const std::vector<uint32_t> spirv = indice.LeerSpirv(i);
    if (spirv.empty()) {
      ++sin_spirv;
      continue;
    }
    huellas_spirv[i] = rex::deko3d::HuellaSpirv(spirv);
    mascaras[i] = rex::deko3d::MascaraEspecializacion(spirv);
    if (s.vertices && ManualesR0(spirv)) ++con_manual;
  }
  std::printf("biblioteca: %zu vertex, %zu pixel; LeerSpirv de todos: %.2f s, %zu ilegibles\n", vs, ps,
              std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count(), sin_spirv);
  if (sin_spirv) fallo("SPIR-V ilegible");
  for (const auto& [m, n] : motivos) {
    std::printf("contenedor que Leer() del runtime rechazaria: %zu x %s\n", n, m.c_str());
    ++errores;
  }
  std::printf("vertex shaders con g_ManualesR0 (spec constant 1 != 0): %zu\n", con_manual);

  // --- library: the whole-file load (integrity hash) agrees with the index
  try {
    ds::BibliotecaShaders completa;
    completa.Cargar(dslib);
    std::printf("biblioteca Cargar completa (hash de integridad): %zu shaders\n", completa.shaders().size());
    if (completa.shaders().size() != indice.shaders().size()) fallo("Cargar y CargarIndice no coinciden");
    size_t no_encontrados = 0;
    for (const ds::Shader& s : completa.shaders())
      if (!completa.Buscar(s.original)) ++no_encontrados;
    for (size_t i = 0; i < completa.shaders().size() && i < indice.shaders().size(); ++i)
      if (completa.shaders()[i].original != indice.shaders()[i].original) { fallo("orden distinto"); break; }
    std::printf("Buscar(contenedor) de todos: %zu no encontrados\n", no_encontrados);
    if (no_encontrados) fallo("contenedor no encontrado");
  } catch (const std::exception& e) {
    fallo(std::string("Cargar completa: ") + e.what());
  }

  // --- cache
  rex::deko3d::CacheDksh cache_indice;
  std::string motivo;
  t0 = std::chrono::steady_clock::now();
  if (!cache_indice.CargarIndice(cache_ruta, motivo)) {
    fallo("cache CargarIndice: " + motivo);
    return 2;
  }
  std::printf("cache CargarIndice: %zu entradas, %.2f s (id del compilador: %s)\n", cache_indice.size(),
              std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count(),
              rex::deko3d::IdCompilador().c_str());
  const std::vector<uint8_t> bruto = LeerTodo(cache_ruta);
  rex::deko3d::CacheDksh completa_cache;
  if (!completa_cache.Cargar(bruto, motivo)) {
    fallo("cache Cargar completa: " + motivo);
    return 2;
  }
  std::printf("cache Cargar completa (hash de integridad): %zu entradas\n", completa_cache.size());
  if (completa_cache.size() != cache_indice.size()) fallo("Cargar y CargarIndice de la cache no coinciden");

  std::set<uint64_t> huellas(huellas_spirv.begin(), huellas_spirv.end());
  size_t magia_mala = 0, huerfanas = 0, bytes_dksh = 0;
  std::map<uint64_t, size_t> por_huella;
  for (const auto& [clave, dksh] : completa_cache.entradas()) {
    bytes_dksh += dksh.size();
    if (dksh.size() < 16 || std::memcmp(dksh.data(), "DKSH", 4) != 0) ++magia_mala;
    if (!huellas.count(clave.huella)) ++huerfanas;
    ++por_huella[clave.huella];
  }
  std::printf("cache: %zu bytes de DKSH, %zu sin cabecera DKSH, %zu entradas cuya huella no esta en la biblioteca\n",
              bytes_dksh, magia_mala, huerfanas);
  if (magia_mala) fallo("entradas sin magia DKSH");
  if (huerfanas) fallo("entradas huerfanas");

  // The renderer's own keys for the usual pipelines (ds_dksh's kBase and 0 values): a vertex shader with every
  // interpolator, a pixel shader without early depth.
  constexpr uint32_t kBase = SPEC_CONSTANT_CONSTANTES_UBO | SPEC_CONSTANT_INV_TAMANO_TEX;
  size_t sin_dksh = 0, con_dksh = 0, difiere = 0, sin_base = 0;
  std::vector<std::string> lista_sin;
  for (size_t i = 0; i < indice.shaders().size(); ++i) {
    const ds::Shader& s = indice.shaders()[i];
    if (!por_huella.count(huellas_spirv[i])) {
      ++sin_dksh;
      lista_sin.push_back(std::string(s.vertices ? "vs" : "ps") + "\t" + std::to_string(i));
      continue;
    }
    ++con_dksh;
    for (const uint32_t base : {kBase, 0u}) {
      const rex::deko3d::CacheDksh::Clave clave{huellas_spirv[i], base & mascaras[i], s.vertices ? ~0u : 0u};
      const auto* a = cache_indice.Buscar(clave);
      const auto* b = completa_cache.Buscar(clave);
      if (!a || !b) { ++sin_base; continue; }
      if (*a != *b) ++difiere;
    }
  }
  std::printf("shaders con al menos un DKSH: %zu; sin ninguno: %zu; claves base ausentes: %zu; bytes que difieren entre lectura por indice y completa: %zu\n",
              con_dksh, sin_dksh, sin_base, difiere);
  if (difiere) fallo("lectura por indice y completa difieren");

  // xmow 1028: the runtime's pixel shader variants (dibujos.cpp: early Z 0x001, 7e3 clamp of render target 0 with
  // revision 2 = 0x208, both = 0x209) at specialization 0, under the key the renderer builds in ShaderDe:
  // {HuellaSpirv(library SPIR-V), especializacion & MascaraEspecializacion, variante_ps}. Read back through the
  // file index like the console (CacheDksh::CargarIndice + Buscar).
  {
    const uint32_t kVariantesPs[] = {0x001, 0x208, 0x209};
    size_t presentes[3] = {}, total_ps = 0;
    for (size_t i = 0; i < indice.shaders().size(); ++i) {
      if (indice.shaders()[i].vertices) continue;
      ++total_ps;
      for (size_t v = 0; v < 3; ++v)
        if (cache_indice.Buscar({huellas_spirv[i], 0u & mascaras[i], kVariantesPs[v]})) ++presentes[v];
    }
    std::printf("variantes de pixel shader del runtime (especializacion 0): 0x001 %zu/%zu, 0x208 %zu/%zu, 0x209 %zu/%zu\n",
                presentes[0], total_ps, presentes[1], total_ps, presentes[2], total_ps);
    // Shaders that missed in console test 29 (0.1.22, helicopter): their 0.1.23 keys, as the renderer logs them.
    for (const size_t n : {size_t(9631), size_t(10074), size_t(12441), size_t(6770), size_t(4517)}) {
      if (n >= indice.shaders().size() || indice.shaders()[n].vertices) continue;
      for (const uint32_t v : kVariantesPs) {
        const bool hay = cache_indice.Buscar({huellas_spirv[n], 0u & mascaras[n], v}) != nullptr;
        std::printf("  PS n%zu 00000000 %08X (huella %016llX): %s\n", n, v, (unsigned long long)huellas_spirv[n],
                    hay ? "en la cache" : "AUSENTE");
      }
    }
  }
  if (argc > 3) {
    std::ofstream t(argv[3]);
    t << "tipo\tindice_en_biblioteca\n";
    for (const auto& l : lista_sin) t << l << "\n";
  }
  std::printf(errores ? "RESULTADO: %d problemas\n" : "RESULTADO: valido (%d problemas)\n", errores);
  return errores ? 2 : 0;
}
