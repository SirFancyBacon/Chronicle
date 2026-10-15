#pragma once

#include <cstdint>
#include <functional>
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

// -----------------------------------------------------------------------------
// Native Palette Constants (from gameutil.hpp)
// -----------------------------------------------------------------------------
constexpr u32 ColorWhite  = FONT_COLOR_WHITE;
constexpr u32 ColorGold   = FONT_COLOR_GOLD;
constexpr u32 ColorYellow = FONT_COLOR_YELLOW;
constexpr u32 ColorBlue   = 3;

constexpr u32 NATIVE_COLOR_WHITE  = FONT_COLOR_WHITE;
constexpr u32 NATIVE_COLOR_GOLD   = FONT_COLOR_GOLD;
constexpr u32 NATIVE_COLOR_YELLOW = FONT_COLOR_YELLOW;
constexpr u32 NATIVE_COLOR_BLUE   = ColorBlue;

// -----------------------------------------------------------------------------
// Standard Metrics & Spacing Constants (640x480 Logical Canvas)
// -----------------------------------------------------------------------------
namespace Metrics {
    constexpr int   kScreenWidth       = 640;
    constexpr int   kScreenHeight      = 480;
    constexpr int   kScreenCenterX     = 320;
    constexpr int   kScreenCenterY     = 240;

    // Standard Font Scales
    constexpr float kScaleButton       = 1.08f;
    constexpr float kScaleNormal       = 1.00f;
    constexpr float kScaleHeader       = 0.92f;
    constexpr float kScaleBody         = 0.84f;
    constexpr float kScaleSmall        = 0.76f;

    // Standard Window & SubWindow Spacing
    constexpr int   kWindowMarginX     = 28;
    constexpr int   kWindowMarginY     = 18;
    constexpr int   kSubWindowPadding  = 12;
    constexpr int   kPaneGap           = 16;
    constexpr int   kDividerMargin     = 10;

    // Standard Row & List Geometry
    constexpr int   kRowHeight         = 22;
    constexpr int   kRowStride         = 25;
    constexpr int   kIconSizeSmall     = 20;
    constexpr int   kIconSizeStandard  = 24;
    constexpr int   kIconTextGap       = 12;
    constexpr int   kCounterWidth      = 82;

    // Standard Widget Dimensions
    constexpr int   kButtonWidth       = 100;
    constexpr int   kButtonHeight      = 26;
    constexpr int   kButtonGap         = 6;
    constexpr int   kBannerWidth       = 340;
    constexpr int   kBannerHeight      = 28;
    constexpr int   kProgressBarHeight = 22;
    constexpr int   kModalMinWidth     = 260;
    constexpr int   kModalMaxWidth     = 360;
} // namespace Metrics

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
// Godot-Style Anchor Presets for Windows & SubWindows
// -----------------------------------------------------------------------------
enum class Anchor {
    TopLeft,
    TopCenter,
    TopRight,
    CenterLeft,
    Center,
    CenterRight,
    BottomLeft,
    BottomCenter,
    BottomRight,
    TopWide,
    BottomWide,
    LeftWide,
    RightWide,
    FullRect
};

// -----------------------------------------------------------------------------
// Rect: Screen-Space & SubWindow Layout Primitive
// -----------------------------------------------------------------------------
struct Rect {
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;

    constexpr Rect() = default;
    constexpr Rect(int in_x, int in_y, int in_w, int in_h)
        : x(in_x), y(in_y), w(in_w), h(in_h) {}

    static constexpr Rect Screen() {
        return Rect(0, 0, Metrics::kScreenWidth, Metrics::kScreenHeight);
    }

    constexpr int  Left() const    { return x; }
    constexpr int  Right() const   { return x + w; }
    constexpr int  Top() const     { return y; }
    constexpr int  Bottom() const  { return y + h; }
    constexpr int  CenterX() const { return x + (w / 2); }
    constexpr int  CenterY() const { return y + (h / 2); }
    constexpr bool Empty() const   { return w <= 0 || h <= 0; }

    constexpr bool Contains(float px, float py) const {
        return px >= static_cast<float>(x) && px < static_cast<float>(x + w) &&
               py >= static_cast<float>(y) && py < static_cast<float>(y + h);
    }

