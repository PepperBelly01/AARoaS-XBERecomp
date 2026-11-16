// The overlay's Developer section (Settings::developer_mode, set by hand in settings.toml; user, 2026-11-16): aids for
// testing playthroughs, never shown to players.
// God Mode, through the game's own properties (aim_assist.cpp's reflection): the pawn's Health held at kCushion and its
// BleedHealth at 0 every frame; switched off, Health goes back to what it was. Found on the way (2026-11-16):
// - AA's soldier has Pawn.Health (an int, 100 at full), no HealthMax and no bGodMode.
// - Topped up to 100 each frame a rifle hit still killed within one tick.
// - Held at 10000 the view spawned upside down; at 250 the view narrows (the game reads Health for its view: the FoV
//   setting makes up for it).
// - Pawn.ReducedDamageType set to the DamageType class (FindClass) is not honoured: a hit still took 100 to 80.
// - A grenade can still kill. Real invulnerability would mean catching the game's TakeDamage in its script calls.
#include <cstdint>
#include <string>
#include <vector>

#include "app/app.h"
#include "core/log.h"
#include "xdk/hle.h"

uint32_t FindProperty(uint8_t* base, uint32_t controller, uint32_t object, const char* name);  // aim_assist.cpp
uint32_t PropertyOffset(uint8_t* base, uint32_t property);
// probes kept for the next step (aim_assist.cpp): an object's properties named with any of the words into the log
// (found Health, BleedHealth), and a class object by name
void LogProperties(uint8_t* base, uint32_t object, const std::vector<std::string>& words);
uint32_t FindClass(uint8_t* base, const char* name);

namespace {
constexpr int32_t kCushion = 250;  // more than a hit or a burst takes in one tick, under 256
}  // namespace

// each frame, mouse.cpp's UpdateInput hook, with the player viewport
void DevFrame(uint8_t* base, uint32_t viewport) {
    const app::Settings& s = app::Get();
    bool on = s.developer_mode && s.god_mode;
    static bool was_on = false;
    static uint32_t held_pawn = 0;  // the pawn God Mode holds, to let go again
    static int32_t before = 100;    // its Health when taken
    static uint32_t health_at = 0, bleed_at = 0;
    uint32_t controller = X86_MEM32(viewport + 0x34);
    if (!controller) return;
    static uint32_t seen = 0, pawn_p = 0;
    if (controller != seen) {
        seen = controller;
        pawn_p = FindProperty(base, controller, controller, "Pawn");
    }
    uint32_t pawn = pawn_p ? X86_MEM32(controller + PropertyOffset(base, pawn_p)) : 0;
    static uint32_t seen_pawn = 0;
    if (pawn && pawn != seen_pawn) {
        seen_pawn = pawn;
        health_at = PropertyOffset(base, FindProperty(base, controller, pawn, "Health"));
        bleed_at = PropertyOffset(base, FindProperty(base, controller, pawn, "BleedHealth"));
        if (on) LOGR("dev: god mode on pawn %08X: Health +0x%X, BleedHealth +0x%X", pawn, health_at, bleed_at);
    }
    // switched off (or a new life): the held pawn's Health back as it was
    if (was_on && (!on || pawn != held_pawn)) {
        if (held_pawn && held_pawn == pawn && health_at && int32_t(X86_MEM32(pawn + health_at)) > before)
            X86_MEM32(pawn + health_at) = uint32_t(before);
        held_pawn = 0;
    }
    was_on = on && pawn;
    if (!on || !pawn || !health_at) return;
    int32_t health = int32_t(X86_MEM32(pawn + health_at));
    if (health <= 0) return;  // already dead: left to the game
    if (held_pawn != pawn) {
        held_pawn = pawn;
        before = health < 100 ? health : 100;
    }
    X86_MEM32(pawn + health_at) = uint32_t(kCushion);
    if (bleed_at) X86_MEM32(pawn + bleed_at) = 0;
}
