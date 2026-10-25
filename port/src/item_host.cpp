#include "item_host.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <functional>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <vector>

#include "btitem.hpp"
#include "item_framework.hpp"
#include "itemdata.hpp"
#include "platform/native_config.hpp"

// ps2/src/itemdata.cpp's tables that its header does not declare.
extern COM_ITEM_INFO ComItemInfo[296];
extern ATTACH_DATA   AttachList[50];
extern WEAPON_DATA   WeaponList[120];

// ps2/src/shop.cpp's prices, from ITEM_ATTACH_START on, which no header declares.
struct ITEM_MONEY_ENTRY {
    s16 buy_price;
    s16 sell_price;
};

extern ITEM_MONEY_ENTRY PriceList[296];

// ps2/src/fishing.cpp's baits, static there, given a link name by port/include/stubs/fishing.hpp.
struct EsaInfo {
    int   item_no;
    float radius;
};

extern EsaInfo esa_info[13] asm("Fishing_esa_info");

namespace {

using native_config::Json;

enum class Width { S8, U8, S16, U16, S32, U32, F32 };

// One field of a table's rows, by its retail name.
struct Field {
    const char *name;
    std::size_t offset;
    Width       width;
    int         count; // elements of an array; 1 for a single value
    // Decides where a save's copies can sit (an item's kind and index, a weapon's owner and holes), so
    // it comes from the extraction only and a document that changes it is refused (section 6).
    bool fixed;
};

template <class M>
constexpr int Elements() {
    if constexpr (std::is_array_v<M>) {
        return static_cast<int>(std::extent_v<M>);
    } else {
        return 1;
    }
}

#define ITEM_FIELD(T, member, width) Field{#member, offsetof(T, member), Width::width, Elements<decltype(T::member)>(), false}
#define ITEM_FIXED(T, member, width) Field{#member, offsetof(T, member), Width::width, Elements<decltype(T::member)>(), true}

constexpr Field kCommonFields[] = {
    ITEM_FIXED(COM_ITEM_INFO, kind, S8),        ITEM_FIELD(COM_ITEM_INFO, unk_01, U8),
    ITEM_FIXED(COM_ITEM_INFO, index, S16),      ITEM_FIELD(COM_ITEM_INFO, icon_index, S16),
    ITEM_FIELD(COM_ITEM_INFO, msg, S16),
};

constexpr Field kItemFields[] = {
    ITEM_FIELD(ITEM_DATA, sort_key, S16),   ITEM_FIELD(ITEM_DATA, unk_02, S16),    ITEM_FIELD(ITEM_DATA, use_flags, U32),
    ITEM_FIELD(ITEM_DATA, kind_flags, S16), ITEM_FIELD(ITEM_DATA, vol, S16),       ITEM_FIELD(ITEM_DATA, vol_range, S16),
    ITEM_FIELD(ITEM_DATA, unk_0E, S16),     ITEM_FIELD(ITEM_DATA, stack_kind, S32),
};

// An attachment's own item_no is its row's.
constexpr Field kAttachFields[] = {
    ITEM_FIELD(ATTACH_DATA, sphere_weapon_no, S16), ITEM_FIELD(ATTACH_DATA, sphere_flags, S16),
    ITEM_FIELD(ATTACH_DATA, sphere_level, S16),     ITEM_FIELD(ATTACH_DATA, attack, S16),
    ITEM_FIELD(ATTACH_DATA, endurance, S16),        ITEM_FIELD(ATTACH_DATA, speed, S16),
    ITEM_FIELD(ATTACH_DATA, magic, S16),            ITEM_FIELD(ATTACH_DATA, elem, U8),
    ITEM_FIELD(ATTACH_DATA, vs_monster, U8),        ITEM_FIELD(ATTACH_DATA, unk_1F, U8),
};

constexpr Field kWeaponFields[] = {
    ITEM_FIELD(WEAPON_DATA, durability, S16),    ITEM_FIELD(WEAPON_DATA, attack, S16),
    ITEM_FIELD(WEAPON_DATA, endurance, S16),     ITEM_FIELD(WEAPON_DATA, speed, S16),
    ITEM_FIELD(WEAPON_DATA, magic, S16),         ITEM_FIXED(WEAPON_DATA, owner, U8),
    ITEM_FIELD(WEAPON_DATA, hole, S8),           ITEM_FIXED(WEAPON_DATA, hole_num, U8),
    ITEM_FIELD(WEAPON_DATA, elem, S16),          ITEM_FIELD(WEAPON_DATA, vs_monster, S16),
    ITEM_FIELD(WEAPON_DATA, exp_base, S16),      ITEM_FIELD(WEAPON_DATA, exp_per_level, S16),
    ITEM_FIELD(WEAPON_DATA, unk_34, S16),        ITEM_FIELD(WEAPON_DATA, unk_36, S16),
    ITEM_FIELD(WEAPON_DATA, flags, S16),         ITEM_FIELD(WEAPON_DATA, unk_3A, S16),
    ITEM_FIELD(WEAPON_DATA, buildup_mask0, U32), ITEM_FIELD(WEAPON_DATA, buildup_mask1, U32),
    ITEM_FIELD(WEAPON_DATA, attack_max, S16),    ITEM_FIELD(WEAPON_DATA, magic_max, S16),
    ITEM_FIELD(WEAPON_DATA, chain_pos, S16),     ITEM_FIELD(WEAPON_DATA, unk_4A, S16),
};

// A bait's item stays the extraction's while the picker's list of baits is still code (phase 2).
constexpr Field kBaitFields[] = {ITEM_FIELD(EsaInfo, radius, F32)};

constexpr Field kPriceFields[] = {ITEM_FIELD(ITEM_MONEY_ENTRY, buy_price, S16), ITEM_FIELD(ITEM_MONEY_ENTRY, sell_price, S16)};

#undef ITEM_FIELD
#undef ITEM_FIXED

std::size_t Size(Width width) {
    switch (width) {
        case Width::S8:
        case Width::U8:
            return 1;
        case Width::S16:
        case Width::U16:
            return 2;
        default:
            return 4;
    }
}

template <class T>
T Load(const unsigned char *at) {
    T value;
    std::memcpy(&value, at, sizeof value);
    return value;
}

template <class T>
void Store(unsigned char *at, T value) {
    std::memcpy(at, &value, sizeof value);
}

Json Value(const unsigned char *at, Width width) {
    switch (width) {
        case Width::S8:
            return Load<s8>(at);
        case Width::U8:
            return Load<u8>(at);
        case Width::S16:
            return Load<s16>(at);
        case Width::U16:
            return Load<u16>(at);
        case Width::S32:
            return Load<s32>(at);
        case Width::U32:
            return Load<u32>(at);
        case Width::F32:
            return Load<float>(at);
    }
    return nullptr;
}

// Writes a document's value into a field, refusing one the field cannot hold.
void Put(unsigned char *at, Width width, const Json &value, const char *name) {
    if (width == Width::F32) {
        Store(at, native_config::Float(value));
        return;
    }
    if (!value.is_number_integer()) {
        throw std::runtime_error(std::string(name) + " is not an integer");
    }
    const auto range = [&]() -> std::pair<long long, long long> {
        switch (width) {
            case Width::S8:
                return {std::numeric_limits<s8>::min(), std::numeric_limits<s8>::max()};
            case Width::U8:
                return {0, std::numeric_limits<u8>::max()};
            case Width::S16:
                return {std::numeric_limits<s16>::min(), std::numeric_limits<s16>::max()};
            case Width::U16:
                return {0, std::numeric_limits<u16>::max()};
            case Width::S32:
                return {std::numeric_limits<s32>::min(), std::numeric_limits<s32>::max()};
            default:
                return {0, std::numeric_limits<u32>::max()};
        }
    }();
    // A large unsigned number is clamped to just past the range, so it is refused rather than wrapped.
    const long long number = value.is_number_unsigned()
                                 ? static_cast<long long>(std::min<std::uint64_t>(value.get<std::uint64_t>(), range.second + 1ull))
                                 : value.get<long long>();
    if (number < range.first || number > range.second) {
        throw std::runtime_error(std::string(name) + " " + value.dump() + " is outside " + std::to_string(range.first) +
                                 " to " + std::to_string(range.second));
    }
    switch (width) {
        case Width::S8:
            return Store(at, static_cast<s8>(number));
        case Width::U8:
            return Store(at, static_cast<u8>(number));
        case Width::S16:
            return Store(at, static_cast<s16>(number));
        case Width::U16:
            return Store(at, static_cast<u16>(number));
        case Width::S32:
            return Store(at, static_cast<s32>(number));
        default:
            return Store(at, static_cast<u32>(number));
    }
}

// A table: the rows of its document, keyed by item number, and the retail rows they fill.
struct Table {
    const char             *path; // under gameplay/
    const char             *kind;
    const char             *rows; // the document's key for its rows
    std::span<const Field>  fields;
    std::size_t             row_size;
    std::vector<int>        items;
    // A row's bytes, live or as compiled.
    std::function<unsigned char *(int item, bool original)> row;
};

// The tables as compiled, kept before anything is loaded into them.
struct Compiled {
    COM_ITEM_INFO    common[296];
    char            *models[175];
    ITEM_DATA        items[175];
    ATTACH_DATA      attachments[50];
    WEAPON_DATA      weapons[120];
    EsaInfo          bait[13];
    ITEM_MONEY_ENTRY prices[296];
};

Compiled &Original() {
    static Compiled *compiled = [] {
        auto *copy = new Compiled;
        std::memcpy(copy->common, ComItemInfo, sizeof copy->common);
        std::memcpy(copy->models, ITEM_NAME_TBL_NEW, sizeof copy->models);
        std::memcpy(copy->items, ITEM_LIST, sizeof copy->items);
        std::memcpy(copy->attachments, AttachList, sizeof copy->attachments);
        std::memcpy(copy->weapons, WeaponList, sizeof copy->weapons);
        std::memcpy(copy->bait, esa_info, sizeof copy->bait);
        std::memcpy(copy->prices, PriceList, sizeof copy->prices);
        return copy;
    }();
    return *compiled;
}

// The item numbers whose rows lie in a table of `length` rows of this kind. Items 131 and 256 index one
// past the end of theirs; they have no row.
std::vector<int> ItemsOfKind(int kind, int length) {
    std::vector<int> items;
    for (int item = ITEM_ATTACH_START; item < ITEM_ATTACH_START + 296; item++) {
        const COM_ITEM_INFO &info = Original().common[item - ITEM_ATTACH_START];
        if (info.kind == kind && info.index >= 0 && info.index < length) {
            items.push_back(item);
        }
    }
    return items;
}

std::vector<int> ItemRange(int first, int count) {
    std::vector<int> items(count);
    for (int i = 0; i < count; i++) {
        items[i] = first + i;
    }
    return items;
}

unsigned char *Bytes(void *table, std::size_t row_size, int index) {
    return static_cast<unsigned char *>(table) + row_size * index;
}

const std::vector<Table> &Tables() {
    static const std::vector<Table> tables = [] {
        auto index_of = [](int item) { return Original().common[item - ITEM_ATTACH_START].index; };
        std::vector<int> baits;
        for (const EsaInfo &bait : Original().bait) {
            baits.push_back(bait.item_no);
        }
        return std::vector<Table>{
            {"items/item_common.json", "item_common", "records", kCommonFields, sizeof(COM_ITEM_INFO),
             ItemRange(ITEM_ATTACH_START, 296),
             [](int item, bool original) {
                 return Bytes(original ? Original().common : ComItemInfo, sizeof(COM_ITEM_INFO), item - ITEM_ATTACH_START);
             }},
            {"items/items.json", "items", "items", kItemFields, sizeof(ITEM_DATA), ItemsOfKind(ITEMKIND_ITEM, 175),
             [index_of](int item, bool original) {
                 return Bytes(original ? Original().items : ITEM_LIST, sizeof(ITEM_DATA), index_of(item));
             }},
            {"items/attachments.json", "attachments", "attachments", kAttachFields, sizeof(ATTACH_DATA),
             ItemsOfKind(ITEMKIND_ATTACH, 50),
             [index_of](int item, bool original) {
                 return Bytes(original ? Original().attachments : AttachList, sizeof(ATTACH_DATA), index_of(item));
             }},
            {"items/weapons.json", "weapons", "weapons", kWeaponFields, sizeof(WEAPON_DATA), ItemsOfKind(ITEMKIND_WEAPON, 120),
             [index_of](int item, bool original) {
                 return Bytes(original ? Original().weapons : WeaponList, sizeof(WEAPON_DATA), index_of(item));
             }},
            {"items/bait.json", "bait", "bait", kBaitFields, sizeof(EsaInfo), baits,
             [baits](int item, bool original) {
                 const auto index = std::ranges::find(baits, item) - baits.begin();
                 return Bytes(original ? Original().bait : esa_info, sizeof(EsaInfo), static_cast<int>(index));
             }},
            {"items/prices.json", "prices", "prices", kPriceFields, sizeof(ITEM_MONEY_ENTRY), ItemRange(ITEM_ATTACH_START, 296),
             [](int item, bool original) {
                 return Bytes(original ? Original().prices : PriceList, sizeof(ITEM_MONEY_ENTRY), item - ITEM_ATTACH_START);
             }},
        };
    }();
    return tables;
}

bool HasModel(int item) {
    return item - ITEM_ATTACH_START < 175;
}

// Each ITEM_LIST row's quick-slot maximum, which retail has no field for: the items document's
// quick_slot_max, kept here for the registry.
std::array<int, 175> &QuickSlotMaxima() {
    static std::array<int, 175> maxima = [] {
        std::array<int, 175> retail;
        retail.fill(Items::kRetailQuickSlotMax);
        return retail;
    }();
    return maxima;
}

// A model name as BtGetItemNamePath builds it into its 64-byte paths ("dun/item/main_data/<name>.mds").
bool ValidModel(const std::string &name) {
    return name.size() <= 32 && std::ranges::all_of(name, [](char c) {
               return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
           });
}

Json Document(const Table &table) {
    Json rows = Json::array();
    for (int item : table.items) {
        Json                 row = {{"item_no", item}};
        const unsigned char *bytes = table.row(item, true);
        for (const Field &field : table.fields) {
            if (field.count == 1) {
                row[field.name] = Value(bytes + field.offset, field.width);
                continue;
            }
            Json values = Json::array();
            for (int i = 0; i < field.count; i++) {
                values.push_back(Value(bytes + field.offset + i * Size(field.width), field.width));
            }
            row[field.name] = std::move(values);
        }
        if (std::string_view(table.kind) == "item_common" && HasModel(item)) {
            const char *model = Original().models[item - ITEM_ATTACH_START];
            row["model"] = model != nullptr ? Json(model) : Json(nullptr);
        }
        if (std::string_view(table.kind) == "items") {
            row["quick_slot_max"] = Items::kRetailQuickSlotMax;
        }
        rows.push_back(std::move(row));
    }
    return {{"schema_version", native_config::kSchemaVersion}, {"kind", table.kind}, {table.rows, std::move(rows)}};
}

// A table's rows as a document gives them, checked whole: bytes per item in the table's order, for
// item_common the model names, and for items the quick-slot maxima.
struct Parsed {
    std::vector<std::vector<unsigned char> > rows;
    std::vector<std::optional<std::string> > models;
    std::vector<int>                         quick_slot_max;
    std::set<std::string>                    unknown;
};

Parsed Parse(const Table &table, const Json &document) {
    std::map<int, const Json *> by_item;
    for (const Json &row : native_config::Array(document, table.rows)) {
        const int item = native_config::Int(row, "item_no");
        if (!std::ranges::contains(table.items, item)) {
            throw std::runtime_error("item " + std::to_string(item) + " is not one this table holds");
        }
        if (!by_item.emplace(item, &row).second) {
            throw std::runtime_error("item " + std::to_string(item) + " is listed twice");
        }
    }
    const bool common = std::string_view(table.kind) == "item_common";
    const bool items = std::string_view(table.kind) == "items";
    Parsed     parsed;
    for (int item : table.items) {
        const auto found = by_item.find(item);
        if (found == by_item.end()) {
            throw std::runtime_error("item " + std::to_string(item) + " is missing");
        }
        const Json                &row = *found->second;
        const unsigned char       *original = table.row(item, true);
        std::vector<unsigned char> bytes(original, original + table.row_size);
        for (const Field &field : table.fields) {
            const Json &value = native_config::At(row, field.name);
            const auto  where = "item " + std::to_string(item) + " " + field.name;
            if (field.count == 1) {
                Put(bytes.data() + field.offset, field.width, value, where.c_str());
            } else {
                if (!value.is_array() || value.size() != static_cast<std::size_t>(field.count)) {
                    throw std::runtime_error(where + " needs " + std::to_string(field.count) + " values");
                }
                for (int i = 0; i < field.count; i++) {
                    Put(bytes.data() + field.offset + i * Size(field.width), field.width, value[i], where.c_str());
                }
            }
            const std::size_t length = Size(field.width) * field.count;
            if (field.fixed && std::memcmp(bytes.data() + field.offset, original + field.offset, length) != 0) {
                throw std::runtime_error(where + " is fixed by the save's shape and cannot change");
            }
        }
        for (const auto &[key, value] : row.items()) {
            const bool known = key == "item_no" || (common && key == "model") || (items && key == "quick_slot_max") ||
                               std::ranges::any_of(table.fields, [&](const Field &field) { return key == field.name; });
            if (!known) {
                parsed.unknown.insert(key);
            }
        }
        if (common && HasModel(item)) {
            const Json &model = native_config::At(row, "model");
            if (model.is_null()) {
                parsed.models.emplace_back();
            } else if (!model.is_string() || !ValidModel(model.get<std::string>())) {
                throw std::runtime_error("item " + std::to_string(item) +
                                         " model must be up to 32 lower-case letters, digits and underscores");
            } else {
                parsed.models.push_back(model.get<std::string>());
            }
        }
        if (items) {
            // Optional, so a pack written before the field existed keeps retail's maximum.
            int max = Items::kRetailQuickSlotMax;
            if (row.contains("quick_slot_max")) {
                const Json &value = row["quick_slot_max"];
                if (!value.is_number_integer() || value.get<long long>() < 1 ||
                    value.get<long long>() > Items::kQuickSlotMaxLimit) {
                    throw std::runtime_error("item " + std::to_string(item) + " quick_slot_max must be 1 to " +
                                             std::to_string(Items::kQuickSlotMaxLimit));
                }
                max = value.get<int>();
            }
            parsed.quick_slot_max.push_back(max);
        }
        parsed.rows.push_back(std::move(bytes));
    }
    return parsed;
}

// Points the model table at a document's names: retail's own string where the name is unchanged.
void LoadModels(const std::vector<std::optional<std::string> > &models) {
    static std::deque<std::string> names; // the strings a changed name points at, kept for the run
    for (int i = 0; i < 175; i++) {
        char *original = Original().models[i];
        if (!models[i]) {
            ITEM_NAME_TBL_NEW[i] = nullptr;
        } else if (original != nullptr && *models[i] == original) {
            ITEM_NAME_TBL_NEW[i] = original;
        } else {
            ITEM_NAME_TBL_NEW[i] = names.emplace_back(*models[i]).data();
        }
    }
}

// The vanilla items' IDs by number; a number the decomp has not named is vanilla:item_<number>.
std::string VanillaId(int item) {
    static const std::map<int, std::string_view> names = [] {
        struct Entry {
            int              number;
            std::string_view name;
        };
        static constexpr Entry kIds[] = {
#include "item_ids.inc"
        };
        std::map<int, std::string_view> by_number;
        for (const Entry &entry : kIds) {
            by_number.emplace(entry.number, entry.name);
        }
        return by_number;
    }();
    const auto found = names.find(item);
    return "vanilla:" + (found != names.end() ? std::string(found->second) : "item_" + std::to_string(item));
}

Items::Sort SortOf(int sort_key) {
    return sort_key >= ITEM_SORT_NONE && sort_key < ITEM_SORT_COUNT ? static_cast<Items::Sort>(sort_key) : Items::Sort::None;
}

// A vanilla item as the tables hold it now, documents and packs included.
Items::Def VanillaDef(int item) {
    const COM_ITEM_INFO &info = ComItemInfo[item - ITEM_ATTACH_START];
    Items::Def           def;
    def.id = VanillaId(item);
    def.number = item;
    def.icon = info.icon_index;
    def.message = info.msg;
    if (HasModel(item) && ITEM_NAME_TBL_NEW[item - ITEM_ATTACH_START] != nullptr) {
        def.model = ITEM_NAME_TBL_NEW[item - ITEM_ATTACH_START];
    }
    const ITEM_MONEY_ENTRY &price = PriceList[item - ITEM_ATTACH_START];
    def.With(Items::Sellable{.buy = price.buy_price, .sell = price.sell_price});
    switch (info.kind) {
        case ITEMKIND_ITEM: {
            def.kind = Items::Kind::Item;
            if (info.index < 0 || info.index >= 175) {
                break;
            }
            const ITEM_DATA &data = ITEM_LIST[info.index];
            def.sort = SortOf(data.sort_key);
            if (data.kind_flags & ITEMKINDF_CONSUMABLE) {
                def.With(Items::Consumable{.heals_hp = (data.use_flags & ITEMUSE_HEAL_HP) != 0,
                                           .restores_water = (data.use_flags & ITEMUSE_DRINK) != 0,
                                           .grants_status = (data.use_flags & ITEMUSE_STATUS) != 0,
                                           .volume = data.vol,
                                           .volume_range = data.vol_range});
            }
            if (data.kind_flags & ITEMKINDF_THROWABLE) {
                def.With(Items::Throwable{});
            }
            if (data.stack_kind == ITEM_STACK_MULTI || data.stack_kind == ITEM_STACK_SINGLE) {
                def.With(Items::QuickSlot{
                    .stack = data.stack_kind == ITEM_STACK_MULTI ? Items::Stack::Multi : Items::Stack::Single,
                    .max = QuickSlotMaxima()[info.index]});
            }
            if (def.sort == Items::Sort::Fish) {
                def.With(Items::Fish{});
            } else if (def.sort == Items::Sort::KeyItem) {
                def.With(Items::KeyItem{});
            } else if (def.sort == Items::Sort::Quest) {
                def.With(Items::QuestItem{});
            }
            break;
        }
        case ITEMKIND_ATTACH: {
            def.kind = Items::Kind::Attachment;
            if (info.index < 0 || info.index >= 50) {
                break;
            }
            const ATTACH_DATA &data = AttachList[info.index];
            Items::Attachment  attachment{.attack = data.attack, .endurance = data.endurance, .speed = data.speed, .magic = data.magic};
            std::ranges::copy(data.elem, attachment.elements);
            std::ranges::copy(data.vs_monster, attachment.vs_monster);
            def.With(attachment);
            break;
        }
        case ITEMKIND_WEAPON: {
            def.kind = Items::Kind::Weapon;
            if (info.index < 0 || info.index >= 120) {
                break;
            }
            const WEAPON_DATA &data = WeaponList[info.index];
            def.With(Items::Weapon{.owner = data.owner,
                                   .durability = data.durability,
                                   .attack = data.attack,
                                   .endurance = data.endurance,
                                   .speed = data.speed,
                                   .magic = data.magic,
                                   .attack_max = data.attack_max,
                                   .magic_max = data.magic_max,
                                   .holes = data.hole_num});
            break;
        }
    }
    for (const EsaInfo &bait : esa_info) {
        if (bait.item_no == item) {
            def.With(Items::Bait{.radius = bait.radius});
        }
    }
    return def;
}

void RegisterVanilla() {
    Items::Clear();
    for (int item = ITEM_ATTACH_START; item < ITEM_ATTACH_START + 296; item++) {
        Items::Register(VanillaDef(item));
    }
}

} // namespace

