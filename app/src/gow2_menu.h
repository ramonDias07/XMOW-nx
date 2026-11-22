// XMOW - the port's menus (gow2_menu.cpp, shared menu template): + and - together open the quick menu,
// L + R + Right the advanced settings. Instalar installs the controller filter behind both.

#pragma once

#include <filesystem>

namespace gow2::menu {

// `config` = the xmow.toml the menus' SAVE writes into (XmowApp: package_ / "xmow.toml"). Call it once the cvars are
// loaded (XmowApp::OnPreLaunchModule).
void Instalar(const std::filesystem::path& config);

}  // namespace gow2::menu
