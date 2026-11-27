// rex::deko3d (from nfsmw-nx): the DKSH cache file (docs/HowToDeko3d.md, section 3.3).
//
// One DKSH per (SPIR-V, specialization value, variant): the SPIR-V the renderer converts (a library shader, or
// its color-write pruning or early depth variant), constant_id 0 masked to the bits the shader reads
// (MascaraEspecializacion), and for a vertex shader the interpolators it keeps (SpirvAGlsl's `salidas`; 0 for
// pixel shaders). The file is made by shaders/dksh/nfsmw_dksh on the PC, or by the console's compile worker, and
// kept on the SD card. It is made from the game's shaders, so it is never distributed.
//
// The DKSH depends on more than the key: uam and its patches, SPIRV-Cross, and sdk/src/ui/deko3d/glsl.cpp. Their identity
// (IdCompilador) is in the header, and a cache made by another one is not loaded: the lesson of NVK's shader
// cache, which kept old compiled shaders after a compiler fix (docs/shaders.md).
//
// Layout, little-endian:
//   0  "NFDK"            4  version (2)        8  XXH3 of IdCompilador()
//   16 entries           20 0                  24 XXH3 of the bytes from 32 to the end
//   32 entries x {u64 SPIR-V fingerprint, u32 specialization value, u32 variant, u32 DKSH bytes, u32 0}, sorted
//      by key
//   then the DKSH of each entry, in the same order, each starting at a multiple of 4.

#pragma once

#include <compare>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <map>
#include <mutex>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace rex::deko3d {

// "uam ... ; SPIRV-Cross ... ; GLSL revision N". Change the first two with tools/fetch_thirdparty.py's commits.
std::string IdCompilador();

// XXH3 of the SPIR-V words as given to SpirvAGlsl.
uint64_t HuellaSpirv(std::span<const uint32_t> espirv);

class CacheDksh {
 public:
  // Replaces the content with the file's. false (and empty) if it is not a cache of this compiler or is damaged;
  // `motivo` says why, to log it.
  bool Cargar(std::span<const uint8_t> archivo, std::string& motivo);
  // Only the entry table (Gears of War 2 with every shader: ~800 MB of DKSH): Buscar reads an entry from the file the
  // first time it is asked for. The entries already here (Anadir) stay and win. No integrity hash over the file
  // (it would read all of it): an entry is checked by its size and DKSH header when read.
  bool CargarIndice(const std::filesystem::path& ruta, std::string& motivo);
  std::vector<uint8_t> Serializar() const;

  struct Clave {
    uint64_t huella = 0;
    uint32_t especializacion = 0;
    uint32_t variante = 0;
    auto operator<=>(const Clave&) const = default;
  };
  // nullptr if it is not there. Valid until the next Anadir or Cargar.
  const std::vector<uint8_t>* Buscar(const Clave& clave) const;
  void Anadir(const Clave& clave, std::vector<uint8_t> dksh);
  size_t size() const { return entradas_.size() + indice_.size(); }
  const std::map<Clave, std::vector<uint8_t>>& entradas() const { return entradas_; }
  // Takes the entries out (the cache is left empty): merging a large cache into another without copying it.
  std::map<Clave, std::vector<uint8_t>> Extraer() { return std::move(entradas_); }

 private:
  // mutable: Buscar moves an entry of the file (CargarIndice) here when it is first asked for.
  mutable std::map<Clave, std::vector<uint8_t>> entradas_;
  struct EnArchivo {
    Clave clave;
    uint64_t posicion = 0;
    uint32_t bytes = 0;
  };
  std::vector<EnArchivo> indice_;  // sorted by clave
  std::filesystem::path ruta_;
  mutable std::FILE* archivo_ = nullptr;
  mutable std::mutex mutex_;

 public:
  CacheDksh() = default;
  CacheDksh(const CacheDksh&) = delete;
  CacheDksh& operator=(const CacheDksh&) = delete;
  ~CacheDksh();
};

}  // namespace rex::deko3d

// NFS's renderer code (nfsmw_pal/src/nfsmw_dk_*) still names these nfsmw::deko::..., until it is renamed.
namespace nfsmw::deko {
using namespace ::rex::deko3d;
}  // namespace nfsmw::deko
