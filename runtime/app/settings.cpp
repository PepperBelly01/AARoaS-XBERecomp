// the settings file beside the exe
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <mutex>
#include <sstream>

#include <toml++/toml.hpp>

#include "app/app.h"
#include "audio/audio.h"
#include "core/log.h"

namespace app {

namespace {

const char* kModes[] = {"fullscreen", "borderless", "windowed"};

std::filesystem::path File() { return ExeDir() / kGame.settings; }

// "Move Forward" as move_forward
std::string KeySlug(const char* name) {
    std::string out;
    for (const char* c = name; *c; c++) {
        if (std::isalnum(uint8_t(*c))) out += char(std::tolower(uint8_t(*c)));
        else if (!out.empty() && out.back() != '_') out += '_';
    }
    while (!out.empty() && out.back() == '_') out.pop_back();
    return out;
}

}  // namespace

Settings& Get() {
    static Settings s;
    return s;
}

namespace {
Settings g_defaults;
}

const Settings& Defaults() { return g_defaults; }

void ApplyKeys() {
    const Settings& s = Get();
    std::vector<input::KeyBind> binds;
    for (size_t i = 0; i < kGame.key_actions.size() && i < s.keys.size(); i++) {
        const input::KeyAction& a = kGame.key_actions[i];
        for (uint16_t k : s.keys[i])
            if (k) binds.push_back({k, a.target, a.index});
    }
    input::SetKeys(binds);
}

void Load() {
    Settings& s = Get();
    s.keys.clear();
    for (const input::KeyAction& a : kGame.key_actions) s.keys.push_back({a.keys[0], a.keys[1]});
    g_defaults = s;
    toml::table t;
    try {
        t = toml::parse_file(File().string());
    } catch (...) {
        ApplyKeys();
        return;
    }
    std::string mode = t["display_mode"].value_or(std::string(kModes[uint32_t(s.display_mode)]));
    for (uint32_t i = 0; i < 3; i++)
        if (mode == kModes[i]) s.display_mode = DisplayMode(i);
    s.width = t["width"].value_or(s.width);
    s.height = t["height"].value_or(s.height);
    s.render_percent = t["render_percent"].value_or(s.render_percent);
    s.aspect = std::min(t["aspect"].value_or(s.aspect), kAspect16x9);
    s.fov = std::min(t["fov"].value_or(s.fov), 150u);
    s.fps_cap = t["fps_cap"].value_or(s.fps_cap);
    s.vsync = t["vsync"].value_or(s.vsync);
    s.fps_counter = t["fps_counter"].value_or(s.fps_counter);
    s.hide_hud = t["hide_hud"].value_or(s.hide_hud);
    s.skip_intros = t["skip_intros"].value_or(s.skip_intros);
    s.aniso = t["anisotropic_filtering"].value_or(s.aniso);
    s.smaa = t["smaa"].value_or(s.smaa);
    s.msaa = std::min(t["msaa"].value_or(s.msaa), 8u);
    s.ambient_occlusion = std::min(t["ambient_occlusion"].value_or(s.ambient_occlusion), 2u);
    s.ao_strength = std::clamp(t["ao_strength"].value_or(s.ao_strength), 25u, 200u);
    s.mouse_x = std::clamp(t["mouse_x"].value_or(s.mouse_x), 0.1f, 10.0f);
    s.mouse_y = std::clamp(t["mouse_y"].value_or(s.mouse_y), 0.1f, 10.0f);
    s.invert_mouse_x = t["invert_mouse_x"].value_or(s.invert_mouse_x);
    s.invert_mouse_y = t["invert_mouse_y"].value_or(s.invert_mouse_y);
    s.aim_assist = t["aim_assist"].value_or(s.aim_assist);
    s.auto_zoom = t["auto_zoom"].value_or(s.auto_zoom);
    s.zoom_sensitivity = t["zoom_sensitivity"].value_or(s.zoom_sensitivity);
    s.zoom_sensitivity_percent = std::clamp(t["zoom_sensitivity_percent"].value_or(s.zoom_sensitivity_percent), 10u, 100u);
    s.slow_walk_mode = std::min(t["slow_walk_mode"].value_or(s.slow_walk_mode), 1u);
    s.sprint_mode = std::min(t["sprint_mode"].value_or(s.sprint_mode), 1u);
    s.lean_mode = std::min(t["lean_mode"].value_or(s.lean_mode), 1u);
    s.aim_mode = std::min(t["aim_mode"].value_or(s.aim_mode), 1u);
    s.vibration = t["vibration"].value_or(s.vibration);
    s.pad_invert_x = t["pad_invert_x"].value_or(s.pad_invert_x);
    s.pad_invert_y = t["pad_invert_y"].value_or(s.pad_invert_y);
    if (const toml::table* keys = t["keys"].as_table()) {
        for (size_t i = 0; i < kGame.key_actions.size(); i++) {
            const toml::array* a = (*keys)[KeySlug(kGame.key_actions[i].name)].as_array();
            if (!a) continue;
            for (size_t j = 0; j < 2; j++) {
                int64_t k = j < a->size() ? (*a)[j].value_or(int64_t(0)) : 0;
                s.keys[i][j] = k > 0 && k < input::kKeyCount ? uint16_t(k) : 0;
            }
        }
    }
    s.custom_textures = t["custom_textures"].value_or(s.custom_textures);
    s.dump_textures = t["dump_textures"].value_or(s.dump_textures);
    s.texture_dump_format = std::min(t["texture_dump_format"].value_or(s.texture_dump_format), 1u);
    s.join_ip = t["join_ip"].value_or(s.join_ip);
    s.audio_output = std::min(t["audio_output"].value_or(s.audio_output), audio::kOutputMono);
    for (const Option& o : kGame.options) {
        if (o.flag) *o.flag = t[o.key].value_or(*o.flag);
        else *o.value = t[o.key].value_or(*o.value);
    }
    if (kGame.load) kGame.load(&t);
    audio::SetOutputMode(s.audio_output);
    ApplyKeys();
}

namespace {

toml::table Table() {
    const Settings& s = Get();
    toml::table t{{"display_mode", kModes[uint32_t(s.display_mode)]},
                  {"width", int64_t(s.width)},
                  {"height", int64_t(s.height)},
                  {"render_percent", int64_t(s.render_percent)},
                  {"aspect", int64_t(s.aspect)},
                  {"fov", int64_t(s.fov)},
                  {"fps_cap", int64_t(s.fps_cap)},
                  {"vsync", s.vsync},
                  {"fps_counter", s.fps_counter},
                  {"hide_hud", s.hide_hud},
                  {"skip_intros", s.skip_intros},
                  {"anisotropic_filtering", int64_t(s.aniso)},
                  {"smaa", s.smaa},
                  {"msaa", int64_t(s.msaa)},
                  {"ambient_occlusion", int64_t(s.ambient_occlusion)},
                  {"ao_strength", int64_t(s.ao_strength)},
                  {"mouse_x", double(s.mouse_x)},
                  {"mouse_y", double(s.mouse_y)},
                  {"invert_mouse_x", s.invert_mouse_x},
                  {"invert_mouse_y", s.invert_mouse_y},
                  {"aim_assist", s.aim_assist},
                  {"auto_zoom", s.auto_zoom},
                  {"zoom_sensitivity", s.zoom_sensitivity},
                  {"zoom_sensitivity_percent", int64_t(s.zoom_sensitivity_percent)},
                  {"slow_walk_mode", int64_t(s.slow_walk_mode)},
                  {"sprint_mode", int64_t(s.sprint_mode)},
                  {"lean_mode", int64_t(s.lean_mode)},
                  {"aim_mode", int64_t(s.aim_mode)},
                  {"vibration", s.vibration},
                  {"pad_invert_x", s.pad_invert_x},
                  {"pad_invert_y", s.pad_invert_y},
                  {"custom_textures", s.custom_textures},
                  {"dump_textures", s.dump_textures},
                  {"texture_dump_format", int64_t(s.texture_dump_format)},
                  {"join_ip", s.join_ip},
                  {"audio_output", int64_t(s.audio_output)}};
    for (const Option& o : kGame.options) {
        if (o.flag) t.insert(o.key, *o.flag);
        else t.insert(o.key, int64_t(*o.value));
    }
    if (!kGame.key_actions.empty()) {
        toml::table keys;
        for (size_t i = 0; i < kGame.key_actions.size() && i < s.keys.size(); i++)
            keys.insert(KeySlug(kGame.key_actions[i].name), toml::array{int64_t(s.keys[i][0]), int64_t(s.keys[i][1])});
        t.insert("keys", std::move(keys));
    }
    if (kGame.save) kGame.save(&t);
    return t;
}

}  // namespace

// written beside the real file and renamed over it, so a crash mid save keeps the old settings
void Save() {
    std::filesystem::path tmp = File();
    tmp += ".tmp";
    std::ofstream f(tmp);
    f << Table() << "\n";
    f.close();
    if (f) MoveFileExW(tmp.c_str(), File().c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
}

void LogSettings() {
    std::ostringstream text;
    text << Table();
    std::istringstream lines(text.str());
    LOGR("Settings:");
    for (std::string line; std::getline(lines, line);)
        if (!line.empty()) LOGR("  %s", line.c_str());
}

Mode Desktop() {
    HMONITOR mon = MonitorFromWindow(static_cast<HWND>(Window()), MONITOR_DEFAULTTOPRIMARY);
    MONITORINFOEXW mi{};
    mi.cbSize = sizeof(mi);
    DEVMODEW dm{};
    dm.dmSize = sizeof(dm);
    if (GetMonitorInfoW(mon, &mi) && EnumDisplaySettingsW(mi.szDevice, ENUM_REGISTRY_SETTINGS, &dm))
        return {dm.dmPelsWidth, dm.dmPelsHeight};
    return {uint32_t(GetSystemMetrics(SM_CXSCREEN)), uint32_t(GetSystemMetrics(SM_CYSCREEN))};
}

Mode Output() {
    const Settings& s = Get();
    if (s.width && s.height) return {s.width, s.height};
    return Desktop();
}

// Output for every frame: the desktop mode lookup reads the registry, so it runs again only when the resolution
// setting or the window's monitor changes
Mode CachedOutput() {
    struct Key {
        uint32_t width;
        uint32_t height;
        HMONITOR monitor;
        bool operator==(const Key&) const = default;
    };
    static std::mutex lock;
    static Key key{};
    static Mode mode{};
    const Settings& s = Get();
    Key now{s.width, s.height, MonitorFromWindow(static_cast<HWND>(Window()), MONITOR_DEFAULTTOPRIMARY)};
    std::lock_guard<std::mutex> guard(lock);
    if (!(now == key) || !mode.height) {
        key = now;
        mode = Output();
    }
    return mode;
}

// asked per frame and per HUD quad: CachedOutput costs a monitor lookup, the registry only on a change
bool Widescreen() {
    uint32_t aspect = Get().aspect;
    if (aspect == kAspect4x3) return false;
    if (aspect == kAspect16x9) return true;
    Mode m = CachedOutput();
    return m.width * 2 >= m.height * 3;  // 3:2 or wider
}

float FovScale() {
    uint32_t fov = Get().fov;
    if (!fov || kGame.fov <= 0) return 1.0f;
    constexpr float kHalfDegree = 3.14159265f / 360.0f;
    return std::tan(float(fov) * kHalfDegree) / std::tan(kGame.fov * kHalfDegree);
}

float RenderScale(uint32_t guest_height) {
    float h = CachedOutput().height * (Get().render_percent / 100.0f);
    return guest_height ? std::max(h / guest_height, 0.1f) : 1.0f;
}

}  // namespace app
