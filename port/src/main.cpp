#include <SDL3/SDL_stdinc.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include "app/exitcodes.hpp"
#include "app/gameloop.hpp"
#include "app/identity.hpp"
#include "battle_globals.hpp"
#include "dun/gameloop.hpp"
#include "dungeon/loot.hpp"
#include "dungeon/rules.hpp"
#include "fishing/host.hpp"
#include "gamemode.hpp"
#include "georama/tables.hpp"
#include "gfx/gfx.hpp"
#include "item/host.hpp"
#include "langset.hpp"
#include "localize/localize.hpp"
#include "menu_save.hpp"
#include "nowload.hpp"
#include "options/menu_option.hpp"
#include "options/quality_pick.hpp"
#include "options/rows.hpp"
#include "platform/clock.hpp"
#include "platform/config.hpp"
#include "platform/crash_report.hpp"
#include "platform/data_archive.hpp"
#include "platform/discord.hpp"
#include "platform/data_needed.hpp"
#include "platform/display.hpp"
#include "platform/hitches.hpp"
#include "platform/input.hpp"
#include "platform/input_script.hpp"
#include "platform/paths.hpp"
#include "platform/settings.hpp"
#include "platform/speaker_test.hpp"
#include "platform/window.hpp"
#include "qol/features.hpp"
#include "render/noktus_host.hpp"
#include "render/passes.hpp"
#include "render/screen.hpp"
#include "save/save_state.hpp"
#include "snd.hpp"
#include "sound/backend.hpp"
#include "sound/soundtrack.hpp"
#include "text/glyphs.hpp"
#include "title/opening.hpp"
#include "title/rushmovi.hpp"

namespace fs = std::filesystem;

