// Full screen overlays remade from the player's own textures the first time they are seen (texturedumper::AddMaker),
// as a release cannot carry game data: night vision without its HUD shapes (@hidehud: with Hide HUD or 16:9), and the
// scopes and binoculars round again across 16:9 (@wide). The groups are switched in mouse.cpp. User, 2026-11-15.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "gpu/texturedumper.h"

namespace {

using Px = std::vector<uint8_t>;  // BGRA, rows packed

constexpr uint32_t kScale = 4;  // made at 4x: the squeezed pictures keep their detail

// bilinear, u and v in source pixels, u kept within [u0, u1]
void Sample(const uint8_t* s, uint32_t w, uint32_t h, float u, float v, float u0, float u1, uint8_t* out) {
    u = std::clamp(u, u0, u1);
    v = std::clamp(v, 0.0f, float(h - 1));
    int x0 = int(std::floor(u)), y0 = int(std::floor(v));
    int x1 = std::min(x0 + 1, int(u1 < float(w - 1) ? std::ceil(u1) : w - 1)), y1 = std::min(y0 + 1, int(h - 1));
    float fx = u - x0, fy = v - y0;
    for (int c = 0; c < 4; c++) {
        float a = s[(y0 * w + x0) * 4 + c] * (1 - fx) + s[(y0 * w + x1) * 4 + c] * fx;
        float b = s[(y1 * w + x0) * 4 + c] * (1 - fx) + s[(y1 * w + x1) * 4 + c] * fx;
        out[c] = uint8_t(std::lround(a * (1 - fy) + b * fy));
    }
}

void Upscale(const uint8_t* s, uint32_t w, uint32_t h, Px& out, uint32_t& ow, uint32_t& oh) {
    ow = w * kScale;
    oh = h * kScale;
    out.assign(size_t(ow) * oh * 4, 0);
    for (uint32_t y = 0; y < oh; y++)
        for (uint32_t x = 0; x < ow; x++)
            Sample(s, w, h, (x + 0.5f) / kScale - 0.5f, (y + 0.5f) / kScale - 0.5f, 0, float(w - 1),
                   &out[(size_t(y) * ow + x) * 4]);
}

// Night vision (256x256, black, its darkness in alpha): the lens case and compass at the bottom corners, solid
// patches, filled in from around them by repeated averaging of the neighbours
bool NightVision(const uint8_t* s, uint32_t w, uint32_t h, Px& out, uint32_t& ow, uint32_t& oh) {
    if (w != 256 || h != 256) return false;
    std::vector<float> a(w * h);
    for (uint32_t i = 0; i < w * h; i++) a[i] = s[i * 4 + 3];
    std::vector<uint8_t> m(w * h, 0);
    for (uint32_t y = 165; y < 252; y++)
        for (uint32_t x = 4; x < 252; x++)
            if ((x < 100 || x > 156) && a[y * w + x] >= 234) m[y * w + x] = 1;
    for (int k = 0; k < 4; k++) {  // grown by 4 pixels, the soft edges in
        std::vector<uint8_t> n = m;
        for (uint32_t y = 1; y < h - 1; y++)
            for (uint32_t x = 1; x < w - 1; x++) {
                uint32_t i = y * w + x;
                if (!m[i] && (m[i - 1] || m[i + 1] || m[i - w] || m[i + w])) n[i] = 1;
            }
        m.swap(n);
    }
    std::vector<uint32_t> idx;
    for (uint32_t i = 0; i < w * h; i++)
        if (m[i]) idx.push_back(i);
    for (int it = 0; it < 300; it++)
        for (uint32_t i : idx) a[i] = (a[i - 1] + a[i + 1] + a[i - w] + a[i + w]) / 4;
    Px clean(w * h * 4, 0);
    for (uint32_t i = 0; i < w * h; i++) clean[i * 4 + 3] = uint8_t(std::lround(a[i]));
    Upscale(clean.data(), w, h, out, ow, oh);
    return true;
}

// sides black, eased into the picture over 16% of the width from where it begins (its own edge showed as lines)
void BlackSides(Px& out, uint32_t ow, uint32_t oh, uint32_t side) {
    uint32_t blend = uint32_t(ow * 0.16f);
    for (uint32_t y = 0; y < oh; y++) {
        for (uint32_t x = 0; x < ow; x++) {
            uint8_t* p = &out[(size_t(y) * ow + x) * 4];
            if (x < side || x >= ow - side) {
                p[0] = p[1] = p[2] = 0;
                p[3] = 255;
                continue;
            }
            uint32_t k = std::min(x - side, ow - side - 1 - x);
            if (k >= blend) continue;
            float t = float(k) / blend;
            t = t * t * (3 - 2 * t);
            p[0] = p[1] = p[2] = 0;
            p[3] = uint8_t(std::max(float(p[3]), 255 * (1 - t) + p[3] * t));
        }
    }
}

// A scope for 16:9: the picture at 3/4 of the width about the centre (round again when stretched across 16:9)
bool Scope(const uint8_t* s, uint32_t w, uint32_t h, Px& out, uint32_t& ow, uint32_t& oh) {
    ow = w * kScale;
    oh = h * kScale;
    out.assign(size_t(ow) * oh * 4, 0);
    uint32_t inner = ow * 3 / 4, side = (ow - inner) / 2;
    for (uint32_t y = 0; y < oh; y++)
        for (uint32_t x = side; x < ow - side; x++)
            Sample(s, w, h, (x - side + 0.5f) * w / inner - 0.5f, (y + 0.5f) / kScale - 0.5f, 0, float(w - 1),
                   &out[(size_t(y) * ow + x) * 4]);
    BlackSides(out, ow, oh, side);
    return true;
}

// The binoculars for 16:9 (256x256): each eyepiece round (3x across, 4x down) at the screen's sides, the gap between
// them the column where they meet stretched, the T (x 108-149, y 131-154) taken out of them and put back in the middle
bool Binoculars(const uint8_t* s, uint32_t w, uint32_t h, Px& out, uint32_t& ow, uint32_t& oh) {
    if (w != 256 || h != 256) return false;
    Px clean(s, s + w * h * 4);
    for (uint32_t y = 126; y < 160; y++)
        for (uint32_t x = 105; x < 153; x++) {
            float f = (x - 104) / 49.0f;
            for (int c = 0; c < 4; c++)
                clean[(y * w + x) * 4 + c] =
                    uint8_t(std::lround(s[(y * w + 104) * 4 + c] * (1 - f) + s[(y * w + 153) * 4 + c] * f));
        }
    ow = oh = 1024;
    out.assign(size_t(ow) * oh * 4, 0);
    for (uint32_t y = 0; y < oh; y++) {
        float v = (y + 0.5f) / 4 - 0.5f;
        for (uint32_t x = 0; x < ow; x++) {
            uint8_t* p = &out[(size_t(y) * ow + x) * 4];
            if (x < 384) Sample(clean.data(), w, h, (x + 0.5f) / 3 - 0.5f, v, 0, 127, p);
            else if (x >= 640) Sample(clean.data(), w, h, 128 + (x - 640 + 0.5f) / 3 - 0.5f, v, 128, 255, p);
            else Sample(clean.data(), w, h, 128, v, 128, 128, p);
        }
    }
    for (uint32_t y = 504; y < 640; y++)
        for (uint32_t x = 437; x < 587; x++)
            Sample(s, w, h, 103 + (x - 437 + 0.5f) / 3 - 0.5f, 126 + (y - 504 + 0.5f) / 4 - 0.5f, 0, float(w - 1),
                   &out[(size_t(y) * ow + x) * 4]);
    return true;
}

}  // namespace

// the textures by their pack keys (the hash of their bytes, size and format), seen in dumps
void AddOverlayMakers() {
    using gpu::texturedumper::AddMaker;
    AddMaker(0x12c86152af72d766ull, "@hidehud/256x256_12c86152af72d766_nv_clean.png", NightVision);
    AddMaker(0x1e0001d4be4e9b83ull, "@wide/256x256_1e0001d4be4e9b83_wide.png", Binoculars);
    AddMaker(0x3b9bd624ac58b47dull, "@wide/256x256_3b9bd624ac58b47d_wide.png", Scope);  // M24
    AddMaker(0xa171326c2f631404ull, "@wide/256x256_a171326c2f631404_wide.png", Scope);  // ACOG (M4)
    AddMaker(0xf20071ab3f20df6bull, "@wide/256x256_f20071ab3f20df6b_wide.png", Scope);  // M82
}
