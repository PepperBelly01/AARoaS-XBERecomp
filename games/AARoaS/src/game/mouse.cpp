// Mouse look as the PC game has it. The Xbox build kept Unreal's PC input: xboxuser.ini still binds
// MouseX=Axis aMouseX and MouseY=Axis aMouseY, and bAlwaysMouseLook is on. After the viewport's controller update
// (UXboxViewport::UpdateInput, 0x2F4D00) the mouse's raw counts go to UEngine::InputEvent as IK_MouseX and IK_MouseY
// axis events, as the PC viewport sends them. Keys and mouse buttons press the controller (GameInfo::key_actions).
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <vector>
#include <iterator>
#include <string>

#include "app/app.h"
#include "core/log.h"
#include "gpu/screen2d.h"
#include "gpu/texturedumper.h"
#include "kernel/memory.h"
#include "core/input.h"
#include "xdk/hle.h"

// LOG_FIRST at report level: a release build drops info lines
#define REPORT_FIRST(n, ...)                   \
    do {                                       \
        static int _count = 0;                 \
        if (_count++ < (n)) LOGR(__VA_ARGS__); \
    } while (0)

namespace {

// viewport +0x18 client; client +0x2C engine, +0x30 viewports (first is the player's); engine vtable +0x94
// InputEvent(viewport, key, action, delta), thiscall
constexpr uint32_t kClient = 0x18, kEngine = 0x2C, kViewports = 0x30, kInputEvent = 0x94;
constexpr uint32_t kMouseX = 0xE4, kMouseY = 0xE5;  // EInputKey
constexpr uint32_t kAxis = 4;                       // EInputAction IST_Axis

uint32_t FloatBits(float f) {
    uint32_t u;
    std::memcpy(&u, &f, 4);
    return u;
}

// a "Key=" line's value set, in text of 1 or 2 byte characters; true when it changed
template <typename C>
bool SetIniValue(std::basic_string<C>& text, const char* key, const char* value) {
    std::basic_string<C> k, v;
    for (const char* c = key; *c; c++) k += C(*c);
    k += C('=');
    for (const char* c = value; *c; c++) v += C(*c);
    for (size_t at = text.find(k); at != std::basic_string<C>::npos; at = text.find(k, at + 1)) {
        if (at && text[at - 1] != C('\n')) continue;  // the start of a line only
        size_t from = at + k.size(), to = text.find_first_of(std::basic_string<C>{C('\r'), C('\n')}, from);
        if (to == std::basic_string<C>::npos) to = text.size();
        if (text.compare(from, to - from, v) == 0) return false;
        text.replace(from, to - from, v);
        return true;
    }
    return false;
}

}  // namespace

// Raw mouse: the game's user settings (xboxuser.ini, copied to the cache drive Z: and read and rewritten there) turn
// on Unreal's mouse acceleration (MouseAccelThreshold: small movements are scaled down, so slow and vertical motion
// lagged) and smoothing. Both go off in that copy at each start; the disc's own is left as it is. The game writes it
// as 1 byte text, the disc has it as UTF-16.
void PatchUserIni() {
    std::filesystem::path dir = app::ExeDir() / (app::Instance() > 1 ? "hdd" + std::to_string(app::Instance()) : "hdd");
    std::filesystem::path file = dir / "Z" / "System" / "xboxuser.ini";
    std::ifstream in(file, std::ios::binary);
    if (!in) return;  // not copied yet: the first start keeps the game's
    std::string bytes((std::istreambuf_iterator<char>(in)), {});
    in.close();
    // Ctrl back to unbound: an experiment once made it the game's FreeLook (W and S look up and down, not a free camera)
    const std::pair<const char*, const char*> kRaw[] = {
        {"MouseAccelThreshold", "0.000000"}, {"MouseSmoothingMode", "0"}, {"Ctrl", ""}};
    bool changed = false;
    if (bytes.size() >= 2 && uint8_t(bytes[0]) == 0xFF && uint8_t(bytes[1]) == 0xFE) {
        std::u16string text(bytes.size() / 2, u'\0');
        std::memcpy(text.data(), bytes.data(), text.size() * 2);
        for (auto [k, v] : kRaw) changed |= SetIniValue(text, k, v);
        if (changed) bytes.assign(reinterpret_cast<const char*>(text.data()), text.size() * 2);
    } else {
        for (auto [k, v] : kRaw) changed |= SetIniValue(bytes, k, v);
    }
    if (!changed) return;
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out.write(bytes.data(), std::streamsize(bytes.size()));
    LOGR("mouse: acceleration and smoothing off in %s", file.string().c_str());
}

