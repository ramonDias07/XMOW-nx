// XMOW - the port's menus, following the menu template shared by the author's ports (quick menu, advanced settings;
// the compiled reference is the Budokai 3 deko port, Budokai3-nx app/src/main.cpp).
//
//   + and - together : QUICK MENU   internal resolution, shadows, output resolution, official GPU boost,
//                                    height fog, FPS counter, mute, SAVE AND CLOSE, RESTORE DEFAULTS
//   L + R + Right    : ADVANCED     level 1 = categories + SAVE TO CONFIG; level 2 = the options of one category
//                                    (tables in xmow_menu_tabelas.h) + SAVE TO CONFIG
//   L + R + Left     : ACHIEVEMENTS 6 per page with icons, A = detail card, B = back / close, unlock toast
//                                    (catalog, state and icons from the game's own XDBF at run time)
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
#include <atomic>
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
#include <tuple>
#include <utility>
#include <vector>

#include <fmt/format.h>

#include <rex/cvar.h>
#include <rex/input/filtro_mando.h>
#include <rex/logging.h>
#include <rex/system/achievement_manager.h>
#include <rex/system/kernel_state.h>
#include <rex/ui/superposicion.h>

#if defined(__SWITCH__)
#include <switch.h>
#include <rex/ui/switch_saltynx.h>
#endif

#include "gow2_menu.h"
#include "xmow_bench.h"
#include "xmow_menu_tabelas.h"

// 0.1.33d: a live change restarts the 10 s report windows (the port's and the renderer's in sdk-g2 destinos.cpp, the
// profiler's in switch_perf.cpp), so each side of an A/B made with the menu open has whole windows.
namespace rex::x360 {
extern std::atomic<uint32_t> g_xmow_reinicio_informes;
}
#if defined(__SWITCH__)
extern "C" void RexSwitchPerfReiniciarVentana(void);
#endif

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
constexpr uint64_t kAcordeConquistas = HidNpadButton_L | HidNpadButton_R | HidNpadButton_Left;  // L + R + Left
#else
constexpr uint64_t kBotonA = 0, kBotonB = 0, kBotonL = 0, kBotonR = 0, kArriba = 0, kAbajo = 0, kIzquierda = 0,
                   kDerecha = 0, kAcordeRapido = 0, kAcordeAvanzado = 0, kAcordeConquistas = 0;
#endif
constexpr uint64_t kControles = kArriba | kAbajo | kIzquierda | kDerecha | kBotonA | kBotonB | kBotonL | kBotonR;

constexpr size_t kColumnas = 44;  // overlay columns
constexpr int kVisibles = 13;     // rows between title and footer; more rows scroll

constexpr char kAyudaGuardarConfig[] = "WRITES THE CURRENT SETTINGS TO THE CONFIG FILE ON THE SD CARD.";
constexpr char kAvisoGuardado[] = "SAVED TO THE CONFIG FILE. (R) OPTIONS APPLY AFTER A RESTART.";
constexpr char kAvisoFallo[] = "COULD NOT WRITE THE CONFIG FILE. CHECK THE SD CARD.";
constexpr char kSufijoReinicio[] = " (R) NEEDS A RESTART TO APPLY.";
constexpr char kAyudaPruebas[] =
    "AUTOMATIC TESTS FROM XMOW BENCH.TOML BESIDE XMOW.NRO. PAUSE THE GAME FIRST, THEN A STARTS A TEST: IT CHANGES THE "
    "OPTIONS BY ITSELF, MEASURES AND PUTS EVERYTHING BACK. THE CORNER SHOWS THE STEP, THEN DONE. B ABORTS. NOTHING IS "
    "SAVED. A LIVE TEST IS PLAYED NORMALLY; A ON TESTS STOPS IT.";

int Siguiente(int indice, int total, int direccion) { return (indice + total + direccion) % total; }

// Upper case (the font has only capitals) and cut to `maximo` characters (the box width by default) with "...".
// Control characters (a line break inside a game string) become spaces: the overlay uses them as markers.
std::string TextoMenu(std::string_view fuente, size_t maximo = kColumnas) {
  std::string texto(fuente);
  std::ranges::transform(texto, texto.begin(), [](char c) {
    if (static_cast<unsigned char>(c) < 0x20) return ' ';
    return c >= 'a' && c <= 'z' ? char(c - 'a' + 'A') : c;
  });
  if (texto.size() > maximo) texto.replace(maximo - 3, std::string::npos, "...");
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
  rex::system::AchievementManager* conquistas = nullptr;  // the kernel's catalog / unlock state (null: no menu)
  rex::system::AchievementListenerHandle oyente = 0;      // its unlock toast listener
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
    XMOW_LOG_CONTEXTO("[xmow menu] {}={} (report windows restart here)", nombre, valor);
    rex::x360::g_xmow_reinicio_informes.fetch_add(1, std::memory_order_relaxed);
#if defined(__SWITCH__)
    RexSwitchPerfReiniciarVentana();
#endif
  } else {
    REXLOG_WARN("[xmow menu] {}={} REJECTED by the cvar", nombre, valor);
  }
  return ok;
}

// (R) change: kept in the menu until SAVE.
void FijarReinicio(const std::string& nombre, const std::string& valor) {
  LosDatos().pendientes[nombre] = valor;
  XMOW_LOG_CONTEXTO("[xmow menu] {}={} (R: pending until saved, applies after a restart)", nombre, valor);
}