    constexpr Rect Offset(int dx, int dy) const {
        return Rect(x + dx, y + dy, w, h);
    }

    // Shrinks the rectangle inward (like Godot's MarginContainer)
    constexpr Rect Inset(int all) const {
        return Inset(all, all, all, all);
    }
    constexpr Rect Inset(int pad_x, int pad_y) const {
        return Inset(pad_x, pad_y, pad_x, pad_y);
    }
    constexpr Rect Inset(int left, int top, int right, int bottom) const {
        const int nw = w - (left + right);
        const int nh = h - (top + bottom);
        return Rect(x + left, y + top, nw > 0 ? nw : 0, nh > 0 ? nh : 0);
    }

    // Computes a child Rect anchored inside this Rect using a Godot-style Anchor preset
    constexpr Rect Anchored(Anchor anchor, int child_w, int child_h,
                            int offset_x = 0, int offset_y = 0) const {
        int cx = x;
        int cy = y;
        int cw = child_w;
        int ch = child_h;

        switch (anchor) {
            case Anchor::TopLeft:
                cx = x;
                cy = y;
                break;
            case Anchor::TopCenter:
                cx = x + (w - cw) / 2;
                cy = y;
                break;
            case Anchor::TopRight:
                cx = x + w - cw;
                cy = y;
                break;
            case Anchor::CenterLeft:
                cx = x;
                cy = y + (h - ch) / 2;
                break;
            case Anchor::Center:
                cx = x + (w - cw) / 2;
                cy = y + (h - ch) / 2;
                break;
            case Anchor::CenterRight:
                cx = x + w - cw;
                cy = y + (h - ch) / 2;
                break;
            case Anchor::BottomLeft:
                cx = x;
                cy = y + h - ch;
                break;
            case Anchor::BottomCenter:
                cx = x + (w - cw) / 2;
                cy = y + h - ch;
                break;
            case Anchor::BottomRight:
                cx = x + w - cw;
                cy = y + h - ch;
                break;
            case Anchor::TopWide:
                cx = x;
                cy = y;
                cw = w;
                break;
            case Anchor::BottomWide:
                cx = x;
                cy = y + h - ch;
                cw = w;
                break;
            case Anchor::LeftWide:
                cx = x;
                cy = y;
                ch = h;
                break;
            case Anchor::RightWide:
                cx = x + w - cw;
                cy = y;
                ch = h;
                break;
            case Anchor::FullRect:
                cx = x;
                cy = y;
                cw = w;
                ch = h;
                break;
        }
        return Rect(cx + offset_x, cy + offset_y, cw, ch);
    }

    // Splits this Rect into Left and Right child sub-rects with a gap between them
    constexpr std::pair<Rect, Rect> SplitVertical(int left_w, int gap = Metrics::kPaneGap,
                                                  int* out_divider_x = nullptr) const {
        if (out_divider_x != nullptr) {
            *out_divider_x = x + left_w + (gap / 2);
        }
        const int right_x = x + left_w + gap;
        const int right_w = (x + w > right_x) ? (x + w - right_x) : 0;
        return {Rect(x, y, left_w, h), Rect(right_x, y, right_w, h)};
    }

    // Splits this Rect into Top and Bottom child sub-rects with a gap between them
    constexpr std::pair<Rect, Rect> SplitHorizontal(int top_h, int gap = Metrics::kPaneGap,
                                                    int* out_divider_y = nullptr) const {
        if (out_divider_y != nullptr) {
            *out_divider_y = y + top_h + (gap / 2);
        }
        const int bot_y = y + top_h + gap;
        const int bot_h = (y + h > bot_y) ? (y + h - bot_y) : 0;
        return {Rect(x, y, w, top_h), Rect(x, bot_y, w, bot_h)};
    }

    // Returns the bounding box of row `index` inside this Rect
    constexpr Rect Row(int index, int row_h = Metrics::kRowHeight,
                       int stride = Metrics::kRowStride) const {
        return Rect(x, y + index * stride, w, row_h);
    }
};

