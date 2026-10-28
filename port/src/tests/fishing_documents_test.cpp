// nlohmann/json before the game headers, whose one-letter macros it cannot stand.
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <map>
#include <random>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include "editloop3.hpp"
#include "fish.hpp"
#include "fishing/framework.hpp"
#include "fishing/host.hpp"
#include "item/framework.hpp"
#include "item/host.hpp"
#include "platform/datapacks.hpp"
#include "shop.hpp"
#include "tests/platform_fixture.hpp"

namespace dc::test {

using namespace dc::game;
using namespace dc::platform;

namespace {

namespace fs = std::filesystem;

// What the documents fill, as bytes: each kind's row, model and name, and the prizes.
std::vector<unsigned char> Tables() {
    std::vector<unsigned char> bytes;
    for (int kind = -1; kind <= 18; kind++) {
        const FishInfo *info = fishing::Info(kind);
        const auto     *begin = reinterpret_cast<const unsigned char *>(info);
        if (info != nullptr) {
            bytes.insert(bytes.end(), begin, begin + sizeof(FishInfo));
        }
        const char *model = GetFishFileName(kind);
        if (model != nullptr) {
            bytes.insert(bytes.end(), model, model + std::strlen(model) + 1);
        }
        const int name = GetFishMsgNo(kind);
        bytes.insert(bytes.end(), reinterpret_cast<const unsigned char *>(&name), reinterpret_cast<const unsigned char *>(&name + 1));
        bytes.push_back(fishing::CountsAsMardanGarayan(kind) ? 1 : 0);
    }
    const auto *prizes = reinterpret_cast<const unsigned char *>(exitemlst);
    bytes.insert(bytes.end(), prizes, prizes + sizeof(exitemlst));
    return bytes;
}

nlohmann::json &Row(nlohmann::json &document, const char *rows, const char *key, int value) {
    for (auto &row : document[rows]) {
        if (row[key] == value) {
            return row;
        }
    }
    throw std::runtime_error("no such row");
}

// The share of each kind among the fish a place spawns at a time of day, over many seeded draws.
std::map<int, double> Shares(int spot, fishing::TimeOfDay time) {
    const fishing::Place *place = fishing::PlaceAtSpot(spot);
    std::mt19937          engine(1234);
    std::uniform_real_distribution<double> uniform(0.0, 1.0);
    const fishing::SpawnContext            context{.time = time, .random = [&] { return uniform(engine); }};
    std::map<int, double>                  shares;
    int                                    total = 0;
    for (int run = 0; run < 50000; run++) {
        for (const fishing::Species *species : place->spawn->Spawn(*place, context)) {
            shares[species->kind]++;
            total++;
        }
    }
    for (auto &[kind, share] : shares) {
        share /= total;
    }
    return shares;
}

// A data folder with a datapacks folder beside it, and the exported documents to start packs from.
class FishingDocuments : public ::testing::Test {
protected:
    fs::path directory = fs::canonical(fs::temp_directory_path()) / std::format("dc_fishing_{}", ProcessId());
    fs::path data = directory / "data";

    void SetUp() override {
        fs::remove_all(directory);
        fs::create_directories(data);
        ASSERT_TRUE(fishing::Export(directory / "exported"));
        datapacks::Clear();
        item::Load();
        fishing::Load();
    }

    void TearDown() override {
        datapacks::Clear();
        item::Load();
        fishing::Load();
        fs::remove_all(directory);
    }

    nlohmann::json Document(const std::string &name) const {
        return nlohmann::json::parse(std::ifstream(directory / "exported/gameplay/fishing" / name));
    }

    void Pack(const std::string &pack, const std::string &name, const nlohmann::json &document) const {
        const fs::path file = directory / "datapacks" / pack / "gameplay/fishing" / name;
        fs::create_directories(file.parent_path());
        std::ofstream(file) << document.dump();
    }

