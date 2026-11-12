// Mouse look as the PC game has it. The Xbox build kept Unreal's PC input: xboxuser.ini still binds
// MouseX=Axis aMouseX and MouseY=Axis aMouseY, and bAlwaysMouseLook is on. After the viewport's controller update
// (UXboxViewport::UpdateInput, 0x2F4D00) the mouse's raw counts go to UEngine::InputEvent as IK_MouseX and IK_MouseY
// axis events, as the PC viewport sends them. Keys and mouse buttons press the controller (GameInfo::key_actions).
#include <algorithm>
#include <cstring>
#include <fstream>
#include <vector>
#include <iterator>
#include <string>

#include "app/app.h"
#include "core/log.h"
#include "kernel/memory.h"
#include "core/input.h"
#include "xdk/hle.h"

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
    const std::pair<const char*, const char*> kRaw[] = {{"MouseAccelThreshold", "0.000000"}, {"MouseSmoothingMode", "0"}};
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
    LOGI("mouse: acceleration and smoothing off in %s", file.string().c_str());
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

// the world camera's rotation as this frame builds it (widescreen.cpp's UpdateMatrices hook, the frame's first)
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
    uint32_t client = X86_MEM32(viewport + kClient);
    uint32_t engine = client ? X86_MEM32(client + kEngine) : 0;
    uint32_t list = client ? X86_MEM32(client + kViewports) : 0;
    uint32_t player = list ? X86_MEM32(list) : 0;
    if (!engine || !player) return;

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
            LOG_FIRST(6, "mouse: still searching: camera pitch %d yaw %d fov %.1f, %zu candidate pairs", g_cam.pitch,
                      g_cam.yaw, g_cam.fov, g_finds.size());
    }

    float dx, dy;
    if (!input::TakeMouse(dx, dy)) return;
    const app::Settings& s = app::Get();
    dx *= s.invert_mouse_x ? -s.mouse_x : s.mouse_x;
    dy *= (s.invert_mouse_y ? s.mouse_y : -s.mouse_y) * app::kGame.mouse_y_ratio;  // screen down, look down

    uint32_t obj = g_found ? X86_MEM32(player + g_use.field) : 0;
    if (obj && Readable(obj + g_use.offset, 8)) {
        // aiming zooms: the turn scales with the camera's angle, as Unreal's own mouse look does
        float zoom = g_cam.fov > 1 && app::kGame.fov > 0 ? g_cam.fov / app::kGame.fov : 1.0f;
        float ux = dx * kUnitsPerCount * zoom + g_rest_x, uy = dy * kUnitsPerCount * zoom + g_rest_y;
        int32_t ix = int32_t(ux), iy = int32_t(uy);
        g_rest_x = ux - float(ix);
        g_rest_y = uy - float(iy);
        uint32_t at = obj + g_use.offset;
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