// 0.1.34 (the bench, xmow_bench.cpp): a test may change only a live row of the advanced menu that this build has.
// 0.1.34b: the value must be of the cvar's type (a float for an integer option would be cut silently); double options
// are refused (their text form keeps 6 decimals, so the value before the test could not be put back exactly).
std::string ValidarBench(const std::string& cvar, char tipo) {
  for (const AdvancedCategory& c : kCategories) {
    for (const AdvancedOption& o : c.options) {
      if (cvar != o.cvar) continue;
      if (o.restart) return "(R) OPTION " + cvar;
      if (!Existe(cvar)) return "NOT IN THIS BUILD: " + cvar;
      const rex::cvar::FlagEntry* info = rex::cvar::GetFlagInfo(cvar);
      if (!info) return "NOT IN THIS BUILD: " + cvar;
      using T = rex::cvar::FlagType;
      switch (info->type) {
        case T::Boolean:
          return tipo == 'b' ? std::string() : "NOT A TRUE/FALSE VALUE FOR " + cvar;
        case T::Int32:
        case T::Int64:
        case T::Uint32:
        case T::Uint64:
          return tipo == 'i' ? std::string() : "NOT A WHOLE NUMBER FOR " + cvar;
        case T::String:
          return tipo == 's' ? std::string() : "NOT A TEXT VALUE FOR " + cvar;
        default:
          return "OPTION TYPE NOT SUPPORTED: " + cvar;
      }
    }
  }
  return "NOT A MENU OPTION: " + cvar;
}

// 0.1.34b (review S9): rows that do nothing while PLAY MODE is on.
bool InactivaEnModoJogo(std::string_view cvar) {
  return xmow::bench::ModoJogoEfetivo() && (cvar == "xmow_amostragem_pilas" || cvar == "xmow_log_hitch_ms");
}
std::filesystem::path ArchivoBench() { return LosDatos().config.parent_path() / "xmow_bench.toml"; }

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

// Writes the menu's keys into xmow.toml WITHOUT rewriting the rest of it. rex::cvar::SaveConfig (sdk-g2
// src/core/cvar.cpp) is not used: it truncates the file and writes back only "# Auto-generated" plus every cvar whose
// value differs from its default, so it would drop all the comments of xmow.toml, write cvars the port changes at run
// time (video_mode_width / height at 540p, for one), and write nothing at all when every cvar is at its default (a
// RESTORE DEFAULTS would leave the old file in place). Here:
//  - a top-level `key = value` line of a menu key is replaced with the current value (also when it is the default:
//    an explicit line in the file stays explicit);
//  - a menu key that is not in the file is appended only if its value differs from the default;
//  - every other line (comments, other keys, tables) is kept as it was, and the file's line ending (LF / CRLF) too.
// 0.1.33c (review F3): keys the menu shows but never stores (an image-breaking probe): written as their default.
bool NuncaGuardar(const std::string& nombre) { return nombre == "xmow_diag_custo_dibujo"; }

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
        lineas[i] = LineaToml(d.claves[j], NuncaGuardar(d.claves[j].nombre) ? d.claves[j].defecto
                                                                              : Valor(d.claves[j].nombre));
        escrita[j] = true;
        break;
      }
    }
  }
  std::vector<std::string> nuevas;
  for (size_t j = 0; j < d.claves.size(); ++j) {
    if (escrita[j] || NuncaGuardar(d.claves[j].nombre)) continue;
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
    XMOW_LOG_CONTEXTO("[xmow menu] saved {} ({} menu keys, {} of them added to the file)", d.config.string(),
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
      XMOW_LOG_CONTEXTO("[xmow menu] restore {}={}", c.nombre, c.defecto);
    } else {
      d.guardados[c.nombre] = c.defecto;  // init-only: the default goes to the file, applies after a restart
      XMOW_LOG_CONTEXTO("[xmow menu] restore {}={} (R: applies after a restart)", c.nombre, c.defecto);
    }
  }
  XMOW_LOG_CONTEXTO("[xmow menu] restore defaults: {} cvars reset", d.claves.size());
  return EscribirConfig();
}

// =====================================================================================================================
// Quick menu (+ and -)
// =====================================================================================================================

