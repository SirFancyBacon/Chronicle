#include "data_needed.hpp"

#include <SDL3/SDL.h>

#include <cstdlib>
#include <filesystem>
#include <format>
#include <print>
#include <string>
#include <system_error>

#include "app/exitcodes.hpp"
#include "data_version.hpp"
#include "platform/data_archive.hpp"
#include "platform/paths.hpp"

namespace dc::platform {

namespace fs = std::filesystem;

namespace {

#ifdef _WIN32
constexpr const char *kLauncher = "noktus-launcher.exe";
#else
constexpr const char *kLauncher = "noktus-launcher";
#endif

bool HoldsAFile(const fs::path &root) {
    std::error_code error;
    const auto      options = fs::directory_options::follow_directory_symlink | fs::directory_options::skip_permission_denied;
    for (fs::recursive_directory_iterator it(root, options, error), end; !error && it != end; it.increment(error)) {
        std::error_code entry_error;
        if (it->is_regular_file(entry_error)) {
            return true;
        }
    }
    return false;
}

fs::path LauncherPath() {
    const char *base = SDL_GetBasePath();
    return base != nullptr ? PathsFromUtf8(base) / kLauncher : fs::path(kLauncher);
}

// The game's own art and text come from the data it hasn't got, so this is the system's message box, not the game's interface.
void Ask(bool old, const fs::path &launcher) {
    std::error_code error;
    const bool      beside = fs::is_regular_file(launcher, error);
    const char     *title = old ? "Dark Cloud needs setting up again" : "Dark Cloud needs setting up";
    std::string     text = old ? "The game's data was set up by an older version, and this version needs it set up again from your Dark Cloud "
                                 "disc. Saves and mods aren't touched."
                               : "The game is set up once from your own Dark Cloud disc: an image of it, or the disc itself.";
    if (!beside) {
        text += std::format("\n\nThe launcher, which sets it up, should be beside the game as {}, and isn't: reinstall the game.", kLauncher);
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, title, text.c_str(), nullptr);
        return;
    }
    text += " The launcher does it, in a few minutes, then plays the game.";
    const SDL_MessageBoxButtonData buttons[] = {
        {SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, "Open the launcher"},
        {SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, "Quit"},
    };
    const SDL_MessageBoxData box = {SDL_MESSAGEBOX_INFORMATION, nullptr, title, text.c_str(), SDL_arraysize(buttons), buttons, nullptr};
    int                      chosen = 0;
    if (!SDL_ShowMessageBox(&box, &chosen)) {
        std::println(stderr, "can't show the message: {}", SDL_GetError());
        return;
    }
    if (chosen != 1) {
        return;
    }
    const std::string path = PathsDisplay(launcher);
    const char       *args[] = {path.c_str(), nullptr};
    // The launcher sets up the data where this game looked for it, which --data may have chosen (as the release folders' scripts do), and
    // plays with the same saves.
    const std::string data = PathsDisplay(PathsDataRoot());
    const std::string save = PathsDisplay(PathsSaveRoot());
    SDL_Environment  *environment = SDL_CreateEnvironment(true);
    if (environment != nullptr) {
        SDL_SetEnvironmentVariable(environment, "DC_DATA", data.c_str(), true);
        SDL_SetEnvironmentVariable(environment, "DC_SAVE", save.c_str(), true);
    }
    const SDL_PropertiesID properties = SDL_CreateProperties();
    SDL_SetPointerProperty(properties, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, args);
    SDL_SetPointerProperty(properties, SDL_PROP_PROCESS_CREATE_ENVIRONMENT_POINTER, environment);
    // Not waited for: the launcher goes on once the game has gone.
    SDL_Process *process = SDL_CreateProcessWithProperties(properties);
    SDL_DestroyProperties(properties);
    SDL_DestroyEnvironment(environment);
    if (process == nullptr) {
        std::println(stderr, "can't start the launcher {}: {}", path, SDL_GetError());
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, title, std::format("The launcher didn't start: {}", SDL_GetError()).c_str(), nullptr);
        return;
    }
    SDL_DestroyProcess(process);
}

} // namespace

void RequireCurrentData(bool headless) {
    const fs::path &root = PathsDataRoot();
    const char     *missing = nullptr;
    if (!DataArchivePresent(root)) {
        std::error_code error;
        if (!fs::is_directory(root, error)) {
            missing = "is not a directory";
        } else if (!HoldsAFile(root)) {
            missing = "is empty";
        }
    }
    int version = noktus::convert::kDataVersion;
    if (missing == nullptr) {
        version = noktus::convert::ReadDataVersion(root, root.parent_path() / "data.zip").value_or(noktus::convert::kUnmarkedDataVersion);
        if (version > noktus::convert::kDataVersion) {
            std::println(stderr, "game data: {} says it's of layout {}, newer than the {} this game reads; carrying on", PathsDisplay(root),
                         version, noktus::convert::kDataVersion);
        }
        if (version >= noktus::convert::kDataVersion) {
            return;
        }
    }
    const fs::path launcher = LauncherPath();
    if (missing != nullptr) {
        std::println(stderr, "no game data: {} {}; set it up from the disc with the launcher, {} --setup <disc image>", PathsDisplay(root),
                     missing, PathsDisplay(launcher));
    } else {
        std::println(stderr, "old game data: {} is of layout {}, and this game reads layout {}; set it up again with the launcher, {} --setup "
                     "<disc image>",
                     PathsDisplay(root), version, noktus::convert::kDataVersion, PathsDisplay(launcher));
    }
    if (!headless) {
        Ask(missing == nullptr, launcher);
    }
    std::exit(app::kExitNoData);
}

} // namespace dc::platform
