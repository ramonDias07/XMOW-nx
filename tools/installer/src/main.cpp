// xmow_setup - builds the shader library of the X-Men Origins: Wolverine Switch port from the user's own disc.
//   xmow_setup (--iso <image.iso> | --disc <extracted folder>) --out <folder> [--no-game-root] [--jobs N]
//              [--start entrada|hlsl|dxc|dslib]
// Stages (M1, M2): disc -> game_root, default.xex -> 18 XEX containers, .xxx -> 16,372 containers, 67 dump variants
// (diffs shipped), manifest check of all 16,457; ds_hlsl -> DXC (SPIR-V) -> ds_empaquetar -> xmow_shaders.dslib (SHA-256).
// Data next to the exe: data\ (manifest, recipes, shader_common.h), tools\ (frozen exes, dxc).
// Copyright (C) 2026 xmow-nx contributors.
// This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public
// License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later
// version. It is distributed WITHOUT ANY WARRANTY; see LICENSE.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "cripto.h"
#include "disco.h"
#include "nfdk.h"
#include <psapi.h>
#include <conio.h>

namespace fs = std::filesystem;
using Bytes = std::vector<uint8_t>;

namespace {

std::mutex g_log_mu;
FILE* g_log = nullptr;
void Log(const char* fmt, ...) {
  char buf[4096];
  va_list ap;
  va_start(ap, fmt);
  std::vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  std::lock_guard<std::mutex> l(g_log_mu);
  std::fputs(buf, stdout);
  std::fputc('\n', stdout);
  std::fflush(stdout);
  if (g_log) { std::fputs(buf, g_log); std::fputc('\n', g_log); std::fflush(g_log); }
}

struct Falha {
  std::string msg;
};
[[noreturn]] void Erro(const std::string& m) { throw Falha{m}; }

std::string U8(const fs::path& p) {
  const auto s = p.u8string();
  return std::string(s.begin(), s.end());
}

fs::path P8(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); }

Bytes LerArq(const fs::path& p) {
  std::ifstream f(p, std::ios::binary);
  if (!f) Erro("cannot read " + U8(p));
  return Bytes(std::istreambuf_iterator<char>(f), {});
}
void GravarArq(const fs::path& p, const void* d, size_t n) {
  std::ofstream f(p, std::ios::binary | std::ios::trunc);
  f.write(static_cast<const char*>(d), std::streamsize(n));
  if (!f) Erro("cannot write " + U8(p));
}
std::string ShaArq(const fs::path& p) {
  FILE* f = _wfopen(p.wstring().c_str(), L"rb");
  if (!f) return "";
  cripto::Sha256 s;
  std::vector<char> b(1u << 20);
  size_t n;
  while ((n = std::fread(b.data(), 1, b.size(), f)) > 0) s.Update(b.data(), n);
  std::fclose(f);
  return cripto::Sha256::Hex(s.Final());
}
std::vector<std::vector<std::string>> LerTsv(const fs::path& p) {
  std::ifstream f(p, std::ios::binary);
  if (!f) Erro("missing data file " + U8(p));
  std::vector<std::vector<std::string>> r;
  std::string l;
  while (std::getline(f, l)) {
    if (!l.empty() && l.back() == '\r') l.pop_back();
    if (l.empty() || l[0] == '#') continue;
    std::vector<std::string> c;
    std::stringstream ss(l);
    std::string x;
    while (std::getline(ss, x, '\t')) c.push_back(x);
    r.push_back(c);
  }
  return r;
}
Bytes DeHex(const std::string& h) {
  if (h.size() % 2) Erro("bad hex in data");
  Bytes b(h.size() / 2);
  for (size_t i = 0; i < b.size(); ++i) b[i] = uint8_t(std::stoul(h.substr(2 * i, 2), nullptr, 16));
  return b;
}

double Agora() {
  using namespace std::chrono;
  return duration<double>(steady_clock::now().time_since_epoch()).count();
}