// Output heights (window_switch.cpp accepts the same lists). 612 / 480 / 360 match the internal resolutions.
constexpr std::array<int, 7> kSalidasBase{1080, 900, 720, 612, 540, 480, 360};  // docked
constexpr std::array<int, 5> kSalidasPortatil{720, 612, 540, 480, 360};         // handheld
constexpr uint32_t kShaderNiebla = 2639;  // full screen height fog (xmow_niebla_altura; in xmow_omitir_ps: always off)

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
    if (Existe("xmow_escala_interna")) filas_.push_back(kInterna);
    if (Existe("xmow_sombras")) filas_.push_back(kSombras);
    if (Existe("xmow_resolucao_saida")) filas_.push_back(kSalida);
    if (Existe("xmow_gpu_boost")) filas_.push_back(kBoost);
    if (Existe("xmow_distancia_corte")) filas_.push_back(kDistancia);  // 0.1.28
    if (Existe("xmow_corte_pequenos")) filas_.push_back(kPequenos);    // 0.1.29 (MESH DETAIL: advanced menu only)
    if (Existe("xmow_iluminacao")) filas_.push_back(kLuzes);           // 0.1.28
    if (Existe("xmow_niebla_altura")) filas_.push_back(kNiebla);
    if (Existe("switch_contador_fps")) filas_.push_back(kFps);
    if (Existe("audio_mute")) filas_.push_back(kSilencio);
    if (Existe("language")) filas_.push_back(kIdioma);  // 0.1.34f
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
            XMOW_LOG_CONTEXTO("[xmow menu] defaults restored{}", ok ? "" : " - SAVE FAILED");
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
  enum Fila { kInterna, kSombras, kSalida, kBoost, kDistancia, kPequenos, kLuzes, kNiebla, kFps, kSilencio, kIdioma, kGuardar, kRestaurar };

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
  // 16:9 with the width rounded up to even, as window_switch.cpp sizes the swapchain (480 -> 854X480).
  static std::string Resolucion(int altura) { return fmt::format("{}X{}", (altura * 16 / 9 + 1) / 2 * 2, altura); }
  static const char* SiNo(bool v) { return v ? "YES" : "NO"; }
  // The label of a cvar's value in a choice list (the raw value when it is not in the list).
  static std::string Eleccion(std::span<const SettingChoice> lista, const char* cvar) {
    const std::string valor = Valor(cvar);
    for (const SettingChoice& c : lista) {
      if (MismoValor(c.value, valor)) return std::string(c.label);
    }
    return valor;
  }
  // The next value of a choice list (the first one when the current value is not in it).
  static std::string SiguienteEleccion(std::span<const SettingChoice> lista, const char* cvar, int direccion) {
    const std::string valor = Valor(cvar);
    for (size_t i = 0; i < lista.size(); ++i) {
      if (MismoValor(lista[i].value, valor)) {
        return std::string(lista[size_t(Siguiente(int(i), int(lista.size()), direccion))].value);
      }
    }
    return std::string(lista.front().value);
  }
  // 2639 in xmow_omitir_ps (a config of 0.1.18 or older, or SKIPPED SHADERS = FOG) leaves the fog out whatever
  // xmow_niebla_altura says: shown as NO until this row is changed (which takes 2639 out of the list).
  static bool NieblaEnListaOmitir() {
    const auto lista = LeerNumeros(Valor("xmow_omitir_ps"));
    return std::ranges::find(lista, kShaderNiebla) != lista.end();
  }
  static std::string EstadoNiebla() {
    return NieblaEnListaOmitir() ? "NO (SKIPPED SHADERS)" : Eleccion(kHeightFog, "xmow_niebla_altura");
  }

  std::string Linea(Fila fila) const {
    switch (fila) {
      // xmow_escala_interna is live (xmow_resolucao_interna.cpp applies it at the next game frame); xmow_sombras is latched at start (0.1.22).
      case kInterna: return "INTERNAL RESOLUTION: " + Eleccion(kInternalScale, "xmow_escala_interna");
      case kSombras: return "SHADOWS: " + Eleccion(kShadows, "xmow_sombras");
      case kSalida: return "OUTPUT RESOLUTION: " + Resolucion(AlturaSalida());
      case kBoost:
        return std::string("OFFICIAL GPU BOOST: ") + SiNo(Cierto("xmow_gpu_boost")) + (EnBase() ? " (HANDHELD ONLY)" : "");
      case kDistancia: return "VIEW DISTANCE: " + Eleccion(kViewDistance, "xmow_distancia_corte");
      case kPequenos: return "SMALL OBJECTS: " + Eleccion(kSmallObjects, "xmow_corte_pequenos");
      case kLuzes: return "LIGHTING: " + Eleccion(kLighting, "xmow_iluminacao");
      case kNiebla: return "HEIGHT FOG: " + EstadoNiebla();
      case kFps: return std::string("FPS COUNTER: ") + SiNo(Cierto("switch_contador_fps"));
      case kSilencio: return std::string("MUTE AUDIO: ") + SiNo(Cierto("audio_mute"));
      case kIdioma: return "LANGUAGE: " + Eleccion(kLanguage, "language");
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
        return "SIZE THE GAME DRAWS THE 3D VIEW AT, THEN THE GAME STRETCHES IT TO 1280X720. HUD AND TEXT STAY AT 720. "
               "LOWER IS FASTER AND SOFTER. APPLIES AT ONCE, SAVE AND CLOSE KEEPS IT.";
      case kSombras:
        return "ALL = THE GAME'S SHADOWS. REDUCED = HALF SHADOW RESOLUTION AND SIMPLER FILTERING. OFF = NO DYNAMIC "
               "SHADOWS, FASTEST. APPLIES AT THE NEXT START, SAVE AND CLOSE KEEPS IT.";
      case kSalida:
        return EnBase() ? "SIZE OF THE IMAGE SENT TO THE SCREEN, SEPARATE FROM INTERNAL. IF IT IS BIGGER THAN "
                          "INTERNAL IT IS STRETCHED. DOCKED 1080P LOOKS BEST. HANDHELD HAS ITS OWN LIST."
                        : "SIZE OF THE IMAGE SENT TO THE SCREEN, SEPARATE FROM INTERNAL. IF IT IS BIGGER THAN "
                          "INTERNAL IT IS STRETCHED. HANDHELD 720P IS NATIVE. DOCKED HAS ITS OWN LIST.";
      case kBoost:
        return "HANDHELD ONLY. ASKS THE SYSTEM FOR ITS OFFICIAL 460 MHZ GPU MODE. DOES NOTHING DOCKED. NO OVERCLOCK. "
               "OFF BY DEFAULT.";
      case kDistancia: return std::string(kAyudaViewDistance);
      case kPequenos: return std::string(kAyudaSmallObjects);
      case kLuzes: return std::string(kAyudaLighting);
      case kNiebla:
        return "THE GAME'S HEIGHT FOG, A FULL SCREEN PASS. AUTO DRAWS IT EVERYWHERE, THE MAIN MENU TOO (ITS BLUE "
               "WATER LOOK). YES = ALWAYS. NO = OFF IN GAMEPLAY ONLY, A LITTLE FASTER; THE TITLE AND MENUS KEEP IT. "
               "APPLIES AT ONCE, SAVE AND CLOSE KEEPS IT.";
      case kFps:
        return "SHOWS THE FPS AND THE WORST FRAME GAP AT THE TOP LEFT. HANDY TO TEST SETTINGS, TURN OFF TO PLAY.";
      case kSilencio: return "TURNS ALL SOUND OFF.";
      case kIdioma: return std::string(kAyudaLanguage);
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
      // Live: the game picks the new value up at its next frame; SAVE AND CLOSE writes it to xmow.toml.
      case kInterna:
        FijarVivo("xmow_escala_interna", SiguienteEleccion(kInternalScale, "xmow_escala_interna", direccion));
        break;
      case kSombras: FijarVivo("xmow_sombras", SiguienteEleccion(kShadows, "xmow_sombras", direccion)); break;
      case kSalida:
        FijarVivo("xmow_resolucao_saida", std::to_string(Ciclo(Salidas(), AlturaSalida(), direccion)));
        break;
      case kBoost: alternar("xmow_gpu_boost"); break;
      case kDistancia:
        FijarVivo("xmow_distancia_corte", SiguienteEleccion(kViewDistance, "xmow_distancia_corte", direccion));
        break;
      case kPequenos:
        FijarVivo("xmow_corte_pequenos", SiguienteEleccion(kSmallObjects, "xmow_corte_pequenos", direccion));
        break;
      case kLuzes: FijarVivo("xmow_iluminacao", SiguienteEleccion(kLighting, "xmow_iluminacao", direccion)); break;
      case kNiebla: {
        // AUTO / YES / NO in xmow_niebla_altura (gow2_gpu.cpp reads it at each fog draw: live). A 2639 left in
        // xmow_omitir_ps (0.1.18's HEIGHT FOG: NO) is taken out first, the other numbers in the list stay; that
        // first press goes to AUTO (the default), and from then on the row rules alone.
        auto lista = LeerNumeros(Valor("xmow_omitir_ps"));
        const auto it = std::ranges::find(lista, kShaderNiebla);
        if (it != lista.end()) {
          lista.erase(it);
          std::string texto;
          for (const uint32_t n : lista) texto += (texto.empty() ? "" : ",") + std::to_string(n);
          FijarVivo("xmow_omitir_ps", texto);  // gow2_gpu.cpp re-reads the list on change (live)
          FijarVivo("xmow_niebla_altura", "0");
        } else {
          FijarVivo("xmow_niebla_altura", SiguienteEleccion(kHeightFog, "xmow_niebla_altura", direccion));
        }
        break;
      }
      case kFps: alternar("switch_contador_fps"); break;
      case kSilencio: alternar("audio_mute"); break;
      // 0.1.34f: read once at start (the game asks for its language at boot): kept until SAVE, applies after a restart.
      case kIdioma: FijarReinicio("language", SiguienteEleccion(kLanguage, "language", direccion)); break;
      default: break;
    }
  }

  bool Guardar() {
    const bool config = GuardarTodo();
    XMOW_LOG_CONTEXTO("[xmow menu] save and close: config ({})", config ? "ok" : "FAILED");
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
    const bool pruebas = nivel_ == 3;
    nivel_ = 1;
    fila_ = pruebas ? Categorias() : categoria_;
    aviso_.clear();
    Mostrar();
    return true;
  }

  bool Pulsar(uint64_t pulsados) {
    aviso_.clear();
    if (nivel_ == 3) return PulsarPruebas(pulsados);
    if (nivel_ == 1) {
      // Level 1: the categories, then TESTS (0.1.34) at index Categorias() and SAVE TO CONFIG after it.
      if (pulsados & kArriba) fila_ = Siguiente(fila_, Total1(), -1);
      if (pulsados & kAbajo) fila_ = Siguiente(fila_, Total1(), 1);
      if (pulsados & kBotonA) {
        if (fila_ == Categorias() + 1) {
          Guardar();
        } else if (fila_ == Categorias()) {
          AbrirPruebas();
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
        if (InactivaEnModoJogo(o.cvar)) aviso_ = "NO EFFECT IN PLAY MODE: SET LOGS AND DIAGNOSTICS > PLAY MODE TO NO.";
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
  int Total1() const { return Categorias() + 2; }  // + TESTS + SAVE TO CONFIG

  // 0.1.34: the tests page (level 3), the [[test]] tables of xmow_bench.toml beside the NRO, read when it opens.
  void AbrirPruebas() {
    if (xmow::bench::Activo()) {  // a live test runs (a blocking one keeps the menu closed): A on TESTS stops it
      xmow::bench::Abortar("stopped from the menu");
      aviso_ = "THE RUNNING TEST WAS STOPPED. ITS RESULTS ARE IN THE LOG.";
      return;
    }
    error_pruebas_.clear();
    pruebas_ = xmow::bench::Listar(ArchivoBench(), &ValidarBench, &error_pruebas_);
    nivel_ = 3;
    fila_ = 0;
  }

  // A starts the selected test and closes the menu (returns false); a test that cannot run says why.
  bool PulsarPruebas(uint64_t pulsados) {
    const int total = int(pruebas_.size());
    if (total > 0 && (pulsados & kArriba)) fila_ = Siguiente(fila_, total, -1);
    if (total > 0 && (pulsados & kAbajo)) fila_ = Siguiente(fila_, total, 1);
    if ((pulsados & kBotonA) && total > 0) {
      const xmow::bench::Entrada& p = pruebas_[size_t(fila_)];
      std::string error;
      if (!p.error.empty()) {
        aviso_ = TextoMenu("TEST " + std::to_string(p.id) + " CANNOT RUN: " + p.error, 200);
      } else if (xmow::bench::Iniciar(ArchivoBench(), p.id, &ValidarBench, &error)) {
        XMOW_LOG_CONTEXTO("[xmow menu] TESTS: test {} '{}' started (about {:.0f} s; B aborts)", p.id, p.nombre,
                          p.segundos);
        return false;
      } else {
        aviso_ = TextoMenu(error, 200);
        REXLOG_WARN("[xmow menu] TESTS: test {} not started: {}", p.id, error);
      }
    }
    Mostrar();
    return true;
  }

  // -1 when the value is not in the option's list (the menu then shows it raw).
  static int IndiceEleccion(const AdvancedOption& o, std::string_view valor) {
    for (size_t i = 0; i < o.choices.size(); ++i) {
      if (MismoValor(o.choices[i].value, valor)) return int(i);
    }
    return -1;
  }

  void Guardar() {
    const bool ok = GuardarTodo();
    XMOW_LOG_CONTEXTO("[xmow menu] save to config ({})", ok ? "ok" : "FAILED");
    aviso_ = ok ? kAvisoGuardado : kAvisoFallo;
  }

  void Mostrar() const {
    std::vector<std::string> lineas;
    std::string ayuda;
    if (nivel_ == 3) {
      const int total = int(pruebas_.size());
      lineas.push_back(TextoMenu(total ? fmt::format("SETTINGS / TESTS  {}/{}", fila_ + 1, total) : "SETTINGS / TESTS"));
      if (!total) {
        lineas.push_back(TextoMenu("  " + (error_pruebas_.empty() ? std::string("NO TESTS IN THE FILE") : error_pruebas_)));
        ayuda = kAyudaPruebas;
      }
      const int inicio = InicioVentana(fila_, total, kVisibles);
      for (int i = inicio; i < std::min(inicio + kVisibles, total); ++i) {
        const xmow::bench::Entrada& p = pruebas_[size_t(i)];
        lineas.push_back(TextoMenu(fmt::format("{}{} {} ({:.0f} MIN{}){}", i == fila_ ? "> " : "  ", p.id, p.nombre,
                                               std::max(1.0, std::round(p.segundos / 60.0)), p.vivo ? ", LIVE" : "",
                                               p.error.empty() ? "" : " !")));
      }
      lineas.emplace_back("A: START / B: BACK");
      if (total) {
        const xmow::bench::Entrada& p = pruebas_[size_t(fila_)];
        ayuda = p.error.empty() ? (p.ayuda.empty() ? std::string(kAyudaPruebas) : TextoMenu(p.ayuda, 400))
                                : TextoMenu("CANNOT RUN: " + p.error, 200);
      }
    } else if (nivel_ == 1) {
      lineas.emplace_back("SETTINGS / CATEGORIES");
      for (int i = 0; i < Categorias(); ++i) {
        lineas.push_back(TextoMenu(std::string(i == fila_ ? "> " : "  ") + categorias_[size_t(i)].datos->name));
      }
      lineas.push_back(std::string(fila_ == Categorias() ? "> " : "  ") + "TESTS");
      lineas.push_back(std::string(fila_ == Categorias() + 1 ? "> " : "  ") + "SAVE TO CONFIG");
      lineas.emplace_back("ARROWS: SELECT / A: OPEN / B: CLOSE");
      ayuda = fila_ == Categorias() + 1 ? kAyudaGuardarConfig
              : fila_ == Categorias()   ? kAyudaPruebas
                                        : categorias_[size_t(fila_)].datos->help;
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
        if (InactivaEnModoJogo(o.cvar)) linea += " (OFF: PLAY MODE)";
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
  std::vector<xmow::bench::Entrada> pruebas_;  // 0.1.34: the tests page
  std::string error_pruebas_;
};

// =====================================================================================================================
// Achievements (L + R + Left), the shared menu template: 6 per page, A opens the detail card, B goes back.
// Everything is read at run time from the game's own XDBF (kernel_state()->title_xdbf(), loaded into the achievement
// manager by KernelState::LoadAchievementsData); the icons are the XDBF's PNGs, nothing is shipped.
// =====================================================================================================================

// The achievement's image from the game's XDBF (in the user's own XEX), decoded once and shared with the unlock toast.
// Null when it has none (the text-only layout). Only with the game's module loaded (title_xdbf() requires it): from a
// non-empty catalog or from an unlock event.
rex::ui::IconoCompartido Icono(uint32_t image_id, uint32_t lado_maximo = 32) {
  static std::mutex candado;
  static std::vector<std::tuple<uint32_t, uint32_t, rex::ui::IconoCompartido>> cache;
  if (!image_id) return nullptr;
  std::lock_guard cerrojo(candado);
  for (const auto& [id, lado, icono] : cache) {
    if (id == image_id && lado == lado_maximo) return icono;
  }
  rex::system::KernelState* kernel = rex::system::kernel_state();
  if (!kernel) return nullptr;
  rex::ui::IconoCompartido icono;
  const auto db = kernel->title_xdbf();
  if (db.is_valid()) {
    const auto bloque = db.GetEntry(rex::system::util::XdbfSection::kImage, image_id);
    if (bloque) icono = rex::ui::DecodificarIcono(image_id, bloque.buffer, bloque.size, lado_maximo);
  }
  if (!icono) REXLOG_INFO("[xmow menu] achievement without icon (image {}): text only", image_id);
  cache.emplace_back(image_id, lado_maximo, icono);  // a failure is remembered too
  return icono;
}

// Words wrapped to `ancho` columns, at most `max_filas` rows (the last ends in "..." if the text is cut). A word longer
// than a row is broken.
std::vector<std::string> EnvolverPalabras(std::string_view texto, size_t ancho, size_t max_filas) {
  std::vector<std::string> filas;
  std::string actual;
  bool cortado = false;
  size_t pos = 0;
  while (pos < texto.size() && !cortado) {
    while (pos < texto.size() && texto[pos] == ' ') ++pos;
    const size_t fin = std::min(texto.find(' ', pos), texto.size());
    std::string palabra(texto.substr(pos, fin - pos));
    pos = fin;
    while (!palabra.empty()) {
      if (!actual.empty() && actual.size() + 1 + palabra.size() <= ancho) {
        actual += ' ' + palabra;
        palabra.clear();
      } else if (actual.empty() && palabra.size() <= ancho) {
        actual = std::move(palabra);
        palabra.clear();
      } else {
        if (!actual.empty()) {
          filas.push_back(std::move(actual));
          actual.clear();
        } else {
          filas.push_back(palabra.substr(0, ancho));
          palabra.erase(0, ancho);
        }
        if (filas.size() >= max_filas) {
          cortado = true;
          break;
        }
      }
    }
  }
  if (!cortado && !actual.empty()) filas.push_back(std::move(actual));
  if (filas.size() > max_filas) filas.resize(max_filas);
  if (cortado && !filas.empty()) {
    auto& ultima = filas.back();
    if (ultima.size() + 3 > ancho) ultima.resize(ancho - 3);
    ultima += "...";
  }
  return filas;
}

class MenuConquistas {
 public:
  // Opens on the list (never straight on a detail card).
  void Abrir(rex::system::AchievementManager& gerente) {
    detalle_ = false;
    Mostrar(gerente);
  }

  // B: the detail card back to the list (true: consumed); false when already on the list (the menu closes).
  bool Atras(rex::system::AchievementManager& gerente) {
    if (!detalle_) return false;
    detalle_ = false;
    Mostrar(gerente);
    return true;
  }

  void Mostrar(rex::system::AchievementManager& gerente, uint64_t pulsados = 0) {
    const auto catalogo = gerente.ListAchievements();
    std::vector<std::string> lineas{"WOLVERINE / ACHIEVEMENTS"};
    std::vector<rex::ui::IconoEnLinea> iconos;
    std::string letreiro;
    if (catalogo.empty()) {
      detalle_ = false;
      lineas.emplace_back("NO ACHIEVEMENTS LOADED");
    } else if (detalle_) {
      // Detail card: left / right = the previous / next achievement, B returns to the list.
      const int total = int(catalogo.size());
      int pasos = 0;
      if (pulsados & kIzquierda) pasos = -1;
      if (pulsados & kDerecha) pasos = 1;
      fila_ = std::clamp(fila_ + pasos, 0, total - 1);
      const auto& item = catalogo[size_t(fila_)];
      const bool tiene = gerente.IsUnlocked(item.id);
      lineas.push_back(fmt::format("ACHIEVEMENT {} OF {}", fila_ + 1, total));
      // Lines 2-7: the 96x96 icon (columns 2-9) and, from column 11, the name in the big font (two lines each, up to two
      // rows of 16 characters), then the state and the score. The big font leaves the line under it empty.
      std::string sangria(11, ' ');
      if (!tiene) sangria[2] = '\x02';
      const auto nombre = EnvolverPalabras(TextoMenu(item.label, 64), 16, 2);
      if (rex::ui::IconoCompartido icono = Icono(item.image_id, 96)) {
        rex::ui::IconoEnLinea entrada;
        entrada.linea = 2;
        entrada.columna = 2;
        entrada.filas = 6;
        entrada.gris = !tiene;
        entrada.icono = std::move(icono);
        iconos.push_back(std::move(entrada));
      }
      for (size_t i = 0; i < 2; ++i) {
        lineas.push_back(i < nombre.size() ? sangria + '\x03' + nombre[i] : sangria);
        lineas.push_back(sangria);
      }
      lineas.push_back(sangria + fmt::format("{}  {} G", tiene ? "UNLOCKED" : "LOCKED", item.gamerscore));
      lineas.emplace_back();
      lineas.emplace_back();
      const std::string_view descripcion = tiene ? item.description : item.unachieved_description;
      for (const auto& fila : EnvolverPalabras(TextoMenu(descripcion, 400), 42, 6)) {
        lineas.push_back(std::string(tiene ? " " : "\x02") + fila);
      }
      while (lineas.size() < 15) lineas.emplace_back();
      lineas.emplace_back("L/R: PREVIOUS / NEXT / B: BACK");
      letreiro = "B: BACK TO LIST";
    } else {
      // Console style: each entry takes two lines, its icon (32x32, grey when locked) over both, the name and score on
      // the first and the description in small type under it. The selected one's goes to the marquee too.
      constexpr int kPorPagina = 6;
      const int total = int(catalogo.size());
      int pasos = 0;
      if (pulsados & kArriba) pasos = -1;
      if (pulsados & kAbajo) pasos = 1;
      if (pulsados & kIzquierda) pasos = -kPorPagina;
      if (pulsados & kDerecha) pasos = kPorPagina;
      fila_ = std::clamp(fila_ + pasos, 0, total - 1);
      if (pulsados & kBotonA) {
        detalle_ = true;
        Mostrar(gerente);
        return;
      }
      int ganadas = 0;
      uint32_t puntos = 0, maximo = 0;
      for (const auto& item : catalogo) {
        maximo += item.gamerscore;
        if (gerente.IsUnlocked(item.id)) {
          ++ganadas;
          puntos += item.gamerscore;
        }
      }
      lineas.push_back(fmt::format("{} OF {} UNLOCKED / {} OF {} G", ganadas, total, puntos, maximo));
      const int inicio = fila_ / kPorPagina * kPorPagina;
      for (int i = inicio; i < std::min(inicio + kPorPagina, total); ++i) {
        const auto& item = catalogo[size_t(i)];
        const bool tiene = gerente.IsUnlocked(item.id);
        rex::ui::IconoCompartido icono = Icono(item.image_id);
        // Columns 0-1: the cursor. Column 2 (under the icon): '\x02' dims the rest of a locked one. Columns 3-5: the
        // icon, or + / - without one (the text-only layout). The name from column 6.
        std::string linea = i == fila_ ? "> " : "  ";
        linea += tiene ? ' ' : '\x02';
        linea += icono ? "   " : (tiene ? " + " : " - ");
        linea += fmt::format("{} {} G", TextoMenu(item.label, 30), item.gamerscore);
        if (icono) {
          rex::ui::IconoEnLinea entrada;
          entrada.linea = uint32_t(lineas.size());
          entrada.columna = 2;
          entrada.filas = 2;
          entrada.gris = !tiene;
          entrada.icono = std::move(icono);
          iconos.push_back(std::move(entrada));
        }
        lineas.push_back(std::move(linea));
        // The description in the small font ('\x01') from column 6: two rows of ~76 characters.
        const std::string_view descripcion = tiene ? item.description : item.unachieved_description;
        lineas.push_back(std::string(tiene ? "      \x01" : "  \x02   \x01") + TextoMenu(descripcion, 148));
      }
      const auto& item = catalogo[size_t(fila_)];
      const std::string_view descripcion = gerente.IsUnlocked(item.id) ? item.description : item.unachieved_description;
      letreiro = "A: DETAILS  ";
      letreiro += descripcion.empty() ? TextoMenu(item.label, 400)
                                      : TextoMenu(fmt::format("{}: {}", item.label, descripcion), 400);
    }
    if (!detalle_) lineas.emplace_back("ARROWS: NAVIGATE / A: DETAILS / B: CLOSE");
    rex::ui::FijarLetreiro(std::move(letreiro));
    rex::ui::FijarSuperposicionConIconos(std::move(lineas), std::move(iconos));
  }

 private:
  int fila_ = 0;
  bool detalle_ = false;
};

// The unlock toast (menu closed, top right, ~4 s): the icon over the first two lines (columns 0-2), the header and the
// name beside it, the description in small type under them. Without an icon, the same text from column 0.
void AvisoConquista(const rex::system::AchievementInfo& item) {
  rex::ui::IconoEnLinea aviso;
  aviso.linea = 0;
  aviso.columna = 0;
  aviso.filas = 2;
  aviso.icono = Icono(item.image_id);
  const std::string sangria = aviso.icono ? "   " : "";
  std::vector<std::string> lineas{sangria + fmt::format("ACHIEVEMENT UNLOCKED  {} G", item.gamerscore),
                                  sangria + TextoMenu(item.label, kColumnas - sangria.size())};
  if (!item.description.empty()) {
    // Two small rows as wide as the box (two small characters per column), broken at a space.
    const size_t columnas = std::max(lineas[0].size(), lineas[1].size());
    const size_t ancho = std::max<size_t>(24, (columnas - sangria.size()) * 2);
    std::string texto = TextoMenu(item.description, ancho * 2);
    if (texto.size() > ancho) {
      const size_t espacio = texto.rfind(' ', ancho);
      if (espacio != std::string::npos && espacio > 0) texto[espacio] = '\n';
    }
    lineas.push_back(sangria + "\x01" + texto);
  }
  rex::ui::AvisarSuperposicionConIcono(std::move(lineas), std::move(aviso));
  REXLOG_INFO("[xmow menu] achievement unlocked: {} {}", item.id, item.label);
}

// =====================================================================================================================
// The controller filter: chords, menu state, input blocking
// =====================================================================================================================

enum class Menu { kCerrado, kRapido, kAvanzado, kConquistas };

struct Atajo {
  uint64_t acorde;
  Menu menu;
};
constexpr Atajo kAtajos[] = {
    {kAcordeRapido, Menu::kRapido},
    {kAcordeAvanzado, Menu::kAvanzado},
    {kAcordeConquistas, Menu::kConquistas},
};

struct Estado {
  std::mutex candado;
  bool instalado = false;
  Menu menu = Menu::kCerrado;
  MenuRapido rapido;
  MenuAvanzado avanzado;
  MenuConquistas conquistas;
  std::array<uint64_t, 8> antes{};   // buttons held at each slot's previous poll
  std::array<bool, 8> soltar{};      // after closing: the slot stays blocked until all its buttons are released
  bool bancada_vista = false;        // 0.1.34b: a blocking bench test had the controller at the previous poll
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
    XMOW_LOG_CONTEXTO("[xmow menu] {} unsaved (R) change(s) dropped", d.pendientes.size());
    d.pendientes.clear();
  }
  rex::ui::FijarSuperposicion({});
  rex::ui::FijarLetreiro({});
  XMOW_LOG_CONTEXTO("[xmow menu] closed");
}

void Alternar(Estado& e, Menu menu) {
  if (e.menu == menu) {
    Cerrar(e);
    return;
  }
  if (menu == Menu::kConquistas && !LosDatos().conquistas) return;  // no kernel achievement manager (not installed)
  LosDatos().pendientes.clear();  // switching menus drops unsaved (R) changes, as closing does
  e.menu = menu;
  e.antes.fill(UINT64_MAX);  // what is held now (the chord) does not count as a new press
  e.soltar.fill(false);
  if (menu == Menu::kRapido) {
    e.rapido.Abrir();
    XMOW_LOG_CONTEXTO("[xmow menu] quick menu opened");
  } else if (menu == Menu::kConquistas) {
    e.conquistas.Abrir(*LosDatos().conquistas);
    XMOW_LOG_CONTEXTO("[xmow menu] achievements opened");
  } else {
    e.avanzado.Abrir();
    XMOW_LOG_CONTEXTO("[xmow menu] advanced settings opened");
  }
}

bool Filtro(size_t ranura, uint64_t botones) {
  Estado& e = ElEstado();
  std::lock_guard<std::mutex> cerrojo(e.candado);
  if (!e.instalado) return false;
  if (ranura >= e.antes.size()) return e.menu != Menu::kCerrado || xmow::bench::Bloqueante();
  const uint64_t antes = std::exchange(e.antes[ranura], botones);
  if (e.soltar[ranura]) {
    if (botones == 0) e.soltar[ranura] = false;
    return true;
  }
  // 0.1.34: while a blocking bench test runs the game sees an idle controller and no menu opens; B aborts the test.
  // 0.1.34b (review S4): with no frame for 5 s the controller goes back to the game.
  if (xmow::bench::Bloqueante() && !xmow::bench::AnilloParado()) {
    e.bancada_vista = true;
    if (botones & ~antes & kBotonB) xmow::bench::Abortar("B pressed");
    return true;
  }
  // 0.1.34b (review S1): when the test ends (B, DONE or the ring stopped), what is held then (the B that aborted) does
  // not reach the game: every slot stays blocked until all its buttons are released, as after closing a menu.
  if (e.bancada_vista) {
    e.bancada_vista = false;
    e.soltar.fill(true);
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
    // B on an achievement's detail card goes back to the list; only B on the list closes the menu.
    if (e.menu == Menu::kConquistas && e.conquistas.Atras(*LosDatos().conquistas)) {
      // consumed by the detail card
    } else if (e.menu != Menu::kAvanzado || !e.avanzado.Atras()) {
      Cerrar(e);
    }
  } else if (pulsados) {
    if (e.menu == Menu::kRapido && !e.rapido.Pulsar(pulsados)) Cerrar(e);
    if (e.menu == Menu::kAvanzado && !e.avanzado.Pulsar(pulsados)) Cerrar(e);
    if (e.menu == Menu::kConquistas) e.conquistas.Mostrar(*LosDatos().conquistas, pulsados);
  }
  return true;  // the game does not see the controller while a menu is open
}

}  // namespace

void Instalar(const std::filesystem::path& config, rex::system::AchievementManager* conquistas) {
  Estado& e = ElEstado();
  {
    std::lock_guard<std::mutex> cerrojo(e.candado);
    Datos& d = LosDatos();
    d.config = config;
    d.conquistas = conquistas;
    if (conquistas) {
      // The catalog comes from the game's XDBF (KernelState::LoadAchievementsData, when the module is set) and the
      // unlock state from <user_root>/achievements/<TITLEID>.toml.
      const auto catalogo = conquistas->ListAchievements();
      size_t ganadas = 0;
      for (const auto& item : catalogo) ganadas += conquistas->IsUnlocked(item.id) ? 1 : 0;
      REXLOG_INFO("[xmow menu] achievements: {} in the catalog, {} unlocked", catalogo.size(), ganadas);
      // The toast: AchievementManager calls this for every unlock the kernel shows (XGIUserWriteAchievements). The
      // app returns no ImGui toast (XmowApp::CreateAchievementNotificationDialog), this one is the overlay's.
      d.oyente = conquistas->RegisterNotificationCallback([](const rex::system::AchievementEvent& evento) {
        Estado& estado = ElEstado();
        std::lock_guard<std::mutex> candado(estado.candado);
        if (estado.instalado) AvisoConquista(evento.achievement);
      });
    }
    d.claves.clear();
    auto anadir = [&d](const char* nombre) {
      if (BuscarClave(nombre)) return;  // listed in both menus (audio_mute, xmow_niebla_altura)
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
    REXLOG_INFO("[xmow menu] installed: + and - quick menu, L + R + Right advanced settings, L + R + Left "
                "achievements; {} cvars; config {}",
                d.claves.size(), config.string());
  }
  rex::input::FijarFiltroMando(&Filtro);
}

void Desinstalar() {
  Estado& e = ElEstado();
  rex::system::AchievementManager* gerente = nullptr;
  rex::system::AchievementListenerHandle oyente = 0;
  {
    std::lock_guard<std::mutex> cerrojo(e.candado);
    Datos& d = LosDatos();
    gerente = std::exchange(d.conquistas, nullptr);
    oyente = std::exchange(d.oyente, 0);
  }
  // Outside the menu's lock: the manager may be dispatching the toast listener (which takes it) right now.
  if (gerente && oyente) gerente->UnregisterCallback(oyente);
  rex::ui::AvisarSuperposicion({});
}

}  // namespace gow2::menu
