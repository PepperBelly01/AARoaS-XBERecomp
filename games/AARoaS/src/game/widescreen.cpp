// Widescreen and field of view. The 3D view widens instead of stretching: the camera's projection keeps its vertical
// view and takes in more at the sides, and the runtime shows the frame 16:9 (app::Widescreen). The Field of View
// setting widens or narrows the world camera, aiming zooms in from it as before; the weapon's own camera keeps its
// angle, so the rifle is not distorted.
#include "app/app.h"
#include "xdk/d3d8.h"
#include "xdk/hle.h"

namespace {

// FCameraSceneNode::UpdateMatrices (0x1F3E50, virtual: four scene node classes share it) builds the projection here:
// FOV at node +0x1EC times pi/360, then FPerspectiveMatrix(half fov x, half fov y, 1, SizeX / SizeY, near, far)
constexpr uint32_t kCameraProjection = 0x1F42F7;  // that call's return address

// Two cameras draw a frame in play, both through UpdateMatrices: first the world at the player's angle (75, less
// while aiming), then the held weapon at its own (47, less while aiming). The weapon's node class is also the menus'
// camera, so the class cannot tell them apart; the order can.
uint32_t g_frame = ~0u;    // the Swap count at the last UpdateMatrices
bool g_world = false;      // the running UpdateMatrices is the frame's first

}  // namespace

// Ambient occlusion is armed with the world camera's projection as this frame builds it, and goes in once the world
// is drawn: before the depth clear that comes ahead of the first person weapon, else (a cutscene: no weapon) as the
// frame's second camera is set up. The weapon and the HUD come after it.
X86_EXTERN_FUNC(__imp__sub_001F3E50);
X86_FUNC(sub_001F3E50) {
    uint32_t frame = d3d::Presents();
    g_world = frame != g_frame;
    g_frame = frame;
    if (!g_world && app::Get().ambient_occlusion) d3d::Occlusion();
    __imp__sub_001F3E50(ctx, base);
    g_world = false;
}

// FPerspectiveMatrix (this = the matrix, rows of 4 floats): M[0][0] = 1 / tan(half fov x),
// M[1][1] = SizeX / SizeY / tan(half fov y). thiscall, 6 arguments
X86_EXTERN_FUNC(__imp__sub_001F1AC0);
X86_FUNC(sub_001F1AC0) {
    uint32_t from = X86_MEM32(ctx.esp), matrix = ctx.ecx;
    __imp__sub_001F1AC0(ctx, base);
    if (from != kCameraProjection) return;
    // shown 16:9: the same height of view, 16:9 / 4:3 as much across
    if (app::Widescreen()) X86_MEMF32(matrix) *= 0.75f;
    if (g_world) {
        float s = app::FovScale();
        X86_MEMF32(matrix) /= s;
        X86_MEMF32(matrix + 0x14) /= s;
        if (uint32_t ao = app::Get().ambient_occlusion) {
            float m[16];
            for (uint32_t i = 0; i < 16; i++) m[i] = X86_MEMF32(matrix + i * 4);
            d3d::ArmOcclusion(m, ao == 2);
        }
    }
}
