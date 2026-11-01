// nlohmann/json before the game headers, whose one-letter macros it cannot stand.
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <format>
#include <fstream>
#include <string>

#include "battlemenu.hpp"
#include "dun/gameloop.hpp"
#include "hitvalue.hpp"
#include "item/host.hpp"
#include "itembombeffect.hpp"
#include "itemdata.hpp"
#include "menu_inventory.hpp"
#include "menu_save.hpp"
#include "platform/datapacks.hpp"
#include "qol/features.hpp"
#include "savedata.hpp"
#include "tests/platform_fixture.hpp"
#include "userstatus.hpp"

namespace dc::test {

using namespace dc::game;
using namespace dc::platform;

namespace {

namespace fs = std::filesystem;

// The packs the game ships, as the repository holds them.
const fs::path kShipped = fs::path(__FILE__).parent_path().parent_path().parent_path() / "datapacks";

// A data folder with a datapacks folder beside it.
class QolFeatures : public ::testing::Test {
protected:
    fs::path directory = fs::canonical(fs::temp_directory_path()) / std::format("dc_qol_{}", ProcessId());
    fs::path data = directory / "data";

    void SetUp() override {
        fs::remove_all(directory);
        fs::create_directories(data);
    }

    void TearDown() override {
        datapacks::Clear();
        item::Load();
        qol::Load();
        item::Clear();
        fs::remove_all(directory);
    }

    void Features(const std::string &pack, const nlohmann::json &document) const {
        const fs::path file = directory / "datapacks" / pack / "gameplay/qol/features.json";
        fs::create_directories(file.parent_path());
        std::ofstream(file) << document.dump();
    }

    void Order(const std::string &packs) const {
        fs::create_directories(directory / "datapacks");
        std::ofstream(directory / "datapacks/datapacks.json") << R"({"schema_version":1,"load_order":[)" + packs + "]}";
        datapacks::Load(data, kShipped);
        item::Load();
        qol::Load();
    }
};

TEST_F(QolFeatures, WithoutThePackEveryChangeIsOffAsRetailPlays) {
    Order("");
    EXPECT_FALSE(qol::Get().party_pool_restoration);
    EXPECT_FALSE(qol::Get().inventory_auto_repair);
    EXPECT_EQ(GetItemData(ITEM_REPAIR_POWDER)->stack_kind, ITEM_STACK_NEVER);
}

TEST_F(QolFeatures, TheShippedPackTurnsEveryChangeOn) {
    Order(R"("qol")");
    EXPECT_TRUE(qol::Get().party_pool_restoration);
    EXPECT_TRUE(qol::Get().inventory_auto_repair);
    EXPECT_EQ(GetItemData(ITEM_REPAIR_POWDER)->stack_kind, ITEM_STACK_MULTI);
}

TEST_F(QolFeatures, APlayersCopyTurnsOneOff) {
    Features("qol", {
                        {"schema_version",         2             },
                        {"kind",                   "qol_features"},
                        {"party_pool_restoration", false         },
                        {"inventory_auto_repair",  true          },
    });
    Order(R"("qol")");
    EXPECT_FALSE(qol::Get().party_pool_restoration);
    EXPECT_TRUE(qol::Get().inventory_auto_repair);
}

TEST_F(QolFeatures, ADocumentThatCannotBeReadLeavesEveryChangeOff) {
    Features("broken", {
                           {"schema_version",         2             },
                           {"kind",                   "qol_features"},
                           {"party_pool_restoration", "yes"         },
    });
    Order(R"("broken")");
    EXPECT_FALSE(qol::Get().party_pool_restoration);
    EXPECT_FALSE(qol::Get().inventory_auto_repair);
}

// A party in a dungeon: Toan with his equipped weapon in slot 0.
class QolParty : public QolFeatures {
protected:
    alignas(64) static inline CSaveData save;
    WEAPON_HAVE  now_weapon{};
    CSaveData   *saved_data = nullptr;
    CUserStatus *saved_status = nullptr;
    WEAPON_HAVE *saved_weapon = nullptr;

    void SetUp() override {
        QolFeatures::SetUp();
        saved_data = SaveData;
        saved_status = UserStatus;
        saved_weapon = NowWeaponHave;
        save.Initialize();
        SaveData = &save;
        UserStatus = save.GetDngStatus();
        NowWeaponHave = &now_weapon;
        UserStatus->party_size = 1;
        UserStatus->cur_chara = 0;
        UserStatus->equipped_weapon_slot[0] = 0;
    }

    void TearDown() override {
        SaveData = saved_data;
        UserStatus = saved_status;
        NowWeaponHave = saved_weapon;
        QolFeatures::TearDown();
    }

    WEAPON_HAVE &Weapon() const { return UserStatus->chara_weapons[0][0]; }
};

TEST_F(QolParty, RepairPowderInAQuickSlotMendsAWornWeapon) {
    WepDataListToHaveCopy(ITEM_WEAPON_DAGGER, &Weapon());
    Weapon().durability_f = 5.0f;
    UserStatus->item_pack.quick_item_slot[0] = ITEM_REPAIR_POWDER;
    EXPECT_EQ(checkItemUsed(0), 1);
    RepairEquippedWeapon(UserStatus);
    EXPECT_EQ(Weapon().durability_f, static_cast<float>(Weapon().durability));
    // Whole, it would do nothing, so it is not spent.
    EXPECT_EQ(checkItemUsed(0), 0);
}

TEST_F(QolParty, RepairPowderBringsTheBrokenStartingWeaponBack) {
    const int broken = GetDefaultWeaponNo(0);
    WepDataListToHaveCopy(broken, &Weapon());
    Weapon().durability_f = 0.0f;
    RepairEquippedWeapon(UserStatus);
    EXPECT_EQ(Weapon().item_no, broken + 1);
}

TEST_F(QolParty, AnAutoRepairPowderInTheBagSavesABreakingWeaponOnlyWithThePack) {
    auto wear_out = [this] {
        WepDataListToHaveCopy(ITEM_WEAPON_DAGGER, &Weapon());
        Weapon().durability_f = 0.01f;
        now_weapon = Weapon();
        UserStatus->item_pack.item[0] = ITEM_AUTO_REPAIR_POWDER;
        UserStatus->item_pack.num = 1;
        BattleSubWeaponDmg(10.0f, 0);
    };

    Order("");
    wear_out();
    EXPECT_EQ(UserStatus->item_pack.item[0], ITEM_AUTO_REPAIR_POWDER);

    Order(R"("qol")");
    wear_out();
    EXPECT_EQ(UserStatus->item_pack.item[0], -1);
    EXPECT_EQ(Weapon().item_no, ITEM_WEAPON_DAGGER);
    EXPECT_EQ(Weapon().durability_f, static_cast<float>(Weapon().durability));
}

TEST_F(QolFeatures, TheExportIsEveryChangeOff) {
    ASSERT_TRUE(qol::Export(directory / "exported"));
    const auto document = nlohmann::json::parse(std::ifstream(directory / "exported/gameplay/qol/features.json"));
    EXPECT_EQ(document["kind"], "qol_features");
    EXPECT_EQ(document["party_pool_restoration"], false);
    EXPECT_EQ(document["inventory_auto_repair"], false);
}

} // namespace

} // namespace dc::test