// Raw mouse look, direct. The game's scripts shape whatever reaches them as input (made for a thumbstick: quick
// movements turn further than slow ones), so the mouse turns the view itself: its counts go straight into the player
// controller's Rotation (an FRotator, Pitch Yaw Roll in 65536ths of a turn), as a native PC game turns its camera.
//
// Where Rotation sits is found while playing: each frame the world camera's rotation (the camera scene node's, at
// +0x1E0 as UpdateMatrices builds it) is looked for in the objects the player's viewport points to; a Pitch and Yaw
// pair that keeps matching as the view turns is the controller's. It is checked once in use: the camera has to follow
// what is written, else the next one is tried. Until then the mouse goes through the game's input as before.
namespace {

constexpr uint32_t kNodeRotation = 0x1E0, kNodeFov = 0x1EC;  // FCameraSceneNode: ViewRotation, FOV in degrees
constexpr uint32_t kFirstField = 0x28, kLastField = 0x80;      // viewport fields past UObject's that may point to it
constexpr uint32_t kSearchBytes = 0x1000;                      // of each object, for the pair
constexpr int kAdoptChanges = 8;   // the pair matched through this many different yaws
constexpr int kVerify = 6;         // writes the camera has to follow (or not) to keep (or drop) a find
constexpr float kUnitsPerCount = 4.0f;  // Rotation units a count at sensitivity 1: 0.022 degrees, as common PC games

struct Camera {
    int16_t pitch = 0, yaw = 0;
    float fov = 0;
    bool fresh = false;  // set since the last UpdateInput
} g_cam;

uint32_t g_pre_at = 0;  // the controller's Rotation the tick turns (the view), for the recoil after it

struct Find {
    uint32_t field, offset;
    int changes;
    int16_t last_yaw;
};
std::vector<Find> g_finds;                // matching pairs while searching
std::vector<uint64_t> g_dropped;          // field << 32 | offset that the camera did not follow
// found in play (viewport +0x34 is UPlayer::Actor, the PlayerController; +0x124 its Rotation): used from the start
// and trusted, never dropped (in the main menu the camera does not follow the controller, and checking it there threw
// it out). Only a location a search finds is checked.
bool g_found = true;
Find g_use{0x34, 0x124, 0, 0};
int g_good = kVerify, g_bad = 0;          // checks of the find in use; the known one counts as passed
bool g_expect = false;
int16_t g_expect_yaw = 0;
float g_rest_x = 0, g_rest_y = 0;         // fractions of a unit not written yet

bool Readable(uint32_t a, uint32_t n) { return a >= 0x10000 && mem::IsCommitted(a) && mem::IsCommitted(a + n - 1); }

uint64_t Key(uint32_t field, uint32_t offset) { return uint64_t(field) << 32 | offset; }

void Search(uint8_t* base, uint32_t viewport) {
    if (g_cam.pitch == 0 && g_cam.yaw == 0) return;  // zeroed memory matches that everywhere
    for (uint32_t field = kFirstField; field < kLastField; field += 4) {
        uint32_t obj = X86_MEM32(viewport + field);
        if (!Readable(obj, kSearchBytes)) continue;
        for (uint32_t off = 0x28; off + 8 <= kSearchBytes; off += 4) {
            if (int16_t(X86_MEM32(obj + off)) != g_cam.pitch || int16_t(X86_MEM32(obj + off + 4)) != g_cam.yaw) continue;
            if (std::find(g_dropped.begin(), g_dropped.end(), Key(field, off)) != g_dropped.end()) continue;
            auto it = std::find_if(g_finds.begin(), g_finds.end(), [&](const Find& f) { return f.field == field && f.offset == off; });
            if (it == g_finds.end()) {
                if (g_finds.size() < 64) g_finds.push_back({field, off, 0, g_cam.yaw});
                continue;
            }
            if (it->last_yaw != g_cam.yaw) {
                it->changes++;
                it->last_yaw = g_cam.yaw;
            }
            if (it->changes >= kAdoptChanges) {
                g_use = *it;
                g_found = true;
                g_good = g_bad = 0;
                g_expect = false;
                g_finds.clear();
                LOGR("mouse: view rotation at viewport +0x%X, +0x%X: direct mouse look", field, off);
                return;
            }
        }
    }
}

// a find the camera did not follow: dropped, the search goes on
void Drop() {
    LOGR("mouse: viewport +0x%X, +0x%X did not turn the view, searching on", g_use.field, g_use.offset);
    g_dropped.push_back(Key(g_use.field, g_use.offset));
    g_found = false;
    g_expect = false;
}

}  // namespace

