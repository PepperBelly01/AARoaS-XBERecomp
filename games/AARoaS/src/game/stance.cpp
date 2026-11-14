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

// the keyboard's left stick all the way over, its direction kept
void Full(input::Pad& pad) {
    float len = std::sqrt(float(pad.lx) * pad.lx + float(pad.ly) * pad.ly);
    if (len == 0) return;
    pad.lx = int16_t(std::clamp(pad.lx * 32767.0f / len, -32767.0f, 32767.0f));
    pad.ly = int16_t(std::clamp(pad.ly * 32767.0f / len, -32767.0f, 32767.0f));
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

// --- Sprint and lean on the keyboard (Settings::sprint_mode, lean_mode; the overlay's Movement). The game's sprint is
// a toggle (the left stick's click: a press starts it, another stops it) and its lean a press each way (D-pad left and
// right: a lean, the other way upright, moving ends it). In play on the keyboard the game gets our presses instead of
// the keys': Sprint on Hold keeps the game's sprint in step with the key (read from the pawn,
// clicked when they differ); Lean on Toggle sends the other
// way for the same key again, on Hold as the key comes up; and from a lean asked for and while leaning the stick goes no
// further than a slow walk, so the lean is taken while walking and walking does not end it.
namespace {
constexpr uint16_t kSprintBit = 1u << 6, kLeanLeft = 1u << 2, kLeanRight = 1u << 3;
constexpr ULONGLONG kClick = 100;     // ms a press we send is held
constexpr ULONGLONG kSprintRetry = 400;  // ms between sprint clicks at most
constexpr ULONGLONG kSprintEase = 250;    // ms at a slow walk to end a sprint before stopping for a moment
constexpr uint32_t kSprintingBits = 0x07;  // in bIsSprinting's word: 0x08 walking, 0x0F sprinting (seen in play; GroundSpeed
                                           // stays 220, the game multiplies it)
constexpr ULONGLONG kLeanHold = 500;   // ms the stick stays at a slow walk for a lean asked for, as for a crouch
ULONGLONG g_lean_until = 0;
uint16_t g_lean_queued = 0;  // a lean pressed while sprinting: the game spent that press ending the sprint, sent again
// Sprint on Hold, crouch (or prone, a double tap) pressed: the held sprint is off until the sprint key is let go or
// pressed again. Hold started the game's sprint again at once, which stood the player up (user, 2026-11-13)
bool g_sprint_blocked = false;
constexpr float kLeanReach = 0.5f;    // of the stick while leaning: the slow walk's
uint32_t g_ground_at = 0, g_lean_at = 0, g_sprint_at = 0;
uint16_t g_raw_last = 0, g_send = 0;
ULONGLONG g_send_until = 0;
constexpr uint8_t kLeanIsLeft = 0, kLeanIsCenter = 1, kLeanIsRight = 2;  // LeanDirection (seen in play)

float Ground() {
    uint8_t* base = g_base;
    return base && g_pawn && g_ground_at ? X86_MEMF32(g_pawn + g_ground_at) : 0;
}
uint32_t SprintWord() {
    uint8_t* base = g_base;
    return base && g_pawn && g_sprint_at ? X86_MEM32(g_pawn + g_sprint_at) : 0;
}
bool Sprinting() {
    uint8_t* base = g_base;
    return base && g_pawn && g_sprint_at && (X86_MEM32(g_pawn + g_sprint_at) & kSprintingBits);
}
uint8_t LeanState() {
    uint8_t* base = g_base;
    return base && g_pawn && g_lean_at ? uint8_t(X86_MEM32(g_pawn + g_lean_at)) : kLeanIsCenter;
}
void Send(uint16_t bit, ULONGLONG now) {
    g_send = bit;
    g_send_until = now + kClick;
}
}  // namespace

static void SprintLean(input::Pad& pad, ULONGLONG now, bool aiming) {
    const app::Settings& s = app::Get();
    uint16_t raw = pad.buttons, pressed = raw & ~g_raw_last;
    g_raw_last = raw;
    bool sprinting = Sprinting();
    // sprint pressed while leaning: upright first (the game took no sprint from a lean), then the sprint: Hold's
    // wanting does it once upright, Toggle's is clicked then
    static bool sprint_after_lean = false;
    uint8_t leaning_now = LeanState();
    if ((pressed & kSprintBit) && leaning_now != kLeanIsCenter) {
        Send(leaning_now == kLeanIsLeft ? kLeanRight : kLeanLeft, now);
        g_lean_until = 0;
        g_lean_queued = 0;
        sprint_after_lean = s.sprint_mode == 1;
    }
    if (sprint_after_lean && leaning_now == kLeanIsCenter && now >= g_send_until + kClick) {
        Send(kSprintBit, now);
        sprint_after_lean = false;
    }
    // Hold: the game kept in step with the key, wanted while held and moving, not while leaning (a lean ends a
    // sprint) or aiming. A click starts a sprint (retried no oftener than kSprintRetry). The game ignores clicks while
    // sprinting (ten in a row did nothing) and ends a sprint as the stick slows: so ended by easing the stick to a slow
    // walk until it has, then to nothing for a moment if that did not. Aiming (either mode) ends a sprint the same way:
    // the game raised the sights only once it ended, so they came up as the player stopped
    bool hold = s.sprint_mode == 0;
    if ((pressed & kSprintBit) || !(raw & kSprintBit)) g_sprint_blocked = false;  // a new press, or let go
    bool want = hold && (raw & kSprintBit) && (pad.lx || pad.ly) && LeanState() == kLeanIsCenter && !g_lean_queued &&
                now >= g_lean_until && !aiming && !g_sprint_blocked;
    if (hold) {
        pad.buttons &= ~kSprintBit;
        static ULONGLONG next = 0;
        if (want && !sprinting && now >= next && now >= g_send_until) {
            Send(kSprintBit, now);
            next = now + kSprintRetry;
        }
    }
    static ULONGLONG stopping = 0;  // when easing the stick to end a sprint began, 0 not
    if (sprinting && ((hold && !want) || aiming)) {
        if (!stopping) stopping = now;
        if (now - stopping < kSprintEase) Limit(pad, kLeanReach);
        else if (now - stopping < 2 * kSprintEase + 100) pad.lx = pad.ly = 0;  // at most this long: never held still
    } else {
        stopping = 0;
        // sprinting, or about to: the stick all the way, whatever Slow Walk says (half over, the game ended the sprint)
        if (want || sprinting) Full(pad);
    }
    uint8_t lean = LeanState();
    pad.buttons &= ~(kLeanLeft | kLeanRight);
    for (uint16_t key : {kLeanLeft, kLeanRight}) {
        uint16_t other = key == kLeanLeft ? kLeanRight : kLeanLeft;
        bool that_way = lean == (key == kLeanLeft ? kLeanIsLeft : kLeanIsRight);  // leaning the key's way now
        if (pressed & key) {
            bool up = s.lean_mode == 1 && that_way;  // Toggle: again, upright
            Send(up ? other : key, now);
            if (!up) g_lean_until = now + kLeanHold;  // walking: slowed so the game takes the lean
            if (!up && Sprinting()) {  // the game ends the sprint with it: the lean sent again once it has
                g_lean_queued = key;
                g_lean_until = now + 2 * kLeanHold;
            }
        }
        // Hold: leaning its way with the key up (let go, or let go before the lean came), upright
        if (s.lean_mode == 0 && that_way && !(raw & key) && now >= g_send_until + kClick) Send(other, now);
    }
    if (g_lean_queued && !Sprinting() && now >= g_send_until + kClick) {
        if (lean == kLeanIsCenter) Send(g_lean_queued, now);
        g_lean_queued = 0;
    }
    if (now < g_send_until) pad.buttons |= g_send;
    if (lean != kLeanIsCenter || now < g_lean_until) Limit(pad, kLeanReach);
}

// Aim on Toggle (Settings::aim_mode): the aim key (Tweak Lock, the left trigger: the game aims while it is held)
// switches aiming on and off; the trigger is held for the game meanwhile. Out of play (a menu, the overlay), off.
static bool Aim(input::Pad& pad, bool in_play) {
    constexpr int kAimAnalog = 6;
    static bool was = false, on = false;
    bool held = pad.analog[kAimAnalog] != 0;
    if (app::Get().aim_mode != 1 || !in_play) {
        was = held;
        on = false;
        return held;
    }
    if (held && !was) on = !on;
    was = held;
    pad.analog[kAimAnalog] = on ? 0xFF : 0;
    return on;
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
            g_sprint_blocked = true;  // a held sprint waits for its key again (SprintLean)
        }
        if (pressed & kDpadUp) g_crouch_until = g_prone_until = 0;
        g_last_buttons = pad.buttons;
        Rise(pad, pressed, now);
        bool aiming = Aim(pad, input::MouseLooks());
        if (input::MouseLooks()) SprintLean(pad, now, aiming);  // in play: menus keep the D-pad's own left and right
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
        g_ground_at = pawn ? PropertyOffset(base, FindProperty(base, controller, pawn, "GroundSpeed")) : 0;
        g_lean_at = pawn ? PropertyOffset(base, FindProperty(base, controller, pawn, "LeanDirection")) : 0;
        g_sprint_at = pawn ? PropertyOffset(base, FindProperty(base, controller, pawn, "bIsSprinting")) : 0;
        if (pawn)
            LOGR("stance: pawn %08X, bIsCrouched +0x%X, bIsProne +0x%X, GroundSpeed +0x%X, LeanDirection +0x%X", pawn,
                 g_crouched_at, g_prone_at, g_ground_at, g_lean_at);
    }
    g_base = base;
    g_pawn = pawn;
    static uint8_t lean = 0xFF;  // the lean's value logged as it changes (its values seen in play)
    if (pawn && g_lean_at && LeanState() != lean) {
        lean = LeanState();
        LOGR("stance: lean %u, ground speed %.0f", lean, Ground());
    }
}

