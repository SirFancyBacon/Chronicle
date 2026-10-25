#include <gtest/gtest.h>

#include <stdexcept>
#include <string>

#include "item_framework.hpp"

namespace {

class ItemFramework : public ::testing::Test {
protected:
    void SetUp() override { Items::Clear(); }

    void TearDown() override { Items::Clear(); }

    static Items::Def Item(std::string id, int number) {
        Items::Def item;
        item.id = std::move(id);
        item.number = number;
        return item;
    }
};

} // namespace

TEST_F(ItemFramework, ItemsAreFoundByIdAndByNumber) {
    Items::Register(Item("test:bread", 148).With(Items::Sellable{.buy = 20, .sell = 10}));
    const Items::Def *bread = Items::Find("test:bread");
    ASSERT_NE(bread, nullptr);
    EXPECT_EQ(Items::Find(148), bread);
    EXPECT_EQ(Items::NumberOf("test:bread"), 148);
    EXPECT_EQ(Items::NumberOf("test:cake"), -1);
    EXPECT_EQ(Items::Find(149), nullptr);
    ASSERT_NE(bread->Get<Items::Sellable>(), nullptr);
    EXPECT_EQ(bread->Get<Items::Sellable>()->buy, 20);
    EXPECT_FALSE(bread->Has<Items::Bait>());
}

TEST_F(ItemFramework, TheRegistryRefusesItemsThatCannotWork) {
    Items::Register(Item("test:bread", 148));
    EXPECT_THROW(Items::Register(Item("test:bread", 149)), std::logic_error);
    EXPECT_THROW(Items::Register(Item("test:cake", 148)), std::logic_error);
    EXPECT_THROW(Items::Register(Item("bread", 150)), std::logic_error);
    EXPECT_THROW(Items::Register(Item("Test:Cake", 150)), std::logic_error);
    EXPECT_THROW(Items::Register(Item("test:", 150)), std::logic_error);
    EXPECT_THROW(Items::Register(Item("test:sword", 150).With(Items::Weapon{}).With(Items::Consumable{})), std::logic_error);
    EXPECT_THROW(Items::Register(Item("test:sword", 150).With(Items::Weapon{}).With(Items::Attachment{})), std::logic_error);
    EXPECT_THROW(Items::Register(Item("test:cake", 150).With(Items::QuickSlot{.max = 0})), std::logic_error);
    EXPECT_THROW(Items::Register(Item("test:cake", 150).With(Items::QuickSlot{.max = Items::kQuickSlotMaxLimit + 1})),
                 std::logic_error);
    // Nothing a refusal saw is kept.
    EXPECT_EQ(Items::All().size(), 1u);
    Items::Register(Item("test:cake", 150).With(Items::QuickSlot{.max = Items::kQuickSlotMaxLimit}));
}

TEST_F(ItemFramework, ItemsWithATraitComeInNumberOrder) {
    Items::Register(Item("test:minon", 188).With(Items::Bait{.radius = 30.0f}));
    Items::Register(Item("test:bread", 148));
    Items::Register(Item("test:carrot", 186).With(Items::Bait{.radius = 20.0f}).With(Items::Consumable{}));
    const auto baits = Items::With<Items::Bait>();
    ASSERT_EQ(baits.size(), 2u);
    EXPECT_EQ(baits[0]->id, "test:carrot");
    EXPECT_EQ(baits[1]->id, "test:minon");
    EXPECT_EQ(Items::All().front()->id, "test:bread");

    Items::Clear();
    EXPECT_TRUE(Items::All().empty());
    EXPECT_EQ(Items::Find("test:bread"), nullptr);
}
