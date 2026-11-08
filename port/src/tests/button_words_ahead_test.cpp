#include <gtest/gtest.h>

#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "render/types.hpp"
#include "text/caption_text.hpp"

namespace dc::test {

using namespace dc::text;

// Every picture a button word asks for, drawn anywhere on whole logical pixels, is one painted ahead: the full screen
// at 4K and 1080p, and a window of an odd size whose letters fall on its pixels many ways.
TEST(ButtonWordsAhead, CoverEveryWholePlaceOnTheScreen) {
    constexpr std::string_view kWords[] = {"EQUIP", "CUSTOMIZE", "ATTRIBUTE", "REPAIR", "UPGRADE", "BUILD-UP", "YES", "NO", "DUMP", "STATUS BREAK"};
    for (const render::LogicalMapping mapping : {render::LogicalMapping{4.5f, 4.5f, 480.0f, 0.0f, 3840, 2160},
                                                 render::LogicalMapping{2.25f, 2.25f, 240.0f, 0.0f, 1920, 1080},
                                                 render::LogicalMapping{1.6625f, 1.6625f, 151.0f, 0.0f, 1366, 798}}) {
        std::set<std::string> ahead;
        for (const capfont::PaintJob &job : caption::ButtonWordPaintJobs(mapping)) {
            ahead.insert(capfont::PaintJobKey(job));
        }
        for (const std::string_view word : kWords) {
            for (const bool pressed : {false, true}) {
                for (int x = 0; x < 640; x += 37) {
                    for (int y = 0; y < 480; y += 41) {
                        const std::vector<capfont::PaintJob> paints = caption::ButtonWordPaintsAt(word, static_cast<float>(x), static_cast<float>(y), pressed, mapping);
                        ASSERT_FALSE(paints.empty()) << word;
                        for (const capfont::PaintJob &paint : paints) {
                            ASSERT_TRUE(ahead.contains(capfont::PaintJobKey(paint)))
                                << word << (pressed ? " pressed" : "") << " at " << x << "," << y << ", scale " << mapping.scale_x;
                        }
                    }
                }
            }
        }
    }
}

} // namespace dc::test
