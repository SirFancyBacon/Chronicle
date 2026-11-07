#pragma once

#include "gfx/gfx.hpp"

// The renderer's types and constants as game code names them, so that game code says nothing of which renderer is behind them. The calls it makes
// are in render/renderer_calls.hpp and render/passes.hpp. tools/check_boundary.sh in Noktus Nimbus keeps game code to these.

namespace dc::render {

using gfx::AspectMode;
using gfx::CompareOp;
using gfx::CullMode;
using gfx::DepthPlanes2D;
using gfx::DepthTest;
using gfx::DisplayListRef;
using gfx::DrawState;
using gfx::Filter;
using gfx::FrameLayout;
using gfx::GsBlend;
using gfx::LogicalMapping;
using gfx::LogicalRect;
using gfx::MeshConstants;
using gfx::MeshTransform;
using gfx::Primitive;
using gfx::Rect;
using gfx::TextureBinding;
using gfx::TextureDesc;
using gfx::TextureFormat;
using gfx::TextureHandle;
using gfx::TextureInfo;
using gfx::UiAnchor;
using gfx::UiAnchorScope;
using gfx::Vertex2D;
using gfx::Vertex3D;
using gfx::Wrap;
using gfx::kMainTarget;
using gfx::kMeshScreenUv;
using gfx::kMeshShadow;
using gfx::kMeshVertexColor;
using gfx::kNullMesh;
using gfx::kNullTexture;
using gfx::kPreviousFrame;

} // namespace dc::render