void AimAssist(uint8_t* base, uint32_t viewport);  // aim_assist.cpp: whether the game holds a target (Target Zoom)
float ShownFov();                                   // aim_assist.cpp
bool GamePaused(uint8_t* base, uint32_t viewport);  // aim_assist.cpp
void MenusPaused(bool paused);                      // menus.cpp
bool MenusNameEntry();                              // menus.cpp
void UpdateKeyIcons();                              // key_icons.cpp
void StanceFrame(uint8_t* base, uint32_t viewport);  // stance.cpp
void CutsceneFrame(uint8_t* base, uint32_t viewport);  // cutscene.cpp
bool InCutscene();                                   // cutscene.cpp
void LookSpringDefault(X86Context& ctx, uint8_t* base, uint32_t controller);  // below
void GameOptionsSync(uint8_t* base, uint32_t options);                     // below
uint32_t PlayerOptions(X86Context& ctx, uint32_t controller);              // below
void DrawDistanceFrame(uint8_t* base, uint32_t viewport);  // drawdistance.cpp

bool RecoilTurn(uint8_t* base, uint32_t controller, int32_t& pitch, int32_t& yaw);  // recoil.cpp

// After the tick, as the frame's world camera is set up (widescreen.cpp): the weapon's recoil and sway (recoil.cpp)
// turn the view and the camera built from it
void AimAfterTick(uint8_t* base, uint32_t node) {
    uint32_t view = g_pre_at;
    if (!view || g_use.offset != 0x124 || !Readable(view, 8)) return;
    int32_t p = 0, y = 0;
    RecoilTurn(base, view - 0x124, p, y);
    if (!p && !y) return;
    X86_MEM32(view) = X86_MEM32(view) + uint32_t(p);
    X86_MEM32(view + 4) = X86_MEM32(view + 4) + uint32_t(y);
    X86_MEM32(node + kNodeRotation) = X86_MEM32(node + kNodeRotation) + uint32_t(p);
    X86_MEM32(node + kNodeRotation + 4) = X86_MEM32(node + kNodeRotation + 4) + uint32_t(y);
}

// --- Free Look (key action "Free Look", Alt): held, the mouse turns the camera alone, the aim (the controller's
// Rotation), the body and where they walk stay. The game has no such thing (its own FreeLook button makes forward and
// back look up and down). The offset goes onto both cameras as the frame sets them up (widescreen.cpp): the world's,
// and partway the weapon's, so the gun drifts off centre as the head turns from it. Let go, it eases back to the aim.
namespace {
constexpr float kFreeYaw = 18000, kFreePitch = 11000;  // the most, Rotation units (99 and 60 degrees)
constexpr float kFreeEase = 0.045f;                     // seconds: the offset's time constant going back
constexpr float kFreeWeaponLeft = 6400, kFreeWeaponRight = 10900, kFreeWeaponPitch = 3640;  // the weapon camera turns at
    // most 35 degrees left, 60 right (the gun sits right: looking right it crossed the screen and stayed in view), 20 up
    // or down, then follows (further, the arms showed their unmodelled backs)
constexpr float kUnitToRadian = 3.14159265f / 32768.0f;
float g_world_fov = 0;     // the world camera node's FOV this frame (degrees)
float g_world_scale = 1;   // what the world's projection gets beyond its FOV: Field of View, the zoom hold (widescreen.cpp)
constexpr uint32_t kFovAt = 0x1EC;  // FCameraSceneNode FOV, degrees
float g_free_yaw = 0, g_free_pitch = 0;
bool g_free_held = false;
}  // namespace

