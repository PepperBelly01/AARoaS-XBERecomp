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
// selected. A left click presses A over the selected item (or anywhere on a screen with no list, a "press A to go on"
// page), and does nothing off the list's items.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <cmath>
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

void OnScreenDraw(float x0, float y0, float x1, float y1) {
    if (g_stack.empty()) return;
    Drawn& d = g_frame[size_t(g_stack.back())];
    d.x0 = std::min(d.x0, x0);
    d.y0 = std::min(d.y0, y0);
    d.x1 = std::max(d.x1, x1);
    d.y1 = std::max(d.y1, y1);
    d.draws++;
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
        input::SetMenuClick(true);  // no list known: a click presses A ("press A to go on")
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
    input::SetMenuClick(hovered && hovered->element == g_selected);
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

void NewFrame() {
    uint32_t now = d3d::Presents();
    if (now == g_frame_no) return;
    g_frame_no = now;
    g_last.swap(g_frame);
    BoxBelow(g_last);
    g_frame.clear();
    g_stack.clear();
    FollowCursor();
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
