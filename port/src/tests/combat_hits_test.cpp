#include <gtest/gtest.h>

#include <array>
#include <memory>
#include <string>

#include "collisiondata.hpp"
#include "combat/hits.hpp"

namespace dc::test {

using namespace dc::combat;

// A described slot keeps its description until the slot holds a new hit.
TEST(CombatHits, DescriptionFollowsItsSlot) {
    Describe(5, {.attacker = Attacker::Character, .who = 0, .move = Move::Finisher, .step = 5});
    const Hit *hit = Find(5);
    ASSERT_NE(hit, nullptr);
    EXPECT_EQ(hit->attacker, Attacker::Character);
    EXPECT_EQ(hit->move, Move::Finisher);
    EXPECT_EQ(hit->step, 5);

    Reset(5);
    EXPECT_EQ(Find(5)->attacker, Attacker::None);
    EXPECT_EQ(Find(5)->move, Move::Unknown);
}

// CCollisionData::Set hands out a slot with no description, so a reused slot never names the old move.
TEST(CombatHits, CollisionSetForgetsTheOldDescription) {
    auto  collision = std::make_unique<game::CCollisionData>();
    float position[4] = {0.0f, 0.0f, 0.0f, 1.0f};

    Describe(0, {.attacker = Attacker::Character, .who = 2, .move = Move::Charged, .step = 1});
    const int slot = collision->Set(position, 10, 1, 5.0f, 0.0f, 2, 2, 0, 0);
    ASSERT_EQ(slot, 0);
    EXPECT_EQ(Find(slot)->attacker, Attacker::None);
}

TEST(CombatHits, SlotsOutOfRangeAreIgnored) {
    EXPECT_EQ(Find(-1), nullptr);
    EXPECT_EQ(Find(kHitSlots), nullptr);
    Describe(kHitSlots, {.attacker = Attacker::Bomb, .move = Move::Blast});
    Reset(-1);
}

// With no documents every hit's values are 0; a source fills them as a hit is described.
TEST(CombatHits, ValuesComeFromTheSource) {
    Describe(1, {.attacker = Attacker::Character, .who = 0, .move = Move::Combo, .step = 1});
    EXPECT_EQ(Find(1)->poise, 0.0f);
    EXPECT_EQ(Find(1)->stamina, 0.0f);

    SetValueSource([](const Hit &hit) {
        return hit.move == Move::Finisher ? HitValues{.poise = 20.0f, .stamina = 0.0f} : HitValues{.poise = 5.0f, .stamina = 0.0f};
    });
    Describe(2, {.attacker = Attacker::Character, .who = 0, .move = Move::Finisher, .step = 5});
    Describe(3, {.attacker = Attacker::Character, .who = 0, .move = Move::Combo, .step = 2});
    EXPECT_EQ(Find(2)->poise, 20.0f);
    EXPECT_EQ(Find(3)->poise, 5.0f);

    SetValueSource({});
    Describe(4, {.attacker = Attacker::Character, .who = 0, .move = Move::Finisher, .step = 5});
    EXPECT_EQ(Find(4)->poise, 0.0f);
}

// A monster attack is keyed by the motion's name in its model's key table and the frame it rides on.
TEST(CombatHits, AttackKeyNamesTheMotion) {
    const std::array<std::string, 4> names = {"idle", "walk", "", "attack_1"};
    EXPECT_EQ(AttackKey(3, "dcol0", names), "attack_1/dcol0");
    EXPECT_EQ(AttackKey(2, "dcol0", names), "2/dcol0");   // a key with no name
    EXPECT_EQ(AttackKey(13, "dcol1", names), "13/dcol1"); // past the table
    EXPECT_EQ(AttackKey(-1, "shot", names), "shot");      // a projectile
}

TEST(CombatHits, NamesForTheTrace) {
    EXPECT_EQ(Name({.attacker = Attacker::Character, .who = 0, .move = Move::Finisher, .step = 5}), "Toan finisher (5)");
    EXPECT_EQ(Name({.attacker = Attacker::Character, .who = 1, .move = Move::Shot}), "Xiao shot");
    EXPECT_EQ(Name({.attacker = Attacker::Bomb, .move = Move::Blast}), "bomb");
    EXPECT_EQ(Name({}), "undescribed hit");
    EXPECT_EQ(CharacterName(5), "Osmond");
}

} // namespace dc::test
