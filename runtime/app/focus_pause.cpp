// Pause on focus loss (Settings::pause_on_focus_loss): the game thread waits at its Swap while the window is in the
// background or minimized, with its sound and the guest's clocks stopped (kernel::HoldGuestTime), so nothing races
// to catch up on return. After the SCDA, Nightfire and BLACK recomps' (same author and licence as this SDK).
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "app/app.h"
#include "audio/audio.h"
#include "core/log.h"
#include "kernel/kernel.h"

namespace app {

namespace {

constexpr DWORD kPollMs = 20;

// a hidden window (behind the splash, or a handover) never pauses: it opens from the game thread
bool ShouldPause() {
    if (!Get().pause_on_focus_loss) return false;
    HWND hwnd = static_cast<HWND>(Window());
    if (!hwnd || !IsWindowVisible(hwnd)) return false;
    return GetForegroundWindow() != hwnd || IsIconic(hwnd);
}

}  // namespace

void WaitWhileUnfocused() {
    if (!ShouldPause()) return;
    LOGR("focus lost: paused");
    audio::SetPaused(true);
    kernel::HoldGuestTime(true);
    while (ShouldPause()) Sleep(kPollMs);
    kernel::HoldGuestTime(false);
    audio::SetPaused(false);
    LOGR("focus back: resumed");
}

}  // namespace app
