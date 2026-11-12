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

}  // namespace d3d
