#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "core/input.h"

// the host side: window, settings
namespace app {

enum class DisplayMode : uint32_t { Fullscreen, Borderless, Windowed };
inline constexpr uint32_t kFpsCapRefresh = 1;  // fps_cap: the monitor's refresh rate
inline constexpr uint32_t kBodiesAll = 0xFFFF;  // persistent_bodies: every body stays
// aspect: auto follows the output's shape; 4:3 shows the frame pillarboxed and the console reports a 4:3 TV
inline constexpr uint32_t kAspectAuto = 0, kAspect4x3 = 1, kAspect16x9 = 2;

struct Settings {
    DisplayMode display_mode = DisplayMode::Borderless;
    uint32_t width = 0, height = 0;  // 0 is the desktop mode
    uint32_t render_percent = 100;   // of the output height
    uint32_t aspect = 0;             // kAspectAuto, kAspect4x3 or kAspect16x9
    uint32_t fov = 0;                // horizontal degrees on a 4:3 frame, 0 the game's own (GameInfo::fov)
    uint32_t fps_cap = kFpsCapRefresh;  // 0 unlimited, kFpsCapRefresh the monitor's refresh rate (the default)
    bool vsync = true;
    bool hdr_output = true;          // HDR10 on a display with Windows HDR on (gpu/d3d12.cpp); SDR elsewhere
    bool fps_counter = false;        // frames a second, top left
    bool hide_hud = false;           // the 2D over the 3D view is not drawn (GameInfo::screen_2d)
    bool skip_intros = false;        // the boot movies, for a game whose hooks honour it (GameInfo::skip_intros)
    bool pause_on_focus_loss = true;  // the game and its sound wait while the window is in the background (focus_pause.cpp)
    // settings.toml's developer_mode = true alone shows the overlay's Developer section (testing aids, not for
    // players: written back only while on). god_mode lasts the session, never saved
    bool developer_mode = false;
    bool god_mode = false;
    uint32_t aniso = 0;              // 0 as the game asks
    bool smaa = false;               // SMAA over the finished frame, HUD included
    uint32_t msaa = 0;               // samples a pixel of the scene, 0 off (2, 4 or 8)
    uint32_t ambient_occlusion = 0;  // 0 off, 1 SSAO, 2 GTAO (GameInfo::ambient_occlusion)
    // mouse look: times the mouse's raw counts, each axis (GameInfo::mouse_y_ratio evens out the game's own)
    float mouse_x = 1.0f, mouse_y = 1.0f;
    bool invert_mouse_x = false, invert_mouse_y = false;
    bool aim_assist = true;  // the game's own aim help: this game's Auto Sighting option (GameInfo::aim_assist)
    bool acquisition_icon = true;  // the game's marker over a target: this game's Acquisition Icon option (GameInfo::aim_assist)
    bool auto_zoom = true;   // the game's zoom onto a target shown, with aim assist on or off (GameInfo::aim_assist)
    // with auto_zoom: the mouse's turn slowed by the zoom onto a target, zoom_sensitivity_percent of the way to keeping
    // the picture's speed on screen (the game as it was: on, 100); off, the turn keeps its speed from before the zoom
    bool zoom_sensitivity = false;
    uint32_t zoom_sensitivity_percent = 100;
    // the keyboard's movement keys (GameInfo::movement_modes): 0 the key held, 1 a press switches it on and off
    uint32_t slow_walk_mode = 0;
    uint32_t sprint_mode = 1;  // the game's own: a toggle
    uint32_t lean_mode = 1;    // a press leans, the same again stands up
    uint32_t aim_mode = 0;     // the game's own: aims while held
    // per GameInfo::key_actions, its two keys (input::KeyName codes, 0 none); filled with the defaults at load
    std::vector<std::array<uint16_t, 2>> keys;
    bool vibration = true;
    bool pad_invert_x = false, pad_invert_y = false;  // the look stick
    uint32_t ao_strength = 50;       // percent of the occlusion's darkening, 25 to 200 (50 looked right in play)
    uint32_t grass_distance = 100;   // percent of the game's own grass fade distance (GameInfo::grass_distance)
    uint32_t persistent_bodies = 0;  // dead soldiers kept: 0 the game's own, kBodiesAll every one (GameInfo::persistent_bodies)
    uint32_t weapon_tuning = 0;  // 0 the game's weapon values, 1 weapon_tuning.ini's over them (GameInfo::weapon_tuning)
    bool custom_textures = true;     // images in textures\replacement\ beside the exe replace the game's
    bool dump_textures = false;      // each texture once into textures\dumps\ beside the exe
    uint32_t texture_dump_format = 0;  // 0 PNG, 1 DDS
    uint32_t audio_output = 0;         // audio::kOutputAuto, Stereo, Surround or Mono
    std::string join_ip;                   // co-op: a host past the LAN the system link search also asks
    bool operator==(const Settings&) const = default;
};

Settings& Get();
const Settings& Defaults();  // the settings before the file loaded: the SDK's, with the game's own defaults
void Load();
void Save();
void ApplyKeys();  // Settings::keys into the input layer
void LogSettings();  // every setting, as the settings file has them
void LogSystem();    // the machine and the settings, at a run's start
void ApplyDisplay();  // any thread
struct Mode {
    uint32_t width, height;
};
Mode Desktop();
Mode Output();
float RenderScale(uint32_t guest_height);
// tangent of the fov setting's half angle over that of the game's own (GameInfo::fov); 1 on the game's
float FovScale();
// the console reports a widescreen TV and the frame is shown 16:9, else 4:3 as on a 4:3 TV: the aspect setting, or
// on auto an output 3:2 or wider
bool Widescreen();

std::filesystem::path ExeDir();
// the game in assets (game_files.cpp): its folder and this executable's xbe, from the extracted files or a disc
// image (.iso), which it mounts as D:; false after telling the player what is missing
struct GameFiles {
    std::filesystem::path dir;
    std::vector<uint8_t> xbe;
};
bool FindGame(GameFiles& game);
std::string Sha1(const std::vector<uint8_t>& data);  // lower case hex
// the recompiled code, <exe>.dll, built from the game in assets when missing or out of date (game_code.cpp) and
// loaded; false after telling the player why the game cannot start, or when they chose not to start it yet
bool PrepareGameCode(const GameFiles& game);
int Instance();  // --instance N on the command line, 0 without: a second copy on this PC for system link tests
void* Window();  // HWND
void WaitWhileUnfocused();  // focus_pause.cpp: the game thread, at its Swap
// the window's display has Windows HDR on (told by the renderer each time it looks): the overlay says when HDR Output
// cannot apply
void SetHdrDisplay(bool on);
bool HdrDisplay();
// boot splash: GameInfo::splash .bmp or .png beside the exe, up before the game window
bool OpenSplash();        // window thread, at launch; false without one, and the game window opens at once
void SplashFirstFrame();  // the game's first Swap: waits out the splash's minimum, then opens the game window
void CloseSplash();       // window thread
void OpenGameWindow(bool apply = true);  // any thread but the window thread's; returns once the window is up
// relaunched: the previous instance keeps its window up until this one has shown its first frames (handover.cpp)
bool StartHandover();   // window thread, at launch; true: the game window starts hidden
bool HandingOver();
void FinishHandover();  // any thread but the window thread's: opens the game window, lets the old instance go

// held around ImGui: the window thread's messages and the renderer's frame
std::recursive_mutex& UiLock();

// F10 settings overlay (menu.cpp): while open the game's controllers read idle. Its text is kMenuFontPx on a 720
// line frame, scaled with the frame's height
inline constexpr float kMenuFontPx = 13.0f;
bool MenuOpen();
void ToggleMenu();
void DrawMenu(double fps);  // each frame inside the ImGui frame, UiLock held; also the FPS counter

// what the game repo defines once
struct Option {  // a setting in the settings file, flag or value
    const char* key;
    bool* flag;
    uint32_t* value;
};
struct GameInfo {
    const wchar_t* title;        // window and splash
    const wchar_t* short_title;  // message boxes
    const wchar_t* name;         // window classes, crash dumps, handover event
    const char* log;             // log prefix
    const char* settings;        // settings file beside the exe
    const wchar_t* splash;       // .bmp or .png beside the exe
    const wchar_t* missing;      // message when neither the game files nor a disc image are found
    std::vector<std::pair<const char*, const char*>> images;  // xbe file name, executable name
    std::vector<Option> options;
    // the game's horizontal field of view in degrees, for the overlay's Field of View; 0: the game has no such
    // setting (its hooks apply app::FovScale)
    float fov = 0;
    // the HUD, menus and text are screen space (XYZRHW) 2D made for 4:3: kept so on a 16:9 frame, and hideable
    // (gpu/screen2d.h)
    bool screen_2d = false;
    bool skip_intros = false;  // the game's hooks skip its boot movies with Settings::skip_intros: the overlay offers it
    // the highest frame rate the game's timing holds up at, 0 for none: Pace and the overlay keep under it
    uint32_t max_fps = 0;
    bool ambient_occlusion = false;  // the game's hooks call d3d::Occlusion: the overlay offers it
    // keyboard and mouse on controller port 0: the overlay's keyboard list with its defaults (core/input.h); empty:
    // none
    std::vector<input::KeyAction> key_actions;
    float mouse_y_ratio = 1.0f;  // vertical mouse look over horizontal, for the game's own difference between them
    bool aim_assist = false;     // the game's hooks can turn its aim assist off (Settings::aim_assist): the overlay offers it
    bool movement_modes = false;  // the game side honours Settings::sprint_mode and lean_mode: the overlay offers them
    bool grass_distance = false;  // the game side honours Settings::grass_distance: the overlay offers it
    bool persistent_bodies = false;  // the game side honours Settings::persistent_bodies: the overlay offers it
    bool weapon_tuning = false;  // the game side merges weapon_tuning.ini with Settings::weapon_tuning: the overlay offers it
    // a radial menu (the weapon wheel) opened by holding the key that presses this analog button (0 A ... 3 Y), -1
    // none: while it is held the mouse points the left stick instead of looking around
    int radial_menu_analog = -1;
    // a file of the game's disc (its path there, as asked for) the game changes: true when it did. Asked once a file,
    // the first time it is opened; the result is kept in overrides\ beside the exe and read instead (kernel/files.cpp)
    bool (*patch_file)(const char* disc_path, std::vector<uint8_t>& data) = nullptr;
    // the settings file's toml::table, for what options cannot hold
    void (*load)(const void* table);
    void (*save)(void* table);
    void (*init)();                // after the settings load, before the window and boot
    void (*loaded)();              // image in memory, before its entry runs
    void (*window)(void* hwnd);    // the game window is up, before boot
    // window thread, each message after ImGui's; true returns result instead of the host's handling
    bool (*message)(void* hwnd, uint32_t msg, uintptr_t wp, intptr_t lp, intptr_t* result);
    void (*ui)(double fps);        // each frame inside the ImGui frame, UiLock held
    void (*pad)(int port, input::Pad& pad);  // a polled pad before the game gets it
    void (*rumble)(int port, uint16_t& left, uint16_t& right);
    // xbe file name, SHA-1: other releases the executable runs, each from its own [xbe] named config
    std::vector<std::pair<const char*, const char*>> releases;
};
extern const GameInfo kGame;

// sleeps out the rest of the frame for the fps cap
void Pace();

}  // namespace app
