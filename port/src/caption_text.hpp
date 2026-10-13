#pragma once

#include <array>
#include <optional>
#include <string_view>
#include <vector>

#include "texture_port.hpp"

namespace caption {

constexpr int kIcons = 14;

struct CaptionBounds {
    float left;
    float top;
    float width;
    float height;
};

// Sets an explicit localization override for the next Draw() call
void SetNextCaption(const char *key, const char *english);
void SetNextCaptionText(std::string_view text);

std::vector<std::pair<std::string, std::string>> Strings();
bool Ready();
bool ThinReady();
void PreparePainted(const char *name, const PortDecodedTexture &texture);
std::optional<CaptionBounds> PaintedBounds(int icon);
bool Draw(int icon, int x, int y, int box_width, int alpha, int bright);

struct LetterWidth {
    float width;
    bool  blank;
};

std::vector<LetterWidth> LetterWidths(std::string_view utf8);

struct FillField {
    uint32_t                   id;
    int                        width;
    int                        height;
    std::vector<unsigned char> rgb;
};

struct LineStyle {
    float            dx = 0.0f;
    float            dy = 0.0f;
    float            xscale = 1.0f;
    float            yscale = 1.0f;
    float            letter = 0.0f;
    float            word = 0.0f;
    float            weight = 0.0f;
    float            opacity = 1.0f;
    bool             thin = false;
    bool             ink_fit = false;
    unsigned char    fill[3] = {255, 255, 255};
    float            edge_width = 0.0f;
    unsigned char    edge[3] = {0, 0, 0};
    float            glow_width = 0.0f;
    float            glow_amount = 0.0f;
    unsigned char    glow[3] = {0, 0, 0};
    const FillField *field = nullptr;
    float            field_x = 0.0f;
    float            field_y = 0.0f;
    float            field_w = 0.0f;
    float            field_h = 0.0f;
    std::vector<std::array<float, 2>> slots;
};

struct LineBounds {
    float left;
    float right;
};

bool DrawLine(std::string_view utf8, float center_x, float cap_top, float cap_height, float xsq, float max_width,
              const LineStyle &style, float fade = 1.0f, float rows = 1.0f, LineBounds *bounds = nullptr);

} // namespace caption