// -----------------------------------------------------------------------------
// ZLayer & LayerQueue: Deterministic Draw Ordering
// -----------------------------------------------------------------------------
enum class ZLayer : int {
    Background = 0,
    Window     = 10,
    Recess     = 20,
    Content    = 30,
    Widgets    = 40,
    Brackets   = 50,
    Modal      = 60,
    Overlay    = 70,
    Cursor     = 80
};

class LayerQueue {
public:
    static void Begin();
    static void Submit(ZLayer layer, std::function<void()> draw_fn);
    static void Flush();
};

// -----------------------------------------------------------------------------
// TextStyle: Declarative Text Styling
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

std::vector<std::string> WrapText(std::string_view text, int max_width, float scale = 1.0f);

// -----------------------------------------------------------------------------
// Windows, Stone/Paper/DarkTranslucent Boards & Indentation Primitives
// -----------------------------------------------------------------------------
enum class WindowPreset {
    BlueStone,
    Paper,
    DarkTranslucent,
    Recessed,
    None
};

constexpr WindowPreset BLUE_STONE_MENU       = WindowPreset::BlueStone;
constexpr WindowPreset PAPER_MENU            = WindowPreset::Paper;
constexpr WindowPreset DARK_TRANSLUCENT_MENU = WindowPreset::DarkTranslucent;

class TrackerWindow {
public:
    // Draws a procedural or native window (BlueStone, Paper, DarkTranslucent, or Recessed)
    static void Draw(int x, int y, int width_px, int height_px, int alpha = 0x80,
                     WindowPreset preset = WindowPreset::BlueStone);
    static void Draw(const Rect& box, int alpha = 0x80,
                     WindowPreset preset = WindowPreset::BlueStone) {
        Draw(box.x, box.y, box.w, box.h, alpha, preset);
    }

    static void DrawPaper(int x, int y, int width_px, int height_px, int alpha = 0x80) {
        Draw(x, y, width_px, height_px, alpha, WindowPreset::Paper);
    }
    static void DrawPaper(const Rect& box, int alpha = 0x80) {
        Draw(box.x, box.y, box.w, box.h, alpha, WindowPreset::Paper);
    }

    static void DrawDark(int x, int y, int width_px, int height_px, int alpha = 0x80) {
        Draw(x, y, width_px, height_px, alpha, WindowPreset::DarkTranslucent);
    }
    static void DrawDark(const Rect& box, int alpha = 0x80) {
        Draw(box.x, box.y, box.w, box.h, alpha, WindowPreset::DarkTranslucent);
    }

    // Draws a shade-mask recessed trough that inherits the underlying panel's color
    static void DrawIndentation(const Rect& box, bool dark_variant = true, int alpha = 0x80);

