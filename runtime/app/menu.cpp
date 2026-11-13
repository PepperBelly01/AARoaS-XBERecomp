// The F10 settings overlay: a panel with three tabs (Recomp Settings, Mouse and Keyboard, Controller), each a list of
// rows that step through their values with the arrows, and key rows with two keys apiece. Changes are made to a copy
// and take hold with Apply; Back, Escape or F10 close it without them. Defaults resets the tab shown.
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
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include <imgui.h>

#include "app/app.h"
#include "audio/audio.h"
#include "core/input.h"

namespace app {

namespace {

std::atomic<bool> g_open{false};
Settings g_draft;          // what the overlay shows and changes; Apply makes it the settings
int g_tab = 0;             // 0 Recomp Settings, 1 Mouse and Keyboard, 2 Controller
int g_bind_row = -1;       // the key row and slot waiting for a key
int g_bind_slot = 0;
const char* g_hint = "";   // the row under the mouse or the keyboard's, shown under the list
std::vector<Mode> g_modes;  // read when the overlay opens: a mode list takes a moment to enumerate

constexpr ImU32 kPanel = IM_COL32(16, 18, 14, 228);
constexpr ImU32 kBorder = IM_COL32(196, 196, 158, 255);
constexpr ImU32 kText = IM_COL32(226, 226, 220, 255);
constexpr ImU32 kDim = IM_COL32(150, 152, 140, 255);
constexpr ImU32 kHeader = IM_COL32(208, 198, 142, 255);
constexpr ImU32 kLine = IM_COL32(86, 92, 76, 255);
constexpr ImU32 kTabOn = IM_COL32(104, 116, 82, 255);
constexpr ImU32 kTabOff = IM_COL32(38, 44, 32, 255);
constexpr ImU32 kTabHover = IM_COL32(64, 74, 52, 255);
constexpr ImU32 kRowOn = IM_COL32(255, 255, 255, 22);
constexpr ImU32 kArrowBox = IM_COL32(44, 50, 38, 255);
constexpr ImU32 kArrowHover = IM_COL32(86, 98, 68, 255);
constexpr ImU32 kSlot = IM_COL32(56, 66, 44, 255);
constexpr ImU32 kSlotOn = IM_COL32(88, 102, 68, 255);
constexpr ImU32 kButton = IM_COL32(66, 80, 52, 255);
constexpr ImU32 kButtonOn = IM_COL32(96, 112, 74, 255);

struct Choice {
    uint32_t value;
    std::string label;
};

std::string Upper(std::string s) {
    for (char& c : s) c = char(std::toupper(uint8_t(c)));
    return s;
}

// the window's monitor's modes, widest first, 640x480 and up, each size once
std::vector<Mode> Resolutions() {
    HMONITOR mon = MonitorFromWindow(static_cast<HWND>(Window()), MONITOR_DEFAULTTOPRIMARY);
    MONITORINFOEXW mi{};
    mi.cbSize = sizeof(mi);
    std::vector<Mode> out;
    if (!GetMonitorInfoW(mon, &mi)) return out;
    DEVMODEW dm{};
    dm.dmSize = sizeof(dm);
    for (DWORD i = 0; EnumDisplaySettingsW(mi.szDevice, i, &dm); i++) {
        if (dm.dmPelsWidth < 640 || dm.dmPelsHeight < 480) continue;
        Mode m{dm.dmPelsWidth, dm.dmPelsHeight};
        bool seen = std::any_of(out.begin(), out.end(), [&](const Mode& o) { return o.width == m.width && o.height == m.height; });
        if (!seen) out.push_back(m);
    }
    std::sort(out.begin(), out.end(), [](const Mode& a, const Mode& b) {
        return a.width != b.width ? a.width > b.width : a.height > b.height;
    });
    return out;
}

float Em() { return ImGui::GetFontSize(); }
float RowHeight() { return std::round(Em() * 1.65f); }

void Text(ImDrawList* dl, ImVec2 at, ImU32 color, const std::string& s) { dl->AddText(at, color, s.c_str()); }

void CenteredText(ImDrawList* dl, ImVec2 a, ImVec2 b, ImU32 color, const std::string& s) {
    ImVec2 size = ImGui::CalcTextSize(s.c_str());
    dl->AddText({std::round((a.x + b.x - size.x) * 0.5f), std::round((a.y + b.y - size.y) * 0.5f)}, color, s.c_str());
}

void Section(const char* name) {
    ImGui::Dummy({0, Em() * 0.5f});
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    float w = ImGui::GetContentRegionAvail().x;
    float h = Em() * 1.5f;
    Text(dl, {p.x, p.y + (h - Em()) * 0.5f}, kHeader, Upper(name));
    dl->AddLine({p.x, p.y + h}, {p.x + w, p.y + h}, kLine, 1.0f);
    ImGui::Dummy({w, h + Em() * 0.3f});
}

// "(?)" after a row's label, its description in a tooltip under the mouse (it is also shown under the list for the
// row the keyboard or controller is on)
void HelpMark(const char* label, const char* hint, ImVec2 row, float h) {
    if (!hint || !*hint) return;
    ImVec2 size = ImGui::CalcTextSize(Upper(label).c_str());
    ImVec2 at{row.x + size.x + Em() * 0.5f, row.y + (h - Em()) * 0.5f};
    ImVec2 mark = ImGui::CalcTextSize("(?)");
    bool over = ImGui::IsMouseHoveringRect(at, {at.x + mark.x, at.y + mark.y}, false) && ImGui::IsWindowHovered();
    ImGui::GetWindowDrawList()->AddText(at, over ? kHeader : kDim, "(?)");
    if (!over) return;
    ImGui::PushStyleColor(ImGuiCol_PopupBg, ImGui::ColorConvertU32ToFloat4(IM_COL32(24, 27, 20, 245)));
    ImGui::PushStyleColor(ImGuiCol_Border, ImGui::ColorConvertU32ToFloat4(kBorder));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {Em() * 0.6f, Em() * 0.45f});
    ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 1.0f);
    ImGui::BeginTooltip();
    ImGui::PushTextWrapPos(Em() * 24);
    ImGui::TextUnformatted(hint);
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);
}

