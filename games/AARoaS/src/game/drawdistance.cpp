// Grass Distance (Settings::grass_distance, a percent of the game's own; the overlay's Graphics). Named for grass alone so
// other distances can come beside it. The map's actors are
// the controller's XLevel's list (data, count, max at XLevel +0x30, the LevelInfo first). Its terrains' grass layers
// (TerrainInfo.DecoLayers, structs of DecorationLayer) fade out over FadeoutRadius, a range (Min, Max: 0 to 5000 on
// the training map, the grass gone by about 95 m): both are scaled from the originals, kept, so the setting changes either way in play.
// The actors are gone over once a map and when the setting changes (twice a second it cost frames). Actor.LODBias on
// the people changed nothing seen (this game picks its soldiers' detail its own way): not set.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstdint>
#include <string>
#include <unordered_map>

#include "app/app.h"
#include "core/log.h"
#include "kernel/memory.h"
#include "xdk/hle.h"

uint32_t FindProperty(uint8_t* base, uint32_t controller, uint32_t object, const char* name);  // aim_assist.cpp
uint32_t PropertyOffset(uint8_t* base, uint32_t property);
bool IsA(uint8_t* base, uint32_t object, const char* class_name);
uint32_t StructField(uint8_t* base, uint32_t strct, const char* name);

namespace {
constexpr uint32_t kActorsAt = 0x30;  // ULevel's actor list (seen in play)

bool Readable(uint32_t a, uint32_t n) { return a >= 0x10000 && mem::IsCommitted(a) && mem::IsCommitted(a + n - 1); }

struct Layout {
    uint32_t deco = 0;         // TerrainInfo.DecoLayers
    uint32_t fade = 0;         // DecorationLayer.FadeoutRadius (Min, Max)
    uint32_t stride = 0;       // DecorationLayer's size
    bool deco_known = false;   // looked for (a terrain seen)
} g_layout;
std::unordered_map<uint32_t, float> g_original;  // by the float's address: its value as the game had it
uint32_t g_level = 0;

// the array property's struct: whichever pointer of its leads to one with FadeoutRadius
void LearnDeco(uint8_t* base, uint32_t controller, uint32_t terrain) {
    g_layout.deco_known = true;
    uint32_t p = FindProperty(base, controller, terrain, "DecoLayers");
    g_layout.deco = PropertyOffset(base, p);
    for (uint32_t k = 0x40; k < 0x60 && p; k += 4) {
        uint32_t inner = X86_MEM32(p + k);
        if (!Readable(inner, 0x60)) continue;
        for (uint32_t j = 0x40; j < 0x60; j += 4) {
            uint32_t strct = X86_MEM32(inner + j);
            uint32_t f = StructField(base, strct, "FadeoutRadius");
            if (!f) continue;
            g_layout.fade = PropertyOffset(base, f);
            uint32_t size = X86_MEM32(strct + 0x3C);  // UStruct's size (0x64 for DecorationLayer)
            g_layout.stride = size >= 0x40 && size <= 0x400 ? size : 0x64;
            LOGR("grass distance: DecoLayers +0x%X, FadeoutRadius +0x%X, a layer 0x%X bytes", g_layout.deco, g_layout.fade,
                 g_layout.stride);
            return;
        }
    }
    LOGR("grass distance: the grass layers' fade distance not found");
}

void Scale(uint8_t* base, uint32_t at, float factor) {
    auto it = g_original.find(at);
    if (it == g_original.end()) it = g_original.emplace(at, X86_MEMF32(at)).first;
    X86_MEMF32(at) = it->second * factor;
}
}  // namespace

// each frame (mouse.cpp's UpdateInput hook)
void DrawDistanceFrame(uint8_t* base, uint32_t viewport) {
    static uint32_t applied = 0, applied_level = 0;
    uint32_t percent = app::Get().grass_distance;
    uint32_t controller = X86_MEM32(viewport + 0x34);
    if (!Readable(controller, 0x200)) return;
    static uint32_t level_at = 0;
    if (!level_at) level_at = PropertyOffset(base, FindProperty(base, controller, controller, "XLevel"));
    uint32_t level = level_at ? X86_MEM32(controller + level_at) : 0;
    if (!Readable(level, kActorsAt + 12)) return;
    if (level == applied_level && percent == applied) return;
    static ULONGLONG retry = 0;  // a map with no grass found yet (still loading, or none): looked again now and then
    if (GetTickCount64() < retry) return;
    LARGE_INTEGER t0, t1, freq;
    QueryPerformanceCounter(&t0);
    if (level != g_level) {  // a new map: its own originals
        g_level = level;
        g_original.clear();
    }
    uint32_t actors = X86_MEM32(level + kActorsAt), count = X86_MEM32(level + kActorsAt + 4);
    if (count > 200000 || !Readable(actors, count * 4)) return;
    float factor = float(percent) / 100.0f;
    int layers = 0;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t a = X86_MEM32(actors + i * 4);
        if (!Readable(a, 0x40)) continue;
        if (IsA(base, a, "TerrainInfo")) {
            if (!g_layout.deco_known) LearnDeco(base, controller, a);
            if (!g_layout.deco || !g_layout.fade) continue;
            uint32_t data = X86_MEM32(a + g_layout.deco), n = X86_MEM32(a + g_layout.deco + 4);
            if (n > 64 || !Readable(data, n * g_layout.stride)) continue;
            for (uint32_t l = 0; l < n; l++, layers++) {
                uint32_t fade = data + l * g_layout.stride + g_layout.fade;
                Scale(base, fade, factor);      // Min
                Scale(base, fade + 4, factor);  // Max
            }
        }
    }
    QueryPerformanceCounter(&t1);
    QueryPerformanceFrequency(&freq);
    LOGR("grass distance: %u%%, %d grass layers (%u actors gone over in %.2f ms)", percent, layers, count,
         double(t1.QuadPart - t0.QuadPart) * 1000.0 / double(freq.QuadPart));
    applied = percent;
    applied_level = layers ? level : 0;
    if (!layers) retry = GetTickCount64() + 2000;
}

