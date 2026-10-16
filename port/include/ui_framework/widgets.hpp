#pragma once

#include <string_view>

#include "texture.hpp"
#include "ui_framework/types.hpp"

namespace UI {

class ItemIcon {
public:
    static void DrawSocket(int x, int y, int size = 26, bool filled = false, int alpha = 0x80);
    static bool Draw(int item_no, int x, int y, int size = Metrics::kIconSizeStandard, int alpha = 0x80,
                     bool drop_shadow = true, bool with_socket = false);
};

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

} // namespace UI