// an arrow box, pointing left or right; true when clicked
bool Arrow(const char* id, ImVec2 at, float size, bool left) {
    ImGui::SetCursorScreenPos(at);
    ImGui::PushItemFlag(ImGuiItemFlags_NoNav, true);
    bool clicked = ImGui::InvisibleButton(id, {size, size});
    ImGui::PopItemFlag();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(at, {at.x + size, at.y + size}, ImGui::IsItemHovered() ? kArrowHover : kArrowBox);
    float m = size * 0.3f, cy = at.y + size * 0.5f;
    if (left) dl->AddTriangleFilled({at.x + size - m, at.y + m}, {at.x + size - m, at.y + size - m}, {at.x + m, cy}, kText);
    else dl->AddTriangleFilled({at.x + m, at.y + m}, {at.x + m, at.y + size - m}, {at.x + size - m, cy}, kText);
    return clicked;
}

// a row with a value between arrows: -1 or +1 when stepped (the arrows, Left and Right on the row, a click or Enter on
// it steps forward), else 0
int Stepper(const char* label, const std::string& value, const char* hint) {
    ImGui::PushID(label);
    float w = ImGui::GetContentRegionAvail().x, h = RowHeight();
    ImVec2 p = ImGui::GetCursorScreenPos();
    int step = 0;
    if (ImGui::Selectable("##row", false, ImGuiSelectableFlags_AllowOverlap, {w, h})) step = 1;
    bool focused = ImGui::IsItemFocused();
    if (ImGui::IsItemHovered() || focused) g_hint = hint;
    if (focused && ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) step = -1;
    if (focused && ImGui::IsKeyPressed(ImGuiKey_RightArrow)) step = 1;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    Text(dl, {p.x + Em() * 0.15f, p.y + (h - Em()) * 0.5f}, kText, Upper(label));
    HelpMark(label, hint, {p.x + Em() * 0.15f, p.y}, h);
    float box = std::round(h * 0.78f), top = p.y + (h - box) * 0.5f;
    float right = p.x + w - Em() * 0.15f, left = p.x + w * 0.665f;
    ImVec2 back = ImGui::GetCursorScreenPos();
    if (Arrow("##left", {left, top}, box, true)) step = -1;
    if (Arrow("##right", {right - box, top}, box, false)) step = 1;
    CenteredText(dl, {left + box, p.y}, {right - box, p.y + h}, kText, Upper(value));
    ImGui::SetCursorScreenPos(back);
    ImGui::PopID();
    return step;
}