void FreeLookCamera(uint8_t* base, uint32_t node, bool world) {
    if (world) g_world_fov = X86_MEMF32(node + kFovAt);
    if (world && !g_free_held && (g_free_yaw || g_free_pitch)) {
        static LARGE_INTEGER last{};
        LARGE_INTEGER now, freq;
        QueryPerformanceCounter(&now);
        QueryPerformanceFrequency(&freq);
        float dt = last.QuadPart ? float(double(now.QuadPart - last.QuadPart) / double(freq.QuadPart)) : 0.0f;
        last = now;
        float k = std::exp(-std::min(dt, 0.1f) / kFreeEase);
        g_free_yaw *= k;
        g_free_pitch *= k;
        if (std::abs(g_free_yaw) < 8 && std::abs(g_free_pitch) < 8) {
            g_free_yaw = g_free_pitch = 0;
            last.QuadPart = 0;
        }
    }
    if (!g_free_yaw && !g_free_pitch) return;
    float yaw = g_free_yaw, pitch = g_free_pitch;
    if (!world) {
        // the weapon's camera: turned so the gun moves across the screen as far as the world does, its muzzle staying
        // on what it pointed at. Its lens is narrower (47 degrees, the world's 75 and more), so the same angle moved it
        // further and it overshot: a point at angle a shows at m tan a (m the projection's scale, 1 / tan of half the FOV
        // and what the world's gets beyond it), so the weapon turns atan(tan a m_world / m_weapon). Both from the nodes
        // themselves each frame (the projections the game last built held a menu camera's for a while after a menu)
        constexpr float kHalfDegree = 3.14159265f / 360.0f;
        float weapon_fov = X86_MEMF32(node + kFovAt);
        float ratio = 0;
        if (g_world_fov > 1 && g_world_fov < 179 && weapon_fov > 1 && weapon_fov < 179)
            ratio = std::tan(weapon_fov * kHalfDegree) / std::tan(g_world_fov * kHalfDegree) * g_world_scale;
        auto match = [ratio](float units) {
            if (ratio <= 0) return units;
            float a = std::clamp(units * kUnitToRadian, -1.4f, 1.4f);  // 80 degrees: tan stays finite
            return std::atan(std::tan(a) * ratio) / kUnitToRadian;
        };
        yaw = std::clamp(match(yaw), -kFreeWeaponLeft, kFreeWeaponRight);
        pitch = std::clamp(match(pitch), -kFreeWeaponPitch, kFreeWeaponPitch);
    }
    // pitch kept within a little of straight up and down, as the mouse's own turn
    int32_t p = int16_t(X86_MEM32(node + kNodeRotation)), q = std::clamp(p + int32_t(pitch), -16000, 16000);
    X86_MEM32(node + kNodeRotation) = X86_MEM32(node + kNodeRotation) + uint32_t(q - p);
    X86_MEM32(node + kNodeRotation + 4) = X86_MEM32(node + kNodeRotation + 4) + uint32_t(int32_t(yaw));
}

void NoteWorldScale(float scale) { g_world_scale = scale > 0 ? scale : 1; }

void NoteWorldCamera(uint8_t* base, uint32_t node) {
    g_cam.pitch = int16_t(X86_MEM32(node + kNodeRotation));
    g_cam.yaw = int16_t(X86_MEM32(node + kNodeRotation + 4));
    g_cam.fov = X86_MEMF32(node + kNodeFov);
    g_cam.fresh = true;
}

