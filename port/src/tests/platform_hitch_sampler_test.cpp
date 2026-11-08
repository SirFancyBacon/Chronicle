#include <gtest/gtest.h>

#include <chrono>
#include <mutex>
#include <string>
#include <vector>

#include "platform/hitch_sampler.hpp"

namespace dc::test {

using namespace dc::platform;

TEST(PlatformHitchSampler, NamesTheStacksAndFunctionsFoundMost) {
    std::vector<std::vector<std::string>> stacks;
    for (int i = 0; i < 6; i++) {
        stacks.push_back({"memcpy", "Upload", "OpenMenu", "Tick", "main"});
    }
    for (int i = 0; i < 3; i++) {
        stacks.push_back({"Wait", "Upload", "OpenMenu", "Tick", "main"});
    }
    stacks.push_back({"Draw", "Tick", "main"});
    const std::vector<std::string> lines = HitchSamplesDescribe("frame 7", stacks, 9.4);
    ASSERT_EQ(lines.size(), 5u);
    EXPECT_EQ(lines[0], "hitch: frame 7, the game's thread looked at 10 times in 9 ms, found most in (innermost first):");
    EXPECT_EQ(lines[1], "hitch:   60% memcpy < Upload < OpenMenu < Tick < main");
    EXPECT_EQ(lines[2], "hitch:   30% Wait < Upload < OpenMenu < Tick < main");
    EXPECT_EQ(lines[3], "hitch:   10% Draw < Tick < main");
    // Among functions found as often, the callee comes first.
    EXPECT_EQ(lines[4], "hitch:   anywhere on the stack: 100% Tick, 100% main, 90% Upload, 90% OpenMenu, 60% memcpy, 30% Wait, 10% Draw");
    EXPECT_TRUE(HitchSamplesDescribe("frame 8", {}, 0.0).empty());
}

#if defined(__linux__) && (defined(__x86_64__) || defined(__aarch64__))
TEST(PlatformHitchSampler, SamplesALongTickOfTheThreadItWasAttachedTo) {
    std::mutex               lock;
    std::vector<std::string> lines;
    HitchSamplerSetSink([&](const std::string &line) {
        const std::scoped_lock hold(lock);
        lines.push_back(line);
    });
    HitchSamplerAttach(5);
    HitchSamplerTickBegins();
    const auto        until = std::chrono::steady_clock::now() + std::chrono::milliseconds(60);
    volatile unsigned spin = 0;
    while (std::chrono::steady_clock::now() < until) {
        spin = spin + 1;
    }
    HitchSamplerTickEnds();
    HitchSamplerReport("frame 1");
    HitchSamplerStop(); // writes the report first
    HitchSamplerSetSink({});
    ASSERT_GE(lines.size(), 3u);
    EXPECT_TRUE(lines[0].starts_with("hitch: frame 1, the game's thread looked at ")) << lines[0];
}
#endif

} // namespace dc::test