// ---- processes ------------------------------------------------------------------------------------------------
HANDLE g_job = nullptr;
std::wstring Aspas(const std::wstring& a) {
  if (!a.empty() && a.find_first_of(L" \t\"") == std::wstring::npos) return a;
  std::wstring r = L"\"";
  size_t barras = 0;
  for (wchar_t c : a) {
    if (c == L'\\') { ++barras; continue; }
    if (c == L'"') r.append(barras * 2 + 1, L'\\');
    else r.append(barras, L'\\');
    barras = 0;
    r += c;
  }
  r.append(barras * 2, L'\\');
  return r + L"\"";
}
// Runs exe with args, stdout+stderr to log (or NUL). Returns exit code, -1 on start failure, -2 on timeout.
int Rodar(const fs::path& exe, const std::vector<std::wstring>& args, const fs::path& log, DWORD timeout_ms = INFINITE) {
  std::wstring cmd = Aspas(exe.wstring());
  for (const auto& a : args) cmd += L" " + Aspas(a);
  SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
  HANDLE h = CreateFileW(log.empty() ? L"NUL" : log.wstring().c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa,
                         CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) return -1;
  SIZE_T tam = 0;
  InitializeProcThreadAttributeList(nullptr, 1, 0, &tam);
  std::vector<uint8_t> lista(tam);
  auto* attr = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(lista.data());
  InitializeProcThreadAttributeList(attr, 1, 0, &tam);
  UpdateProcThreadAttribute(attr, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, &h, sizeof(h), nullptr, nullptr);
  STARTUPINFOEXW si{};
  si.StartupInfo.cb = sizeof(si);
  si.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
  si.StartupInfo.hStdInput = nullptr;
  si.StartupInfo.hStdOutput = h;
  si.StartupInfo.hStdError = h;
  si.lpAttributeList = attr;
  PROCESS_INFORMATION pi{};
  std::vector<wchar_t> linha(cmd.begin(), cmd.end());
  linha.push_back(0);
  const BOOL ok = CreateProcessW(exe.wstring().c_str(), linha.data(), nullptr, nullptr, TRUE,
                                 CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT | CREATE_SUSPENDED, nullptr, nullptr,
                                 &si.StartupInfo, &pi);
  DeleteProcThreadAttributeList(attr);
  CloseHandle(h);
  if (!ok) return -1;
  if (g_job) AssignProcessToJobObject(g_job, pi.hProcess);
  ResumeThread(pi.hThread);
  CloseHandle(pi.hThread);
  int r;
  if (WaitForSingleObject(pi.hProcess, timeout_ms) == WAIT_TIMEOUT) {
    TerminateProcess(pi.hProcess, 1);
    WaitForSingleObject(pi.hProcess, INFINITE);
    r = -2;
  } else {
    DWORD c = 0;
    GetExitCodeProcess(pi.hProcess, &c);
    r = int(c);
  }
  CloseHandle(pi.hProcess);
  return r;
}

template <class F>
void EmParalelo(size_t n, int jobs, F f) {
  std::atomic<size_t> prox{0};
  std::vector<std::thread> t;
  for (int k = 0; k < jobs; ++k)
    t.emplace_back([&] {
      for (size_t i; (i = prox++) < n;) f(i);
    });
  for (auto& x : t) x.join();
}

int JobsAuto(double gb_por_job) {
  const int hw = int(std::max(1u, std::thread::hardware_concurrency()));
  MEMORYSTATUSEX m{};
  m.dwLength = sizeof(m);
  GlobalMemoryStatusEx(&m);
  const int ram = int(std::max<double>(1.0, double(m.ullAvailPhys) / (gb_por_job * (1ull << 30))));
  return std::max(1, std::min(hw, ram));
}

void Ligar(const fs::path& de, const fs::path& para) {
  if (!CreateHardLinkW(para.wstring().c_str(), de.wstring().c_str(), nullptr)) fs::copy_file(de, para, fs::copy_options::overwrite_existing);
}

// ---- manifest ---------------------------------------------------------------------------------------------------
struct Item {
  uint64_t tam;
  std::string sha;
};
std::map<std::string, Item> CarregarManifesto(const fs::path& p) {
  std::map<std::string, Item> m;
  for (const auto& c : LerTsv(p)) {
    if (c.size() < 3) Erro("bad line in " + U8(p));
    m[c[0]] = Item{std::stoull(c[1]), c[2]};
  }
  return m;
}
std::string Maiusc(std::string s) {
  for (char& c : s) c = char(std::toupper((unsigned char)c));
  return s;
}

struct Opcoes {
  fs::path iso, disc, out;
  bool sem_game_root = false;
  int jobs = 0;
  bool manter_work = false;
  bool interativo = false;
  int inicio = 0;  // index in kEtapas
  int fim = 6;     // last stage to run (--until)
};

constexpr int kNumEtapas = 7;
const char* kEtapas[kNumEtapas] = {"entrada", "hlsl", "dxc", "dslib", "fix", "vs", "dksh"};

void Uso() {
  std::puts(
      "xmow_setup - builds the shader files of the X-Men Origins: Wolverine Switch port from your own disc.\n"
      "usage: xmow_setup (--iso <image.iso> | --disc <extracted disc folder>) --out <folder>\n"
      "                  [--no-game-root] [--jobs N] [--keep-work] [--start S] [--until S]\n"
      "                  S = entrada|hlsl|dxc|dslib|fix|vs|dksh (stage order)\n"
      "  --iso           Xbox 360 disc image (XDVDFS; XGD2/XGD3/raw)\n"
      "  --disc          folder with default.xex and WGame\\ (already extracted disc)\n"
      "  --out           output folder; copy <out>\\switch to the root of the SD card\n"
      "                  (switch\\XMOW: game_root\\ + the 4 shader files)\n"
      "  --no-game-root  do not copy the game files to <out>\\switch\\XMOW\\game_root (they are only read)\n"
      "  --jobs N        parallel processes (default: from the hardware threads and the free RAM)\n"
      "  --keep-work     keep <out>\\work (containers, HLSL, SPIR-V, DKSH journals) after a successful run\n"
      "  --start S       resume from a stage (the earlier outputs in --out must exist)\n"
      "  --until S       stop after a stage\n"
      "Run without arguments to be asked for the paths.");
}

