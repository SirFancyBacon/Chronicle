#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <random>
#include <string>
#include <vector>

#include "data/data_read.hpp"
#include "data_fixture.hpp"
#include "dataread.hpp"
#include "ktx2.hpp"
#include "platform/paths.hpp"
#include "render/texture_registry.hpp"

namespace dc::test {

using namespace dc::game;
using namespace dc::data;
using namespace dc::render;

namespace {

std::vector<std::uint8_t> Noise(std::size_t bytes, unsigned seed) {
    std::mt19937              random(seed);
    std::vector<std::uint8_t> out(bytes);
    for (std::uint8_t &byte : out) {
        byte = static_cast<std::uint8_t>(random());
    }
    return out;
}

} // namespace

TEST(Ktx2, GivesBackEveryTexelExactly) {
    const std::vector<std::uint8_t> rgba = Noise(37 * 19 * 4, 7);
    const std::vector<std::uint8_t> file = dcdata::ktx2::Encode(37, 19, rgba);
    ASSERT_FALSE(file.empty());
    EXPECT_TRUE(dcdata::ktx2::IsKtx2(file));
    std::uint32_t             width = 0;
    std::uint32_t             height = 0;
    std::vector<std::uint8_t> back;
    std::string               why;
    ASSERT_TRUE(dcdata::ktx2::Decode(file, width, height, back, &why)) << why;
    EXPECT_EQ(width, 37u);
    EXPECT_EQ(height, 19u);
    EXPECT_EQ(back, rgba);
}

TEST(Ktx2, IsTheKhronosLayoutForSrgbRgba8WithZstd) {
    const std::vector<std::uint8_t> rgba(4 * 4 * 4, 0x40);
    const std::vector<std::uint8_t> file = dcdata::ktx2::Encode(4, 4, rgba);
    ASSERT_GE(file.size(), 104u);
    const auto word = [&](std::size_t at) { return file[at] | (file[at + 1] << 8) | (file[at + 2] << 16) | (static_cast<std::uint32_t>(file[at + 3]) << 24); };
    EXPECT_EQ(word(12), 43u); // VK_FORMAT_R8G8B8A8_SRGB
    EXPECT_EQ(word(16), 1u);  // type size
    EXPECT_EQ(word(40), 1u);  // one level
    EXPECT_EQ(word(44), 2u);  // Zstandard
    EXPECT_EQ(word(48), 104u); // the data format descriptor follows the level index
    EXPECT_EQ(word(52), 92u);
    EXPECT_EQ(word(104), 92u); // and begins with its own size
    // Solid colour packs far smaller than its texels.
    EXPECT_LT(file.size(), 4u * 4u * 4u + 104u + 92u);
}

TEST(Ktx2, RefusesWhatItDidNotWriteOrWhatIsDamaged) {
    const std::vector<std::uint8_t> rgba = Noise(8 * 8 * 4, 3);
    std::vector<std::uint8_t>       file = dcdata::ktx2::Encode(8, 8, rgba);
    std::uint32_t                   width = 0;
    std::uint32_t                   height = 0;
    std::vector<std::uint8_t>       back;
    std::string                     why;
    std::vector<std::uint8_t>       cut(file.begin(), file.end() - 5);
    EXPECT_FALSE(dcdata::ktx2::Decode(cut, width, height, back, &why));
    EXPECT_FALSE(why.empty());
    std::vector<std::uint8_t> other_format = file;
    other_format[12] = 100;
    EXPECT_FALSE(dcdata::ktx2::Decode(other_format, width, height, back));
    std::vector<std::uint8_t> png = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n', 0, 0, 0, 0};
    EXPECT_FALSE(dcdata::ktx2::IsKtx2(png));
    EXPECT_TRUE(dcdata::ktx2::Encode(2, 2, std::vector<std::uint8_t>(3)).empty());
}

TEST(Texture, BakedKtx2LoaderReadsEveryLevel) {
    namespace fs = std::filesystem;
    const fs::path    dir = datafix::TempDir("texture_baked_ktx2");
    const fs::path    data = dir / "data";
    const std::string document =
        R"({"schema_version":3,"groups":[0],"entries":[{"name":"TEST","pictures":[{"levels":["test_0.ktx2","test_1.ktx2"]}]}]})";
    datafix::WriteBytes(data / "textures/img/test_img.json", datafix::Bytes(document.begin(), document.end()));
    const std::vector<std::uint8_t> big = Noise(2 * 2 * 4, 11);
    const std::vector<std::uint8_t> small = {1, 2, 3, 4};
    const std::vector<std::uint8_t> big_file = dcdata::ktx2::Encode(2, 2, big);
    const std::vector<std::uint8_t> small_file = dcdata::ktx2::Encode(1, 1, small);
    datafix::WriteBytes(data / "textures/img/test_0.ktx2", datafix::Bytes(big_file.begin(), big_file.end()));
    datafix::WriteBytes(data / "textures/img/test_1.ktx2", datafix::Bytes(small_file.begin(), small_file.end()));

    PathsReset();
    PathsConfigure({.app_dir = "chronicle", .env_data = "DC_DATA", .env_save = "DC_SAVE"});
    PathsSetDataRoot(data);
    InitCDFile();
    PortDecodedTexture decoded;
    std::string        error;
    ASSERT_TRUE(PortLoadBakedTexture("textures/img/test_img.json", "TEST", 0, 0, 0, 3, nullptr, 0, decoded, error)) << error;
    EXPECT_EQ(decoded.width, 2u);
    EXPECT_EQ(decoded.height, 2u);
    ASSERT_EQ(decoded.levels.size(), 2u);
    EXPECT_EQ(decoded.levels[0], big);
    EXPECT_EQ(decoded.levels[1], small);

    // A damaged one is refused, so the game decodes the disc's picture instead.
    std::vector<std::uint8_t> damaged = big_file;
    damaged.resize(damaged.size() - 3);
    datafix::WriteBytes(data / "textures/img/test_0.ktx2", datafix::Bytes(damaged.begin(), damaged.end()));
    InitCDFile();
    EXPECT_FALSE(PortLoadBakedTexture("textures/img/test_img.json", "TEST", 0, 0, 0, 3, nullptr, 0, decoded, error));
    EXPECT_NE(error.find("KTX2"), std::string::npos) << error;
    DataReadReset();
    PathsReset();
    std::error_code ignored;
    fs::remove_all(dir, ignored);
}

} // namespace dc::test
