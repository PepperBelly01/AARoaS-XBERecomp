// Trigger squeeze for the mouse (user confirmed 2026-11-13: "as smooth as the controller, with the precision of a
// mouse"). The game reads Fire as the right trigger's pressure (0 to 255; a shot at 80, xboxuser.ini
// TriggerSensitivity=80) and a fast rise jerks the muzzle down and sideways (HumanController.UpdateTriggerVelocity):
// a slow squeeze on the pad fires steady, a mouse click, 0 to 255 in one poll, jerked every shot. Plain to see with
// Recoil System Revised's no recoil; measured, the kick, view recoil, sway and TASOffset were the same for a slow and
// a hard pull, so the jerk is the rise itself.
// So while the keyboard and mouse play, Fire squeezes: at once to just short of the shot (kRest), then up at a slow
// squeeze's rate (kSqueezeRate; the pad's slow pull measured ~100 a second, a hard one ~5000), through the shot
// about 20 ms later and on to full, held for automatic fire; let go, at once to nothing. The controller's trigger is
// the player's own.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <cstdint>

namespace {
constexpr float kRest = 76;          // just short of the shot (80)
constexpr float kSqueezeRate = 200;  // a second, from kRest: the shot about 20 ms after the click
double g_pressed_at = -1;            // seconds, -1 not held

double Seconds() {
    LARGE_INTEGER c, f;
    QueryPerformanceCounter(&c);
    QueryPerformanceFrequency(&f);
    return double(c.QuadPart) / double(f.QuadPart);
}
}  // namespace

// stance.cpp's StancePad, each poll while the keyboard and mouse play: the Fire key's full press as a squeeze;
// 0 resets it
uint8_t SqueezeTrigger(uint8_t pressed) {
    if (!pressed) {
        g_pressed_at = -1;
        return 0;
    }
    double now = Seconds();
    if (g_pressed_at < 0) g_pressed_at = now;
    float value = kRest + float(now - g_pressed_at) * kSqueezeRate;
    return uint8_t(std::min(value, float(pressed)));
}