// UXboxViewport::UpdateInput(skip, delta time), thiscall: nothing when the first argument is set
X86_EXTERN_FUNC(__imp__sub_002F4D00);
X86_FUNC(sub_002F4D00) {
    uint32_t viewport = ctx.ecx, skip = X86_MEM32(ctx.esp + 4);
    __imp__sub_002F4D00(ctx, base);
    if (skip) return;
    UpdateKeyIcons();  // key_icons.cpp: play's button icons drawn with the player's keys
    uint32_t client = X86_MEM32(viewport + kClient);
    uint32_t engine = client ? X86_MEM32(client + kEngine) : 0;
    uint32_t list = client ? X86_MEM32(client + kViewports) : 0;
    uint32_t player = list ? X86_MEM32(list) : 0;
    if (!engine || !player) return;
    // a game menu has the mouse: the front end (no 3D drawn) or the pause menu (the game paused). The cursor is
    // free there, the view does not turn, and the mouse buttons and wheel work the menu (core/keyboard.cpp)
    StanceFrame(base, player);  // stance.cpp: the pawn for the keyboard's stance
    CutsceneFrame(base, player);  // cutscene.cpp
    DrawDistanceFrame(base, player);  // drawdistance.cpp: Grass Distance
    bool paused = GamePaused(base, player);
    if (gpu::screen2d::Frame3D() && !paused) LookSpringDefault(ctx, base, X86_MEM32(player + 0x34));  // below
    // the game's Game Options and the overlay's rows kept the same, in menus and in play (below)
    if (uint32_t c = X86_MEM32(player + 0x34)) {
        uint32_t options = PlayerOptions(ctx, c);
        if (options && Readable(options + 0x84, 1)) GameOptionsSync(base, options);
    }
    input::SetGameMenu(!gpu::screen2d::Frame3D() || paused);
    MenusPaused(paused);  // menus.cpp: Escape closes the pause menu from its first page
    // the button icons follow the device used last: the keyboard's and mouse's (textures\replacement\@keyboard\,
    // tools\make_key_icons.ps1) or the controller's own. The on-screen keyboard's are @typing's: Backspace, Space, Shift,
    // Delete and Enter press its buttons there (menus.cpp, core/keyboard.cpp)
    bool typing = MenusNameEntry();
    gpu::texturedumper::SetPackGroup("keyboard", !input::UsingPad() && !typing);
    gpu::texturedumper::SetPackGroup("typing", !input::UsingPad() && typing);
    // the night vision overlay without its HUD shapes (lens case, compass) unless the HUD shows 4:3 over them: hidden,
    // or spread 16:9 away from them (user, 2026-11-15)
    gpu::texturedumper::SetPackGroup("hidehud", app::Get().hide_hud || app::Widescreen());
    // the scopes and binoculars remade for 16:9 (round again across it), the originals in 4:3
    gpu::texturedumper::SetPackGroup("wide", app::Widescreen());
    AimAssist(base, player);  // aim_assist.cpp: a target held, for Target Zoom
    uint32_t view_obj = g_found ? X86_MEM32(player + g_use.field) : 0;
    uint32_t view = view_obj && Readable(view_obj + g_use.offset, 8) ? view_obj + g_use.offset : 0;
    g_pre_at = view;  // the Rotation the tick turns: the recoil turns it after (AimAfterTick)

    // the find in use: did the camera follow the last write
    bool fresh = g_cam.fresh;
    g_cam.fresh = false;
    if (g_found && g_expect && fresh) {
        if (g_cam.yaw == g_expect_yaw) g_good++;
        else if (g_good < kVerify) g_bad++;
        g_expect = false;
        if (g_bad >= kVerify) Drop();
    }
    if (!g_found && fresh && input::MouseLooks()) {
        Search(base, player);
        static uint32_t frames = 0;
        if (!g_found && ++frames % 900 == 0)
            REPORT_FIRST(6, "mouse: still searching: camera pitch %d yaw %d fov %.1f, %zu candidate pairs", g_cam.pitch,
                      g_cam.yaw, g_cam.fov, g_finds.size());
    }

    g_free_held = view && !InCutscene() && input::ActionHeld(0);  // Free Look (game_info.cpp), above
    float dx, dy;
    if (!input::TakeMouse(dx, dy)) return;
    if (InCutscene()) return;  // cutscene.cpp: the scene's camera is not the player's to turn (the counts dropped)
    const app::Settings& s = app::Get();
    dx *= s.invert_mouse_x ? -s.mouse_x : s.mouse_x;
    dy *= (s.invert_mouse_y ? s.mouse_y : -s.mouse_y) * app::kGame.mouse_y_ratio;  // screen down, look down

    if (view) {
        // aiming zooms: the turn scales with the camera's angle, as Unreal's own mouse look does
        // the angle from before any zoom onto a target (aim_assist.cpp); sights and scopes slow the turn by theirs
        float fov = ShownFov() > 1 ? ShownFov() : g_cam.fov;
        float zoom = fov > 1 && app::kGame.fov > 0 ? fov / app::kGame.fov : 1.0f;
        // Zoom Sensitivity: the zoom onto a target slows the turn too, by the setting's share (100%: the picture moves
        // as fast on screen as before the zoom)
        const app::Settings& set = app::Get();
        if (set.auto_zoom && set.zoom_sensitivity && g_cam.fov > 1 && fov > 1 && g_cam.fov < fov)
            zoom *= std::pow(g_cam.fov / fov, float(set.zoom_sensitivity_percent) / 100.0f);
        float ux = dx * kUnitsPerCount * zoom + g_rest_x, uy = dy * kUnitsPerCount * zoom + g_rest_y;
        int32_t ix = int32_t(ux), iy = int32_t(uy);
        g_rest_x = ux - float(ix);
        g_rest_y = uy - float(iy);
        if (g_free_held) {  // the camera alone turns
            g_free_yaw = std::clamp(g_free_yaw + float(ix), -kFreeYaw, kFreeYaw);
            g_free_pitch = std::clamp(g_free_pitch + float(iy), -kFreePitch, kFreePitch);
            return;
        }
        uint32_t at = view;
        uint32_t pitch = X86_MEM32(at), yaw = X86_MEM32(at + 4);
        // pitch kept within a little of straight up and down; the game's own limits (prone and the like) apply on
        int32_t p = int16_t(pitch), q = std::clamp(p + iy, -16000, 16000);
        X86_MEM32(at) = pitch + uint32_t(q - p);
        X86_MEM32(at + 4) = yaw + uint32_t(ix);
        if (ix && g_good < kVerify) {
            g_expect = true;
            g_expect_yaw = int16_t(yaw + uint32_t(ix));
        }
        return;
    }

    // not found yet: the game's own mouse input
    uint32_t fn = X86_MEM32(X86_MEM32(engine) + kInputEvent);
    uint32_t eax = ctx.eax;
    ctx.ecx = engine;
    if (dx != 0) kernel::CallGuest(ctx, fn, {player, kMouseX, kAxis, FloatBits(dx)});
    ctx.ecx = engine;
    if (dy != 0) kernel::CallGuest(ctx, fn, {player, kMouseY, kAxis, FloatBits(dy)});
    ctx.eax = eax;
}

