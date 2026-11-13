// Weapon recoil and sway as the game has them at its own 30 fps, whatever the frame rate, turned by us.
//
// How the game does it: the pawn's recoil update (0x928F0, from the pawn's tick at 0x9DA10 with the frame's delta
// time) steps a spring once a call with no regard for the time between calls: rRecoil (pawn +0x8E4) shrinks and
// vRecoil (+0x8F0, pitch and yaw) gains it. The sway's update (0x92850) sets vWaver (+0x908) from the time. Then the
// player input (0x33050) takes last frame's recoil and sway back out of the look axes (the controller's +0x4F8 pitch,
// +0x504 turn, remembered at +0x9FC, +0xA00) and 0x22390 puts this frame's in. So what turns the view each frame is
// the change in vRecoil + vWaver since the last frame, times the frame's delta time, as the stick's turn is: at 30 fps
// the view sits kGain units of Rotation per unit of them off the aim, kicked up and settling back.
//
// Faster than 30 fps the spring steps more often and never settles, and each change is multiplied by a shorter frame;
// scaled back up, the axes ran into the input's stick acceleration (0x33201) at random. So the game's way is left out:
//  - the spring steps on a fixed 30 Hz clock with a delta time of 1/30 (zero, one or more steps a frame);
//  - the look axes get none of it (0x22390 sees zeros); the sway (its update 0x92850 is a spring
//    too, stepped the same way and eased) still goes through them: its change each frame, scaled by 1/30 over the
//    frame's time (turned directly it fed back into itself and the view shook);
//  - each frame the view is turned to sit kGain x vRecoil off the aim, vRecoil eased from one step's value
//    to the next over the 1/30 between them: the 30 fps kick, smooth at any frame rate. That is done after
//    the tick, past Aim Assist off's undoing of the lock (mouse.cpp), so nothing filters or boosts it.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <unordered_map>

#include "app/app.h"
#include "core/log.h"
#include "xdk/hle.h"

namespace {
constexpr float kStep = 1.0f / 30.0f;
constexpr int kMaxSteps = 4;            // after a hitch, no burst of catching up
constexpr float kGain = 1.2f;  // Rotation units per unit of vRecoil at 30 fps (measured: 36 x delta time)
constexpr uint32_t kVRecoil = 0x8F0, kVWaver = 0x908;  // pawn: pitch, yaw floats each
constexpr uint32_t kPawn = 0x300;                       // the controller's Pawn
constexpr uint32_t kAxisPitch = 0x4F8, kAxisYaw = 0x504;

struct Spring {
    float owed = 0;                  // time not yet stepped
    float from[2] = {}, to[2] = {};  // vRecoil before and after the last step
};
std::unordered_map<uint32_t, Spring> g_springs;  // by pawn
std::unordered_map<uint32_t, Spring> g_wavers;   // the sway (vWaver) the same way, by pawn
const bool g_ours = true;  // the recoil turned here

uint32_t Bits(float f) {
    uint32_t u;
    std::memcpy(&u, &f, 4);
    return u;
}
float Float(uint32_t u) {
    float f;
    std::memcpy(&f, &u, 4);
    return f;
}
double Seconds() {
    LARGE_INTEGER c, f;
    QueryPerformanceCounter(&c);
    QueryPerformanceFrequency(&f);
    return double(c.QuadPart) / double(f.QuadPart);
}

// vRecoil as shown now: eased between the last two steps
void Shown(const Spring& s, float out[2]) {
    float t = std::clamp(s.owed / kStep, 0.0f, 1.0f);
    for (int i = 0; i < 2; i++) out[i] = s.from[i] + (s.to[i] - s.from[i]) * t;
}
}  // namespace

// APawn's recoil update (thiscall, delta time; ret 4)
X86_EXTERN_FUNC(__imp__sub_000928F0);
X86_FUNC(sub_000928F0) {
    uint32_t self = ctx.ecx, sp = ctx.esp;
    uint32_t pawn = X86_MEM32(self + 4);
    float dt = Float(X86_MEM32(sp + 4));
    if (g_springs.size() > 256) g_springs.clear();  // pawns come and go with the levels
    Spring& s = g_springs[pawn];
    s.owed += dt > 0 && dt < 1 ? dt : 0;
    int steps = 0;
    while (s.owed >= kStep && steps < kMaxSteps) {
        s.owed -= kStep;
        steps++;
    }
    if (steps == kMaxSteps) s.owed = 0;
    if (steps == 0) {
        ctx.esp = sp + 8;  // the return address and the argument, as the routine's ret 4
        return;
    }
    for (int i = 0; i < steps; i++) {
        s.from[0] = X86_MEMF32(pawn + kVRecoil);
        s.from[1] = X86_MEMF32(pawn + kVRecoil + 4);
        ctx.esp = sp;
        ctx.ecx = self;
        X86_MEM32(sp + 4) = Bits(kStep);
        __imp__sub_000928F0(ctx, base);
    }
    s.to[0] = X86_MEMF32(pawn + kVRecoil);
    s.to[1] = X86_MEMF32(pawn + kVRecoil + 4);
}


