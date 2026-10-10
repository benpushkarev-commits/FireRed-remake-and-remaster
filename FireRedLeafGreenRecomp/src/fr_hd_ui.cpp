// fr_hd_ui.cpp — HD interface core: overlay entry, scene routing, field
// windows (dialogue, menus, lists, pause menu) and pointer targets.
// See fr_hd_ui.h.

#include "fr_hd_ui.h"
#include "fr_pc_input.h"
#include "fr_diorama.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "fr_guest.h"
#include "fr_hd_capture.h"
#include "fr_hd_draw.h"
#include "fr_hd_text.h"
#include "fr_settings_menu.h"
#include "gba_ppu.h"
#include "host_overlay.h"
#include "host_window.h"
#include "runtime.h"

namespace frlg {
namespace hd {
bool fonts_prepare(int dw, int dh);
bool fonts_ready();
void set_view_width_hint(float vw);
void set_modern_text(bool on);
Tex tex_window_pixels(const Mem& m, int window, std::uint32_t gen);
// Other renderers (fr_hd_battle.cpp / fr_hd_screens.cpp).
bool battle_active(const Mem& m, const Scene& s);
void battle_guest_frame(const Mem& m, const Scene& s, unsigned* bg_mask, bool* hide_objs);
int battle_obj_hide(int index, std::uint16_t a0, std::uint16_t a1, std::uint16_t a2);
void draw_battle(Ctx& c);
bool screens_draw(Ctx& c);  // party / bag / summary / title menu ... (true = drew)
bool screens_active(const Mem& m, const Scene& s);
void draw_settings_page(Ctx& c, const SettingsHdView& sv);
}  // namespace hd

namespace {
using namespace hd;

constexpr std::uint32_t kCB2_Overworld      = 0x080565B4;
constexpr std::uint32_t kCB2_OverworldBasic = 0x080565A8;
constexpr std::uint32_t kBattleMainCB2      = 0x08011100;
constexpr std::uint32_t kStartMenuWindowId  = 0x0203ABE0;
constexpr std::uint32_t kStartMenuOrder     = 0x020370F6;
constexpr std::uint32_t kStartMenuDescPtrs  = 0x083A7394;
constexpr std::uint32_t kHelpWindowId       = 0x0203B020;
constexpr std::uint32_t kMapHeader          = 0x02036DFC;
constexpr std::uint32_t kMapNames           = 0x083F1CAC;

struct TargetRec {
    float x, y, w, h;
    HdHitKind kind;
    int index;
};
std::vector<TargetRec> g_build;
std::vector<TargetRec> g_targets;
bool g_owns_pointer = false;
bool g_owns_build = false;

bool g_parts[kHdPartCount] = {true, true, true, true, true, true};
bool any_part() { for (bool b : g_parts) if (b) return true; return false; }
void (*g_prev_overlay)(gbarecomp::HostOverlay*) = nullptr;
double g_last_t = 0;

float ease_out(float x) {
    x = std::clamp(x, 0.0f, 1.0f);
    return 1.0f - (1.0f - x) * (1.0f - x) * (1.0f - x);
}
float approach(float cur, float target, float dt, float speed) {
    return cur + (target - cur) * std::min(1.0f, dt * speed);
}

bool is_field(std::uint32_t cb2) { return cb2 == kCB2_Overworld || cb2 == kCB2_OverworldBasic; }

std::string location_name(const Mem& m) {
    const int sec = m.u8(kMapHeader + 0x14);
    if (sec < 0x58 || sec >= 0x58 + 109) return {};
    return rom_string(m, m.u32(kMapNames + (sec - 0x58) * 4), 24, true);
}

// ── window placement (mirrors the widescreen edge anchoring) ─────────────
struct Placed {
    int id = -1;
    WindowInfo info;
    float x = 0, y = 0, w = 0, h = 0;  // view units, window interior
};

Placed place_window(const Ctx& c, int id) {
    Placed p;
    p.id = id;
    p.info = window_info(c.m, id);
    const Rect r = window_rect(c.m, id);
    const bool at_left = p.info.left <= 2, at_right = p.info.left + p.info.width >= 28;
    const float ml = c.margin, mr = c.vw - 240.0f - c.margin;
    float dx = 0;
    if (at_left != at_right) dx = at_right ? mr : -ml;
    p.x = c.margin + r.x + dx;
    p.y = static_cast<float>(r.y);
    p.w = static_cast<float>(r.w);
    p.h = static_cast<float>(r.h);
    return p;
}

// ── text runs of a window, drawn at window-relative positions ───────────
struct RunStyle {
    float scale = 1.0f;       // view units per GBA pixel
    float size_mul = 1.0f;
    bool bold_rows = false;
    int bold_row_y = -1000;   // GBA y of a row drawn bold/white (selection)
    int bold_row_h = 0;
    ImU32 force = 0;          // override colour
};

float run_size(const TextRun& r) {
    return r.font == 0 ? 9.0f : 10.6f;
}

void draw_runs(Ctx& c, int id, float x0, float y0, float win_w, const RunStyle& st) {
    const auto runs = cap_runs(id, modern_text());
    for (std::size_t i = 0; i < runs.size(); ++i) {
        const TextRun& r = runs[i];
        if (r.arrow) continue;
        const float size = run_size(r) * st.size_mul * st.scale;
        const float cy = y0 + (r.y + r.h * 0.5f) * st.scale;
        const float x = x0 + r.x * st.scale;
        if (r.keypad >= 0) {
            static const int kBit[] = {0, 1, 9, 8, 3, 2, 6, 7, 5, 4, 6, 4, 6};
            const float h = 9.0f * st.scale;
            keycap_for(c, x, cy - h * 0.5f, h, kBit[std::min(r.keypad, 12)]);
            continue;
        }
        // room until the next run on the same line (or the window edge)
        float room = win_w - r.x;
        for (std::size_t j = i + 1; j < runs.size(); ++j)
            if (runs[j].y == r.y && runs[j].x > r.x) { room = static_cast<float>(runs[j].x - r.x) - 2; break; }
        const bool selected = st.bold_row_h > 0 && r.y + r.h * 0.5f >= st.bold_row_y &&
                              r.y + r.h * 0.5f < st.bold_row_y + st.bold_row_h;
        const F f = selected ? F::Bold : F::Medium;
        ImU32 color = st.force ? st.force : col::text_color(r.fg);
        if (selected) color = col::kWhite;
        const bool right = r.x + r.w >= win_w - 6 && r.x > win_w * 0.45f;
        const float tw = text_w(f, size, r.text);
        float sz = size;
        if (tw > room * st.scale) sz = fit_size(f, size, r.text, room * st.scale, 0.7f);
        const float ty = cy - sz * 0.5f;
        if (right) text(c, f, sz, x0 + (r.x + r.w) * st.scale, ty, color, r.text, kRight);
        else text(c, f, sz, x, ty, color, r.text, kLeft);
    }
}

// ── dialogue box ─────────────────────────────────────────────────────────
struct DialogueAnim {
    int id = -1;
    double since = 0;
    bool visible = false;
} g_dialogue;

void draw_dialogue(Ctx& c, const Placed& w, bool waiting) {
    if (g_dialogue.id != w.id || !g_dialogue.visible) {
        g_dialogue.id = w.id;
        g_dialogue.since = c.t;
    }
    g_dialogue.visible = true;
    const float k = ease_out(static_cast<float>((c.t - g_dialogue.since) / 0.16));
    const float bw = std::min(c.vw - 16.0f, 252.0f), bh = 38.0f;
    const float bx = (c.vw - bw) * 0.5f;
    const float by = 160.0f - bh - 5.0f + (1.0f - k) * 6.0f;
    const float a = k;
    // speaker tint: the game prints male NPC lines in blue and female in red
    ImU32 tint1 = col::kAccent2, tint2 = col::kAccent;
    for (const TextRun& r : cap_runs(w.id, false)) {
        if (r.arrow || r.keypad >= 0) continue;
        if (r.fg == 8 || r.fg == 9) { tint1 = col::rgba(120, 180, 255); tint2 = col::rgba(70, 130, 240); }
        else if (r.fg == 4 || r.fg == 5) { tint1 = col::rgba(255, 140, 170); tint2 = col::rgba(236, 80, 120); }
        break;
    }
    shadow(c, bx, by, bw, bh, 6.0f, 4.0f, a);
    grad_v(c, bx, by, bw, bh, col::alpha(col::rgba(24, 30, 46, 246), a),
           col::alpha(col::rgba(12, 15, 24, 248), a), 6.0f);
    rect_line(c, bx, by, bw, bh, col::alpha(col::kStroke, a), 6.0f, 0.45f);
    grad_v(c, bx + 3.0f, by + 7.0f, 1.6f, bh - 14.0f, col::alpha(tint1, a), col::alpha(tint2, a), 0.8f);
    push_clip(c, bx + 4, by + 3, bw - 8, bh - 6);
    RunStyle st;
    st.scale = 1.0f;
    st.force = col::alpha(col::kText, a);
    const float text_x = bx + 11.0f;
    const float text_y = by + 2.6f;
    draw_runs(c, w.id, text_x, text_y, (bw - 22.0f), st);
    pop_clip(c);
    if (waiting) {
        const float bob = std::sin(static_cast<float>(c.t) * 6.0f) * 1.1f;
        chevron_down(c, bx + bw - 10.0f, by + bh - 8.0f + bob, 5.0f, col::alpha(tint1, a));
    }
    target(bx, by, bw, bh, kHdHitAdvance, 0);
}

// ── generic framed window ────────────────────────────────────────────────
void draw_generic(Ctx& c, const Placed& w) {
    const float pad = 3.5f;
    const CapWindow& cw = cap_window(w.id);
    panel(c, w.x - pad, w.y - pad, w.w + pad * 2, w.h + pad * 2, 5.0f);
    if (cw.gfx) {
        const Tex t = tex_window_pixels(c.m, w.id, cw.gen);
        image(c, t, w.x, w.y, w.w, w.h);
        return;
    }
    RunStyle st;
    draw_runs(c, w.id, w.x, w.y, w.w, st);
    if (cw.arrow) {
        const float bob = std::sin(static_cast<float>(c.t) * 6.0f) * 1.0f;
        chevron_down(c, w.x + cw.arrow_x + 4, w.y + cw.arrow_y + 6 + bob, 4.5f, col::kAccent2);
    }
}

void draw_bar_window(Ctx& c, const Placed& w, bool bottom) {
    // Full-width help / caption bar: edge to edge across the margins.
    const float y = w.y - 2, h = w.h + 4;
    grad_v(c, 0, y, c.vw, h, col::rgba(18, 23, 36, bottom ? 235 : 245),
           col::rgba(10, 13, 22, bottom ? 245 : 235), 0);
    line(c, 0, bottom ? y : y + h, c.vw, bottom ? y : y + h, col::alpha(col::kAccent, 0.8f), 0.6f);
    RunStyle st;
    draw_runs(c, w.id, c.margin + w.x - c.margin + 6.0f, w.y, w.w - 12.0f, st);
}

// ── sMenu / ListMenu windows ─────────────────────────────────────────────
struct MenuAnim {
    int window = -1;
    float hl_y = 0;
    double since = 0;
} g_menu_anim, g_list_anim;

void draw_menu_window(Ctx& c, const Placed& w, const Scene& s) {
    const float pad = 3.5f;
    panel(c, w.x - pad, w.y - pad, w.w + pad * 2, w.h + pad * 2, 5.0f);
    const int count = s.menu_max - s.menu_min + 1;
    const float row_h = static_cast<float>(s.menu_oh);
    int sel_y = -1000;
    if (s.menu_grid && s.menu_cols > 0) {
        for (int i = s.menu_min; i <= s.menu_max; ++i) {
            const float cx = w.x + s.menu_left - 9 + (i % s.menu_cols) * s.menu_ow;
            const float cy = w.y + s.menu_top + (i / s.menu_cols) * row_h;
            if (i == s.menu_cursor) {
                pill_select(c, cx, cy - 0.5f, static_cast<float>(s.menu_ow) - 1, row_h - 1, 3.5f);
                sel_y = s.menu_top + (i / s.menu_cols) * s.menu_oh;
            }
            target(cx, cy, static_cast<float>(s.menu_ow), row_h, kHdHitMenu, i);
        }
    } else {
        const float target_y = w.y + s.menu_top + (s.menu_cursor - s.menu_min) * row_h;
        if (g_menu_anim.window != s.menu_window) {
            g_menu_anim.window = s.menu_window;
            g_menu_anim.hl_y = target_y;
        }
        g_menu_anim.hl_y = approach(g_menu_anim.hl_y, target_y, c.dt, 22.0f);
        pill_select(c, w.x - 1.5f, g_menu_anim.hl_y, w.w + 3.0f, row_h, 4.0f);
        sel_y = s.menu_top + (s.menu_cursor - s.menu_min) * s.menu_oh;
        for (int i = 0; i < count; ++i)
            target(w.x - pad, w.y + s.menu_top + i * row_h, w.w + pad * 2, row_h, kHdHitMenu,
                   s.menu_min + i);
    }
    RunStyle st;
    st.bold_row_y = sel_y;
    st.bold_row_h = s.menu_oh;
    draw_runs(c, w.id, w.x, w.y, w.w, st);
}

void draw_list_window(Ctx& c, const Placed& w, const Scene& s) {
    const float pad = 3.5f;
    panel(c, w.x - pad, w.y - pad, w.w + pad * 2, w.h + pad * 2, 5.0f);
    const float row_h = static_cast<float>(s.list_row_h);
    const float target_y = w.y + s.list_up + s.list_sel * row_h;
    if (g_list_anim.window != s.list_window) {
        g_list_anim.window = s.list_window;
        g_list_anim.hl_y = target_y;
    }
    g_list_anim.hl_y = approach(g_list_anim.hl_y, target_y, c.dt, 22.0f);
    pill_select(c, w.x - 1.5f, g_list_anim.hl_y, w.w + 3.0f, row_h, 4.0f);
    const int visible = std::min(s.list_max, s.list_total - s.list_scroll);
    for (int r = 0; r < visible; ++r)
        target(w.x - pad, w.y + s.list_up + r * row_h, w.w + pad * 2, row_h, kHdHitList,
               s.list_scroll + r);
    // scroll hints
    if (s.list_scroll > 0)
        tri(c, w.x + w.w * 0.5f - 3, w.y - pad + 1.5f, w.x + w.w * 0.5f + 3, w.y - pad + 1.5f,
            w.x + w.w * 0.5f, w.y - pad - 1.5f, col::kAccent2);
    if (s.list_scroll + s.list_max < s.list_total)
        chevron_down(c, w.x + w.w * 0.5f, w.y + w.h + pad - 0.5f, 6.0f, col::kAccent2);
    RunStyle st;
    st.bold_row_y = s.list_up + s.list_sel * s.list_row_h;
    st.bold_row_h = s.list_row_h;
    draw_runs(c, w.id, w.x, w.y, w.w, st);
}

// ── pause menu ───────────────────────────────────────────────────────────
struct StartMenuState {
    bool open = false;
    double since = 0;
    float hl = 0;
    int count = 7;
    int cursor = 0;
    std::uint8_t order[9] = {0, 1, 2, 3, 4, 5, 6, 7, 8};
} g_start;

Icon start_icon(int option) {
    switch (option) {
        case 0: return Icon::Pokedex;
        case 1: return Icon::Pokeball;
        case 2: return Icon::Bag;
        case 3: case 8: return Icon::Card;
        case 4: return Icon::Save;
        case 5: return Icon::Gear;
        case 7: return Icon::Flag;
        default: return Icon::Exit;
    }
}

std::string start_label(const Mem& m, int option) {
    switch (option) {
        case 0: return "Pok\xc3\xa9" "dex";
        case 1: return "Pok\xc3\xa9mon";
        case 2: return "Bag";
        case 3: case 8: return "Trainer Card";
        case 4: return "Save";
        case 5: return "Options";
        case 7: return "Retire";
        default: return "Close";
    }
}

std::string with_commas(unsigned v) {
    std::string s = std::to_string(v);
    for (int i = static_cast<int>(s.size()) - 3; i > 0; i -= 3) s.insert(static_cast<std::size_t>(i), ",");
    return s;
}

void draw_start_menu(Ctx& c, const Scene& s) {
    const Mem& m = c.m;
    if (!g_start.open) { g_start.open = true; g_start.since = c.t; }
    if (s.start_menu && s.menu) {
        g_start.count = std::clamp(s.menu_max - s.menu_min + 1, 1, 9);
        g_start.cursor = std::clamp(s.menu_cursor, 0, g_start.count - 1);
        for (int i = 0; i < g_start.count; ++i) g_start.order[i] = m.u8(kStartMenuOrder + i);
    }
    const float k = ease_out(static_cast<float>((c.t - g_start.since) / 0.2));
    // dim the world
    rect(c, 0, 0, c.vw, c.vh, col::rgba(6, 8, 14, static_cast<int>(92 * k)));

    const float pw = 98.0f, ph = 144.0f;
    const float px = c.vw - pw - 8.0f + (1.0f - k) * 24.0f, py = 8.0f;
    panel(c, px, py, pw, ph, 7.0f, col::alpha(col::kPanel, 0.97f));
    // header: trainer + location
    const std::uint32_t sb1 = m.u32(g::SaveBlock1Ptr), sb2 = m.u32(g::SaveBlock2Ptr);
    const std::string loc = location_name(m);
    grad_h(c, px, py, pw, 25.0f, col::alpha(col::kAccent, 0.22f), col::alpha(col::kAccent, 0.0f), 7.0f);
    {
        std::string n = decode_gba(m.ptr(sb2, 8), 8, 0);
        if (modern_text()) n = modern_case(n);
        text(c, F::Bold, 10.5f, px + 8, py + 3.6f, col::kText, n, kLeft, pw - 16);
    }
    text(c, F::Medium, 7.2f, px + 8, py + 13.6f, col::kTextDim, loc.empty() ? std::string("Kanto") : loc,
         kLeft, pw - 16);
    line(c, px + 6, py + 25.5f, px + pw - 6, py + 25.5f, col::kStroke, 0.45f);
    // stats row
    const std::uint32_t key = m.u32(sb2 + 0xF20);
    const unsigned money = m.u32(sb1 + 0x290) ^ key;
    int badges = 0;
    for (int b = 0; b < 8; ++b) badges += (m.u8(sb1 + 0xEE0 + 0x104) >> b) & 1;
    const int hours = m.u16(sb2 + 0xE), mins = m.u8(sb2 + 0x10);
    char tbuf[32];
    std::snprintf(tbuf, sizeof tbuf, "%d:%02d", hours, mins);
    const float sy = py + 28.0f;
    text(c, F::Bold, 7.4f, px + 8, sy, col::kGold, "\xe2\x82\xbd" + with_commas(money > 9999999 ? 0 : money));
    text(c, F::Medium, 7.0f, px + pw - 8, sy, col::kTextDim, std::string(tbuf), kRight);
    // badge pips
    for (int b = 0; b < 8; ++b) {
        const bool got = (m.u8(sb1 + 0xEE0 + 0x104) >> b) & 1;
        circle(c, px + 9.5f + b * 5.0f, sy + 12.6f, 1.6f, got ? col::kGold : col::alpha(col::kWhite, 0.14f), 10);
    }
    (void)badges;
    // items
    const float list_y = sy + 19.0f;
    const float row_h = std::min(14.0f, (py + ph - 5.0f - list_y) / g_start.count);
    const float target_hl = list_y + g_start.cursor * row_h;
    if (k < 0.05f) g_start.hl = target_hl;
    g_start.hl = approach(g_start.hl, target_hl, c.dt, 24.0f);
    pill_select(c, px + 4, g_start.hl + 0.5f, pw - 8, row_h - 1.0f, (row_h - 1.0f) * 0.5f);
    for (int i = 0; i < g_start.count; ++i) {
        const int opt = g_start.order[i];
        const float ry = list_y + i * row_h;
        const bool sel = i == g_start.cursor;
        const ImU32 fg = sel ? col::kWhite : col::kTextDim;
        icon(c, start_icon(opt), px + 14.0f, ry + row_h * 0.5f, row_h * 0.62f, fg);
        text(c, sel ? F::Bold : F::Medium, 8.8f, px + 24.0f, ry + (row_h - 8.8f) * 0.5f, fg,
             start_label(m, opt), kLeft, pw - 30);
        target(px, ry, pw, row_h, kHdHitMenu, i);
    }
    // description bar
    const int opt = g_start.order[g_start.cursor];
    const std::string desc = rom_string(m, m.u32(kStartMenuDescPtrs + std::min(opt, 8) * 4), 200, true);
    const float dw = std::min(c.vw - pw - 26.0f, 190.0f), dh = 26.0f;
    const float dx = 8.0f, dy = 160.0f - dh - 8.0f + (1.0f - k) * 10.0f;
    panel(c, dx, dy, dw, dh, 6.0f, col::alpha(col::kPanel, 0.95f));
    icon(c, Icon::Info, dx + 9.0f, dy + dh * 0.5f, 7.5f, col::kAccent2);
    // two-line wrap
    {
        const float size = 7.6f, max_w = dw - 24.0f;
        std::string l1, l2, word;
        auto push = [&](const std::string& wd) {
            std::string& cur = l2.empty() && text_w(F::Medium, size, l1 + (l1.empty() ? "" : " ") + wd) <= max_w ? l1 : l2;
            cur += (cur.empty() ? "" : " ") + wd;
        };
        for (char ch : desc) {
            if (ch == ' ') { if (!word.empty()) push(word); word.clear(); }
            else word += ch;
        }
        if (!word.empty()) push(word);
        const float ty = l2.empty() ? dy + (dh - size) * 0.5f : dy + 3.4f;
        text(c, F::Medium, size, dx + 18.0f, ty, col::kText, l1, kLeft, max_w);
        if (!l2.empty()) text(c, F::Medium, size, dx + 18.0f, ty + 9.6f, col::kText, l2, kLeft, max_w);
    }
    // key hints
    float hx = c.vw - 8.0f;
    auto hint = [&](int bit, const char* label) {
        const float w = text_w(F::Medium, 6.6f, label);
        hx -= w;
        text(c, F::Medium, 6.6f, hx, 160.0f - 7.0f - 1.0f, col::kTextDim, label);
        hx -= 2.0f;
        const float kw = keycap_for(c, 0, 0, 7.0f, bit, true);
        hx -= kw;
        keycap_for(c, hx, 160.0f - 8.0f, 7.0f, bit);
        hx -= 5.0f;
    };
    (void)hint;
}

// ── field ────────────────────────────────────────────────────────────────
// Which HD part replaces a field window: the message box, or the rest.
enum class FieldWin { Skip, Dialogue, Menu };
FieldWin classify_field_window(const Mem& m, const Scene& s, int id, const WindowInfo& info) {
    if (info.bg != 0) return FieldWin::Skip;
    const int start_win = m.u8(kStartMenuWindowId);
    if (id == start_win && start_win != 0xFF && (s.start_menu || g_start.open)) return FieldWin::Menu;
    const bool full = info.left == 0 && info.width >= 30;
    if (full) return FieldWin::Menu;   // help / description bars
    if (info.height == 4 && info.width >= 26 && info.top >= 14) return FieldWin::Dialogue;
    return FieldWin::Menu;
}

void draw_field(Ctx& c) {
    const Mem& m = c.m;
    const Scene& s = *c.sc;
    const bool hd_dialogue = g_parts[kHdPartDialogue], hd_menus = g_parts[kHdPartText];
    const int start_win = m.u8(kStartMenuWindowId);
    const int help_win = m.u8(kHelpWindowId);
    bool start_open = false;
    std::vector<Placed> dialogue, generic, bars_top, bars_bottom, lists, menus;
    for (int id = 0; id < 32; ++id) {
        if (!window_shown(m, id)) continue;
        const Placed p = place_window(c, id);
        if (p.info.bg != 0) continue;
        const FieldWin kind = classify_field_window(m, s, id, p.info);
        if (kind == FieldWin::Dialogue ? !hd_dialogue : !hd_menus) continue;   // left to the game
        if (id == start_win && start_win != 0xFF && (s.start_menu || g_start.open)) { start_open = true; continue; }
        const bool full = p.info.left == 0 && p.info.width >= 30;
        if (full && p.info.top == 0) { bars_top.push_back(p); continue; }
        if (full && p.info.top + p.info.height >= 19) {
            // the Start menu's own description bar is replaced by the panel
            if (id == help_win && (s.start_menu || g_start.open)) continue;
            bars_bottom.push_back(p);
            continue;
        }
        if (p.info.height == 4 && p.info.width >= 26 && p.info.top >= 14) { dialogue.push_back(p); continue; }
        if (s.menu && id == s.menu_window && !s.start_menu) { menus.push_back(p); continue; }
        if (s.list && id == s.list_window) { lists.push_back(p); continue; }
        generic.push_back(p);
    }
    if (!start_open) g_start.open = false;
    if (dialogue.empty()) g_dialogue.visible = false;
    for (const Placed& p : bars_top) draw_bar_window(c, p, false);
    for (const Placed& p : dialogue) {
        const CapWindow& cw = cap_window(p.id);
        const PrinterInfo pr = printer_info(m, p.id);
        const bool waiting = cw.arrow || (pr.active && (pr.state == 2 || pr.state == 3));
        draw_dialogue(c, p, waiting);
    }
    if (start_open) draw_start_menu(c, s);
    for (const Placed& p : generic) draw_generic(c, p);
    for (const Placed& p : bars_bottom) draw_bar_window(c, p, true);
    for (const Placed& p : lists) draw_list_window(c, p, s);
    for (const Placed& p : menus) draw_menu_window(c, p, s);
    // Classic menus keep their own pointer handling (the game's layout).
    g_owns_build = hd_menus && (start_open || !menus.empty() || !lists.empty() || !dialogue.empty());
}

// ── overlay entry ────────────────────────────────────────────────────────
void apply_fade(ImDrawList* dl, int start, const Fade& f) {
    if (f.black <= 0.001f && f.white <= 0.001f) return;
    for (int i = start; i < dl->VtxBuffer.Size; ++i) {
        ImU32& cc = dl->VtxBuffer[i].col;
        const ImU32 a = cc & IM_COL32_A_MASK;
        ImU32 v = col::mix(cc | IM_COL32_A_MASK, col::kBlack, f.black);
        v = col::mix(v, col::kWhite, f.white);
        cc = (v & ~IM_COL32_A_MASK) | a;
    }
}

bool make_ctx(gbarecomp::HostOverlay* ov, const Mem& m, Ctx* out) {
    Ctx& c = *out;
    c.dl = ImGui::GetBackgroundDrawList();
    float x0, y0, x1, y1;
    ov->to_drawable(0, 0, gbarecomp::OverlaySpace::View, &x0, &y0);
    ov->to_drawable(1, 1, gbarecomp::OverlaySpace::View, &x1, &y1);
    c.ox = x0;
    c.oy = y0;
    c.s = std::max(0.5f, x1 - x0);
    c.vw = static_cast<float>(ov->view_width());
    c.vh = static_cast<float>(ov->view_height());
    c.margin = std::max(0.0f, std::floor((c.vw - 240.0f) * 0.5f));
    c.t = ov->host_ms() / 1000.0;
    c.dt = static_cast<float>(std::clamp(c.t - g_last_t, 0.0, 0.1));
    g_last_t = c.t;
    c.m = m;
    c.sc = &pc_scene();
    set_view_width_hint(c.vw);
    return true;
}

void overlay_main(gbarecomp::HostOverlay* ov);

// "▶▶ 3×" in the top-right corner while fast forward runs (any interface).
void draw_fast_forward_badge(gbarecomp::HostOverlay* ov) {
    const int speed = pc_fast_forward_now();
    static float shown = 0;
    static double last = -1;
    const double now = ov->host_ms() / 1000.0;
    const float dt = last < 0 ? 0.0f : static_cast<float>(std::clamp(now - last, 0.0, 0.1));
    last = now;
    shown += ((speed > 0 ? 1.0f : 0.0f) - shown) * std::min(1.0f, dt * 12.0f);
    static int last_speed = 2;
    if (speed > 0) last_speed = speed;
    if (shown < 0.02f || !fonts_ready()) return;
    const Mem m = Mem::current();
    Ctx c;
    make_ctx(ov, m, &c);
    const float a = shown;
    char lab[8];
    std::snprintf(lab, sizeof lab, "%d\xc3\x97", last_speed);
    const float tw = text_w(F::Bold, 7.4f, lab);
    const float w = tw + 20.0f, h = 11.0f, x = c.vw - w - 6.0f, y = 5.0f - (1.0f - a) * 4.0f;
    rect(c, x, y, w, h, col::alpha(col::rgba(12, 15, 26, 225), a), h * 0.5f);
    rect_line(c, x, y, w, h, col::alpha(col::kAccent2, 0.7f * a), h * 0.5f, 0.5f);
    const float cy = y + h * 0.5f, tx0 = x + 5.0f;
    tri(c, tx0, cy - 2.8f, tx0, cy + 2.8f, tx0 + 3.8f, cy, col::alpha(col::kAccent2, a));
    tri(c, tx0 + 3.8f, cy - 2.8f, tx0 + 3.8f, cy + 2.8f, tx0 + 7.6f, cy, col::alpha(col::kAccent2, a));
    text(c, F::Bold, 7.4f, x + 15.0f, y + (h - 7.4f) * 0.5f, col::alpha(col::kText, a), lab);
}

void overlay(gbarecomp::HostOverlay* ov) {
    diorama_draw(ov);   // experimental 3D view (under the HD windows)
    overlay_main(ov);
    draw_fast_forward_badge(ov);
}

void overlay_main(gbarecomp::HostOverlay* ov) {
    const Scene& s0 = pc_scene();
    // The classic OPTION-page overlay only when the HD screen is off.
    if (g_prev_overlay && !(g_parts[kHdPartScreens] && s0.cb2 == 0x0808835C)) g_prev_overlay(ov);
    g_build.clear();
    g_owns_build = false;
    const Mem m = Mem::current();
    if (!any_part() || !m.ok() || !fonts_ready()) {
        g_targets.clear();
        g_owns_pointer = false;
        return;
    }
    Ctx c;
    make_ctx(ov, m, &c);
    const Scene& s = *c.sc;
    const int vtx0 = c.dl->VtxBuffer.Size;
    c.dl->PushClipRect(c.P(0, 0), c.P(c.vw, c.vh), true);
    if (screens_draw(c)) {
        g_owns_build = true;
    } else if (is_field(s.cb2)) {
        if (g_parts[kHdPartText] || g_parts[kHdPartDialogue]) draw_field(c);
    } else if (s.cb2 == kBattleMainCB2 && battle_active(m, s)) {
        if (g_parts[kHdPartBattle]) draw_battle(c);
    }
    c.dl->PopClipRect();
    apply_fade(c.dl, vtx0, guest_fade(m));
    g_targets.swap(g_build);
    g_owns_pointer = g_owns_build;
}

bool prepare(int dw, int dh) { return fonts_prepare(dw, dh); }

}  // namespace

namespace hd {
void target(float x, float y, float w, float h, HdHitKind kind, int index) {
    g_build.push_back({x, y, w, h, kind, index});
}
}  // namespace hd

void install_hd_ui(gbarecomp::RunOptions& opts) {
    capture_install();
    g_prev_overlay = opts.host_overlay;
    opts.host_overlay = overlay;
    opts.host_ui_prepare = prepare;
}

void hd_ui_rom_loaded() {
    capture_enable(true);
    std::fprintf(stderr, "[frlg-hd] HD interface ready\n");
}

int obj_hide(int index, std::uint16_t a0, std::uint16_t a1, std::uint16_t a2) {
    return hd::battle_obj_hide(index, a0, a1, a2);
}

void hd_ui_guest_frame() {
    const Mem m = Mem::current();
    if (!m.ok()) return;
    capture_validate(m);
    unsigned mask = 0;
    bool hide = false;
    int rects = 0;
    {
        const Scene& s = pc_scene();
        if (is_field(s.cb2)) {
            const bool dlg = g_parts[kHdPartDialogue], men = g_parts[kHdPartText];
            if (dlg && men) mask |= 1u;   // every field window is redrawn: hide the whole text layer
            else if (dlg || men) {
                // only some windows are redrawn: hide just those (frame included)
                for (int id = 0; id < 32 && rects < gba::kGbaHdMaskRectMax; ++id) {
                    if (!window_shown(m, id)) continue;
                    const WindowInfo info = window_info(m, id);
                    const FieldWin kind = classify_field_window(m, s, id, info);
                    if (kind == FieldWin::Skip || (kind == FieldWin::Dialogue ? !dlg : !men)) continue;
                    const Rect r = window_rect(m, id);
                    if (r.empty()) continue;
                    gba::GbaHdMaskRect& mr = gba::g_hd_mask_rects[rects++];
                    const bool wide = kind == FieldWin::Dialogue || (info.left == 0 && info.width >= 30);
                    // windows touching a screen edge are anchored to the
                    // widescreen view edge: extend the mask out to it
                    const bool at_left = info.left <= 2, at_right = info.left + info.width >= 28;
                    mr.x0 = static_cast<short>(wide || (at_left && !at_right) ? -160 : r.x - 8);
                    mr.x1 = static_cast<short>(wide || (at_right && !at_left) ? 400 : r.x + r.w + 8);
                    mr.y0 = static_cast<short>(r.y - 8);
                    mr.y1 = static_cast<short>(r.y + r.h + 8);
                    mr.layers = 1;
                }
            }
        }
        else if (s.cb2 == kBattleMainCB2) { if (g_parts[kHdPartBattle]) hd::battle_guest_frame(m, s, &mask, &hide); }
        else if (s.cb2 == rmap::CB2_RegionMap && g_parts[kHdPartScreens]) {
            // region map: the HD chrome replaces the text layer (names, top bar, guide text)
            const std::uint32_t f = rmap::task(m);
            if (f && f != rmap::Task_MapOpenAnim && f != rmap::Task_MapCloseAnim) mask |= 8u;
        }
    }
    gba::g_hd_bg_mask = mask;
    gba::g_hd_mask_count = rects;
    gba::g_hd_obj_hide = hide ? obj_hide : nullptr;
}

bool hd_ui_part(HdPart part) { return part >= 0 && part < kHdPartCount && g_parts[part]; }
void set_hd_ui_part(HdPart part, bool on) { if (part >= 0 && part < kHdPartCount) g_parts[part] = on; }
bool hd_ui_enabled() { return any_part(); }
void set_hd_ui_enabled(bool on) { for (bool& b : g_parts) b = on; }
bool hd_ui_modern_text() { return hd::modern_text(); }
void set_hd_ui_modern_text(bool on) { hd::set_modern_text(on); }

bool hd_ui_hit(float vx, float vy, HdHit* out) {
    for (auto it = g_targets.rbegin(); it != g_targets.rend(); ++it) {
        if (vx >= it->x && vy >= it->y && vx < it->x + it->w && vy < it->y + it->h) {
            if (out) { out->kind = it->kind; out->index = it->index; }
            return true;
        }
    }
    return false;
}

bool hd_ui_owns_pointer() { return any_part() && g_owns_pointer; }

void hd_ui_render_menu(gbarecomp::HostOverlay* ov) {
    const Mem m = Mem::current();
    if (!ov || !m.ok() || !fonts_ready()) return;
    SettingsHdView sv;
    if (!settings_hd_view(&sv)) return;
    g_build.clear();
    Ctx c;
    make_ctx(ov, m, &c);
    if (sv.mode == 2 && sv.black > 0.0f)
        rect(c, 0, 0, c.vw, c.vh, col::rgba(0, 0, 0, static_cast<int>(255 * std::min(1.0f, sv.black))));
    if (sv.mode == 2 && sv.bright <= 0.0f) { g_targets.swap(g_build); return; }
    const int vtx0 = c.dl->VtxBuffer.Size;
    c.dl->PushClipRect(c.P(0, 0), c.P(c.vw, c.vh), true);
    draw_settings_page(c, sv);
    c.dl->PopClipRect();
    if (sv.mode == 2) { Fade f; f.black = 1.0f - std::clamp(sv.bright, 0.0f, 1.0f); apply_fade(c.dl, vtx0, f); }
    g_targets.swap(g_build);
    g_owns_pointer = true;
}

}  // namespace frlg
