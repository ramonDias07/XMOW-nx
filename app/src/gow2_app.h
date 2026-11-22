// X-Men Origins: Wolverine (Xbox 360, retail disc, no title update) on the Nintendo Switch.
// App layer from gears2-nx (VasilkovND, GPL-3), adapted to Wolverine (XMOW-nx).
//
// Where everything is: the folder of the NRO (sdmc:/switch/XMOW/ on the console):
//   XMOW.nro, xmow.toml, xmow_shaders.dslib, shadercache_dksh.bin,
//   game_root/   the extracted disc (default.xex and WGame/)
//   cache/       mounted as cache: for the game (SDK patch 1001)
//   logs/        xmow_NNN.log and logs/rex/ (crash logs)
// The package is also looked for in a XMOW/ or switch/XMOW/ folder beside the NRO, as the earlier XMOW build did.
//
// The recompiled code is the retail executable's (default.xex, SHA-256 2f987f7b...5378). A title update's
// default.xexp would change the code under it, so it is refused.

#pragma once

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/logging.h>
#include <rex/rex_app.h>
#include <rex/runtime.h>
#include <rex/system.h>

#include <array>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <memory>
#include <string>
#include <system_error>

#if defined(__SWITCH__)
#include <switch.h>
#endif

#include "gow2_cpu.h"
#include "gow2_gpu.h"
#include "gow2_menu.h"
#include "gow2_resolution.h"

// The version (NACP and the first lines of the log), from CMake (GOW2_VERSION); the same string in both.
#ifndef GOW2_VERSION
#define GOW2_VERSION "0.1.0-diag"
#endif

REXCVAR_DEFINE_STRING(gow2_game, "", "XMOW", "Carpeta del juego (disco extraido). Vacio: game_root/ junto al ejecutable.");

class XmowApp : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;

  static std::unique_ptr<rex::ui::WindowedApp> Create(rex::ui::WindowedAppContext& ctx) {
    return std::unique_ptr<XmowApp>(new XmowApp(ctx, "xmow", PPCImageConfig));
  }

 protected:
  // No Xenos emulation: gow2_gpu.cpp reads the game's command ring and draws with the deko3d renderer.
  void OnPreSetup(rex::RuntimeConfig& config) override {
    if (!config.graphics) config.graphics = gow2::gpu::CreateGraphicsSystem();
#if defined(__SWITCH__)
    // libnx's BSD sockets (Unreal Engine 3 starts its network layer at boot).
    const Result rc = socketInitializeDefault();
    if (R_FAILED(rc)) {
      REXLOG_WARN("xmow: socketInitializeDefault failed: 0x{:X} (no sockets)", rc);
    } else {
      REXLOG_INFO("xmow: sockets ready (libnx BSD)");
    }
#endif
  }

  // The package is the folder of the NRO; game_data_root is the folder with default.xex (ConstructRuntime checks it).
  void OnConfigurePaths(rex::PathConfig& paths) override {
    const auto executable = rex::filesystem::GetExecutableFolder();
    package_ = executable;
    const std::array candidates{executable, executable / "XMOW", executable / "switch" / "XMOW"};
    for (const auto& candidate : candidates) {
      std::error_code ec;
      if (std::filesystem::is_regular_file(candidate / "game_root" / "default.xex", ec)) {
        package_ = candidate;
        break;
      }
    }
    paths.config_path = package_ / "xmow.toml";
    if (paths.user_data_root.empty()) paths.user_data_root = package_ / "userdata";
    if (paths.cache_root.empty()) paths.cache_root = package_ / "cache";
    std::error_code ec;
    std::filesystem::create_directories(paths.cache_root, ec);  // cache: needs it (SDK patch 1001: Runtime::SetupVfs)
    if (!paths.game_data_root.empty()) return;
    const std::string flag = REXCVAR_GET(gow2_game);
    paths.game_data_root = flag.empty() ? package_ / "game_root" : std::filesystem::path(flag);
  }

  void OnPostInitLogging() override {
    LogSessionHeader();
    gow2::cpu::InformarSistema();
  }

  void OnLoadXexImage(std::string& xex_image) override {
    std::error_code ec;
    const auto root = game_data_root();
    if (!std::filesystem::is_regular_file(root / "default.xex", ec)) {
      Fail(fmt::format("X-Men Origins: Wolverine: default.xex is missing from {}. The game folder (game_root) goes "
                       "next to the NRO.",
                       root.string()));
    }
    if (std::filesystem::exists(root / "default.xexp", ec)) {
      Fail(fmt::format("X-Men Origins: Wolverine: {} has a title update (default.xexp). This build runs the retail "
                       "executable without one: remove default.xexp.",
                       root.string()));
    }
    REXLOG_INFO("xmow: game at {}; loading {}", root.string(), xex_image);
  }

  // The scene resolution first: the game asks the OS for the display mode while it sets up its renderer.
  void OnPreLaunchModule() override {
    gow2::resolucion::Aplicar();
    gow2::menu::Instalar(package_ / "xmow.toml");  // the same file as paths.config_path (OnConfigurePaths)
    gow2::cpu::Configurar();
  }

 private:
  // First lines of every log: which build and which session this file belongs to, so logs of different runs (and a
  // stale rex_crash.log next to them) cannot be mixed up.
  void LogSessionHeader() const {
    const std::time_t now = std::time(nullptr);
    char stamp[32] = "?";
    std::tm local{};
    if (localtime_r(&now, &local)) std::strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", &local);
    const char* mode = "?";
#if defined(__SWITCH__)
    switch (appletGetOperationMode()) {
      case AppletOperationMode_Handheld: mode = "portatil"; break;
      case AppletOperationMode_Console: mode = "dock"; break;
      default: break;
    }
#endif
    REXLOG_INFO("[XMOW sessao] ===== XMOW {} | build {} {} | inicio {} | modo {} =====", GOW2_VERSION, __DATE__,
                __TIME__, stamp, mode);
    REXLOG_INFO("[XMOW sessao] pacote={} config={}", package_.string(), (package_ / "xmow.toml").string());
    // rex_crash.log is moved to rex_crash_prev.log at boot (SDK patch 1001, switch_crash_hooks.c): say whether the
    // last run crashed.
    std::error_code ec;
    const auto previous = package_ / "logs" / "rex" / "rex_crash_prev.log";
    if (std::filesystem::is_regular_file(previous, ec)) {
      REXLOG_INFO("[XMOW sessao] a sessao anterior deixou {} ({} bytes): nao e desta sessao", previous.string(),
                  std::filesystem::file_size(previous, ec));
    }
  }

  [[noreturn]] static void Fail(const std::string& msg) {
    REXLOG_ERROR("{}", msg);
    rex::ShowSimpleMessageBox(rex::SimpleMessageBoxType::Error, msg);
    rex::FlushLogging();
    std::_Exit(1);
  }

  std::filesystem::path package_;
};