// steps through choices, at the ends it stops; true when the value changed
bool Cycle(const char* label, uint32_t& value, const std::vector<Choice>& choices, const char* hint) {
    size_t at = 0;
    for (size_t i = 0; i < choices.size(); i++)
        if (choices[i].value == value) at = i;
    int step = Stepper(label, choices.empty() ? "" : choices[at].label, hint);
    if (!step || choices.empty()) return false;
    size_t next = step < 0 ? (at ? at - 1 : 0) : std::min(at + 1, choices.size() - 1);
    if (next == at) return false;
    value = choices[next].value;
    return true;
}

void Toggle(const char* label, bool& value, const char* hint) {
    if (Stepper(label, value ? "On" : "Off", hint)) value = !value;
}

void Slider(const char* label, float& value, float lo, float hi, float step, const char* hint) {
    char text[32];
    std::snprintf(text, sizeof(text), "%.1f", value);
    if (int s = Stepper(label, text, hint)) value = std::clamp(std::round((value + s * step) / step) * step, lo, hi);
}

// a key row: the action and its two keys; a click on a key waits for the next press, a right click or Delete clears
void KeyRow(int row) {
    const input::KeyAction& a = kGame.key_actions[size_t(row)];
    ImGui::PushID(row);
    float w = ImGui::GetContentRegionAvail().x, h = RowHeight();
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    Text(dl, {p.x + Em() * 0.15f, p.y + (h - Em()) * 0.5f}, kText, Upper(a.name));
    float slot_w = w * 0.215f, gap = Em() * 0.35f, x = p.x + w * 0.53f;
    for (int s = 0; s < 2; s++) {
        ImVec2 at{x + s * (slot_w + gap), p.y + h * 0.08f};
        ImVec2 size{slot_w, h * 0.84f};
        ImGui::SetCursorScreenPos(at);
        ImGui::PushID(s);
        bool waiting = g_bind_row == row && g_bind_slot == s;
        if (ImGui::InvisibleButton("##slot", size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight)) {
            if (ImGui::IsMouseReleased(ImGuiMouseButton_Right)) {
                g_draft.keys[size_t(row)][size_t(s)] = 0;
            } else {
                g_bind_row = row;
                g_bind_slot = s;
                input::StartKeyCapture();
            }
        }
        bool on = ImGui::IsItemHovered() || ImGui::IsItemFocused() || waiting;
        if (ImGui::IsItemHovered() || ImGui::IsItemFocused())
            g_hint = "Click or Enter, then press a key or mouse button to bind it. Right click or Delete clears it. "
                     "Escape cancels.";
        if (ImGui::IsItemFocused() && !waiting && ImGui::IsKeyPressed(ImGuiKey_Delete)) g_draft.keys[size_t(row)][size_t(s)] = 0;
        dl->AddRectFilled(at, {at.x + size.x, at.y + size.y}, on ? kSlotOn : kSlot);
        std::string name = waiting ? "PRESS A KEY" : input::KeyName(g_draft.keys[size_t(row)][size_t(s)]);
        CenteredText(dl, at, {at.x + size.x, at.y + size.y}, waiting ? kHeader : kText, name);
        ImGui::PopID();
    }
    ImGui::SetCursorScreenPos(p);
    ImGui::Dummy({w, h});
    ImGui::PopID();
}

