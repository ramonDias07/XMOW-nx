// XMOW - the port's menus, following the menu template shared by the author's ports (quick menu, advanced settings;
// the compiled reference is the Budokai 3 deko port, Budokai3-nx app/src/main.cpp).
//
//   + and - together : QUICK MENU   internal resolution (fixed), output resolution, official GPU boost, height fog,
//                                    FPS counter, mute, SAVE AND CLOSE, RESTORE DEFAULTS
//   L + R + Right    : ADVANCED     level 1 = categories + SAVE TO CONFIG; level 2 = the options of one category
//                                    (tables in xmow_menu_tabelas.h) + SAVE TO CONFIG
//   (L3 + R3 is the SDK's frame capture: not handled here, it keeps working with a menu open.)
//
// The deko3d presenter paints the box (rex/ui/superposicion.h: FijarSuperposicion, up to 16 rows of 44 columns, and
// FijarLetreiro, the scrolling help line under it); no ImGui, which would move presentation to the UI thread.
//
// Behaviour (template):
//  - Up / Down move the row (wrapping), Left / Right / A change the value (Left goes back), A on SAVE / RESTORE runs it.
//    Level 2 of the advanced menu: L / R switch category, B goes back to level 1; B on level 1 or in the quick menu
//    closes. Rows scroll in a 13-row window; the title shows row/total.
//  - Rows without (R) apply LIVE (the cvar is set at once). Rows with (R) are only read at start: the new value is kept
//    in the menu (pendientes) and only written on SAVE (the cvar too when the SDK accepts it after start; the init-only
//    ones only go to the config file).
//  - B closes WITHOUT saving: live changes stay applied until the game closes, unsaved (R) changes are dropped.
//  - While a menu is open the game sees the controller idle; after closing, the game's input stays blocked until every
//    button is released (the B that closed it never reaches the game), and the + - / L R Right chords never leak.
//  - SAVE (both menus) rewrites only the menu's keys inside xmow.toml, keeping every other line and comment (see
//    EscribirConfig: rex::cvar::SaveConfig is NOT used, it would rewrite the whole file).
//  - RESTORE DEFAULTS (quick menu, A twice) resets every cvar of both menus to its compiled-in default, the video mode
//    and saves.
//
// Threads: the filter runs on the input thread (Switch input driver, every poll of a connected controller). All menu
// state is behind one mutex; cvars are written with rex::cvar::SetFlagByName (the registry's own lock). No std::thread
// (ENOSYS in this toolchain).

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <fmt/format.h>

#include <rex/cvar.h>
#include <rex/input/filtro_mando.h>
#include <rex/logging.h>
#include <rex/ui/superposicion.h>

#if defined(__SWITCH__)
#include <switch.h>
#include <rex/ui/switch_saltynx.h>
#endif

#include "gow2_menu.h"
#include "gow2_resolution.h"
#include "xmow_menu_tabelas.h"

