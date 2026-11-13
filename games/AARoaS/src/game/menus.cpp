// The game's menus with the mouse as on a PC: the item under the cursor is selected, a click picks it.
//
// The menus are Sidecar (CSidecarUI, CElement, script behaviours in UI\XBox\*.pkg), not Unreal. What is known:
// - CElement::Draw (0x36B170, thiscall on the element) runs for each element, its children inside it, so a stack of
//   the elements drawing gives each one its parent. The screen space draws made while an element is on top of the
//   stack are its own (d3d::OnScreenDraw: their boxes in the game's 640x480 pixels): an element and all below it
//   cover where it is. Element +0x2C its place among its parent's children, +0x30 visible (a byte, Draw skips it at 0).
// - CElement::Send (0x36B2A0, thiscall, the message; its first word the message's type). Type 0x17 with 0x12C in its
//   third word goes to the item selected, 0x12D to the one left (the script's NOTIFY_selected and _deselected); a
//   list's items are siblings. Type 0x15 tells the line of text at the foot of the main menu which item's text to
//   show. (Found by logging the messages around presses; the selection is not a plain variable in memory: a search
//   of all memory for a number or element that moved with Up and Down found none.)
//
// So the cursor: when the mouse moves over another item of the list the selection is in, Up or Down (Left or Right
// for a row of items) is pressed for it, a step at a time, each once the last was answered, until that item is
// selected. A left click presses A over the selected item (or anywhere on a screen with no list and no button prompts,
// a "press A to go on" page), and does nothing off the list's items. The button prompts drawn (the menus' button
// sheet, told by the screen draws' texture and cell) are buttons: a click on one or its label presses its button. On a
// tabbed page with an Edit (the Personnel Jacket) a click on the selected row presses X, Edit: A there leaves the page.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/input.h"
#include "core/log.h"
#include "kernel/memory.h"
#include "xdk/d3d8.h"
#include "xdk/hle.h"

