#include "render/noktus_mirror.hpp"

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "noktus/vulkan_renderer.hpp"
#include "render/noktus_adapter.hpp"
#include "render/passes.hpp"

namespace dc::render {

namespace {

struct Mirror {
    std::string                                              path;
    std::unique_ptr<noktus::Renderer>                        renderer;
    std::unordered_map<gfx::TextureHandle, noktus::TextureId> textures;
    std::set<gfx::TextureHandle>                             wanted; // drawn with, and not made here: read across between ticks
    std::mutex                                               lock;
    bool                                                     open = false;
    std::uint64_t                                            drawn = 0;
    std::uint64_t                                            skipped = 0;
    std::uint64_t                                            elsewhere = 0;
};

// A mirror is a test tool: whatever goes wrong in it, the game plays on.
bool g_failed = false;

template <typename Work>
void Guarded(Work work) {
    try {
        work();
    } catch (const std::exception &error) {
        std::fprintf(stderr, "noktus mirror: stopped: %s\n", error.what());
        g_failed = true;
    }
}

Mirror &State() {
    static Mirror mirror;
    return mirror;
}

const char *RequestedPath() {
    static const char *path = std::getenv("DC_NOKTUS_MIRROR");
    return path != nullptr && *path != '\0' ? path : nullptr;
}

void WriteLastPicture() {
    Mirror &mirror = State();
    if (mirror.renderer) {
        Guarded([&] {
            std::uint32_t                   width = 0;
            std::uint32_t                   height = 0;
            const std::vector<std::uint8_t> pixels = noktus::ReadScreen(*mirror.renderer, &width, &height);
            gfx::WritePng(mirror.path, pixels.data(), width, height);
        });
        std::fprintf(stderr, "noktus mirror: %llu drawn, %llu skipped for a texture it lacks, %llu to other targets\n",
                     static_cast<unsigned long long>(mirror.drawn), static_cast<unsigned long long>(mirror.skipped),
                     static_cast<unsigned long long>(mirror.elsewhere));
        mirror.renderer.reset();
    }
}

void Start(Mirror &mirror) {
    if (mirror.renderer) {
        return;
    }
    mirror.path = RequestedPath();
    const gfx::LogicalMapping mapping = gfx::GetLogicalMapping(gfx::kMainTarget);
    noktus::VulkanHost        host;
    host.pixel_size = [width = mapping.pixel_width, height = mapping.pixel_height](std::uint32_t *w, std::uint32_t *h) {
        *w = width;
        *h = height;
    };
    mirror.renderer = noktus::CreateVulkanRenderer(host);
    std::atexit(WriteLastPicture);
}

void ReadAcross(Mirror &mirror, gfx::TextureHandle handle) {
    std::vector<std::uint8_t> pixels;
    std::uint32_t             width = 0;
    std::uint32_t             height = 0;
    const auto                info = gfx::GetTextureInfo(handle);
    // A render target holds today's renderer's own drawing, which would make the mirror show that and prove nothing.
    if (!info || info->format != gfx::TextureFormat::Rgba8 || info->render_target || !gfx::ReadbackTexture(handle, pixels, width, height)) {
        return;
    }
    auto found = mirror.textures.find(handle);
    if (found == mirror.textures.end()) {
        noktus::TextureDesc desc;
        desc.width = width;
        desc.height = height;
        desc.has_alpha = info->has_alpha;
        found = mirror.textures.emplace(handle, mirror.renderer->CreateTexture(desc)).first;
    }
    mirror.renderer->UpdateTexture(found->second, 0, {0, 0, width, height}, pixels);
}

noktus::Primitive2D ToPrimitive(gfx::Primitive primitive) {
    switch (primitive) {
        case gfx::Primitive::Triangles:
            return noktus::Primitive2D::Triangles;
        case gfx::Primitive::TriangleStrip:
            return noktus::Primitive2D::TriangleStrip;
        case gfx::Primitive::TriangleFan:
            return noktus::Primitive2D::TriangleFan;
        case gfx::Primitive::Lines:
            return noktus::Primitive2D::Lines;
        case gfx::Primitive::LineStrip:
            return noktus::Primitive2D::LineStrip;
        case gfx::Primitive::Quads:
            break;
    }
    return noktus::Primitive2D::Quads;
}

noktus::Layer2D ToLayer(Layer2D layer) {
    switch (layer) {
        case Layer2D::World:
            return noktus::Layer2D::World;
        case Layer2D::Window:
            return noktus::Layer2D::Window;
        case Layer2D::Cover:
            return noktus::Layer2D::Cover;
        default:
            return noktus::Layer2D::Ui;
    }
}

} // namespace

bool MirrorOn() {
    return RequestedPath() != nullptr && !g_failed;
}

void MirrorBeginTick() {
    if (!MirrorOn()) {
        return;
    }
    Mirror               &mirror = State();
    std::scoped_lock      guard(mirror.lock);
    Guarded([&] {
        Start(mirror);
        for (gfx::TextureHandle handle : mirror.wanted) {
            ReadAcross(mirror, handle);
        }
        mirror.renderer->BeginTick();
        mirror.open = true;
    });
}

void MirrorEndTick() {
    if (!MirrorOn()) {
        return;
    }
    Mirror          &mirror = State();
    std::scoped_lock guard(mirror.lock);
    if (mirror.open) {
        Guarded([&] { mirror.renderer->EndTick(); });
        mirror.open = false;
    }
}

void MirrorTexture(gfx::TextureHandle texture, const gfx::TextureDesc &desc) {
    if (!MirrorOn() || desc.format != gfx::TextureFormat::Rgba8) {
        return;
    }
    Mirror          &mirror = State();
    std::scoped_lock guard(mirror.lock);
    Guarded([&] {
        Start(mirror);
        noktus::TextureDesc made;
        made.width = desc.width;
        made.height = desc.height;
        made.levels = desc.mip_levels;
        made.has_alpha = desc.has_alpha;
        mirror.textures[texture] = mirror.renderer->CreateTexture(made);
    });
}

void MirrorTexturePixels(gfx::TextureHandle texture, std::uint32_t level, std::uint32_t x, std::uint32_t y, std::uint32_t width,
                         std::uint32_t height, const void *pixels) {
    if (!MirrorOn()) {
        return;
    }
    Mirror          &mirror = State();
    std::scoped_lock guard(mirror.lock);
    const auto       found = mirror.textures.find(texture);
    if (found != mirror.textures.end()) {
        const auto *bytes = static_cast<const std::uint8_t *>(pixels);
        Guarded([&] {
            mirror.renderer->UpdateTexture(found->second, level, {x, y, width, height}, std::span(bytes, static_cast<std::size_t>(width) * height * 4));
        });
    }
}

void MirrorDestroyTexture(gfx::TextureHandle texture) {
    if (!MirrorOn()) {
        return;
    }
    Mirror          &mirror = State();
    std::scoped_lock guard(mirror.lock);
    mirror.wanted.erase(texture);
    const auto found = mirror.textures.find(texture);
    if (found != mirror.textures.end()) {
        mirror.renderer->DestroyTexture(found->second);
        mirror.textures.erase(found);
    }
}

void MirrorDraw2D(gfx::Primitive primitive, std::span<const gfx::Vertex2D> vertices, const gfx::TextureBinding &texture,
                  const gfx::DrawState &state) {
    Mirror          &mirror = State();
    std::scoped_lock guard(mirror.lock);
    if (!mirror.open) {
        return;
    }
    if (gfx::CurrentRenderTarget() != gfx::kMainTarget) {
        mirror.elsewhere++;
        return;
    }
    const bool textured = texture.texture != gfx::kNullTexture;
    noktus::Draw2D draw;
    draw.material = ToMaterial(state, texture);
    if (textured) {
        const auto found = mirror.textures.find(texture.texture);
        if (found == mirror.textures.end()) {
            if (texture.texture != gfx::kPreviousFrame) {
                mirror.wanted.insert(texture.texture);
            }
            mirror.skipped++;
            return;
        }
        draw.material.albedo = found->second;
    } else {
        draw.material.albedo = noktus::TextureId::None;
    }
    std::vector<noktus::Vertex2D> converted;
    converted.reserve(vertices.size());
    for (const gfx::Vertex2D &vertex : vertices) {
        converted.push_back(ToVertex2D(vertex, textured));
    }
    draw.vertices = converted;
    draw.primitive = ToPrimitive(primitive);
    draw.layer = ToLayer(CurrentLayer2D());
    Guarded([&] { mirror.renderer->Draw(draw); });
    mirror.drawn++;
}

} // namespace dc::render