namespace dc::app {

using namespace dc::game;
using namespace dc::localize;
using namespace dc::options;
using namespace dc::platform;
using namespace dc::render;

namespace {

struct Options {
    bool         headless = false;
    bool         offscreen = false;
    std::int64_t frames = -1;
    const char  *screenshot = nullptr;
    const char  *input = nullptr;
    int          width = 0;
    int          height = 0;
    const char  *jump = nullptr;
    const char  *give = nullptr;
    const char  *town_menu = nullptr;
    const char  *town_dungeon = nullptr;
    const char  *town_clock = nullptr;
    const char  *town_place = nullptr;
    const char  *town_interior = nullptr;
    bool         dungeon_ura = false;
    const char  *dungeon_open = nullptr;
    const char  *town_event = nullptr;
    bool         fast_load = false;
    bool         show_fps = false;
    bool         screenshot_fps = false;
    const char  *export_text = nullptr;
    const char  *export_gameplay = nullptr;
    const char  *font = nullptr;
    const char  *seed_save = nullptr;
};

Options g_options;

[[noreturn]] void Usage(const char *program) {
    std::fprintf(stderr,
                 "usage: %s [--data DIR] [--save DIR] [--headless] [--frames N] [--screenshot PATH]\n"
                 "          [--input FILE]"
                 " [--width W] [--height H]"
                 " [--offscreen]\n"
                 " [--show-fps] [--jump MODE[:MAP]] [--fast-load]\n"
                 "          [--give ITEM,...]\n"
                 "  --data DIR         extracted game data or its sibling data.zip (default: DC_DATA,\n"
                 "                     then ./data, then data/ beside the executable)\n"
                 "  --save DIR         saves and config.json (default: DC_SAVE, then\n"
                 "                     ./save, then save/ beside the executable)\n"
                 "  --seed-save DIR    copy a save-root fixture before startup; requires explicit --save\n"
                 "                     pointing to a new or empty directory (omit on subsequent runs)\n"
                 "  --headless         draw into a picture with no window, no audio device, the game clock\n"
                 "                     unbounded\n"
                 "  --offscreen        the same as --headless\n"
                 "  --frames N         stop after N frames of the game's main loop\n"
                 "  --screenshot PATH  write the last tick's picture to PATH on exit, with the FPS counter if it's on\n"
                 "  --input FILE       drive pad 1 from a script (default: DC_INPUT); see docs/PC.md\n"
                 "  --width, --height  window size in pixels (default: config.json, then the monitor's)\n"
                 "  --screenshot-fps   with --screenshot: fail if the FPS counter is off\n"
                 "  --export-text DIR  write the game's message files to DIR as language JSON (docs/LOCALIZATION.md)\n"
                 "  --export-gameplay DIR  write the item, fishing and Georama tables to DIR as gameplay documents,\n"
                 "                     a starting point for a datapack (docs/ITEM_FRAMEWORK.md, docs/GEORAMA.md)\n"
                 "  --font FILE        draw message text from this TrueType font (else lang/font.ttf)\n"
                 "  --show-fps         draw the FPS counter on presented frames when headless too\n"
                 "test hooks:\n"
                 "  --jump MODE[:MAP]  start in edit:<map>, e01/e03/e04 (populated presets), dungeon:<0-6>, title, rush, opening or menu,\n"
                 "                     or a save's town: load:<slot> or autosave\n"
                 "                     skipping the warm-up (DC_JUMP); see docs/PC.md\n"
                 "  --fast-load        loading-screen holds and fades of a few ticks (DC_FAST_LOAD=1)\n"
                 "  --give ITEM,...    give the party these item and weapon numbers, or custom item IDs, when a\n"
                 "                     town or dungeon starts, unless it holds them (DC_GIVE); see docs/PC.md\n"
                 "  --town-menu N      open town menu N once a town walks: 8 fish exchange, 9 bait board,\n"
                 "                     10 fishing records, 11 rods board (DC_TOWN_MENU)\n"
                 "  --town-event N     raise town program N the first time Cross goes down in a town (DC_TOWN_EVENT)\n"
                 "  --town-dungeon N   enter dungeon N (0-6) once a town walks, at its floor select (DC_TOWN_DUNGEON)\n"
                 "  --town-clock H     set the town's clock to hour H (0-23) once it walks (DC_TOWN_CLOCK)\n"
                 "  --town-place X,Y,Z[,TURN]  put the player there once the town walks (DC_TOWN_PLACE)\n"
                 "  --town-interior N  enter the town's interior N (i01h01) once it walks (DC_TOWN_INTERIOR)\n"
                 "  --dungeon-ura      swap to the floor's back side once the dungeon plays\n"
                 "  --dungeon-open X   once a floor plays, open the nearest atla or chest (DC_DUNGEON_OPEN)\n",
                 program);
    std::exit(kExitUsage);
}

Options ParseOptions(int argc, const char **argv) {
    Options options;
    for (int i = 1; i < argc; i++) {
        std::string_view arg = argv[i];
        auto             value = [&] {
            if (i + 1 >= argc) {
                Usage(argv[0]);
            }
            return argv[++i];
        };
        auto number = [&] {
            char *end = nullptr;
            long  result = std::strtol(value(), &end, 10);
            if (end == nullptr || *end != '\0' || result < 0) {
                Usage(argv[0]);
            }
            return result;
        };
        if (arg == "--headless") {
            options.headless = true;
        } else if (arg == "--offscreen") {
            options.headless = true;
            options.offscreen = true;
        } else if (arg == "--frames") {
            options.frames = number();
        } else if (arg == "--screenshot") {
            options.screenshot = value();
        } else if (arg == "--input") {
            options.input = value();
        } else if (arg == "--seed-save") {
            options.seed_save = value();
        } else if (arg == "--width") {
            options.width = static_cast<int>(number());
        } else if (arg == "--height") {
            options.height = static_cast<int>(number());
        } else if (arg == "--jump") {
            options.jump = value();
        } else if (arg == "--give") {
            options.give = value();
        } else if (arg == "--town-menu") {
            options.town_menu = value();
        } else if (arg == "--town-event") {
            options.town_event = value();
        } else if (arg == "--town-dungeon") {
            options.town_dungeon = value();
        } else if (arg == "--town-clock") {
            options.town_clock = value();
        } else if (arg == "--town-place") {
            options.town_place = value();
        } else if (arg == "--town-interior") {
            options.town_interior = value();
        } else if (arg == "--dungeon-ura") {
            options.dungeon_ura = true;
        } else if (arg == "--dungeon-open") {
            options.dungeon_open = value();
        } else if (arg == "--fast-load") {
            options.fast_load = true;
        } else if (arg == "--export-text") {
            options.export_text = value();
        } else if (arg == "--export-gameplay") {
            options.export_gameplay = value();
        } else if (arg == "--font") {
            options.font = value();
        } else if (arg == "--show-fps") {
            options.show_fps = true;
        } else if (arg == "--screenshot-fps") {
            options.screenshot_fps = true;
        } else {
            Usage(argv[0]);
        }
    }
    return options;
}

// The Options screen keeps the interface at 100%, so a size that takes the HUD past the window
// cannot take the screen's own controls with it.
gfx::FrameLayout Layout(const Config &config) {
    return {.aspect = config.aspect == ConfigAspect::Auto ? gfx::AspectMode::Fill : gfx::AspectMode::Letterbox,
            .ui_scale = MenuOptionOpen() ? 1.0f : config.ui_scale};
}

// DC_BGM_TEST=<set>:<track>: a test aid. From frame 150 of the run (the title has started) the music
// stops and sound set <set> plays its sequence number <track> on the music port, so DC_AUDIO_WAV
// records what that sequence sounds like. Run it with --jump menu, which is silent.
void BgmTestPump() {
    static const char *spec = std::getenv("DC_BGM_TEST");
    static bool        done = false;
    if (spec == nullptr || done || GameFrameCount() < 150) {
        return;
    }
    done = true;
    int set = 0;
    int track = 0;
    if (std::sscanf(spec, "%d:%d", &set, &track) < 1) {
        return;
    }
    for (int port = 0; port < sound::kPorts; ++port) {
        sound::Audio().Stop(port);
    }
    SndBgmInit();
    SndBgmLoad(set);
    SndBgmPlay(track);
}

void PumpHost() {
    BgmTestPump();
    if (!WindowPollEvents()) {
        GameRequestStop();
    }
    DisplayPump();
    // A change of video.aspect or video.ui_scale, or the Options screen opening or closing, waits
    // for a pump outside a frame.
    if (!gfx::InFrame()) {
        gfx::FrameLayout layout = Layout(ConfigGet());
        gfx::FrameLayout current = gfx::CurrentFrameLayout();
        if (layout.aspect != current.aspect || layout.ui_scale != current.ui_scale) {
            gfx::SetFrameLayout(layout);
        }
    }
    InputPoll();
    InputScriptApply(GameFrameCount());
}

void LoadInputScript(const char *path) {
#ifdef _WIN32
    std::string environment;
#endif
    if (path == nullptr) {
#ifdef _WIN32
        if (const wchar_t *value = ::_wgetenv(L"DC_INPUT")) {
            environment = PathsDisplay(fs::path(value));
            path = environment.c_str();
        }
#else
        path = std::getenv("DC_INPUT");
#endif
    }
    if (path == nullptr || *path == '\0') {
        return;
    }
    InputScript script;
    std::string error;
    if (!InputScriptLoad(PathsFromUtf8(path), script, error)) {
        std::fprintf(stderr, "bad input script: %s\n", error.c_str());
        std::exit(kExitUsage);
    }
    InputScriptInstall(std::move(script));
}

gfx::PresentMode PresentMode(ConfigPresentMode mode) {
    switch (mode) {
        case ConfigPresentMode::Mailbox:
            return gfx::PresentMode::Mailbox;
        case ConfigPresentMode::Immediate:
            return gfx::PresentMode::Immediate;
        default:
            return gfx::PresentMode::Fifo;
    }
}

render::NoktusSettings NoktusSettingsOf(const Config &config) {
    const float render_scale = config.render_scale;
    const int   render_height = config.render_height;
    const render::NoktusHdr hdr = config.hdr == ConfigHdr::Hdr10 ? render::NoktusHdr::Hdr10 : config.hdr == ConfigHdr::ScRgb ? render::NoktusHdr::ScRgb : render::NoktusHdr::Off;
    return {.interpolate = config.interpolation,
            .present_mode = PresentMode(config.present_mode),
            .max_fps = config.max_fps,
            .anisotropy = config.anisotropy,
            .anti_aliasing = config.anti_aliasing == ConfigAntiAliasing::Taa    ? render::NoktusAntiAliasing::Taa
                             : config.anti_aliasing == ConfigAntiAliasing::Smaa ? render::NoktusAntiAliasing::Smaa
                                                                                : render::NoktusAntiAliasing::Off,
            .supersampling = config.supersampling,
            .render_scale = render_scale,
            .render_height = render_height,
            .hdr = hdr,
            .paper_white = static_cast<float>(config.hdr_paper_white),
            .peak = static_cast<float>(config.hdr_peak),
            .planar_reflections = config.planar_reflections,
            .shadow_quality = config.shadow_quality,
            .ambient_occlusion = config.ambient_occlusion,
            .point_lights = config.point_lights,
            .real_water = config.real_water,
            .glow = config.glow};
}

GamePresentSettings PresentSettings(const Config &config) {
    return {.show_fps = g_options.show_fps || (config.show_fps && !g_options.headless),
            .fps_detail = config.fps_detail};
}

void ApplyDiscord(const Config &config) {
    DiscordStop();
    if (config.discord_rich_presence && !g_options.headless) {
        DiscordStart(kDiscordClientId);
    }
}

// Called mid-tick, from a settings screen: what cannot change inside a frame (the frame layout)
// waits for PumpHost.
void ApplyConfigChange(const Config &before, const Config &after) {
    sound::Audio().SetMasterGain(after.master_volume);
    sound::Audio().SetSurround(after.surround, after.seven_one);
    sound::Audio().SetInterpolation(after.cubic_interpolation);
    InputApplyConfig(after);
    if (after.tick_rate != before.tick_rate) {
        ClockSetTickRate(after.tick_rate);
        render::NoktusTickRate(after.tick_rate);
    }
    // These settings put back an FPS counter the player toggled.
    if (after.show_fps != before.show_fps || after.fps_detail != before.fps_detail) {
        GameSetPresentSettings(PresentSettings(after));
    }
    render::NoktusApply(NoktusSettingsOf(after));
    if (after.discord_rich_presence != before.discord_rich_presence) {
        ApplyDiscord(after);
    }
}

int Screenshot(const char *path) {
    std::vector<uint8_t> pixels;
    uint32_t             width = 0;
    uint32_t             height = 0;
    bool                 grabbed = (!g_options.screenshot_fps || GameShowingFps()) && GameScreenshot(pixels, width, height);
    if (!grabbed || !gfx::WritePng(PathsFromUtf8(path), pixels.data(), width, height)) {
        std::fprintf(stderr, "cannot write the screenshot to %s\n", path);
        return kExitFailure;
    }
    const char *rgba = std::getenv("DC_SCREENSHOT_RGBA");
    if (rgba != nullptr && std::string_view(rgba) == "1") {
        auto raw_path = PathsFromUtf8(path);
        raw_path += ".rgba";
        std::ofstream raw(raw_path, std::ios::binary);
        raw.write(reinterpret_cast<const char *>(pixels.data()), static_cast<std::streamsize>(pixels.size()));
        raw.close();
        if (!raw) {
            std::fprintf(stderr, "cannot write screenshot RGBA to %s.rgba\n", path);
            return kExitFailure;
        }
    }
    return kExitOk;
}

int Run(int argc, const char **argv) {
    CrashReportInstall();
    GameSetIdentity();
    const char *explicit_save = nullptr;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "--save" && i + 1 < argc) {
            explicit_save = argv[++i];
        } else if (arg.starts_with("--save=")) {
            explicit_save = argv[i] + std::string_view("--save=").size();
        }
    }
    argc = PathsConsumeArgs(argc, argv);
    g_options = ParseOptions(argc, argv);
    GameLoadTraceLaunch();
    Options &options = g_options;
    if (options.seed_save != nullptr) {
        if (explicit_save == nullptr || *explicit_save == '\0') {
            std::fprintf(stderr, "--seed-save requires an explicit nonempty --save directory\n");
            return kExitUsage;
        }
        std::string error;
        if (!save::SaveStateSeed(PathsFromUtf8(options.seed_save), PathsFromUtf8(explicit_save), error)) {
            std::fprintf(stderr, "cannot seed --save %s: %s\n", explicit_save, error.c_str());
            return kExitFailure;
        }
        std::fprintf(stderr, "seeded --save %s from %s\n", explicit_save, options.seed_save);
    }
    if (options.export_gameplay != nullptr) {
        // The tables are compiled in, so this needs no disc data, no window and no game.
        const std::filesystem::path directory = PathsFromUtf8(options.export_gameplay);
        return item::Export(directory) && fishing::Export(directory) && dungeon::ExportRules(directory) && dungeon::ExportLoot(directory) &&
                       qol::Export(directory) && georama::Export(directory)
                   ? kExitOk
                   : kExitFailure;
    }
    if (const char *speakers = std::getenv("DC_SPEAKER_TEST")) {
        // Noktus's audio on the real speakers, before the game uses it; needs no disc data, no window and no game.
        const dc::platform::SpeakerTest result = dc::platform::RunSpeakerTest(speakers);
        return result == dc::platform::SpeakerTest::Done ? kExitOk : result == dc::platform::SpeakerTest::BadLayout ? kExitUsage
                                                                                                                    : kExitFailure;
    }
    if (options.jump == nullptr) {
        options.jump = std::getenv("DC_JUMP");
    }
    if (options.give == nullptr) {
        options.give = std::getenv("DC_GIVE");
    }
    if (options.give != nullptr && *options.give != '\0' && !GameSetGive(options.give)) {
        std::fprintf(stderr, "bad --give: %s\n", options.give);
        std::exit(kExitUsage);
    }
    if (options.town_menu == nullptr) {
        options.town_menu = std::getenv("DC_TOWN_MENU");
    }
    if (options.town_menu != nullptr && *options.town_menu != '\0' && !GameSetTownMenu(options.town_menu)) {
        std::fprintf(stderr, "bad --town-menu: %s\n", options.town_menu);
        std::exit(kExitUsage);
    }
    if (options.town_event == nullptr) {
        options.town_event = std::getenv("DC_TOWN_EVENT");
    }
    if (options.town_event != nullptr && *options.town_event != '\0' && !GameSetTownEvent(options.town_event)) {
        std::fprintf(stderr, "bad --town-event: %s\n", options.town_event);
        std::exit(kExitUsage);
    }
    if (options.town_dungeon == nullptr) {
        options.town_dungeon = std::getenv("DC_TOWN_DUNGEON");
    }
    if (options.town_dungeon != nullptr && *options.town_dungeon != '\0' && !GameSetTownDungeon(options.town_dungeon)) {
        std::fprintf(stderr, "bad --town-dungeon: %s\n", options.town_dungeon);
        std::exit(kExitUsage);
    }
    render::PassTraceStart();
    if (options.town_clock == nullptr) {
        options.town_clock = std::getenv("DC_TOWN_CLOCK");
    }
    if (options.town_clock != nullptr && *options.town_clock != '\0' && !GameSetTownClock(options.town_clock)) {
        std::fprintf(stderr, "bad --town-clock: %s\n", options.town_clock);
        std::exit(kExitUsage);
    }
    if (options.town_place == nullptr) {
        options.town_place = std::getenv("DC_TOWN_PLACE");
    }
    if (options.town_place != nullptr && *options.town_place != '\0' && !GameSetTownPlace(options.town_place)) {
        std::fprintf(stderr, "bad --town-place: %s\n", options.town_place);
        std::exit(kExitUsage);
    }
    if (options.town_interior == nullptr) {
        options.town_interior = std::getenv("DC_TOWN_INTERIOR");
    }
    if (options.town_interior != nullptr && *options.town_interior != '\0') {
        GameSetTownInterior(options.town_interior);
    }
    if (options.dungeon_ura) {
        GameSetDungeonUra();
    }
    if (options.dungeon_open == nullptr) {
        options.dungeon_open = std::getenv("DC_DUNGEON_OPEN");
    }
    if (options.dungeon_open != nullptr && *options.dungeon_open != '\0' && !GameSetDungeonOpen(options.dungeon_open)) {
        std::fprintf(stderr, "bad --dungeon-open: %s (atla or chest)\n", options.dungeon_open);
        std::exit(kExitUsage);
    }
    if (options.jump != nullptr && *options.jump != '\0' && !GameSetJump(options.jump)) {
        std::fprintf(stderr, "bad --jump: %s\n", options.jump);
        std::exit(kExitUsage);
    }
    GameSetAutosave(!options.headless && (options.jump == nullptr || *options.jump == '\0'));
    const char *fast_load = std::getenv("DC_FAST_LOAD");
    GameSetFastLoad(options.fast_load || (fast_load != nullptr && *fast_load != '\0' && *fast_load != '0'));
    RequireCurrentData(options.headless);
    GameLoadTraceCheckpoint("data_ready");
    if (!options.headless) {
        GameSetDataIcon();
    }
    if (options.font != nullptr) {
        LocalizeSetFontPath(PathsFromUtf8(options.font));
    }
    if (options.export_text != nullptr) {
        // The game's message files as language JSON for translators; no window, no game.
        return LocalizeExport(options.export_text, true) >= 0 ? kExitOk : kExitUsage;
    }
    LoadInputScript(options.input);