namespace {

constexpr uint32_t kSelectMessage = 0x17, kSelected = 0x12C, kDeselected = 0x12D;
// a step: held two frames, then let go two frames before the next (a press held on into the next looked to the menu
// like one long hold, and stepped only at its own slow repeat; one frame held was sometimes not seen), about 20 a second
constexpr uint32_t kStepHoldMs = 34, kStepGapMs = 17;
constexpr uint32_t kStepTimeoutMs = 100;  // a step not answered by then (the press not seen): pressed again
constexpr float kPageWidth = 640, kPageHeight = 480;
constexpr uint32_t kEscapeSettleMs = 250;  // Escape (B) in the pause menu: a page change shows by then
constexpr int kNewPageElements = 8;       // that many elements not drawn before: another page
constexpr uint32_t kSelectorAnswerMs = 150;  // a step on a row with a selector: judged no sooner

struct Drawn {
    uint32_t element;
    int parent;  // index in this frame's list, -1 for a root
    int depth;
    uint32_t position;
    float x0 = 1e30f, y0 = 1e30f, x1 = -1e30f, y1 = -1e30f;  // its own draws, then (BoxBelow) all below it too
    int draws = 0;
};

std::vector<Drawn> g_frame, g_last;  // this frame's elements in draw order, and the last whole frame's
std::vector<int> g_stack;            // indices into g_frame of the elements drawing
uint32_t g_frame_no = ~0u;

uint32_t g_selected = 0;    // this frame: the selected item of the list worked
// each item's state as the messages left it: a page left and come back to keeps its selection without a new message
struct ItemState {
    bool selected;
    uint32_t frame;  // the Swap it was told in
};
std::unordered_map<uint32_t, ItemState> g_item_selected;
ULONGLONG g_woken_at = 0;   // a list with no known selection was woken (a Down pressed) then
uint32_t g_target = 0;      // the item the cursor went to, stepped toward
uint32_t g_unreachable = 0; // an item a step did not bring the selection nearer to (disabled: skipped over), left
                            // alone until the cursor goes to another
int g_step_gap = -1;        // the distance to the target when the last step was pressed, -1 for none
ULONGLONG g_step_at = 0;    // a step pressed then, not yet answered
int g_last_x = -1, g_last_y = -1;
bool g_paused = false;       // the game paused: the pause menu
uint32_t g_pause_frame = 0;  // the Swap the pause began in

bool Readable(uint32_t a, uint32_t n) { return a >= 0x10000 && mem::IsCommitted(a) && mem::IsCommitted(a + n - 1); }

// --- The button prompts (the menus' button sheet, 128x128: 32x32 cells, row 1 A X B, row 2 Start Y, row 3 left trigger
// Black, row 4 White right trigger; toolsmake_key_icons.ps1): each one drawn, with its box, so a click on it (or the label
// beside it) presses its button
struct Glyph {
    int col, row;
    float x0, y0, x1, y1;
};
std::vector<Glyph> g_glyphs, g_last_glyphs;  // this frame's, the last whole frame's

void NoteGlyph(const d3d::ScreenDraw& s) {
    if (s.tex_width != 128 || s.tex_height != 128) return;
    // the menus draw through a vertex shader: 48 byte vertices, the texture coordinates at +32 (seen); the first two
    // vertices are a quad's top edge, left to right
    float u0 = s.u0, u1 = s.u1, v0 = s.v0;
    if (!s.uv) {
        if (s.stride != 48) return;
        u0 = std::min(s.raw[0][8], s.raw[1][8]);
        u1 = std::max(s.raw[0][8], s.raw[1][8]);
        v0 = s.raw[0][9];
    }
    if (std::abs(u1 - u0 - 0.25f) > 0.02f) return;  // a cell across (inset half a texel each side)
    int col = int(std::lround(u0 * 4)), row = int(std::lround(v0 * 4));
    if (std::abs(u0 * 4 - col) > 0.1f || std::abs(v0 * 4 - row) > 0.1f || col > 3 || row > 3) return;
    if (s.x1 <= 0 || s.x0 >= kPageWidth) return;  // a tab not shown, drawn off the page
    float w = s.x1 - s.x0, h = s.y1 - s.y0;
    if (w < 8 || h < 8 || w > 80 || h > 80) return;
    if (g_glyphs.size() < 32) g_glyphs.push_back({col, row, s.x0, s.y0, s.x1, s.y1});
}

void OnScreenDraw(const d3d::ScreenDraw& s) {
    NoteGlyph(s);
    if (g_stack.empty()) return;
    float x0 = s.x0, y0 = s.y0, x1 = s.x1, y1 = s.y1;
    Drawn& d = g_frame[size_t(g_stack.back())];
    d.x0 = std::min(d.x0, x0);
    d.y0 = std::min(d.y0, y0);
    d.x1 = std::max(d.x1, x1);
    d.y1 = std::max(d.y1, y1);
    d.draws++;
}

// a prompt's press: an analog button's bit (Pad::analog), or Start
void PressGlyph(const Glyph& g) {
    static const int kAnalog[4][4] = {{0, 2, 1, -1}, {-2, 3, -1, -1}, {6, 4, -1, -1}, {5, 7, -1, -1}};  // [row][col], -2 Start
    int a = kAnalog[g.row][g.col];
    if (a == -2) input::PressMenuButtons(1u << 4, 2 * 34);
    else if (a >= 0) input::PressMenuAnalog(uint8_t(1u << a), 2 * 34);
    LOGR("menus: prompt clicked (sheet cell %d,%d)", g.col, g.row);
}

// the prompt under the cursor: its icon and, for all but the shoulders (the tabs' Q and E), the label right of it up to the
// next prompt on its line
const Glyph* GlyphAt(float x, float y) {
    for (const Glyph& g : g_last_glyphs) {
        float right = g.x1;
        bool shoulder = g.row == 2 && g.col == 0 || g.row == 3 && g.col == 1;
        if (!shoulder) {
            right = g.x1 + 130;
            for (const Glyph& o : g_last_glyphs)
                if (o.x0 > g.x1 && o.y0 < g.y1 && o.y1 > g.y0) right = std::min(right, o.x0 - 2);
        }
        if (x >= g.x0 && x < right && y >= g.y0 - 4 && y < g.y1 + 4) return &g;
    }
    return nullptr;
}

bool HasGlyph(int col, int row) {
    for (const Glyph& g : g_last_glyphs)
        if (g.col == col && g.row == row) return true;
    return false;
}

// each element's box widened by all below it (children follow their parent in draw order)
void BoxBelow(std::vector<Drawn>& list) {
    for (size_t i = list.size(); i-- > 0;) {
        int p = list[i].parent;
        if (p < 0 || list[i].x0 > list[i].x1) continue;
        Drawn& q = list[size_t(p)];
        q.x0 = std::min(q.x0, list[i].x0);
        q.y0 = std::min(q.y0, list[i].y0);
        q.x1 = std::max(q.x1, list[i].x1);
        q.y1 = std::max(q.y1, list[i].y1);
    }
}

// the cursor on the menu page: pages are drawn 4:3 in the middle of the window
bool CursorOnPage(float& x, float& y, int& wx, int& wy) {
    int w = 0, h = 0;
    if (!input::CursorInWindow(wx, wy, w, h) || w <= 0 || h <= 0) return false;
    float fw = std::min(float(w), float(h) * 4 / 3), fh = fw * 3 / 4;
    x = (float(wx) - (float(w) - fw) / 2) / fw * kPageWidth;
    y = (float(wy) - (float(h) - fh) / 2) / fh * kPageHeight;
    return true;
}

bool Inside(const Drawn& d, float x, float y) { return d.x0 <= d.x1 && x >= d.x0 && x < d.x1 && y >= d.y0 && y < d.y1; }

// --- Rows with a selector. Some pages send no selection messages (the mission select: its roles and its missions):
// their highlight is a child of the row that moves onto the selected tile (the white corners), the tiles its siblings,
// all one size and in a line. Found each frame: a sibling about a tile's size with its centre in one tile. The cursor
// over another tile steps the highlight there with Left / Right (Up / Down for a column), a step once the selector has
// stood still (it slides). A step that leaves it on the same tile: the tile cannot be selected (a locked role), given
// up on until the cursor goes to another.
struct SelectorRow {
    std::vector<const Drawn*> tiles;  // in order along the line
    const Drawn* selector = nullptr;
    int selected = -1;                // the tile under the selector
    bool column = false;
};

bool SameSize(const Drawn& a, const Drawn& b) {
    return std::abs((a.x1 - a.x0) - (b.x1 - b.x0)) < 2 && std::abs((a.y1 - a.y0) - (b.y1 - b.y0)) < 2;
}

// the row under the cursor; anywhere: any such row on the page
bool FindSelectorRow(float px, float py, SelectorRow& out, bool anywhere = false) {
    std::vector<std::vector<const Drawn*>> children(g_last.size());
    for (const Drawn& d : g_last)
        if (d.parent >= 0 && d.x0 <= d.x1) children[size_t(d.parent)].push_back(&d);
    for (size_t c = 0; c < g_last.size(); c++) {
        const std::vector<const Drawn*>& kids = children[c];
        if (kids.size() < 3) continue;
        for (const Drawn* t : kids) {
            if (!anywhere && !Inside(*t, px, py)) continue;
            // the tiles: t's siblings its size, in its line
            std::vector<const Drawn*> tiles;
            for (const Drawn* k : kids)
                if (SameSize(*k, *t) && (std::abs(k->y0 - t->y0) < 2 || std::abs(k->x0 - t->x0) < 2)) tiles.push_back(k);
            if (tiles.size() < 2) continue;
            bool column = std::abs(tiles[0]->x0 - tiles[1]->x0) < 2;
            std::sort(tiles.begin(), tiles.end(), [&](const Drawn* a, const Drawn* b) { return column ? a->y0 < b->y0 : a->x0 < b->x0; });
            // the selector: another sibling near a tile's size whose centre is in a tile
            float tw = t->x1 - t->x0, th = t->y1 - t->y0;
            for (const Drawn* s : kids) {
                if (std::find(tiles.begin(), tiles.end(), s) != tiles.end()) continue;
                float sw = s->x1 - s->x0, sh = s->y1 - s->y0;
                if (sw < tw * 0.8f || sw > tw * 1.6f || sh < th * 0.8f || sh > th * 1.6f) continue;
                float cx = (s->x0 + s->x1) / 2, cy = (s->y0 + s->y1) / 2;
                for (size_t i = 0; i < tiles.size(); i++)
                    if (Inside(*tiles[i], cx, cy)) {
                        out = {tiles, s, int(i), column};
                        return true;
                    }
            }
        }
    }
    return false;
}

// true when the cursor is over such a row and it was dealt with
bool FollowSelectorRow(float px, float py, bool moved) {
    SelectorRow row;
    int hovered = -1;
    if (FindSelectorRow(px, py, row))
        for (size_t i = 0; i < row.tiles.size(); i++)
            if (Inside(*row.tiles[i], px, py)) hovered = int(i);
    if (hovered < 0) {
        // such a row on the page, the cursor off its tiles: a click does nothing (it pressed A anywhere)
        SelectorRow any;
        if (!FindSelectorRow(px, py, any, true)) return false;
        input::SetMenuClick(false);
        input::SetMenuStart(false, false);
        return true;
    }
    input::SetMenuClick(hovered == row.selected);
    input::SetMenuStart(false, false);
    // Steps go in a burst, a press each hold and gap (the selector slides, and waiting for it each step was slow),
    // then once it has stood still the result is looked at: on the target, done; nearer, another burst for the rest;
    // on the tile it started from, the target cannot be selected (a locked role)
    static uint32_t target = 0, unreachable = 0, burst_from = 0;
    static int left = 0, dir_bit = 0;
    static float last_x = 0, last_y = 0;
    static int still = 0;
    static ULONGLONG pressed_at = 0;
    uint32_t over = row.tiles[size_t(hovered)]->element;
    if (over != unreachable) unreachable = 0;
    if (moved && over != unreachable && over != target) {
        target = over;
        left = 0;
        burst_from = 0;
    }
    bool settled = std::abs(row.selector->x0 - last_x) < 0.5f && std::abs(row.selector->y0 - last_y) < 0.5f;
    still = settled ? still + 1 : 0;
    last_x = row.selector->x0;
    last_y = row.selector->y0;
    ULONGLONG now = GetTickCount64();
    if (now - pressed_at < kStepHoldMs + kStepGapMs) return true;  // the last press let go long enough
    if (left > 0) {                                                // the burst goes on
        input::PressMenuButtons(uint16_t(dir_bit), kStepHoldMs);
        pressed_at = now;
        left--;
        return true;
    }
    int goal = -1;
    for (size_t i = 0; i < row.tiles.size(); i++)
        if (row.tiles[i]->element == target) goal = int(i);
    uint32_t at = row.tiles[size_t(row.selected)]->element;
    if (goal < 0 || goal == row.selected) {
        burst_from = 0;
        return true;
    }
    if (still < 2 || now - pressed_at < kSelectorAnswerMs) return true;  // the last burst still being taken
    if (burst_from && burst_from == at) {                               // it did not move: the target is locked
        unreachable = target;
        target = 0;
        burst_from = 0;
        return true;
    }
    int ahead = goal - row.selected;
    dir_bit = row.column ? (ahead > 0 ? 1 << 1 : 1 << 0) : (ahead > 0 ? 1 << 3 : 1 << 2);
    left = std::abs(ahead) - 1;
    burst_from = at;
    input::PressMenuButtons(uint16_t(dir_bit), kStepHoldMs);
    pressed_at = now;
    return true;
}

// once a frame, with the last whole frame's elements: hover and click
void FollowCursor() {
    if (input::MouseLooks()) {  // play: the mouse looks
        g_target = 0;
        input::SetMenuClick(true);
        input::SetMenuStart(false, false);
        input::TakeMenuEscape();
        return;
    }
    // Escape in the pause menu presses B; when B left the page as it was (the pause menu's first page, where B does
    // nothing) Start is pressed after it, which closes the pause menu. A page change makes the page's elements anew, so
    // "as it was" is: few elements drawn now that were not as Escape was pressed (a blink or an animation shows the
    // same ones again). Guessing the first page failed: each page is made afresh, and the HUD's lists linger beneath
    {
        static ULONGLONG escaped_at = 0;
        static std::unordered_map<uint32_t, bool> before;
        ULONGLONG now = GetTickCount64();
        if (input::TakeMenuEscape() && g_paused) {
            escaped_at = now;
            before.clear();
            for (const Drawn& d : g_last) before[d.element] = true;
        }
        if (escaped_at && now - escaped_at >= kEscapeSettleMs) {
            int fresh = 0;
            for (const Drawn& d : g_last) fresh += before.count(d.element) ? 0 : 1;
            if (g_paused && fresh < kNewPageElements) input::PressMenuButtons(1u << 4, kStepHoldMs * 2);
            escaped_at = 0;
        }
    }
    float px, py;
    int wx, wy;
    bool on_page = CursorOnPage(px, py, wx, wy);
    bool moved = on_page && (wx != g_last_x || wy != g_last_y);
    g_last_x = wx;
    g_last_y = wy;
    // a click (the left button going down), for the prompts and Edit below; the game sees a click as A only where
    // SetMenuClick allows it
    static bool was_down = false;
    bool down = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
    bool click = on_page && down && !was_down;
    was_down = down;
    // a click used here (a prompt, Edit) is not also A while the button stays down: the page it opened took the held
    // button as A and closed again
    // editing on a tabbed page (X there, clicked or pressed, and the tabs' Q and E gone): the field's arrows looked like a row
    // of tiles, and following the cursor stepped through its values. Only the prompts are followed until the tabs are back
    static bool editing = false, was_tabbed = false;
    static ULONGLONG x_at = 0;
    bool tabbed = HasGlyph(0, 2) || HasGlyph(1, 3);
    if ((GetAsyncKeyState('X') & 0x8000) && was_tabbed) x_at = GetTickCount64();
    if (was_tabbed && !tabbed && !g_last_glyphs.empty() && GetTickCount64() - x_at < 1500) editing = true;
    if (tabbed || g_last_glyphs.empty()) editing = false;
    was_tabbed = tabbed;
    static bool used = false;
    if (!down) used = false;
    if (used) {
        input::SetMenuClick(false);
        return;
    }
    if (const Glyph* g = on_page ? GlyphAt(px, py) : nullptr) {  // a prompt under the cursor: its button
        input::SetMenuClick(false);
        if (click) {
            PressGlyph(*g);
            used = true;
            if (g->col == 1 && g->row == 0) x_at = GetTickCount64();  // the X prompt: Edit
        }
        return;
    }
    if (editing) {
        input::SetMenuClick(false);
        g_target = 0;
        return;
    }
    if (on_page && FollowSelectorRow(px, py, moved)) return;
    // the selected items drawn (a list each); the list worked is the one with an item under the cursor, else the one
    // in front
    int selected = -1, topmost = -1;
    const Drawn* hovered = nullptr;
    for (size_t i = 0; i < g_last.size(); i++) {
        auto state = g_item_selected.find(g_last[i].element);
        if (state == g_item_selected.end() || !state->second.selected || g_last[i].parent < 0) continue;
        // paused: only the lists told since the pause began are the pause menu's (the HUD's, left from play, are not)
        if (g_paused && state->second.frame < g_pause_frame) continue;
        const Drawn* over = nullptr;
        for (const Drawn& d : g_last)
            if (on_page && d.parent == g_last[i].parent && Inside(d, px, py)) over = &d;
        // the page in front is drawn last: a later list wins, unless only an earlier one is under the cursor
        topmost = int(i);
        if (over) {
            selected = int(i);
            hovered = over;
        } else if (!hovered) {
            selected = int(i);
        }
    }
    if (selected < 0) {
        g_target = 0;
        g_selected = 0;
        // no list known: a click presses A ("press A to go on", or the main menu before its selection is told: its
        // first item), except on a tabbed page (Q and E shown: the Personnel Jacket, where A leaves): its prompts are
        // clicked instead
        input::SetMenuClick(!(HasGlyph(0, 2) || HasGlyph(1, 3)));
        // the title page (before the menus have said any item is selected): Enter and a click press Start too. Not
        // later: a page whose items send no selection (the mission select) took A and Start as two presses
        input::SetMenuStart(g_item_selected.empty(), false);
        // a list that has not said which item is selected (a page opened, its first item selected without a
        // message): the mouse moving wakes it with a Down, and its answer says
        ULONGLONG now = GetTickCount64();
        if (moved && now - g_woken_at > 1500) {
            input::PressMenuButtons(1u << 1, kStepHoldMs);
            g_woken_at = now;
        }
        return;
    }
    g_selected = g_last[size_t(selected)].element;
    int list = g_last[size_t(selected)].parent;
    input::SetMenuStart(false, false);
    if (!on_page) {
        input::SetMenuClick(false);
        return;
    }
    // a tabbed page (the Personnel Jacket: Q and E shown) with an Edit (X): a click on the selected row edits it, A
    // there leaves the page
    bool edits = (HasGlyph(0, 2) || HasGlyph(1, 3)) && HasGlyph(1, 0);
    input::SetMenuClick(!edits && hovered && hovered->element == g_selected);
    if (edits && click && hovered && hovered->element == g_selected) {
        input::PressMenuAnalog(1u << 2, 2 * 34);
        used = true;
        x_at = GetTickCount64();  // Edit: see editing above
    }
    if (hovered && hovered->element != g_unreachable) g_unreachable = 0;  // the cursor went to another item
    if (moved && hovered && hovered->element != g_unreachable) {
        if (hovered->element != g_target) g_step_gap = -1;
        g_target = hovered->element;
    }
    if (!g_target || g_target == g_selected) {
        g_target = 0;
        return;
    }
    // a step toward the target, once the last one was answered
    const Drawn* target = nullptr;
    for (const Drawn& d : g_last)
        if (d.element == g_target && d.parent == list) target = &d;
    if (!target) {
        g_target = 0;
        return;
    }
    ULONGLONG now = GetTickCount64();
    static ULONGLONG pressed_at = 0;
    if (now - pressed_at < kStepHoldMs + kStepGapMs) return;    // the last press let go long enough
    if (g_step_at && now - g_step_at < kStepTimeoutMs) return;  // and answered (or given up on)
    const Drawn& from = g_last[size_t(selected)];
    int ahead = int(target->position) - int(from.position);
    if (!ahead) {
        g_step_gap = -1;
        return;
    }
    // the last step, answered, left it no nearer: the target is disabled (the menu skips it, and wraps): given up on.
    // A step not answered (the press not seen) is only pressed again.
    bool answered = g_step_at == 0;
    static int misses = 0;  // presses in a row not answered: three, and the end of a list that does not wrap
    misses = answered ? 0 : misses + 1;
    if ((answered && g_step_gap >= 0 && std::abs(ahead) >= g_step_gap) || misses >= 3) {
        misses = 0;
        g_unreachable = g_target;
        g_target = 0;
        g_step_gap = -1;
        return;
    }
    g_step_gap = std::abs(ahead);
    // a column of items steps with Up and Down, a row with Left and Right (D-pad bits 0 1 2 3)
    bool row = std::abs((target->x0 + target->x1) - (from.x0 + from.x1)) > std::abs((target->y0 + target->y1) - (from.y0 + from.y1));
    uint16_t bit = row ? (ahead > 0 ? 1u << 3 : 1u << 2) : (ahead > 0 ? 1u << 1 : 1u << 0);
    input::PressMenuButtons(bit, kStepHoldMs);
    pressed_at = now;
    g_step_at = now;
}

// --- Probe (F9 in a menu, 10 seconds): the rarer messages (Send hook) and the elements that jump after standing
// still, each with where it is, to find how a page without selection messages (the mission select) shows its highlight
ULONGLONG g_probe_until = 0;
int g_probe_lines = 0;

bool Probing() { return g_probe_until && GetTickCount64() < g_probe_until && g_probe_lines > 0; }

void ProbeFrame() {
    static bool held = false;
    bool down = (GetAsyncKeyState(VK_F9) & 0x8000) != 0;
    if (down && !held) {
        g_probe_until = GetTickCount64() + 10000;
        g_probe_lines = 600;
        LOGR("menus: probe for 10 seconds, %zu elements drawn", g_last.size());
    }
    held = down;
    struct Was {
        float x0, y0, x1, y1;
        uint32_t moved_at;
    };
    static std::unordered_map<uint32_t, Was> was;
    if (!Probing()) {
        was.clear();
        return;
    }
    for (const Drawn& d : g_last) {
        if (d.x0 > d.x1) continue;
        auto it = was.find(d.element);
        if (it == was.end()) {
            was[d.element] = {d.x0, d.y0, d.x1, d.y1, 0};
            continue;
        }
        Was& w = it->second;
        bool moved = std::abs(d.x0 - w.x0) > 4 || std::abs(d.y0 - w.y0) > 4;
        if (moved && w.moved_at + 1 != g_frame_no && g_probe_lines-- > 0)
            LOGR("menus: jumped %08X parent %08X pos %u from %.0f,%.0f-%.0f,%.0f to %.0f,%.0f-%.0f,%.0f", d.element,
                 d.parent >= 0 ? g_last[size_t(d.parent)].element : 0, d.position, w.x0, w.y0, w.x1, w.y1, d.x0, d.y0,
                 d.x1, d.y1);
        if (moved) w.moved_at = g_frame_no;
        w.x0 = d.x0;
        w.y0 = d.y0;
        w.x1 = d.x1;
        w.y1 = d.y1;
    }
}

std::string WhereIs(uint32_t element) {
    for (const Drawn& d : g_last)
        if (d.element == element) {
            char text[96];
            std::snprintf(text, sizeof(text), "parent %08X pos %u box %.0f,%.0f-%.0f,%.0f",
                          d.parent >= 0 ? g_last[size_t(d.parent)].element : 0, d.position, d.x0, d.y0, d.x1, d.y1);
            return text;
        }
    return "not drawn";
}

void NewFrame() {
    uint32_t now = d3d::Presents();
    if (now == g_frame_no) return;
    g_frame_no = now;
    g_last.swap(g_frame);
    g_last_glyphs.swap(g_glyphs);
    g_glyphs.clear();
    BoxBelow(g_last);
    g_frame.clear();
    g_stack.clear();
    FollowCursor();
    ProbeFrame();
}

}  // namespace