namespace gow2::menu {
namespace {

using namespace tablas;

// ---- Buttons (HidNpadButton_* bits, as the input driver passes them: the buttons HELD at this poll) ----
#if defined(__SWITCH__)
constexpr uint64_t kBotonA = HidNpadButton_A;
constexpr uint64_t kBotonB = HidNpadButton_B;
constexpr uint64_t kBotonL = HidNpadButton_L;
constexpr uint64_t kBotonR = HidNpadButton_R;
constexpr uint64_t kArriba = HidNpadButton_Up | HidNpadButton_StickLUp;
constexpr uint64_t kAbajo = HidNpadButton_Down | HidNpadButton_StickLDown;
constexpr uint64_t kIzquierda = HidNpadButton_Left | HidNpadButton_StickLLeft;
constexpr uint64_t kDerecha = HidNpadButton_Right | HidNpadButton_StickLRight;
constexpr uint64_t kAcordeRapido = HidNpadButton_Plus | HidNpadButton_Minus;                     // + and - together
constexpr uint64_t kAcordeAvanzado = HidNpadButton_L | HidNpadButton_R | HidNpadButton_Right;  // L + R + Right
#else
constexpr uint64_t kBotonA = 0, kBotonB = 0, kBotonL = 0, kBotonR = 0, kArriba = 0, kAbajo = 0, kIzquierda = 0,
                   kDerecha = 0, kAcordeRapido = 0, kAcordeAvanzado = 0;
#endif
constexpr uint64_t kControles = kArriba | kAbajo | kIzquierda | kDerecha | kBotonA | kBotonB | kBotonL | kBotonR;

constexpr size_t kColumnas = 44;  // overlay columns
constexpr int kVisibles = 13;     // rows between title and footer; more rows scroll

constexpr char kAyudaGuardarConfig[] = "WRITES THE CURRENT SETTINGS TO THE CONFIG FILE ON THE SD CARD.";
constexpr char kAvisoGuardado[] = "SAVED TO THE CONFIG FILE. (R) OPTIONS APPLY AFTER A RESTART.";
constexpr char kAvisoFallo[] = "COULD NOT WRITE THE CONFIG FILE. CHECK THE SD CARD.";
constexpr char kSufijoReinicio[] = " (R) NEEDS A RESTART TO APPLY.";

int Siguiente(int indice, int total, int direccion) { return (indice + total + direccion) % total; }

// Upper case (the font has only capitals) and cut to the box width with "...".
std::string TextoMenu(std::string_view fuente) {
  std::string texto(fuente);
  std::ranges::transform(texto, texto.begin(), [](char c) { return c >= 'a' && c <= 'z' ? char(c - 'a' + 'A') : c; });
  if (texto.size() > kColumnas) texto.replace(kColumnas - 3, std::string::npos, "...");
  return texto;
}

// First row of a window of `visibles` rows that keeps `fila` inside it (the selected row near the middle).
int InicioVentana(int fila, int total, int visibles) {
  return std::clamp(fila - visibles / 2, 0, std::max(0, total - visibles));
}

// "1" and "1.0" are the same value.
bool MismoValor(std::string_view a, std::string_view b) {
  if (a == b) return true;
  const std::string izquierda(a), derecha(b);
  char* fin_i = nullptr;
  char* fin_d = nullptr;
  const double x = std::strtod(izquierda.c_str(), &fin_i);
  const double y = std::strtod(derecha.c_str(), &fin_d);
  return !izquierda.empty() && !derecha.empty() && *fin_i == '\0' && *fin_d == '\0' && std::abs(x - y) < 1e-6;
}

// Docked or handheld, as the SDK's window decides it (Reverse-NX's mode when it is active).
bool EnBase() {
#if defined(__SWITCH__)
  return rex::ui::switch_saltynx::ModoBase(appletGetOperationMode() == AppletOperationMode_Console);
#else
  return false;
#endif
}

// =====================================================================================================================
// The keys of both menus and how they are read, set, saved and restored. Everything here runs with the menu mutex held.
// =====================================================================================================================

struct Clave {
  std::string nombre;
  std::string defecto;  // compiled-in default (FlagEntry::default_value)
  bool texto = false;   // string cvar: quoted in the TOML
};

struct Datos {
  std::filesystem::path config;                    // xmow.toml beside the NRO (XmowApp::OnConfigurePaths)
  std::vector<Clave> claves;                       // every cvar of both menus that exists in this build
  std::map<std::string, std::string> pendientes;   // (R) changes not saved yet (dropped when the menu closes)
  std::map<std::string, std::string> guardados;    // (R) values saved in this session (the cvar may not take them)
};
Datos& LosDatos() {
  static Datos d;
  return d;
}

const Clave* BuscarClave(std::string_view nombre) {
  for (const Clave& c : LosDatos().claves) {
    if (c.nombre == nombre) return &c;
  }
  return nullptr;
}
bool Existe(std::string_view nombre) { return BuscarClave(nombre) != nullptr; }

// What the menu shows and SAVE writes: an unsaved (R) change, else a saved (R) value, else the cvar.
std::string Valor(const std::string& nombre) {
  const Datos& d = LosDatos();
  if (const auto p = d.pendientes.find(nombre); p != d.pendientes.end()) return p->second;
  if (const auto g = d.guardados.find(nombre); g != d.guardados.end()) return g->second;
  return rex::cvar::GetFlagByName(nombre);
}
bool Cierto(const std::string& nombre) { return Valor(nombre) == "true"; }
int Numero(const std::string& nombre) { return std::atoi(Valor(nombre).c_str()); }

// Live change: the cvar now.
bool FijarVivo(const std::string& nombre, const std::string& valor) {
  const bool ok = rex::cvar::SetFlagByName(nombre, valor);
  if (ok) {
    REXLOG_INFO("[xmow menu] {}={}", nombre, valor);
  } else {
    REXLOG_WARN("[xmow menu] {}={} REJECTED by the cvar", nombre, valor);
  }
  return ok;
}

// (R) change: kept in the menu until SAVE.
void FijarReinicio(const std::string& nombre, const std::string& valor) {
  LosDatos().pendientes[nombre] = valor;
  REXLOG_INFO("[xmow menu] {}={} (R: pending until saved, applies after a restart)", nombre, valor);
}

std::string Recortar(std::string_view s) {
  const size_t a = s.find_first_not_of(" \t");
  if (a == std::string_view::npos) return {};
  const size_t b = s.find_last_not_of(" \t");
  return std::string(s.substr(a, b - a + 1));
}

std::string LineaToml(const Clave& c, const std::string& valor) {
  if (!c.texto) return c.nombre + " = " + valor;
  std::string escapado;
  for (const char ch : valor) {
    if (ch == '\\' || ch == '"') escapado += '\\';
    escapado += ch;
  }
  return c.nombre + " = \"" + escapado + "\"";
}

// Writes the menu's keys into xmow.toml WITHOUT rewriting the rest of it. rex::cvar::SaveConfig (sdk
// src/core/cvar.cpp) is not used: it truncates the file and writes back only "# Auto-generated" plus every cvar whose
// value differs from its default, so it would drop all the comments of xmow.toml, write cvars the port changes at run
// time (video_mode_width / height at 540p, for one), and write nothing at all when every cvar is at its default (a
// RESTORE DEFAULTS would leave the old file in place). Here:
//  - a top-level `key = value` line of a menu key is replaced with the current value (also when it is the default:
//    an explicit line in the file stays explicit);
//  - a menu key that is not in the file is appended only if its value differs from the default;
//  - every other line (comments, other keys, tables) is kept as it was, and the file's line ending (LF / CRLF) too.
bool EscribirConfig() {
  const Datos& d = LosDatos();
  std::vector<std::string> lineas;
  bool crlf = false;
  {
    std::ifstream entrada(d.config, std::ios::binary);
    std::string linea;
    while (std::getline(entrada, linea)) {
      if (!linea.empty() && linea.back() == '\r') {
        linea.pop_back();
        crlf = true;
      }
      lineas.push_back(std::move(linea));
    }
  }
  std::vector<bool> escrita(d.claves.size(), false);
  size_t tabla = lineas.size();  // first [table] header: the cvars are the keys before it
  constexpr std::string_view kCabecera = "# ---- Port menu (written by the + - and L + R + Right menus) ----";
  bool hay_cabecera = false;
  for (size_t i = 0; i < lineas.size() && i < tabla; ++i) {
    const std::string t = Recortar(lineas[i]);
    if (t == kCabecera) hay_cabecera = true;
    if (t.empty() || t[0] == '#') continue;
    if (t[0] == '[') {
      tabla = i;
      break;
    }
    const size_t igual = t.find('=');
    if (igual == std::string::npos) continue;
    const std::string clave = Recortar(std::string_view(t).substr(0, igual));
    for (size_t j = 0; j < d.claves.size(); ++j) {
      if (d.claves[j].nombre == clave) {
        lineas[i] = LineaToml(d.claves[j], Valor(d.claves[j].nombre));
        escrita[j] = true;
        break;
      }
    }
  }
  std::vector<std::string> nuevas;
  for (size_t j = 0; j < d.claves.size(); ++j) {
    if (escrita[j]) continue;
    const std::string valor = Valor(d.claves[j].nombre);
    if (!MismoValor(valor, d.claves[j].defecto)) nuevas.push_back(LineaToml(d.claves[j], valor));
  }
  const size_t anadidas = nuevas.size();
  if (!nuevas.empty()) {
    if (!hay_cabecera) {
      nuevas.insert(nuevas.begin(), std::string(kCabecera));
      if (tabla > 0 && !Recortar(lineas[tabla - 1]).empty()) nuevas.insert(nuevas.begin(), std::string());
    }
    lineas.insert(lineas.begin() + std::ptrdiff_t(tabla), nuevas.begin(), nuevas.end());
  }
  std::ofstream salida(d.config, std::ios::binary | std::ios::trunc);
  for (const std::string& linea : lineas) salida << linea << (crlf ? "\r\n" : "\n");
  salida.flush();
  const bool ok = bool(salida);
  if (ok) {
    REXLOG_INFO("[xmow menu] saved {} ({} menu keys, {} of them added to the file)", d.config.string(),
                d.claves.size(), anadidas);
  } else {
    REXLOG_ERROR("[xmow menu] could not write {}", d.config.string());
  }
  return ok;
}

// SAVE of both menus: the pending (R) changes become saved ones (and go to the cvar when it still takes them), then
// the config file.
bool GuardarTodo() {
  Datos& d = LosDatos();
  for (auto& [nombre, valor] : d.pendientes) {
    // Init-only cvars (mipmaps, compression, upload and geometry sizes...) refuse a change after start: they only go
    // to the file. That is expected, so no warning here.
    rex::cvar::SetFlagByName(nombre, valor);
    d.guardados[nombre] = valor;
  }
  d.pendientes.clear();
  return EscribirConfig();
}

// RESTORE DEFAULTS: every key of both menus to its compiled-in default, then saved.
bool RestaurarTodo() {
  Datos& d = LosDatos();
  d.pendientes.clear();
  for (const Clave& c : d.claves) {
    if (rex::cvar::SetFlagByName(c.nombre, c.defecto)) {
      d.guardados.erase(c.nombre);
      REXLOG_INFO("[xmow menu] restore {}={}", c.nombre, c.defecto);
    } else {
      d.guardados[c.nombre] = c.defecto;  // init-only: the default goes to the file, applies after a restart
      REXLOG_INFO("[xmow menu] restore {}={} (R: applies after a restart)", c.nombre, c.defecto);
    }
  }
  REXLOG_INFO("[xmow menu] restore defaults: {} cvars reset", d.claves.size());
  return EscribirConfig();
}

// =====================================================================================================================
// Quick menu (+ and -)
// =====================================================================================================================

constexpr std::array<int, 4> kSalidasBase{1080, 900, 720, 540};  // output heights docked
constexpr std::array<int, 3> kSalidasPortatil{720, 540, 360};    // ... and handheld
constexpr uint32_t kShaderNiebla = 2639;                         // full screen height fog (xmow_omitir_ps)

std::vector<uint32_t> LeerNumeros(std::string_view lista) {
  std::vector<uint32_t> salida;
  uint32_t v = 0;
  bool hay = false;
  for (const char c : std::string(lista) + ",") {
    if (c >= '0' && c <= '9') {
      v = v * 10 + uint32_t(c - '0');
      hay = true;
    } else if (hay) {
      salida.push_back(v);
      v = 0;
      hay = false;
    }
  }
  return salida;
}

class MenuRapido {
 public:
  // Once, at Instalar: the rows whose cvar exists in this build.
  void Preparar() {
    filas_.clear();
    filas_.push_back(kInterna);
    if (Existe("xmow_resolucao_saida")) filas_.push_back(kSalida);
    if (Existe("xmow_gpu_boost")) filas_.push_back(kBoost);
    if (Existe("xmow_omitir_ps")) filas_.push_back(kNiebla);
    if (Existe("switch_contador_fps")) filas_.push_back(kFps);
    if (Existe("audio_mute")) filas_.push_back(kSilencio);
    filas_.push_back(kGuardar);
    filas_.push_back(kRestaurar);
  }

