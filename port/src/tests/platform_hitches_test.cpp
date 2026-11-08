#include <gtest/gtest.h>

#include <optional>
#include <string>

#include "platform/hitches.hpp"

namespace dc::test {

using namespace dc::platform;

namespace {

TickWork Took(double work, double handing = 0.0) {
    TickWork tick;
    tick.work = work;
    tick.handing = handing;
    return tick;
}

} // namespace

TEST(PlatformHitches, TicksInTimeSayNothing) {
    HitchLog log(1.0 / 60.0);
    EXPECT_FALSE(log.Note(Took(0.010, 0.002), 1).has_value());
    EXPECT_FALSE(log.Note(Took(0.016), 2).has_value());
    EXPECT_FALSE(log.Flush().has_value());
}

TEST(PlatformHitches, ALongTickIsToldWhenTheNextIsInTime) {
    HitchLog log(1.0 / 60.0);
    TickWork slow = Took(0.030, 0.004);
    slow.loads[static_cast<unsigned>(load_profile::Kind::Read)] = 0.012;
    slow.loads[static_cast<unsigned>(load_profile::Kind::Texture)] = 0.008;
    EXPECT_FALSE(log.Note(slow, 100).has_value());
    const std::optional<std::string> line = log.Note(Took(0.005), 101);
    ASSERT_TRUE(line.has_value());
    EXPECT_EQ(*line, "hitch: frame 100, a tick of 16.7 ms took the game 30.0 ms (and handing it over 4.0); of it textures 8.0, reads 12.0");
    EXPECT_FALSE(log.Note(Took(0.005), 102).has_value());
}

TEST(PlatformHitches, HandingOverIsntCounted) {
    HitchLog log(1.0 / 60.0);
    EXPECT_FALSE(log.Note(Took(0.010, 0.050), 7).has_value());
    EXPECT_FALSE(log.Flush().has_value());
}

TEST(PlatformHitches, ARunOfLongTicksIsToldAsOne) {
    HitchLog log(1.0 / 60.0);
    EXPECT_FALSE(log.Note(Took(0.020), 10).has_value());
    EXPECT_FALSE(log.Note(Took(0.050), 11).has_value());
    EXPECT_FALSE(log.Note(Took(0.030), 12).has_value());
    const std::optional<std::string> line = log.Flush();
    ASSERT_TRUE(line.has_value());
    EXPECT_EQ(*line, "hitch: frames 10 to 12, 3 ticks of 16.7 ms in a row took the game 100.0 ms, the longest 50.0 ms (and handing them over 0.0)");
    EXPECT_FALSE(log.Flush().has_value());
}

TEST(PlatformHitches, FileReadsSayHowManyFiles) {
    HitchLog log(1.0 / 60.0);
    TickWork slow = Took(0.040);
    slow.loads[static_cast<unsigned>(load_profile::Kind::Files)] = 0.025;
    slow.files = 37;
    EXPECT_FALSE(log.Note(slow, 5).has_value());
    const std::optional<std::string> line = log.Flush();
    ASSERT_TRUE(line.has_value());
    EXPECT_NE(line->find("file reads 25.0 (37 files)"), std::string::npos) << *line;
}

TEST(PlatformHitches, TheTickLengthFollowsTheTickRate) {
    HitchLog log(1.0 / 60.0);
    log.SetTickLength(1.0 / 30.0);
    EXPECT_FALSE(log.Note(Took(0.025), 1).has_value());
    EXPECT_FALSE(log.Flush().has_value());
}

} // namespace dc::test
