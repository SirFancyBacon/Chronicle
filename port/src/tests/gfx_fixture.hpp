#pragma once

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "gfx/gfx.hpp"
#include "platform/window.hpp"
#include "render/renderer_calls.hpp"
#include "tests/platform_fixture.hpp"

namespace dc::test {

// Kept for the tests that name it: every fixture runs with no window, so that nothing needs a display.
struct GfxOptions {
    bool offscreen = true;
};

// A headless window, with Noktus drawing into a picture that a test reads back.
struct GfxFixture {
    explicit GfxFixture(int width = 640, int height = 480, float render_scale = 1.0f, const GfxOptions & = {}) {
        WindowConfig window{width, height, true};
        window.vulkan = false;
        WindowInit(window);
        gfx::RendererConfig config;
        config.logical_width = 640.0f;
        config.logical_height = 480.0f;
        config.display_aspect = 4.0f / 3.0f;
        config.render_scale = render_scale;
        gfx::RendererInit(WindowHandle(), config);
        render::MirrorTakeWindow(nullptr);
    }

    ~GfxFixture() {
        render::MirrorDropWindow();
        gfx::RendererShutdown();
        WindowShutdown();
    }

    // Draws nothing but what record does, on a cleared frame, and reads the frame back.
    template <class Record>
    void Frame(const std::array<uint8_t, 4> &clear, Record record) {
        ASSERT_TRUE(render::BeginFrame());
        render::Clear(true, clear.data(), true, 0.0f);
        record();
        render::EndFrame();
        ASSERT_TRUE(render::MirrorReadScreen(pixels, width, height));
    }

    std::array<uint8_t, 4> Pixel(uint32_t x, uint32_t y) const {
        const uint8_t *p = &pixels[(static_cast<size_t>(y) * width + x) * 4];
        return {p[0], p[1], p[2], p[3]};
    }

    bool PixelNear(uint32_t x, uint32_t y, int r, int g, int b, int tolerance = 2) const {
        std::array<uint8_t, 4> p = Pixel(x, y);
        bool                   near = std::abs(p[0] - r) <= tolerance && std::abs(p[1] - g) <= tolerance && std::abs(p[2] - b) <= tolerance;
        if (!near) {
            std::fprintf(stderr, "pixel %u,%u is %d,%d,%d, expected %d,%d,%d\n", x, y, p[0], p[1], p[2], r, g, b);
        }
        return near;
    }

    std::vector<uint8_t> pixels;
    uint32_t             width = 0;
    uint32_t             height = 0;
};

inline gfx::Vertex2D Vertex(float x, float y, float u, float v, std::array<uint8_t, 4> color, float z = 0.0f) {
    gfx::Vertex2D vertex = {};
    vertex.x = x;
    vertex.y = y;
    vertex.z = z;
    vertex.u = u;
    vertex.v = v;
    vertex.color[0] = color[0];
    vertex.color[1] = color[1];
    vertex.color[2] = color[2];
    vertex.color[3] = color[3];
    vertex.fog = 0xFF;
    return vertex;
}

// A rectangle as one quad, uv over [u0, u1] x [v0, v1].
inline std::array<gfx::Vertex2D, 4> Quad(float x, float y, float w, float h, std::array<uint8_t, 4> color, float u0 = 0, float v0 = 0, float u1 = 0, float v1 = 0, float z = 0.0f) {
    return {Vertex(x, y, u0, v0, color, z), Vertex(x + w, y, u1, v0, color, z), Vertex(x + w, y + h, u1, v1, color, z), Vertex(x, y + h, u0, v1, color, z)};
}

inline uint32_t Rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 0xFF) {
    return static_cast<uint32_t>(r) | static_cast<uint32_t>(g) << 8 | static_cast<uint32_t>(b) << 16 | static_cast<uint32_t>(a) << 24;
}

} // namespace dc::test
