#include <gtest/gtest.h>

#include <limits>

#include "nowload.hpp"

namespace dc::test {

using dc::game::AreaCardAlpha;

namespace {

constexpr double kLoading = std::numeric_limits<double>::infinity();

} // namespace

// The area card, the owner's choice of 2026-11-15: eased in over 1.25 seconds, and gone 5 seconds after it showed when the area loaded in time,
// eased out over its last 1.25. Clear at both ends, half way through each fade at half, and moving smoothly both ways.
TEST(NowLoadCardFade, EasesInAndIsGoneFiveSecondsAfterItShowed) {
    constexpr double loaded = 0.5;
    EXPECT_EQ(AreaCardAlpha(0.0, loaded), 0);
    EXPECT_EQ(AreaCardAlpha(0.625, loaded), 0x40);
    EXPECT_EQ(AreaCardAlpha(1.25, loaded), 0x80);
    EXPECT_EQ(AreaCardAlpha(3.7, loaded), 0x80);
    EXPECT_EQ(AreaCardAlpha(4.375, loaded), 0x40);
    EXPECT_EQ(AreaCardAlpha(5.0, loaded), 0);
    EXPECT_EQ(AreaCardAlpha(9.0, loaded), 0);
    EXPECT_LE(AreaCardAlpha(1.25 / 12, loaded), 3); // eased: 2% a twelfth of the way in, not the 8% a straight fade would be
    int last = 0;
    for (double t = 0.0; t <= 1.25; t += 0.005) {
        const int alpha = AreaCardAlpha(t, loaded);
        EXPECT_GE(alpha, last) << t;
        EXPECT_LE(alpha - last, 1) << t;
        last = alpha;
    }
    for (double t = 3.75; t <= 5.0; t += 0.005) {
        const int alpha = AreaCardAlpha(t, loaded);
        EXPECT_LE(alpha, last) << t;
        EXPECT_LE(last - alpha, 1) << t;
        last = alpha;
    }
    EXPECT_EQ(last, 0);
}

// While the area is still loading the card holds, however long; once it has loaded late, it eases out over 1.25 seconds from then.
TEST(NowLoadCardFade, HoldsWhileLoadingAndEasesOutAfterALateLoad) {
    EXPECT_EQ(AreaCardAlpha(4.5, kLoading), 0x80);
    EXPECT_EQ(AreaCardAlpha(30.0, kLoading), 0x80);
    EXPECT_EQ(AreaCardAlpha(7.0, 7.0), 0x80);
    EXPECT_EQ(AreaCardAlpha(7.625, 7.0), 0x40);
    EXPECT_EQ(AreaCardAlpha(8.25, 7.0), 0);
    // Loaded before it had even eased in, it still eases in before it eases out.
    EXPECT_EQ(AreaCardAlpha(0.625, 0.1), 0x40);
}

} // namespace dc::test
