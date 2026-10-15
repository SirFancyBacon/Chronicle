#pragma once

#include <string>
#include <string_view>
#include <utility>
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
constexpr u32 ColorBlue   = 3;

constexpr u32 NATIVE_COLOR_WHITE  = FONT_COLOR_WHITE;
constexpr u32 NATIVE_COLOR_GOLD   = FONT_COLOR_GOLD;
constexpr u32 NATIVE_COLOR_YELLOW = FONT_COLOR_YELLOW;
constexpr u32 NATIVE_COLOR_BLUE   = ColorBlue;

enum class Alignment {
    Left,
    Center,
    Right
};

enum class FontWeight {
    Thin,
    Normal,
    Bold
};

// -----------------------------------------------------------------------------
// Rect: Screen-space and UV bounding box primitive
// -----------------------------------------------------------------------------
struct Rect {
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;

    constexpr Rect() = default;
    constexpr Rect(int in_x, int in_y, int in_w, int in_h)
        : x(in_x), y(in_y), w(in_w), h(in_h) {}

    constexpr int Left() const { return x; }
    constexpr int Right() const { return x + w; }
    constexpr int Top() const { return y; }
    constexpr int Bottom() const { return y + h; }
    constexpr int CenterX() const { return x + (w / 2); }
    constexpr int CenterY() const { return y + (h / 2); }
    constexpr bool Empty() const { return w <= 0 || h <= 0; }
    constexpr bool Contains(float px, float py) const {
        return px >= static_cast<float>(x) && px < static_cast<float>(x + w) &&
               py >= static_cast<float>(y) && py < static_cast<float>(y + h);
    }
};

// -----------------------------------------------------------------------------
// TextStyle: Declarative text styling (Color, Shadow, Justification, Scale, Stretch)
// -----------------------------------------------------------------------------
struct TextStyle {
    u32        color       = ColorWhite;        // Default: White
    bool       drop_shadow = true;              // Default: Drop shadow enabled
    Alignment  align       = Alignment::Center; // Default: Center justification
    float      scale       = 1.0f;              // Font size multiplier
    float      xscale      = 1.0f;              // Horizontal stretch multiplier (>1.0 = wider)
    FontWeight weight      = FontWeight::Normal;// Thin, Normal, or Bold
    int        line_height = 24;
    int        text_dx     = 0;
    int        text_dy     = 0;
};

std::vector<std::string> WrapText(std::string_view text, int max_width);

// -----------------------------------------------------------------------------
// Windows, Stone/Paper Boards & Skeuomorphic Indentation Primitives
// -----------------------------------------------------------------------------
enum class WindowPreset {
    BlueStone,
    Paper
};

constexpr WindowPreset BLUE_STONE_MENU = WindowPreset::BlueStone;
constexpr WindowPreset PAPER_MENU      = WindowPreset::Paper;

class TrackerWindow {
public:
    // Draws the seamless procedural window (BlueStone by default, or Paper)
    static void Draw(int x, int y, int width_px, int height_px, int alpha = 0x80,
                     WindowPreset preset = WindowPreset::BlueStone);
    static void Draw(const Rect& box, int alpha = 0x80,
                     WindowPreset preset = WindowPreset::BlueStone) {
        Draw(box.x, box.y, box.w, box.h, alpha, preset);
    }

    // Convenience helpers for the Paper variant
    static void DrawPaper(int x, int y, int width_px, int height_px, int alpha = 0x80) {
        Draw(x, y, width_px, height_px, alpha, WindowPreset::Paper);
    }
    static void DrawPaper(const Rect& box, int alpha = 0x80) {
        Draw(box.x, box.y, box.w, box.h, alpha, WindowPreset::Paper);
    }

    // Draws a 3-slice recessed stone trough ("perbrd" / "wepstatus") for rows or bars
    static void DrawIndentation(const Rect& box, bool dark_variant = true, int alpha = 0x80);

    // Draws the 3-slice molded scroll banner from "wepstatus" with optional centered title text
    static void DrawStoneBanner(const Rect& box, int alpha = 0x80, int brightness = 0x80,
                                std::string_view title = {},
                                const TextStyle& title_style = {
                                    .color = ColorGold,
                                    .drop_shadow = true,
                                    .align = Alignment::Center,
                                    .scale = 0.92f
                                });
};

