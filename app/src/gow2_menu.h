// XMOW - the port's menus (gow2_menu.cpp, shared menu template): + and - together open the quick menu,
// L + R + Right the advanced settings, L + R + Left the achievements. Instalar installs the controller filter behind
// all three.

#pragma once

#include <filesystem>

namespace rex::system {
class AchievementManager;
}

namespace gow2::menu {

// `config` = the xmow.toml the menus' SAVE writes into (XmowApp: package_ / "xmow.toml"). Call it once the cvars are
// loaded and the game's module is set (XmowApp::OnPreLaunchModule: the XDBF catalog is read by then).
// `conquistas` = the kernel's achievement manager (the catalog, what is unlocked, the unlock toast); null: no
// achievements menu.
void Instalar(const std::filesystem::path& config, rex::system::AchievementManager* conquistas);

// Takes the unlock listener off the manager (XmowApp::OnShutdown, with the runtime still alive).
void Desinstalar();

}  // namespace gow2::menu
