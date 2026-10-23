#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <string>
#include <vector>

#include "btitem.hpp"
#include "gameplay.hpp"
#include "item_host.hpp"
#include "itemdata.hpp"
#include "platform/datapacks.hpp"
#include "tests/platform_fixture.hpp"

namespace fs = std::filesystem;

// ps2/src/itemdata.cpp's tables that its header does not declare.
extern COM_ITEM_INFO ComItemInfo[296];
extern ATTACH_DATA   AttachList[50];
extern WEAPON_DATA   WeaponList[120];

// ps2/src/shop.cpp's prices and ps2/src/fishing.cpp's baits, as port/src/item_host.cpp reaches them.
struct ITEM_MONEY_ENTRY {
    s16 buy_price;
    s16 sell_price;
};

extern ITEM_MONEY_ENTRY PriceList[296];

struct EsaInfo {
    int   item_no;
    float radius;
};

extern EsaInfo esa_info[13] asm("Fishing_esa_info");

namespace {

// Every table the documents fill, as bytes.
std::vector<unsigned char> Tables() {
    std::vector<unsigned char> bytes;
    auto add = [&](const void *table, std::size_t size) {
        const auto *begin = static_cast<const unsigned char *>(table);
        bytes.insert(bytes.end(), begin, begin + size);
    };
    add(ComItemInfo, sizeof(COM_ITEM_INFO) * 296);
    add(ITEM_NAME_TBL_NEW, sizeof(char *) * 175);
    add(ITEM_LIST, sizeof(ITEM_DATA) * 175);
    add(AttachList, sizeof(ATTACH_DATA) * 50);
    add(WeaponList, sizeof(WEAPON_DATA) * 120);
    add(esa_info, sizeof esa_info);
    add(PriceList, sizeof PriceList);
    return bytes;
}

nlohmann::json &Row(nlohmann::json &document, const char *rows, int item) {
    for (auto &row : document[rows]) {
        if (row["item_no"] == item) {
            return row;
        }
    }
    throw std::runtime_error("no such row");
}

// A data folder with a datapacks folder beside it.
class ItemDocumentsTest : public ::testing::Test {
protected:
    fs::path directory = fs::canonical(fs::temp_directory_path()) / std::format("dc_items_{}", dc::test::ProcessId());
    fs::path data = directory / "data";
    std::vector<unsigned char> compiled = Tables();

    void SetUp() override {
        fs::remove_all(directory);
        fs::create_directories(data);
    }

    void TearDown() override {
        fs::remove_all(directory / "datapacks");
        GameplayLoad(data);
        datapacks::Clear();
        fs::remove_all(directory);
    }

    // The extraction's document at path, to change and put in a pack.
    static nlohmann::json Document(const std::string &path) { return ItemDocuments().at("gameplay/items/" + path); }

    void Pack(const std::string &pack, const std::string &path, const nlohmann::json &document) {
        const fs::path file = directory / "datapacks" / pack / "gameplay/items" / path;
        fs::create_directories(file.parent_path());
        std::ofstream(file) << document.dump();
    }

    void Order(const std::string &packs) {
        std::ofstream(directory / "datapacks/datapacks.json") << R"({"schema_version":1,"load_order":[)" + packs + "]}";
        GameplayLoad(data);
    }
};

} // namespace

TEST_F(ItemDocumentsTest, TheExtractionLoadsBackByteForByte) {
    GameplayLoad(data);
    EXPECT_EQ(Tables(), compiled);
    const auto documents = ItemDocuments();
    EXPECT_EQ(documents.size(), 6u);
    EXPECT_EQ(documents.at("gameplay/items/items.json")["items"].size(), 124u);
    EXPECT_EQ(documents.at("gameplay/items/attachments.json")["attachments"].size(), 50u);
    EXPECT_EQ(documents.at("gameplay/items/weapons.json")["weapons"].size(), 120u);
    EXPECT_EQ(documents.at("gameplay/items/bait.json")["bait"].size(), 13u);
    EXPECT_EQ(documents.at("gameplay/items/item_common.json")["records"].size(), 296u);
}

