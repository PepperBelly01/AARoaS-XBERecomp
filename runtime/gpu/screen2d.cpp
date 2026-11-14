#include "gpu/screen2d.h"

#include <algorithm>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstring>
#include <numeric>
#include <unordered_map>
#include <vector>

#include "app/app.h"
#include "core/log.h"

namespace gpu::screen2d {

namespace {

std::atomic<uint64_t> g_frames{0};   // presents so far
std::atomic<int> g_trace{0};         // frames left to log the decisions of
std::atomic<uint64_t> g_last3d{0};   // g_frames + 1 at the last 3D draw, 0 for none
constexpr uint64_t kHold = 10;       // frames a 3D frame's shape outlasts it: a loading frame does not flicker 4:3

constexpr float kNarrow = 0.75f;     // a 4:3 shape's width on a 16:9 frame
constexpr float kFill = 0.85f;       // a draw this share of the width across still fills it
constexpr size_t kMaxGroups = 1500;  // more pieces than this are placed as one

struct Box {
    float x0, y0, x1, y1;
};

uint32_t Find(std::vector<uint32_t>& parent, uint32_t i) {
    while (parent[i] != i) i = parent[i] = parent[parent[i]];
    return i;
}

// A canvas element is several draws in a row (a box as a 3x3 grid, then its text, letter by letter): they move
// together. The HUD's elements overlap in their 4:3 layout (the subtitle box spans the width behind the ammo panel and
// compass), so place alone cannot group them; draw order can. Each frame's canvas draws (clip space boxes, render
// thread) are cut into runs, a draw that touches the run so far joining it; the next frame places the draw at the
// same position in the order, where its box still matches, by its run's centre. A changed layout is placed draw by
// draw for its first frame.
// A draw is matched to last frame's by its box, not its place in the order: text typed out letter by letter adds
// draws each frame. A draw with no match that touches the run being drawn joins it at once.
//
// 2D that follows the world (a name over a soldier's head, the sun's flare) is placed by the game on its 4:3 frame
// from a 3D position: on the 16:9 frame it belongs where the 3D view puts that position, narrowed about the centre,
// whatever third it is in. Anchored by its third it jumped from one side's anchor to another as it crossed a third.
// It is told from the HUD by moving: a draw with no match last frame, the same size as one of last frame's a little
// way off, is the same thing moved; its run is centred from then on (and while it stands still, matched by its box).
struct Last {
    Canvas anchor;
    bool world;  // follows the world: centred
};
std::vector<Box> g_drawn;                          // this frame's canvas draws in order
std::vector<bool> g_drawn_world;                   // each one found to follow the world
std::unordered_map<uint64_t, Last> g_last;         // last frame's, by box, the anchor of each one's run
std::vector<Box> g_last_boxes;                     // last frame's, for finding a moved one
constexpr float kSameSize = 0.004f;                // clip space: a box this close in width and height is the same
constexpr float kMoved = 0.2f;                     // ... and moved, this near (a turn's worth in a frame)
// this frame so far: any canvas draw yet, the first was across the screen (the dimmed layer of a pause menu or
// briefing over the 3D view: the frame is a menu, all of it kept 4:3 about the centre), the run being drawn
bool g_any = false, g_menu = false, g_in_run = false;
// A menu frame stays one: last frame was a menu, so this one is from its first draw, kept if a draw across the screen
// comes at all (the mission end screen sometimes drew another piece first, and taken for the HUD its text in the left
// third went to the left edge, the line broken up; user, 2026-11-13). No draw across: the menu is over.
// Held only while a band shows (across, not down the whole screen): play draws a full screen layer every frame after
// a cutscene (its fade), which held the menu on and bunched the HUD into the middle (user, 2026-11-13).
bool g_menu_last = false, g_menu_held = false, g_across = false, g_across_first = false, g_band = false;
Box g_run{};
Canvas g_run_anchor = Canvas::Centre;
// clip space (the frame is 2 across): a run takes a draw this near it, a word space across, touching up and down
constexpr float kGapX = 0.06f, kGapY = 0.02f;

bool Touches(const Box& p, const Box& q) {
    return q.x0 <= p.x1 + kGapX && p.x0 <= q.x1 + kGapX && q.y0 <= p.y1 + kGapY && p.y0 <= q.y1 + kGapY;
}

Box Join(const Box& p, const Box& q) {
    return {std::min(p.x0, q.x0), std::min(p.y0, q.y0), std::max(p.x1, q.x1), std::max(p.y1, q.y1)};
}

// a box to 1/400 of the frame, as a key
uint64_t BoxKey(const Box& b) {
    auto q = [](float v) { return uint64_t(uint16_t(int16_t(std::lround(v * 200)))); };
    return q(b.x0) | q(b.y0) << 16 | q(b.x1) << 32 | q(b.y1) << 48;
}

Canvas AnchorOf(const Box& b) {
    float centre = (b.x0 + b.x1) * 0.5f;
    return centre < -1.0f / 3 ? Canvas::Left : centre > 1.0f / 3 ? Canvas::Right : Canvas::Centre;
}

void GroupDrawn() {
    size_t n = g_drawn.size();
    g_last.clear();
    for (size_t start = 0; start < n;) {
        Box run = g_drawn[start];
        bool world = g_drawn_world[start];
        size_t end = start + 1;
        for (; end < n && Touches(run, g_drawn[end]); end++) {
            run = Join(run, g_drawn[end]);
            world = world || g_drawn_world[end];
        }
        Last last{world ? Canvas::Centre : AnchorOf(run), world};
        for (size_t i = start; i < end; i++) g_last[BoxKey(g_drawn[i])] = last;
        start = end;
    }
    g_last_boxes.swap(g_drawn);
    g_drawn.clear();
    g_drawn_world.clear();
    if (g_menu_held && g_across && !g_across_first) {
        static int told = 0;
        if (told++ < 20) LOGR("2d: a menu frame began with another draw; kept a menu (%d)", told);
    }
    g_menu_last = g_menu && g_band;
    g_any = g_menu = g_in_run = g_menu_held = g_across = g_across_first = g_band = false;
}

// a box new this frame that is one of last frame's moved a little: it follows the world
bool Moved(const Box& b) {
    float w = b.x1 - b.x0, h = b.y1 - b.y0;
    for (const Box& o : g_last_boxes) {
        if (std::abs((o.x1 - o.x0) - w) > kSameSize || std::abs((o.y1 - o.y0) - h) > kSameSize) continue;
        float dx = std::abs(o.x0 - b.x0), dy = std::abs(o.y0 - b.y0);
        if (dx + dy > 0.001f && dx <= kMoved && dy <= kMoved) return true;
    }
    return false;
}

// One element of a 3D frame's 2D, its box in clip space (y up), as it comes in the order: where it goes. Both kinds
// of 2D come through here in draw order, canvas draws and screen space ones (a briefing's text is a draw a letter).
// additive (blended, the destination added in full): a lens flare's pieces
bool Additive(const State& s) { return s.simple[kMBlendEnable / 4] && s.simple[kMDestBlend / 4] == 1; }

// additive: the draw is light added (Additive). One that follows the world is placed where the game put it: the flare
// is placed through the camera's projection, already widened, so narrowing it again set the sun's core apart from the
// sun (two suns); name tags, placed by the game's own 4:3 sums, are narrowed
Canvas Decide(const Box& b, bool hide, bool wide, bool additive) {
    bool first = !g_any;
    g_any = true;
    bool across = b.x1 - b.x0 >= 2 * kFill;
    if (first && !across && g_menu_last) g_menu = g_menu_held = true;  // last frame's menu, until shown otherwise
    if (across) {  // across the screen: fades, flashes, overlays still fill it
        // a band (across, not down the whole screen) later in the frame starts a menu too: the death screen's subtitle
        // (the HUD's, its 9-slice pieces each short of the width) came first. A full screen layer (a flash, a fade, a
        // scope) does not: the HUD drawn after it stays the HUD
        bool band = b.y1 - b.y0 < 2 * kFill;
        if (band) g_band = true;
        if (!first && band && !g_menu) {
            g_menu = true;
            static int told = 0;
            if (told++ < 10) LOGR("2d: a band across the screen began a menu: %.3f,%.3f-%.3f,%.3f", b.x0, b.y0, b.x1, b.y1);
        }
        if (first) g_menu = g_across_first = true;
        g_across = true;
        if (g_trace.load() > 0)
            LOGR("2d trace: %.3f,%.3f-%.3f,%.3f (%.3fx%.3f) across%s", b.x0, b.y0, b.x1, b.y1, b.x1 - b.x0, b.y1 - b.y0,
                 g_menu ? ", menu" : "");
        return Canvas::Keep;
    }
    if (g_menu) return wide ? Canvas::Centre : Canvas::Keep;  // a menu over the 3D view: as made, hidden or not
    if (hide) return Canvas::Hide;
    if (!wide) return Canvas::Keep;
    // the same draw last frame: its run's anchor; new, touching the run being drawn: that run's; new and moved: it
    // follows the world, centred; else its own
    Canvas anchor;
    bool world = false;
    auto last = g_last.find(BoxKey(b));
    bool joins = g_in_run && Touches(g_run, b);
    if (last != g_last.end()) {
        anchor = last->second.anchor;
        world = last->second.world;
    } else if (additive) {
        // new light added over the 3D view (a lens flare's piece) follows the world from its first frame: as a new
        // piece placed by its third it was narrowed for that frame (the sun coming into view, or a fast turn moving a
        // piece past kMoved), a second sun flashing apart from the first (user, 2026-11-13)
        anchor = Canvas::Centre;
        world = true;
    } else if (joins) {
        anchor = g_run_anchor;
    } else if (Moved(b)) {
        anchor = Canvas::Centre;
        world = true;
    } else {
        anchor = AnchorOf(b);
    }
    if (g_drawn.size() < kMaxGroups) {
        g_drawn.push_back(b);
        g_drawn_world.push_back(world);
    }
    if (g_trace.load() > 0)
        LOGR("2d trace: %.3f,%.3f-%.3f,%.3f (%.3fx%.3f) %s%s", b.x0, b.y0, b.x1, b.y1, b.x1 - b.x0, b.y1 - b.y0,
             anchor == Canvas::Left ? "left" : anchor == Canvas::Right ? "right" : "centre",
             world ? (additive ? " world, additive: kept" : " world") : last != g_last.end() ? " same" : joins ? " joins" : " new");
    g_run = joins ? Join(g_run, b) : b;
    if (!joins) g_run_anchor = anchor;
    g_in_run = true;
    if (world && additive) return Canvas::Keep;
    return anchor;
}

}  // namespace

void Trace() { g_trace = 3; }

bool Wanted() { return app::kGame.screen_2d && (app::Widescreen() || app::Get().hide_hud); }

void Note3D() { g_last3d.store(g_frames.load() + 1, std::memory_order_relaxed); }

void EndFrame() {
    if (g_trace.load() > 0) {
        LOGR("2d trace: frame end");
        g_trace.fetch_sub(1);
    }
    GroupDrawn();
    g_frames.fetch_add(1);
}

bool Frame3D() {
    uint64_t last = g_last3d.load(), now = g_frames.load() + 1;
    return last && now - last <= kHold;
}

bool Place(const State& s, Assembled& out, int k) {
    if (!Frame3D()) return true;  // a menu or movie frame: shown 4:3 as made
    bool hide = app::Get().hide_hud, wide = app::Widescreen();
    if (!hide && !wide) return true;
    uint32_t n = out.vertex_count, regs = uint32_t(std::popcount(out.regs));
    if (!n || !regs) return true;
    auto x = [&](uint32_t v) -> float& { return out.vertices[(size_t(v) * regs + k) * 4]; };
    auto y = [&](uint32_t v) -> float& { return out.vertices[(size_t(v) * regs + k) * 4 + 1]; };

    // pieces: vertices joined by a primitive
    std::vector<uint32_t> parent(n);
    std::iota(parent.begin(), parent.end(), 0u);
    uint32_t per = out.topology == Topology::Triangles ? 3 : out.topology == Topology::Lines ? 2 : 1;
    const auto& ix = out.indices;
    for (size_t i = 0; i + per <= ix.size(); i += per)
        for (uint32_t j = 1; j < per; j++)
            if (ix[i] < n && ix[i + j] < n) parent[Find(parent, ix[i + j])] = Find(parent, ix[i]);

    std::vector<uint32_t> root_of(n), group_of_root(n, ~0u);
    std::vector<Box> boxes;
    for (uint32_t v = 0; v < n; v++) {
        uint32_t r = Find(parent, v);
        root_of[v] = r;
        if (group_of_root[r] == ~0u) {
            group_of_root[r] = uint32_t(boxes.size());
            boxes.push_back({x(v), y(v), x(v), y(v)});
        }
        Box& b = boxes[group_of_root[r]];
        b.x0 = std::min(b.x0, x(v));
        b.x1 = std::max(b.x1, x(v));
        b.y0 = std::min(b.y0, y(v));
        b.y1 = std::max(b.y1, y(v));
    }

    // groups: pieces that touch or nearly do (the letters of a line, an icon and its frame)
    float vx = float(s.viewport.x), vw = float(s.viewport.w), vh = float(s.viewport.h);
    float gap_x = vw * 0.02f, gap_y = vh * 0.02f;
    size_t pieces = boxes.size();
    std::vector<uint32_t> group(pieces);
    std::iota(group.begin(), group.end(), 0u);
    if (pieces > kMaxGroups) {
        std::fill(group.begin(), group.end(), 0u);
    } else {
        for (size_t a = 0; a < pieces; a++)
            for (size_t b = a + 1; b < pieces; b++) {
                const Box &p = boxes[a], &q = boxes[b];
                bool near = p.x0 <= q.x1 + gap_x && q.x0 <= p.x1 + gap_x && p.y0 <= q.y1 + gap_y && q.y0 <= p.y1 + gap_y;
                if (near) group[Find(group, uint32_t(b))] = Find(group, uint32_t(a));
            }
    }
    std::vector<Box> span(pieces, Box{1e30f, 1e30f, -1e30f, -1e30f});
    size_t groups = 0;
    for (size_t p = 0; p < pieces; p++) {
        if (Find(group, uint32_t(p)) == p) groups++;
        Box& g = span[Find(group, uint32_t(p))];
        g.x0 = std::min(g.x0, boxes[p].x0);
        g.x1 = std::max(g.x1, boxes[p].x1);
        g.y0 = std::min(g.y0, boxes[p].y0);
        g.y1 = std::max(g.y1, boxes[p].y1);
    }

    // one element (a letter, an icon): placed with the frame's other 2D, in clip space
    if (groups == 1) {
        const Box& g = span[Find(group, 0)];
        Box c{(g.x0 - vx) / vw * 2 - 1, 1 - (g.y1 - float(s.viewport.y)) / vh * 2, (g.x1 - vx) / vw * 2 - 1,
              1 - (g.y0 - float(s.viewport.y)) / vh * 2};
        Canvas d = Decide(c, hide, wide, Additive(s));
        if (d == Canvas::Hide) return false;
        if (d != Canvas::Left && d != Canvas::Centre && d != Canvas::Right) return true;
        float at = d == Canvas::Left ? vx : d == Canvas::Right ? vx + vw : vx + vw / 2;
        for (uint32_t v = 0; v < n; v++) x(v) = at + (x(v) - at) * kNarrow;
        return true;
    }
    // several (a batched layer): each group placed by itself
    if (hide && !g_menu) return false;
    if (!wide) return true;

    // each group narrowed about its anchor: the screen side it sits nearer, or the centre
    std::vector<float> anchor(pieces, 0);
    std::vector<bool> keep(pieces, false);
    for (size_t p = 0; p < pieces; p++) {
        if (Find(group, uint32_t(p)) != p) continue;
        const Box& g = span[p];
        if (g.x1 - g.x0 >= vw * kFill) {
            keep[p] = true;
            continue;
        }
        float centre = (g.x0 + g.x1) * 0.5f - vx;
        anchor[p] = g_menu ? vx + vw / 2 : centre < vw / 3 ? vx : centre > vw * 2 / 3 ? vx + vw : vx + vw / 2;
    }
    for (uint32_t v = 0; v < n; v++) {
        uint32_t g = Find(group, group_of_root[root_of[v]]);
        if (keep[g]) continue;
        x(v) = anchor[g] + (x(v) - anchor[g]) * kNarrow;
    }
    return true;
}

namespace {

// D3D8 transform numbers, as State keeps them
constexpr size_t kWorld = 256, kView = 2, kProjection = 3;

void Mul(const float* a, const float* b, float* out) {
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++) {
            float v = 0;
            for (int k = 0; k < 4; k++) v += a[r * 4 + k] * b[k * 4 + c];
            out[r * 4 + c] = v;
        }
}

// the draw's position element, as three floats a vertex; false when it is in another form
bool Positions(const Assembled& a, std::vector<float>& xyz) {
    uint32_t n = a.vertex_count;
    if (!n || !(a.regs & 1)) return false;
    xyz.resize(size_t(n) * 3);
    if (!a.vertices.empty()) {
        uint32_t regs = uint32_t(std::popcount(a.regs));  // register 0 comes first
        for (uint32_t v = 0; v < n; v++)
            for (int i = 0; i < 3; i++) xyz[v * 3 + i] = a.vertices[size_t(v) * regs * 4 + i];
        return true;
    }
    const VertexElement& e = a.elements[0];
    const VertexBuffer& b = a.buffers[e.buffer];
    bool floats = e.format == 2 || e.format == 6 || e.format == 16;  // R32G32B32A32, R32G32B32, R32G32 _FLOAT
    if (!b.src || !floats || !b.stride) return false;
    uint32_t comps = e.format == 16 ? 2 : 3;
    for (uint32_t v = 0; v < n; v++) {
        size_t at = size_t(v) * b.stride + e.offset;
        if (at + comps * 4 > b.bytes) return false;
        float f[3] = {0, 0, 0};
        std::memcpy(f, b.src + at, comps * 4);
        for (int i = 0; i < 3; i++) xyz[v * 3 + i] = f[i];
    }
    return true;
}

}  // namespace