std::map<std::string, Json> ItemDocuments() {
    std::map<std::string, Json> documents;
    for (const Table &table : Tables()) {
        documents.emplace(std::string("gameplay/") + table.path, Document(table));
    }
    return documents;
}

void ItemsLoad() {
    for (const Table &table : Tables()) {
        const auto parsed =
            native_config::Read<Parsed>(table.path, table.kind, [&](const Json &document) { return Parse(table, document); });
        if (!parsed) {
            continue;
        }
        for (std::size_t i = 0; i < table.items.size(); i++) {
            std::memcpy(table.row(table.items[i], false), parsed->rows[i].data(), table.row_size);
        }
        if (std::string_view(table.kind) == "item_common") {
            LoadModels(parsed->models);
        }
        if (std::string_view(table.kind) == "items") {
            for (std::size_t i = 0; i < table.items.size(); i++) {
                QuickSlotMaxima()[Original().common[table.items[i] - ITEM_ATTACH_START].index] = parsed->quick_slot_max[i];
            }
        }
        if (!parsed->unknown.empty()) {
            std::string names;
            for (const std::string &name : parsed->unknown) {
                names += (names.empty() ? "" : ", ") + name;
            }
            native_config::Report(std::string("gameplay/") + table.path, "fields this version does not read: " + names);
        }
    }
    RegisterVanilla();
}