class PaperWindow {
public:
    static void Draw(int x, int y, int width_px, int height_px, int alpha = 0x80) {
        TrackerWindow::Draw(x, y, width_px, height_px, alpha, WindowPreset::Paper);
    }
    static void Draw(const Rect& box, int alpha = 0x80) {
        TrackerWindow::Draw(box.x, box.y, box.w, box.h, alpha, WindowPreset::Paper);
    }
};

class HelpBox {
public:
    static void Draw(int x, int y, int width_px, int height_px, int alpha = 0x80);
    static void Draw(const Rect& box, int alpha = 0x80) {
        Draw(box.x, box.y, box.w, box.h, alpha);
    }

    // Draws an auto-sized, bottom-anchored help window with a title and wrapped description
    static void DrawTitled(int box_x, int bottom_y, int box_w, int text_offset_x,
                           std::string_view title, std::string_view desc,
                           int alpha = 0x80, int line_height = 28);
};

class Window {
public:
    static void Draw(int x, int y, int width_px, int height_px, int alpha = 0x80,
                     WindowPreset preset = WindowPreset::BlueStone);
    static void Draw(const Rect& box, int alpha = 0x80,
                     WindowPreset preset = WindowPreset::BlueStone) {
        Draw(box.x, box.y, box.w, box.h, alpha, preset);
    }
};

class TabPlate {
public:
    static void Draw(int left, int right, int y, int alpha = 0x80);
};

// Chiseled stone grooves for separating panes and totals
class Divider {
public:
    static void DrawVertical(int x, int top_y, int bottom_y, int alpha = 0x80);
    static void DrawHorizontal(int left_x, int right_x, int y, int alpha = 0x80);
};

// -----------------------------------------------------------------------------
// Label: Supports standard GameText AND scaled/stretched TrueType rendering
// -----------------------------------------------------------------------------
class Label {
public:
    Label() = default;
    explicit Label(std::string_view text, u32 color = ColorWhite,
                   Alignment align = Alignment::Left, bool drop_shadow = true,
                   float scale = 1.0f, FontWeight weight = FontWeight::Normal,
                   float xscale = 1.0f);
    Label(std::string_view text, const TextStyle& style);

    void SetText(std::string_view text);
    void SetColor(u32 color);
    void SetAlignment(Alignment align);
    void SetDropShadow(bool enabled);
    void SetScale(float scale);
    void SetXScale(float xscale);
    void SetWeight(FontWeight weight);
    void SetOffset(int dx, int dy);
    void SetStyle(const TextStyle& style);

    const std::string& GetText() const { return m_raw_text; }
    int GetWidth();

    void Draw(int x, int y, int alpha = 0x80);
    void DrawInRect(const Rect& box, int alpha = 0x80);
    void DrawCenteredInRect(int x, int y, int width, int height, int alpha = 0x80);
    void DrawWithStroke(int x, int y, u32 fill_color, u32 stroke_color, int stroke_px = 2, int alpha = 0x80);
    void DrawWrapped(int x, int y, int max_width, int line_height = 24, int alpha = 0x80);

private:
    std::string m_raw_text;
    GameText    m_text;
    u32         m_color{ColorWhite};
    Alignment   m_align{Alignment::Left};
    bool        m_drop_shadow{true};
    float       m_scale{1.0f};
    float       m_xscale{1.0f};
    FontWeight  m_weight{FontWeight::Normal};
    int         m_dx{0};
    int         m_dy{0};
    bool        m_dirty{true};
    int         m_cached_width{0};
};

// Immediate-mode helper to draw justified text inside a screen box
void DrawTextBox(std::string_view text, const Rect& box, const TextStyle& style = {}, int alpha = 0x80);

// Immediate-mode helper to draw a formatted "[ cur / max ]" counter
void DrawCounter(int x, int y, int current_val, int max_val,
                 float scale = 0.84f, int alpha = 0x80,
                 u32 normal_color = ColorWhite, u32 complete_color = ColorGold);

// -----------------------------------------------------------------------------
// ItemIcon: Draws a game item icon (optionally inside a recessed "wepstatus" stone socket)
// -----------------------------------------------------------------------------
class ItemIcon {
public:
    static void DrawSocket(int x, int y, int size = 26, bool filled = false, int alpha = 0x80);
    static bool Draw(int item_no, int x, int y, int size = 24, int alpha = 0x80,
                     bool drop_shadow = true, bool with_socket = false);
};

// -----------------------------------------------------------------------------
// Cursor: Pointing hand supporting both gamepad lerping and mouse pointing
// -----------------------------------------------------------------------------
class Cursor {
public:
    Cursor();

