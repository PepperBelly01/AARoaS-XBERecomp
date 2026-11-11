// The F10 settings overlay: the settings file's options, each applied as it changes and saved at once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <string>
#include <vector>

#include <imgui.h>

#include "app/app.h"
#include "audio/audio.h"

namespace app {

namespace {

std::atomic<bool> g_open{false};

struct Choice {
    uint32_t value;
    std::string label;
};

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

std::vector<Mode> g_modes;  // read when the overlay opens: a mode list takes a moment to enumerate

// a drop down list over choices; true when the player picked another
bool Pick(const char* label, uint32_t& value, const std::vector<Choice>& choices) {
    const char* shown = "";
    for (const Choice& c : choices)
        if (c.value == value) shown = c.label.c_str();
    bool changed = false;
    if (ImGui::BeginCombo(label, shown)) {
        for (const Choice& c : choices) {
            bool selected = c.value == value;
            if (ImGui::Selectable(c.label.c_str(), selected) && !selected) {
                value = c.value;
                changed = true;
            }
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    return changed;
}

void Hint(const char* text) {
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", text);
}

void DrawFps(double fps, float scale) {
    char text[32];
    std::snprintf(text, sizeof(text), "%.0f FPS", fps);
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    ImVec2 at{12 * scale, 10 * scale};
    float size = ImGui::GetFontSize() * 1.2f;
    dl->AddText(nullptr, size, {at.x + 1, at.y + 1}, IM_COL32(0, 0, 0, 200), text);
    dl->AddText(nullptr, size, at, IM_COL32(255, 255, 255, 230), text);
}

void DrawDisplay() {
    Settings& s = Get();
    ImGui::SeparatorText("Display");

    uint32_t mode = uint32_t(s.display_mode);
    if (Pick("Display Mode", mode, {{uint32_t(DisplayMode::Fullscreen), "Fullscreen"},
                                    {uint32_t(DisplayMode::Borderless), "Borderless"},
                                    {uint32_t(DisplayMode::Windowed), "Windowed"}})) {
        s.display_mode = DisplayMode(mode);
        Save();
        ApplyDisplay();
    }

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
    if (Pick("Resolution", current, sizes)) {
        s.width = current ? g_modes[current - 1].width : 0;
        s.height = current ? g_modes[current - 1].height : 0;
        Save();
        ApplyDisplay();
    }
    Hint("Borderless always fills the monitor; there the resolution sets how sharp the picture is drawn.");

    std::vector<Choice> scales;
    for (uint32_t p : {50u, 75u, 100u, 125u, 150u, 175u, 200u}) scales.push_back({p, std::to_string(p) + "%"});
    if (Pick("Render Resolution", s.render_percent, scales)) Save();
    Hint("Draws the game at this share of the output resolution. Above 100% is supersampling: sharper, slower.");

    uint32_t aspect = s.aspect == kAspectAuto ? (Widescreen() ? kAspect16x9 : kAspect4x3) : s.aspect;
    if (Pick("Aspect Ratio", aspect, {{kAspect4x3, "4:3 (original)"}, {kAspect16x9, "16:9"}})) {
        s.aspect = aspect;
        Save();
    }

    if (kGame.fov > 0) {
        std::vector<Choice> angles;
        char text[48];
        std::snprintf(text, sizeof(text), "Game default (%.0f)", kGame.fov);
        angles.push_back({0, text});
        for (uint32_t a = 60; a <= 120; a += 5)
            if (a != uint32_t(kGame.fov + 0.5f)) angles.push_back({a, std::to_string(a)});
        if (Pick("Field of View", s.fov, angles)) Save();
        Hint("Horizontal degrees as on a 4:3 screen; 16:9 adds to the sides. Aiming zooms in from it as usual.");
    }

    uint32_t cap = s.fps_cap;
    if (Pick("Max Frame Rate", cap, {{kFpsCapRefresh, "Monitor refresh rate"},
                                     {30, "30"},
                                     {60, "60"},
                                     {120, "120"},
                                     {144, "144"},
                                     {165, "165"},
                                     {240, "240"},
                                     {0, "Unlimited"}})) {
        s.fps_cap = cap;
        Save();
    }
    if (ImGui::Checkbox("V-Sync", &s.vsync)) Save();
    if (ImGui::Checkbox("FPS Counter", &s.fps_counter)) Save();
    if (kGame.screen_2d) {
        if (ImGui::Checkbox("Hide HUD", &s.hide_hud)) Save();
        Hint("Hides everything drawn flat over the 3D view in play, subtitles and the pause menu included. "
             "Menu screens still show.");
    }
}

void DrawGraphics() {
    Settings& s = Get();
    ImGui::SeparatorText("Graphics");
    if (Pick("Anisotropic Filtering", s.aniso, {{0, "Game default"}, {2, "2x"}, {4, "4x"}, {8, "8x"}, {16, "16x"}})) Save();
    if (ImGui::Checkbox("SMAA", &s.smaa)) Save();
    Hint("Smooths jagged edges over the whole picture, HUD included, alpha tested fences and foliage too. "
         "Slightly softer image.");
}

void DrawGeneral() {
    Settings& s = Get();
    if (!kGame.skip_intros) return;
    ImGui::SeparatorText("General");
    if (ImGui::Checkbox("Skip Intros", &s.skip_intros)) Save();
    Hint("The logo and rating movies at start up. Takes effect the next time the game starts.");
}

void DrawAudio() {
    Settings& s = Get();
    ImGui::SeparatorText("Audio");
    if (Pick("Output", s.audio_output, {{audio::kOutputAuto, "Automatic"},
                                        {audio::kOutputStereo, "Stereo"},
                                        {audio::kOutputSurround, "5.1 Surround"},
                                        {audio::kOutputMono, "Mono"}})) {
        audio::SetOutputMode(s.audio_output);
        Save();
    }
}

}  // namespace

bool MenuOpen() { return g_open.load(); }

void ToggleMenu() {
    std::lock_guard lock(UiLock());
    bool open = !g_open.load();
    if (open) g_modes = Resolutions();
    g_open = open;
}

void DrawMenu(double fps) {
    ImGuiIO& io = ImGui::GetIO();
    // the font was made for the monitor's height: a smaller window shrinks it, 720 lines as 1
    float scale = std::clamp(ImGui::GetMainViewport()->Size.y / 720.0f, 1.0f, 4.0f);
    float made = io.Fonts->Fonts.empty() ? kMenuFontPx : io.Fonts->Fonts[0]->FontSize;
    io.FontGlobalScale = std::min(1.0f, kMenuFontPx * scale / made);
    bool open = g_open.load();
    io.MouseDrawCursor = open;
    if (Get().fps_counter) DrawFps(fps, scale);
    if (!open) return;

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos({vp->Pos.x + vp->Size.x * 0.5f, vp->Pos.y + vp->Size.y * 0.5f}, ImGuiCond_Always, {0.5f, 0.5f});
    float em = ImGui::GetFontSize();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 0.4f * em;
    style.FrameRounding = 0.25f * em;
    style.WindowPadding = {0.9f * em, 0.7f * em};
    style.FramePadding = {0.45f * em, 0.2f * em};
    style.ItemSpacing = {0.5f * em, 0.35f * em};
    ImGui::SetNextWindowSize({30 * em, 0}, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.92f);
    constexpr ImGuiWindowFlags kFlags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                                        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize;
    bool keep = true;
    ImGui::Begin("Settings", &keep, kFlags);
    ImGui::PushItemWidth(-12 * em);
    DrawDisplay();
    DrawGraphics();
    DrawAudio();
    DrawGeneral();
    ImGui::PopItemWidth();
    ImGui::Separator();
    if (ImGui::Button("Reset to Defaults")) {
        Settings& s = Get();
        bool mode_changed = s.display_mode != Defaults().display_mode || s.width != Defaults().width || s.height != Defaults().height;
        s = Defaults();
        audio::SetOutputMode(s.audio_output);
        Save();
        if (mode_changed) ApplyDisplay();
    }
    ImGui::SameLine();
    if (ImGui::Button("Close")) keep = false;
    ImGui::SameLine();
    ImGui::TextDisabled("F10 opens and closes this");
    ImGui::End();
    if (!keep) g_open = false;
}

}  // namespace app
