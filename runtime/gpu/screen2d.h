#pragma once
// A 4:3 game's screen space 2D (XYZRHW draws: HUD, menus, text) on a 16:9 frame, for a game that sets
// GameInfo::screen_2d. A frame with 3D in it is shown 16:9: its 2D shapes keep their 4:3 proportions, each group of
// touching shapes pulled to the side it sits on, or kept centred; shapes across the whole width still fill it. A
// frame of 2D alone (menus, movies) is shown 4:3 as it was made. Hide HUD drops the 2D of 3D frames.
#include <cstdint>

#include "gpu/state.h"
#include "gpu/vertex.h"

namespace gpu::screen2d {

// pretransformed draws need their vertices on the CPU this frame
bool Wanted();
// a draw that is not screen space: the frame has 3D in it
void Note3D();
// at each present
void EndFrame();
// the frame, or one of the last few, drew 3D: it is shown 16:9 when the output is
bool Frame3D();
// a pretransformed draw converted on the CPU, position at register slot k: re-placed for 16:9; false drops it
bool Place(const State& s, Assembled& out, int k);

// A fixed function draw through an orthographic projection (a 2D canvas the game draws with transforms, as Unreal's
// HUD): where its picture goes on a 16:9 frame
enum class Canvas { Not, Keep, Hide, Left, Centre, Right };
Canvas Classify(const State& s, const Assembled& a);
// the clip space matrix (row vectors, 16 floats) narrowed toward the canvas anchor
void Narrow(Canvas anchor, float m[16]);
// the next few frames' placing decisions into the log (F9's frame trace)
void Trace();

}  // namespace gpu::screen2d
