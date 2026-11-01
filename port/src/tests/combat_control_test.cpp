#include <gtest/gtest.h>

#include <vector>

#include "combat/control.hpp"

namespace dc::test {

using namespace dc::combat;

namespace {

// The Skeleton Soldier's key table (e03a), as its scripts number its motions: name, first frame, frame
// after the last, and for its down, the speed it plays at.
const std::vector<MotionKey> kSkeleton = {
    {"idle", 10, 20},
    {"walk", 25, 45},
    {"back_step", 295, 305},
    {"right_step", 230, 250},
    {"left_step", 255, 275},
    {"guard_enter", 100, 105},
    {"guard_loop", 105, 115},
    {"guard_return", 115, 120},
    {"damage_1", 125, 135},
    {"damage_2", 140, 165, 0.2f},
    {"get_up", 165, 185, 0.2f},
    {"die", 205, 225},
    {"death_loop", 225, 225},
    {"attack_1", 50, 70}
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

// The Skeleton falls to the ground, rolls on it until it lies still, then gets up as far as its feet.
TEST(CombatControl, DownIsTheFallTheRollAndTheGetUp) {
    const auto down = DownReaction("e03a", kSkeleton);
    ASSERT_TRUE(down);
    ASSERT_EQ(down->steps.size(), 3u);
    EXPECT_EQ(down->steps[0].motion, "damage_2");
    EXPECT_EQ(down->steps[0].until, 4);
    EXPECT_EQ(down->steps[1].motion, "damage_2");
    EXPECT_TRUE(down->steps[1].carry_on);
    EXPECT_EQ(down->steps[1].until, 20);
    EXPECT_EQ(down->steps[2].motion, "get_up");
    EXPECT_EQ(down->steps[2].until, 9);
    EXPECT_FALSE(DownReaction("c14a", kUtan));
}

// Only the roll is sped up: the fall's 4 frames and the get-up's 9 keep their own speed, and the roll's 16
// fill what is left of the two seconds.
TEST(CombatControl, DownLastsTwoSecondsOnlyTheGroundGivingWay) {
    const auto down = DownReaction("e03a", kSkeleton);
    ASSERT_TRUE(down);
    EXPECT_LT(down->steps[0].speed, 0.0f);
    EXPECT_LT(down->steps[2].speed, 0.0f);
    const float roll = down->steps[1].speed;
    EXPECT_GT(roll, 0.2f);
    EXPECT_FLOAT_EQ((4.0f / 0.2f) + (16.0f / roll) + (9.0f / 0.2f), 120.0f);
}

// A model whose frames aren't known plays its fall and get-up whole, at their own speed.
TEST(CombatControl, AnUnknownModelFallsWhole) {
    const auto down = DownReaction("e999a", kSkeleton);
    ASSERT_TRUE(down);
    ASSERT_EQ(down->steps.size(), 2u);
    EXPECT_LT(down->steps[0].until, 0);
    EXPECT_LT(down->steps[0].speed, 0.0f);
    EXPECT_EQ(down->steps[1].motion, "get_up");
}

// A model whose damage_2 and get_up sit on its idle frames can't fall: it staggers with damage_1.
TEST(CombatControl, PlaceholderKeysAreNotADown) {
    EXPECT_FALSE(DownReaction("e06a", kDasher));
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
    EXPECT_EQ(StepHold(3, 0, true).action, HoldAction::None);
    EXPECT_EQ(StepHold(3, 1, false).action, HoldAction::None);
    const HoldTick second = StepHold(3, 2, true);
    EXPECT_EQ(second.action, HoldAction::Play);
    EXPECT_EQ(second.step, 1u);

    // A loop ends after its ticks.
    EXPECT_EQ(StepHold(3, 0, true).action, HoldAction::None);
    EXPECT_EQ(StepHold(3, 1, false).action, HoldAction::None);
    EXPECT_EQ(StepHold(3, 2, false).action, HoldAction::Release);
    EXPECT_FALSE(Held(3));
    EXPECT_EQ(StepHold(3, 0, true).action, HoldAction::None);
}

// A step with an until frame ends there, before its motion does.
TEST(CombatControl, AStepEndsAtItsFrame) {
    Hold(4, Reaction{
                .steps = {{.motion = "damage_2", .until = 4}, {.motion = "damage_2", .until = 20, .carry_on = true}}
    });
    EXPECT_EQ(StepHold(4, 0.2f, false).action, HoldAction::None);
    EXPECT_EQ(StepHold(4, 3.8f, false).action, HoldAction::None);
    EXPECT_EQ(StepHold(4, 4.0f, false).action, HoldAction::Play);
    EXPECT_EQ(StepHold(4, 19.0f, false).action, HoldAction::None);
    EXPECT_EQ(StepHold(4, 20.1f, false).action, HoldAction::Release);
}

// A swing at chest height passes over a lying monster's sphere, but reaches it as a column of the height
// it stood; one beside it, or above that height, still misses.
TEST(CombatControl, ADownedSphereIsAColumn) {
    const float lying[3] = {0.0f, 0.5f, 0.0f};
    const float swing[3] = {1.0f, 8.0f, 0.5f};
    EXPECT_TRUE(ReachesDowned(swing, 1.0f, lying, 1.5f, 0.0f, 12.0f));
    const float wide[3] = {4.0f, 8.0f, 0.0f};
    EXPECT_FALSE(ReachesDowned(wide, 1.0f, lying, 1.5f, 0.0f, 12.0f));
    const float high[3] = {1.0f, 15.0f, 0.5f};
    EXPECT_FALSE(ReachesDowned(high, 1.0f, lying, 1.5f, 0.0f, 12.0f));
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
