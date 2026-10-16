#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "ui_framework/buttons.hpp"
#include "ui_framework/surfaces.hpp"
#include "ui_framework/types.hpp"
#include "ui_framework/widgets.hpp"

namespace UI {

struct DialogOption {
    std::string           label;
    std::function<void()> on_select{};
    ButtonStyle           style = ButtonStyle::Thick();
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

    bool HandleInput();
    void Draw(int alpha = 0x80);

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

    Rect GetVisibleRowRect(int visible_slot) const;
    int  HitTest(float px, float py, int total_items) const;

    bool HandleInput(int total_items,
                     const std::function<void(int)>& on_confirm = {},
                     const std::function<void()>& on_cancel = {});

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

} // namespace UI
