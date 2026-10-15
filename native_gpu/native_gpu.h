// C ABI for the host render path. Integrators provide explicit geometry,
// camera and material data; v3 characters also provide quaternion palettes.
// This is NOT the rex_gpu_create ABI: it does not consume Xenos commands.
#pragma once
#include <stdint.h>
#ifdef _WIN32
#ifdef DBZ_NATIVE_GPU_BUILD
#define DBZ_NATIVE_API __declspec(dllexport)
#else
#define DBZ_NATIVE_API __declspec(dllimport)
#endif
#else
#define DBZ_NATIVE_API
#endif
#ifdef __cplusplus
extern "C" {
#endif
typedef struct DbzNativeVertex { float position[3], normal[3], uv[2]; } DbzNativeVertex;
typedef struct DbzNativeFrameVertex {
    float position[3], normal[3], uv[2], color[4], fog;
} DbzNativeFrameVertex;
typedef void* DbzNativeRenderer;
typedef enum DbzNativeTextureFormat {
    DBZ_NATIVE_RGBA8=0, DBZ_NATIVE_BC1=1, DBZ_NATIVE_BC2=2, DBZ_NATIVE_BC3=3,
    DBZ_NATIVE_R8=4, DBZ_NATIVE_RG8=5, DBZ_NATIVE_R32_FLOAT=6
} DbzNativeTextureFormat;
typedef enum DbzNativeMaterialMode {
    DBZ_NATIVE_TEXTURED_COLOR=1, DBZ_NATIVE_SHADOW=2, DBZ_NATIVE_FLAT_COLOR=3,
    DBZ_NATIVE_SKIN_TOON=4, DBZ_NATIVE_WORLD_TOON=5, DBZ_NATIVE_RIGID_TOON=6,
    DBZ_NATIVE_SKIN_OUTLINE=7, DBZ_NATIVE_RIGID_OUTLINE=8,
    DBZ_NATIVE_SKIN_COLOR=10, DBZ_NATIVE_SKIN_MASK=11, DBZ_NATIVE_RIGID_MASK=12,
    DBZ_NATIVE_VIDEO_601=13, DBZ_NATIVE_FXAA=14, DBZ_NATIVE_IMAGE_COPY=15,
    DBZ_NATIVE_GAUSSIAN_13=16, DBZ_NATIVE_DEPTH_OF_FIELD_9=17,
    DBZ_NATIVE_RESAMPLE_DEPTH_STENCIL=18, DBZ_NATIVE_SOLID_COLOR_FOG=19
} DbzNativeMaterialMode;
enum { DBZ_NATIVE_ALPHA_DEST_ZERO=1u<<12, DBZ_NATIVE_ALPHA_DEST_INVERSE=1u<<13,
    DBZ_NATIVE_TARGET_MASK=1u<<20, DBZ_NATIVE_SAMPLE_MASK=1u<<21,
    DBZ_NATIVE_INPUT_CLIP=1u<<22, DBZ_NATIVE_TOON_CAMERA_W=1u<<23,
    DBZ_NATIVE_SAMPLE_SCENE=1u<<24, DBZ_NATIVE_LINE_LIST=1u<<25 };
// Native compare/op codes are D3D12's enum values minus one. Face order:
// compare, stencil-fail, depth-fail, pass. Masks and reference are 8-bit.
typedef struct DbzNativeStencil {
    uint8_t enabled, reference, read_mask, write_mask;
    uint8_t front[4], back[4];
} DbzNativeStencil;
typedef struct DbzNativeAlphaTest {
    uint8_t enabled, function;
    uint16_t reserved;
    float reference;
} DbzNativeAlphaTest;
typedef struct DbzNativeRaster {
    DbzNativeStencil stencil;
    DbzNativeAlphaTest alpha;
    float viewport[6]; // x, y, width, height, minimum/maximum depth
    int32_t scissor[4]; // left, top, right, bottom
} DbzNativeRaster;
typedef struct DbzNativeClear {
    float color[4], depth;
    uint32_t stencil, flags; // flags: 1=color, 2=depth, 4=stencil
} DbzNativeClear;
typedef enum DbzNativeOperationType {
    DBZ_NATIVE_COPY_COLOR=1, DBZ_NATIVE_COPY_DEPTH=2, DBZ_NATIVE_CLEAR_TARGET=3,
    DBZ_NATIVE_RESTORE_DEPTH=4
} DbzNativeOperationType;
typedef struct DbzNativeFrameOperation {
    uint32_t before_draw, type, source_target, destination_texture;
    DbzNativeClear clear; // used only by CLEAR_TARGET
    // RESTORE_DEPTH copies destination_texture's saved depth+stencil back to
    // source_target. The snapshot must have been copied in a completed frame
    // or earlier in this frame's ordered operation list.
} DbzNativeFrameOperation;
// Frame-operation source ID for the separate native mask color/depth resources.
#define DBZ_NATIVE_MASK_SURFACE UINT32_C(0xffffffff)
typedef struct DbzNativeStats {
    uint32_t draw_count, texture_uploads, pipeline_count, owned_objects;
    uint64_t texture_bytes_uploaded, submitted_frames;
} DbzNativeStats;
// A streaming frame supplies native geometry and resource IDs directly.
// Texture IDs come from add_texture; UINT32_MAX selects a fallback texture.
// The caller retains input arrays only until set_frame returns.
typedef struct DbzNativeFrameDraw {
    uint32_t first_index, index_count, base_texture, ramp_texture;
    float diffuse[3]; uint32_t flags;
    // TOON_CAMERA_W: material[19] supplies the camera's homogeneous coordinate.
    float view_projection[16], material[20];
    DbzNativeRaster raster;
    const float* palette; uint32_t palette_rows, reserved;
    // reserved selects a native render target: 0=main, IDs returned below.
    // RESAMPLE_DEPTH_STENCIL requires a full destination quad with UV 0..1,
    // point sampling, no color/stencil writes in flags, and depth ALWAYS/write.
    // Both texture IDs name the same earlier native depth snapshot. The GPU
    // preserves all eight stencil bits without optional pixel stencil export.
} DbzNativeFrameDraw;
// hwnd may be NULL for offscreen rendering / integration tests.
DBZ_NATIVE_API DbzNativeRenderer dbz_native_create(void* hwnd, const char* scene, const char* shaders);
DBZ_NATIVE_API DbzNativeRenderer dbz_native_create_stream(void* hwnd, uint32_t width,
    uint32_t height, const char* shaders);
DBZ_NATIVE_API int dbz_native_add_texture(DbzNativeRenderer renderer, uint32_t width,
    uint32_t height, const uint8_t* rgba, uint32_t byte_count, uint32_t* texture_id);
// Linear rows of native RGBA8 pixels or BC blocks. Swizzle uses four 3-bit
// selectors: R/G/B/A=0..3, constant zero/one=4/5. Identity is 0x688.
DBZ_NATIVE_API int dbz_native_add_texture_blocks(DbzNativeRenderer renderer,
    uint32_t format, uint32_t width, uint32_t height, const uint8_t* data,
    uint32_t byte_count, uint32_t swizzle, uint32_t* texture_id);
DBZ_NATIVE_API int dbz_native_update_texture_blocks(DbzNativeRenderer renderer,
    uint32_t texture_id, uint32_t format, uint32_t width, uint32_t height,
    const uint8_t* data, uint32_t byte_count, uint32_t swizzle);
// Bounded RGBA8/D24S8 offscreen target. Its color has an ordinary texture ID.
// Every render starts with this clear. Switches preserve color/depth/stencil.
// A frame must produce the target before sampling it; feedback is rejected.
// CPU texture updates cannot replace native target resources.
DBZ_NATIVE_API int dbz_native_add_render_target(DbzNativeRenderer renderer,
    uint32_t width, uint32_t height, const DbzNativeClear* clear,
    uint32_t* target_id, uint32_t* texture_id);
// GPU-only immutable-size storage for ordered color/depth snapshots. type is
// COPY_COLOR or COPY_DEPTH. Depth samples expose normalized 24-bit depth in R.
DBZ_NATIVE_API int dbz_native_add_snapshot_texture(DbzNativeRenderer renderer,
    uint32_t type, uint32_t width, uint32_t height, uint32_t* texture_id);
DBZ_NATIVE_API int dbz_native_set_frame(DbzNativeRenderer renderer,
    const DbzNativeFrameVertex* vertices, uint32_t vertex_count,
    const uint32_t* indices, uint32_t index_count,
    const DbzNativeFrameDraw* draws, uint32_t draw_count, const DbzNativeClear* clear);
// Call after set_frame, which resets this list. Operations run in supplied
// order before the indexed draw, including before_draw==draw_count. Source
// target 0 is main, MASK_SURFACE is the mask; other IDs come from add_render_target.
// A color snapshot may crop a larger source from (0,0); depth sizes must match.
// Snapshot reads require a copy in an earlier completed frame or earlier in
// this frame. Storage survives frame clears. No guest command stream is accepted.
DBZ_NATIVE_API int dbz_native_set_frame_operations(DbzNativeRenderer renderer,
    const DbzNativeFrameOperation* operations, uint32_t count);
DBZ_NATIVE_API int dbz_native_update_vertices(DbzNativeRenderer renderer,
    const DbzNativeVertex* vertices, uint32_t count);
// Matrix is 16 row-major floats; maps world coordinates directly to D3D clip space.
DBZ_NATIVE_API int dbz_native_set_camera(DbzNativeRenderer renderer,
    const float* view_projection, const float* eye_xyz);
// Version-2 frame geometry includes the game's CPU vertex colors and fog.
DBZ_NATIVE_API int dbz_native_update_frame_vertices(DbzNativeRenderer renderer,
    const DbzNativeFrameVertex* vertices, uint32_t count);
// Update a version-2 draw without changing its topology, textures, or PSO.
// material is 20 floats: control, fog parameters, tint, blend color, fog color.
DBZ_NATIVE_API int dbz_native_update_draw(DbzNativeRenderer renderer, uint32_t draw,
    const float* view_projection, const float* material);
// Version-3 character: 388 float4 rows for skin (4 model + 128 * 3 joints),
// or 4 model rows for a rigid character draw.
// Count is in float4 rows; the draw's allocation and topology remain fixed.
DBZ_NATIVE_API int dbz_native_update_palette(DbzNativeRenderer renderer, uint32_t draw,
    const float* rows, uint32_t count);
// Viewport, scissor and stencil reference may vary. Stencil operations/masks
// belong to the immutable pipeline established by the scene.
DBZ_NATIVE_API int dbz_native_update_raster(DbzNativeRenderer renderer, uint32_t draw,
    const DbzNativeRaster* raster);
DBZ_NATIVE_API int dbz_native_set_clear(DbzNativeRenderer renderer, const DbzNativeClear* clear);
// Visibility for a version-2 draw; hidden draws submit no GPU work.
DBZ_NATIVE_API int dbz_native_set_draw_visible(DbzNativeRenderer renderer,
    uint32_t draw, int visible);
DBZ_NATIVE_API int dbz_native_render(DbzNativeRenderer renderer);
// 0 for caller-paced presentation, 1 for DXGI sync pacing (default).
DBZ_NATIVE_API int dbz_native_set_present_interval(DbzNativeRenderer renderer, uint32_t interval);
DBZ_NATIVE_API int dbz_native_capture(DbzNativeRenderer renderer, const char* ppm_path);
DBZ_NATIVE_API int dbz_native_get_stats(DbzNativeRenderer renderer, DbzNativeStats* stats);
DBZ_NATIVE_API void dbz_native_destroy(DbzNativeRenderer renderer);
// Thread-local message; copy it before making another ABI call on this thread.
DBZ_NATIVE_API const char* dbz_native_last_error(void);
#ifdef __cplusplus
}
#endif