// APawn's sway update (thiscall, delta time; ret 4): vWaver from the weapon's sway, which steps once a call as the
// recoil does (at 200 fps it swung six times as fast). Stepped the same way, on the 30 Hz clock with 1/30
X86_EXTERN_FUNC(__imp__sub_00092850);
X86_FUNC(sub_00092850) {
    uint32_t self = ctx.ecx, sp = ctx.esp;
    uint32_t pawn = X86_MEM32(self + 4);
    float dt = Float(X86_MEM32(sp + 4));
    if (g_wavers.size() > 256) g_wavers.clear();
    Spring& s = g_wavers[pawn];
    s.owed += dt > 0 && dt < 1 ? dt : 0;
    int steps = 0;
    while (s.owed >= kStep && steps < kMaxSteps) {
        s.owed -= kStep;
        steps++;
    }
    if (steps == kMaxSteps) s.owed = 0;
    if (steps == 0) {
        ctx.esp = sp + 8;
        return;
    }
    for (int i = 0; i < steps; i++) {
        s.from[0] = X86_MEMF32(pawn + kVWaver);
        s.from[1] = X86_MEMF32(pawn + kVWaver + 4);
        ctx.esp = sp;
        ctx.ecx = self;
        X86_MEM32(sp + 4) = Bits(kStep);
        __imp__sub_00092850(ctx, base);
    }
    s.to[0] = X86_MEMF32(pawn + kVWaver);
    s.to[1] = X86_MEMF32(pawn + kVWaver + 4);
}
namespace {
float g_input_dt = kStep;  // the player input's delta time this frame (0x33050)

// the sway as the look axes are given it: what was added last frame, and the sway it stood for
struct Sway {
    uint32_t pawn = 0;
    bool added = false;  // last frame added it (0x22390 adds the sway only while it applies, aiming)
    float given[2] = {}, from[2] = {};
} g_sway_given;
}  // namespace

// the recoil and sway into the look axes (thiscall, no arguments; the owner's +0x18 is the controller). The input
// resets the axes each frame and takes back what was added last frame, so the view turns by the change, times the
// frame's time. The recoil: none (turned directly, RecoilTurn). The sway: its change since last frame, scaled by
// 1/30 over the frame's time, added to what was given last frame, so it turns the view as far as at 30 fps. Scaling
// the whole value instead put the scale's own frame-to-frame jitter into the change: the view shook.
X86_EXTERN_FUNC(__imp__sub_00022390);
X86_FUNC(sub_00022390) {
    uint32_t owner = X86_MEM32(ctx.ecx + 4);
    uint32_t controller = owner ? X86_MEM32(owner + 0x18) : 0;
    uint32_t pawn = controller ? X86_MEM32(controller + kPawn) : 0;
    auto it = g_springs.find(pawn);  // only a pawn the recoil update has run for is touched
    if (!pawn || it == g_springs.end()) {
        __imp__sub_00022390(ctx, base);
        return;
    }
    float scale = g_input_dt > 0 ? std::clamp(kStep / g_input_dt, 0.25f, 10.0f) : 1.0f;
    Sway& s = g_sway_given;
    if (s.pawn != pawn) s = Sway{pawn};
    float keep[4], give[2];
    for (int i = 0; i < 2; i++) {
        keep[i] = X86_MEMF32(pawn + kVRecoil + 4 * i);
        keep[2 + i] = X86_MEMF32(pawn + kVWaver + 4 * i);
    }
    float sway[2] = {keep[2], keep[3]};  // eased between its 30 Hz steps
    if (auto w = g_wavers.find(pawn); w != g_wavers.end()) Shown(w->second, sway);
    for (int i = 0; i < 2; i++) {
        // not added last frame: the game's own jump in, as at 30 fps
        give[i] = s.added ? s.given[i] + (sway[i] - s.from[i]) * scale : sway[i] * scale;
        X86_MEMF32(pawn + kVRecoil + 4 * i) = 0;
        X86_MEMF32(pawn + kVWaver + 4 * i) = give[i];
    }
    float pitch = X86_MEMF32(controller + kAxisPitch);
    float kept_pitch = X86_MEMF32(controller + 0x9FC);
    __imp__sub_00022390(ctx, base);
    float added_pitch = X86_MEMF32(controller + kAxisPitch) - pitch;
    // added: the remembered value moved (or the sway is exactly zero, as good as added)
    s.added = X86_MEMF32(controller + 0x9FC) != kept_pitch || added_pitch != 0 || give[0] == 0;
    for (int i = 0; i < 2; i++) {
        s.given[i] = give[i];
        s.from[i] = sway[i];
        X86_MEMF32(pawn + kVRecoil + 4 * i) = keep[i];
        X86_MEMF32(pawn + kVWaver + 4 * i) = keep[2 + i];
    }
}