// a key pressed for the waiting slot: taken from any other slot that had it
void FinishBinding() {
    if (g_bind_row < 0) return;
    uint16_t key = 0;
    if (input::TakeCapturedKey(key)) {
        if (key == input::kWheelUp || key == input::kWheelDown) {
            // the wheel either way unless the other way is bound to something else
            bool split = false;
            uint16_t other = key == input::kWheelUp ? input::kWheelDown : input::kWheelUp;
            for (auto& k : g_draft.keys) split |= k[0] == other || k[1] == other;
            if (!split) key = input::kWheel;
        }
        for (auto& k : g_draft.keys)
            for (uint16_t& slot : k)
                if (slot == key) slot = 0;
        g_draft.keys[size_t(g_bind_row)][size_t(g_bind_slot)] = key;
        g_bind_row = -1;
    } else if (!input::CapturingKey()) {
        g_bind_row = -1;  // cancelled
    }
}

void DrawRecomp() {
    Settings& s = g_draft;
    Section("Display");
    uint32_t mode = uint32_t(s.display_mode);
    if (Cycle("Display Mode", mode, {{uint32_t(DisplayMode::Fullscreen), "Fullscreen"},
                                      {uint32_t(DisplayMode::Borderless), "Borderless"},
                                      {uint32_t(DisplayMode::Windowed), "Windowed"}},
              "Borderless fills the monitor without changing its mode; Fullscreen changes the monitor to the "
              "resolution."))
        s.display_mode = DisplayMode(mode);

    // resolutions as one number each: their index in g_modes, past it the desktop
    Mode desktop = Desktop();
    std::vector<Choice> sizes;
    char text[64];
    std::snprintf(text, sizeof(text), "Desktop (%ux%u)", desktop.width, desktop.height);
    sizes.push_back({0, text});
    uint32_t current = 0;
    for (size_t i = 0; i < g_modes.size(); i++) {
        std::snprintf(text, sizeof(text), "%ux%u", g_modes[i].width, g_modes[i].height);
        sizes.push_back({uint32_t(i + 1), text});
        if (s.width == g_modes[i].width && s.height == g_modes[i].height) current = uint32_t(i + 1);
    }
    if (Cycle("Resolution", current, sizes,
              "Borderless always fills the monitor; there the resolution sets how sharp the picture is drawn.")) {
        s.width = current ? g_modes[current - 1].width : 0;
        s.height = current ? g_modes[current - 1].height : 0;
    }

    std::vector<Choice> scales;
    for (uint32_t p : {50u, 75u, 100u, 125u, 150u, 175u, 200u}) scales.push_back({p, std::to_string(p) + "%"});
    Cycle("Render Resolution", s.render_percent, scales,
          "Draws the game at this share of the output resolution. Above 100% is supersampling: sharper, slower.");

    uint32_t aspect = s.aspect == kAspectAuto ? (Widescreen() ? kAspect16x9 : kAspect4x3) : s.aspect;
    if (Cycle("Aspect Ratio", aspect, {{kAspect4x3, "4:3 (Original)"}, {kAspect16x9, "16:9"}},
              "16:9 shows more to the sides; the HUD and menus stay 4:3 in the middle."))
        s.aspect = aspect;

    if (kGame.fov > 0) {
        std::vector<Choice> angles;
        std::snprintf(text, sizeof(text), "%.0f (Default)", kGame.fov);
        angles.push_back({0, text});
        for (uint32_t a = 60; a <= 120; a += 5)
            if (a != uint32_t(kGame.fov + 0.5f)) angles.push_back({a, std::to_string(a)});
        std::sort(angles.begin(), angles.end(), [](const Choice& a, const Choice& b) {
            return (a.value ? a.value : uint32_t(kGame.fov + 0.5f)) < (b.value ? b.value : uint32_t(kGame.fov + 0.5f));
        });
        Cycle("Field of View", s.fov, angles,
              "Horizontal degrees as on a 4:3 screen; 16:9 adds to the sides. Aiming zooms in from it as usual.");
    }

    Toggle("Vertical Sync", s.vsync, "Waits for the monitor's refresh: no tearing, a little more latency.");
    std::vector<Choice> caps = {{kFpsCapRefresh, "Refresh Rate"}};
    for (uint32_t f : {30u, 60u, 75u, 90u, 120u, 144u, 165u, 240u})
        if (!kGame.max_fps || f <= kGame.max_fps) caps.push_back({f, std::to_string(f)});
    if (!kGame.max_fps) caps.push_back({0, "Unlimited"});
    Cycle("Max Frame Rate", s.fps_cap, caps,
          kGame.max_fps ? "This game's timing (recoil and more) only holds up to 60 FPS for now." : "");
    Toggle("FPS Counter", s.fps_counter, "Frames a second, top left.");
    if (kGame.screen_2d)
        Toggle("Hide HUD", s.hide_hud,
               "Hides everything drawn flat over the 3D view in play, subtitles and the pause menu included. Menu "
               "screens still show.");

    Section("Graphics");
    Cycle("Anisotropic Filtering", s.aniso, {{0, "Default"}, {2, "2x"}, {4, "4x"}, {8, "8x"}, {16, "16x"}},
          "Sharper textures on surfaces seen at an angle, like the ground ahead.");
    Cycle("MSAA", s.msaa, {{0, "Off"}, {2, "2x"}, {4, "4x"}, {8, "8x"}},
          "Smooths the edges of 3D shapes while keeping the image sharp. Not alpha tested edges (fences, foliage); "
          "SMAA does those, and the two combine.");
    Toggle("SMAA", s.smaa,
           "Smooths jagged edges over the whole picture, HUD included, fences and foliage too. Slightly softer image.");
    if (kGame.ambient_occlusion) {
        Cycle("Ambient Occlusion", s.ambient_occlusion, {{0, "Off"}, {1, "SSAO"}, {2, "GTAO"}},
              "Darkens creases, corners and where things meet the ground. GTAO is the more accurate and a little "
              "slower.");
        std::vector<Choice> strengths;
        for (uint32_t p : {25u, 50u, 75u, 100u, 125u, 150u, 200u}) strengths.push_back({p, std::to_string(p) + "%"});
        Cycle("AO Strength", s.ao_strength, strengths, "How dark ambient occlusion makes things.");
    }

    Section("Audio");
    Cycle("Output", s.audio_output, {{audio::kOutputAuto, "Automatic"},
                                     {audio::kOutputStereo, "Stereo"},
                                     {audio::kOutputSurround, "5.1 Surround"},
                                     {audio::kOutputMono, "Mono"}},
          "Automatic follows the speakers Windows has set up.");

    if (kGame.skip_intros) {
        Section("General");
        Toggle("Skip Intros", s.skip_intros, "The logo and rating movies at start up. Takes effect the next start.");
    }
}

