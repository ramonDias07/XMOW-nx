// rex::deko3d (from nfsmw-nx): the SPIR-V of the deko3d shader library as GLSL that uam (devkitPro's deko3d shader
// compiler) accepts. docs/HowToDeko3d.md, section 3.
//
// The deko3d library is the same HLSL as the Vulkan one compiled with -DNFSMW_DEKO3D (shader_common.h): no
// descriptor heaps, no push constants, no 64-bit integers, and each Xenos sampler register R as a texture and a
// sampler at binding R. SPIRV-Cross writes it as GLSL 4.60 without Vulkan semantics; this adds what uam needs on
// top: every texture with an explicit binding, and the specialization constant with its value, so one GLSL (and
// one DKSH) per (shader, value).
//
// Pure C++ with SPIRV-Cross: built for the PC (shaders/nfsmw_dksh.cpp, the offline DKSH cache) and the Switch
// (the compile worker), with the same output on both.

#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace rex::deko3d {

// Raise it when the GLSL this writes changes for the same input: it is part of the DKSH cache id
// (rex/ui/deko3d/cache_dksh.h), and a cache made with another revision must not be loaded.
inline constexpr uint32_t kRevisionGlsl = 1;

// deko3d's limits per stage (uam's README): UBO and texture bindings.
inline constexpr uint32_t kMaxUbos = 16;
inline constexpr uint32_t kMaxTexturas = 32;

struct GlslDeko {
  std::string glsl;  // empty if the conversion failed (see error)
  bool vertices = false;
  uint32_t ubos = 0;      // bit N: the shader reads the UBO at binding N
  uint32_t texturas = 0;  // bit N: it samples or queries the texture at binding N
  uint32_t texturas_3d = 0;    // of those, the 3D ones
  uint32_t texturas_cubo = 0;  // and the cube ones (the rest are 2D)
  // Interpolator locations (TEXCOORDn = n, COLORn = 16 + n): what a pixel shader reads, what a vertex shader
  // still writes after `salidas` (SpirvAGlsl).
  uint32_t interpoladores = 0;
  std::string error;
  // Shapes that compile but that uam is known to get wrong (HowToDeko3d.md, section 6): a dynamic component
  // index into a UBO vector. One line each; the caller logs them.
  std::vector<std::string> avisos;
};

// `espirv` is a deko3d library shader, or the renderer's SPIR-V variant of one (the color-write pruning and the
// early depth test work on SPIR-V, before this). `especializacion` is the value of constant_id 0
// (g_SpecConstants), the pipeline key's especializacion.
//
// `salidas` (vertex shaders): the interpolator locations to keep, normally the pixel shader's `interpoladores`.
// Every library vertex shader writes all 22; NVK drops the ones the pixel shader does not read when it links the
// pipeline, and uam has no linking, so the others are removed here and Mesa's front end drops what only fed them.
// ~0u keeps them all.
GlslDeko SpirvAGlsl(std::span<const uint32_t> espirv, uint32_t especializacion, uint32_t salidas = ~0u);

/*
 * The bits of the specialization value that change this shader's code. The pipeline key has one value for both
 * stages, but a vertex shader reads none of it and a pixel shader at most 7 bits (alpha test and its function,
 * 1/size, cheap PCF, shadow minimum, no blur), so a DKSH is made and looked up for `value & mask` only: one per
 * vertex shader instead of one per pipeline.
 *
 * Every use of the constant in the deko3d library is an OpSpecConstantOp: an AND with a literal (its bits go in
 * the mask), or tfetch2DSombraMin's comparison with NFSMW_MARCA_SOMBRA_MINIMO, which has bits 24-30 set and is
 * never equal to a value the renderer uses (shader_common.h), so it reads nothing. Any other use gives ~0u: no
 * masking, correct and only slower to compile.
 */
uint32_t MascaraEspecializacion(std::span<const uint32_t> espirv);

/*
 * xmow 1039 (XMOW 0.1.33): a vertex shader's input fix-ups folded away.
 *
 * Every vertex fetch of the library goes through remapInput(value, g_InputRemap(location)) (shader_common.h: shared
 * words 74-89, one per attribute location 0-15) and every TEXCOORD through tfetchTexcoord(g_SwappedTexcoords, ...)
 * (shared word 65). The renderer sends 0xFFF (identity) in all sixteen remap words for ~98 % of the draws and always
 * 0 in word 65, but the shader still loads each word, compares it and branches around a four-step select loop per
 * attribute, and selects value / value.yxwz per texcoord.
 *
 * PlegarEntradaVs rewrites the GLSL SpirvAGlsl made: each `floatBitsToUint(g_UboCompartidas.v[K].c)` that reads
 * one of those words becomes the literal the renderer guarantees (4095u, or 0u for word 65). Mesa's front end in uam
 * then folds the comparisons and drops the loops. The code is exactly what the shader computes with those values:
 * only moves and selects go away, no arithmetic changes. The renderer may only draw with the folded DKSH when the
 * sixteen remap words it sends are 0xFFF and word 65 is 0.
 *
 * Folded DKSH are keyed with kEspecVsEntradaPlegada set in the specialization of the cache key (same SPIR-V
 * fingerprint, same output mask). The bit is not a specialization of the SPIR-V: no shader of the library tests it
 * (shader_common.h uses bits 0, 1, 8, 9, 15-19 and 23), so it is passed to SpirvAGlsl masked out.
 *
 * Returns false and leaves the text alone if a read of those words is not in that exact form (a swizzle, a whole
 * vector, a computed index, the block passed whole): then nothing is known about it and the shader is not folded.
 * *plegadas is the number of reads replaced; 0 means the folded text is the unfolded one.
 */
inline constexpr uint32_t kEspecVsEntradaPlegada = 1u << 20;
bool PlegarEntradaVs(std::string& glsl, uint32_t* plegadas = nullptr);

}  // namespace rex::deko3d

// NFS's renderer code (nfsmw_pal/src/nfsmw_dk_*) still names these nfsmw::deko::..., until it is renamed.
namespace nfsmw::deko {
using namespace ::rex::deko3d;
}  // namespace nfsmw::deko
