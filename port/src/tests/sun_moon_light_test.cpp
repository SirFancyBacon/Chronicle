#include "common.h"

#include <gtest/gtest.h>

#include <array>
#include <algorithm>
#include <cmath>

#include "edit.hpp"

namespace dc::test {

using namespace dc::game;

namespace {

float Height(const std::array<float, 3> &towards) {
    return std::atan2(towards[1], std::hypot(towards[0], towards[2]));
}

} // namespace

// At noon (1) the light falls from where the sky draws the sun: straight up, turned by the sky's tilt of half a radian.
TEST(SunAndMoonLight, NoonIsTheSkysSun) {
    const std::array<float, 3> noon = EdSunAndMoonLight(1.0f).towards;
    EXPECT_NEAR(noon[0], 0.0f, 1e-4f);
    EXPECT_NEAR(noon[1], std::cos(0.5f), 1e-4f);
    EXPECT_NEAR(noon[2], std::sin(0.5f), 1e-4f);
}

// The sun sets in the west (x below 0 at 4, as the game's own table had it) and rises in the east (10), held above the horizon.
TEST(SunAndMoonLight, SetsAndRisesOnTheTablesSides) {
    EXPECT_LT(EdSunAndMoonLight(3.9f).towards[0], -0.5f);
    EXPECT_GT(EdSunAndMoonLight(10.1f).towards[0], 0.5f);
}

// Never lower than about 27 degrees, and a unit length, all day.
TEST(SunAndMoonLight, NeverLowAndAlwaysUnit) {
    for (float clock = 0.0f; clock < 12.0f; clock += 0.01f) {
        const std::array<float, 3> towards = EdSunAndMoonLight(clock).towards;
        EXPECT_GE(Height(towards), 0.4636f - 1e-3f) << "at " << clock;
        EXPECT_NEAR(std::hypot(towards[0], towards[1], towards[2]), 1.0f, 1e-4f) << "at " << clock;
    }
}

// The light holds its direction but where it changes from the sun to the moon or back, and there it has faded to next to nothing, so no shadow, faded
// with it, is seen to swing; elsewhere it turns as slowly as the sky does.
TEST(SunAndMoonLight, TurnsOnlyWhileFaint) {
    EdSkyLight before = EdSunAndMoonLight(0.0f);
    for (float clock = 0.01f; clock < 12.0f; clock += 0.01f) {
        const EdSkyLight now = EdSunAndMoonLight(clock);
        const float      dot = before.towards[0] * now.towards[0] + before.towards[1] * now.towards[1] + before.towards[2] * now.towards[2];
        const float      turn = std::acos(std::min(dot, 1.0f));
        if (turn > 0.05f) {
            EXPECT_LT(std::max(before.strength, now.strength), 0.05f) << "a turn of " << turn << " at " << clock;
        }
        before = now;
    }
}

// A shadow fades with the light: none where the light changes from the sun to the moon.
TEST(SunAndMoonLight, ShadowsFadeWithTheLight) {
    EXPECT_FLOAT_EQ(EdSunAndMoonLight(4.75f).strength, 0.0f);
    EXPECT_FLOAT_EQ(EdSunAndMoonLight(9.25f).strength, 0.0f);
}

// Full strength by day and by night, as the presets' light.
TEST(SunAndMoonLight, FullByDayAndNight) {
    EXPECT_FLOAT_EQ(EdSunAndMoonLight(1.0f).strength, 1.0f);
    EXPECT_FLOAT_EQ(EdSunAndMoonLight(7.0f).strength, 1.0f);
}

// At night (7) the light falls from the moon at its height, overhead as the sky draws it.
TEST(SunAndMoonLight, MidnightIsTheMoon) {
    const std::array<float, 3> midnight = EdSunAndMoonLight(7.0f).towards;
    EXPECT_NEAR(midnight[0], 0.0f, 1e-4f);
    EXPECT_NEAR(midnight[1], std::cos(0.5f), 1e-4f);
}

} // namespace dc::test
