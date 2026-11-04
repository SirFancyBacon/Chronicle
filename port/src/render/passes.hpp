#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "gfx/gfx.hpp"
#include "render/passes.hpp"

namespace dc::render {

/**
 * The named stretches of a tick's drawing, so the frame's shape is written down in the code rather than implied by the order
 * of its calls. Naming a pass draws nothing and changes nothing.
 */
enum class Pass : std::uint8_t {
    Unnamed,          /**< Drawing nothing has named yet: a trace reports it, and there should be none. */
    TextureAnimation, /**< Copies that animate textures, before a world frame draws. */
    Sky,              /**< Sky, sun and moon. */
    Ground,           /**< The town's ground, parts and the dungeon's map. */
    Fishing,          /**< The fish and the fishing line. */
    Water,            /**< The frame grab for water, and the water and ripples drawn from it. */
    DepthOfField,     /**< The blur laid over what has been drawn so far. */
    Characters,       /**< The player, villagers, monsters and what they hold. */
    Shadows,          /**< The shadow volumes and the darkening they mark. */
    MovingParts,      /**< Parts that move, drawn over the shadows. */
    Effects,          /**< World effects: fire, spray, shots, shimmer and heat haze. */
    LensFlare,        /**< The town's lens flare. */
    Overlay,          /**< 2D over the world: the HUD, messages and event sprites. */
    Menu,             /**< 2D drawn outside the world's frame: menus and the floor select. */
    Fade,             /**< Full-screen fades. */
    Title,            /**< The title screen. */
    Opening,          /**< The opening. */
    Attract,          /**< The attract movie. */
    Screen,           /**< Screens outside the game's world: language, saving, memory check. */
    Count,
};

/**
 * Where a 2D draw belongs on screen, said outright so that nothing has to work it out from the draw's shape. Each pass has a
 * usual layer; a draw that differs says so with a Layer2DScope.
 */
enum class Layer2D : std::uint8_t {
    World,  /**< Among the 3D scene, and placed by it. */
    Ui,     /**< Interface: follows the interface size, and stays where it's put. */
    Window, /**< Reaches the window's edges, past the 4:3 frame: fades, fills and images of the frame. */
    Cover,  /**< A full-frame picture, stretched to fill the window. */
    Count,
};

/** The pass's name, for a trace. */
const char *PassName(Pass pass);

/** The pass drawing belongs to from now on. */
void SetPass(Pass pass);

/** The pass drawing belongs to now. */
Pass CurrentPass();

/** Names a stretch inside another pass, and returns to that pass when it ends. */
class PassScope {
public:
    explicit PassScope(Pass pass) : previous_(CurrentPass()) { SetPass(pass); }
    ~PassScope() { SetPass(previous_); }
    PassScope(const PassScope &) = delete;
    PassScope &operator=(const PassScope &) = delete;

private:
    Pass previous_;
};

/** The layer a 2D draw is in now: the one a scope gives, or else the pass's usual one. */
Layer2D CurrentLayer2D();

/** Puts the 2D drawn inside it in a layer other than its pass's usual one. */
class Layer2DScope {
public:
    /** For a draw whose shape misleads the renderer's rule, so the trace counts it apart and doesn't fail it. */
    static constexpr bool kDiffersFromRule = true;

    explicit Layer2DScope(Layer2D layer, bool differs_from_rule = false);
    ~Layer2DScope();
    Layer2DScope(const Layer2DScope &) = delete;
    Layer2DScope &operator=(const Layer2DScope &) = delete;

private:
    int  previous_;
    bool previous_differs_;
};

/** Counts a draw or copy against the current pass while a trace runs. */
void CountDraw();
void CountCopy();

/** A draw's state and texture, as they stand after the trip through Noktus's material. */
struct Routed {
    gfx::DrawState      state;
    gfx::TextureBinding texture;
};

/**
 * Takes a draw through the material Noktus would be given, and back to what today's renderer takes. A draw a material can't
 * say yet goes on unchanged, and a trace counts it.
 */
Routed Route(const gfx::TextureBinding &texture, const gfx::DrawState &state);

/** While a trace runs, notes what the renderer's rules will decide about a 2D draw, from its shape and state. */
void Note2D(gfx::Primitive primitive, std::span<const gfx::Vertex2D> vertices, const gfx::TextureBinding &texture,
            const gfx::DrawState &state);

/**
 * DC_PASS_TRACE=<path>: at exit writes each shape of frame seen, with its passes in order, as JSON. `drawn` is what the tick's
 * display list says it holds, the truth the counted draws are checked against: every draw has to come through the calls below.
 */
void PassTraceStart();
void PassTraceBeginTick();
void PassTraceTick(std::uint32_t drawn);

// Drawing goes through these rather than gfx, so that each draw is counted against a pass.

inline void Draw2D(gfx::Primitive primitive, std::span<const gfx::Vertex2D> vertices, const gfx::TextureBinding &texture,
                   const gfx::DrawState &state, const gfx::Presentation2D *presentation = nullptr) {
    CountDraw();
    Note2D(primitive, vertices, texture, state);
    const Routed routed = Route(texture, state);
    gfx::Draw2D(primitive, vertices, routed.texture, routed.state, presentation);
}

inline void DrawDepthPlanes2D(const gfx::DepthPlanes2D &planes, std::span<const gfx::Vertex2D, 4> quad,
                              const gfx::TextureBinding &texture, const gfx::DrawState &state) {
    CountDraw();
    const Routed routed = Route(texture, state);
    gfx::DrawDepthPlanes2D(planes, quad, routed.texture, routed.state);
}

inline void DrawMesh(gfx::MeshHandle mesh, std::uint32_t first_index, std::uint32_t index_count,
                     const gfx::MeshConstants &constants, const gfx::TextureBinding &texture, const gfx::DrawState &state,
                     const gfx::MeshTransform *transform = nullptr) {
    CountDraw();
    const Routed routed = Route(texture, state);
    gfx::DrawMesh(mesh, first_index, index_count, constants, routed.texture, routed.state, transform);
}

inline void DrawMeshImmediate(std::span<const gfx::Vertex3D> vertices, std::span<const std::uint32_t> indices,
                              const gfx::MeshConstants &constants, const gfx::TextureBinding &texture,
                              const gfx::DrawState &state, const gfx::MeshTransform *transform = nullptr) {
    CountDraw();
    const Routed routed = Route(texture, state);
    gfx::DrawMeshImmediate(vertices, indices, constants, routed.texture, routed.state, transform);
}

inline bool CopyTexture(gfx::TextureHandle src, gfx::Rect src_rect, gfx::TextureHandle dst, std::int32_t dst_x,
                        std::int32_t dst_y) {
    CountCopy();
    return gfx::CopyTexture(src, src_rect, dst, dst_x, dst_y);
}

inline bool BlitTexture(gfx::TextureHandle src, gfx::Rect src_rect, gfx::TextureHandle dst, gfx::Rect dst_rect,
                        gfx::Filter filter) {
    CountCopy();
    return gfx::BlitTexture(src, src_rect, dst, dst_rect, filter);
}

} // namespace dc::render
