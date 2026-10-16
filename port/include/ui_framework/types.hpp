#pragma once

#include <cstdint>
#include <functional>
#include <utility>

#include "gameutil.hpp"
#include "rect.hpp"

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

// Standard Metrics & Spacing Constants (640x480 Logical Canvas)
namespace Metrics {
    constexpr int   kScreenWidth       = 640;
    constexpr int   kScreenHeight      = 480;
    constexpr int   kScreenCenterX     = 320;
    constexpr int   kScreenCenterY     = 240;

    constexpr float kScaleButton       = 1.08f;
    constexpr float kScaleNormal       = 1.00f;
    constexpr float kScaleHeader       = 0.92f;
    constexpr float kScaleBody         = 0.84f;
    constexpr float kScaleSmall        = 0.76f;

    constexpr int   kWindowMarginX     = 28;
    constexpr int   kWindowMarginY     = 18;
    constexpr int   kSubWindowPadding  = 12;
    constexpr int   kPaneGap           = 16;
    constexpr int   kDividerMargin     = 10;

    constexpr int   kRowHeight         = 22;
    constexpr int   kRowStride         = 25;
    constexpr int   kIconSizeSmall     = 20;
    constexpr int   kIconSizeStandard  = 24;
    constexpr int   kIconTextGap       = 12;
    constexpr int   kCounterWidth      = 82;

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
    Thin,   // Slender font for Weapon Menu action buttons (EQUIP, CUSTOMIZE, etc.)
    Normal, // Standard thick font for YES/NO buttons, dialogs, and body text
    Bold,   // Emboldened main font
    Status  // SourceSansProSemibold for Weapon Status numbers & stat abbreviations
};

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

    constexpr Rect Anchored(Anchor anchor, int child_w, int child_h,
                            int offset_x = 0, int offset_y = 0) const {
        int cx = x;
        int cy = y;
        int cw = child_w;
        int ch = child_h;

        switch (anchor) {
            case Anchor::TopLeft:      cx = x;               cy = y;               break;
            case Anchor::TopCenter:    cx = x + (w - cw)/2;  cy = y;               break;
            case Anchor::TopRight:     cx = x + w - cw;      cy = y;               break;
            case Anchor::CenterLeft:   cx = x;               cy = y + (h - ch)/2;  break;
            case Anchor::Center:       cx = x + (w - cw)/2;  cy = y + (h - ch)/2;  break;
            case Anchor::CenterRight:  cx = x + w - cw;      cy = y + (h - ch)/2;  break;
            case Anchor::BottomLeft:   cx = x;               cy = y + h - ch;      break;
            case Anchor::BottomCenter: cx = x + (w - cw)/2;  cy = y + h - ch;      break;
            case Anchor::BottomRight:  cx = x + w - cw;      cy = y + h - ch;      break;
            case Anchor::TopWide:      cx = x;               cy = y;      cw = w;  break;
            case Anchor::BottomWide:   cx = x;               cy = y+h-ch; cw = w;  break;
            case Anchor::LeftWide:     cx = x;               cy = y;      ch = h;  break;
            case Anchor::RightWide:    cx = x + w - cw;      cy = y;      ch = h;  break;
            case Anchor::FullRect:     cx = x;               cy = y; cw = w; ch = h; break;
        }
        return Rect(cx + offset_x, cy + offset_y, cw, ch);
    }

    constexpr std::pair<Rect, Rect> SplitVertical(int left_w, int gap = Metrics::kPaneGap,
                                                  int* out_divider_x = nullptr) const {
        if (out_divider_x != nullptr) {
            *out_divider_x = x + left_w + (gap / 2);
        }
        const int right_x = x + left_w + gap;
        const int right_w = (x + w > right_x) ? (x + w - right_x) : 0;
        return {Rect(x, y, left_w, h), Rect(right_x, y, right_w, h)};
    }

    constexpr std::pair<Rect, Rect> SplitHorizontal(int top_h, int gap = Metrics::kPaneGap,
                                                    int* out_divider_y = nullptr) const {
        if (out_divider_y != nullptr) {
            *out_divider_y = y + top_h + (gap / 2);
        }
        const int bot_y = y + top_h + gap;
        const int bot_h = (y + h > bot_y) ? (y + h - bot_y) : 0;
        return {Rect(x, y, w, top_h), Rect(x, bot_y, w, bot_h)};
    }

    constexpr Rect Row(int index, int row_h = Metrics::kRowHeight,
                       int stride = Metrics::kRowStride) const {
        return Rect(x, y + index * stride, w, row_h);
    }
};

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

} // namespace UI