    void Order(const std::string &packs) const {
        fs::create_directories(directory / "datapacks");
        std::ofstream(directory / "datapacks/datapacks.json") << R"({"schema_version":1,"load_order":[)" + packs + "]}";
        datapacks::Load(data);
        item::Load();
        fishing::Load();
    }
};

TEST_F(FishingDocuments, TheCompiledTablesAreRetails) {
    const FishInfo *mardan = fishing::Info(5);
    ASSERT_NE(mardan, nullptr);
    EXPECT_EQ(mardan->min_fp, 200);
    EXPECT_EQ(mardan->max_fp, 400);
    EXPECT_EQ(mardan->food_affinity[6], 1.0f); // the Poisonous Apple
    EXPECT_EQ(fishing::Info(3)->model_size, 35.4f);
    EXPECT_EQ(fishing::Info(18), nullptr);
    EXPECT_STREQ(GetFishFileName(5), "chara/f06a.chr");
    EXPECT_EQ(GetFishFileName(18), nullptr);
    EXPECT_EQ(GetFishMsgNo(5), FishMsg[5] + 30);
    EXPECT_EQ(GetFishMsgNo(-1), 0);
    EXPECT_TRUE(fishing::CountsAsMardanGarayan(5) && fishing::CountsAsMardanGarayan(17));
    EXPECT_FALSE(fishing::CountsAsMardanGarayan(4));
    EXPECT_EQ(exitemlst[0].item_no, ITEM_ANTIDOTE_DRINK);
    EXPECT_EQ(exitemlst[32].price, 2500);
    EXPECT_EQ(fishing::SpawnAt(3, ED_TIME_DAY).kinds.size(), 5u);
    EXPECT_FALSE(fishing::SpawnAt(3, ED_TIME_DAY).draw_under_water);
    EXPECT_EQ(fishing::SpawnAt(9, ED_TIME_DAY).kinds, std::vector<int>(5, -1));
}

TEST_F(FishingDocuments, TheTrackedDocumentsHoldTheCompiledTables) {
    // port/gameplay/fishing, which the build seals into its gameplay seed and dcdata writes into the data.
    const fs::path        tracked = fs::path(__FILE__).parent_path().parent_path().parent_path() / "gameplay/fishing";
    std::set<std::string> files;
    for (const fs::directory_entry &entry : fs::directory_iterator(tracked)) {
        files.insert(entry.path().filename().string());
    }
    EXPECT_EQ(files, (std::set<std::string>{"fish.json", "places.json", "prizes.json"}));
    for (const std::string &name : files) {
        EXPECT_EQ(nlohmann::json::parse(std::ifstream(tracked / name)), Document(name)) << name;
    }
}

TEST_F(FishingDocuments, TheExportLoadsBackAsCompiled) {
    const std::vector<unsigned char> compiled = Tables();
    fs::create_directories(directory / "datapacks");
    fs::rename(directory / "exported", directory / "datapacks/exported");
    Order(R"("exported")");
    EXPECT_EQ(Tables(), compiled);
}

TEST_F(FishingDocuments, APacksChangeReachesTheGameAndLeavesWithIt) {
    const std::vector<unsigned char> compiled = Tables();
    auto                             fish = Document("fish.json");
    Row(fish, "fish", "fish_kind", 4)["max_fp"] = 90;
    Row(fish, "fish", "fish_kind", 4)["mardan_garayan"] = true;
    Row(fish, "fish", "fish_kind", 4)["food_affinity"] = nlohmann::json::array({
        {{"bait", "vanilla:poisonous_apple"}, {"affinity", 0.75}}
    });
    auto places = Document("places.json");
    Row(places, "places", "spot", 0)["fish_count"] = 6;
    Row(places, "places", "spot", 0)["spawn"]["fish"] = nlohmann::json::array({
        {{"fish", "vanilla:fish_8"}, {"weight", 1}}
    });
    auto prizes = Document("prizes.json");
    prizes["prizes"][0] = {
        {"item",  "vanilla:bread"},
        {"price", 7              }
    };
    Pack("fishy", "fish.json", fish);
    Pack("fishy", "places.json", places);
    Pack("fishy", "prizes.json", prizes);
    Order(R"("fishy")");

    EXPECT_EQ(fishing::Info(4)->max_fp, 90);
    EXPECT_EQ(fishing::Info(4)->food_affinity[6], 0.75f);
    EXPECT_EQ(fishing::Info(4)->food_affinity[1], 0.0f);
    EXPECT_TRUE(fishing::CountsAsMardanGarayan(4));
    EXPECT_EQ(fishing::SpawnAt(0, ED_TIME_DAY).kinds, std::vector<int>(6, 8));
    EXPECT_EQ(exitemlst[0].item_no, ITEM_BREAD);
    EXPECT_EQ(exitemlst[0].price, 7);

    Order("");
    EXPECT_EQ(Tables(), compiled);
}

TEST_F(FishingDocuments, ADocumentThatCannotBeUsedFallsBackWhole) {
    const std::vector<unsigned char> compiled = Tables();
    auto                             fish = Document("fish.json");
    Row(fish, "fish", "fish_kind", 4)["max_fp"] = 90;
    fish["fish"].erase(0); // every kind must be there
    auto places = Document("places.json");
    Row(places, "places", "spot", 1)["spawn"]["stage"] = "nonesuch";
    auto prizes = Document("prizes.json");
    prizes["prizes"][0]["item"] = "vanilla:no_such_item";
    auto liking = Document("fish.json");
    Row(liking, "fish", "fish_kind", 4)["food_affinity"][0]["bait"] = "vanilla:bread"; // not a bait
    Pack("bad", "fish.json", fish);
    Pack("bad", "places.json", places);
    Pack("bad", "prizes.json", prizes);
    Pack("worse", "fish.json", liking);
    Order(R"("bad", "worse")");
    EXPECT_EQ(Tables(), compiled);
}

TEST_F(FishingDocuments, EachPlaceSpawnsWithRetailsOdds) {
    using enum fishing::TimeOfDay;
    const auto expect = [](int spot, fishing::TimeOfDay time, const std::map<int, double> &odds) {
        const std::map<int, double> shares = Shares(spot, time);
        ASSERT_EQ(shares.size(), odds.size()) << "spot " << spot;
        for (const auto &[kind, chance] : odds) {
            EXPECT_NEAR(shares.at(kind), chance, 0.005) << "spot " << spot << " kind " << kind;
        }
    };
    expect(0, Day, {{1, 0.25}, {2, 0.25}, {6, 0.25}, {7, 0.25}});
    expect(1, Day, {{1, 0.35}, {4, 0.35}, {9, 0.10}, {10, 0.20}});
    expect(3, Night, {{0, 0.2}, {3, 0.2}, {11, 0.2}, {12, 0.2}, {13, 0.2}});
    // The rare fish come one time in the time of day's interval, kind 17 one time in five of those.
    for (const auto &[time, interval] : std::map<fishing::TimeOfDay, double>{{Day, 50}, {Evening, 20}, {Night, 35}, {Morning, 25}}) {
        const double rare = 1.0 / interval;
        expect(2, time, {{17, rare / 5}, {5, rare * 4 / 5}, {2, (1 - rare) / 3}, {4, (1 - rare) / 3}, {6, (1 - rare) / 3}});
        expect(4, time, {{17, rare / 5}, {5, rare * 4 / 5}, {14, (1 - rare) * 0.4}, {15, (1 - rare) * 0.3}, {16, (1 - rare) * 0.3}});
    }
}

} // namespace

} // namespace dc::test