// the game's aiming help (GameInfo::aim_assist), in both the Mouse and Keyboard and the Controller tabs
constexpr const char* kAimAssistHint =
    "The game's lock on. Sighted near an enemy, your aim is pulled onto them and held there inside a circle "
    "around them, and jumps to the next enemy nearby. Off, your aim goes only where you move it, with the mouse "
    "or the stick.";
constexpr const char* kTargetZoomHint =
    "Sighted on an enemy, the game zooms in by how far away they are, so each target has its own zoom. Off, the "
    "zoom stays as your sights or scope have it. Works with Aim Assist on or off.";
constexpr const char* kZoomSensitivityHint =
    "With Target Zoom: as the game zooms in on a target, your aim slows down to match, for finer aim on far "
    "targets. Off, your aim keeps the same speed however far it zooms. The original game has this on at 100%.";
constexpr const char* kZoomStrengthHint =
    "How much the target zoom slows your aim. 100%: the picture moves as fast on screen as before the zoom. Lower: "
    "less slowdown.";

// Target Zoom, and under it Zoom Sensitivity with its strength
void DrawTargetZoom(Settings& s) {
    Toggle("Target Zoom", s.auto_zoom, kTargetZoomHint);
    if (!s.auto_zoom) return;
    Toggle("Zoom Sensitivity", s.zoom_sensitivity, kZoomSensitivityHint);
    if (!s.zoom_sensitivity) return;
    std::vector<Choice> shares;
    for (uint32_t p = 10; p <= 100; p += 10) shares.push_back({p, std::to_string(p) + "%"});
    Cycle("Strength", s.zoom_sensitivity_percent, shares, kZoomStrengthHint);
}

