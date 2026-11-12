// Keyboard and mouse: held keys and mouse buttons press the controller on port 0 through the bindings in use, and the
// mouse's raw movement is kept for the game side's mouse look. While the mouse looks the cursor is hidden and held in
// the window; the settings overlay, a game menu or another window in front frees it.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <mutex>

#include "app/app.h"
#include "core/input.h"
#include "core/log.h"

namespace input {

namespace {

using Clock = std::chrono::steady_clock;
constexpr auto kWheelHold = std::chrono::milliseconds(80);  // a wheel notch holds its key this long

std::mutex g_lock;
bool g_held[kKeyCount] = {};         // virtual keys and the wheel keys
Clock::time_point g_wheel_until[2];  // up, down
float g_dx = 0, g_dy = 0;            // raw counts not taken yet
std::vector<KeyBind> g_binds;
std::atomic<bool> g_game_menu{false};
std::atomic<uint16_t> g_menu_buttons{0};              // held for the game side's cursor ...
std::atomic<int64_t> g_menu_buttons_until{0};         // ... until then (steady clock, ms)
std::atomic<bool> g_menu_click{true};                 // a left click presses A
HWND g_hwnd = nullptr;
// rebinding: 1 waiting for a key, 2 a key taken
std::atomic<int> g_capture{0};
std::atomic<uint16_t> g_captured{0};

bool HasBinds() {
    std::lock_guard lock(g_lock);
    return !g_binds.empty();
}

bool Active() { return g_hwnd && GetForegroundWindow() == g_hwnd; }

bool Looks() { return Active() && !app::MenuOpen() && !g_game_menu.load() && HasBinds(); }

// the cursor hidden and held in the client area while the mouse looks, free otherwise
void Capture(bool on) {
    static bool captured = false;
    if (on) {
        RECT r;
        GetClientRect(g_hwnd, &r);
        POINT a{r.left, r.top}, b{r.right, r.bottom};
        ClientToScreen(g_hwnd, &a);
        ClientToScreen(g_hwnd, &b);
        RECT clip{a.x, a.y, b.x, b.y};
        ClipCursor(&clip);
    } else if (captured) {
        ClipCursor(nullptr);
    }
    captured = on;
}

void Set(uint16_t key, bool down) {
    std::lock_guard lock(g_lock);
    if (key < kKeyCount) g_held[key] = down;
}

// rebinding: a press is kept rather than pressed
bool Captures(uint16_t key) {
    if (g_capture.load() != 1) return false;
    if (key == VK_ESCAPE) {
        g_capture = 0;
        return true;
    }
    g_captured = key;
    g_capture = 2;
    return true;
}

// left and right Shift, Ctrl and Alt are told apart by the scan code
uint16_t KeyOf(uintptr_t wp, intptr_t lp) {
    uint16_t vk = uint16_t(wp & 0xFF);
    UINT scan = (lp >> 16) & 0xFF;
    bool ext = (lp >> 24) & 1;
    if (vk == VK_SHIFT) return uint16_t(MapVirtualKeyW(scan, MAPVK_VSC_TO_VK_EX));
    if (vk == VK_CONTROL) return ext ? VK_RCONTROL : VK_LCONTROL;
    if (vk == VK_MENU) return ext ? VK_RMENU : VK_LMENU;
    return vk;
}

}  // namespace

void SetKeys(const std::vector<KeyBind>& binds) {
    std::lock_guard lock(g_lock);
    g_binds = binds;
}

std::string KeyName(uint16_t key) {
    switch (key) {
        case 0: return "";
        case VK_LBUTTON: return "LEFT MOUSE";
        case VK_RBUTTON: return "RIGHT MOUSE";
        case VK_MBUTTON: return "MIDDLE MOUSE";
        case VK_XBUTTON1: return "MOUSE 4";
        case VK_XBUTTON2: return "MOUSE 5";
        case kWheelUp: return "WHEEL UP";
        case kWheelDown: return "WHEEL DOWN";
        case kWheel: return "WHEEL";
        case VK_LSHIFT: return "LEFT SHIFT";
        case VK_RSHIFT: return "RIGHT SHIFT";
        case VK_LCONTROL: return "LEFT CTRL";
        case VK_RCONTROL: return "RIGHT CTRL";
        case VK_LMENU: return "LEFT ALT";
        case VK_RMENU: return "RIGHT ALT";
    }
    UINT scan = MapVirtualKeyW(key, MAPVK_VK_TO_VSC);
    switch (key) {  // these share scan codes with the number pad: the extended bit picks them
        case VK_LEFT: case VK_RIGHT: case VK_UP: case VK_DOWN: case VK_PRIOR: case VK_NEXT: case VK_END:
        case VK_HOME: case VK_INSERT: case VK_DELETE: case VK_DIVIDE: case VK_NUMLOCK:
            scan |= 0x100;
    }
    wchar_t text[64];
    int n = GetKeyNameTextW(LONG(scan << 16), text, 64);
    std::string out;
    for (int i = 0; i < n; i++) out += text[i] < 128 ? char(std::toupper(int(text[i]))) : '?';
    if (out.empty()) {
        char hex[16];
        std::snprintf(hex, sizeof(hex), "KEY %02X", key);
        out = hex;
    }
    return out;
}

void StartKeyCapture() {
    g_captured = 0;
    g_capture = 1;
}
void CancelKeyCapture() { g_capture = 0; }
bool CapturingKey() { return g_capture.load() == 1; }
bool TakeCapturedKey(uint16_t& key) {
    int two = 2;
    if (!g_capture.compare_exchange_strong(two, 0)) return false;
    key = g_captured.load();
    return true;
}

void AttachWindow(void* hwnd) {
    g_hwnd = static_cast<HWND>(hwnd);
    RAWINPUTDEVICE rid{0x01, 0x02, 0, g_hwnd};  // generic desktop, mouse
    if (!RegisterRawInputDevices(&rid, 1, sizeof(rid))) LOGW("input: raw mouse input not registered");
}

bool OnWindowMessage(void* hwnd, uint32_t msg, uintptr_t wp, intptr_t lp) {
    (void)hwnd;
    switch (msg) {
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN: {
            uint16_t key = KeyOf(wp, lp);
            if (!(lp & (1 << 30)) && wp != VK_F10 && Captures(key)) return true;
            if (app::MenuOpen() || !HasBinds()) return false;
            Set(key, true);
            return wp != VK_F10 && wp != VK_F4 && wp != VK_F3;  // the overlay's key, Alt+F4 and F3 go on
        }
        case WM_KEYUP:
        case WM_SYSKEYUP:
            if (wp == VK_SHIFT) {  // both: a key up for one Shift is not always sent while the other is held
                Set(VK_LSHIFT, false);
                Set(VK_RSHIFT, false);
            }
            Set(KeyOf(wp, lp), false);
            // Alt alone would open the window's system menu on its release, and the game would stop taking keys
            return wp == VK_MENU && !app::MenuOpen() && HasBinds();
        case WM_LBUTTONDOWN:
        case WM_RBUTTONDOWN:
        case WM_MBUTTONDOWN:
        case WM_XBUTTONDOWN: {
            uint16_t key = msg == WM_LBUTTONDOWN   ? VK_LBUTTON
                           : msg == WM_RBUTTONDOWN ? VK_RBUTTON
                           : msg == WM_MBUTTONDOWN ? VK_MBUTTON
                           : GET_XBUTTON_WPARAM(wp) == XBUTTON1 ? VK_XBUTTON1 : VK_XBUTTON2;
            if (Captures(key)) return true;
            Set(key, !app::MenuOpen());
            return false;
        }
        case WM_LBUTTONUP: Set(VK_LBUTTON, false); return false;
        case WM_RBUTTONUP: Set(VK_RBUTTON, false); return false;
        case WM_MBUTTONUP: Set(VK_MBUTTON, false); return false;
        case WM_XBUTTONUP: Set(GET_XBUTTON_WPARAM(wp) == XBUTTON1 ? VK_XBUTTON1 : VK_XBUTTON2, false); return false;
        case WM_MOUSEWHEEL: {
            int notches = GET_WHEEL_DELTA_WPARAM(wp) / WHEEL_DELTA;
            if (notches && Captures(notches > 0 ? kWheelUp : kWheelDown)) return true;
            if (app::MenuOpen()) return false;
            if (notches) {
                std::lock_guard lock(g_lock);
                g_wheel_until[notches > 0 ? 0 : 1] = Clock::now() + kWheelHold;
            }
            return false;
        }
        case WM_INPUT: {
            RAWINPUT ri;
            UINT size = sizeof(ri);
            if (GetRawInputData(reinterpret_cast<HRAWINPUT>(lp), RID_INPUT, &ri, &size, sizeof(RAWINPUTHEADER)) == UINT(-1))
                return false;
            if (ri.header.dwType == RIM_TYPEMOUSE && !(ri.data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE) && Looks()) {
                std::lock_guard lock(g_lock);
                g_dx += float(ri.data.mouse.lLastX);
                g_dy += float(ri.data.mouse.lLastY);
            }
            return false;
        }
        case WM_SETCURSOR:
            Capture(Looks());
            if (Looks() && LOWORD(lp) == HTCLIENT) {
                SetCursor(nullptr);
                return true;
            }
            return false;
        case WM_ACTIVATE:
            if (LOWORD(wp) == WA_INACTIVE) ReleaseAll();
            return false;
        default:
            return false;
    }
}

void ReleaseAll() {
    {
        std::lock_guard lock(g_lock);
        std::fill(std::begin(g_held), std::end(g_held), false);
        g_wheel_until[0] = g_wheel_until[1] = {};
        g_dx = g_dy = 0;
    }
    Capture(false);
}

bool TakeMouse(float& dx, float& dy) {
    bool looks = Looks();
    Capture(looks);
    std::lock_guard lock(g_lock);
    dx = g_dx;
    dy = g_dy;
    g_dx = g_dy = 0;
    return looks && (dx != 0 || dy != 0);
}

void SetGameMenu(bool menu) { g_game_menu = menu; }

int64_t NowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count();
}