// --- Look Spring off by default: the option lives in the player's profile (new ones start with it on; xboxuser.ini's
// bSnapToLevel is already False and is not it). The first time play runs with this build, it is switched off once and
// that noted in the settings file (look_spring_default_applied), so a player who turns it on keeps it
bool g_look_spring_applied = false;  // GameInfo::options

// the player's options object, as the game's input reaches it from the controller (0x33050: 0xEF670, 0x1AF00, 0x3DB80)
uint32_t PlayerOptions(X86Context& ctx, uint32_t controller) {
    uint32_t ecx = ctx.ecx, at = controller;
    for (uint32_t fn : {0xEF670u, 0x1AF00u, 0x3DB80u}) {
        if (!at) break;
        ctx.ecx = at;
        at = kernel::CallGuest(ctx, fn, {});
    }
    ctx.ecx = ecx;
    return at;
}

void LookSpringDefault(X86Context& ctx, uint8_t* base, uint32_t controller) {
    if (g_look_spring_applied || !controller) return;
    uint32_t options = PlayerOptions(ctx, controller);
    if (!options || !Readable(options + 0x78, 1)) return;
    X86_MEM8(options + 0x78) &= uint8_t(~0x02);
    g_look_spring_applied = true;
    app::Save();
    LOGR("look spring: off by default (the player's options %08X), noted in the settings", options);
}

