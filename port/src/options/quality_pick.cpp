#include "options/quality_pick.hpp"

#include <algorithm>
#include <cctype>
#include <print>
#include <string>
#include <string_view>
#include <utility>

#include "options/screen.hpp"

namespace dc::options {

using platform::Config;

namespace {

bool g_pick_waiting = false;

constexpr int kLow = 0;
constexpr int kMedium = 1;
constexpr int kHigh = 2;
constexpr int kUltra = 3;

constexpr const char *kPresetNames[] = {"Low", "Medium", "High", "Ultra"};

// A word of the name that's a model number ending in M or S, as AMD's and Intel's laptop GPUs are named (RX 6800M, RX 7600S, Arc A770M).
bool LaptopModel(std::string_view name) {
    std::size_t start = 0;
    while (start < name.size()) {
        const std::size_t      end = std::min(name.find(' ', start), name.size());
        const std::string_view word = name.substr(start, end - start);
        if (word.size() >= 4 && (word.back() == 'M' || word.back() == 'S') && std::isdigit(static_cast<unsigned char>(word[word.size() - 2])) != 0) {
            return true;
        }
        start = end + 1;
    }
    return false;
}

bool Names(std::string_view name, std::string_view part) {
    return std::ranges::search(name, part, [](char a, char b) { return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b)); })
               .begin() != name.end();
}

// An RTX, or a Radeon RX 6000, 7000 or 9000 (the numbers AMD's desktop cards have had since 2020).
bool Recent(std::string_view name) {
    return Names(name, "RTX") || Names(name, "RX 6") || Names(name, "RX 7") || Names(name, "RX 9");
}

constexpr std::uint64_t GiB(double gib) {
    return static_cast<std::uint64_t>(gib * 1024.0 * 1024.0 * 1024.0);
}

} // namespace

int QualityForGpu(const render::NoktusGpuFacts &gpu) {
    if (gpu.kind == render::NoktusGpuKind::Integrated || gpu.kind == render::NoktusGpuKind::Software) {
        return kLow;
    }
    // A 4 GB card says 4,096 MB, or a little more.
    if (gpu.memory <= GiB(4.5)) {
        return kLow;
    }
    if (Names(gpu.name, "Laptop") || Names(gpu.name, "Mobile") || Names(gpu.name, "Max-Q") || LaptopModel(gpu.name)) {
        return kMedium;
    }
    const int by_memory = gpu.memory < GiB(7.0) ? kMedium : gpu.memory < GiB(11.0) ? kHigh : kUltra;
    return Recent(gpu.name) ? std::max(by_memory, kHigh) : by_memory;
}

bool QualityCheckStretch(Config &config, double gpu_ms, int *stretches) {
    if (config.quality_settled) {
        return false;
    }
    const int preset = platform::ConfigQualityPreset(config);
    if (preset < 0) {
        config.quality_settled = true; // changed by hand: the player's
        return true;
    }
    if (gpu_ms > kQualitySlowMs) {
        if (preset > kLow) {
            platform::ConfigApplyQualityPreset(config, preset - 1);
        }
        config.quality_settled = true;
        return true;
    }
    if (++*stretches >= kQualityCheckStretches) {
        config.quality_settled = true;
        return true;
    }
    return false;
}

namespace {

void PickForGpu() {
    const std::optional<render::NoktusGpuFacts> gpu = render::NoktusGpu();
    if (!gpu) {
        std::println(stderr, "quality: no GPU of Noktus's to pick for: the defaults stay");
        return;
    }
    Config     config = platform::ConfigGet();
    const int  preset = QualityForGpu(*gpu);
    platform::ConfigApplyQualityPreset(config, preset);
    config.quality_settled = false;
    std::println(stderr, "quality: {} for {} ({:.0f} MB of its own), to be checked in play", kPresetNames[preset], gpu->name,
                 static_cast<double>(gpu->memory) / 1048576.0);
    platform::ConfigChange(config);
}

} // namespace

void QualitySettleByHand(const Config &before, Config &after) {
    if (!after.quality_settled && platform::ConfigQualityPreset(before) != platform::ConfigQualityPreset(after)) {
        after.quality_settled = true;
        std::println(stderr, "quality: chosen by hand, so settled");
    }
}

void QualityPickAtFirstTick() {
    g_pick_waiting = true;
}

void QualityCheckTick() {
    static int  stretches = 0;
    static bool options_seen = false; // the stretch under way when Options closed had it over the scene
    if (std::exchange(g_pick_waiting, false)) {
        PickForGpu();
        return;
    }
    if (platform::ConfigGet().quality_settled) {
        return;
    }
    if (g_screen.open) {
        render::NoktusTakePlayGpuMs();
        options_seen = true;
        return;
    }
    const std::optional<double> gpu_ms = render::NoktusTakePlayGpuMs();
    if (!gpu_ms || std::exchange(options_seen, false)) {
        return;
    }
    Config    config = platform::ConfigGet();
    const int before = platform::ConfigQualityPreset(config);
    if (QualityCheckStretch(config, *gpu_ms, &stretches)) {
        const int after = platform::ConfigQualityPreset(config);
        if (after != before && after >= 0) {
            std::println(stderr, "quality: play took {:.1f} ms a frame on the GPU, so {}", *gpu_ms, kPresetNames[after]);
        } else {
            std::println(stderr, "quality: settled, play at {:.1f} ms a frame on the GPU", *gpu_ms);
        }
        platform::ConfigChange(config);
    }
}

} // namespace dc::options
