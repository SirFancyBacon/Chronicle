#include <gtest/gtest.h>

#include <cstdint>
#include <string>

#include "options/quality_pick.hpp"
#include "platform/config.hpp"

using namespace dc;
using dc::platform::Config;

namespace {

constexpr std::uint64_t kMiB = 1024ull * 1024ull;

int Pick(render::NoktusGpuKind kind, std::string name, std::uint64_t mib) {
    return options::QualityForGpu({.name = std::move(name), .kind = kind, .memory = mib * kMiB});
}

constexpr auto kDiscrete = render::NoktusGpuKind::Discrete;

} // namespace

TEST(QualityPick, TheOwnersTable) {
    // Integrated and software GPUs, however much of the system's memory they may use: Low.
    EXPECT_EQ(Pick(render::NoktusGpuKind::Integrated, "AMD Radeon Graphics (RADV PHOENIX)", 16384), 0);
    EXPECT_EQ(Pick(render::NoktusGpuKind::Software, "llvmpipe (LLVM 20.1.0, 256 bits)", 32768), 0);
    // 4 GB or less: Low, a laptop's or a desktop's.
    EXPECT_EQ(Pick(kDiscrete, "NVIDIA T1200 Laptop GPU", 4096), 0);
    EXPECT_EQ(Pick(kDiscrete, "AMD Radeon RX 6500 XT", 4096), 0);
    // A laptop's with more: Medium, RTX or not, Radeon or Arc by its model's M or S.
    EXPECT_EQ(Pick(kDiscrete, "NVIDIA GeForce RTX 3060 Laptop GPU", 6144), 1);
    EXPECT_EQ(Pick(kDiscrete, "NVIDIA GeForce GTX 1070 with Max-Q Design", 8192), 1);
    EXPECT_EQ(Pick(kDiscrete, "AMD Radeon RX 6800M", 12272), 1);
    EXPECT_EQ(Pick(kDiscrete, "AMD Radeon RX 7600S", 8176), 1);
    EXPECT_EQ(Pick(kDiscrete, "Intel(R) Arc(TM) A770M Graphics", 16032), 1);
    // A desktop's by memory...
    EXPECT_EQ(Pick(kDiscrete, "NVIDIA GeForce GTX 1060 6GB", 6144), 1);
    EXPECT_EQ(Pick(kDiscrete, "AMD Radeon RX 580 Series", 8192), 2);
    EXPECT_EQ(Pick(kDiscrete, "NVIDIA GeForce RTX 3070", 8192), 2);
    EXPECT_EQ(Pick(kDiscrete, "NVIDIA GeForce RTX 4090", 24564), 3);
    // ...an RTX or a recent Radeon at least High.
    EXPECT_EQ(Pick(kDiscrete, "NVIDIA GeForce RTX 3050", 6144), 2);
    EXPECT_EQ(Pick(kDiscrete, "AMD Radeon RX 7600", 8192), 2);
    EXPECT_EQ(Pick(kDiscrete, "AMD Radeon RX 9070 XT", 16304), 3);
    // Neither Vulkan's kind nor a name known: by memory.
    EXPECT_EQ(Pick(render::NoktusGpuKind::Other, "Virtual GPU", 2048), 0);
}

TEST(QualityPick, CheckedInPlay) {
    Config high;
    high.quality_settled = false;
    // Play fast enough: counted, then settled at the last stretch, the preset kept.
    {
        Config config = high;
        int    stretches = 0;
        for (int i = 1; i < options::kQualityCheckStretches; i++) {
            EXPECT_FALSE(options::QualityCheckStretch(config, options::kQualitySlowMs - 1.0, &stretches));
        }
        EXPECT_TRUE(options::QualityCheckStretch(config, options::kQualitySlowMs - 1.0, &stretches));
        EXPECT_TRUE(config.quality_settled);
        EXPECT_EQ(platform::ConfigQualityPreset(config), 2);
        EXPECT_FALSE(options::QualityCheckStretch(config, options::kQualitySlowMs + 10.0, &stretches)); // settled: no more
    }
    // A slow stretch: a preset down, once.
    {
        Config config = high;
        int    stretches = 0;
        EXPECT_TRUE(options::QualityCheckStretch(config, options::kQualitySlowMs + 1.0, &stretches));
        EXPECT_TRUE(config.quality_settled);
        EXPECT_EQ(platform::ConfigQualityPreset(config), 1);
        EXPECT_FALSE(options::QualityCheckStretch(config, options::kQualitySlowMs + 1.0, &stretches));
        EXPECT_EQ(platform::ConfigQualityPreset(config), 1);
    }
    // Low has nowhere lower.
    {
        Config config = high;
        platform::ConfigApplyQualityPreset(config, 0);
        int stretches = 0;
        EXPECT_TRUE(options::QualityCheckStretch(config, options::kQualitySlowMs + 1.0, &stretches));
        EXPECT_EQ(platform::ConfigQualityPreset(config), 0);
        EXPECT_TRUE(config.quality_settled);
    }
    // Changed by hand meanwhile: left as it is.
    {
        Config config = high;
        config.point_lights = 64;
        int stretches = 0;
        EXPECT_TRUE(options::QualityCheckStretch(config, options::kQualitySlowMs + 1.0, &stretches));
        EXPECT_TRUE(config.quality_settled);
        EXPECT_EQ(config.point_lights, 64);
        EXPECT_EQ(config.shadow_quality, 2);
    }
}

TEST(QualityPick, ChosenByHand) {
    Config before;
    before.quality_settled = false;
    // Another preset picked, or a row changed: the player's, settled.
    Config ultra = before;
    platform::ConfigApplyQualityPreset(ultra, 3);
    options::QualitySettleByHand(before, ultra);
    EXPECT_TRUE(ultra.quality_settled);
    Config custom = before;
    custom.glow = false;
    options::QualitySettleByHand(before, custom);
    EXPECT_TRUE(custom.quality_settled);
    // Anything else (the volume): the check goes on.
    Config louder = before;
    louder.master_volume = 0.5f;
    options::QualitySettleByHand(before, louder);
    EXPECT_FALSE(louder.quality_settled);
}

TEST(QualityPick, SettledIsKept) {
    EXPECT_TRUE(platform::ConfigParse("").quality_settled);
    EXPECT_FALSE(platform::ConfigParse(R"({"video": {"quality_settled": false}})").quality_settled);
    EXPECT_TRUE(platform::ConfigParse(R"({"video": {"quality_settled": 3}})").quality_settled);
    Config unsettled;
    unsettled.quality_settled = false;
    EXPECT_FALSE(platform::ConfigParse(platform::ConfigSerialize(unsettled)).quality_settled);
}
