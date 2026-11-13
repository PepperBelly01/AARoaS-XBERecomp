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
#include <cmath>
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
// a radial menu open: the mouse's way, held to kRadialReach long, so turning back swings it straight over
float g_radial_x = 0, g_radial_y = 0;
constexpr float kRadialReach = 120, kRadialDead = 25;  // raw counts
std::vector<KeyBind> g_binds;
std::atomic<bool> g_game_menu{false};
std::atomic<uint16_t> g_menu_buttons{0};              // held for the game side's cursor ...
std::atomic<int64_t> g_menu_buttons_until{0};         // ... until then (steady clock, ms)
std::atomic<bool> g_menu_click{true};                 // a left click presses A
std::atomic<bool> g_enter_start{false}, g_escape_start{false};  // SetMenuStart
std::atomic<int> g_menu_escapes{0};  // Escape pressed in a game menu, not yet taken (TakeMenuEscape)
HWND g_hwnd = nullptr;
// rebinding: 1 waiting for a key, 2 a key taken
std::atomic<int> g_capture{0};
std::atomic<uint16_t> g_captured{0};

bool HasBinds() {
    std::lock_guard lock(g_lock);
    return !g_binds.empty();
}

bool Active() { return g_hwnd && GetForegroundWindow() == g_hwnd; }

// a radial menu held open (GameInfo::radial_menu_analog: the button its key presses, held): the mouse steers it
bool RadialOpen() {
    int index = app::kGame.radial_menu_analog;
    if (index < 0 || app::MenuOpen() || g_game_menu.load()) return false;
    for (const KeyBind& b : g_binds)
        if (b.target == PadTarget::Analog && b.index == index && b.key && b.key < kWheelUp && g_held[b.key]) return true;
    return false;
}

