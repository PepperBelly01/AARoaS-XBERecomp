// Aim Assist off: the game's aim lock (AACPlayerInput's TweakMe) and its auto zoom, taken back out.
//
// What the lock does, measured in play: while the controller holds a target (SelectedTarget, the red dot's, or
// LockTarget) its tick turns the controller's Rotation toward it, up to ~700 units (4 degrees) a tick, harder for
// nearer targets, inside a cone around each (the Observation skill likely sets its size); it never offsets the camera
// alone. Clearing LockTarget did nothing (the tick picks again). So the aim is kept apart from the view (mouse.cpp):
// the tick starts from the player's true aim, so the game picks its target fairly, and after it, as the frame's camera
// is set up, the pull is taken back out of the controller and the camera. Firing, the aim is turned away by the
// pull ahead of the tick instead, so the shot leaves along the sights; the weapon's kick is kept. With a target held
// the game also zooms in by its distance (AutoZoom): shown with Auto Zoom on, else the world is drawn at the angle it
// had before; the mouse's turn follows the angle from before the zoom either way (ZoomHold).
//
// This build keeps variable names but not function names, so the variables are found through the engine's own
// reflection: names are entries in memory (Index, Flags, HashNext, then the text), objects the usual UObject layout
// (+0x20 name index, +0x24 class), classes link their variables (+0x2C super, +0x38 first child, children chained
// at +0x30). Which property member holds the byte offset is learned from Actor.Rotation, known to be at +0x124
// (+0x44). The same text can sit elsewhere in memory, so each candidate index is tried on the class chain.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>
#include <algorithm>

#include "app/app.h"
#include "core/input.h"
#include "core/log.h"
#include "kernel/memory.h"
#include "xdk/hle.h"