void DrawMouseKeyboard() {
    Settings& s = g_draft;
    Section("Mouse");
    Slider("Horizontal Sensitivity", s.mouse_x, 0.1f, 10.0f, 0.1f, "How far the view turns for the mouse's movement.");
    Slider("Vertical Sensitivity", s.mouse_y, 0.1f, 10.0f, 0.1f, "How far the view tilts for the mouse's movement.");
    Toggle("Invert X Axis", s.invert_mouse_x, "Mouse right turns left.");
    Toggle("Invert Y Axis", s.invert_mouse_y, "Mouse forward looks down.");
    if (kGame.aim_assist) {
        Toggle("Aim Assist", s.aim_assist, kAimAssistHint);
        DrawTargetZoom(s);
    }
    Section("Keyboard");
    for (int i = 0; i < int(kGame.key_actions.size()); i++) KeyRow(i);
}

void DrawController() {
    Settings& s = g_draft;
    Section("Controller");
    Toggle("Vibration", s.vibration, "Rumble on controllers that have it.");
    Toggle("Invert X Axis", s.pad_invert_x, "The look stick: right turns left.");
    Toggle("Invert Y Axis", s.pad_invert_y, "The look stick: up looks down.");
    if (kGame.aim_assist) {  // the same settings as in Mouse and Keyboard
        Toggle("Aim Assist", s.aim_assist, kAimAssistHint);
        DrawTargetZoom(s);
    }
}

// the tab's settings back to their defaults, in the copy
void ResetTab() {
    Settings& s = g_draft;
    const Settings& d = Defaults();
    if (g_tab == 0) {
        s.display_mode = d.display_mode;
        s.width = d.width;
        s.height = d.height;
        s.render_percent = d.render_percent;
        s.aspect = d.aspect;
        s.fov = d.fov;
        s.fps_cap = d.fps_cap;
        s.vsync = d.vsync;
        s.fps_counter = d.fps_counter;
        s.hide_hud = d.hide_hud;
        s.skip_intros = d.skip_intros;
        s.aniso = d.aniso;
        s.smaa = d.smaa;
        s.msaa = d.msaa;
        s.ambient_occlusion = d.ambient_occlusion;
        s.ao_strength = d.ao_strength;
        s.audio_output = d.audio_output;
    } else if (g_tab == 1) {
        s.mouse_x = d.mouse_x;
        s.mouse_y = d.mouse_y;
        s.invert_mouse_x = d.invert_mouse_x;
        s.invert_mouse_y = d.invert_mouse_y;
        s.aim_assist = d.aim_assist;
        s.auto_zoom = d.auto_zoom;
        s.zoom_sensitivity = d.zoom_sensitivity;
        s.zoom_sensitivity_percent = d.zoom_sensitivity_percent;
        s.keys = d.keys;
    } else {
        s.vibration = d.vibration;
        s.pad_invert_x = d.pad_invert_x;
        s.pad_invert_y = d.pad_invert_y;
    }
}

void Apply() {
    Settings& s = Get();
    bool display = s.display_mode != g_draft.display_mode || s.width != g_draft.width || s.height != g_draft.height;
    s = g_draft;
    audio::SetOutputMode(s.audio_output);
    ApplyKeys();
    Save();
    if (display) ApplyDisplay();
}

void Close() {
    input::CancelKeyCapture();
    g_bind_row = -1;
    g_open = false;
}

// a footer or tab button; true when clicked
bool PanelButton(const char* label, ImVec2 at, ImVec2 size, ImU32 fill, ImU32 hover, ImU32 text) {
    ImGui::SetCursorScreenPos(at);
    bool clicked = ImGui::InvisibleButton(label, size);
    bool on = ImGui::IsItemHovered() || ImGui::IsItemFocused();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(at, {at.x + size.x, at.y + size.y}, on ? hover : fill);
    CenteredText(dl, at, {at.x + size.x, at.y + size.y}, text, label);
    return clicked;
}