void PressMenuButtons(uint16_t buttons, uint32_t ms) {
    g_menu_buttons = buttons;
    g_menu_buttons_until = NowMs() + ms;
}

void SetMenuClick(bool presses) { g_menu_click = presses; }

bool CursorInWindow(int& x, int& y, int& width, int& height) {
    POINT p;
    RECT r;
    if (!g_hwnd || !GetCursorPos(&p) || !ScreenToClient(g_hwnd, &p) || !GetClientRect(g_hwnd, &r)) return false;
    x = p.x;
    y = p.y;
    width = r.right;
    height = r.bottom;
    return x >= 0 && y >= 0 && x < width && y < height;
}

bool MouseLooks() { return Looks(); }

// the keyboard's and the mouse's presses over port 0's controller (input.cpp's Poll)
void MergeKeyboard(int port, Pad& p) {
    if (port != 0) return;
    std::lock_guard lock(g_lock);
    if (g_binds.empty()) return;
    p.connected = true;  // the keyboard is a controller on port 0
    if (!Active() || app::MenuOpen()) return;
    Clock::time_point now = Clock::now();
    bool wheel[2] = {now < g_wheel_until[0], now < g_wheel_until[1]};
    int lx = 0, ly = 0, rx = 0, ry = 0;
    int slow = 0;  // percent of full the keys move the left stick, 0 full
    // a game menu: the mouse works it as a PC menu, left click selects (A), right click goes back (B), the wheel
    // steps up and down (the D-pad); its game bindings (fire, aim, weapon) are left out
    bool menu = g_game_menu.load();
    if (menu) {
        if (g_held[VK_LBUTTON] && g_menu_click.load()) p.analog[0] = 0xFF;
        if (NowMs() < g_menu_buttons_until.load()) p.buttons |= g_menu_buttons.load();
        if (g_held[VK_RBUTTON]) p.analog[1] = 0xFF;
        if (wheel[0]) p.buttons |= 1u << 0;
        if (wheel[1]) p.buttons |= 1u << 1;
    }
    for (const KeyBind& b : g_binds) {
        bool mouse = b.key == VK_LBUTTON || b.key == VK_RBUTTON || b.key == VK_MBUTTON || b.key == VK_XBUTTON1 ||
                     b.key == VK_XBUTTON2 || b.key >= kWheelUp;
        if (menu && mouse) continue;
        bool down = b.key == kWheelUp     ? wheel[0]
                    : b.key == kWheelDown ? wheel[1]
                    : b.key == kWheel     ? wheel[0] || wheel[1]
                                          : b.key && b.key < kKeyCount && g_held[b.key];
        if (!down) continue;
        switch (b.target) {
            case PadTarget::Button: p.buttons |= uint16_t(1u << b.index); break;
            case PadTarget::Analog: if (b.index < 8) p.analog[b.index] = 0xFF; break;
            case PadTarget::SlowWalk: slow = std::clamp(int(b.index), 1, 100); break;
            case PadTarget::LeftStick:
            case PadTarget::RightStick: {
                int& x = b.target == PadTarget::LeftStick ? lx : rx;
                int& y = b.target == PadTarget::LeftStick ? ly : ry;
                if (b.index == 0) y += 1;
                if (b.index == 1) y -= 1;
                if (b.index == 2) x -= 1;
                if (b.index == 3) x += 1;
                break;
            }
        }
    }
    auto full = [](int v, int16_t pad) { return v > 0 ? int16_t(32767) : v < 0 ? int16_t(-32768) : pad; };
    // slow: the stick that far over in any direction, a diagonal no further than straight (games read the stick's
    // length, and a dead zone takes a short one)
    float reach = 32767.0f * float(slow) / 100.0f * (lx && ly ? 0.70710678f : 1.0f);
    auto part = [reach](int v, int16_t pad) { return v ? int16_t(v > 0 ? reach : -reach) : pad; };
    p.lx = slow ? part(lx, p.lx) : full(lx, p.lx);
    p.ly = slow ? part(ly, p.ly) : full(ly, p.ly);
    p.rx = full(rx, p.rx);
    p.ry = full(ry, p.ry);
}

}  // namespace input