  void Abrir() {
    confirmar_ = false;
    aviso_.clear();
    fila_ = std::clamp(fila_, 0, Total() - 1);
    Mostrar();
  }

  // `pulsados` = buttons newly pressed at this poll. Returns false when the menu must close (after SAVE AND CLOSE).
  bool Pulsar(uint64_t pulsados) {
    const bool confirmado = confirmar_ && Actual() == kRestaurar && pulsados == kBotonA;  // exactly A, nothing else
    confirmar_ = false;  // any press cancels a pending confirmation
    aviso_.clear();
    if (pulsados & kArriba) fila_ = Siguiente(fila_, Total(), -1);
    if (pulsados & kAbajo) fila_ = Siguiente(fila_, Total(), 1);
    if (pulsados & (kBotonA | kIzquierda | kDerecha)) {
      if (Actual() == kGuardar) {
        if (pulsados & kBotonA) {
          if (Guardar()) return false;
          aviso_ = kAvisoFallo;
        }
      } else if (Actual() == kRestaurar) {
        if (pulsados & kBotonA) {
          if (confirmado) {
            const bool ok = RestaurarTodo();
            REXLOG_INFO("[xmow menu] defaults restored{}", ok ? "" : " - SAVE FAILED");
            aviso_ = ok ? "FACTORY VALUES RESTORED AND SAVED. (R) OPTIONS APPLY AFTER A RESTART." : kAvisoFallo;
          } else {
            confirmar_ = true;
          }
        }
      } else {
        Cambiar(pulsados & kIzquierda ? -1 : 1);
      }
    }
    Mostrar();
    return true;
  }