// ---- stage 1: inputs ------------------------------------------------------------------------------------------
void EtapaEntrada(const Opcoes& o, const fs::path& data, const fs::path& tools, const std::map<std::string, Item>& man, int jobs) {
  const fs::path work = o.out / "work", entrada = work / "contenedores", extr = work / "extraidos";
  const fs::path gr = o.out / "switch" / "XMOW" / "game_root";
  fs::remove_all(entrada);
  fs::remove_all(extr);
  fs::create_directories(entrada);
  fs::create_directories(extr);
  const fs::path extrator = tools / "xmow_extract_shaders.exe";
  Bytes xex;
  double t0 = Agora();
  int n_lote = 0;
  auto rodar_extrator = [&](const fs::path& pasta) {
    const fs::path log = work / "logs" / ("extrator_" + std::to_string(n_lote++) + ".log");
    const int r = Rodar(extrator, {extr.wstring(), pasta.wstring()}, log);
    // 2 = a package with no compressed stream (expected on this disc: GuidCache.xxx, scanned as plain); the manifest decides
    if (r != 0 && r != 2) Erro("xmow_extract_shaders failed with code " + std::to_string(r) + " (see work\\logs\\extrator_N.log)");
  };
  if (!o.iso.empty()) {
    disco::Iso iso;
    std::string e;
    if (!iso.Abrir(o.iso, e)) Erro(e);
    Log("image: partition at 0x%08llX (%s), %zu entries", (unsigned long long)iso.Base(), iso.Tipo(), iso.Entradas().size());
    const disco::Entrada* dx = iso.Achar("default.xex");
    if (!dx) Erro("default.xex not found in the image");
    if (!iso.Ler(*dx, xex, e)) Erro(e);
    std::vector<const disco::Entrada*> wg;
    uint64_t bytes = 0;
    for (const auto& x : iso.Entradas())
      if (!x.pasta && (x.caminho.rfind("WGame/", 0) == 0 || x.caminho.rfind("wgame/", 0) == 0)) { wg.push_back(&x); bytes += x.tamanho; }
    if (wg.empty()) Erro("WGame\\ not found in the image");
    if (!o.sem_game_root) {
      Log("extracting default.xex + WGame\\ (%zu files, %.2f GB) into %s", wg.size(), bytes / 1e9, U8(gr).c_str());
      fs::create_directories(gr);
      GravarArq(gr / "default.xex", xex.data(), xex.size());
      size_t k = 0;
      for (const auto* x : wg) {
        if (!iso.Extrair(*x, gr / P8(x->caminho), e)) Erro(e);
        if (++k % 100 == 0) Log("  %zu/%zu files", k, wg.size());
      }
      Log("game_root: %.0f s", Agora() - t0);
      t0 = Agora();
      rodar_extrator(gr / "WGame" / "CookedXenon");
    } else {
      // only the cooked packages, a batch at a time, deleted after the scan (the extractor keeps its keys between runs)
      const fs::path lote = work / "lote";
      std::vector<const disco::Entrada*> cx;
      for (const auto* x : wg)
        if (x->caminho.size() > 18 && _strnicmp(x->caminho.c_str(), "WGame/CookedXenon/", 18) == 0) cx.push_back(x);
      Log("reading %zu packages of WGame\\CookedXenon from the image (no game_root)", cx.size());
      size_t i = 0;
      while (i < cx.size()) {
        fs::remove_all(lote);
        fs::create_directories(lote);
        uint64_t acc = 0;
        for (; i < cx.size() && acc < (1ull << 30); ++i) {
          if (!iso.Extrair(*cx[i], lote / P8(cx[i]->caminho.substr(18)), e)) Erro(e);
          acc += cx[i]->tamanho;
        }
        rodar_extrator(lote);
        Log("  %zu/%zu packages scanned", i, cx.size());
      }
      fs::remove_all(lote);
    }
  } else {
    const fs::path dx = o.disc / "default.xex", ck = o.disc / "WGame" / "CookedXenon";
    if (!fs::exists(dx) || !fs::is_directory(ck)) Erro("--disc must contain default.xex and WGame\\CookedXenon");
    xex = LerArq(dx);
    if (!o.sem_game_root && fs::weakly_canonical(o.disc) != fs::weakly_canonical(gr)) {
      Log("copying default.xex + WGame folder into %s", U8(gr).c_str());
      fs::create_directories(gr);
      fs::copy_file(dx, gr / "default.xex", fs::copy_options::overwrite_existing);
      fs::copy(o.disc / "WGame", gr / "WGame", fs::copy_options::recursive | fs::copy_options::overwrite_existing);
      Log("game_root: %.0f s", Agora() - t0);
      t0 = Agora();
    }
    rodar_extrator(ck);
  }
  Log(".xxx extraction: %.0f s", Agora() - t0);
  t0 = Agora();
  // default.xex -> image -> 18 containers (our header bytes + microcode at the recorded offset)
  Bytes img;
  std::string info, e;
  if (!disco::XexImagem(xex, img, info, e)) Erro(e);
  Log("default.xex: %s (sha256 %s)", info.c_str(), cripto::Sha256::Of(xex.data(), xex.size()).c_str());
  size_t n_xex = 0;
  for (const auto& c : LerTsv(data / "xex.tsv")) {
    const uint64_t off = std::stoull(c[1], nullptr, 16);
    const uint64_t pal = std::stoull(c[2]);
    Bytes b = DeHex(c[3]);
    if (off + pal * 4 > img.size()) Erro("XEX shader offset out of the image: different game version?");
    b.insert(b.end(), img.begin() + off, img.begin() + off + pal * 4);
    GravarArq(entrada / c[0], b.data(), b.size());
    ++n_xex;
  }
  // extracted containers: {ps,vs}_<xxh3 lower>.bin -> {ps,vs}_<XXH3 upper>.bin
  size_t n_disc = 0, n_fora = 0;
  for (const auto& f : fs::directory_iterator(extr)) {
    const std::string n = U8(f.path().filename());
    const std::string alvo = n.substr(0, 3) + Maiusc(n.substr(3, 16)) + ".bin";
    if (!man.count(alvo)) { ++n_fora; continue; }  // not part of the library (should not happen with this disc)
    fs::rename(f.path(), entrada / alvo);
    ++n_disc;
  }
  // dump variants: parent + u32 word patches, or the container itself (2 shaders with no parent on the disc)
  size_t n_var = 0, n_raw = 0;
  for (const auto& c : LerTsv(data / "variantes.tsv")) {
    const size_t tam = std::stoull(c[1]);
    Bytes b;
    if (c[3] == "raw") {
      b = DeHex(c[5]);
      ++n_raw;
    } else {
      const fs::path pai = entrada / c[4];
      if (!fs::exists(pai)) Erro("variant " + c[0] + ": parent " + c[4] + " missing (the disc did not give it)");
      b = LerArq(pai);
      b.resize(tam, 0);
      std::stringstream ss(c[5]);
      std::string op;
      while (std::getline(ss, op, ';')) {
        const size_t eq = op.find('=');
        const size_t idx = std::stoull(op.substr(0, eq), nullptr, 16);
        const Bytes v = DeHex(op.substr(eq + 1));
        if (idx * 4 + 4 > b.size() || v.size() != 4) Erro("bad variant patch " + c[0]);
        std::copy(v.begin(), v.end(), b.begin() + idx * 4);
      }
    }
    GravarArq(entrada / c[0], b.data(), b.size());
    ++n_var;
  }
  Log("containers: %zu from the .xxx packages (%zu outside the library ignored), %zu from default.xex, %zu dump variants (%zu shipped whole)",
      n_disc, n_fora, n_xex, n_var, n_raw);
  // manifest check: every container, nothing missing, nothing extra
  std::vector<std::string> nomes;
  for (const auto& kv : man) nomes.push_back(kv.first);
  std::vector<std::string> res(nomes.size());
  EmParalelo(nomes.size(), jobs, [&](size_t i) {
    const fs::path p = entrada / nomes[i];
    std::error_code ec;
    if (!fs::exists(p, ec)) { res[i] = "missing"; return; }
    if (fs::file_size(p, ec) != man.at(nomes[i]).tam || ShaArq(p) != man.at(nomes[i]).sha) res[i] = "differs";
  });
  size_t ruins = 0;
  for (size_t i = 0; i < nomes.size(); ++i)
    if (!res[i].empty()) {
      if (ruins < 20) Log("  %s: %s", nomes[i].c_str(), res[i].c_str());
      ++ruins;
    }
  size_t extra = 0;
  for (const auto& f : fs::directory_iterator(entrada))
    if (!man.count(U8(f.path().filename()))) ++extra;
  if (ruins || extra) Erro("manifest check: " + std::to_string(ruins) + " missing/different, " + std::to_string(extra) + " extra containers");
  Log("manifest: %zu of %zu containers identical (%.0f s)", nomes.size(), man.size(), Agora() - t0);
  fs::remove_all(extr);
}