    const bool first_start = !ConfigLoad();
    sound::SoundtrackEnsure();
    const Config &config = ConfigGet();

    DisplaySetOverrides(options.width, options.height, options.headless);
    WindowConfig window = DisplayWindowConfig(config);
    bool offscreen = options.offscreen || (options.headless && !gfx::HeadlessSurfaceAvailable());
    window.vulkan = !offscreen;
    WindowInit(window);
    render::NoktusTakeWindow(offscreen ? nullptr : WindowHandle());
    render::NoktusApply(NoktusSettingsOf(config));
    if (first_start && !offscreen) {
        options::QualityPickAtFirstTick(); // the Quality preset for the GPU found
    }
    InputInit();

    gfx::RendererConfig renderer;
    renderer.app_name = "Chronicle: Dark Cloud";
    renderer.logical_width = kScreenWidth;
    renderer.logical_height = kScreenHeight;
    renderer.display_aspect = 4.0f / 3.0f;
    renderer.present_mode = PresentMode(config.present_mode);
    renderer.layout = Layout(config);
    renderer.validation = renderer.validation || SettingsEnv("VULKAN_VALIDATION") != nullptr;
    gfx::RendererInit(WindowHandle(), renderer);
    GameLoadTraceCheckpoint("renderer_ready");

    sound::Audio().SetMasterGain(config.master_volume);
    sound::Audio().SetSurround(config.surround, config.seven_one);
    sound::Audio().SetInterpolation(config.cubic_interpolation);
    sound::Audio().StartOutput();

