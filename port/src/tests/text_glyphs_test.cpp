// Derived from Kazarov's tests/glyphs_test.cpp, by TheMoonPeople, under the MIT licence: see port/src/text/LICENSE.kazarov.
#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "gfx/gfx.hpp"
#include "platform/paths.hpp"
#include "text/glyphs.hpp"

TEST(TextGlyphs, CpuAtlasViewsShareDecodedPixelsAndRejectInvalidRegions) {
    const auto previous = PathsSaveRoot();
    const auto root = std::filesystem::temp_directory_path() / ("ct_cpu_glyph_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));

    struct Cleanup {
        std::filesystem::path previous, root;

        ~Cleanup() {
            dc::glyphs::Reset();
            PathsSetSaveRoot(previous);
            std::error_code error;
            std::filesystem::remove_all(root, error);
        }
    } cleanup{previous, root};

    std::filesystem::create_directories(root / "glyphs");
    PathsSetSaveRoot(root);
    const std::vector<uint8_t> pixels{255, 0, 0, 255, 0, 255, 0, 255};
    ASSERT_TRUE(gfx::WritePng(root / "glyphs/cpu.png", pixels.data(), 2, 1));
    std::ofstream(root / "glyphs/glyphs.json") << R"({"styles":{"parity_cpu":{"atlas":"cpu.png","glyphs":{"left":[0,0,1,1],"right":[1,0,1,1],"invalid":[2,0,1,1]}}}})";
    dc::glyphs::Reset();
    dc::glyphs::CpuImage left, right, invalid;
    dc::glyphs::CpuImage atlas;
    ASSERT_TRUE(dc::glyphs::FindAtlasPixels("parity_cpu", atlas));
    EXPECT_EQ(atlas.width, 2);
    EXPECT_EQ(atlas.height, 1);
    EXPECT_FALSE(dc::glyphs::FindAtlasPixels("missing_parity_style", invalid));
    ASSERT_TRUE(dc::glyphs::FindNamedPixels("parity_cpu", "left", left));
    ASSERT_TRUE(dc::glyphs::FindNamedPixels("parity_cpu", "right", right));
    EXPECT_EQ(left.rgba.data(), right.rgba.data());
    EXPECT_EQ(left.rgba.data(), atlas.rgba.data());
    EXPECT_EQ(left.rgba.size(), 8u);
    EXPECT_EQ(left.rgba[0], 255);
    EXPECT_EQ(right.rgba[4 + 1], 255);
    EXPECT_EQ(right.x, 1);
    EXPECT_FALSE(dc::glyphs::FindNamedPixels("parity_cpu", "invalid", invalid));
    EXPECT_FALSE(dc::glyphs::FindNamedPixels("parity_cpu", "missing", invalid));
}
