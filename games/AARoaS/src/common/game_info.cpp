#include "app/app.h"

// The game draws 4:3 only, whatever the console reports: pillarboxed by default rather than stretched. The settings
// file's aspect still wins, it loads after this.
static const bool g_aspect_default = (app::Get().aspect = app::kAspect4x3, true);

void PatchUserIni();  // mouse.cpp: the game's mouse acceleration and smoothing off
bool PatchGameText(const char* path, std::vector<uint8_t>& data);  // text.cpp

// America's Army: Rise of a Soldier (Xbox, USA). XDK 5849, Unreal Engine 2 based.
const app::GameInfo app::kGame = {
    .title = L"America's Army: Rise of a Soldier",
    .short_title = L"America's Army",
    .name = L"AARoaS",
    .log = "aaroas",
    .settings = "settings.toml",
    .splash = L"splash",
    .missing = L"Put America's Army: Rise of a Soldier in the assets folder: its disc image (.iso), or default.xbe "
               L"with its folders (MapsXBoxL, System, TexturesXBoxW, ...) beside it.",
    .images = {{"default.xbe", "AARoaS"}},
    .options = {},
    .fov = 75,  // the player camera's angle (widescreen.cpp)
    .screen_2d = true,
    .skip_intros = true,  // intro.cpp
    .max_fps = 60,        // weapon recoil is added each frame: above 60 FPS it throws the view
    .ambient_occlusion = true,  // widescreen.cpp
    // the controller as xboxuser.ini binds it: A Action, B _Command, X Reload, Y _Weapon, Black _DisplayMap,
    // White AltFire, LT TweakLock, RT Fire, Start ShowMenu, left thumb Sprint, right thumb Zoom. Mouse look: mouse.cpp
    // the user's own layout (2026-11-12), the defaults. Virtual keys: 0x01 left mouse, 0x02 right, 0x04 middle,
    // 0x0D Enter, 0x08 Backspace, 0x09 Tab, 0x1B Escape, 0x20 Space, 0x25-0x28 arrows (left, up, right, down),
    // 0xA0 left Shift
    .key_actions = {{"Move Forward", input::PadTarget::LeftStick, 0, {'W', 0}},
                    {"Move Back", input::PadTarget::LeftStick, 1, {'S', 0}},
                    {"Move Left", input::PadTarget::LeftStick, 2, {'A', 0}},
                    {"Move Right", input::PadTarget::LeftStick, 3, {'D', 0}},
                    {"Fire", input::PadTarget::Analog, 7, {0x01, 0}},
                    {"Aim / Zoom", input::PadTarget::Button, 7, {input::kWheel, 0}},
                    {"Action / Select", input::PadTarget::Analog, 0, {'F', 0x0D}},
                    {"Command / Back", input::PadTarget::Analog, 1, {'G', 0x08}},
                    {"Reload", input::PadTarget::Analog, 2, {'R', 0}},
                    {"Switch Weapon", input::PadTarget::Analog, 3, {0x04, 0}},
                    {"Alt Fire", input::PadTarget::Analog, 5, {'Z', 0}},
                    {"Map", input::PadTarget::Analog, 4, {0x09, 0}},
                    {"Sprint", input::PadTarget::Button, 6, {0xA0, 0}},
                    {"Slow Walk", input::PadTarget::SlowWalk, 50, {0x20, 0}},  // the stick half over (35% is in the dead zone)
                    {"Tweak Lock", input::PadTarget::Analog, 6, {0x02, 0}},
                    {"D-Pad Up", input::PadTarget::Button, 0, {'C', 0x26}},
                    {"D-Pad Down", input::PadTarget::Button, 1, {'X', 0x28}},
                    {"D-Pad Left", input::PadTarget::Button, 2, {'Q', 0x25}},
                    {"D-Pad Right", input::PadTarget::Button, 3, {'E', 0x27}},
                    {"Pause Menu", input::PadTarget::Button, 4, {0x1B, 0}}},
    .mouse_y_ratio = 1.0f,  // with acceleration off (mouse.cpp) the game turns and tilts alike
    .aim_assist = true,     // aim_assist.cpp
    .radial_menu_analog = 3,  // the weapon wheel: Y held (Switch Weapon), picked with the left stick, the mouse here
    .patch_file = PatchGameText,  // text.cpp: the menus' text names keys, not buttons
    .load = nullptr,
    .save = nullptr,
    .init = PatchUserIni,  // mouse.cpp
    .loaded = nullptr,
    .window = nullptr,
    .message = nullptr,
    .ui = nullptr,
    .pad = nullptr,
    .rumble = nullptr,
};