bool Looks() { return Active() && !app::MenuOpen() && !g_game_menu.load() && HasBinds() && !RadialOpen(); }

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
            if (wp != VK_F10 && wp != VK_F9) NoteInput(false);  // the keyboard used: its icons
            if (wp == VK_ESCAPE && !(lp & (1 << 30)) && g_game_menu.load() && !app::MenuOpen()) g_menu_escapes++;
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
            NoteInput(false);
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
            if (ri.header.dwType == RIM_TYPEMOUSE && !(ri.data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE)) {
                float mx = float(ri.data.mouse.lLastX), my = float(ri.data.mouse.lLastY);
                if (std::abs(mx) + std::abs(my) > 3) NoteInput(false);  // the mouse moved, not a twitch
                if (Looks()) {
                    std::lock_guard lock(g_lock);
                    g_dx += mx;
                    g_dy += my;
                } else if (Active() && RadialOpen()) {
                    std::lock_guard lock(g_lock);
                    g_radial_x += mx;
                    g_radial_y += my;
                    float length = std::sqrt(g_radial_x * g_radial_x + g_radial_y * g_radial_y);
                    if (length > kRadialReach) {
                        g_radial_x *= kRadialReach / length;
                        g_radial_y *= kRadialReach / length;
                    }
                }
            }
            return false;
        }
        case WM_SETCURSOR:
            Capture(Looks() || (Active() && RadialOpen()));
            if ((Looks() || (Active() && RadialOpen())) && LOWORD(lp) == HTCLIENT) {
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
    Capture(looks || (Active() && RadialOpen()));  // a radial menu open: still hidden and held
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

void SetMenuStart(bool enter, bool escape) {
    g_enter_start = enter;
    g_escape_start = escape;
}

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

namespace {
std::atomic<bool> g_using_pad{false};
}
bool UsingPad() { return g_using_pad.load(); }
bool ActionHeld(uint8_t index) {
    if (!Looks()) return false;
    std::lock_guard lock(g_lock);
    for (const KeyBind& b : g_binds)
        if (b.target == PadTarget::Game && b.index == index && b.key && b.key < kWheelUp && g_held[b.key]) return true;
    return false;
}
bool TakeMenuEscape() { return g_menu_escapes.exchange(0) > 0; }
void NoteInput(bool pad) {
    if (g_using_pad.exchange(pad) && !pad) StopRumble();  // to the keyboard: the controller's motors stopped
}

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
    // The layouts switch (a menu opened or closed): keys held then count only once let go, so Escape that opened the
    // pause menu (Start in play) does not close it again at once (Back in menus)
    static bool was_menu = false;
    static bool stale[kKeyCount] = {};
    if (menu != was_menu) {
        std::copy(std::begin(g_held), std::end(g_held), std::begin(stale));
        was_menu = menu;
    }
    for (size_t k = 0; k < kKeyCount; k++)
        if (!g_held[k]) stale[k] = false;
    auto held = [&](uint16_t k) { return g_held[k] && !stale[k]; };
    // Game menus have their own keys, the same for everyone, so the icons can name them (GameInfo::key_actions are
    // play's): A Enter, Space or a click on an item; B Escape, Backspace or right click; X and Y their letters; the
    // D-pad the arrows and the wheel; the shoulders (pages, tabs) Q and E
    if (menu) {
        bool accept = (held(VK_LBUTTON) && g_menu_click.load()) || held(VK_RETURN) || held(VK_SPACE);

        if (accept) p.analog[0] = 0xFF;
        if (accept && g_enter_start.load()) p.buttons |= 1u << 4;  // Start, on a page that takes only it
        if (held(VK_RBUTTON) || held(VK_ESCAPE) || held(VK_BACK)) p.analog[1] = 0xFF;
        if (held('X')) p.analog[2] = 0xFF;
        if (held('Y')) p.analog[3] = 0xFF;
        if (held('Q')) p.analog[6] = 0xFF;
        if (held('E')) p.analog[7] = 0xFF;
        if (held(VK_UP) || wheel[0]) p.buttons |= 1u << 0;
        if (held(VK_DOWN) || wheel[1]) p.buttons |= 1u << 1;
        if (held(VK_LEFT)) p.buttons |= 1u << 2;
        if (held(VK_RIGHT)) p.buttons |= 1u << 3;
        if (NowMs() < g_menu_buttons_until.load()) p.buttons |= g_menu_buttons.load();
        return;
    }
    // a radial menu open: the left stick points the mouse's way (the movement keys left out); closed, forgotten
    bool radial = !menu && RadialOpen();
    if (!radial) g_radial_x = g_radial_y = 0;
    for (const KeyBind& b : g_binds) {
        if (radial && b.target == PadTarget::LeftStick) continue;
        bool mouse = b.key == VK_LBUTTON || b.key == VK_RBUTTON || b.key == VK_MBUTTON || b.key == VK_XBUTTON1 ||
                     b.key == VK_XBUTTON2 || b.key >= kWheelUp;
        if (menu && mouse) continue;
        bool down = b.key == kWheelUp     ? wheel[0]
                    : b.key == kWheelDown ? wheel[1]
                    : b.key == kWheel     ? wheel[0] || wheel[1]
                                          : b.key && b.key < kKeyCount && held(b.key);
        if (!down) continue;
        switch (b.target) {
            case PadTarget::Button: p.buttons |= uint16_t(1u << b.index); break;
            case PadTarget::Analog: if (b.index < 8) p.analog[b.index] = 0xFF; break;
            case PadTarget::SlowWalk: slow = std::clamp(int(b.index), 1, 100); break;
            case PadTarget::Game: break;  // ActionHeld
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
    float length = std::sqrt(g_radial_x * g_radial_x + g_radial_y * g_radial_y);
    if (radial && length > kRadialDead) {
        p.lx = int16_t(std::clamp(g_radial_x / length * 32767.0f, -32767.0f, 32767.0f));
        p.ly = int16_t(std::clamp(-g_radial_y / length * 32767.0f, -32767.0f, 32767.0f));  // mouse up, stick up
    }
}

}  // namespace input
