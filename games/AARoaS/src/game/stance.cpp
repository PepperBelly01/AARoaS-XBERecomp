// Stance on the keyboard, as a PC game has it: crouch and prone while moving, double tap down for prone. The
// controller is left as the Xbox had it.
//
// What the game does (measured with the controller): the stance asked for (D-pad up and down, C and X here) is kept,
// but the left stick pushed past ~87% (28500) is running, which stands the pawn up while it lasts; below that it
// crouch walks (60, against 220 running). Crouch asked for while running is tried for a moment and given up. On the
// keyboard the movement keys push the stick all the way, so a crouch never held. So here, with the keyboard in use:
//  - crouched (the pawn's bIsCrouched), or a crouch just asked for, the movement keys push the stick to kCrouchReach,
//    a crouch walk, so the crouch is made and kept;
//  - stance up lets the stick go full again: standing, walking at full speed;
//  - stance down twice within kDoubleTap, or down while crouched, asks for prone: movement held at nothing for a
//    moment (the game goes prone standing still), then on (a full stick crawls; prone is bit 0x200 of the flags);
//  - up twice from prone: the second waits for the crouch and kRiseTime after the first (the game cut straight to standing).
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

#include "app/app.h"
#include "core/input.h"
#include "core/log.h"
#include "xdk/hle.h"

uint32_t FindProperty(uint8_t* base, uint32_t controller, uint32_t object, const char* name);  // aim_assist.cpp
uint32_t PropertyOffset(uint8_t* base, uint32_t property);

namespace {
constexpr float kCrouchReach = 0.70f;      // of the stick: a crouch walk, well under running
constexpr ULONGLONG kDoubleTap = 400;      // ms between the downs of a double tap
constexpr ULONGLONG kCrouchHold = 500;     // ms the stick stays at a crouch walk for a crouch asked for
constexpr ULONGLONG kProneHold = 700;      // ms movement is held at nothing for prone
constexpr uint32_t kCrouchedBit = 0x40;    // bIsCrouched in its dword (seen in play: 0x20 asked, 0x04 slow or still)
constexpr uint32_t kProneBit = 0x200;      // in the same dword, while prone (seen in play)
constexpr ULONGLONG kRiseTime = 1150;      // ms from the first up to the queued stand up (the user's own timing:
                                           // the flags say crouched at once, the animation plays after)
constexpr ULONGLONG kRiseGiveUp = 3000;    // ms to wait for the crouch at most
constexpr ULONGLONG kUpPulse = 120;        // ms the queued stance up is held
constexpr uint16_t kDpadUp = 1u << 0, kDpadDown = 1u << 1;

// the pawn as the last frame found it (UpdateInput hook); the pad poll reads it
uint8_t* g_base = nullptr;
uint32_t g_pawn = 0, g_crouched_at = 0, g_prone_at = 0;

uint16_t g_last_buttons = 0;
ULONGLONG g_last_down = 0, g_crouch_until = 0, g_prone_until = 0;
uint32_t g_prone_seen = 0;  // the prone dword when prone was asked for
// getting up from prone: the first up passes, a second one waits for the crouch (else the game went straight up)
bool g_rising = false, g_stand_queued = false;
ULONGLONG g_rise_start = 0, g_crouch_reached = 0, g_up_until = 0;

uint32_t Flags() {
    uint8_t* base = g_base;
    return base && g_pawn && g_crouched_at ? X86_MEM32(g_pawn + g_crouched_at) : 0;
}
bool Prone() { return (Flags() & kProneBit) != 0; }
bool Crouched() {
    uint8_t* base = g_base;
    return base && g_pawn && g_crouched_at && (X86_MEM32(g_pawn + g_crouched_at) & kCrouchedBit);
}
uint32_t ProneWord() {
    uint8_t* base = g_base;
    return base && g_pawn && g_prone_at ? X86_MEM32(g_pawn + g_prone_at) : 0;
}

// the keyboard's left stick no further than reach of its length
void Limit(input::Pad& pad, float reach) {
    float len = std::sqrt(float(pad.lx) * pad.lx + float(pad.ly) * pad.ly), most = 32767.0f * reach;
    if (len <= most || len == 0) return;
    pad.lx = int16_t(pad.lx * most / len);
    pad.ly = int16_t(pad.ly * most / len);
}
}  // namespace