    // Draws the 3-slice molded scroll banner from "wepstatus" with optional centered title text
    static void DrawStoneBanner(const Rect& box, int alpha = 0x80, int brightness = 0x80,
                                std::string_view title = {},
                                const TextStyle& title_style = {
                                    .color = ColorGold,
                                    .drop_shadow = true,
                                    .align = Alignment::Center,
                                    .scale = Metrics::kScaleHeader
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

// 4px recessed shade-mask grooves for separating panes and totals
class Divider {
public:
    static void DrawVertical(int x, int top_y, int bottom_y, int alpha = 0x80);
    static void DrawHorizontal(int left_x, int right_x, int y, int alpha = 0x80);
};

// -----------------------------------------------------------------------------
// Label: Supports standard GameText AND artifact-free scaled TrueType rendering
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
    void DrawAnchored(const Rect& parent, Anchor anchor, int offset_x = 0, int offset_y = 0, int alpha = 0x80);
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

void DrawTextBox(std::string_view text, const Rect& box, const TextStyle& style = {}, int alpha = 0x80);

void DrawCounter(int x, int y, int current_val, int max_val,
                 float scale = Metrics::kScaleBody, int alpha = 0x80,
                 u32 normal_color = ColorWhite, u32 complete_color = ColorGold);

// -----------------------------------------------------------------------------
// SubWindow: Godot-Inspired Container with Anchoring, Splitting & Optional Surface
// -----------------------------------------------------------------------------
class SubWindow {
public:
    constexpr SubWindow() = default;
    constexpr explicit SubWindow(const Rect& bounds, WindowPreset preset = WindowPreset::None,
                                 int pad_x = 0, int pad_y = 0)
        : m_outer(bounds),
          m_content(bounds.Inset(pad_x, pad_y)),
          m_preset(preset) {}

    static constexpr SubWindow Screen(int pad_x = 0, int pad_y = 0) {
        return SubWindow(Rect::Screen(), WindowPreset::None, pad_x, pad_y);
    }

    constexpr const Rect& OuterRect() const   { return m_outer; }
    constexpr const Rect& ContentRect() const { return m_content; }

    void DrawBackground(int alpha = 0x80) const;

    void DrawBanner(std::string_view title, int banner_w = Metrics::kBannerWidth,
                    int banner_h = Metrics::kBannerHeight, int y_offset = -17,
                    int alpha = 0x80) const;

    constexpr Rect AnchorRect(Anchor anchor, int w, int h, int dx = 0, int dy = 0) const {
        return m_content.Anchored(anchor, w, h, dx, dy);
    }

    void DrawAnchoredLabel(std::string_view text, Anchor anchor, const TextStyle& style = {},
                           int dx = 0, int dy = 0, int alpha = 0x80) const;

    std::pair<SubWindow, SubWindow> SplitVertical(int left_w, int gap = Metrics::kPaneGap,
                                                  bool draw_divider = false, int alpha = 0x80) const;

    std::pair<SubWindow, SubWindow> SplitHorizontal(int top_h, int gap = Metrics::kPaneGap,
                                                    bool draw_divider = false, int alpha = 0x80) const;

private:
    Rect         m_outer{};
    Rect         m_content{};
    WindowPreset m_preset{WindowPreset::None};
};

// -----------------------------------------------------------------------------
// ItemIcon: Draws a game item icon (optionally inside a recessed "wepstatus" stone socket)
// -----------------------------------------------------------------------------
class ItemIcon {
public:
    static void DrawSocket(int x, int y, int size = 26, bool filled = false, int alpha = 0x80);
    static bool Draw(int item_no, int x, int y, int size = Metrics::kIconSizeStandard, int alpha = 0x80,
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
    void DeferDraw(int alpha = 0x80);

private:
    float     m_cur_x{0.0f};
    float     m_cur_y{0.0f};
    float     m_target_x{0.0f};
    float     m_target_y{0.0f};
    bool      m_pointing{false};
    float     m_pointer_x{0.0f};
    float     m_pointer_y{0.0f};
    int       m_anim_count{0};
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
    void DeferDraw(const Rect& box, int alpha = 0x80);

private:
    int       m_beat{0};
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
    int       m_bar_frames{0};
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
    u32          text_color  = ColorWhite;
    bool         drop_shadow = true;
    Alignment    align       = Alignment::Center;
    float        text_scale  = Metrics::kScaleButton;
    float        text_xscale = 1.25f;
    FontWeight   text_weight = FontWeight::Normal;
    bool         rounded     = true;
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
// DialogBox & ConfirmModal: Extensible Multi-Choice Dialogs with Callbacks
// -----------------------------------------------------------------------------
struct DialogOption {
    std::string           label;
    std::function<void()> on_select{};
    ButtonStyle           style{};
    bool                  enabled = true;
};

struct DialogStyle {
    WindowPreset window_preset = WindowPreset::DarkTranslucent;
    int          min_width     = Metrics::kModalMinWidth;
    int          max_width     = Metrics::kModalMaxWidth;
    int          button_width  = Metrics::kButtonWidth;
    int          button_height = Metrics::kButtonHeight;
    int          button_gap    = Metrics::kButtonGap;
    int          base_y        = 164;
};

class DialogBox {
public:
    DialogBox() = default;

    void Configure(std::string_view title, std::string_view description,
                   std::vector<DialogOption> options,
                   std::function<void()> on_cancel = {},
                   const DialogStyle& style = {});

    void Open(int default_choice = 0);
    void Close() { m_active = false; }
    bool IsOpen() const { return m_active; }

    int  SelectedIndex() const { return m_selected; }
    void SetSelectedIndex(int index);

    // Processes gamepad/keyboard and mouse hover/click; invokes callbacks on confirm/cancel.
    // Returns true while the dialog remains open.
    bool HandleInput();

    // Draws the dialog box, wrapped text, buttons, brackets, and cursor in proper Z-order.
    void Draw(int alpha = 0x80);

    // Returns the screen Rect of option `index` (for external mouse hit-testing)
    Rect GetButtonRect(int index) const;
    int  HitTest(float px, float py) const;

private:
    Rect ComputeBoxRect(std::vector<std::string>& out_lines) const;

    bool                      m_active{false};
    int                       m_selected{0};
    std::string               m_title;
    std::string               m_description;
    std::vector<DialogOption> m_options;
    std::function<void()>     m_on_cancel;
    DialogStyle               m_style{};
    Cursor                    m_cursor;
};

class ConfirmModal {
public:
    static void Draw(std::string_view title, std::string_view description,
                     std::string_view yes_label, std::string_view no_label,
                     int selected_choice, Cursor& cursor, int alpha = 0x80,
                     WindowPreset preset = WindowPreset::DarkTranslucent);
};

// -----------------------------------------------------------------------------
// ListView: Expandable List with Unified Layout, Scrolling & Mouse Hit-Testing
// -----------------------------------------------------------------------------
struct ListViewConfig {
    Rect  bounds{};
    int   row_height      = Metrics::kRowHeight;
    int   row_stride      = Metrics::kRowStride;
    int   visible_rows    = 6;
    int   bracket_pad_x   = 4;
    int   bracket_pad_y   = 1;
    float cursor_offset_x = -44.0f;
    float cursor_offset_y = -2.0f;
    bool  wrap_navigation = true;
    bool  draw_brackets   = true;
    bool  draw_scrollbar  = true;
    bool  draw_cursor     = true;
};

class ListView {
public:
    using RowDrawFn = std::function<void(int item_index, const Rect& row_rect, bool selected, int alpha)>;

    ListView() = default;
    explicit ListView(const ListViewConfig& cfg) : m_cfg(cfg) {}

    void SetConfig(const ListViewConfig& cfg) { m_cfg = cfg; }
    const ListViewConfig& GetConfig() const   { return m_cfg; }

    void Reset(int initial_index = 0);

    int  SelectedIndex() const { return m_selected; }
    int  FirstRow() const      { return m_first_row; }
    void SetSelectedIndex(int index, int total_items);

    // Returns the exact screen Rect for a visible slot (0 .. visible_rows - 1)
    Rect GetVisibleRowRect(int visible_slot) const;

    // Returns the item index (0 .. total_items - 1) under screen point (px, py), or -1
    int  HitTest(float px, float py, int total_items) const;

    // Handles PAD_UP/DOWN, mouse hover, mouse wheel, and Cross/Circle clicks.
    // Returns true if selection changed this tick.
    bool HandleInput(int total_items,
                     const std::function<void(int)>& on_confirm = {},
                     const std::function<void()>& on_cancel = {});

    // Draws visible rows via `draw_row`, then overlays ScrollBar, Brackets, and Cursor in proper order.
    void Draw(int total_items, const RowDrawFn& draw_row, int alpha = 0x80);

    Cursor&   GetCursor()   { return m_cursor; }
    Brackets& GetBrackets() { return m_brackets; }

private:
    void EnsureVisible(int total_items);

    ListViewConfig m_cfg{};
    int            m_selected{0};
    int            m_first_row{0};
    Cursor         m_cursor;
    Brackets       m_brackets;
    ScrollBar      m_scrollbar;
};

// -----------------------------------------------------------------------------
// TextureButton & ProgressBar
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

struct ProgressBarStyle {
    u32        text_color  = ColorWhite;
    bool       drop_shadow = true;
    float      text_scale  = Metrics::kScaleSmall;
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

std::vector<std::pair<std::string, std::string>> BattleMenuStrings();
std::vector<std::pair<std::string, std::string>> JournalStrings();

} // namespace UI