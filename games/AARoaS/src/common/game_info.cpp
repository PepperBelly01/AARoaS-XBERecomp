#include "app/app.h"

// The game draws 4:3 only, whatever the console reports: pillarboxed by default rather than stretched. The settings
// file's aspect still wins, it loads after this.
static const bool g_aspect_default = (app::Get().aspect = app::kAspect4x3, true);

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
    .load = nullptr,
    .save = nullptr,
    .init = nullptr,
    .loaded = nullptr,
    .window = nullptr,
    .message = nullptr,
    .ui = nullptr,
    .pad = nullptr,
    .rumble = nullptr,
};
