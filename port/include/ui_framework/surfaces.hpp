#pragma once

#include <string_view>
#include <utility>

#include "ui_framework/text.hpp"
#include "ui_framework/types.hpp"

namespace UI {

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

    static void DrawIndentation(const Rect& box, bool dark_variant = true, int alpha = 0x80);

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

class Divider {
public:
    static void DrawVertical(int x, int top_y, int bottom_y, int alpha = 0x80);
    static void DrawHorizontal(int left_x, int right_x, int y, int alpha = 0x80);
};

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

} // namespace UI