 private:
  enum Fila { kInterna, kSalida, kBoost, kNiebla, kFps, kSilencio, kGuardar, kRestaurar };

  int Total() const { return int(filas_.size()); }
  Fila Actual() const { return filas_[size_t(fila_)]; }

  static std::span<const int> Salidas() {
    return EnBase() ? std::span<const int>(kSalidasBase) : std::span<const int>(kSalidasPortatil);
  }
  // The output height in use: a value that is not valid for the current mode is the mode's maximum (as the SDK does).
  static int AlturaSalida() {
    const auto lista = Salidas();
    const int valor = Numero("xmow_resolucao_saida");
    return std::ranges::find(lista, valor) == lista.end() ? lista.front() : valor;
  }
  static std::string Resolucion(int altura) { return fmt::format("{}X{}", altura * 16 / 9, altura); }  // 16:9
  static const char* SiNo(bool v) { return v ? "YES" : "NO"; }
  static bool NieblaVisible() {
    const auto lista = LeerNumeros(Valor("xmow_omitir_ps"));
    return std::ranges::find(lista, kShaderNiebla) == lista.end();
  }

  std::string Linea(Fila fila) const {
    switch (fila) {
      case kInterna: return "INTERNAL RESOLUTION: " + Resolucion(720);  // fixed: the real internal size is not implemented
      case kSalida: return "OUTPUT RESOLUTION: " + Resolucion(AlturaSalida());
      case kBoost:
        return std::string("OFFICIAL GPU BOOST: ") + SiNo(Cierto("xmow_gpu_boost")) + (EnBase() ? " (HANDHELD ONLY)" : "");
      case kNiebla: return std::string("HEIGHT FOG: ") + SiNo(NieblaVisible());
      case kFps: return std::string("FPS COUNTER: ") + SiNo(Cierto("switch_contador_fps"));
      case kSilencio: return std::string("MUTE AUDIO: ") + SiNo(Cierto("audio_mute"));
      case kGuardar: return "SAVE AND CLOSE";
      case kRestaurar: return confirmar_ ? "PRESS A AGAIN TO CONFIRM" : "RESTORE DEFAULTS";
    }
    return {};
  }