namespace {

constexpr uint32_t kObjName = 0x20, kObjClass = 0x24, kSuper = 0x2C, kChildren = 0x38;
constexpr uint32_t kNextCandidates[] = {0x30, 0x28, 0x2C};  // UField::Next: the first that walks
constexpr uint32_t kRotationOffset = 0x124;
constexpr bool kSkipLockTurn = true;  // Aim Assist off: the lock's turn not made (false: undone after the tick, mouse.cpp)

bool Readable(uint32_t a, uint32_t n) { return a >= 0x10000 && mem::IsCommitted(a) && mem::IsCommitted(a + n - 1); }

// The indexes a name may have: each place its text sits as a name entry would (word aligned, an index 12 bytes ahead,
// flags of 0 after it, the allocation's small size word ahead of the index), best first. The same text can sit
// elsewhere in memory, so the caller keeps the one the class chain has.
const std::vector<uint32_t>& NameIndexes(uint8_t* base, const char* text) {
    static std::unordered_map<std::string, std::vector<uint32_t>> cache;
    auto it = cache.find(text);
    if (it != cache.end()) return it->second;
    size_t n = std::strlen(text) + 1;  // with its terminator
    std::vector<uint32_t> strict, loose;
    for (uint32_t page = 0x10000; page < 0x80000000u; page += 0x1000) {
        if (!mem::IsCommitted(page) || !mem::IsCommitted(page - 0x1000)) continue;
        const uint8_t* p = base + page;
        for (uint32_t i = 0; i + n <= 0x1000; i += 4) {  // entries are word aligned
            if (p[i] != uint8_t(text[0]) || std::memcmp(p + i, text, n) != 0) continue;
            uint32_t at = page + i;
            uint32_t size = X86_MEM32(at - 16), index = X86_MEM32(at - 12), flags = X86_MEM32(at - 8);
            if (!index || index >= 0x40000) continue;
            std::vector<uint32_t>& to = flags == 0 && size >= 0x10 && size < 0x200 ? strict : loose;
            if (std::find(to.begin(), to.end(), index) == to.end() && to.size() < 8) to.push_back(index);
        }
    }
    strict.insert(strict.end(), loose.begin(), loose.end());
    std::string list;
    char item[16];
    for (uint32_t v : strict) {
        std::snprintf(item, sizeof(item), " %X", v);
        list += item;
    }
    LOGR("aim assist: name %s may be:%s", text, list.empty() ? " (none)" : list.c_str());
    return cache[text] = strict;
}

uint32_t g_next = 0;          // UField::Next, learned
uint32_t g_offset_field = 0;  // the UProperty member holding its byte offset, learned

// the property (or other field) named so in the object's class or its supers
uint32_t Field(uint8_t* base, uint32_t object, uint32_t name, uint32_t next) {
    uint32_t cls = Readable(object, 0x40) ? X86_MEM32(object + kObjClass) : 0;
    for (int depth = 0; cls && depth < 32 && Readable(cls, 0x40); depth++, cls = X86_MEM32(cls + kSuper))
        for (uint32_t f = X86_MEM32(cls + kChildren), n = 0; f && n < 2048 && Readable(f, 0x60); f = X86_MEM32(f + next), n++)
            if (X86_MEM32(f + kObjName) == name) return f;
    return 0;
}

void Words(uint8_t* base, const char* what, uint32_t at, uint32_t count) {
    if (!Readable(at, count * 4)) return;
    std::string line;
    char word[16];
    for (uint32_t i = 0; i < count; i++) {
        std::snprintf(word, sizeof(word), "%s%08X", i % 8 ? " " : (i ? " | " : ""), X86_MEM32(at + i * 4));
        line += word;
    }
    LOGR("aim assist: %s %08X: %s", what, at, line.c_str());
}

// the controller's class chain and the start of each class's field list, for when the walk finds nothing
void DumpClasses(uint8_t* base, uint32_t controller) {
    uint32_t cls = X86_MEM32(controller + kObjClass);
    for (int depth = 0; cls && depth < 10 && Readable(cls, 0x60); depth++, cls = X86_MEM32(cls + kSuper)) {
        char what[48];
        std::snprintf(what, sizeof(what), "class %d (name %X)", depth, X86_MEM32(cls + kObjName));
        Words(base, what, cls, 24);
        uint32_t first = X86_MEM32(cls + kChildren);
        Words(base, "  first child", first, 24);
        for (uint32_t next : kNextCandidates) {
            std::string chain;
            char item[24];
            for (uint32_t f = first, n = 0; f && n < 12 && Readable(f, 0x40); f = X86_MEM32(f + next), n++) {
                std::snprintf(item, sizeof(item), " %X", X86_MEM32(f + kObjName));
                chain += item;
            }
            LOGR("aim assist:   names along +0x%X:%s", next, chain.c_str());
        }
    }
}

// learns UField::Next and the offset member from Actor.Rotation on the controller
bool Calibrate(uint8_t* base, uint32_t controller) {
    if (g_offset_field) return true;
    static uint32_t tried = 0;  // once a controller: each map has its own
    if (tried == controller) return false;
    tried = controller;
    for (uint32_t rotation : NameIndexes(base, "Rotation")) {
        for (uint32_t next : kNextCandidates) {
            uint32_t f = Field(base, controller, rotation, next);
            if (!f) continue;
            for (uint32_t k = 0x28; k < 0x80; k += 4) {
                if (X86_MEM32(f + k) != kRotationOffset) continue;
                g_next = next;
                g_offset_field = k;
                LOGR("aim assist: Rotation is name %X, fields chained at +0x%X, property offset at +0x%X", rotation,
                     next, k);
                return true;
            }
            Words(base, "Rotation property without 0x124", f, 32);
        }
    }
    LOGR("aim assist: Rotation not found on the class of controller %08X", controller);
    static int dumps = 0;
    if (dumps++ < 2) DumpClasses(base, controller);
    return false;
}

struct Var {
    const char* name;
    uint32_t holder = 0;  // the object it was found on
    uint32_t offset = 0;
};

// on the controller, else on its PlayerInput object
bool Locate(uint8_t* base, uint32_t controller, Var& v) {
    uint32_t holders[2] = {controller, 0};
    for (uint32_t input_name : NameIndexes(base, "PlayerInput")) {
        uint32_t pi = Field(base, controller, input_name, g_next);
        if (!pi) continue;
        uint32_t object = X86_MEM32(controller + X86_MEM32(pi + g_offset_field));
        if (Readable(object, 0x40)) holders[1] = object;
        break;
    }
    for (uint32_t name : NameIndexes(base, v.name))
        for (uint32_t h : holders) {
            uint32_t f = h ? Field(base, h, name, g_next) : 0;
            if (!f) continue;
            v.holder = h;
            v.offset = X86_MEM32(f + g_offset_field);
            return v.offset && v.offset < 0x4000;
        }
    return false;
}

// fire held, or let go of within the last moment (the kick lands within a few ticks of the shot; any longer and the
// lock's pull toward the next target came through as kick, a rubber band after each shot)
bool Firing() {
    static ULONGLONG last = 0;
    ULONGLONG now = GetTickCount64();
    for (size_t i = 0; i < app::kGame.key_actions.size() && i < app::Get().keys.size(); i++) {
        const input::KeyAction& a = app::kGame.key_actions[i];
        if (a.target != input::PadTarget::Analog || a.index != 7) continue;  // right trigger: Fire
        for (uint16_t k : app::Get().keys[i])
            if (k && k < 0x100 && (GetAsyncKeyState(k) & 0x8000)) last = now;
    }
    input::Pad pad = input::Poll(0);
    if (pad.analog[7] > 30) last = now;
    return last && now - last < 150;  // the kick lands within a few ticks of the shot
}

// the look stick in use: its turning is the player's own
bool StickLooks() {
    input::Pad pad = input::Poll(0);
    return std::abs(int(pad.rx)) > 8000 || std::abs(int(pad.ry)) > 8000;
}

bool g_target = false;     // the game held a target (the red dot's, or its lock) before this tick
bool g_recent = false;     // held one, or within the last second
float g_base_fov = 0;      // the world camera's angle before a target was taken
float g_shown_fov = 0;     // the angle the world is drawn at
ULONGLONG g_target_seen = 0;
}  // namespace