    void SetTarget(float x, float y);
    void SnapTo(float x, float y);
    void SetPointer(float x, float y);
    void ClearPointer();
    bool IsPointing() const { return m_pointing; }
    void Draw(int alpha = 0x80);

private:
    float m_cur_x{0.0f};
    float m_cur_y{0.0f};
    float m_target_x{0.0f};
    float m_target_y{0.0f};
    bool  m_pointing{false};
    float m_pointer_x{0.0f};
    float m_pointer_y{0.0f};
    int   m_anim_count{0};
    CTexture* m_texture{nullptr};
};

// -----------------------------------------------------------------------------
// Brackets: 4-corner pulsing selection framing (works in all menus)
// -----------------------------------------------------------------------------
class Brackets {
public:
    Brackets();
    void Draw(int left, int right, int top, int bottom, int alpha = 0x80);
    void Draw(const Rect& box, int alpha = 0x80) {
        Draw(box.Left(), box.Right(), box.Top(), box.Bottom(), alpha);
    }

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

// -----------------------------------------------------------------------------
// BevelButton: Rounded procedural beveled button with wide vanilla text option
// -----------------------------------------------------------------------------
enum class ButtonPreset {
    Terracotta,
    StoneGreen
};

struct ButtonStyle {
    ButtonPreset preset      = ButtonPreset::Terracotta;
    u32          text_color  = ColorWhite;        // Default: White (matches vanilla YES/NO)
    bool         drop_shadow = true;              // Default: Drop shadow on
    Alignment    align       = Alignment::Center; // Default: Center justified
    float        text_scale  = 1.08f;             // Slightly larger scale matching vanilla YES/NO height
    float        text_xscale = 1.25f;             // Wide X-axis stretch matching vanilla button font
    FontWeight   text_weight = FontWeight::Normal;
    bool         rounded     = true;              // Rounded/chamfered button corners
    int          text_dx     = 0;
    int          text_dy     = 0;
};

class BevelButton {
public:
    static void Draw(const Rect& box, std::string_view text,
                     const ButtonStyle& style = {}, int alpha = 0x80);

    static void Draw(int x, int y, int width, int height, std::string_view text,
                     u32 text_color = ColorWhite, int alpha = 0x80,
                     ButtonPreset preset = ButtonPreset::Terracotta,
                     Alignment align = Alignment::Center,
                     bool drop_shadow = true);
};

// -----------------------------------------------------------------------------
// ConfirmModal: Centered two-choice confirmation dialog with BevelButtons
// -----------------------------------------------------------------------------
class ConfirmModal {
public:
    static void Draw(std::string_view title, std::string_view description,
                     std::string_view yes_label, std::string_view no_label,
                     int selected_choice, Cursor& cursor, int alpha = 0x80);
};

// -----------------------------------------------------------------------------
// TextureButton: Sprite-backed button with optional text overlay
// -----------------------------------------------------------------------------
class TextureButton {
public:
    static void Draw(const Rect& box, CTexture* texture, const Rect& uv,
                     int alpha = 0x80, std::string_view text = {},
                     const TextStyle& text_style = {});

    static void Draw(int x, int y, int width, int height,
                     CTexture* texture, int u, int v, int uv_w, int uv_h,
                     int alpha = 0x80, std::string_view text = {},
                     const TextStyle& text_style = {});
};

// -----------------------------------------------------------------------------
// ProgressBar: Skeuomorphic recessed stone trough + PS2 gradation fill
// -----------------------------------------------------------------------------
struct ProgressBarStyle {
    u32        text_color  = ColorWhite;
    bool       drop_shadow = true;
    float      text_scale  = 0.78f;
    FontWeight text_weight = FontWeight::Normal;
    u8         fill_r = 28,  fill_g = 68,  fill_b = 45;
    u8         hi_r   = 45,  hi_g   = 95,  hi_b   = 65;
    u8         done_r = 95,  done_g = 70,  done_b = 18;
    u8         done_hi_r = 125, done_hi_g = 98, done_hi_b = 32;
};

class ProgressBar {
public:
    static void Draw(const Rect& box, float progress, std::string_view text = {},
                     const ProgressBarStyle& style = {}, int alpha = 0x80);
};

/**
 * Battle menu translation keys and English defaults.
 */
std::vector<std::pair<std::string, std::string>> BattleMenuStrings();

/**
 * Journal translation keys and English defaults.
 */
std::vector<std::pair<std::string, std::string>> JournalStrings();

} // namespace UI