  // Marquee text of the selected row (adapted from the shared menu template).
  std::string Ayuda() const {
    if (!aviso_.empty()) return aviso_;
    switch (Actual()) {
      case kInterna:
        return "SIZE THE GAME DRAWS AT. ONLY 1280X720 FOR NOW: LOWER SIZES COME IN A LATER VERSION.";
      case kSalida:
        return EnBase() ? "SIZE OF THE IMAGE SENT TO THE SCREEN, SEPARATE FROM INTERNAL. IF IT IS BIGGER THAN "
                          "INTERNAL IT IS STRETCHED. DOCKED 1080P LOOKS BEST. HANDHELD HAS ITS OWN LIST."
                        : "SIZE OF THE IMAGE SENT TO THE SCREEN, SEPARATE FROM INTERNAL. IF IT IS BIGGER THAN "
                          "INTERNAL IT IS STRETCHED. HANDHELD 720P IS NATIVE. DOCKED HAS ITS OWN LIST.";
      case kBoost:
        return "HANDHELD ONLY. ASKS THE SYSTEM FOR ITS OFFICIAL 460 MHZ GPU MODE. DOES NOTHING DOCKED. NO OVERCLOCK. "
               "OFF BY DEFAULT.";
      case kNiebla:
        return "THE GAME'S DISTANCE FOG, A FULL SCREEN PASS. RIGHT NOW IT WASHES THE IMAGE BLUE WHITE, SO NO (OFF) IS "
               "RECOMMENDED. YES IS FOR TESTS.";
      case kFps:
        return "SHOWS THE FPS AND THE WORST FRAME GAP AT THE TOP LEFT. HANDY TO TEST SETTINGS, TURN OFF TO PLAY.";
      case kSilencio: return "TURNS ALL SOUND OFF.";
      case kGuardar:
        return "STORES THESE OPTIONS IN THE CONFIG FILE, THEN CLOSES.";
      case kRestaurar:
        return confirmar_ ? "PRESS A AGAIN TO CONFIRM. ANY OTHER KEY CANCELS."
                          : "RESETS EVERY OPTION OF BOTH MENUS TO FACTORY VALUES, THEN SAVES. NEEDS A SECOND PRESS OF A.";
    }
    return {};
  }

  // Box: title with row/total, the visible window of rows ("> " marks the selected one), footer; then the marquee.
  void Mostrar() const {
    const int inicio = InicioVentana(fila_, Total(), kVisibles);
    std::vector<std::string> lineas;
    lineas.push_back(fmt::format("WOLVERINE / PORT MENU  {}/{}", fila_ + 1, Total()));
    for (int i = inicio; i < std::min(inicio + kVisibles, Total()); ++i) {
      lineas.push_back(TextoMenu((i == fila_ ? "> " : "  ") + Linea(filas_[size_t(i)])));
    }
    lineas.emplace_back("ARROWS: ADJUST / A: CHANGE / B: CLOSE");
    rex::ui::FijarSuperposicion(std::move(lineas));
    rex::ui::FijarLetreiro(Ayuda());
  }

