#include <gtest/gtest.h>

#include "render/supersampling.hpp"

namespace {

using dc::render::GpuRoom;
using dc::render::MostSupersamplingFor;

constexpr std::uint64_t kMiB = 1024ull * 1024ull;

// The laptop that ran out: a 4 GB T1200 at 3840x2400, the desktop holding some of it.
TEST(SupersamplingRoom, AFourGigabyteGpuAt4KDrawsWithoutSupersampling) {
    EXPECT_EQ(MostSupersamplingFor({.heap = 4096 * kMiB, .budget = 3300 * kMiB}, 3840, 2400, false), 1);
}

TEST(SupersamplingRoom, AnEightGigabyteGpuAt4KTakesTwoNotFour) {
    EXPECT_EQ(MostSupersamplingFor({.heap = 8192 * kMiB, .budget = 6700 * kMiB}, 3840, 2400, false), 2);
}

TEST(SupersamplingRoom, ALargeGpuTakesFour) {
    EXPECT_EQ(MostSupersamplingFor({.heap = 24576 * kMiB, .budget = 22000 * kMiB}, 3840, 2160, true), 4);
}

TEST(SupersamplingRoom, ASmallerWindowLeavesRoomForMore) {
    const GpuRoom room = {.heap = 4096 * kMiB, .budget = 3300 * kMiB};
    EXPECT_GT(MostSupersamplingFor(room, 1920, 1080, false), MostSupersamplingFor(room, 3840, 2400, false));
}

TEST(SupersamplingRoom, WithoutABudgetLessOfTheMemoryIsCountedOn) {
    // 2x at 4K is counted at about 4.3 GB. A 5 GB GPU that allows 4.9 GB has room (90% of it, 4.4 GB); with no budget known, 70% of 5 GB, 3.5 GB, is counted on.
    EXPECT_EQ(MostSupersamplingFor({.heap = 5120 * kMiB, .budget = 0}, 3840, 2400, false), 1);
    EXPECT_EQ(MostSupersamplingFor({.heap = 5120 * kMiB, .budget = 5000 * kMiB}, 3840, 2400, false), 2);
}

TEST(SupersamplingRoom, NothingKnownLimitsNothing) {
    EXPECT_EQ(MostSupersamplingFor({}, 3840, 2400, false), 4);
    EXPECT_EQ(MostSupersamplingFor({.heap = 8192 * kMiB, .budget = 6700 * kMiB}, 0, 0, false), 4);
}

TEST(SupersamplingRoom, OffIsOfferedHoweverLittleRoomThereIs) {
    EXPECT_EQ(MostSupersamplingFor({.heap = 256 * kMiB, .budget = 200 * kMiB}, 7680, 4320, true), 1);
}

} // namespace
