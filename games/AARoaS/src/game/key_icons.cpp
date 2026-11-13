// Play's button icons drawn with the player's own keys (the HUD's A prompt, the tutorial boxes' buttons). At the
// first frame and whenever a binding changes, each icon is drawn as its action's first key (a key cap with the key's
// name, or a mouse with the button lit) and saved into textures\replacement\@keyboard\ beside the exe, where the
// texture pack's watcher (gpu/texturedumper.cpp) loads it again within half a second. The menus' icons stay
// tools\make_key_icons.ps1's fixed ones: the menus have their own keys (core/keyboard.cpp), safe from odd bindings.
//
// The tutorial boxes draw T-UIAAC.utx's buttons_xbox_* (32x32 DXT5); their A is the HUD's. The keys below are those
// textures' keys (texturedumper Key), worked out from the package.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>
#pragma comment(lib, "gdiplus.lib")

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "app/app.h"
#include "core/input.h"
#include "core/log.h"

namespace {

namespace gp = Gdiplus;

constexpr int kSide = 128;  // drawn at 4x for sharp keys at high resolutions

// a texture and the action whose key it shows (GameInfo::key_actions names). The D-pad shows its four
struct Icon {
    const char* key;
    const char* action;
};
constexpr Icon kIcons[] = {
    {"e4dd91ad26fcd422", "Action / Select"},  // A, the HUD's and the tutorial's
    {"1538c644e9ac0353", "Command / Back"},   // B
    {"bdc2f027099b16f3", "Reload"},           // X
    {"29a9bc5af6801dba", "Switch Weapon"},    // Y
    {"42c1bc7874044606", "Sprint"},           // left stick click
    {"589124fb2ef0855b", "Aim / Zoom"},       // right stick click
    {"ad544119729dd5ef", "Alt Fire"},         // Back
    {"21fc4578608ea091", nullptr},            // D-pad: up, left, right, down
    {"4d15816daa74ed2e", "Pause Menu"},       // Start
    {"5889971a557d2bf0", "Map"},              // Black
    {"191dc294f8d99c51", "Tweak Lock"},       // left trigger
    {"ee3debd77c39e691", "Fire"},             // right trigger
    {"060bb671b2957f25", "Alt Fire"},         // White
};
constexpr const char* kDpad[] = {"D-Pad Up", "D-Pad Left", "D-Pad Right", "D-Pad Down"};

const gp::Color kCap(255, 28, 31, 24);       // key face
const gp::Color kEdge(255, 214, 214, 190);   // key rim, text
const gp::Color kLit(255, 150, 196, 84);     // the mouse button meant

// the action's first key, else its second; 0 unbound
uint16_t KeyOf(const char* action) {
    const auto& actions = app::kGame.key_actions;
    const auto& keys = app::Get().keys;
    for (size_t i = 0; i < actions.size() && i < keys.size(); i++)
        if (std::strcmp(actions[i].name, action) == 0) return keys[i][0] ? keys[i][0] : keys[i][1];
    return 0;
}

// a key's name as short as a cap holds it
std::wstring Label(uint16_t key) {
    std::string n = input::KeyName(key);
    auto drop = [&](const char* prefix) {
        if (n.rfind(prefix, 0) == 0) n = n.substr(std::strlen(prefix));
    };
    drop("LEFT ");
    drop("RIGHT ");
    if (n == "MOUSE 4") n = "M4";
    if (n == "MOUSE 5") n = "M5";
    if (n == "BACKSPACE") n = "BKSP";
    if (n == "CAPS LOCK") n = "CAPS";
    if (n == "PAGE UP") n = "PGUP";
    if (n == "PAGE DOWN") n = "PGDN";
    if (n == "DELETE") n = "DEL";
    if (n == "INSERT") n = "INS";
    if (n == "ESCAPE") n = "ESC";
    if (key == VK_UP) return L"\x2191";  // the arrow keys as arrows
    if (key == VK_DOWN) return L"\x2193";
    if (key == VK_LEFT) return L"\x2190";
    if (key == VK_RIGHT) return L"\x2192";
    std::wstring w;
    for (char c : n) w += wchar_t(uint8_t(c));
    return w;
}

void RoundRect(gp::GraphicsPath& p, float x, float y, float w, float h, float r) {
    p.AddArc(x, y, 2 * r, 2 * r, 180, 90);
    p.AddArc(x + w - 2 * r, y, 2 * r, 2 * r, 270, 90);
    p.AddArc(x + w - 2 * r, y + h - 2 * r, 2 * r, 2 * r, 0, 90);
    p.AddArc(x, y + h - 2 * r, 2 * r, 2 * r, 90, 90);
    p.CloseFigure();
}

// a key cap with its label in a cell of size s at (x, y)
void Key(gp::Graphics& g, float x, float y, float s, const std::wstring& label) {
    float m = s * 0.08f;
    gp::SolidBrush face(kCap), text(kEdge);
    gp::Pen rim(kEdge, s * 0.06f);
    gp::GraphicsPath cap;
    RoundRect(cap, x + m, y + m, s - 2 * m, s - 2 * m, s * 0.16f);
    g.FillPath(&face, &cap);
    g.DrawPath(&rim, &cap);
    if (label.empty()) return;
    // the largest bold type that fits the face
    gp::FontFamily family(L"Arial");
    float box = s - 4 * m, size = s * 0.62f;
    gp::StringFormat fmt;
    fmt.SetAlignment(gp::StringAlignmentCenter);
    fmt.SetLineAlignment(gp::StringAlignmentCenter);
    for (; size > 4; size *= 0.9f) {
        gp::Font font(&family, size, gp::FontStyleBold, gp::UnitPixel);
        gp::RectF fit;
        g.MeasureString(label.c_str(), -1, &font, gp::PointF(0, 0), &fmt, &fit);
        if (fit.Width <= box) break;
    }
    gp::Font font(&family, size, gp::FontStyleBold, gp::UnitPixel);
    g.DrawString(label.c_str(), -1, &font, gp::RectF(x, y + s * 0.02f, s, s), &fmt, &text);
}

enum class Button { Left, Right, Middle, Wheel };

// a mouse with one button lit
void Mouse(gp::Graphics& g, float x, float y, float s, Button button) {
    float w = s * 0.56f, h = s * 0.84f, mx = x + (s - w) / 2, my = y + (s - h) / 2, r = w * 0.45f;
    gp::SolidBrush face(kCap), lit(kLit);
    gp::Pen rim(kEdge, s * 0.06f), thin(kEdge, s * 0.04f);
    gp::GraphicsPath body;
    RoundRect(body, mx, my, w, h, r);
    g.FillPath(&face, &body);
    float split = my + h * 0.42f;
    g.SetClip(&body);
    if (button == Button::Left) g.FillRectangle(&lit, mx, my, w / 2, split - my);
    if (button == Button::Right) g.FillRectangle(&lit, mx + w / 2, my, w / 2, split - my);
    g.ResetClip();
    g.DrawPath(&rim, &body);
    g.DrawLine(&thin, mx, split, mx + w, split);
    g.DrawLine(&thin, mx + w / 2, my, mx + w / 2, split);
    gp::RectF wheel(mx + (w - w * 0.2f) / 2, my + h * 0.12f, w * 0.2f, h * 0.2f);
    bool wheel_lit = button == Button::Middle || button == Button::Wheel;
    g.FillRectangle(wheel_lit ? static_cast<gp::Brush*>(&lit) : &face, wheel);
    g.DrawRectangle(&thin, wheel);
    if (button == Button::Wheel) {  // arrows above and below, beside the mouse
        float ax = mx + w + s * 0.04f, aw = s * 0.12f;
        gp::PointF up[] = {{ax + aw / 2, my + h * 0.05f}, {ax, my + h * 0.2f}, {ax + aw, my + h * 0.2f}};
        gp::PointF dn[] = {{ax + aw / 2, my + h * 0.47f}, {ax, my + h * 0.32f}, {ax + aw, my + h * 0.32f}};
        g.FillPolygon(&lit, up, 3);
        g.FillPolygon(&lit, dn, 3);
    }
}

// one key or mouse button filling a cell
void Draw(gp::Graphics& g, float x, float y, float s, uint16_t key) {
    switch (key) {
        case VK_LBUTTON: return Mouse(g, x, y, s, Button::Left);
        case VK_RBUTTON: return Mouse(g, x, y, s, Button::Right);
        case VK_MBUTTON: return Mouse(g, x, y, s, Button::Middle);
        case input::kWheel:
        case input::kWheelUp:
        case input::kWheelDown: return Mouse(g, x, y, s, Button::Wheel);
    }
    Key(g, x, y, s, Label(key));
}

bool PngEncoder(CLSID& out) {
    UINT n = 0, bytes = 0;
    gp::GetImageEncodersSize(&n, &bytes);
    std::vector<uint8_t> buf(bytes);
    auto* codecs = reinterpret_cast<gp::ImageCodecInfo*>(buf.data());
    if (!bytes || gp::GetImageEncoders(n, bytes, codecs) != gp::Ok) return false;
    for (UINT i = 0; i < n; i++)
        if (std::wcscmp(codecs[i].MimeType, L"image/png") == 0) {
            out = codecs[i].Clsid;
            return true;
        }
    return false;
}

// the icons for these keys, written
void Write(std::vector<std::array<uint16_t, 4>> keys) {
    static ULONG_PTR token = 0;
    if (!token) {
        gp::GdiplusStartupInput in;
        if (gp::GdiplusStartup(&token, &in, nullptr) != gp::Ok) return;
    }
    CLSID png;
    if (!PngEncoder(png)) return;
    std::filesystem::path dir = app::ExeDir() / "textures" / "replacement" / "@keyboard";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    for (size_t i = 0; i < std::size(kIcons); i++) {
        gp::Bitmap bmp(kSide, kSide, PixelFormat32bppARGB);
        gp::Graphics g(&bmp);
        g.SetSmoothingMode(gp::SmoothingModeAntiAlias);
        g.SetTextRenderingHint(gp::TextRenderingHintAntiAliasGridFit);
        g.Clear(gp::Color(0, 0, 0, 0));
        if (kIcons[i].action) {
            Draw(g, 0, 0, kSide, keys[i][0]);
        } else {  // the D-pad: a plus of three keys across
            float k = kSide / 3.0f;
            Draw(g, k, 0, k, keys[i][0]);
            Draw(g, 0, k, k, keys[i][1]);
            Draw(g, 2 * k, k, k, keys[i][2]);
            Draw(g, k, 2 * k, k, keys[i][3]);
        }
        std::filesystem::path file = dir / (std::string("32x32_") + kIcons[i].key + "_keys.png");
        bmp.Save(file.c_str(), &png, nullptr);
    }
}

std::vector<std::array<uint16_t, 4>> g_drawn;  // the keys the icons on disk show
std::mutex g_lock;
std::atomic<bool> g_busy{false};

}  // namespace

// each frame (mouse.cpp's UpdateInput hook): the icons drawn again off this thread once the keys they show changed
void UpdateKeyIcons() {
    std::vector<std::array<uint16_t, 4>> keys;
    for (const Icon& icon : kIcons) {
        std::array<uint16_t, 4> k{};
        if (icon.action) {
            k[0] = KeyOf(icon.action);
        } else {
            for (int d = 0; d < 4; d++) k[d] = KeyOf(kDpad[d]);
        }
        keys.push_back(k);
    }
    if (keys == g_drawn || g_busy) return;
    g_drawn = keys;
    g_busy = true;
    std::thread([keys] {
        {
            std::lock_guard l(g_lock);
            Write(keys);
        }
        LOGR("key icons: drawn with the player's keys");
        g_busy = false;
    }).detach();
}