// Auto zoom: with a target held the game narrows the view by the target's distance (AutoZoom), a different zoom for
// each target, and the mouse (whose turn follows the zoom) slowed with it, which felt like the aim sticking. With Aim
// Assist off the world is drawn at the angle it had before the target was taken (no zoom on iron sights, a scope's
// own), held until the game's zoom has come back after the target is let go. widescreen.cpp's projection hook: the
// world camera's angle in, the factor for its M00 and M11 out.
float ZoomHold(float fov) {
    g_shown_fov = fov;
    if (fov <= 1) {
        g_base_fov = fov;
        return 1.0f;
    }
    ULONGLONG now = GetTickCount64();
    if (g_target) g_target_seen = now;
    bool returning = g_target_seen && now - g_target_seen < 1500 && fov < g_base_fov - 0.5f;
    if (!g_target && !returning) {
        g_base_fov = fov;
        return 1.0f;
    }
    g_shown_fov = g_base_fov;  // the mouse's turn follows the angle from before the zoom either way
    if (app::Get().auto_zoom) return 1.0f;  // the game's zoom shown
    constexpr float kHalfDegree = 3.14159265f / 360.0f;
    return std::tan(fov * kHalfDegree) / std::tan(g_base_fov * kHalfDegree);
}

// a property of an object's class chain by name (the controller, to learn the layout from): the property object,
// 0 when none. Its byte offset is PropertyOffset's
uint32_t FindProperty(uint8_t* base, uint32_t controller, uint32_t object, const char* name) {
    if (!Readable(controller, 0x200) || !Calibrate(base, controller) || !Readable(object, 0x40)) return 0;
    for (uint32_t index : NameIndexes(base, name))
        if (uint32_t f = Field(base, object, index, g_next)) return f;
    return 0;
}
uint32_t PropertyOffset(uint8_t* base, uint32_t property) { return property ? X86_MEM32(property + g_offset_field) : 0; }

// the object's class, or a super of it, is named so (after a FindProperty has learned the layout)
bool IsA(uint8_t* base, uint32_t object, const char* class_name) {
    if (!g_next || !Readable(object, 0x40)) return false;
    const std::vector<uint32_t>& names = NameIndexes(base, class_name);
    uint32_t cls = X86_MEM32(object + kObjClass);
    for (int depth = 0; cls && depth < 32 && Readable(cls, 0x40); depth++, cls = X86_MEM32(cls + kSuper))
        if (std::find(names.begin(), names.end(), X86_MEM32(cls + kObjName)) != names.end()) return true;
    return false;
}

// a name's text from its index (NameIndexes the other way: an entry's index 12 bytes ahead of its text, flags 0 after
// it); "" when not found. Slow (all memory), cached: for probes
std::string NameText(uint8_t* base, uint32_t index) {
    static std::unordered_map<uint32_t, std::string> cache;
    auto it = cache.find(index);
    if (it != cache.end()) return it->second;
    std::string found;
    for (uint32_t page = 0x10000; page < 0x80000000u && found.empty(); page += 0x1000) {
        if (!mem::IsCommitted(page) || !mem::IsCommitted(page + 0x1000)) continue;
        for (uint32_t i = 16; i + 16 <= 0x1000; i += 4) {
            uint32_t at = page + i;
            if (X86_MEM32(at - 12) != index || X86_MEM32(at - 8) != 0) continue;
            uint32_t size = X86_MEM32(at - 16);
            if (size < 0x10 || size >= 0x200) continue;
            std::string s;
            for (uint32_t k = 0; k < 64; k++) {
                char c = char(X86_MEM8(at + k));
                if (!c) break;
                if (c < 0x20 || c > 0x7E) {
                    s.clear();
                    break;
                }
                s += c;
            }
            if (s.size() >= 2) {
                found = s;
                break;
            }
        }
    }
    return cache[index] = found;
}

std::string ClassName(uint8_t* base, uint32_t object) {
    uint32_t cls = Readable(object, 0x40) ? X86_MEM32(object + kObjClass) : 0;
    return Readable(cls, 0x40) ? NameText(base, X86_MEM32(cls + kObjName)) : "";
}