// Compares an output folder against a diagnostic hash list (does not fail; the dslib hash is the check).
void Diagnostico(const fs::path& pasta, const fs::path& lista, const char* nome, int jobs) {
  if (!fs::exists(lista)) return;
  const auto m = CarregarManifesto(lista);
  std::vector<std::string> nomes;
  for (const auto& kv : m) nomes.push_back(kv.first);
  std::vector<char> dif(nomes.size(), 0);
  EmParalelo(nomes.size(), jobs, [&](size_t i) { dif[i] = ShaArq(pasta / nomes[i]) != m.at(nomes[i]).sha; });
  size_t n = 0;
  std::string ex;
  for (size_t i = 0; i < nomes.size(); ++i)
    if (dif[i]) { if (n < 10) ex += " " + nomes[i]; ++n; }
  Log("%s vs the reference run: %zu of %zu differ%s%s", nome, n, nomes.size(), n ? ", e.g." : "", ex.c_str());
}

}  // namespace

namespace {

// Worker count for the uam stages (ds_dksh, ds_vs_plegado): (free RAM - 2 GB) / 2.5 GB, capped by the hardware threads.
int JobsUam() {
  const int hw = int(std::max(1u, std::thread::hardware_concurrency()));
  MEMORYSTATUSEX m{};
  m.dwLength = sizeof(m);
  GlobalMemoryStatusEx(&m);
  const double livre = double(m.ullAvailPhys) - 2.0 * (1ull << 30);
  return std::max(1, std::min(hw, int(livre / (2.5 * (1ull << 30)))));
}

uint64_t TamanhoPasta(const fs::path& p) {
  uint64_t t = 0;
  std::error_code ec;
  for (fs::recursive_directory_iterator it(p, ec), fim; !ec && it != fim; it.increment(ec))
    if (it->is_regular_file(ec)) t += it->file_size(ec);
  return t;
}

std::string LerTexto(const fs::path& p) {
  std::ifstream f(p, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(f), {});
}

void Conferir(const fs::path& f, const Item& e) {
  const uint64_t tam = fs::exists(f) ? fs::file_size(f) : 0;
  const std::string h = ShaArq(f);
  Log("%s: %llu bytes, sha256 %s", U8(f.filename()).c_str(), (unsigned long long)tam, h.c_str());
  if (tam != e.tam || h != e.sha)
    Erro(U8(f.filename()) + " does not match the release (expected " + std::to_string(e.tam) + " bytes, sha256 " + e.sha + ")");
}

// ds_dksh run: exit code 0 and the summary line with no failed, refused, timed out or dead variant.
void DkshOk(int r, const fs::path& log, const char* nome) {
  const std::string t = LerTexto(log);
  const size_t p = t.find(" distintas con la mascara");
  if (p != std::string::npos) {
    const size_t a = t.rfind('\n', p), b = t.find('\n', p);
    Log("  %s", t.substr(a == std::string::npos ? 0 : a + 1, b - (a == std::string::npos ? 0 : a + 1)).c_str());
  }
  if (r != 0 || t.find(" 0 fallidas (0 rechazadas, 0 fuera de tiempo, 0 procesos muertos)") == std::string::npos)
    Erro(std::string(nome) + " failed (code " + std::to_string(r) + "; some variant missing, refused or timed out: see " + U8(log) + ")");
}

// Double-click use: keep the window open at the end (only when stdin is a real console).
void Pausar(const Opcoes& o) {
  DWORD modo = 0;
  if (!o.interativo || !GetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), &modo)) return;
  std::puts("Press any key to close.");
  _getch();
}