// After the tick (mouse.cpp's AimAfterTick): how far to turn the view this frame, Rotation units, so it sits the
// recoil and sway off the aim. The controller is the one whose Rotation the view is.
bool RecoilTurn(uint8_t* base, uint32_t controller, int32_t& pitch, int32_t& yaw) {
    pitch = yaw = 0;
    static uint32_t seen = 0;
    static int32_t shown_pitch = 0, shown_yaw = 0;  // the offset in the view now
    uint32_t pawn = controller ? X86_MEM32(controller + kPawn) : 0;
    auto it = g_springs.find(pawn);
    if (!g_ours || it == g_springs.end()) {
        seen = 0;
        return false;
    }
    float v[2];
    Shown(it->second, v);
    int32_t want_pitch = int32_t(std::lround(kGain * v[0]));
    int32_t want_yaw = int32_t(std::lround(kGain * v[1]));
    if (seen != pawn) {  // a new pawn: what it has now is the start, no jump
        seen = pawn;
        shown_pitch = want_pitch;
        shown_yaw = want_yaw;
        LOGR("recoil: ours, pawn %08X", pawn);
    }
    pitch = want_pitch - shown_pitch;
    yaw = want_yaw - shown_yaw;
    shown_pitch = want_pitch;
    shown_yaw = want_yaw;
    return pitch || yaw;
}

// ours in use: the game's kick is not in the tick's turn
bool RecoilOurs() { return g_ours; }

// the player input (0x33050, thiscall, delta time; ret 4): its delta time, the frame's the axes turn the view by
X86_EXTERN_FUNC(__imp__sub_00033050);
X86_FUNC(sub_00033050) {
    float dt = Float(X86_MEM32(ctx.esp + 4));
    if (dt > 0 && dt < 0.25f) g_input_dt = dt;
    __imp__sub_00033050(ctx, base);
}

// --- Aim Assist on: the lock (AACPlayerInput's, 0x32DB0, thiscall, delta time; ret 4) picks a target and pulls the
// look axes toward it, once a call with no regard for the time between calls, so at 200 fps it pulled six times as
// often and the view slid away. Here it runs on the 30 Hz clock with 1/30, and what it put into the axes (the axes
// are reset each frame and turn the view by their value times the frame's time) is given again every frame until
// the next step: over a 1/30 the same pull as at 30 fps, smooth. Aim Assist off: as it is (its turn skipped,
// aim_assist.cpp).
namespace {
struct Lock {
    uint32_t controller = 0;
    float owed = 0;
    float pull[2] = {};  // what the last step put into the axes
} g_lock;
}  // namespace

X86_EXTERN_FUNC(__imp__sub_00032DB0);
X86_FUNC(sub_00032DB0) {
    uint32_t self = ctx.ecx, sp = ctx.esp;
    uint32_t owner = X86_MEM32(self + 4);
    uint32_t controller = owner ? X86_MEM32(owner + 0x18) : 0;
    if (!controller || !app::Get().aim_assist) {
        __imp__sub_00032DB0(ctx, base);
        return;
    }
    Lock& l = g_lock;
    if (l.controller != controller) l = Lock{controller, kStep};  // the first call steps at once
    float dt = Float(X86_MEM32(sp + 4));
    l.owed += dt > 0 && dt < 1 ? dt : 0;
    if (l.owed < kStep) {
        X86_MEMF32(controller + kAxisPitch) = X86_MEMF32(controller + kAxisPitch) + l.pull[0];
        X86_MEMF32(controller + kAxisYaw) = X86_MEMF32(controller + kAxisYaw) + l.pull[1];
        ctx.esp = sp + 8;  // the return address and the argument, as the routine's ret 4
        return;
    }
    l.owed = std::min(l.owed - kStep, kStep);  // after a hitch, no catching up
    // the lock aims from the view plus the axes, and the axes hold last frame's sway taken back out: as given
    // (scaled up, 0x22390 above) it threw the lock's aim off by that much. It sees the sway as 30 fps gives it
    float axis[2] = {X86_MEMF32(controller + kAxisPitch), X86_MEMF32(controller + kAxisYaw)}, seen[2];
    const Sway& s = g_sway_given;
    for (int i = 0; i < 2; i++) seen[i] = s.added ? axis[i] + s.given[i] - s.from[i] : axis[i];
    X86_MEMF32(controller + kAxisPitch) = seen[0];
    X86_MEMF32(controller + kAxisYaw) = seen[1];
    X86_MEM32(sp + 4) = Bits(kStep);
    __imp__sub_00032DB0(ctx, base);
    for (int i = 0; i < 2; i++) {
        uint32_t at = i ? kAxisYaw : kAxisPitch;
        l.pull[i] = X86_MEMF32(controller + at) - seen[i];
        X86_MEMF32(controller + at) = axis[i] + l.pull[i];
    }
}
