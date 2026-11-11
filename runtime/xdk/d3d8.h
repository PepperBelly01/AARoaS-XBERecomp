#pragma once
#include <mutex>

#include "gpu/state.h"

namespace d3d {

gpu::State& State();
std::recursive_mutex& Lock();
// shows a D3DDevice_UpdateOverlay surface (YUY2 video) as a whole frame
void PresentOverlay(uint32_t surface);

}  // namespace d3d