// a field of a struct (a UStruct: its fields chained as a class's are), by name; 0 when none
uint32_t StructField(uint8_t* base, uint32_t strct, const char* name) {
    if (!g_next || !Readable(strct, 0x40)) return 0;
    for (uint32_t index : NameIndexes(base, name))
        for (uint32_t f = X86_MEM32(strct + kChildren), n = 0; f && n < 512 && Readable(f, 0x60); f = X86_MEM32(f + g_next), n++)
            if (X86_MEM32(f + kObjName) == index) return f;
    return 0;
}

// The game paused (the pause menu): LevelInfo.Pauser set, the controller's Level (Actor.Level) its LevelInfo.
// Looked up once a map (a new controller), by name through the class chain as the lock's variables are.
bool GamePaused(uint8_t* base, uint32_t viewport) {
    uint32_t controller = X86_MEM32(viewport + 0x34);
    if (!Readable(controller, 0x200) || !Calibrate(base, controller)) return false;
    static uint32_t seen = 0, level_at = 0, pauser_at = 0;
    if (controller != seen) {
        seen = controller;
        level_at = pauser_at = 0;
        for (uint32_t name : NameIndexes(base, "Level"))
            if (uint32_t f = Field(base, controller, name, g_next)) {
                level_at = X86_MEM32(f + g_offset_field);
                break;
            }
        uint32_t level = level_at ? X86_MEM32(controller + level_at) : 0;
        if (Readable(level, 0x40))
            for (uint32_t name : NameIndexes(base, "Pauser"))
                if (uint32_t f = Field(base, level, name, g_next)) {
                    pauser_at = X86_MEM32(f + g_offset_field);
                    break;
                }
        LOGR("menus: controller %08X Level +0x%X, LevelInfo.Pauser +0x%X", controller, level_at, pauser_at);
    }
    if (!level_at || !pauser_at) return false;
    uint32_t level = X86_MEM32(controller + level_at);
    return Readable(level + pauser_at, 4) && X86_MEM32(level + pauser_at) != 0;
}

// the angle the world is drawn at, for the mouse's turn (0 before the first frame)
float ShownFov() { return g_shown_fov; }

// mouse.cpp's UpdateInput hook, each frame with the player viewport, before the game's tick: 0 nothing, 1 undo the
// last tick's turn, 2 the same while firing (the kick kept)
int AimAssist(uint8_t* base, uint32_t viewport) {
    g_target = false;
    const app::Settings& s = app::Get();
    // the game as it was (the lock, its zoom, the turn slowed fully by it): nothing to watch
    if (s.aim_assist && s.auto_zoom && s.zoom_sensitivity && s.zoom_sensitivity_percent >= 100) return 0;
    uint32_t controller = X86_MEM32(viewport + 0x34);
    if (!Readable(controller, 0x200) || !Calibrate(base, controller)) return 0;

    static uint32_t seen_controller = 0;
    static Var lock{"LockTarget"}, selected{"SelectedTarget"};
    static bool located = false;
    if (controller != seen_controller) {  // a new map: a new controller
        seen_controller = controller;
        lock = Var{"LockTarget"};
        selected = Var{"SelectedTarget"};
        located = Locate(base, controller, lock) && lock.holder == controller;
        located &= Locate(base, controller, selected) && selected.holder == controller;
        LOGR("aim assist: controller %08X: LockTarget +0x%X, SelectedTarget +0x%X%s", controller, lock.offset,
             selected.offset, located ? "" : ", not both found on it: assist left on");
    }
    if (!located) return 0;
    // a target held, or let go of within the last second (the pull runs on a little past it); never in a cutscene,
    // which has none, so its camera pans are left alone; nor while the look stick turns (its turning is the player's)
    bool target = X86_MEM32(controller + lock.offset) || X86_MEM32(controller + selected.offset);
    ULONGLONG now = GetTickCount64();
    static ULONGLONG last_target = 0;
    if (target) last_target = now;
    g_target = target;
    g_recent = last_target && now - last_target < 1000;
    if (s.aim_assist || !g_recent || StickLooks()) return 0;  // the target is still watched for Auto Zoom
    // the lock's turn is skipped where it is made (0x22520 below): nothing to undo after the tick. Undoing it there
    // also undid the sway and the like, which reach the view in the same tick
    if (kSkipLockTurn) return 0;
    return Firing() ? 2 : 1;
}

// The lock's turn toward its target (thiscall: a rotation, delta time; ret 0x10), called only by AACPlayerInput's
// lock (0x32DB0) after it picks the target (0x32CA0, kept: Auto Zoom and the red dot go by it). Aim Assist off: not
// made at all
X86_EXTERN_FUNC(__imp__sub_00022520);
X86_FUNC(sub_00022520) {
    if (kSkipLockTurn && !app::Get().aim_assist) {
        static bool told = false;
        if (!told) LOGR("aim assist: off, the lock's turn skipped");
        told = true;
        ctx.esp += 4 + 0x10;
        return;
    }
    __imp__sub_00022520(ctx, base);
}
