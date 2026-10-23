#include <gtest/gtest.h>

#include <filesystem>
#include <format>
#include <fstream>
#include <string>
#include <vector>

#include "platform/datapacks.hpp"
#include "platform/memory_mount.hpp"
#include "platform/native_config.hpp"
#include "tests/platform_fixture.hpp"

namespace fs = std::filesystem;

namespace {

std::vector<std::uint8_t> Bytes(std::string_view text) {
    return {text.begin(), text.end()};
}

std::string Text(const vfs::Ref &file) {
    std::vector<std::uint8_t> bytes;
    EXPECT_TRUE(vfs::Read(file, bytes));
    return {bytes.begin(), bytes.end()};
}

// A data folder beside a datapacks folder, with gameplay documents served from memory as the base.
class DatapacksTest : public ::testing::Test {
protected:
    fs::path directory = fs::canonical(fs::temp_directory_path()) / std::format("dc_datapacks_{}", dc::test::ProcessId());
    fs::path data = directory / "data";
    std::shared_ptr<const vfs::Mount> base = MemoryMount(
        {
            {"gameplay/items/items.json", Bytes("base items")},
            {"gameplay/items/custom/vanilla.one.json", Bytes("base one")},
            {"Chara/Hero.dat", Bytes("retail")},
        },
        "base/");

    void SetUp() override {
        fs::remove_all(directory);
        fs::create_directories(data);
    }

    void TearDown() override {
        datapacks::Clear();
        fs::remove_all(directory);
    }

    void Write(const std::string &pack, const std::string &relative, std::string_view text) {
        const fs::path path = directory / "datapacks" / fs::path(std::u8string(pack.begin(), pack.end())) / relative;
        fs::create_directories(path.parent_path());
        std::ofstream(path, std::ios::binary) << text;
    }

    void Config(std::string_view text) {
        fs::create_directories(directory / "datapacks");
        std::ofstream(directory / "datapacks/datapacks.json") << text;
        datapacks::Load(base, data);
    }
};

} // namespace

TEST_F(DatapacksTest, WithoutAConfigurationTheBaseServes) {
    datapacks::Load(base, data);
    const auto found = datapacks::Asset("gameplay/items/items.json");
    ASSERT_TRUE(found);
    EXPECT_EQ(Text(*found), "base items");
    EXPECT_FALSE(datapacks::IsReplacement(*found));
    EXPECT_FALSE(datapacks::Resolve("gameplay/items/items.json"));
}

TEST_F(DatapacksTest, LaterPacksWinAndReorderingChangesTheWinner) {
    Write("First pack", "gameplay/items/items.json", "first");
    Write("日本語 pack", "gameplay/items/items.json", "second");
    Config(R"({"schema_version":1,"load_order":["First pack","日本語 pack"]})");
    EXPECT_EQ(Text(*datapacks::Asset("gameplay/items/items.json")), "second");
    auto candidates = datapacks::Candidates("gameplay/items/items.json");
    ASSERT_EQ(candidates.size(), 3u);
    EXPECT_EQ(Text(candidates[1]), "first");
    EXPECT_EQ(Text(candidates[2]), "base items");
    EXPECT_TRUE(datapacks::IsReplacement(candidates[0]));

    Config(R"({"schema_version":1,"load_order":["日本語 pack","First pack"]})");
    EXPECT_EQ(Text(*datapacks::Asset("gameplay/items/items.json")), "first");
}

TEST_F(DatapacksTest, AnInvalidConfigurationDisablesEveryPack) {
    Write("valid", "Chara/Hero.dat", "replacement");
    for (const char *config : {
             R"({"schema_version":2,"load_order":["valid"]})",
             R"({"schema_version":1.0,"load_order":["valid"]})",
             R"({"schema_version":1,"load_order":["valid","../escape"]})",
             R"({"schema_version":1,"load_order":["valid","a/b"]})",
             R"({"schema_version":1,"load_order":["valid","C:escape"]})",
             R"({"schema_version":1,"load_order":["valid",""]})",
             R"({"schema_version":1,"load_order":["valid",false]})",
             "invalid JSON"}) {
        Config(R"({"schema_version":1,"load_order":["valid"]})");
        EXPECT_EQ(Text(*datapacks::Asset("chara/hero.dat")), "replacement");
        Config(config);
        EXPECT_EQ(Text(*datapacks::Asset("chara/hero.dat")), "retail") << config;
    }
}

TEST_F(DatapacksTest, OverridesIgnoreCaseAndRejectTraversal) {
    Write("files", "Chara/Hero.dat", "replacement");
    Config(R"({"schema_version":1,"load_order":["files"]})");
    const auto resolved = datapacks::Resolve("chara/hero.DAT");
    ASSERT_TRUE(resolved);
    EXPECT_EQ(Text(*resolved), "replacement");
    EXPECT_FALSE(datapacks::Resolve("../Chara/Hero.dat"));
    EXPECT_FALSE(datapacks::Resolve("/Chara/Hero.dat"));
    EXPECT_FALSE(datapacks::Resolve("C:Chara/Hero.dat"));
    datapacks::Clear();
    EXPECT_FALSE(datapacks::Resolve("chara/hero.dat"));
}

