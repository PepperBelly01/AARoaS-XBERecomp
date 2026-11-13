#pragma once
#include <mutex>

#include "gpu/state.h"

namespace d3d {

gpu::State& State();
std::recursive_mutex& Lock();
// shows a D3DDevice_UpdateOverlay surface (YUY2 video) as a whole frame
void PresentOverlay(uint32_t surface);
// the title's Swaps so far: a game hook's frame number
uint32_t Presents();
// Ambient occlusion for a game hook: armed with the world camera's projection m (16 floats, row vectors) as it is
// set up, it goes over the bound scene and its depth before the frame's next depth clear once the world has drawn
// (games clear depth before the first person weapon), or at Occlusion, whichever comes first
void ArmOcclusion(const float* m, bool gtao);
void Occlusion();
void TraceFrame();  // the next frame's draws and depth clears into the log (F9)
// a game hook told each screen space draw's box as it is drawn, in pixels of the game's frame (x0, y0, x1, y1); the
// title's thread
// with stage 0's texture (its guest D3DBaseTexture, 0 none) and size, and the span of texture coordinates used (when the
// vertex format gives them: an FVF with a TEX1 after the position and colours)
struct ScreenDraw {
    float x0, y0, x1, y1;
    uint32_t texture = 0, tex_width = 0, tex_height = 0;
    uint32_t fvf = 0, stride = 0;  // the vertex format (0: a vertex shader), stream 0's stride
    float raw[2][12] = {};  // the first two vertices' first 12 floats (for finding a vertex shader's texture coordinates)
    bool uv = false;
    float u0 = 0, v0 = 0, u1 = 0, v1 = 0;
};
using ScreenDrawFn = void (*)(const ScreenDraw&);
void OnScreenDraw(ScreenDrawFn fn);

}  // namespace d3d
