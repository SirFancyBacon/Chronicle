#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

// Items (docs/ITEM_FRAMEWORK.md). An item is a Def: what every item has, and the traits that say what
// it can do. A system asks whether an item has the trait it cares about rather than what type it is, so
// one item can be goods, bait and a quest item at once. Vanilla items are built from retail's tables
// once the gameplay documents have loaded (item_host.cpp). Like quest_framework.hpp, this header includes
// no PS2 headers.
namespace Items {

// Which of retail's tables holds the item's row (COM_ITEM_INFO::kind).
enum class Kind : uint8_t {
    Attachment,
    Item,
    Weapon,
};

// Where the menus sort a dungeon item (ITEM_DATA::sort_key).
enum class Sort : uint8_t {
    None,
    Food,
    Throw,
    Powder,
    Quest,
    KeyItem,
    Goods,
    Fish,
    Rotten,
};

// The most a quick-use slot holds as retail draws it, and the most its two-digit counter can draw.
inline constexpr int kRetailQuickSlotMax = 9;
inline constexpr int kQuickSlotMaxLimit = 99;

// Using the item uses it up: what it restores and its uses (ITEM_DATA's use_flags, vol and vol_range).
struct Consumable {
    bool heals_hp = false;
    bool restores_water = false;
    bool grants_status = false;
    int  volume = 0;       // the uses a fresh copy starts with
    int  volume_range = 0; // the random variation on volume
};

// The item can be thrown; what it does is itembombeffect.cpp's.
struct Throwable {};

enum class Stack : uint8_t {
    Multi,  // copies stack in one slot
    Single, // sits alone
};

// The item may go in a quick-use slot (ITEM_DATA::stack_kind, other than ITEM_STACK_NEVER).
struct QuickSlot {
    Stack stack = Stack::Multi;
    // The most a stack holds, 1 to kQuickSlotMaxLimit. Kept from the documents; the item menu still
    // stops at kRetailQuickSlotMax until it is rebuilt (section 10).
    int max = kRetailQuickSlotMax;
};

// What the attachment adds to a weapon (ATTACH_DATA).
struct Attachment {
    int attack = 0;
    int endurance = 0;
    int speed = 0;
    int magic = 0;
    int elements[5] = {};    // fire, ice, thunder, wind, holy
    int vs_monster[10] = {}; // one per monster family, as WEAPON_DATA orders them
};

// A weapon's starting stats (WEAPON_DATA). What a weapon does is the weapon framework's.
struct Weapon {
    int owner = 0; // the character: 0 Toan, 1 Xiao, 2 Goro, 3 Ruby, 4 Ungaga, 5 Osmond
    int durability = 0;
    int attack = 0;
    int endurance = 0;
    int speed = 0;
    int magic = 0;
    int attack_max = 0;
    int magic_max = 0;
    int holes = 0;
};

// Fish notice the bait from radius away (esa_info).
struct Bait {
    float radius = 0.0f;
};

// A fish the player keeps as an item (ITEM_SORT_FISH).
struct Fish {};

// What a shop pays and charges (PriceList).
struct Sellable {
    int buy = 0;
    int sell = 0;
};

// A story item (ITEM_SORT_KEY_ITEM).
struct KeyItem {};

// A dungeon's quest item (ITEM_SORT_QUEST).
struct QuestItem {};

// An item: an ID in data, a number at run time (section 5), and its traits.
struct Def {
    std::string id; // namespace:name, vanilla:bread
    int         number = -1;
    Kind        kind = Kind::Item;
    Sort        sort = Sort::None;
    int         icon = -1;    // the item-icon index the menus draw, below zero for none
    int         message = -1; // the name's message id
    std::string model;        // dun/item/main_data/<model>.mds, empty for none

    std::tuple<std::optional<Consumable>, std::optional<Throwable>, std::optional<QuickSlot>,
               std::optional<Attachment>, std::optional<Weapon>, std::optional<Bait>, std::optional<Fish>,
               std::optional<Sellable>, std::optional<KeyItem>, std::optional<QuestItem> >
        traits;

    // The trait, or null where the item does not have it.
    template <class T>
    const T *Get() const {
        const std::optional<T> &trait = std::get<std::optional<T> >(traits);
        return trait ? &*trait : nullptr;
    }

    template <class T>
    bool Has() const {
        return Get<T>() != nullptr;
    }

    template <class T>
    Def &With(T trait) {
        std::get<std::optional<T> >(traits) = std::move(trait);
        return *this;
    }
};

// Whether an ID is namespace:name, each part lower-case letters, digits and underscores.
bool ValidId(std::string_view id);

// Adds an item; throws std::logic_error for an invalid ID, an ID or number already registered, or
// traits that cannot go together (a Weapon that is also Consumable, an Attachment that is also a Weapon).
void Register(Def item);
// Forgets every item (before the tables are loaded again, and for tests).
void Clear();

const Def *Find(std::string_view id);
const Def *Find(int number);
// The item's number, or -1 for an ID nothing registered.
int NumberOf(std::string_view id);
// Every item, in number order.
std::vector<const Def *> All();

// Every item with trait T, in number order.
template <class T>
std::vector<const Def *> With() {
    std::vector<const Def *> items;
    for (const Def *item : All()) {
        if (item->Has<T>()) {
            items.push_back(item);
        }
    }
    return items;
}

} // namespace Items