  template <typename Lista>
  static int Ciclo(const Lista& valores, int valor, int direccion) {
    const auto actual = std::ranges::find(valores, valor);
    if (actual == std::ranges::end(valores)) return *std::ranges::begin(valores);
    const int total = int(std::ranges::size(valores));
    return valores[size_t((int(actual - std::ranges::begin(valores)) + total + direccion) % total)];
  }

  void Cambiar(int direccion) {
    auto alternar = [](const char* nombre) { FijarVivo(nombre, Cierto(nombre) ? "false" : "true"); };
    switch (Actual()) {
      case kInterna: break;  // fixed at 1280X720 for now
      case kSalida:
        FijarVivo("xmow_resolucao_saida", std::to_string(Ciclo(Salidas(), AlturaSalida(), direccion)));
        break;
      case kBoost: alternar("xmow_gpu_boost"); break;
      case kNiebla: {
        // ON = 2639 taken out of xmow_omitir_ps, OFF = 2639 put back; the other numbers in the list stay.
        auto lista = LeerNumeros(Valor("xmow_omitir_ps"));
        const auto it = std::ranges::find(lista, kShaderNiebla);
        if (it == lista.end()) {
          lista.push_back(kShaderNiebla);
        } else {
          lista.erase(it);
        }
        std::string texto;
        for (const uint32_t n : lista) texto += (texto.empty() ? "" : ",") + std::to_string(n);
        FijarVivo("xmow_omitir_ps", texto);  // gow2_gpu.cpp re-reads the list on change (live)
        break;
      }
      case kFps: alternar("switch_contador_fps"); break;
      case kSilencio: alternar("audio_mute"); break;
      default: break;
    }
  }

  bool Guardar() {
    const bool config = GuardarTodo();
    REXLOG_INFO("[xmow menu] save and close: config ({})", config ? "ok" : "FAILED");
    return config;
  }

  std::vector<Fila> filas_;
  int fila_ = 0;
  bool confirmar_ = false;
  std::string aviso_;   // replaces the help until the next press (restore done, save failed)
};

// =====================================================================================================================
// Advanced settings (L + R + Right)
// =====================================================================================================================

class MenuAvanzado {
 public:
  // Once, at Instalar: the categories with the options whose cvar exists (empty categories are left out).
  void Preparar() {
    categorias_.clear();
    for (const AdvancedCategory& c : kCategories) {
      Categoria categoria{&c, {}};
      for (const AdvancedOption& o : c.options) {
        if (Existe(o.cvar)) categoria.opciones.push_back(&o);
      }
      if (!categoria.opciones.empty()) categorias_.push_back(std::move(categoria));
    }
  }

  void Abrir() {
    nivel_ = 1;
    fila_ = std::clamp(categoria_, 0, Total1() - 1);
    aviso_.clear();
    Mostrar();
  }

  // B: from the options back to the categories. Returns false when the menu must close (B at level 1).
  bool Atras() {
    if (nivel_ == 1) return false;
    nivel_ = 1;
    fila_ = categoria_;
    aviso_.clear();
    Mostrar();
    return true;
  }

  bool Pulsar(uint64_t pulsados) {
    aviso_.clear();
    if (nivel_ == 1) {
      // Level 1: the categories, then SAVE TO CONFIG at index Categorias().
      if (pulsados & kArriba) fila_ = Siguiente(fila_, Total1(), -1);
      if (pulsados & kAbajo) fila_ = Siguiente(fila_, Total1(), 1);
      if (pulsados & kBotonA) {
        if (fila_ == Categorias()) {
          Guardar();
        } else {
          categoria_ = fila_;
          nivel_ = 2;
          fila_ = 0;
        }
      }
      Mostrar();
      return true;
    }
    // Level 2: L / R jump to the previous / next category.
    if (pulsados & (kBotonL | kBotonR)) {
      categoria_ = Siguiente(categoria_, Categorias(), pulsados & kBotonL ? -1 : 1);
      fila_ = 0;
    }
    const auto& opciones = categorias_[size_t(categoria_)].opciones;
    const int guardar = int(opciones.size());  // the SAVE TO CONFIG row
    if (pulsados & kArriba) fila_ = Siguiente(fila_, guardar + 1, -1);
    if (pulsados & kAbajo) fila_ = Siguiente(fila_, guardar + 1, 1);
    if (fila_ == guardar) {
      if (pulsados & kBotonA) Guardar();
    } else if (pulsados & (kBotonA | kIzquierda | kDerecha)) {
      const AdvancedOption& o = *opciones[size_t(fila_)];
      const int actual = IndiceEleccion(o, Valor(o.cvar));
      const int siguiente =
          actual < 0 ? 0 : Siguiente(actual, int(o.choices.size()), pulsados & kIzquierda ? -1 : 1);
      const std::string valor(o.choices[size_t(siguiente)].value);
      if (o.restart) {
        FijarReinicio(o.cvar, valor);
      } else {
        FijarVivo(o.cvar, valor);
      }
    }
    Mostrar();
    return true;
  }

