// Kazarov's renderer defines these in gfx, and so does render/gfx_host.cpp, which decides who answers. Force-included into Kazarov's renderer
// sources, and included by gfx_host.cpp around gfx/gfx.hpp, this gives its versions other names, so that both can be in one program without
// changing Kazarov. Not guarded: render/kazarov_names_end.hpp takes them away again, and the two lists have to match.
// When moving the Kazarov pin: gfx_host.cpp can't see the plain declarations, so a changed return type won't be caught by the compiler. And an inline
// function in gfx.hpp that calls one of these, like UiAnchorScope, must stay out of Kazarov's renderer sources, or its body would differ from the game's.

#define BeginFrame KazarovBeginFrame
#define BeginRecording KazarovBeginRecording
#define CanCopyMainDepth KazarovCanCopyMainDepth
#define ConvertPs2Alpha KazarovConvertPs2Alpha
#define CopyMainDepthToTarget KazarovCopyMainDepthToTarget
#define CreateMesh KazarovCreateMesh
#define CreatePalette KazarovCreatePalette
#define CreateRenderTarget KazarovCreateRenderTarget
#define CreateTexture KazarovCreateTexture
#define CurrentFrameLayout KazarovCurrentFrameLayout
#define CurrentInterpKey KazarovCurrentInterpKey
#define CurrentNoInterpolation KazarovCurrentNoInterpolation
#define CurrentRenderTarget KazarovCurrentRenderTarget
#define CurrentUiAnchor KazarovCurrentUiAnchor
#define CutCameraInterpolation KazarovCutCameraInterpolation
#define CutInterpolation KazarovCutInterpolation
#define DepthPlanesAvailable KazarovDepthPlanesAvailable
#define DestroyMesh KazarovDestroyMesh
#define DestroyTexture KazarovDestroyTexture
#define Draw2D KazarovDraw2D
#define EndFrame KazarovEndFrame
#define EndRecording KazarovEndRecording
#define FindNamedRenderTarget KazarovFindNamedRenderTarget
#define GetLogicalMapping KazarovGetLogicalMapping
#define GetTextureInfo KazarovGetTextureInfo
#define GetUiMapping KazarovGetUiMapping
#define HeadlessSurfaceAvailable KazarovHeadlessSurfaceAvailable
#define IdentityMeshTransform KazarovIdentityMeshTransform
#define InFrame KazarovInFrame
#define InvertAffineTransform KazarovInvertAffineTransform
#define LogicalFrame KazarovLogicalFrame
#define LogicalHeight KazarovLogicalHeight
#define LogicalWidth KazarovLogicalWidth
#define NamedRenderTarget KazarovNamedRenderTarget
#define PipelineCount KazarovPipelineCount
#define Recording KazarovRecording
#define RenderScale KazarovRenderScale
#define RendererInit KazarovRendererInit
#define RendererResize KazarovRendererResize
#define RendererShutdown KazarovRendererShutdown
#define SetFrameLayout KazarovSetFrameLayout
#define SetInterpKey KazarovSetInterpKey
#define SetRenderTarget KazarovSetRenderTarget
#define SetUiAnchor KazarovSetUiAnchor
#define UpdateMeshVertices KazarovUpdateMeshVertices
#define UpdatePalette KazarovUpdatePalette
#define UpdateTexture KazarovUpdateTexture
#define UpdateTextureNow KazarovUpdateTextureNow
#define VisibleLogicalRect KazarovVisibleLogicalRect
#define WritePng KazarovWritePng