TEST_F(ItemDocumentsTest, APacksChangeReachesTheTableAndLeavesWithIt) {
    auto prices = Document("prices.json");
    Row(prices, "prices", ITEM_PRICKLY)["buy_price"] = 1234;
    auto bait = Document("bait.json");
    Row(bait, "bait", ITEM_PRICKLY)["radius"] = 37.5;
    auto common = Document("item_common.json");
    Row(common, "records", ITEM_PRICKLY)["model"] = "atfire";
    Pack("lures", "prices.json", prices);
    Pack("lures", "bait.json", bait);
    Pack("lures", "item_common.json", common);
    Order(R"("lures")");
    EXPECT_EQ(PriceList[ITEM_PRICKLY - ITEM_ATTACH_START].buy_price, 1234);
    EXPECT_STREQ(ITEM_NAME_TBL_NEW[ITEM_PRICKLY - ITEM_ATTACH_START], "atfire");
    bool found = false;
    for (const EsaInfo &entry : esa_info) {
        if (entry.item_no == ITEM_PRICKLY) {
            EXPECT_EQ(entry.radius, 37.5f);
            found = true;
        }
    }
    EXPECT_TRUE(found);

    Order("");
    EXPECT_EQ(Tables(), compiled);
}

TEST_F(ItemDocumentsTest, ADocumentRetailCannotReadIsRefusedWhole) {
    auto weapons = Document("weapons.json");
    Row(weapons, "weapons", ITEM_WEAPON_START)["owner"] = 3; // decides whose weapon list holds a copy
    Pack("owner", "weapons.json", weapons);
    auto common = Document("item_common.json");
    Row(common, "records", ITEM_PRICKLY)["index"] = 52;
    Pack("index", "item_common.json", common);
    auto prices = Document("prices.json");
    Row(prices, "prices", ITEM_PRICKLY)["sell_price"] = 1;
    Row(prices, "prices", ITEM_PRICKLY + 1)["buy_price"] = 40000; // past an s16
    Pack("range", "prices.json", prices);
    auto items = Document("items.json");
    items["items"].erase(items["items"].begin());
    Pack("missing", "items.json", items);
    auto attachments = Document("attachments.json");
    attachments["attachments"].push_back({{"item_no", ITEM_WEAPON_START}});
    Pack("extra", "attachments.json", attachments);
    auto bait = Document("bait.json");
    bait["schema_version"] = 3;
    Row(bait, "bait", ITEM_PRICKLY)["radius"] = 1.0;
    Pack("newer", "bait.json", bait);
    auto model = Document("item_common.json");
    Row(model, "records", ITEM_PRICKLY)["model"] = "../escape";
    Pack("model", "item_common.json", model);
    Order(R"("owner","index","range","missing","extra","newer","model")");
    EXPECT_EQ(Tables(), compiled);
}

TEST_F(ItemDocumentsTest, AnUnknownFieldIsReportedButNotRefused) {
    auto items = Document("items.json");
    Row(items, "items", ITEM_PRICKLY)["vol"] = 7;
    Row(items, "items", ITEM_PRICKLY)["sparkle"] = true;
    Pack("newer field", "items.json", items);
    Order(R"("newer field")");
    EXPECT_EQ(GetItemData(ITEM_PRICKLY)->vol, 7);
}

TEST_F(ItemDocumentsTest, TheExportLoadsBackToTheSameTables) {
    ASSERT_TRUE(GameplayExport(directory / "datapacks/exported"));
    EXPECT_TRUE(fs::is_regular_file(directory / "datapacks/exported/gameplay/items/weapons.json"));
    Order(R"("exported")");
    EXPECT_EQ(Tables(), compiled);
}
