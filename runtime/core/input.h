#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace input {

// the console controller's report, analog buttons are 0-255
struct Pad {
    bool connected = false;
    uint16_t buttons = 0;  // dpad up/down/left/right, start, back, left thumb, right thumb
    uint8_t analog[8] = {};  // A B X Y black white left trigger right trigger
    int16_t lx = 0, ly = 0, rx = 0, ry = 0;
};

void Init();  // window thread, before boot; the game code window starts it earlier
uint16_t HeldOnAnyController();  // face buttons as XINPUT_GAMEPAD_A/B/X/Y masks, GameInfo::pad left out
// the controller on that port, then GameInfo::pad
Pad Poll(int port);
void Rumble(int port, uint16_t left, uint16_t right);
void StopRumble();  // every controller's motors off

// Keyboard and mouse (keyboard.cpp). Keys and mouse buttons press the controller on port 0 through the game's
// bindings (GameInfo::key_actions, Settings::keys); the mouse's raw movement is taken by the game side for mouse look.
// What a key presses on the console controller
enum class PadTarget : uint8_t {
    Button,      // index: Pad::buttons bit (dpad up, down, left, right, start, back, left thumb, right thumb)
    Analog,      // index: Pad::analog (A B X Y black white, left trigger, right trigger)
    LeftStick,   // index: 0 up, 1 down, 2 left, 3 right, full over
    RightStick,
    SlowWalk,    // held: the keys move the left stick only partway (index: percent of full)
    Game,        // nothing on the pad: the game side asks ActionHeld(index)
};
// keys are Windows virtual keys; mouse buttons VK_LBUTTON, VK_RBUTTON, VK_MBUTTON, VK_XBUTTON1, VK_XBUTTON2; the
// wheel kWheelUp and kWheelDown, or kWheel either way, held for a moment a notch; 0 is no key
constexpr uint16_t kWheelUp = 0x100, kWheelDown = 0x101, kWheel = 0x102, kKeyCount = 0x103;
struct KeyBind {
    uint16_t key;
    PadTarget target;
    uint8_t index;
};
// a row of the overlay's keyboard list: what it presses and its two default keys (GameInfo::key_actions)
struct KeyAction {
    const char* name;  // shown, and its settings file key in lower case with underscores
    PadTarget target;
    uint8_t index;
    uint16_t keys[2];
};

void SetKeys(const std::vector<KeyBind>& binds);  // the bindings in use, any thread
std::string KeyName(uint16_t key);                // upper case, "" for 0
// rebinding (window thread): the next key, mouse button or wheel notch is kept instead of pressing anything.
// Escape cancels
void StartKeyCapture();
void CancelKeyCapture();
bool CapturingKey();
bool TakeCapturedKey(uint16_t& key);

// window thread
void AttachWindow(void* hwnd);  // raw mouse input from the game window
// a window message: true when it was the keyboard's or the mouse's and is taken
bool OnWindowMessage(void* hwnd, uint32_t msg, uintptr_t wp, intptr_t lp);
void ReleaseAll();  // focus lost: nothing held
// raw mouse counts since the last call, while the mouse looks; false when there are none
bool TakeMouse(float& dx, float& dy);
// the game side says a game menu has the input: the cursor is free and the mouse does not look
void SetGameMenu(bool menu);
// a game menu, for the game side's cursor: Pad::buttons bits held on port 0 for ms (a step of the selection), and
// whether a left click presses A now (false while the cursor is off the menu's items)
void PressMenuButtons(uint16_t buttons, uint32_t ms);
// the same for Pad::analog buttons (A B X Y black white, the triggers), a bit each: a menu's on-screen prompt clicked
void PressMenuAnalog(uint8_t analogs, uint32_t ms);
void SetMenuClick(bool presses);
// a game menu that takes Start: Enter (and a click) press it too on a page with no list (a "press start" page); escape
// is not used (Escape is B in menus; the game side presses Start when B left the pause menu's page as it was)
void SetMenuStart(bool enter, bool escape);
// Escape was pressed in a game menu since the last call (it pressed B)
bool TakeMenuEscape();
// which was used last, the controller or the keyboard and mouse: for on-screen button icons
bool UsingPad();
// a PadTarget::Game action's key held, in play only (not in a game menu or the overlay)
bool ActionHeld(uint8_t index);
void NoteInput(bool pad);  // input.cpp and keyboard.cpp, as either is used
// the cursor in the window's client area, false while it is outside the window
bool CursorInWindow(int& x, int& y, int& width, int& height);
// the mouse looks (captured: hidden and held in the window)
bool MouseLooks();

}  // namespace input