void DrawFps(double fps) {
    char text[32];
    std::snprintf(text, sizeof(text), "FPS: %.1f", fps);
    ImGuiIO& io = ImGui::GetIO();
    ImFont* font = io.Fonts->Fonts.Size > 1 ? io.Fonts->Fonts[1] : nullptr;
    float size = Em() * 1.45f;
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    ImVec2 at{size * 0.6f, size * 0.45f};
    dl->AddText(font, size, {at.x + 2, at.y + 2}, IM_COL32(0, 0, 0, 220), text);
    dl->AddText(font, size, at, IM_COL32(255, 255, 255, 240), text);
}

void Style() {
    float em = Em();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 0;
    style.ChildRounding = 0;
    style.FrameRounding = 0;
    style.WindowBorderSize = 0;
    style.ChildBorderSize = 0;
    style.WindowPadding = {0, 0};
    style.ItemSpacing = {0, std::round(em * 0.12f)};
    style.ScrollbarSize = std::round(em * 0.4f);
    style.ScrollbarRounding = 0;
    ImVec4* c = style.Colors;
    c[ImGuiCol_Text] = ImGui::ColorConvertU32ToFloat4(kText);
    c[ImGuiCol_WindowBg] = {0, 0, 0, 0};
    c[ImGuiCol_ChildBg] = {0, 0, 0, 0};
    c[ImGuiCol_Header] = {0, 0, 0, 0};
    c[ImGuiCol_HeaderHovered] = ImGui::ColorConvertU32ToFloat4(kRowOn);
    c[ImGuiCol_HeaderActive] = ImGui::ColorConvertU32ToFloat4(kRowOn);
    c[ImGuiCol_NavHighlight] = ImGui::ColorConvertU32ToFloat4(kBorder);
    c[ImGuiCol_ScrollbarBg] = {0, 0, 0, 0};
    c[ImGuiCol_ScrollbarGrab] = {0.75f, 0.75f, 0.72f, 0.75f};
    c[ImGuiCol_ScrollbarGrabHovered] = {0.85f, 0.85f, 0.82f, 0.9f};
    c[ImGuiCol_ScrollbarGrabActive] = {0.95f, 0.95f, 0.92f, 1.0f};
}

}  // namespace

bool MenuOpen() { return g_open.load(); }

void ToggleMenu() {
    std::lock_guard lock(UiLock());
    if (g_open.load()) {
        Close();
        return;
    }
    g_modes = Resolutions();
    g_draft = Get();
    g_hint = "";
    g_open = true;
}