 private:
  struct Categoria {
    const AdvancedCategory* datos;
    std::vector<const AdvancedOption*> opciones;
  };

  int Categorias() const { return int(categorias_.size()); }
  int Total1() const { return Categorias() + 1; }  // + SAVE TO CONFIG

  // -1 when the value is not in the option's list (the menu then shows it raw).
  static int IndiceEleccion(const AdvancedOption& o, std::string_view valor) {
    for (size_t i = 0; i < o.choices.size(); ++i) {
      if (MismoValor(o.choices[i].value, valor)) return int(i);
    }
    return -1;
  }

  void Guardar() {
    const bool ok = GuardarTodo();
    REXLOG_INFO("[xmow menu] save to config ({})", ok ? "ok" : "FAILED");
    aviso_ = ok ? kAvisoGuardado : kAvisoFallo;
  }

  void Mostrar() const {
    std::vector<std::string> lineas;
    std::string ayuda;
    if (nivel_ == 1) {
      lineas.emplace_back("SETTINGS / CATEGORIES");
      for (int i = 0; i < Categorias(); ++i) {
        lineas.push_back(TextoMenu(std::string(i == fila_ ? "> " : "  ") + categorias_[size_t(i)].datos->name));
      }
      lineas.push_back(std::string(fila_ == Categorias() ? "> " : "  ") + "SAVE TO CONFIG");
      lineas.emplace_back("ARROWS: SELECT / A: OPEN / B: CLOSE");
      ayuda = fila_ == Categorias() ? kAyudaGuardarConfig : categorias_[size_t(fila_)].datos->help;
    } else {
      const Categoria& c = categorias_[size_t(categoria_)];
      const int total = int(c.opciones.size()) + 1;  // + SAVE TO CONFIG
      const int inicio = InicioVentana(fila_, total, kVisibles);
      lineas.push_back(TextoMenu(fmt::format("SETTINGS / {}  {}/{}", c.datos->name, fila_ + 1, total)));
      for (int i = inicio; i < std::min(inicio + kVisibles, total); ++i) {
        if (i == total - 1) {
          lineas.push_back(std::string(i == fila_ ? "> " : "  ") + "SAVE TO CONFIG");
          continue;
        }
        const AdvancedOption& o = *c.opciones[size_t(i)];
        const std::string valor = Valor(o.cvar);
        const int elegido = IndiceEleccion(o, valor);
        std::string linea = fmt::format("{}{}: {}", i == fila_ ? "> " : "  ", o.label,
                                        elegido < 0 ? std::string_view(valor) : o.choices[size_t(elegido)].label);
        if (o.restart) linea += " (R)";
        lineas.push_back(TextoMenu(linea));
      }
      lineas.emplace_back("A: CHANGE / L/R: CATEGORY / B: BACK");
      if (fila_ == total - 1) {
        ayuda = kAyudaGuardarConfig;
      } else {
        const AdvancedOption& o = *c.opciones[size_t(fila_)];
        ayuda = std::string(o.help) + (o.restart ? kSufijoReinicio : "");
      }
    }
    if (!aviso_.empty()) ayuda = aviso_;  // "SAVED TO THE CONFIG FILE..." until the next move
    rex::ui::FijarSuperposicion(std::move(lineas));
    rex::ui::FijarLetreiro(std::move(ayuda));
  }

