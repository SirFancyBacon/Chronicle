#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "clsmes.hpp"
#include "gametext.hpp"
#include "gameutil.hpp"
#include "memcard.hpp"
#include "menu_draw.hpp"
#include "rect.hpp"
#include "texture.hpp"

namespace UI {

// Native palette constants from gameutil.hpp
constexpr u32 ColorWhite  = FONT_COLOR_WHITE;
constexpr u32 ColorGold   = FONT_COLOR_GOLD;
constexpr u32 ColorYellow = FONT_COLOR_YELLOW;
constexpr u32 ColorBlue   = 3; // Native Blue font in atlas 0x1A

constexpr u32 NATIVE_COLOR_WHITE  = FONT_COLOR_WHITE;
constexpr u32 NATIVE_COLOR_GOLD   = FONT_COLOR_GOLD;
constexpr u32 NATIVE_COLOR_YELLOW = FONT_COLOR_YELLOW;
constexpr u32 NATIVE_COLOR_BLUE   = ColorBlue;

enum class Alignment {
    Left,
    Center,
    Right
};

std::vector<std::string> WrapText(std::string_view text, int max_width);

// -----------------------------------------------------------------------------
// TrackerWindow: Dedicated, immutable window for Chest Tracker & Manuals
// -----------------------------------------------------------------------------
class TrackerWindow {
public:
    static void Draw(int x, int y, int width_px, int height_px, int alpha = 0x80);
};

// -----------------------------------------------------------------------------
// HelpBox: Dedicated dynamic window for the Battle Menu help box
// -----------------------------------------------------------------------------
class HelpBox {
public:
    static void Draw(int x, int y, int width_px, int height_px, int alpha = 0x80);
};

// General window alias mapping to TrackerWindow
class Window {
public:
    static void Draw(int x, int y, int width_px, int height_px, int alpha = 0x80);
};

// -----------------------------------------------------------------------------
// Label: GameText wrapper with glyph parsing, auto-width, stroke & alignment
// -----------------------------------------------------------------------------
class Label {
public:
    Label() = default;
    explicit Label(std::string_view text, u32 color = ColorWhite, Alignment align = Alignment::Left);

    void SetText(std::string_view text);
    void SetColor(u32 color);
    void SetAlignment(Alignment align);

    int GetWidth();
    void Draw(int x, int y, int alpha = 0x80);
    void DrawWithStroke(int x, int y, u32 fill_color, u32 stroke_color, int stroke_px = 2, int alpha = 0x80);
    void DrawWrapped(int x, int y, int max_width, int line_height = 18, int alpha = 0x80);

private:
    std::string m_raw_text;
    GameText m_text;
    u32 m_color{ColorWhite};
    Alignment m_align{Alignment::Left};
    bool m_dirty{true};
    int m_cached_width{0};
};

// -----------------------------------------------------------------------------
// Cursor: Pointing hand with target lerp, 2-axis bobbing & drop shadow
// -----------------------------------------------------------------------------
class Cursor {
public:
    Cursor();

    void SetTarget(float x, float y);
    void SnapTo(float x, float y);
    void Draw(int alpha = 0x80);

private:
    float m_cur_x{0.0f};
    float m_cur_y{0.0f};
    float m_target_x{0.0f};
    float m_target_y{0.0f};
    int m_anim_count{0};
    CTexture* m_texture{nullptr};
};

// -----------------------------------------------------------------------------
// Brackets: 4-corner pulsing selection framing
// -----------------------------------------------------------------------------
class Brackets {
public:
    Brackets();
    void Draw(int left, int right, int top, int bottom, int alpha = 0x80);

private:
    int m_beat{0};
    CTexture* m_texture{nullptr};
};

// -----------------------------------------------------------------------------
// ScrollBar: Vertical scroll bar with track, thumb, and auto-fade
// -----------------------------------------------------------------------------
class ScrollBar {
public:
    ScrollBar();
    void Update(bool moved);
    void Draw(int x, int top_y, int height, int total_rows, int visible_rows, int first_row, int alpha = 0x80);

private:
    int m_bar_frames{0};
    CTexture* m_track_tex{nullptr};
    CTexture* m_thumb_tex{nullptr};
};

} // namespace UI