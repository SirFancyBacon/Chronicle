#include <gtest/gtest.h>

#include <vector>

#include "combat/control.hpp"

namespace dc::test {

using namespace dc::combat;

namespace {

// The Skeleton Soldier's key table (e03a), as its scripts number its motions: name, first frame, frame
// after the last.
const std::vector<MotionKey> kSkeleton = {
    {"idle",         10,  20 },
    {"walk",         25,  45 },
    {"back_step",    295, 305},
    {"right_step",   230, 250},
    {"left_step",    255, 275},
    {"guard_enter",  100, 105},
    {"guard_loop",   105, 115},
    {"guard_return", 115, 120},
    {"damage_1",     125, 135},
    {"damage_2",     140, 165},
    {"get_up",       165, 185},
    {"die",          205, 225},
    {"death_loop",   225, 225},
    {"attack_1",     50,  70 }
};

// The Dasher's (e06a): its damage_2 and get_up are placeholders on its idle frames.
const std::vector<MotionKey> kDasher = {
    {"idle",         10,  20 },
    {"walk",         30,  50 },
    {"back_step",    120, 140},
    {"right_step",   60,  80 },
    {"left_step",    90,  110},
    {"guard_enter",  10,  20 },
    {"guard_loop",   10,  20 },
    {"guard_return", 10,  20 },
    {"damage_1",     240, 260},
    {"damage_2",     10,  20 },
    {"get_up",       10,  20 },
    {"die",          270, 290}
};

// Master Utan's (c14a); the frames are only told apart, not his own.
const std::vector<MotionKey> kUtan = {
    {"idle",                     10,  20 },
    {"walk",                     20,  30 },
    {"run",                      30,  40 },
    {"normal_attack",            40,  50 },
    {"charge_attack",            50,  60 },
    {"seed_machine_gun",         60,  70 },
    {"damage_left_foot_enter",   70,  87 },
    {"panting_loop",             87,  103},
    {"damage_left_foot_return",  103, 108},
    {"damage_right_foot_enter",  110, 127},
    {"panting_loop_v2",          127, 143},
    {"damage_right_foot_return", 143, 148},
    {"damage",                   150, 164}
};

} // namespace

TEST(CombatControl, DownIsTheHeavyHitThenTheGetUp) {
    const auto down = DownReaction(kSkeleton);
    ASSERT_TRUE(down);
    ASSERT_EQ(down->steps.size(), 2u);
    EXPECT_EQ(down->steps[0].motion, "damage_2");
    EXPECT_EQ(down->steps[1].motion, "get_up");
    EXPECT_FALSE(DownReaction(kUtan));
}

// A model whose damage_2 and get_up sit on its idle frames can't fall: it staggers with damage_1.
TEST(CombatControl, PlaceholderKeysAreNotADown) {
    EXPECT_FALSE(DownReaction(kDasher));
    const auto stagger = StaggerReaction("e06a", kDasher);
    ASSERT_TRUE(stagger);
    EXPECT_EQ(stagger->steps[0].motion, "damage_1");
}

TEST(CombatControl, MasterUtanKneelsWithTheLockOnOnHisFace) {
    const auto stagger = StaggerReaction("c14a", kUtan);
    ASSERT_TRUE(stagger);
    ASSERT_EQ(stagger->steps.size(), 3u);
    EXPECT_EQ(stagger->steps[0].motion, "damage_left_foot_enter");
    EXPECT_EQ(stagger->steps[1].motion, "panting_loop");
    EXPECT_GT(stagger->steps[1].loop_ticks, 0);
    EXPECT_EQ(stagger->steps[2].motion, "damage_left_foot_return");
    EXPECT_EQ(stagger->lock_on, "face");
}

// Another model staggers with its own hit reaction, then stands a moment.
TEST(CombatControl, AStaggerIsTheHitReactionThenStanding) {
    const auto stagger = StaggerReaction("e03a", kSkeleton);
    ASSERT_TRUE(stagger);
    ASSERT_EQ(stagger->steps.size(), 2u);
    EXPECT_EQ(stagger->steps[0].motion, "damage_1");
    EXPECT_EQ(stagger->steps[1].motion, "idle");
    EXPECT_GT(stagger->steps[1].loop_ticks, 0);
    EXPECT_FALSE(StaggerReaction("c23a", std::vector<MotionKey>{
                                             {"base_idle_0", 10, 20},
                                             {"left_slap_4", 30, 50}
    }));
}

TEST(CombatControl, AHoldPlaysItsStepsThenReleases) {
    const HoldTick first = Hold(3, Reaction{
                                       .name = "test", .steps = {{.motion = "damage_2"}, {.motion = "idle", .loop_ticks = 3}}
    });
    EXPECT_EQ(first.action, HoldAction::Play);
    EXPECT_EQ(first.step, 0u);
    EXPECT_TRUE(Held(3));

    // A step played once doesn't end on the tick it started, whatever the motion's frame says.
    EXPECT_EQ(StepHold(3, true).action, HoldAction::None);
    EXPECT_EQ(StepHold(3, false).action, HoldAction::None);
    const HoldTick second = StepHold(3, true);
    EXPECT_EQ(second.action, HoldAction::Play);
    EXPECT_EQ(second.step, 1u);

    // A loop ends after its ticks.
    EXPECT_EQ(StepHold(3, true).action, HoldAction::None);
    EXPECT_EQ(StepHold(3, false).action, HoldAction::None);
    EXPECT_EQ(StepHold(3, false).action, HoldAction::Release);
    EXPECT_FALSE(Held(3));
    EXPECT_EQ(StepHold(3, true).action, HoldAction::None);
}

TEST(CombatControl, ReleasingForgetsTheHold) {
    Hold(1, Reaction{.steps = {{.motion = "damage_1"}}});
    Hold(2, Reaction{.steps = {{.motion = "damage_1"}}});
    Release(1);
    EXPECT_FALSE(Held(1));
    EXPECT_TRUE(Held(2));
    ReleaseAll();
    EXPECT_FALSE(Held(2));
    EXPECT_EQ(HeldReaction(2), nullptr);
}

TEST(CombatControl, AnEmptyReactionOrASlotOutOfRangeHoldsNothing) {
    EXPECT_EQ(Hold(0, Reaction{}).action, HoldAction::Release);
    EXPECT_FALSE(Held(0));
    EXPECT_EQ(Hold(kMonsterSlots, Reaction{.steps = {{.motion = "damage_1"}}}).action, HoldAction::Release);
    EXPECT_FALSE(Held(-1));
}

} // namespace dc::test