void DrawMenu(double fps) {
    ImGuiIO& io = ImGui::GetIO();
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    // the font was made for the monitor's height: a smaller window shrinks it
    float want = kMenuFontPx * std::max(vp->Size.y, 480.0f) / 720.0f;
    float made = io.Fonts->Fonts.empty() ? kMenuFontPx : io.Fonts->Fonts[0]->FontSize;
    io.FontGlobalScale = std::min(1.0f, want / made);
    bool open = g_open.load();
    io.MouseDrawCursor = open;
    if (Get().fps_counter) DrawFps(fps);
    if (!open) return;
    Style();
    FinishBinding();
    if (g_bind_row < 0) {  // tabs: Ctrl+Tab, Q and E, a controller's shoulders
        int dir = 0;
        if (ImGui::IsKeyPressed(ImGuiKey_Q) || ImGui::IsKeyPressed(ImGuiKey_GamepadL1)) dir = -1;
        if (ImGui::IsKeyPressed(ImGuiKey_E) || ImGui::IsKeyPressed(ImGuiKey_GamepadR1)) dir = 1;
        if (ImGui::IsKeyPressed(ImGuiKey_Tab) && io.KeyCtrl) dir = io.KeyShift ? -1 : 1;
        if (dir) g_tab = (g_tab + 3 + dir) % 3;
    }

    float em = Em();
    float vw = vp->Size.x, vh = vp->Size.y;
    ImVec2 size{std::round(std::min(vh * 0.9f, vw * 0.94f)), std::round(vh * 0.58f)};
    ImVec2 pos{std::round(vp->Pos.x + (vw - size.x) * 0.5f), std::round(vp->Pos.y + (vh - size.y) * 0.5f)};
    ImGui::SetNextWindowPos(pos);
    ImGui::SetNextWindowSize(size);
    constexpr ImGuiWindowFlags kFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollWithMouse;
    ImGui::Begin("##settings", nullptr, kFlags);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(pos, {pos.x + size.x, pos.y + size.y}, kPanel);
    dl->AddRect(pos, {pos.x + size.x, pos.y + size.y}, kBorder, 0, 0, std::max(1.0f, std::round(em * 0.12f)));

    float pad = std::round(size.x * 0.031f);
    float inner_w = size.x - pad * 2;
    float y = pos.y + em * 1.0f;
    CenteredText(dl, {pos.x, y}, {pos.x + size.x, y + em * 1.4f}, kText, "SETTINGS");
    y += em * 1.4f;

    // tabs
    const char* tabs[] = {"Recomp Settings", "Mouse and Keyboard", "Controller"};
    float tab_h = std::round(em * 1.45f), tab_w = inner_w / 3;
    for (int i = 0; i < 3; i++) {
        ImVec2 at{pos.x + pad + i * tab_w, y};
        ImGui::SetCursorScreenPos(at);
        ImGui::PushID(i);
        if (ImGui::InvisibleButton("##tab", {tab_w, tab_h})) g_tab = i;
        bool hover = ImGui::IsItemHovered() || ImGui::IsItemFocused();
        ImGui::PopID();
        dl->AddRectFilled(at, {at.x + tab_w, at.y + tab_h}, g_tab == i ? kTabOn : hover ? kTabHover : kTabOff);
        Text(dl, {at.x + em * 0.2f, at.y + (tab_h - em) * 0.5f}, kText, Upper(tabs[i]));
    }
    y += tab_h + em * 0.25f;

    // the list
    float foot_h = std::round(em * 1.45f);
    float list_h = pos.y + size.y - y - foot_h - em * 2.6f;
    ImGui::SetCursorScreenPos({pos.x + pad, y});
    g_hint = "";
    ImGui::BeginChild("##list", {inner_w, list_h}, ImGuiChildFlags_None, ImGuiWindowFlags_NoSavedSettings);
    ImGui::PushItemWidth(-1);
    if (g_tab == 0) DrawRecomp();
    else if (g_tab == 1) DrawMouseKeyboard();
    else DrawController();
    ImGui::Dummy({0, em * 0.4f});
    ImGui::PopItemWidth();
    ImGui::EndChild();
    float list_bottom = y + list_h;
    dl->AddLine({pos.x + pad, list_bottom}, {pos.x + pad + inner_w, list_bottom}, kLine, 1.0f);

    // footer: the row's hint, Back, Defaults, Apply
    float fy = pos.y + size.y - foot_h - em * 1.3f;
    float bw = std::round(inner_w * 0.16f), gap = std::round(em * 0.35f);
    float bx = pos.x + pad + inner_w - bw * 3 - gap * 2;
    {
        // the hint wrapped by hand into the space left of the buttons
        std::string hint = g_hint;
        float max_w = bx - em - (pos.x + pad);
        float ty = list_bottom + em * 0.45f;
        while (!hint.empty() && ty + em <= fy + foot_h + em * 0.2f) {
            size_t cut = hint.size();
            while (ImGui::CalcTextSize(hint.substr(0, cut).c_str()).x > max_w) {
                size_t space = hint.rfind(' ', cut - 1);
                if (space == std::string::npos || space == 0) break;
                cut = space;
            }
            Text(dl, {pos.x + pad, ty}, kDim, hint.substr(0, cut));
            hint = cut < hint.size() ? hint.substr(cut + 1) : "";
            ty += em * 1.15f;
        }
    }
    bool changed = !(g_draft == Get());
    bool close = false;
    if (PanelButton("Back", {bx, fy}, {bw, foot_h}, kButton, kButtonOn, kText)) close = true;
    if (PanelButton("Defaults", {bx + bw + gap, fy}, {bw, foot_h}, kButton, kButtonOn, kText)) ResetTab();
    if (PanelButton("Apply", {bx + (bw + gap) * 2, fy}, {bw, foot_h}, kButton, kButtonOn, changed ? kText : kDim) &&
        changed)
        Apply();
    ImGui::End();
    if (close) Close();
}

}  // namespace app
