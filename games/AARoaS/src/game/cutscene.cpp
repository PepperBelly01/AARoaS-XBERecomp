// In-mission cutscenes: while one plays the controller has no pawn, or its camera follows something else (ViewTarget;
// both seen in a mission's opening scene). The mouse turned
// the controller's Rotation in them, and the scene's camera with it; Free Look turned it too. Both are off meanwhile
// (mouse.cpp asks InCutscene). The engine's other cutscene flags (bMatineeIsRunning, bInterpolating, bCinematicView,
// bHideHUD) are not on this game's controller, pawn or level.
#include <cstdint>

#include "core/log.h"
#include "xdk/hle.h"

uint32_t FindProperty(uint8_t* base, uint32_t controller, uint32_t object, const char* name);  // aim_assist.cpp
uint32_t PropertyOffset(uint8_t* base, uint32_t property);

namespace {
bool g_cutscene = false;
}  // namespace

bool InCutscene() { return g_cutscene; }

// each frame (mouse.cpp's UpdateInput hook)
void CutsceneFrame(uint8_t* base, uint32_t viewport) {
    uint32_t controller = X86_MEM32(viewport + 0x34);
    static uint32_t seen = 0, pawn_at = 0, target_at = 0;
    if (controller != seen) {
        seen = controller;
        pawn_at = controller ? PropertyOffset(base, FindProperty(base, controller, controller, "Pawn")) : 0;
        target_at = controller ? PropertyOffset(base, FindProperty(base, controller, controller, "ViewTarget")) : 0;
    }
    uint32_t pawn = pawn_at ? X86_MEM32(controller + pawn_at) : 0;
    uint32_t target = target_at ? X86_MEM32(controller + target_at) : 0;
    // a scene that takes the pawn away (the opening one did, for most of it) counts too, as do death and loading
    bool now = controller && pawn_at && (!pawn || (target && target != pawn));
    if (now != g_cutscene) LOGR("cutscene: %s", now ? "the camera follows another, the mouse does not look" : "over");
    g_cutscene = now;
}
