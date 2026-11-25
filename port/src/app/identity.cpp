#include "app/identity.hpp"

#include <cstdlib>
#include <filesystem>
#include <system_error>

#include "app/icon_retail.hpp"
#include "icon/chronicle.hpp"
#include "platform/data_archive.hpp"
#include "platform/game_icon.hpp"
#include "platform/paths.hpp"
#include "platform/settings.hpp"
#include "platform/window.hpp"

namespace dc::app {

using namespace dc::platform;

namespace {

constexpr const char *kWindowTitle = "Dark Cloud";

} // namespace

void GameSetIdentity() {
    SettingsSetEnvPrefix("DC");
    PathsConfigure({.app_dir = "chronicle", .env_data = "DC_DATA", .env_save = "DC_SAVE"});
    const char *data_override = std::getenv("DC_DATA");
    if (data_override == nullptr || *data_override == '\0') {
        std::error_code             error;
        const std::filesystem::path current_data = std::filesystem::current_path(error) / "data";
        if (!error && DataArchivePresent(current_data)) {
            PathsSetDataRoot(current_data);
        } else if (error || !std::filesystem::is_directory(current_data, error)) {
            const std::filesystem::path executable = PathsExecutable().parent_path() / "data";
            if (DataArchivePresent(executable)) {
                PathsSetDataRoot(executable);
            }
        }
    }
    WindowSetIdentity(
        {.title = kWindowTitle, .app_id = "dcdecomp.darkcloud", .icon_rgba = kIconRgba, .icon_size = kIconSize});
}

void GameSetDataIcon() {
    // The save menu's pack in each language folder carries the memory card icon; a disc holds only
    // its own languages, so every folder is tried, English first.
    GameIconSource source = RetailIconSource(PathsDataRoot());
    source.ico_file = L"darkcloud.ico";
    const std::wstring shortcut = (PathsDataRoot().parent_path() / "Dark Cloud").wstring();
    source.shortcut = shortcut.c_str();
    GameIconInstall(PathsDataRoot(), source);
}

} // namespace dc::app