// prone, stance up twice: prone to crouch, a pause, then crouch to stand, each with its animation
static void Rise(input::Pad& pad, uint16_t pressed, ULONGLONG now) {
    if (pressed & kDpadUp) {
        if (g_rising) {
            g_stand_queued = true;  // the second: held back
        } else if (Prone()) {
            g_rising = true;  // the first: the game gets up to a crouch
            g_rise_start = now;
            g_crouch_reached = 0;
        }
    }
    if (g_rising) {
        bool crouched = !Prone() && (Flags() & kCrouchedBit);
        if (crouched && !g_crouch_reached) g_crouch_reached = now;
        if (g_stand_queued && g_crouch_reached && now >= g_rise_start + kRiseTime) {
            g_up_until = now + kUpPulse;  // the stand up sent now
            g_rising = g_stand_queued = false;
            LOGR("stance: up from prone, standing %llu ms after the first up", now - g_rise_start);
        } else if ((g_crouch_reached && !g_stand_queued && now >= g_rise_start + kRiseTime) ||
                   now >= g_rise_start + kRiseGiveUp) {
            g_rising = g_stand_queued = false;  // stayed crouched, or the crouch never came
        }
        if (g_stand_queued) pad.buttons &= ~kDpadUp;
    }
    if (now < g_up_until) pad.buttons |= kDpadUp;
}

// GameInfo::pad: every poll, after the keys are merged in
void StancePad(int port, input::Pad& pad) {
    if (port != 0) return;
    if (!input::UsingPad() && g_pawn) {
        ULONGLONG now = GetTickCount64();
        uint16_t pressed = pad.buttons & ~g_last_buttons;
        if (pressed & kDpadDown) {
            bool second = now - g_last_down < kDoubleTap;
            g_last_down = now;
            if (second || Crouched()) {
                g_prone_until = now + kProneHold;
                g_prone_seen = ProneWord();
                LOGR("stance: prone asked for (%s)", second ? "double tap" : "crouched");
            }
            g_crouch_until = now + kCrouchHold;
        }
        if (pressed & kDpadUp) g_crouch_until = g_prone_until = 0;
        g_last_buttons = pad.buttons;
        Rise(pad, pressed, now);
        // prone: held still until the game has it or a moment passed
        if (now < g_prone_until && ProneWord() != g_prone_seen) {
            LOGR("stance: prone word %08X -> %08X", g_prone_seen, ProneWord());
            g_prone_until = 0;
        }
        if (now < g_prone_until) {
            pad.lx = pad.ly = 0;
        } else if (Crouched() || now < g_crouch_until) {
            Limit(pad, kCrouchReach);
        }
    } else {
        g_last_buttons = pad.buttons;
    }
}

// each frame (mouse.cpp's UpdateInput hook): the pawn and where its flags are
void StanceFrame(uint8_t* base, uint32_t viewport) {
    uint32_t controller = X86_MEM32(viewport + 0x34);
    static uint32_t seen = 0, pawn_p = 0;
    if (controller != seen) {
        seen = controller;
        pawn_p = FindProperty(base, controller, controller, "Pawn");
    }
    uint32_t pawn = pawn_p ? X86_MEM32(controller + PropertyOffset(base, pawn_p)) : 0;
    static uint32_t seen_pawn = 0;
    if (pawn != seen_pawn) {
        seen_pawn = pawn;
        g_crouched_at = pawn ? PropertyOffset(base, FindProperty(base, controller, pawn, "bIsCrouched")) : 0;
        g_prone_at = pawn ? PropertyOffset(base, FindProperty(base, controller, pawn, "bIsProne")) : 0;
        if (pawn) LOGR("stance: pawn %08X, bIsCrouched +0x%X, bIsProne +0x%X", pawn, g_crouched_at, g_prone_at);
    }
    g_base = base;
    g_pawn = pawn;
}