std::wstring Perguntar(const wchar_t* texto) {
  std::fputws(texto, stdout);
  std::fflush(stdout);
  wchar_t buf[4096];
  std::wstring r;
  DWORD lidos = 0, modo = 0;
  HANDLE in = GetStdHandle(STD_INPUT_HANDLE);
  if (GetConsoleMode(in, &modo) && ReadConsoleW(in, buf, DWORD(std::size(buf) - 1), &lidos, nullptr)) r.assign(buf, lidos);
  else if (std::fgetws(buf, int(std::size(buf)), stdin)) r = buf;
  while (!r.empty() && (r.back() == L'\n' || r.back() == L'\r' || r.back() == L' ')) r.pop_back();
  while (!r.empty() && r.front() == L' ') r.erase(r.begin());
  if (r.size() >= 2 && r.front() == L'"' && r.back() == L'"') r = r.substr(1, r.size() - 2);
  return r;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
  Opcoes o;
  wchar_t exe[MAX_PATH * 4];
  GetModuleFileNameW(nullptr, exe, DWORD(std::size(exe)));
  const fs::path raiz = fs::path(exe).parent_path(), data = raiz / "data", tools = raiz / "tools";
  try {
    auto etapa = [](const std::wstring& s) {
      for (int k = 0; k < kNumEtapas; ++k)
        if (s == fs::path(kEtapas[k]).wstring()) return k;
      Erro("unknown stage name (use --help)");
    };
    for (int i = 1; i < argc; ++i) {
      const std::wstring a = argv[i];
      auto val = [&]() -> std::wstring {
        if (i + 1 >= argc) Erro("missing value after an option");
        return argv[++i];
      };
      if (a == L"--iso") o.iso = val();
      else if (a == L"--disc") o.disc = val();
      else if (a == L"--out") o.out = val();
      else if (a == L"--no-game-root") o.sem_game_root = true;
      else if (a == L"--keep-work") o.manter_work = true;
      else if (a == L"--jobs") o.jobs = std::stoi(val());
      else if (a == L"--start") o.inicio = etapa(val());
      else if (a == L"--until") o.fim = etapa(val());
      else if (a == L"-h" || a == L"--help") { Uso(); return 0; }
      else Erro("unknown option (use --help)");
    }
    if (argc == 1) {  // double click: ask in the console
      o.interativo = true;
      std::puts("xmow_setup - shader files of the X-Men Origins: Wolverine Switch port, from your own disc.\n"
                "It needs about 10 GB of free disk space and takes 1-2 hours.\n");
      const std::wstring in = Perguntar(L"Path of your Xbox 360 disc image (.iso) or extracted disc folder: ");
      if (in.empty()) Erro("no input given");
      (fs::is_directory(in) ? o.disc : o.iso) = in;
      const fs::path padrao = raiz / "XMOW-SD";
      const std::wstring out = Perguntar((L"Output folder [" + padrao.wstring() + L"]: ").c_str());
      o.out = out.empty() ? padrao : fs::path(out);
    }
    if (o.out.empty() || (o.inicio == 0 && o.iso.empty() == o.disc.empty())) { Uso(); return 1; }
    if (!o.iso.empty() && !fs::is_regular_file(o.iso)) Erro("the image file does not exist");
    if (!o.disc.empty() && !fs::is_directory(o.disc)) Erro("the disc folder does not exist");
  } catch (const Falha& f) {
    std::fprintf(stderr, "error: %s\n", f.msg.c_str());
    Pausar(o);
    return 1;
  } catch (const std::exception& e) {
    std::fprintf(stderr, "error: bad arguments (%s)\n", e.what());
    return 1;
  }

  int rc = 0;
  try {
    o.out = fs::absolute(o.out);
    const fs::path work = o.out / "work", entrada = work / "contenedores", hlsl = work / "hlsl", spirv = work / "spirv";
    const fs::path dest = o.out / "switch" / "XMOW";
    fs::create_directories(work / "logs");
    fs::create_directories(dest);
    g_log = _wfopen((o.out / "xmow_setup.log").wstring().c_str(), o.inicio ? L"ab" : L"wb");
    g_job = CreateJobObjectW(nullptr, nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION li{};
    li.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    SetInformationJobObject(g_job, JobObjectExtendedLimitInformation, &li, sizeof(li));
    SetEnvironmentVariableW(L"DS_MULTIPLICACION_XENOS", nullptr);  // ds_hlsl must run with the default multiply
    if (!cripto::Aes128Dec::SelfTest()) Erro("AES self test failed");
    const int jobs = o.jobs > 0 ? o.jobs : JobsAuto(1.0);
    const int jobs_uam = o.jobs > 0 ? o.jobs : JobsUam();
    Log("xmow_setup 0.2: %d parallel processes (%d for the uam stages), output %s", jobs, jobs_uam, U8(o.out).c_str());
    const auto man = CarregarManifesto(data / "manifesto.tsv");
    const auto esperado = CarregarManifesto(data / "esperado.tsv");
    std::vector<std::string> nomes;
    for (const auto& kv : man) nomes.push_back(kv.first);
    const fs::path dslib = dest / "xmow_shaders.dslib";
    const double t_ini = Agora();
    double t0 = Agora();
    uint64_t disco_max = 0;
    auto feito = [&](const char* nome) {
      disco_max = std::max(disco_max, TamanhoPasta(o.out));
      Log("stage %s: %.0f s (output folder now %.2f GB)", nome, Agora() - t0, disco_max / 1e9);
    };
    auto roda = [&](int k) { return o.inicio <= k && k <= o.fim; };

    if (roda(0)) {
      Log("== 1/7 inputs (disc -> 16,457 containers)");
      EtapaEntrada(o, data, tools, man, jobs);
      feito("entrada");
    }

    if (roda(1)) {
      t0 = Agora();
      Log("== 2/7 ds_hlsl (--saltos-lineales, %d slices)", jobs);
      fs::remove_all(hlsl);
      fs::remove_all(work / "partes");
      for (int k = 0; k < jobs; ++k) fs::create_directories(work / "partes" / ("in" + std::to_string(k)));
      for (size_t i = 0; i < nomes.size(); ++i)
        Ligar(entrada / nomes[i], work / "partes" / ("in" + std::to_string(i % jobs)) / nomes[i]);
      std::vector<int> cods(jobs);
      EmParalelo(size_t(jobs), jobs, [&](size_t k) {
        const fs::path p = work / "partes";
        cods[k] = Rodar(tools / "ds_hlsl.exe",
                        {(p / ("in" + std::to_string(k))).wstring(), (p / ("out" + std::to_string(k))).wstring(),
                         (data / "shader_common.h").wstring(), L"--saltos-lineales"},
                        work / "logs" / ("hlsl_" + std::to_string(k) + ".log"));
      });
      fs::create_directories(hlsl);
      size_t total = 0;
      for (int k = 0; k < jobs; ++k) {
        const fs::path d = work / "partes" / ("out" + std::to_string(k));
        if (fs::is_directory(d))
          for (const auto& f : fs::directory_iterator(d)) { fs::rename(f.path(), hlsl / f.path().filename()); ++total; }
      }
      fs::remove_all(work / "partes");
      for (int k = 0; k < jobs; ++k)
        if (cods[k] != 0) Log("  ds_hlsl slice %d returned %d", k, cods[k]);
      size_t faltam = 0;
      for (const auto& n : nomes)
        if (!fs::exists(hlsl / (n.substr(0, n.size() - 4) + ".hlsl"))) { if (faltam < 10) Log("  not translated: %s", n.c_str()); ++faltam; }
      if (faltam) Erro(std::to_string(faltam) + " shaders not translated by ds_hlsl (see work\\logs\\hlsl_*.log)");
      Log("ds_hlsl: %zu of %zu translated", total, nomes.size());
      Diagnostico(hlsl, data / "diag_hlsl.tsv", "HLSL", jobs);
      feito("hlsl");
    }

    if (roda(2)) {
      t0 = Agora();
      Log("== 3/7 DXC -> SPIR-V (%d processes)", jobs);
      fs::remove_all(spirv);
      fs::remove_all(work / "err");
      fs::create_directories(spirv);
      fs::create_directories(work / "err");
      std::atomic<size_t> feitos{0}, falhas{0};
      EmParalelo(nomes.size(), jobs, [&](size_t i) {
        const std::string base = nomes[i].substr(0, nomes[i].size() - 4);
        const fs::path dst = spirv / (base + ".spv"), err = work / "err" / (base + ".txt");
        const int r = Rodar(tools / "dxc.exe",
                            {L"-spirv", L"-T", base.rfind("ps_", 0) == 0 ? L"ps_6_6" : L"vs_6_6", L"-E", L"main", L"-HV", L"2021",
                             L"-fspv-target-env=vulkan1.2", L"-fvk-use-dx-layout", L"-Werror=parameter-usage", L"-Fo", dst.wstring(),
                             (hlsl / (base + ".hlsl")).wstring()},
                            err, 900000);
        std::error_code ec;
        if (r == 0 && fs::exists(dst, ec)) fs::remove(err, ec);
        else { fs::remove(dst, ec); ++falhas; }
        const size_t f = ++feitos;
        if (f % 2000 == 0) Log("  %zu/%zu", f, nomes.size());
      });
      if (falhas) Erro(std::to_string(falhas.load()) + " shaders failed in DXC (see work\\err)");
      fs::remove_all(work / "err");
      Log("DXC: %zu of %zu compiled", nomes.size() - falhas, nomes.size());
      Diagnostico(spirv, data / "diag_spirv.tsv", "SPIR-V", jobs);
      feito("dxc");
    }

    if (roda(3)) {
      t0 = Agora();
      Log("== 4/7 ds_empaquetar -> xmow_shaders.dslib");
      fs::remove(dslib);
      const int r = Rodar(tools / "ds_empaquetar.exe", {entrada.wstring(), spirv.wstring(), dslib.wstring()}, work / "logs" / "empaquetar.log");
      Log("  %s", LerTexto(work / "logs" / "empaquetar.log").c_str());
      if (r != 0) Erro("ds_empaquetar failed with code " + std::to_string(r));
      Conferir(dslib, esperado.at("xmow_shaders.dslib"));
      feito("dslib");
    }

    if (roda(4)) {
      // uam post-factor fix (0.1.27): the 3 pixel shaders whose DKSH it changes, alone in a small library, through the
      // fixed ds_dksh with the same variant arguments; that cache is exactly the 36 changed entries.
      t0 = Agora();
      Log("== 5/7 shadercache_dksh_fix.bin (fixed uam, %s)", "mini library of the 3 changed pixel shaders");
      const fs::path mini = work / "mini";
      fs::remove_all(mini);
      fs::create_directories(mini / "c");
      fs::create_directories(mini / "spirv");
      for (const auto& c : LerTsv(data / "dksh_fix.tsv")) {
        Ligar(entrada / (c[0] + ".bin"), mini / "c" / (c[0] + ".bin"));
        Ligar(spirv / (c[0] + ".spv"), mini / "spirv" / (c[0] + ".spv"));
      }
      int r = Rodar(tools / "ds_empaquetar.exe", {(mini / "c").wstring(), (mini / "spirv").wstring(), (mini / "mini.dslib").wstring()},
                    work / "logs" / "fix_empaquetar.log");
      if (r != 0) Erro("ds_empaquetar (mini library) failed with code " + std::to_string(r));
      const fs::path fix = dest / "shadercache_dksh_fix.bin", log = work / "logs" / "dksh_fix.log";
      fs::remove(fix);
      r = Rodar(tools / "ds_dksh_0127.exe",
                {(mini / "mini.dslib").wstring(), fix.wstring(), L"--contenedores", (mini / "c").wstring(), L"--informe",
                 (mini / "dksh.tsv").wstring(), L"--variantes-ps", L"1,208,209", L"--variantes", (data / "variantes_consola.tsv").wstring(),
                 L"--hilos", L"1", L"--limite", L"120", L"--max-por-proceso", L"400", L"--diario", (mini / "diarios").wstring()},
                log);
      DkshOk(r, log, "ds_dksh_0127");
      uint64_t id = 0;
      nfdk::Cache c;
      const std::string e = nfdk::Ler(LerArq(fix), id, &c);
      if (!e.empty()) Erro("shadercache_dksh_fix.bin: " + e);
      Log("  %zu entries, compiler id %016llX", c.size(), (unsigned long long)id);
      Conferir(fix, esperado.at("shadercache_dksh_fix.bin"));
      feito("fix");
    }

    if (roda(5)) {
      // 0.1.33 folded vertex shaders: the 2,355 proven keys through ds_vs_plegado, packed sorted with the cache's compiler id
      t0 = Agora();
      Log("== 6/7 shadercache_dksh_vs.bin (ds_vs_plegado, %d processes)", jobs_uam);
      const fs::path vs = work / "vs";
      fs::remove_all(vs);
      fs::create_directories(vs);
      const fs::path claves = data / "claves_vs.txt";
      std::vector<int> cods(jobs_uam);
      EmParalelo(size_t(jobs_uam), jobs_uam, [&](size_t k) {
        cods[k] = Rodar(tools / "ds_vs_plegado.exe",
                        {dslib.wstring(), entrada.wstring(), claves.wstring(), vs.wstring(), L"--parte", std::to_wstring(k),
                         std::to_wstring(jobs_uam)},
                        work / "logs" / ("vs_" + std::to_string(k) + ".log"));
      });
      for (int k = 0; k < jobs_uam; ++k)
        if (cods[k] != 0) Erro("ds_vs_plegado part " + std::to_string(k) + " failed with code " + std::to_string(cods[k]));
      size_t n_claves = 0;
      {
        std::ifstream f(claves);
        std::string l;
        while (std::getline(f, l))
          if (!l.empty() && l[0] != '#') ++n_claves;
      }
      size_t ruins = 0;
      for (int k = 0; k < jobs_uam; ++k) {
        char nome[32];
        std::snprintf(nome, sizeof(nome), "informe_%02d.tsv", k);
        for (const auto& c : LerTsv(vs / nome))
          if (c.size() < 5 || c[4] != "1") { if (ruins < 10) Log("  not compiled: %s %s", c[0].c_str(), c.size() > 8 ? c[8].c_str() : ""); ++ruins; }
      }
      if (ruins) Erro(std::to_string(ruins) + " folded vertex shader variants not compiled");
      nfdk::Cache cache;
      for (const auto& f : fs::directory_iterator(vs)) {
        if (f.path().extension() != ".dksh") continue;
        unsigned long long h = 0;
        unsigned e = 0, v = 0;
        if (std::sscanf(U8(f.path().stem()).c_str(), "%16llX_%8X_%8X", &h, &e, &v) != 3) Erro("bad DKSH name " + U8(f.path()));
        cache[nfdk::Chave{h, e, v}] = LerArq(f.path());
      }
      if (cache.size() != n_claves) Erro(std::to_string(cache.size()) + " folded DKSH for " + std::to_string(n_claves) + " keys");
      uint64_t id = 0;
      const std::string e = nfdk::Ler(LerArq(dest / "shadercache_dksh_fix.bin"), id, nullptr);  // same compiler id (fixed uam)
      if (!e.empty()) Erro("shadercache_dksh_fix.bin (compiler id source): " + e);
      const Bytes b = nfdk::Montar(id, cache);
      GravarArq(dest / "shadercache_dksh_vs.bin", b.data(), b.size());
      Log("  %zu folded DKSH", cache.size());
      Conferir(dest / "shadercache_dksh_vs.bin", esperado.at("shadercache_dksh_vs.bin"));
      feito("vs");
    }

    if (roda(6)) {
      t0 = Agora();
      Log("== 7/7 shadercache_dksh.bin (ds_dksh 0.1.23, %d workers; the longest stage)", jobs_uam);
      const fs::path cache = dest / "shadercache_dksh.bin", log = work / "logs" / "dksh.log";
      fs::remove(cache);
      fs::remove_all(work / "dksh_diarios");
      const int r = Rodar(tools / "ds_dksh_0123.exe",
                          {dslib.wstring(), cache.wstring(), L"--contenedores", entrada.wstring(), L"--informe", (work / "dksh.tsv").wstring(),
                           L"--variantes-ps", L"1,208,209", L"--variantes", (data / "variantes_consola.tsv").wstring(), L"--hilos",
                           std::to_wstring(jobs_uam), L"--limite", L"120", L"--max-por-proceso", L"400", L"--diario",
                           (work / "dksh_diarios").wstring()},
                          log);
      DkshOk(r, log, "ds_dksh_0123");
      Conferir(cache, esperado.at("shadercache_dksh.bin"));
      feito("dksh");
    }

    JOBOBJECT_EXTENDED_LIMIT_INFORMATION q{};
    QueryInformationJobObject(g_job, JobObjectExtendedLimitInformation, &q, sizeof(q), nullptr);
    PROCESS_MEMORY_COUNTERS pm{};
    pm.cb = sizeof(pm);
    GetProcessMemoryInfo(GetCurrentProcess(), &pm, sizeof(pm));
    Log("peak memory: child processes together %.2f GB, xmow_setup itself %.2f GB; peak output folder %.2f GB",
        q.PeakJobMemoryUsed / 1073741824.0, pm.PeakPagefileUsage / 1073741824.0, disco_max / 1e9);
    if (o.fim == kNumEtapas - 1 && !o.manter_work) {
      for (const auto& f : fs::directory_iterator(work))
        if (f.path().filename() != "logs") fs::remove_all(f.path());
      Log("work\\ removed (logs kept in work\\logs; use --keep-work to keep everything)");
    }
    Log("OK (%.0f s). Copy %s to the root of the SD card, next to the release files (XMOW.nro, xmow.toml).",
        Agora() - t_ini, U8(o.out / "switch").c_str());
  } catch (const Falha& f) {
    Log("ERROR: %s", f.msg.c_str());
    rc = 2;
  } catch (const std::exception& e) {
    Log("ERROR: %s", e.what());
    rc = 2;
  }
  if (g_log) std::fclose(g_log);
  Pausar(o);
  return rc;
}
