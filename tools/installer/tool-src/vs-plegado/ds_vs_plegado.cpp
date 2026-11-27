// ds_vs_plegado (XMOW 0.1.33): vertex shader DKSH with the input fix-ups folded (rex::deko3d::PlegarEntradaVs), through
// the console's path: SPIR-V of the library -> SpirvAGlsl -> PlegarEntradaVs -> uam.
//
//   ds_vs_plegado <library.dslib> <containers folder> <keys.txt> <output folder> [--parte K N] [--glsl DIR]
//                 [--sin-plegar]
//
// keys.txt: one "<container name> <specialization hex> <output mask hex>" per line (the unfolded cache key of a vertex
// shader variant, by the name of its container file without .bin). Lines i with i % N == K are done (--parte).
// Each DKSH goes to <output>/<SPIR-V fingerprint>_<specialization of the key>_<mask>.dksh: the specialization has
// kEspecVsEntradaPlegada set unless --sin-plegar (then the DKSH must be the one the shipped cache has: the tool check).
// A report line per key goes to <output>/informe_K.tsv:
//   name fingerprint specialization mask ok folded_reads bytes ms reason
// A variant whose fold replaces nothing (folded_reads 0) or is refused is reported and not written.
#include <rex/ui/deko3d/glsl.h>
#include "ds_shader_library.h"
#include "uam_api.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

#define XXH_INLINE_ALL
#include <xxhash.h>

namespace fs = std::filesystem;

namespace {

std::vector<uint8_t> Leer(const fs::path& ruta) {
  std::ifstream f(ruta, std::ios::binary | std::ios::ate);
  if (!f || f.tellg() < 0) throw std::runtime_error("unreadable: " + ruta.string());
  std::vector<uint8_t> d(static_cast<size_t>(f.tellg()));
  f.seekg(0);
  if (!d.empty() && !f.read(reinterpret_cast<char*>(d.data()), d.size())) throw std::runtime_error("short read");
  return d;
}

void Escribir(const fs::path& ruta, const void* p, size_t n) {
  std::ofstream f(ruta, std::ios::binary);
  if (!f.write(static_cast<const char*>(p), n) || !f.flush()) throw std::runtime_error("write: " + ruta.string());
}

std::string UnaLinea(std::string s) {
  for (char& c : s)
    if (c == '\n' || c == '\r' || c == '\t') c = ' ';
  while (!s.empty() && s.back() == ' ') s.pop_back();
  return s;
}

}  // namespace

int main(int argc, char** argv) try {
  if (argc < 5) {
    std::printf("usage: ds_vs_plegado <library.dslib> <containers> <keys.txt> <output> [--parte K N] [--glsl DIR] "
                "[--sin-plegar]\n");
    return 1;
  }
  const fs::path biblioteca_ruta = argv[1], contenedores = argv[2], claves = argv[3], salida = argv[4];
  unsigned parte = 0, partes = 1;
  fs::path carpeta_glsl;
  bool plegar = true;
  for (int i = 5; i < argc; ++i) {
    const std::string o = argv[i];
    if (o == "--parte" && i + 2 < argc) {
      parte = unsigned(std::atoi(argv[i + 1]));
      partes = unsigned(std::max(1, std::atoi(argv[i + 2])));
      i += 2;
    } else if (o == "--glsl" && i + 1 < argc) {
      carpeta_glsl = argv[++i];
    } else if (o == "--sin-plegar") {
      plegar = false;
    } else {
      throw std::runtime_error("unknown option " + o);
    }
  }
  fs::create_directories(salida);
  if (!carpeta_glsl.empty()) fs::create_directories(carpeta_glsl);

  ds::BibliotecaShaders biblioteca;
  biblioteca.CargarIndice(biblioteca_ruta);
  uam::init(true);

  std::ifstream f(claves);
  if (!f) throw std::runtime_error("unreadable: " + claves.string());
  char nombre_informe[32];
  std::snprintf(nombre_informe, sizeof(nombre_informe), "informe_%02u.tsv", parte);
  std::FILE* informe = std::fopen((salida / nombre_informe).string().c_str(), "w");
  if (!informe) throw std::runtime_error("cannot write the report");

  std::string linea, ultimo_nombre;
  std::vector<uint32_t> spirv;
  uint64_t huella = 0;
  bool encontrado = false;
  size_t n = 0, hechas = 0, fallidas = 0, sin_cambio = 0;
  for (size_t i = 0; std::getline(f, linea); ++i) {
    if (linea.empty() || linea[0] == '#') {
      continue;
    }
    if (n++ % partes != parte) {
      continue;
    }
    char nombre[128];
    unsigned espec = 0, salidas = 0;
    if (std::sscanf(linea.c_str(), "%127s %x %x", nombre, &espec, &salidas) != 3) {
      throw std::runtime_error("bad key line: " + linea);
    }
    if (ultimo_nombre != nombre) {
      ultimo_nombre = nombre;
      const std::vector<uint8_t> original = Leer(contenedores / (std::string(nombre) + ".bin"));
      const ds::Shader* s = biblioteca.Buscar(original);
      encontrado = s && s->vertices;
      if (encontrado) {
        spirv = biblioteca.LeerSpirv(size_t(s - biblioteca.shaders().data()));
        huella = XXH3_64bits(spirv.data(), spirv.size() * sizeof(uint32_t));
      }
    }
    const auto t0 = std::chrono::steady_clock::now();
    bool ok = false;
    uint32_t plegadas = 0;
    size_t bytes = 0;
    std::string motivo;
    const uint32_t espec_clave = plegar ? (espec | rex::deko3d::kEspecVsEntradaPlegada) : espec;
    if (!encontrado) {
      motivo = "not a vertex shader of the library";
    } else {
      rex::deko3d::GlslDeko g = rex::deko3d::SpirvAGlsl(spirv, espec, salidas);
      if (g.glsl.empty()) {
        motivo = "conversion: " + g.error;
      } else if (plegar && !rex::deko3d::PlegarEntradaVs(g.glsl, &plegadas)) {
        motivo = "fold refused: a read of the folded words in an unknown form";
      } else if (plegar && !plegadas) {
        motivo = "nothing to fold";
        ++sin_cambio;
      } else {
        char base[64];
        std::snprintf(base, sizeof(base), "%016llX_%08X_%08X", static_cast<unsigned long long>(huella), espec_clave,
                      salidas);
        if (!carpeta_glsl.empty()) {
          Escribir(carpeta_glsl / (std::string(base) + ".vert"), g.glsl.data(), g.glsl.size());
        }
        uam::Result r = uam::compile(uam::Stage::Vertex, g.glsl.c_str());
        motivo = UnaLinea(r.log);
        if (r.ok && !r.dksh.empty()) {
          ok = true;
          bytes = r.dksh.size();
          Escribir(salida / (std::string(base) + ".dksh"), r.dksh.data(), r.dksh.size());
        }
      }
    }
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    std::fprintf(informe, "%s\t%016llX\t%08X\t%08X\t%d\t%u\t%zu\t%.1f\t%s\n", nombre,
                 static_cast<unsigned long long>(huella), espec_clave, salidas, int(ok), plegadas, bytes, ms,
                 motivo.c_str());
    std::fflush(informe);
    ok ? ++hechas : ++fallidas;
  }
  std::fclose(informe);
  std::printf("part %u of %u: %zu DKSH written, %zu not written (%zu of them with nothing to fold)\n", parte, partes,
              hechas, fallidas, sin_cambio);
  return 0;
} catch (const std::exception& e) {
  std::fprintf(stderr, "ds_vs_plegado: %s\n", e.what());
  return 1;
}