    ClockSetTickRate(config.tick_rate);
    // Its first texture starts Noktus, which takes the tick rate as it then stands.
    glyphs::Preload();
    ListDisplayModesAhead();
    const char *bounded_headless = std::getenv("DC_HEADLESS_BOUNDED");
    ClockSetUnbounded(options.headless && (bounded_headless == nullptr || std::string_view(bounded_headless) != "1"));
    ClockAddPumpHook(PumpHost);
    GameSetFrameBudget(options.frames);
    ApplyDiscord(config);
    GameSetPresentSettings(PresentSettings(config));
    ConfigAddChangeHook(ApplyConfigChange);
    ConfigAddChangeHook(ConfigDisplayChanged);
    ConfigAddChangeHook(GameOptionsChanged);
    ConfigAddChangeHook(LocalizeConfigChanged);

    int status = RunGame(argc, const_cast<char **>(argv));
    ConfigFlush(); // settings Options applied but hadn't written yet, when the game is closed with it open
    platform::HitchFlush();
    if (status == kExitOk && options.screenshot != nullptr) {
        status = Screenshot(options.screenshot);
    }
    render::NoktusStop();

    ConfigRemoveChangeHook(LocalizeConfigChanged);
    ConfigRemoveChangeHook(GameOptionsChanged);
    ConfigRemoveChangeHook(ConfigDisplayChanged);
    ConfigRemoveChangeHook(ApplyConfigChange);
    ClockRemovePumpHook(PumpHost);
    DiscordStop();
    sound::Audio().StopOutput();
    InputShutdown();
    gfx::RendererShutdown();
    WindowShutdown();
    return status;
}

} // namespace
} // namespace dc::app

int main(int argc, const char **argv) {
    return dc::app::Run(argc, argv);
}

#ifdef _WIN32
int wmain(int argc, wchar_t **wide_argv) {
    std::vector<std::string>  args;
    std::vector<const char *> argv;
    args.reserve(static_cast<std::size_t>(argc));
    argv.reserve(static_cast<std::size_t>(argc) + 1);
    for (int i = 0; i < argc; ++i) {
        args.push_back(PathsDisplay(fs::path(wide_argv[i])));
    }
    for (const std::string &arg : args) {
        argv.push_back(arg.c_str());
    }
    argv.push_back(nullptr);
    return dc::app::Run(argc, argv.data());
}
#endif