TEST_F(DatapacksTest, UnlistedMissingAndZipPacksDoNotOverride) {
    Write("unlisted", "Chara/Hero.dat", "unlisted");
    fs::create_directories(directory / "datapacks");
    std::ofstream(directory / "datapacks/zipped.zip") << "PK";
    Config(R"({"schema_version":1,"load_order":["missing","zipped"]})");
    EXPECT_EQ(Text(*datapacks::Asset("chara/hero.dat")), "retail");
    Config(R"({"schema_version":1,"load_order":[]})");
    EXPECT_EQ(Text(*datapacks::Asset("chara/hero.dat")), "retail");
}

TEST_F(DatapacksTest, GameplayDocumentsAcceptAnUnprefixedPathInAPack) {
    Write("prefixed", "gameplay/items/items.json", "prefixed");
    Write("unprefixed", "items/items.json", "unprefixed");
    Config(R"({"schema_version":1,"load_order":["prefixed","unprefixed"]})");
    const auto candidates = datapacks::GameplayCandidates("items/items.json");
    ASSERT_EQ(candidates.size(), 3u);
    EXPECT_EQ(Text(candidates[0]), "unprefixed");
    EXPECT_EQ(Text(candidates[1]), "prefixed");
    EXPECT_EQ(Text(candidates[2]), "base items");
}

TEST_F(DatapacksTest, DirectoryMembersCombineTheBaseAndEveryPack) {
    Write("one", "gameplay/items/custom/lures.golden.json", "lure");
    Write("two", "gameplay/items/custom/VANILLA.ONE.json", "replaced");
    Config(R"({"schema_version":1,"load_order":["one","two"]})");
    const auto members = datapacks::DirectoryMembers("gameplay/items/custom");
    ASSERT_EQ(members.size(), 2u);
    EXPECT_EQ(members[0].generic_string(), "gameplay/items/custom/lures.golden.json");
    EXPECT_EQ(members[1].generic_string(), "gameplay/items/custom/VANILLA.ONE.json");
}

TEST_F(DatapacksTest, ReadFileBytesReadsWholeFilesWithinTheLimit) {
    const auto path = directory / "bytes.bin";
    std::ofstream(path, std::ios::binary) << std::string("abc\0def", 7);
    std::vector<std::uint8_t> bytes{1, 2, 3};
    ASSERT_TRUE(datapacks::ReadFileBytes(path, bytes));
    EXPECT_EQ(bytes, (std::vector<std::uint8_t>{'a', 'b', 'c', 0, 'd', 'e', 'f'}));
    EXPECT_TRUE(datapacks::ReadFileBytes(path, bytes, 7));
    EXPECT_FALSE(datapacks::ReadFileBytes(path, bytes, 6));
    EXPECT_TRUE(bytes.empty());
    EXPECT_FALSE(datapacks::ReadFileBytes(directory / "missing.bin", bytes));
}

TEST_F(DatapacksTest, MemoryMountFindsAndListsIgnoringCase) {
    EXPECT_TRUE(vfs::Find(base, "GAMEPLAY/Items/ITEMS.json"));
    EXPECT_FALSE(vfs::Find(base, "gameplay/items"));
    const auto root = base->List("");
    ASSERT_EQ(root.size(), 2u);
    EXPECT_EQ(root[0].name, "Chara");
    EXPECT_EQ(root[1].name, "gameplay");
    EXPECT_TRUE(root[0].directory && root[1].directory);
    const auto items = base->List("gameplay/items");
    ASSERT_EQ(items.size(), 2u);
    EXPECT_EQ(items[0].name, "custom");
    EXPECT_TRUE(items[0].directory);
    EXPECT_EQ(items[1].name, "items.json");
    EXPECT_FALSE(items[1].directory);
}

// A document is taken from the highest-priority candidate whose schema, kind and contents are valid.
TEST_F(DatapacksTest, AGameplayDocumentFallsBackPastBadCandidates) {
    base = MemoryMount({{"gameplay/items/prices.json", Bytes(R"({"schema_version":2,"kind":"prices","value":1})")}},
                       "base/");
    Write("newer", "gameplay/items/prices.json", R"({"schema_version":3,"kind":"prices","value":5})");
    Write("other kind", "gameplay/items/prices.json", R"({"schema_version":2,"kind":"items","value":4})");
    Write("bad field", "gameplay/items/prices.json", R"({"schema_version":2,"kind":"prices","value":"3"})");
    Write("not json", "gameplay/items/prices.json", "{");
    const auto read = [] {
        return native_config::Read<int>("items/prices.json", "prices", [](const native_config::Json &document) {
            return native_config::Int(document, "value", 0, 9);
        });
    };
    Config(R"({"schema_version":1,"load_order":["newer","other kind","bad field","not json"]})");
    EXPECT_EQ(read(), 1);
    Write("good", "gameplay/items/prices.json", R"({"schema_version":2,"kind":"prices","value":2})");
    Config(R"({"schema_version":1,"load_order":["good","newer","other kind","bad field"]})");
    EXPECT_EQ(read(), 2);
    Write("out of range", "gameplay/items/prices.json", R"({"schema_version":2,"kind":"prices","value":10})");
    Config(R"({"schema_version":1,"load_order":["good","out of range"]})");
    EXPECT_EQ(read(), 2);
    EXPECT_FALSE((native_config::Read<int>("items/missing.json", "prices",
                                           [](const native_config::Json &) { return 0; })));
}