// --- The game's Game Options as overlay rows (user, 2026-11-14): the player's options object's +0x84 holds them a bit
// each, not in the menu's order: Auto Sighting 0x01, Acquisition Icon 0x08 (each found by toggling it in the menu and
// watching the byte; 0x3F with all on). Auto Sighting off stops the lock's pull
// and snapping at the source, which our undoing of it never fully matched; the Acquisition Icon is the red dot over a
// target, off by choice with Target Zoom off (the zoom moved, the dot is far from the enemy). Both ways: a change in
// the overlay is written into the option, one in the game's menu or by a profile loading comes back to the setting.
// At first sight of an options object the setting is written to it.
void GameOptionsSync(uint8_t* base, uint32_t options) {
    constexpr uint32_t kFlags = 0x84;
    struct Row {
        uint8_t bit;
        bool app::Settings::*setting;
        const char* name;
    };
    static const Row kRows[] = {{0x01, &app::Settings::aim_assist, "Auto Sighting"},
                                {0x08, &app::Settings::acquisition_icon, "Acquisition Icon"}};
    static uint32_t seen = 0;
    static bool last_setting[2] = {}, last_game[2] = {};
    app::Settings& s = app::Get();
    uint8_t flags = X86_MEM8(options + kFlags);
    static int logged = -1;  // the whole byte as it changes, to tell the menu's options apart
    if (flags != logged) LOGR("game options: flags %02X (options %08X)", flags, options);
    logged = flags;
    bool first = options != seen;
    seen = options;
    bool save = false;
    for (int i = 0; i < 2; i++) {
        bool& setting = s.*kRows[i].setting;
        bool game = (flags & kRows[i].bit) != 0;
        if (first || setting != last_setting[i]) {  // the overlay's (or a new profile's first sight): into the game
            if (game != setting) flags = setting ? uint8_t(flags | kRows[i].bit) : uint8_t(flags & ~kRows[i].bit);
            if (game != setting) LOGR("game options: %s %s (the overlay's)", kRows[i].name, setting ? "on" : "off");
        } else if (game != last_game[i]) {  // the game's menu: into the setting
            setting = game;
            save = true;
            LOGR("game options: %s %s (the game's menu)", kRows[i].name, game ? "on" : "off");
        }
        last_setting[i] = setting;
        last_game[i] = (flags & kRows[i].bit) != 0;
    }
    X86_MEM8(options + kFlags) = flags;
    if (save) app::Save();
}

// --- Look Spring off for the keyboard and mouse. The game's Look Spring (its Controller options) eases the view back
// level as the player moves while the look axes are idle: 0x1C26B, inside the controller's rotation update (0x1C240),
// asks the player's options object (0x3DB80, thiscall on what ecx holds there) for it, +0x78 bit 2. The mouse turns the
// view without the axes, so the spring pulled against it. While the keyboard and mouse are in use the bit is cleared
// around that code and put back at once (the option shown and saved stays the player's); the controller has it as set
X86_EXTERN_FUNC(__imp__sub_0001C26B);
X86_FUNC(sub_0001C26B) {
    constexpr uint32_t kOptions = 0x3DB80, kSpringAt = 0x78;
    constexpr uint8_t kSpring = 0x02;
    uint32_t options = kernel::CallGuest(ctx, kOptions, {});
    if (input::UsingPad()) {
        __imp__sub_0001C26B(ctx, base);
        return;
    }
    bool on = options && (X86_MEM8(options + kSpringAt) & kSpring);
    if (on) X86_MEM8(options + kSpringAt) &= uint8_t(~kSpring);
    __imp__sub_0001C26B(ctx, base);
    if (on) X86_MEM8(options + kSpringAt) |= kSpring;
}
