#include "options/menu_option.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdlib>
#include <iterator>
#include <span>
#include <utility>

#include "memcard.hpp"
#include "options/screen.hpp"
#include "savedata.hpp"
#include "sound.hpp"
#include "userstatus.hpp"

namespace dc::options {

using namespace dc::game;
using namespace dc::platform;

namespace {

// Indices into the save's configuration words (CSaveData::config).
constexpr std::size_t kConfigWordCount = 18;
constexpr std::size_t kWordClockOff = 2;
constexpr std::size_t kWordFastTime = 3;
constexpr std::size_t kWordFastMessages = 4;
constexpr std::size_t kWordMono = 5;
constexpr std::size_t kWordSoftFocusOff = 6;
constexpr std::size_t kWordVibrationOff = 7;
constexpr std::size_t kWordNamesOff = 8;
constexpr std::size_t kWordPlayerDamageOff = 9;
constexpr std::size_t kWordEnemyDamageOff = 10;
constexpr std::size_t kWordEnemyHpOff = 11;
constexpr std::size_t kWordReserved15 = 15;
constexpr std::size_t kWordReserved16 = 16;

// minimap_status that hides the dungeon map.
constexpr int kMapOff = 3;

// The smallest size the list offers from the display's own modes.
constexpr int kMinWidth = 800;
constexpr int kMinHeight = 600;

// 0x0 is the monitor's resolution.
constexpr std::array kResolutions = {
    OptionResolution{.width = 0,    .height = 0   },
    OptionResolution{.width = 1024, .height = 768 },
    OptionResolution{.width = 1280, .height = 720 },
    OptionResolution{.width = 1280, .height = 960 },
    OptionResolution{.width = 1366, .height = 768 },
    OptionResolution{.width = 1440, .height = 1080},
    OptionResolution{.width = 1600, .height = 900 },
    OptionResolution{.width = 1920, .height = 1080},
    OptionResolution{.width = 2560, .height = 1440},
    OptionResolution{.width = 3840, .height = 2160},
};

std::span<s32, kConfigWordCount> ConfigWords(CSaveData &save) {
    return std::span<s32, kConfigWordCount>(static_cast<s32 *>(save.GetConfigData()), kConfigWordCount);
}

void WriteOptions(CSaveData &save, const ConfigGameOptions &options) {
    std::span<s32, kConfigWordCount> words = ConfigWords(save);
    words[kWordClockOff] = !options.clock;
    words[kWordFastTime] = options.fast_time;
    words[kWordFastMessages] = options.fast_messages;
    words[kWordMono] = !options.stereo;
    words[kWordSoftFocusOff] = !options.soft_focus;
    words[kWordVibrationOff] = !options.vibration;
    words[kWordNamesOff] = !options.names;
    words[kWordPlayerDamageOff] = !options.player_damage;
    words[kWordEnemyDamageOff] = !options.enemy_damage;
    words[kWordEnemyHpOff] = !options.enemy_hp;
    reinterpret_cast<CUserStatus *>(save.GetDngStatus())->minimap_status = options.map == 0 ? kMapOff : options.map - 1;
    save.GetMenuCursor()->reset_pos = !options.save_cursor_position;
}

} // namespace

std::vector<OptionResolution> OptionResolutionList(std::span<const DisplayModeSize> modes, int configured_width,
                                                   int configured_height, bool display_known, int display_width,
                                                   int display_height) {
    std::vector<OptionResolution> sizes(kResolutions.begin(), kResolutions.end());
    for (const DisplayModeSize &mode : modes) {
        if (mode.width >= kMinWidth && mode.height >= kMinHeight) {
            sizes.push_back({.width = mode.width, .height = mode.height});
        }
    }
    bool configured = configured_width > 0 && configured_height > 0;
    if (configured) {
        sizes.push_back({.width = configured_width, .height = configured_height});
    }
    // Desktop stays first; the rest run from the smallest area, narrower first.
    std::ranges::sort(sizes.begin() + 1, sizes.end(), [](const OptionResolution &lhs, const OptionResolution &rhs) {
        return std::pair(lhs.width * lhs.height, lhs.width) < std::pair(rhs.width * rhs.height, rhs.width);
    });
    std::vector<OptionResolution> listed;
    for (const OptionResolution &size : sizes) {
        bool fits = !display_known || (size.width <= display_width && size.height <= display_height) ||
                    (configured && size.width == configured_width && size.height == configured_height);
        bool again = !listed.empty() && listed.back().width == size.width && listed.back().height == size.height;
        if (fits && !again) {
            listed.push_back(size);
        }
    }
    return listed;
}

std::vector<OptionResolution> OptionFullscreenResolutionList(int display_width, int display_height, int configured_width, int configured_height) {
    // Sizes of 16:9, 16:10, 21:9, 3:2 and 4:3 displays, besides the fractions of the display's own.
    constexpr OptionResolution kKnown[] = {
        {1280, 720 }, {1600, 900 }, {1920, 1080}, {2560, 1440}, {3200, 1800}, {3840, 2160}, {1280, 800 }, {1440, 900 }, {1680, 1050}, {1920, 1200},
        {2560, 1600}, {2880, 1800}, {2560, 1080}, {3440, 1440}, {1920, 1280}, {2160, 1440}, {2256, 1504}, {1024, 768 }, {1280, 960 }, {1440, 1080},
        {1600, 1200}, {2048, 1536},
    };
    std::vector<OptionResolution> sizes(std::begin(kKnown), std::end(kKnown));
    for (const int part : {2, 3, 4}) {
        const int numerator = part == 2 ? 1 : part - 1; // a half, two thirds, three quarters
        const int height = display_height * numerator / part;
        sizes.push_back({.width = (display_width * numerator / part) / 2 * 2, .height = height / 2 * 2});
    }
    // Of the display's shape to within a pixel's width, and smaller than it.
    const auto shaped = [&](const OptionResolution &size) {
        const long long across = static_cast<long long>(size.width) * display_height;
        const long long down = static_cast<long long>(size.height) * display_width;
        return size.width >= kMinWidth && size.height >= kMinHeight && size.width < display_width && size.height < display_height &&
               std::abs(across - down) <= static_cast<long long>(display_height) * 2;
    };
    std::vector<OptionResolution> listed;
    for (const OptionResolution &size : sizes) {
        if (shaped(size)) {
            listed.push_back(size);
        }
    }
    const bool configured = configured_width > 0 && configured_height > 0 && (configured_width != display_width || configured_height != display_height);
    if (configured) {
        listed.push_back({.width = configured_width, .height = configured_height});
    }
    std::ranges::sort(listed, [](const OptionResolution &lhs, const OptionResolution &rhs) {
        return std::pair(lhs.width * lhs.height, lhs.width) < std::pair(rhs.width * rhs.height, rhs.width);
    });
    // Sizes a pixel or two apart, a fraction beside a known size, are one.
    std::vector<OptionResolution> unique = {{.width = 0, .height = 0}};
    for (const OptionResolution &size : listed) {
        const OptionResolution &last = unique.back();
        const bool configured_one = configured && size.width == configured_width && size.height == configured_height;
        if (last.width != 0 && std::abs(last.width - size.width) <= 2 && std::abs(last.height - size.height) <= 2) {
            if (configured_one) {
                unique.back() = size;
            }
            continue;
        }
        unique.push_back(size);
    }
    return unique;
}

bool MenuOptionOpen() {
    return g_screen.open;
}

void GameOptionsApply(const ConfigGameOptions &options) {
    if (SaveData == nullptr) {
        return;
    }
    WriteOptions(*SaveData, options);
    CMenuCursor *cursor = SaveData->GetMenuCursor();
    if (cursor->reset_pos != 0) {
        cursor->InitPos();
    }
    // Takes config.json's stereo option whatever it is passed.
    CSnd.SetStereoMode(1);
}

void GameOptionsChanged(const Config &before, const Config &after) {
    if (after.options != before.options) {
        GameOptionsApply(after.options);
    }
}

void GameOptionsClear(CSaveData &save) {
    std::span<s32, kConfigWordCount> words = ConfigWords(save);
    std::ranges::fill(words.subspan(kWordClockOff, kWordEnemyHpOff - kWordClockOff + 1), 0);
    words[kWordReserved15] = 0;
    words[kWordReserved16] = 0;
    reinterpret_cast<CUserStatus *>(save.GetDngStatus())->minimap_status = 0;
    save.GetMenuCursor()->reset_pos = 0;
}

bool SoftFocusOn(CSaveData *save) {
    return save == nullptr || ConfigWords(*save)[kWordSoftFocusOff] == 0;
}

} // namespace dc::options