  std::vector<Categoria> categorias_;
  int nivel_ = 1;
  int categoria_ = 0;
  int fila_ = 0;
  std::string aviso_;
};

// =====================================================================================================================
// The controller filter: chords, menu state, input blocking
// =====================================================================================================================

enum class Menu { kCerrado, kRapido, kAvanzado };

struct Atajo {
  uint64_t acorde;
  Menu menu;
};
constexpr Atajo kAtajos[] = {
    {kAcordeRapido, Menu::kRapido},
    {kAcordeAvanzado, Menu::kAvanzado},
};

struct Estado {
  std::mutex candado;
  bool instalado = false;
  Menu menu = Menu::kCerrado;
  MenuRapido rapido;
  MenuAvanzado avanzado;
  std::array<uint64_t, 8> antes{};   // buttons held at each slot's previous poll
  std::array<bool, 8> soltar{};      // after closing: the slot stays blocked until all its buttons are released
};
Estado& ElEstado() {
  static Estado e;
  return e;
}

void Cerrar(Estado& e) {
  e.menu = Menu::kCerrado;
  e.soltar.fill(true);
  Datos& d = LosDatos();
  if (!d.pendientes.empty()) {
    REXLOG_INFO("[xmow menu] {} unsaved (R) change(s) dropped", d.pendientes.size());
    d.pendientes.clear();
  }
  rex::ui::FijarSuperposicion({});
  rex::ui::FijarLetreiro({});
  REXLOG_INFO("[xmow menu] closed");
}

void Alternar(Estado& e, Menu menu) {
  if (e.menu == menu) {
    Cerrar(e);
    return;
  }
  LosDatos().pendientes.clear();  // switching menus drops unsaved (R) changes, as closing does
  e.menu = menu;
  e.antes.fill(UINT64_MAX);  // what is held now (the chord) does not count as a new press
  e.soltar.fill(false);
  if (menu == Menu::kRapido) {
    e.rapido.Abrir();
    REXLOG_INFO("[xmow menu] quick menu opened");
  } else {
    e.avanzado.Abrir();
    REXLOG_INFO("[xmow menu] advanced settings opened");
  }
}

bool Filtro(size_t ranura, uint64_t botones) {
  Estado& e = ElEstado();
  std::lock_guard<std::mutex> cerrojo(e.candado);
  if (!e.instalado) return false;
  if (ranura >= e.antes.size()) return e.menu != Menu::kCerrado;
  const uint64_t antes = std::exchange(e.antes[ranura], botones);
  if (e.soltar[ranura]) {
    if (botones == 0) e.soltar[ranura] = false;
    return true;
  }
  // A held chord is always swallowed; only its rising edge opens / closes / switches a menu.
  for (const Atajo& atajo : kAtajos) {
    if (atajo.acorde != 0 && (botones & atajo.acorde) == atajo.acorde) {
      if ((antes & atajo.acorde) != atajo.acorde) Alternar(e, atajo.menu);
      return true;
    }
  }
  if (e.menu == Menu::kCerrado) return false;
  const uint64_t pulsados = botones & ~antes & kControles;
  if (pulsados & kBotonB) {
    if (e.menu != Menu::kAvanzado || !e.avanzado.Atras()) Cerrar(e);
  } else if (pulsados) {
    if (e.menu == Menu::kRapido && !e.rapido.Pulsar(pulsados)) Cerrar(e);
    if (e.menu == Menu::kAvanzado && !e.avanzado.Pulsar(pulsados)) Cerrar(e);
  }
  return true;  // the game does not see the controller while a menu is open
}

}  // namespace

void Instalar(const std::filesystem::path& config) {
  Estado& e = ElEstado();
  {
    std::lock_guard<std::mutex> cerrojo(e.candado);
    Datos& d = LosDatos();
    d.config = config;
    d.claves.clear();
    auto anadir = [&d](const char* nombre) {
      if (BuscarClave(nombre)) return;  // listed in both menus (audio_mute, xmow_omitir_ps)
      const rex::cvar::FlagEntry* info = rex::cvar::GetFlagInfo(nombre);
      if (!info) {
        REXLOG_WARN("[xmow menu] cvar {} is not in this build: its row is hidden", nombre);
        return;
      }
      d.claves.push_back({info->name, info->default_value, info->type == rex::cvar::FlagType::String});
    };
    for (const char* nombre : kQuickCvars) anadir(nombre);
    for (const AdvancedCategory& c : kCategories) {
      for (const AdvancedOption& o : c.options) anadir(o.cvar);
    }
    e.rapido.Preparar();
    e.avanzado.Preparar();
    e.instalado = true;
    REXLOG_INFO("[xmow menu] installed: + and - quick menu, L + R + Right advanced settings; {} cvars; config {}",
                d.claves.size(), config.string());
  }
  rex::input::FijarFiltroMando(&Filtro);
}

}  // namespace gow2::menu
