// xmow_setup - Xbox 360 disc image (XDVDFS) reader and XEX2 decryption/decompression.
// The XDVDFS part is a port of fontes\XMOW-nx\tools\extract_iso.py. Nothing from the game is in this file.
// Copyright (C) 2026 xmow-nx contributors. GPL-3.0-or-later (see LICENSE).
#pragma once
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace disco {

struct Entrada {
  std::string caminho;   // relative, '/' separated, as on the disc
  uint32_t setor = 0;
  uint32_t tamanho = 0;
  bool pasta = false;
};

class Iso {
 public:
  ~Iso();
  // Opens the image and finds the game partition (raw, XGD2, XGD3, XGD1 or by scanning the first GiB).
  bool Abrir(const std::filesystem::path& p, std::string& erro);
  const std::vector<Entrada>& Entradas() const { return entradas_; }
  const Entrada* Achar(const std::string& caminho) const;  // case-insensitive
  bool Ler(const Entrada& e, std::vector<uint8_t>& out, std::string& erro);
  bool Extrair(const Entrada& e, const std::filesystem::path& dest, std::string& erro);
  uint64_t Base() const { return base_; }
  const char* Tipo() const { return tipo_; }

 private:
  bool LerEm(uint64_t off, void* buf, size_t n);
  void Andar(uint32_t setor, uint32_t tamanho, const std::string& prefixo, int nivel);
  FILE* f_ = nullptr;
  uint64_t base_ = 0;
  const char* tipo_ = "";
  std::vector<Entrada> entradas_;
};

// default.xex -> PE image (AES-128-CBC with the retail or devkit key, then basic/no compression).
bool XexImagem(const std::vector<uint8_t>& xex, std::vector<uint8_t>& imagem, std::string& info, std::string& erro);

}  // namespace disco