// CElement::Draw (thiscall, no arguments)
X86_EXTERN_FUNC(__imp__sub_0036B170);
X86_FUNC(sub_0036B170) {
    static bool hooked = false;
    if (!hooked) {
        hooked = true;
        d3d::OnScreenDraw(OnScreenDraw);
    }
    NewFrame();
    uint32_t element = ctx.ecx;
    if (!Readable(element, 0x34) || X86_MEM8(element + 0x30) == 0) {  // not drawn
        __imp__sub_0036B170(ctx, base);
        return;
    }
    int parent = g_stack.empty() ? -1 : g_stack.back();
    g_frame.push_back({element, parent, int(g_stack.size()), X86_MEM32(element + 0x2C)});
    g_stack.push_back(int(g_frame.size()) - 1);
    __imp__sub_0036B170(ctx, base);
    if (!g_stack.empty()) g_stack.pop_back();
}

// CElement::Send (thiscall: the message)
X86_EXTERN_FUNC(__imp__sub_0036B2A0);
X86_FUNC(sub_0036B2A0) {
    uint32_t element = ctx.ecx, message = X86_MEM32(ctx.esp + 4);
    if (Probing() && Readable(message, 16)) {
        uint32_t type = X86_MEM32(message);
        if (type != 2 && type != 8 && type != 9 && type != 0xB && type != 0x13 && g_probe_lines-- > 0)  // per frame
            LOGR("menus: send %08X type %X: %08X %08X %08X, %s", element, type, X86_MEM32(message + 4),
                 X86_MEM32(message + 8), X86_MEM32(message + 12), WhereIs(element).c_str());
    }
    // a message goes to the item, then down through all inside it, the same message: its first receiver is the item
    static uint32_t last_message = 0, last_frame = ~0u, last_code = 0;
    if (Readable(message, 12) && X86_MEM32(message) == kSelectMessage) {
        uint32_t code = X86_MEM32(message + 8);
        bool first = message != last_message || last_frame != d3d::Presents() || code != last_code;
        last_message = message;
        last_frame = d3d::Presents();
        last_code = code;
        if (first && (code == kSelected || code == kDeselected)) {
            g_item_selected[element] = {code == kSelected, d3d::Presents()};
            if (code == kSelected) g_step_at = 0;  // the step answered
        }
    }
    __imp__sub_0036B2A0(ctx, base);
}

// mouse.cpp, each frame: the game paused or not; a new pause learns its first page afresh
void MenusPaused(bool paused) {
    if (paused && !g_paused) {
        g_pause_frame = d3d::Presents();
    }
    g_paused = paused;
}