Canvas Classify(const State& s, const Assembled& a) {
    if (!app::kGame.screen_2d || a.pretransformed || (s.vs && !s.vs->program.empty())) return Canvas::Not;
    const auto& p = s.transforms[kProjection];
    bool ortho = p[3] == 0 && p[7] == 0 && p[11] == 0 && p[15] == 1;
    if (!ortho) return Canvas::Not;
    if (!Frame3D()) return Canvas::Keep;  // a menu frame, shown 4:3
    bool hide = app::Get().hide_hud, wide = app::Widescreen();
    if (!hide && !wide) return Canvas::Keep;

    float wv[16], wvp[16];
    Mul(s.transforms[kWorld].data(), s.transforms[kView].data(), wv);
    Mul(wv, p.data(), wvp);
    static std::vector<float> xyz;
    if (!Positions(a, xyz)) return hide ? Canvas::Hide : Canvas::Keep;
    Box b{1e30f, 1e30f, -1e30f, -1e30f};
    for (size_t v = 0; v * 3 < xyz.size(); v++) {
        const float* q = &xyz[v * 3];
        float cx = q[0] * wvp[0] + q[1] * wvp[4] + q[2] * wvp[8] + wvp[12];
        float cy = q[0] * wvp[1] + q[1] * wvp[5] + q[2] * wvp[9] + wvp[13];
        float cw = q[0] * wvp[3] + q[1] * wvp[7] + q[2] * wvp[11] + wvp[15];
        if (cw == 0) continue;
        b.x0 = std::min(b.x0, cx / cw);
        b.x1 = std::max(b.x1, cx / cw);
        b.y0 = std::min(b.y0, cy / cw);
        b.y1 = std::max(b.y1, cy / cw);
    }
    if (b.x0 > b.x1) return Canvas::Keep;
    return Decide(b, hide, wide, Additive(s));
}

void Narrow(Canvas anchor, float m[16]) {
    float a = anchor == Canvas::Left ? -1.0f : anchor == Canvas::Right ? 1.0f : 0.0f;
    // x' = k x + (1 - k) a w, after m
    for (int r = 0; r < 4; r++) m[r * 4 + 0] = kNarrow * m[r * 4 + 0] + (1 - kNarrow) * a * m[r * 4 + 3];
}

}  // namespace gpu::screen2d
