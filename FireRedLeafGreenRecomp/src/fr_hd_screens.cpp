// fr_hd_screens.cpp — HD full-screen menus: party, bag, summary and the
// title main menu. Each screen is drawn entirely from guest data (the
// game's own cursor / page / list state), so keyboard navigation, sounds and
// every action remain the game's; messages and context menus the game
// prints are taken from the window capture.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "fr_hd_capture.h"
#include "fr_hd_draw.h"
#include "fr_hd_text.h"
#include "fr_hd_screens.h"
#include "fr_settings_menu.h"
#include "fr_settings_ini.h"
#include "host_window.h"
#include "runtime.h"

namespace frlg::hd {
// ── shared screen chrome ─────────────────────────────────────────────────
void screen_bg(Ctx& c, ImU32 accent) {
    grad_v(c, 0, 0, c.vw, c.vh, col::rgba(20, 25, 42), col::rgba(8, 10, 18), 0);
    // soft accent glow, top right
    for (int i = 3; i >= 1; --i)
        circle(c, c.vw - 30.0f, -10.0f, 40.0f + i * 26.0f, col::alpha(accent, 0.035f), 40);
    // fine diagonal pinstripes
    for (float x = -160.0f; x < c.vw; x += 9.0f)
        line(c, x, c.vh, x + 160.0f, 0, col::rgba(255, 255, 255, 5), 0.35f);
}

void title(Ctx& c, const std::string& t, const std::string& sub) {
    rect(c, 10.0f, 7.0f, 2.0f, 11.0f, col::kAccent, 1.0f);
    text(c, F::Bold, 12.5f, 15.0f, 5.5f, col::kText, t);
    if (!sub.empty())
        text(c, F::Medium, 7.2f, 17.0f + text_w(F::Bold, 12.5f, t), 8.8f, col::kTextDim, sub);
}

// Key hint row, right-aligned at (x_right, y). bit2 >= 0 adds a second key.
void hints(Ctx& c, float x_right, float y, std::initializer_list<Hint> list) {
    float x = x_right;
    std::vector<Hint> v(list);
    for (auto it = v.rbegin(); it != v.rend(); ++it) {
        const float lw = text_w(F::Medium, 6.6f, it->label);
        x -= lw;
        text(c, F::Medium, 6.6f, x, y + 0.6f, col::kTextDim, it->label);
        x -= 2.5f;
        const float right_edge = x;
        for (int b : {it->bit2, it->bit}) {
            if (b < 0) continue;
            const float kw = keycap_for(c, 0, 0, 7.5f, b, true);
            x -= kw;
            keycap_for(c, x, y, 7.5f, b);
            x -= 1.5f;
        }
        target(x, y - 1.0f, right_edge - x + lw + 3.0f, 9.5f, kHdHitKey, it->bit);
        x -= 6.0f;
    }
}

void type_badge(Ctx& c, int type, float x, float y, float h, float* out_w) {
    const std::string n = type_name(type);
    const float size = h * 0.66f;
    const float w = text_w(F::Bold, size, n) + h * 1.1f;
    grad_v(c, x, y, w, h, col::mix(col::type_color(type), col::kWhite, 0.12f),
           col::mix(col::type_color(type), col::kBlack, 0.12f), h * 0.5f);
    text(c, F::Bold, size, x + w * 0.5f, y + (h - size) * 0.5f, col::kWhite, n, kCenter);
    if (out_w) *out_w = w;
}

void wrap_text(Ctx& c, F f, float size, float x, float y, float max_w, float line_h, ImU32 color,
               const std::string& s, int max_lines) {
    std::string line, word;
    int lines = 0;
    auto flush_line = [&]() {
        if (lines < max_lines) text(c, f, size, x, y + lines * line_h, color, line, kLeft, max_w);
        ++lines;
        line.clear();
    };
    auto add_word = [&]() {
        if (word.empty()) return;
        const std::string cand = line.empty() ? word : line + " " + word;
        if (!line.empty() && text_w(f, size, cand) > max_w) { flush_line(); line = word; }
        else line = cand;
        word.clear();
    };
    for (char ch : s) {
        if (ch == ' ') add_word();
        else if (ch == '\n') { add_word(); flush_line(); }
        else word += ch;
    }
    add_word();
    if (!line.empty()) flush_line();
}

int wrap_count(F f, float size, float max_w, const std::string& s) {
    int lines = 0;
    std::string line, word;
    auto add_word = [&]() {
        if (word.empty()) return;
        const std::string cand = line.empty() ? word : line + " " + word;
        if (!line.empty() && text_w(f, size, cand) > max_w) { ++lines; line = word; }
        else line = cand;
        word.clear();
    };
    for (char ch : s) {
        if (ch == ' ') add_word();
        else if (ch == '\n') { add_word(); ++lines; line.clear(); }
        else word += ch;
    }
    add_word();
    if (!line.empty()) ++lines;
    return lines;
}

// Message / prompt windows the game prints (anything not part of the
// screen's own layout): one bottom message bar.
std::string window_message(int id) {
    std::string out;
    for (const TextRun& r : cap_runs(id, modern_text())) {
        if (r.arrow || r.keypad >= 0) continue;
        if (!out.empty()) out += ' ';
        out += r.text;
    }
    return out;
}

void message_bar(Ctx& c, float x, float y, float w, float h, const std::string& msg, bool waiting) {
    panel(c, x, y, w, h, 5.0f, col::rgba(16, 21, 33, 245));
    grad_v(c, x + 2.5f, y + 3.0f, 1.4f, h - 6.0f, col::kAccent2, col::kAccent, 0.7f);
    wrap_text(c, F::Medium, 8.0f, x + 8.0f, y + (h > 14 ? 2.6f : (h - 8.0f) * 0.5f), w - 22.0f, 9.4f, col::kText, msg, 2);
    if (waiting) {
        const float bob = std::sin(static_cast<float>(c.t) * 6.0f) * 1.0f;
        chevron_down(c, x + w - 8.0f, y + h - 6.0f + bob, 4.5f, col::kAccent2);
    }
}

// sMenu context menu (USE / GIVE / ...) drawn as a popup from its runs.
void context_menu(Ctx& c, const Scene& s, float x, float y_bottom, float w) {
    const int count = s.menu_max - s.menu_min + 1;
    const float row = 12.0f;
    const float h = count * row + 6.0f;
    const float y = y_bottom - h;
    shadow(c, x, y, w, h, 6.0f, 5.0f, 1.2f);
    grad_v(c, x, y, w, h, col::rgba(30, 37, 56), col::rgba(20, 25, 39), 6.0f);
    rect_line(c, x, y, w, h, col::kStrokeHi, 6.0f, 0.45f);
    // labels: the menu window's runs grouped into rows
    std::vector<std::string> labels(static_cast<std::size_t>(count));
    for (const TextRun& r : cap_runs(s.menu_window, modern_text())) {
        if (r.arrow || r.keypad >= 0) continue;
        const int idx = (r.y + r.h / 2 - s.menu_top) / std::max(1, s.menu_oh);
        if (idx >= 0 && idx < count) {
            auto& l = labels[static_cast<std::size_t>(idx)];
            l += (l.empty() ? "" : " ") + r.text;
        }
    }
    for (int i = 0; i < count; ++i) {
        const float ry = y + 3.0f + i * row;
        const bool sel = s.menu_min + i == s.menu_cursor;
        if (sel) pill_select(c, x + 3.0f, ry, w - 6.0f, row - 0.5f, (row - 0.5f) * 0.5f);
        text(c, sel ? F::Bold : F::Medium, 8.2f, x + 9.0f, ry + (row - 8.2f) * 0.5f,
             sel ? col::kWhite : col::kText, labels[static_cast<std::size_t>(i)], kLeft, w - 14.0f);
        target(x, ry, w, row, kHdHitMenu, s.menu_min + i);
    }
}


bool is_quantity_text(const std::string& t) {
    return t.size() >= 3 && t.compare(0, 2, "\xc3\x97") == 0;   // starts with ×
}

void quantity_stepper(Ctx& c, float x, float y, float w, const std::string& q) {
    const float h = 20.0f;
    shadow(c, x, y, w, h, 6.0f, 4.0f, 1.2f);
    grad_v(c, x, y, w, h, col::rgba(34, 42, 64), col::rgba(22, 27, 42), 6.0f);
    rect_line(c, x, y, w, h, col::kStrokeHi, 6.0f, 0.45f);
    const float bs = h - 6.0f;
    for (int side = 0; side < 2; ++side) {
        const float bx = side ? x + w - 3.0f - bs : x + 3.0f;
        rect(c, bx, y + 3.0f, bs, bs, col::rgba(255, 255, 255, 22), bs * 0.5f);
        line(c, bx + bs * 0.3f, y + 3.0f + bs * 0.5f, bx + bs * 0.7f, y + 3.0f + bs * 0.5f, col::kText, 0.9f);
        if (side) line(c, bx + bs * 0.5f, y + 3.0f + bs * 0.3f, bx + bs * 0.5f, y + 3.0f + bs * 0.7f, col::kText, 0.9f);
        target(bx, y, bs + 3.0f, h, kHdHitKey, side ? 6 : 7);  // Up raises, Down lowers
    }
    text(c, F::Bold, 9.0f, x + w * 0.5f, y + (h - 9.0f) * 0.5f, col::kText, q, kCenter, w - 2 * bs - 12.0f);
}

int bag_count(const Mem& m, int item) {
    const std::uint16_t key = static_cast<std::uint16_t>(m.u32(m.u32(g::SaveBlock2Ptr) + 0xF20) & 0xFFFF);
    for (int p = 0; p < 5; ++p) {
        const std::uint32_t slots = m.u32(0x0203988C + p * 8);
        const int cap = m.u8(0x0203988C + p * 8 + 4);
        for (int i = 0; i < cap && i < 64; ++i)
            if (m.u16(slots + i * 4) == item) return m.u16(slots + i * 4 + 2) ^ key;
    }
    return 0;
}

std::string money_str(unsigned v) {
    std::string s = std::to_string(v);
    for (int i = static_cast<int>(s.size()) - 3; i > 0; i -= 3) s.insert(static_cast<std::size_t>(i), ",");
    return "\xe2\x82\xbd" + s;
}

namespace {

constexpr std::uint32_t kCB2_PartyMenu   = 0x0811EBA0;
constexpr std::uint32_t kCB2_Bag         = 0x08107EE0;
constexpr std::uint32_t kCB2_Summary     = 0x08137EE8;
constexpr std::uint32_t kCB2_MainMenu    = 0x0800C2D4;
constexpr std::uint32_t kCB2_OptionMenu  = 0x0808835C;  // CB2_InitOptionMenu (runs the screen)
constexpr std::uint32_t kOptionMenuPtr   = 0x02039620;

constexpr std::uint32_t kPlayerParty     = 0x02024284;
constexpr std::uint32_t kPlayerPartyCnt  = 0x02024029;  // gPlayerPartyCount (symbol map is 1 low)
constexpr std::uint32_t kPartyMenu       = 0x0203B0A0;
constexpr std::uint32_t kBagMenuState    = 0x0203ACFC;
constexpr std::uint32_t kBagPockets      = 0x0203988C;
constexpr std::uint32_t kItems           = 0x083DB028;
constexpr std::uint32_t kSummaryPtr      = 0x0203B140;
constexpr std::uint32_t kSummaryMoveCur  = 0x0203B16D;
constexpr std::uint32_t kSummarySwapCur  = 0x0203B16E;
constexpr std::uint32_t kAbilityNames    = 0x0824FC40;
constexpr std::uint32_t kAbilityDescs    = 0x0824FB08;
constexpr std::uint32_t kNatureNames     = 0x08463E60;
constexpr std::uint32_t kMoveDescs       = 0x084886E8;
constexpr std::uint32_t kMapNames        = 0x083F1CAC;
constexpr std::uint32_t kDexEntries      = 0x0844E850;
constexpr std::uint32_t kNatDex          = 0x08251FEE;
constexpr std::uint32_t kSpeciesInfo     = 0x08254784;

std::string count_str(int n) { return std::to_string(n); }

// ── party ────────────────────────────────────────────────────────────────
struct PartyAnim { float hl = -1; double sel_since = 0; int last_sel = -1; } g_party;

void draw_party(Ctx& c) {
    const Mem& m = c.m;
    const Scene& s = *c.sc;
    screen_bg(c, col::kAccent);
    const int count = std::clamp<int>(m.u8(kPlayerPartyCnt), 0, 6);
    const int action = m.u8(kPartyMenu + 11);
    const bool switching = action == 8 || action == 9;
    const int cursor = m.s8(kPartyMenu + (switching ? 10 : 9));
    const int source = switching ? m.s8(kPartyMenu + 9) : -1;
    title(c, "Party", count_str(count) + "/6");

    const float cx = 10.0f, cw = std::min(176.0f, c.vw - 112.0f), ch = 18.5f, gap = 2.0f, cy0 = 22.0f;
    if (cursor != g_party.last_sel) { g_party.last_sel = cursor; g_party.sel_since = c.t; }
    const float hl_target = cy0 + std::clamp(cursor, 0, 5) * (ch + gap);
    if (g_party.hl < 0) g_party.hl = hl_target;
    g_party.hl = hl_target + (g_party.hl - hl_target) * std::exp(-c.dt * 22.0f);

    MonView mons[6];
    for (int i = 0; i < count; ++i) mons[i] = read_mon(m, kPlayerParty + i * 100, true);
    for (int i = 0; i < 6; ++i) {
        const float y = cy0 + i * (ch + gap);
        const bool sel = i == cursor;
        if (i >= count) {
            rect(c, cx, y, cw, ch, col::rgba(255, 255, 255, 8), 6.0f);
            rect_line(c, cx, y, cw, ch, col::rgba(255, 255, 255, 14), 6.0f, 0.4f);
            continue;
        }
        const MonView& mv = mons[i];
        const float xo = sel ? 3.0f : 0.0f;
        if (sel) {
            for (int g = 3; g >= 1; --g)
                rect(c, cx + xo - g, y - g, cw + 2 * g, ch + 2 * g, col::alpha(col::kAccent, 0.07f), 6.0f + g);
            grad_h(c, cx + xo, y, cw, ch, col::rgba(255, 98, 60, 235), col::rgba(150, 50, 40, 225), 6.0f);
        } else {
            shadow(c, cx, y, cw, ch, 6.0f, 2.0f, 0.7f);
            grad_v(c, cx, y, cw, ch, col::rgba(30, 37, 56, 240), col::rgba(19, 24, 37, 244), 6.0f);
        }
        rect_line(c, cx + xo, y, cw, ch, sel ? col::alpha(col::kWhite, 0.7f) : col::kStroke, 6.0f, sel ? 0.6f : 0.4f);
        if (i == source)
            rect_line(c, cx - 1.0f, y - 1.0f, cw + 2.0f, ch + 2.0f, col::kGold, 7.0f, 0.8f);
        // icon (bounces when selected)
        const bool bounce = sel && std::fmod(c.t - g_party.sel_since, 0.5) < 0.25;
        const Tex ic = tex_mon_icon(m, mv.egg ? 412 : mv.species, bounce ? 1 : 0);
        image(c, ic, cx + xo + 0.5f, y - 3.5f + (bounce ? -0.8f : 0.0f), 24.0f, 24.0f);
        if (mv.item) {
            circle(c, cx + xo + 19.5f, y + ch - 4.0f, 2.6f, col::kGold, 12);
            circle(c, cx + xo + 19.5f, y + ch - 4.0f, 1.2f, col::rgba(120, 80, 10), 8);
        }
        const float tx = cx + xo + 26.0f;
        const ImU32 fg = sel ? col::kWhite : col::kText;
        std::string name = mv.egg ? std::string("Egg") : mv.nickname;
        text(c, F::Bold, 8.4f, tx, y + 1.2f, fg, name, kLeft, 64.0f);
        const float nw = std::min(64.0f, text_w(F::Bold, 8.4f, name));
        if (!mv.egg && mv.gender != 2)
            text(c, F::Bold, 8.0f, tx + nw + 1.5f, y + 1.4f, mv.gender ? col::kPink : col::kBlue,
                 mv.gender ? "\xe2\x99\x80" : "\xe2\x99\x82");
        if (!mv.egg) {
            char lv[12];
            std::snprintf(lv, sizeof lv, "Lv %d", mv.level);
            text(c, F::Medium, 6.8f, tx, y + 10.4f, sel ? col::alpha(col::kWhite, 0.9f) : col::kTextDim, lv);
            // HP
            const float frac = mv.max_hp ? static_cast<float>(mv.hp) / mv.max_hp : 0.0f;
            const float bx = tx + 22.0f, bw = cw - (bx - cx - xo) - 40.0f;
            const ImU32 hc = frac > 0.5f ? col::kGood : frac > 0.2f ? col::kWarn : col::kBad;
            bar(c, bx, y + 12.2f, bw, 3.0f, frac, hc, col::rgba(0, 0, 0, 140));
            char hp[16];
            std::snprintf(hp, sizeof hp, "%d/%d", mv.hp, mv.max_hp);
            text(c, F::Bold, 6.4f, cx + xo + cw - 6.0f, y + 10.6f, fg, hp, kRight);
            // status
            const std::string st = mv.hp == 0 ? std::string("FNT") : status_label(mv.status);
            if (!st.empty()) {
                const float sw = text_w(F::Bold, 5.6f, st) + 4.0f;
                const ImU32 sc = mv.hp == 0 ? col::rgba(130, 50, 60) : status_color(mv.status);
                rect(c, cx + xo + cw - 6.0f - sw, y + 2.0f, sw, 6.2f, sc, 3.1f);
                text(c, F::Bold, 5.6f, cx + xo + cw - 6.0f - sw * 0.5f, y + 2.2f, col::kWhite, st, kCenter, 0, false);
            }
        }
        target(cx, y, cw + 4.0f, ch, kHdHitParty, i);
    }
    // TM/item compatibility tags printed by the game into slot windows
    for (int i = 0; i < count; ++i) {
        std::string tag;
        for (const TextRun& r : cap_runs(i, true)) {
            if (r.arrow || r.keypad >= 0) continue;
            const std::string& t = r.text;
            bool letters = t.size() > 2;
            for (char ch2 : t)
                if (!((ch2 >= 'A' && ch2 <= 'Z') || (ch2 >= 'a' && ch2 <= 'z') || ch2 == ' ' || ch2 == '!' || ch2 == '.'))
                    letters = false;
            if (!letters || t == mons[i].nickname || t.rfind("Lv", 0) == 0) continue;
            tag = t;
        }
        if (!tag.empty()) {
            const float y = cy0 + i * (ch + gap);
            const float tw = text_w(F::Bold, 6.2f, tag) + 6.0f;
            // "Able!" / "Learned" read positive, "Not Able!" muted
            const bool negative = tag.rfind("Not", 0) == 0 || tag.rfind("NOT", 0) == 0;
            const bool learned = tag.find("earned") != std::string::npos || tag.find("EARNED") != std::string::npos;
            const ImU32 tag_bg = negative ? col::rgba(255, 255, 255, 26) : learned ? col::rgba(120, 130, 150, 200)
                                                                                  : col::rgba(34, 170, 96, 235);
            rect(c, cx + cw - tw - 52.0f, y + 1.8f, tw, 7.0f, tag_bg, 3.5f);
            text(c, F::Bold, 6.2f, cx + cw - 52.0f - tw * 0.5f, y + 2.1f, negative ? col::kTextDim : col::kWhite, tag,
                 kCenter, 0, false);
        }
    }

    // right: selected Pokémon spotlight
    const float rx = cx + cw + 10.0f, rw = c.vw - rx - 10.0f;
    if (cursor >= 0 && cursor < count && mons[cursor].valid) {
        const MonView& mv = mons[cursor];
        const float bob = std::sin(static_cast<float>(c.t) * 2.2f) * 1.2f;
        const ImU32 t1 = col::type_color(species_type(m, mv.species, 0));
        for (int g = 5; g >= 1; --g) circle(c, rx + rw * 0.5f, 56.0f, 18.0f + g * 6.0f, col::alpha(t1, 0.035f), 40);
        const Tex front = tex_mon_front(m, mv.egg ? 412 : mv.species, mv.shiny, mv.personality);
        const float fs = std::min(rw, 72.0f);
        image(c, front, rx + (rw - fs) * 0.5f, 22.0f + bob, fs, fs);
        if (!mv.egg) {
            float bx = rx + rw * 0.5f;
            const int ty1 = species_type(m, mv.species, 0), ty2 = species_type(m, mv.species, 1);
            float w1 = 0, w2 = 0;
            type_badge(c, ty1, 0, -100, 8.0f, &w1);
            if (ty2 != ty1) type_badge(c, ty2, 0, -100, 8.0f, &w2);
            const float total = w1 + (ty2 != ty1 ? w2 + 2.0f : 0.0f);
            bx -= total * 0.5f;
            type_badge(c, ty1, bx, 97.0f, 8.0f);
            if (ty2 != ty1) type_badge(c, ty2, bx + w1 + 2.0f, 97.0f, 8.0f);
            if (mv.item) {
                const Tex it = tex_item_icon(m, mv.item);
                image(c, it, rx + 2.0f, 106.0f, 12.0f, 12.0f);
                text(c, F::Medium, 6.8f, rx + 15.0f, 108.0f, col::kTextDim, item_name(m, mv.item), kLeft, rw - 16.0f);
            }
        }
    }
    // the game's message + context menu
    std::string msg;
    int msg_id = -1;
    for (int id = 6; id < 32; ++id) {
        if (s.menu && id == s.menu_window) continue;
        if (!window_shown(m, id)) continue;
        const std::string t = window_message(id);
        if (t.empty() || t == "Cancel" || t == "Confirm") continue;
        if (t.size() > msg.size()) { msg = t; msg_id = id; }
    }
    const float my = 148.0f;
    const bool waiting = msg_id >= 0 && cap_window(msg_id).arrow;
    message_bar(c, 10.0f, my, c.vw - 20.0f - 52.0f, 10.5f, msg.empty() ? std::string("Choose a Pok\xc3\xa9mon.") : msg, waiting);
    if (waiting) target(10.0f, my, c.vw - 20.0f - 52.0f, 10.5f, kHdHitAdvance, 0);
    // cancel button (slot 7)
    {
        const float bw = 48.0f, bx = c.vw - 10.0f - bw;
        const bool sel = cursor == 7;
        if (sel) pill_select(c, bx, my, bw, 10.5f, 5.25f);
        else { rect(c, bx, my, bw, 10.5f, col::rgba(30, 37, 56, 240), 5.25f); rect_line(c, bx, my, bw, 10.5f, col::kStroke, 5.25f, 0.4f); }
        icon(c, Icon::Back, bx + 8.0f, my + 5.25f, 6.0f, col::kWhite);
        text(c, F::Bold, 7.4f, bx + 14.0f, my + 1.6f, col::kWhite, "Back");
        target(bx, my, bw, 10.5f, kHdHitParty, 7);
    }
    if (s.menu) context_menu(c, s, rx - 4.0f, 144.0f, rw + 4.0f);
}

// ── bag ──────────────────────────────────────────────────────────────────
struct BagAnim { float hl = -1; int pocket = -1; } g_bag;

void draw_bag(Ctx& c) {
    const Mem& m = c.m;
    const Scene& s = *c.sc;
    screen_bg(c, col::kGold);
    const int pocket = std::clamp<int>(m.u16(kBagMenuState + 6), 0, 2);
    title(c, "Bag");
    // pocket tabs
    static const char* const kPockets[3] = {"Items", "Key Items", "Pok\xc3\xa9 Balls"};
    static const Icon kPocketIcons[3] = {Icon::Bag, Icon::Card, Icon::Pokeball};
    float tx = 58.0f;
    for (int p = 0; p < 3; ++p) {
        const float w = text_w(F::Bold, 7.6f, kPockets[p]) + 18.0f;
        const bool sel = p == pocket;
        if (sel) pill_select(c, tx, 5.0f, w, 12.0f, 6.0f, 0.6f);
        else { rect(c, tx, 5.0f, w, 12.0f, col::rgba(255, 255, 255, 14), 6.0f); }
        icon(c, kPocketIcons[p], tx + 7.0f, 11.0f, 7.0f, sel ? col::kWhite : col::kTextDim);
        text(c, F::Bold, 7.6f, tx + 12.5f, 7.2f, sel ? col::kWhite : col::kTextDim, kPockets[p]);
        if (p == pocket - 1) target(tx, 5.0f, w, 12.0f, kHdHitKey, 5);       // Left
        else if (p == pocket + 1) target(tx, 5.0f, w, 12.0f, kHdHitKey, 4);  // Right
        tx += w + 3.0f;
    }
    // items of this pocket
    const std::uint32_t slots = m.u32(kBagPockets + pocket * 8);
    const int cap = m.u8(kBagPockets + pocket * 8 + 4);
    const std::uint16_t key = static_cast<std::uint16_t>(m.u32(m.u32(g::SaveBlock2Ptr) + 0xF20) & 0xFFFF);
    struct Entry { int item; int qty; };
    std::vector<Entry> items;
    for (int i = 0; i < cap && i < 64; ++i) {
        const int id = m.u16(slots + i * 4);
        if (!id) continue;
        items.push_back({id, static_cast<int>(m.u16(slots + i * 4 + 2) ^ key)});
    }
    const int total = static_cast<int>(items.size()) + 1;  // + Cancel
    // gBagMenuState keeps the list position per pocket (+8 row, +14 scroll)
    // while the list itself is paused (context menu, messages).
    const int saved_row = m.u16(kBagMenuState + 8 + pocket * 2);
    const int saved_scroll = m.u16(kBagMenuState + 14 + pocket * 2);
    const int rows = s.list ? std::max(1, s.list_max) : 6;
    const int scroll = s.list ? s.list_scroll : saved_scroll;
    const int sel = s.list ? s.list_scroll + s.list_sel : saved_scroll + saved_row;
    const float lx = 10.0f, ly = 22.0f, lw = std::min(158.0f, c.vw - 120.0f), lh = 122.0f;
    panel(c, lx, ly, lw, lh, 6.0f, col::rgba(17, 22, 35, 235));
    const float row = (lh - 6.0f) / rows;
    if (g_bag.pocket != pocket) { g_bag.pocket = pocket; g_bag.hl = -1; }
    const float hl_t = ly + 3.0f + (sel - scroll) * row;
    if (g_bag.hl < 0) g_bag.hl = hl_t;
    g_bag.hl = hl_t + (g_bag.hl - hl_t) * std::exp(-c.dt * 24.0f);
    pill_select(c, lx + 3.0f, g_bag.hl, lw - 6.0f, row - 0.5f, std::min(6.0f, row * 0.4f), s.list ? 1.0f : 0.3f);
    static ScrollAnim g_bag_scroll;
    const float sp_ = scroll_anim(g_bag_scroll, 0x100u + static_cast<std::uint32_t>(pocket), scroll, c.dt);
    push_clip(c, lx, ly + 2.0f, lw, lh - 4.0f);
    for (int r = -1; r <= rows; ++r) {
        const int idx = scroll + r;
        if (idx < 0) continue;
        if (idx >= total) break;
        const float ry = ly + 3.0f + (static_cast<float>(idx) - sp_) * row;
        const bool is_sel = idx == sel;
        const ImU32 fg = is_sel ? col::kWhite : col::kText;
        if (idx == total - 1) {
            icon(c, Icon::Back, lx + 13.0f, ry + row * 0.5f, 7.0f, is_sel ? col::kWhite : col::kTextDim);
            text(c, is_sel ? F::Bold : F::Medium, 8.6f, lx + 24.0f, ry + (row - 8.6f) * 0.5f, fg, "Close Bag");
        } else {
            const Entry& e = items[static_cast<std::size_t>(idx)];
            image(c, tex_item_icon(m, e.item), lx + 5.0f, ry + (row - 14.0f) * 0.5f, 14.0f, 14.0f);
            text(c, is_sel ? F::Bold : F::Medium, 8.6f, lx + 24.0f, ry + (row - 8.6f) * 0.5f, fg,
                 item_name(m, e.item), kLeft, lw - 58.0f);
            if (pocket != 1) {
                char q[12];
                std::snprintf(q, sizeof q, "\xc3\x97%d", e.qty);
                text(c, F::Bold, 7.6f, lx + lw - 8.0f, ry + (row - 7.6f) * 0.5f,
                     is_sel ? col::kWhite : col::kTextDim, q, kRight);
            }
        }
        if (r >= 0 && r < rows) target(lx, ly + 3.0f + r * row, lw, row, kHdHitList, idx);
    }
    pop_clip(c);
    // scroll bar
    if (total > rows) {
        const float th = lh - 10.0f, frac = static_cast<float>(rows) / total;
        const float pos = static_cast<float>(scroll) / std::max(1, total - rows);
        rect(c, lx + lw + 2.0f, ly + 5.0f, 1.6f, th, col::rgba(255, 255, 255, 18), 0.8f);
        rect(c, lx + lw + 2.0f, ly + 5.0f + (th - th * frac) * pos, 1.6f, th * frac, col::kGold, 0.8f);
    }
    // detail
    const float dx = lx + lw + 8.0f, dw = c.vw - dx - 10.0f;
    panel(c, dx, ly, dw, lh, 6.0f, col::rgba(17, 22, 35, 235));
    if (sel < total - 1 && sel >= 0 && !items.empty()) {
        const Entry& e = items[static_cast<std::size_t>(std::min<int>(sel, static_cast<int>(items.size()) - 1))];
        for (int g = 4; g >= 1; --g) circle(c, dx + dw * 0.5f, ly + 26.0f, 10.0f + g * 4.0f, col::alpha(col::kGold, 0.035f), 32);
        image(c, tex_item_icon(m, e.item), dx + dw * 0.5f - 18.0f, ly + 8.0f, 36.0f, 36.0f);
        text(c, F::Bold, 9.6f, dx + dw * 0.5f, ly + 46.0f, col::kText, item_name(m, e.item), kCenter, dw - 10.0f);
        if (pocket != 1) {
            char q[16];
            std::snprintf(q, sizeof q, "In bag: %d", e.qty);
            text(c, F::Medium, 6.8f, dx + dw * 0.5f, ly + 57.0f, col::kTextDim, q, kCenter);
        }
        const std::string desc = rom_string(m, m.u32(kItems + e.item * 44 + 0x14), 200, true);
        // the context menu takes the description's place while it is open
        if (!s.menu) wrap_text(c, F::Medium, 7.4f, dx + 7.0f, ly + 68.0f, dw - 14.0f, 9.0f, col::kText, desc, 5);
    } else {
        icon(c, Icon::Back, dx + dw * 0.5f, ly + 30.0f, 22.0f, col::kTextDim);
        text(c, F::Bold, 9.0f, dx + dw * 0.5f, ly + 50.0f, col::kText, "Close Bag", kCenter);
    }
    // messages from the game (toss/use prompts) + quantity windows
    std::string msg, qty;
    for (int id = 3; id < 32; ++id) {   // 0-2: list, description, pocket name
        if (s.menu && id == s.menu_window) continue;
        if (s.list && id == s.list_window) continue;
        if (!window_shown(m, id)) continue;
        const std::string t = window_message(id);
        if (is_quantity_text(t)) { qty = t; continue; }
        if (t.size() > msg.size()) msg = t;
    }
    if (!qty.empty()) quantity_stepper(c, dx + 6.0f, ly + lh - 26.0f, dw - 12.0f, qty);
    if (!msg.empty()) message_bar(c, 10.0f, 147.0f, c.vw - 20.0f, 11.0f, msg, false);
    else hints(c, c.vw - 10.0f, 149.0f, {{0, "Select"}, {1, "Back"}, {5, "Pocket", 4}});
    if (s.menu) context_menu(c, s, dx + 4.0f, ly + lh - 2.0f, dw - 8.0f);
}

// ── summary ──────────────────────────────────────────────────────────────
void stat_row(Ctx& c, float x, float y, float w, const char* label, int value, int max_v, ImU32 color,
              int nature_mod) {
    ImU32 lc = col::kTextDim;
    if (nature_mod > 0) lc = col::rgba(255, 140, 120);
    if (nature_mod < 0) lc = col::rgba(130, 170, 255);
    text(c, F::Medium, 7.2f, x, y, lc, label);
    text(c, F::Bold, 7.6f, x + 46.0f, y - 0.2f, col::kText, std::to_string(value), kRight);
    bar(c, x + 50.0f, y + 2.6f, w - 50.0f, 3.0f, static_cast<float>(value) / max_v, color, col::rgba(255, 255, 255, 18));
}

bool is_hm_move(int mv) {
    switch (mv) {
        case 15: case 19: case 57: case 70: case 127: case 148: case 249: case 291: return true;
        default: return false;
    }
}

void draw_summary(Ctx& c) {
    const Mem& m = c.m;
    const std::uint32_t p = m.u32(kSummaryPtr);
    if (!m.valid(p + 0x3290, 100)) return;
    const MonView mv = read_mon(m, p + 0x3290, true);
    const int page = m.u8(p + 0x3214);
    const bool egg = m.u8(p + 0x3200) != 0;
    const ImU32 t1 = mv.valid ? col::type_color(species_type(m, mv.species, 0)) : col::kAccent;
    screen_bg(c, t1);
    // left: spotlight
    const float lw = std::min(112.0f, c.vw * 0.4f);
    grad_h(c, 0, 0, lw + 30.0f, c.vh, col::alpha(col::mix(t1, col::kBlack, 0.55f), 0.45f), col::alpha(col::mix(t1, col::kBlack, 0.55f), 0.0f), 0);
    for (int g = 6; g >= 1; --g) circle(c, lw * 0.5f + 4.0f, 62.0f, 16.0f + g * 7.0f, col::alpha(t1, 0.04f), 48);
    const float bob = std::sin(static_cast<float>(c.t) * 2.0f) * 1.2f;
    image(c, tex_mon_front(m, egg ? 412 : mv.species, mv.shiny, mv.personality), lw * 0.5f + 4.0f - 40.0f, 22.0f + bob, 80.0f, 80.0f);
    if (mv.shiny && !egg) text(c, F::Bold, 9.0f, 14.0f, 24.0f, col::kGold, "\xe2\x98\x85");
    std::string name = egg ? std::string("Egg") : mv.nickname;
    text(c, F::Bold, 11.0f, 12.0f, 104.0f, col::kText, name, kLeft, lw - 30.0f);
    if (!egg && mv.gender != 2)
        text(c, F::Bold, 10.0f, 14.0f + std::min(lw - 30.0f, text_w(F::Bold, 11.0f, name)), 104.6f,
             mv.gender ? col::kPink : col::kBlue, mv.gender ? "\xe2\x99\x80" : "\xe2\x99\x82");
    if (!egg) {
        char lv[24];
        const int dex = m.u16(kNatDex + (mv.species - 1) * 2);
        std::snprintf(lv, sizeof lv, "Lv %d   No. %03d", mv.level, dex);
        text(c, F::Medium, 7.2f, 12.0f, 116.0f, col::kTextDim, lv);
        float bx = 12.0f, w1 = 0;
        const int ty1 = species_type(m, mv.species, 0), ty2 = species_type(m, mv.species, 1);
        type_badge(c, ty1, bx, 127.0f, 8.6f, &w1);
        if (ty2 != ty1) type_badge(c, ty2, bx + w1 + 2.0f, 127.0f, 8.6f);
        if (mv.item) {
            image(c, tex_item_icon(m, mv.item), 10.0f, 138.0f, 13.0f, 13.0f);
            text(c, F::Medium, 7.0f, 24.0f, 141.0f, col::kText, item_name(m, mv.item), kLeft, lw - 20.0f);
        } else {
            text(c, F::Medium, 7.0f, 12.0f, 141.0f, col::kTextFaint, "No held item");
        }
    }
    // right: tabs + page
    const float rx = lw + 14.0f, rw = c.vw - rx - 10.0f;
    // PSS_MODE_SELECT_MOVE (2): a new move waits in moveIds[4];
    // PSS_MODE_FORGET_MOVE (3): pick a move to delete.
    const int mode = m.u8(p + 0x3208);
    const bool learning = mode == 2, forgetting = mode == 3;
    const int new_move = learning ? m.u16(p + 0x3258 + 8) : 0;   // moveIds[4] (moveIds at +0x3258)
    static const char* const kTabs[3] = {"Info", "Stats", "Moves"};
    const int tab = std::min(page, 2);
    float tx = rx;
    if (learning || forgetting) {
        rect(c, rx, 7.0f, 2.0f, 10.0f, col::kGold, 1.0f);
        const char* head = "Forget a move";
        text(c, F::Bold, 10.0f, rx + 5.0f, 6.2f, col::kText, head);
        if (learning && new_move)
            text(c, F::Medium, 7.2f, rx + 10.0f + text_w(F::Bold, 10.0f, head), 8.8f, col::kTextDim,
                 "to learn " + move_name(m, new_move), kLeft, rw - text_w(F::Bold, 10.0f, head) - 12.0f);
    }
    for (int i = 0; i < 3 && !(learning || forgetting); ++i) {
        const float w = text_w(F::Bold, 7.8f, kTabs[i]) + 14.0f;
        const bool sel = i == tab;
        if (sel) pill_select(c, tx, 6.0f, w, 12.0f, 6.0f, 0.6f);
        else rect(c, tx, 6.0f, w, 12.0f, col::rgba(255, 255, 255, 14), 6.0f);
        text(c, F::Bold, 7.8f, tx + w * 0.5f, 8.0f, sel ? col::kWhite : col::kTextDim, kTabs[i], kCenter);
        if (i == tab - 1) target(tx, 6.0f, w, 12.0f, kHdHitKey, 5);
        else if (i == tab + 1) target(tx, 6.0f, w, 12.0f, kHdHitKey, 4);
        tx += w + 3.0f;
    }
    const float py = 24.0f, ph = 122.0f;
    panel(c, rx, py, rw, ph, 6.0f, col::rgba(17, 22, 35, 235));
    if (egg) {
        text(c, F::Medium, 8.0f, rx + 8.0f, py + 10.0f, col::kText, "This Egg needs more time to hatch.");
    } else if (tab == 0) {
        const std::uint32_t nat_ptr = m.u32(kNatureNames + (mv.personality % 25) * 4);
        const int ability = m.u8(kSpeciesInfo + mv.species * 28 + 22 + mv.ability_num);
        struct Row { const char* k; std::string v; };
        char idb[16];
        std::snprintf(idb, sizeof idb, "%05u", mv.ot_id & 0xFFFF);
        const int sec = mv.met_location;
        std::string met = (sec >= 0x58 && sec < 0x58 + 109) ? rom_string(m, m.u32(kMapNames + (sec - 0x58) * 4), 24, true)
                          : std::string(sec == 0xFF ? "a fateful encounter" : "a distant land");
        const Row rows[] = {
            {"Trainer", mv.ot_name}, {"ID No.", idb}, {"Nature", rom_string(m, nat_ptr, 12)},
            {"Ability", rom_string(m, kAbilityNames + ability * 13, 13)},
        };
        float y = py + 6.0f;
        for (const Row& r : rows) {
            text(c, F::Medium, 7.2f, rx + 8.0f, y, col::kTextDim, r.k);
            text(c, F::Bold, 7.8f, rx + 50.0f, y - 0.3f, col::kText, r.v, kLeft, rw - 56.0f);
            y += 11.0f;
        }
        wrap_text(c, F::Medium, 6.8f, rx + 50.0f, y - 2.0f, rw - 56.0f, 8.2f, col::kTextDim,
                  rom_string(m, m.u32(kAbilityDescs + ability * 4), 80, true), 2);
        y += 16.0f;
        char metb[64];
        std::snprintf(metb, sizeof metb, "Met at Lv %d", mv.met_level ? mv.met_level : mv.level);
        text(c, F::Medium, 7.2f, rx + 8.0f, y, col::kTextDim, "Origin");
        text(c, F::Bold, 7.4f, rx + 50.0f, y - 0.2f, col::kText, std::string(metb) + " \xc2\xb7 " + met, kLeft, rw - 56.0f);
        y += 13.0f;
        // EXP
        const int lo = exp_for_level(m, mv.species, mv.level), hi = exp_for_level(m, mv.species, std::min(100, mv.level + 1));
        const float ef = mv.level >= 100 ? 1.0f : std::clamp((static_cast<float>(mv.exp) - lo) / std::max(1, hi - lo), 0.0f, 1.0f);
        text(c, F::Medium, 7.2f, rx + 8.0f, y, col::kTextDim, "EXP");
        text(c, F::Bold, 7.4f, rx + 50.0f, y - 0.2f, col::kText, std::to_string(mv.exp));
        if (mv.level < 100) {
            char nx[32];
            std::snprintf(nx, sizeof nx, "%d to Lv %d", hi - static_cast<int>(mv.exp), mv.level + 1);
            text(c, F::Medium, 6.6f, rx + rw - 8.0f, y + 0.4f, col::kTextDim, nx, kRight);
        }
        bar(c, rx + 8.0f, y + 10.0f, rw - 16.0f, 2.6f, ef, col::kBlue, col::rgba(255, 255, 255, 18));
    } else if (tab == 1) {
        const float frac = mv.max_hp ? static_cast<float>(mv.hp) / mv.max_hp : 0.0f;
        char hp[24];
        std::snprintf(hp, sizeof hp, "%d / %d", mv.hp, mv.max_hp);
        text(c, F::Medium, 7.2f, rx + 8.0f, py + 6.0f, col::kTextDim, "HP");
        text(c, F::Bold, 7.8f, rx + rw - 8.0f, py + 5.6f, col::kText, hp, kRight);
        bar(c, rx + 8.0f, py + 15.0f, rw - 16.0f, 3.4f, frac, frac > 0.5f ? col::kGood : frac > 0.2f ? col::kWarn : col::kBad,
            col::rgba(255, 255, 255, 18));
        static const char* const kStat[5] = {"Attack", "Defense", "Speed", "Sp. Atk", "Sp. Def"};
        static const int kOrder[5] = {0, 1, 3, 4, 2};  // Atk Def SpA SpD Spe
        const int nature = static_cast<int>(mv.personality % 25);
        int max_v = 1;
        for (int v : mv.stats) max_v = std::max(max_v, v);
        max_v = std::max(max_v, 100) + 20;
        float y = py + 25.0f;
        const float sw = rw * 0.6f;
        for (int k = 0; k < 5; ++k) {
            const int i = kOrder[k];
            const int mod = (nature / 5 == i && nature % 5 != i) ? 1 : (nature % 5 == i && nature / 5 != i) ? -1 : 0;
            stat_row(c, rx + 8.0f, y, sw - 8.0f, kStat[i], mv.stats[i], max_v, col::mix(t1, col::kWhite, 0.2f), mod);
            y += 11.5f;
        }
        int total = mv.max_hp;
        for (int v : mv.stats) total += v;
        text(c, F::Medium, 7.0f, rx + 8.0f, y + 2.0f, col::kTextDim, "Total");
        text(c, F::Bold, 7.6f, rx + 54.0f, y + 1.8f, col::kText, std::to_string(total), kRight);
        // radar
        const float cxr = rx + sw + (rw - sw) * 0.5f, cyr = py + 64.0f, rr = std::min(20.0f, (rw - sw) * 0.36f);
        const int vals[6] = {mv.max_hp, mv.stats[0], mv.stats[1], mv.stats[2], mv.stats[4], mv.stats[3]};
        int vmax = 1;
        for (int v : vals) vmax = std::max(vmax, v);
        ImVec2 pts[6], ring_pts[6], half_pts[6];
        for (int k = 0; k < 6; ++k) {
            const float a = -1.5708f + k * 1.0472f;
            const float r = rr * (0.18f + 0.82f * static_cast<float>(vals[k]) / vmax);
            pts[k] = c.P(cxr + std::cos(a) * r, cyr + std::sin(a) * r);
            ring_pts[k] = c.P(cxr + std::cos(a) * rr, cyr + std::sin(a) * rr);
            half_pts[k] = c.P(cxr + std::cos(a) * rr * 0.5f, cyr + std::sin(a) * rr * 0.5f);
        }
        c.dl->AddConvexPolyFilled(ring_pts, 6, col::rgba(255, 255, 255, 10));
        c.dl->AddPolyline(ring_pts, 6, col::rgba(255, 255, 255, 40), ImDrawFlags_Closed, std::max(1.0f, 0.4f * c.s));
        c.dl->AddPolyline(half_pts, 6, col::rgba(255, 255, 255, 22), ImDrawFlags_Closed, std::max(1.0f, 0.35f * c.s));
        c.dl->AddConvexPolyFilled(pts, 6, col::alpha(t1, 0.55f));
        c.dl->AddPolyline(pts, 6, col::mix(t1, col::kWhite, 0.4f), ImDrawFlags_Closed, std::max(1.0f, 0.6f * c.s));
        static const char* const kAx[6] = {"HP", "Atk", "Def", "Spe", "SpD", "SpA"};
        for (int k = 0; k < 6; ++k) {
            const float a = -1.5708f + k * 1.0472f;
            text(c, F::Bold, 5.2f, cxr + std::cos(a) * (rr + 6.0f), cyr + std::sin(a) * (rr + 5.0f) - 2.6f,
                 col::kTextFaint, kAx[k], kCenter, 0, false);
        }
    } else {
        const int cursor = page == 3 ? m.u8(kSummaryMoveCur) : -1;
        const bool selecting = page == 3;
        const bool swapping = selecting && m.u8(p + 0x3268) != 0;
        const int swap_from = swapping ? m.u8(kSummarySwapCur) : -1;
        // While picking, a fifth row: the move to learn, or "Back".
        const float mh = selecting ? 15.5f : 19.0f, gap = selecting ? 1.6f : 2.0f;
        auto move_row = [&](int i, int mvid, int pp_cur, int pp_max, float y, bool is_new) {
            const bool sel = i == cursor;
            const int ty = move_type(m, mvid);
            const ImU32 tc = col::type_color(ty);
            grad_h(c, rx + 5.0f, y, rw - 10.0f, mh, col::mix(tc, col::rgba(16, 20, 30), sel ? 0.25f : 0.6f),
                   col::mix(tc, col::rgba(12, 15, 24), sel ? 0.55f : 0.85f), 5.0f);
            ImU32 edge = sel ? col::alpha(col::kWhite, 0.8f) : col::alpha(col::kWhite, 0.08f);
            if (i == swap_from) edge = col::kGold;
            if (is_new && !sel) edge = col::alpha(col::kGold, 0.55f);
            rect_line(c, rx + 5.0f, y, rw - 10.0f, mh, edge, 5.0f, sel || i == swap_from ? 0.6f : 0.4f);
            const float ny = selecting ? y + 1.0f : y + 1.8f;
            float nx = rx + 11.0f;
            if (is_new) {
                const float bw = text_w(F::Bold, 5.6f, "NEW") + 5.0f;
                rect(c, nx, ny + 1.4f, bw, 6.6f, col::kGold, 3.3f);
                text(c, F::Bold, 5.6f, nx + bw * 0.5f, ny + 1.8f, col::rgba(40, 28, 6), "NEW", kCenter, 0, false);
                nx += bw + 3.0f;
            }
            text(c, F::Bold, selecting ? 7.8f : 8.4f, nx, ny, col::kWhite, move_name(m, mvid), kLeft, rw - 60.0f);
            if ((learning || forgetting) && !is_new && is_hm_move(mvid)) {   // HMs can't be forgotten
                const float hx = nx + std::min(rw - 60.0f, text_w(F::Bold, 7.8f, move_name(m, mvid))) + 4.0f;
                rect(c, hx, ny + 1.4f, 13.0f, 6.6f, col::rgba(255, 255, 255, 40), 3.3f);
                text(c, F::Bold, 5.4f, hx + 6.5f, ny + 1.9f, col::kWhite, "HM", kCenter, 0, false);
            }
            float bw = 0;
            type_badge(c, ty, rx + 11.0f, y + (selecting ? 8.8f : 11.0f), selecting ? 5.4f : 6.0f, &bw);
            char ppb[16];
            std::snprintf(ppb, sizeof ppb, "%d/%d", pp_cur, pp_max);
            text(c, F::Bold, 7.4f, rx + rw - 10.0f, y + (selecting ? 1.2f : 2.0f), col::kWhite, ppb, kRight);
            text(c, F::Medium, 5.4f, rx + rw - 10.0f, y + (selecting ? 9.0f : 11.2f), col::alpha(col::kWhite, 0.7f), "PP", kRight);
            if (selecting) target(rx + 5.0f, y, rw - 10.0f, mh, kHdHitSummaryMove, i);
        };
        for (int i = 0; i < 4; ++i) {
            const float y = py + 4.0f + i * (mh + gap);
            const int mvid = mv.moves[i];
            if (!mvid) {
                rect(c, rx + 5.0f, y, rw - 10.0f, mh, col::rgba(255, 255, 255, 8), 5.0f);
                text(c, F::Medium, 7.4f, rx + rw * 0.5f, y + (mh - 7.4f) * 0.5f, col::kTextFaint, "\xe2\x80\x94", kCenter);
                continue;
            }
            const int base = move_pp(m, mvid), bonus = (mv.pp_bonuses >> (2 * i)) & 3;
            move_row(i, mvid, mv.pp[i], base * (5 + bonus) / 5, y, false);
        }
        float info_y = py + 4.0f + 4 * (mh + gap) + 1.0f;
        if (selecting) {
            const float y = py + 4.0f + 4 * (mh + gap) + 1.5f;
            if (learning && new_move) {
                line(c, rx + 12.0f, y - 1.3f, rx + rw - 12.0f, y - 1.3f, col::alpha(col::kGold, 0.35f), 0.4f);
                move_row(4, new_move, move_pp(m, new_move), move_pp(m, new_move), y + 0.4f, true);
                info_y = y + mh + 2.5f;
            } else if (!swapping) {
                const bool sel = cursor == 4;
                const float bh = 10.0f;
                if (sel) pill_select(c, rx + 5.0f, y, rw - 10.0f, bh, bh * 0.5f);
                else rect(c, rx + 5.0f, y, rw - 10.0f, bh, col::rgba(255, 255, 255, 10), bh * 0.5f);
                icon(c, Icon::Back, rx + 13.0f, y + bh * 0.5f, 6.0f, sel ? col::kWhite : col::kTextDim);
                text(c, sel ? F::Bold : F::Medium, 7.2f, rx + 20.0f, y + (bh - 7.2f) * 0.5f, sel ? col::kWhite : col::kText, "Back");
                target(rx + 5.0f, y, rw - 10.0f, bh, kHdHitSummaryMove, 4);
                info_y = y + bh + 2.5f;
            }
        }
        const int info_move = cursor >= 0 && cursor < 4 ? mv.moves[cursor] : (cursor == 4 ? new_move : 0);
        if (info_move) {
            char pw[48];
            const int power = move_power(m, info_move), acc = move_accuracy(m, info_move);
            std::snprintf(pw, sizeof pw, "Power %s   Accuracy %s", power > 1 ? std::to_string(power).c_str() : "\xe2\x80\x94",
                          acc ? std::to_string(acc).c_str() : "\xe2\x80\x94");
            text(c, F::Bold, 6.6f, rx + 8.0f, info_y, col::kGold, pw);
            const float room = py + ph - (info_y + 8.0f) - 2.0f;
            wrap_text(c, F::Medium, 6.4f, rx + 8.0f, info_y + 7.8f, rw - 16.0f, 7.4f, col::kTextDim,
                      rom_string(m, m.u32(kMoveDescs + (info_move - 1) * 4), 120, true),
                      std::max(1, static_cast<int>(room / 7.4f)));
        }
    }
    if (learning || forgetting) {
        const int cur = page == 3 ? m.u8(kSummaryMoveCur) : -1;
        const bool give_up = learning && cur == 4;
        if (cur >= 0 && cur < 4 && is_hm_move(mv.moves[cur]))
            hints(c, c.vw - 10.0f, 150.0f, {{6, "Choose", 7}, {1, "Cancel"}});
        else if (give_up) hints(c, c.vw - 10.0f, 150.0f, {{6, "Choose", 7}, {0, "Don't learn"}, {1, "Cancel"}});
        else hints(c, c.vw - 10.0f, 150.0f, {{6, "Choose", 7}, {0, "Forget"}, {1, "Cancel"}});
    } else if (page == 3) {
        hints(c, c.vw - 10.0f, 150.0f, {{6, "Choose", 7}, {0, m.u8(p + 0x3268) ? "Swap here" : "Move"}, {1, "Back"}});
    } else {
        hints(c, c.vw - 10.0f, 150.0f, {{6, "Pok\xc3\xa9mon", 7}, {5, "Page", 4}, {1, "Back"}});
    }
}

// ── title main menu ──────────────────────────────────────────────────────
struct MainAnim { float hl = -1; } g_main;

void draw_main_menu(Ctx& c) {
    const Mem& m = c.m;
    const Scene& s = *c.sc;
    if (s.main_task < 0) return;
    screen_bg(c, col::kAccent);
    // big faint ball
    ring(c, c.vw - 40.0f, 120.0f, 70.0f, col::rgba(255, 255, 255, 10), 6.0f);
    line(c, c.vw - 110.0f, 120.0f, c.vw + 30.0f, 120.0f, col::rgba(255, 255, 255, 10), 6.0f);
    ring(c, c.vw - 40.0f, 120.0f, 18.0f, col::rgba(255, 255, 255, 10), 6.0f);
    text(c, F::Bold, 15.0f, 14.0f, 8.0f, col::kText, "FireRed");
    text(c, F::Bold, 15.0f, 15.0f + text_w(F::Bold, 15.0f, "FireRed "), 8.0f, col::kAccent, "Remaster");

    const std::uint32_t t = g::Tasks + s.main_task * 0x28;
    const int cursor = static_cast<std::int16_t>(m.u16(t + 10));
    const bool has_save = s.main_type != 0;
    std::vector<int> items;
    if (has_save) items.push_back(0);
    items.push_back(1);
    if (s.main_type == 2) items.push_back(2);
    const float x = 14.0f, w = std::min(190.0f, c.vw - 28.0f);
    float y = 30.0f;
    const float big_h = 58.0f, small_h = 17.0f;
    for (std::size_t i = 0; i < items.size(); ++i) {
        const int kind = items[i];
        const float h = kind == 0 ? big_h : small_h;
        const bool sel = static_cast<int>(i) == cursor;
        if (sel) {
            for (int g = 3; g >= 1; --g)
                rect(c, x - g, y - g, w + 2 * g, h + 2 * g, col::alpha(col::kAccent, 0.07f), 7.0f + g);
        }
        shadow(c, x, y, w, h, 7.0f, 3.0f, 0.9f);
        if (sel) grad_h(c, x, y, w, h, col::rgba(255, 96, 58, 240), col::rgba(160, 52, 40, 235), 7.0f);
        else grad_v(c, x, y, w, h, col::rgba(30, 37, 56, 240), col::rgba(18, 23, 36, 244), 7.0f);
        rect_line(c, x, y, w, h, sel ? col::alpha(col::kWhite, 0.7f) : col::kStroke, 7.0f, sel ? 0.6f : 0.4f);
        const ImU32 fg = sel ? col::kWhite : col::kText;
        if (kind == 0) {
            const std::uint32_t sb1 = m.u32(g::SaveBlock1Ptr), sb2 = m.u32(g::SaveBlock2Ptr);
            text(c, F::Bold, 10.0f, x + 10.0f, y + 4.0f, fg, "Continue");
            std::string name = decode_gba(m.ptr(sb2, 8), 8, 0);
            if (modern_text()) name = modern_case(name);
            int dex = 0, badges = 0;
            for (int b = 0; b < 52; ++b) {
                std::uint8_t v = m.u8(sb2 + 0x18 + 0x10 + b);
                while (v) { dex += v & 1; v >>= 1; }
            }
            for (int b = 0; b < 8; ++b) badges += (m.u8(sb1 + 0xEE0 + 0x104) >> b) & 1;
            char tb[24];
            std::snprintf(tb, sizeof tb, "%d:%02d", m.u16(sb2 + 0xE), m.u8(sb2 + 0x10));
            struct KV { const char* k; std::string v; };
            const KV kv[4] = {{"Player", name}, {"Time", tb}, {"Pok\xc3\xa9" "dex", std::to_string(dex)}, {"Badges", ""}};
            for (int k = 0; k < 4; ++k) {
                const float kx = x + 10.0f + (k % 2) * (w * 0.5f - 4.0f), ky = y + 20.0f + (k / 2) * 17.0f;
                text(c, F::Medium, 6.6f, kx, ky, sel ? col::alpha(col::kWhite, 0.8f) : col::kTextDim, kv[k].k);
                if (k < 3) text(c, F::Bold, 8.6f, kx, ky + 7.0f, fg, kv[k].v);
            }
            const float bx0 = x + 10.0f + w * 0.5f - 4.0f, by0 = y + 20.0f + 17.0f + 11.0f;
            for (int b = 0; b < 8; ++b)
                circle(c, bx0 + 2.0f + b * 6.0f, by0, 2.0f,
                       ((m.u8(sb1 + 0xEE0 + 0x104) >> b) & 1) ? col::kGold : col::rgba(255, 255, 255, 30), 12);
        } else {
            const char* label = kind == 1 ? "New Game" : "Options";
            icon(c, kind == 1 ? Icon::Pokeball : Icon::Gear, x + 11.0f, y + h * 0.5f, 8.0f, fg);
            text(c, F::Bold, 9.0f, x + 21.0f, y + (h - 9.0f) * 0.5f, fg, label);
        }
        target(x, y, w, h, kHdHitMainMenu, static_cast<int>(i));
        y += h + 5.0f;
    }
    hints(c, c.vw - 10.0f, 150.0f, {{6, "Move", 7}, {0, "Select"}});
    (void)g_main;
}


// ── OPTION screen (game page + system/controls pages) ───────────────────
struct OptRow {
    std::string label, value, desc;
    int kind = 0;        // 0 action, 1 choice, 2 slider, 3 key, 4 stepper
    int choice = 0, choices = 0;
    const char* const* names = nullptr;
    bool enabled = true;
};

struct OptAnim { float hl = -1; int scroll = 0; int page = -1; } g_opt;

void draw_option_rows(Ctx& c, const std::vector<OptRow>& rows, int cursor, bool game_page, bool controls,
                      float x, float y, float w, float h) {
    const int n = static_cast<int>(rows.size());
    const float row_h = 14.0f;
    const int visible = std::max(1, static_cast<int>((h - 6.0f) / row_h));
    if (cursor < g_opt.scroll) g_opt.scroll = cursor;
    if (cursor >= g_opt.scroll + visible) g_opt.scroll = cursor - visible + 1;
    g_opt.scroll = std::clamp(g_opt.scroll, 0, std::max(0, n - visible));
    panel(c, x, y, w, h, 7.0f, col::rgba(17, 22, 35, 240));
    const float hl_t = y + 3.0f + (cursor - g_opt.scroll) * row_h;
    if (g_opt.hl < 0) g_opt.hl = hl_t;
    g_opt.hl = hl_t + (g_opt.hl - hl_t) * std::exp(-c.dt * 24.0f);
    rect(c, x + 3.0f, g_opt.hl, w - 6.0f, row_h - 1.0f, col::alpha(col::kAccent, 0.16f), 4.0f);
    rect(c, x + 3.0f, g_opt.hl + 2.0f, 1.6f, row_h - 5.0f, col::kAccent, 0.8f);
    push_clip(c, x, y + 2.0f, w, h - 4.0f);
    for (int i = g_opt.scroll; i < n && i < g_opt.scroll + visible; ++i) {
        const OptRow& r = rows[static_cast<std::size_t>(i)];
        const float ry = y + 3.0f + (i - g_opt.scroll) * row_h;
        const bool sel = i == cursor;
        const ImU32 lc = !r.enabled ? col::kTextFaint : sel ? col::kWhite : col::kText;
        text(c, sel ? F::Bold : F::Medium, 8.4f, x + 10.0f, ry + (row_h - 8.4f) * 0.5f - 0.4f, lc, r.label, kLeft, w * 0.42f);
        const float right = x + w - 9.0f;
        const float cy = ry + row_h * 0.5f;
        // whole row first (the later, narrower targets win): selects; on settings pages a click activates / cycles
        if (game_page) target(x, ry, w, row_h, kHdHitOption, i);
        else target(x, ry, w, row_h, kHdHitSetting, i | (1 << 8));
        if (r.kind == 1 && r.names) {
            // segmented control
            float total = 0;
            std::vector<float> ws;
            for (int k = 0; k < r.choices; ++k) {
                ws.push_back(text_w(F::Bold, 6.8f, r.names[k]) + 9.0f);
                total += ws.back();
            }
            float sx = right - total - 2.0f;
            rect(c, sx - 1.5f, cy - 5.2f, total + 3.0f + 2.0f, 10.4f, col::rgba(0, 0, 0, 90), 5.2f);
            for (int k = 0; k < r.choices; ++k) {
                const bool on = k == r.choice;
                if (on) {
                    if (r.enabled) grad_h(c, sx, cy - 4.2f, ws[k], 8.4f, col::kAccent, col::kAccent2, 4.2f);
                    else rect(c, sx, cy - 4.2f, ws[k], 8.4f, col::rgba(255, 255, 255, 40), 4.2f);
                }
                text(c, F::Bold, 6.8f, sx + ws[k] * 0.5f, cy - 3.6f,
                     on ? col::kWhite : (r.enabled ? col::kTextDim : col::kTextFaint), r.names[k], kCenter, 0, false);
                const int dir = k < r.choice ? -1 : k > r.choice ? 1 : 0;
                if (game_page) {
                    if (sel && dir) target(sx, cy - 5.0f, ws[k], 10.0f, kHdHitKey, dir < 0 ? 5 : 4);
                } else {
                    target(sx, cy - 5.0f, ws[k], 10.0f, kHdHitSetting, i | ((dir + 1) << 8));
                }
                sx += ws[k];
            }
        } else if (r.kind == 2) {
            const float bw = 70.0f, bx = right - bw - 16.0f;
            bar(c, bx, cy - 1.5f, bw, 3.0f, r.choice / 100.0f, col::kAccent2, col::rgba(255, 255, 255, 26));
            circle(c, bx + bw * r.choice / 100.0f, cy, 3.0f, col::kWhite, 16);
            text(c, F::Bold, 7.0f, right, cy - 3.6f, col::kText, std::to_string(r.choice), kRight);
            target(bx - 4.0f, cy - 5.0f, bw * 0.5f + 4.0f, 10.0f, kHdHitSetting, i | (0 << 8));
            target(bx + bw * 0.5f, cy - 5.0f, bw * 0.5f + 4.0f, 10.0f, kHdHitSetting, i | (2 << 8));
        } else if (r.kind == 3) {
            // key binding: one or two keycaps
            std::string v = r.value;
            std::vector<std::string> keys;
            std::size_t sp;
            while ((sp = v.find(' ')) != std::string::npos && keys.size() < 1) {
                keys.push_back(v.substr(0, sp));
                v = v.substr(sp + 1);
            }
            keys.push_back(v);
            float kx = right;
            for (auto it = keys.rbegin(); it != keys.rend(); ++it) {
                const std::string lab = *it == "RETURN" ? "ENTER" : *it;
                float kw = 0;
                keycap(c, -100, -100, 8.0f, lab, &kw);
                kx -= kw;
                keycap(c, kx, cy - 4.2f, 8.0f, lab);
                kx -= 3.0f;
            }
        } else if (r.kind == 4) {
            const float tw = text_w(F::Bold, 7.4f, r.value);
            text(c, F::Bold, 7.4f, right - 8.0f, cy - 3.8f, r.enabled ? col::kText : col::kTextFaint, r.value, kRight);
            tri(c, right - 8.0f - tw - 6.0f, cy, right - 8.0f - tw - 3.0f, cy - 2.5f, right - 8.0f - tw - 3.0f, cy + 2.5f,
                sel ? col::kAccent2 : col::kTextFaint);
            tri(c, right - 1.0f, cy, right - 4.0f, cy - 2.5f, right - 4.0f, cy + 2.5f, sel ? col::kAccent2 : col::kTextFaint);
            if (game_page) {
                if (sel) {
                    target(right - 8.0f - tw - 8.0f, cy - 5.0f, 8.0f, 10.0f, kHdHitKey, 5);
                    target(right - 6.0f, cy - 5.0f, 8.0f, 10.0f, kHdHitKey, 4);
                }
            } else {
                target(right - 8.0f - tw - 8.0f, cy - 5.0f, (tw + 8.0f) * 0.5f, 10.0f, kHdHitSetting, i | (0 << 8));
                target(right - 8.0f - (tw + 8.0f) * 0.5f, cy - 5.0f, (tw + 8.0f) * 0.5f + 8.0f, 10.0f, kHdHitSetting, i | (2 << 8));
            }
        } else {
            if (!r.value.empty())
                text(c, F::Medium, 7.0f, right - 8.0f, cy - 3.6f, r.value.find("again") != std::string::npos ? col::kBad : col::kTextDim,
                     r.value, kRight);
            if (r.label != "Close" && r.label != "Done") {
                line(c, right - 3.5f, cy - 2.5f, right - 1.0f, cy, col::kTextDim, 0.7f);
                line(c, right - 1.0f, cy, right - 3.5f, cy + 2.5f, col::kTextDim, 0.7f);
            }
        }
    }
    pop_clip(c);
    if (n > visible) {
        const float th = h - 12.0f, frac = static_cast<float>(visible) / n;
        const float pos = static_cast<float>(g_opt.scroll) / std::max(1, n - visible);
        rect(c, x + w - 3.5f, y + 6.0f, 1.4f, th, col::rgba(255, 255, 255, 18), 0.7f);
        rect(c, x + w - 3.5f, y + 6.0f + (th - th * frac) * pos, 1.4f, th * frac, col::kAccent2, 0.7f);
    }
    (void)controls;
}

void draw_options(Ctx& c, const SettingsHdView* sv) {
    const Mem& m = c.m;
    const bool system = sv && sv->mode != 0;
    const bool setup = sv && sv->mode == 3;
    const bool controls = system && sv->controls;
    const bool iface = system && sv->sub == 2;
    const bool extras = system && sv->sub == 3;
    screen_bg(c, col::kAccent);
    float tx = 82.0f;
    if (setup) {
        title(c, "Welcome");
        tx = 17.0f + text_w(F::Bold, 12.5f, "Welcome");
        if (!controls && !iface && !extras)
            text(c, F::Medium, 7.2f, tx, 8.8f, col::kTextDim,
                 "Pick how you want to play  \xc2\xb7  you can change any of it later in Options", kLeft, c.vw - tx - 10.0f);
    } else {
        title(c, "Options");
    }
    // tabs: Game (page 1 of the OPTION screen), then the settings categories
    const bool can_tab = !sv || sv->mode == 1;  // the stand-alone page has no game tab
    struct Tab { const char* longer; const char* shorter; int hit_kind; int index; bool on; };
    std::vector<Tab> tabs;
    if (!setup) {
        if (can_tab) tabs.push_back({"Game", "Game", kHdHitOptionsTab, 0, !system});
        if (!system) tabs.push_back({"System", "System", kHdHitOptionsTab, 1, false});
        else {
            const int cur = sv->sub;
            tabs.push_back({"General", "Main", kHdHitSettingsCat, 0, cur == 0});
            tabs.push_back({"Interface", "UI", kHdHitSettingsCat, 2, cur == 2});
            tabs.push_back({"Extras", "Extras", kHdHitSettingsCat, 3, cur == 3});
            tabs.push_back({"Controls", "Keys", kHdHitSettingsCat, 1, cur == 1});
        }
    }
    // Long names when they fit before the right edge, short ones otherwise.
    float need = 0;
    for (const Tab& t : tabs) need += text_w(F::Bold, 7.2f, t.longer) + 11.0f + 2.5f;
    const bool use_short = tx + need > c.vw - 8.0f;
    for (const Tab& t : tabs) {
        const char* label = use_short ? t.shorter : t.longer;
        const float w = text_w(F::Bold, 7.2f, label) + 11.0f;
        if (t.on) pill_select(c, tx, 6.0f, w, 12.0f, 6.0f, 0.6f);
        else rect(c, tx, 6.0f, w, 12.0f, col::rgba(255, 255, 255, 14), 6.0f);
        text(c, F::Bold, 7.2f, tx + w * 0.5f, 8.1f, t.on ? col::kWhite : col::kTextDim, label, kCenter);
        if (!t.on || t.hit_kind == kHdHitSettingsCat) target(tx, 6.0f, w, 12.0f, static_cast<HdHitKind>(t.hit_kind), t.index);
        tx += w + 2.5f;
    }
    if (setup && (controls || iface || extras))
        text(c, F::Bold, 7.8f, tx + 6.0f, 8.0f, col::kGold,
             controls ? "\xc2\xbb  Controls" : iface ? "\xc2\xbb  Interface" : "\xc2\xbb  Extras");

    std::vector<OptRow> rows;
    int cursor = 0;
    if (!system) {
        const std::uint32_t o = m.u32(kOptionMenuPtr);
        if (!m.valid(o + 0x12, 2)) return;
        cursor = m.u16(o + 0x0E);
        static const char* const kSpeed[] = {"Slow", "Mid", "Fast"};
        static const char* const kScene[] = {"On", "Off"};
        static const char* const kStyle[] = {"Shift", "Set"};
        static const char* const kSound[] = {"Mono", "Stereo"};
        static const char* const kButton[] = {"Help", "LR", "L=A"};
        auto opt = [&](int i) { return static_cast<int>(m.u16(o + i * 2)); };
        OptRow r;
        r = OptRow{}; r.label = "Text Speed"; r.kind = 1; r.names = kSpeed; r.choices = 3; r.choice = std::min(opt(0), 2);
        r.desc = "How quickly messages are printed."; rows.push_back(r);
        r = OptRow{}; r.label = "Battle Animations"; r.kind = 1; r.names = kScene; r.choices = 2; r.choice = std::min(opt(1), 1);
        r.desc = "Show move animations in battle."; rows.push_back(r);
        r = OptRow{}; r.label = "Battle Style"; r.kind = 1; r.names = kStyle; r.choices = 2; r.choice = std::min(opt(2), 1);
        r.desc = "Shift: offered a switch when a foe faints. Set: no switch prompt."; rows.push_back(r);
        r = OptRow{}; r.label = "Sound"; r.kind = 1; r.names = kSound; r.choices = 2; r.choice = std::min(opt(3), 1);
        r.desc = "Mono or stereo audio."; rows.push_back(r);
        r = OptRow{}; r.label = "Button Mode"; r.kind = 1; r.names = kButton; r.choices = 3; r.choice = std::min(opt(4), 2);
        r.desc = "Help: L/R open help. LR: L/R flip pages. L=A: L confirms."; rows.push_back(r);
        r = OptRow{}; r.label = "Window Frame"; r.kind = 4; r.value = "Type " + std::to_string(opt(5) + 1);
        r.desc = "Frame style used by the classic look."; rows.push_back(r);
        r = OptRow{}; r.label = "Close"; r.desc = "Save these options and return."; rows.push_back(r);
    } else {
        cursor = sv->cursor;
        for (const SettingsHdRow& sr : sv->rows) {
            OptRow r;
            r.label = sr.label;
            r.value = sr.value;
            r.kind = sr.kind;
            r.choice = sr.choice;
            r.choices = sr.choices;
            r.names = sr.names;
            r.enabled = sr.enabled;
            if (sr.label == "Window Size") r.kind = 4;
            if (sr.label == "Display Mode") r.desc = "Windowed, borderless or exclusive fullscreen.";
            else if (sr.label == "Widescreen") r.desc = "Show more of the world in 16:9.";
            else if (sr.label == "Window Size") r.desc = sr.enabled ? "Window scale when not fullscreen." : "Only in windowed mode.";
            else if (sr.label == "Smoothing") r.desc = "Soften pixel art when it is scaled (game and 3D view). Off keeps hard edges.";
            else if (sr.label == "Interface") r.desc = "Choose modern or classic look for battles, text, menus and the Pok\xc3\xa9" "dex.";
            else if (sr.label == "Battles") r.desc = "Modern battle HUD: HP cards, move buttons with type and effectiveness.";
            else if (sr.label == "Dialogue Boxes") r.desc = "Modern message boxes for conversations, signs and events outside battle.";
            else if (sr.label == "Field Menus") r.desc = "Modern Start menu, Yes/No choices, shop lists and pop-ups.";
            else if (sr.label == "Title Menu") r.desc = "Modern Continue / New Game screen.";
            else if (sr.label == "Pok\xc3\xa9" "dex") r.desc = "Modern Pok\xc3\xa9" "dex list and entry pages.";
            else if (sr.label == "Menus & Screens") r.desc = "Bag, party, summary, PC, maps, naming and these options.";
            else if (sr.label == "Controller Buttons") r.desc = "Nintendo swaps A and B so the right face button confirms.";
            else if (sr.label == "Left Stick Moves") r.desc = "Use the controller's left stick like the D-pad.";
            else if (sr.label == "Start Playing") r.desc = "Save these choices and start the game.";
            else if (sr.label == "Extras") r.desc = "Fast forward, Infinite Rare Candy and the 3D view.";
            else if (sr.label == "Fast Forward Speed") r.desc = "How fast the game runs while fast forward is on. Music is muted.";
            else if (sr.label == "Fast Forward Key") r.desc = "Select, then press the key to use for fast forward. Esc cancels.";
            else if (sr.label == "Key Mode") r.desc = "Toggle: press once to start, again to stop. Hold: fast while held.";
            else if (sr.label == "Infinite Rare Candy") r.desc = "Rare Candy never runs out (the Bag always holds 99).";
            else if (sr.label == "3D View (test)") r.desc = "Tilted 3D view of towns and routes. F3 switches it on and off.";
            else if (sr.label == "Text Style")
                r.desc = sr.enabled ? "Modern: Pok\xc3\xa9mon, Prof. Oak.  Classic: POK\xc3\xa9MON, PROF. OAK."
                                    : "Needs the Modern interface.";
            else if (sr.label == "Volume") r.desc = "Master volume.";
            else if (sr.label == "Auto Run") r.desc = "Run by default; hold Shift to walk.";
            else if (sr.label == "Mouse Control") r.desc = "Point and click in menus and battles.";
            else if (sr.label == "Controls") r.desc = "Keyboard keys for every action, and controller layout.";
            else if (sr.label == "Quit Game") r.desc = "Close FireRed Remaster (unsaved progress is lost).";
            else if (sr.label == "Restore Default Keys") r.desc = "Put every keyboard key back to its default. Controller buttons are unchanged.";
            else if (sr.label == "Done") r.desc = "Back to the settings list.";
            else if (sr.label == "Close") r.desc = "Close settings and return to the game.";
            else if (sr.label == "Confirm" && sr.kind == 3) r.desc = "A button: talk, confirm, advance text. Select, then press the new key.";
            else if (sr.label == "Back" && sr.kind == 3) r.desc = "B button: cancel, run, close menus. Select, then press the new key.";
            else if (sr.label == "Menu" && sr.kind == 3) r.desc = "Start button: opens the main menu. Select, then press the new key.";
            else if (sr.label == "Use Item" && sr.kind == 3) r.desc = "Select button: uses the registered item. Select, then press the new key.";
            else if ((sr.label == "Up" || sr.label == "Down" || sr.label == "Left" || sr.label == "Right") && sr.kind == 3)
                r.desc = "Moves the player and the menu cursor. Select, then press the new key.";
            else if ((sr.label == "L Button" || sr.label == "R Button") && sr.kind == 3) r.desc = "Shoulder button: pages and shortcuts. Select, then press the new key.";
            else if (sr.kind == 3) r.desc = "Select, then press the new key. Esc cancels.";
            if (!sr.note.empty()) r.desc = sr.note;
            rows.push_back(r);
        }
    }
    const int page = system ? (controls ? 2 : 1) : 0;
    if (page != g_opt.page) { g_opt.page = page; g_opt.hl = -1; g_opt.scroll = 0; }
    cursor = std::clamp(cursor, 0, std::max(0, static_cast<int>(rows.size()) - 1));
    const float px = 10.0f, py = 24.0f, pw = c.vw - 20.0f, ph = 106.0f;
    draw_option_rows(c, rows, cursor, !system, controls, px, py, pw, ph);
    // description of the selected row
    if (!rows.empty()) {
        const OptRow& r = rows[static_cast<std::size_t>(cursor)];
        panel(c, px, py + ph + 4.0f, pw, 13.0f, 5.0f, col::rgba(16, 21, 33, 235));
        icon(c, Icon::Info, px + 8.0f, py + ph + 10.5f, 6.5f, col::kAccent2);
        if (ini::help_needs_wrap(text_w(F::Medium, 7.0f, r.desc), pw - 22.0f)) {   // too long even shrunk: two small lines instead of running off the panel
            // measured with the real font; if two lines are still not enough (wide glyphs, dynamic text) the tail is cut with "..." instead of dropped
            auto measure = [&](const std::string& t) { return text_w(F::Medium, 6.2f, t); };
            const std::string shown = ini::clip_to_lines(r.desc, 2, pw - 22.0f, measure);
            wrap_text(c, F::Medium, 6.2f, px + 15.0f, py + ph + 4.4f, pw - 22.0f, 6.0f, col::kTextDim, shown, 2);
        }
        else
            text(c, F::Medium, 7.0f, px + 15.0f, py + ph + 7.0f, col::kTextDim, r.desc, kLeft, pw - 22.0f);
    }
    if (system && sv->capturing) {
        rect(c, 0, 0, c.vw, c.vh, col::rgba(4, 6, 10, 170));
        const float w = 170.0f, h = sv->notice.empty() ? 40.0f : 50.0f, x = (c.vw - w) * 0.5f, y = 54.0f;
        panel(c, x, y, w, h, 8.0f, col::rgba(24, 30, 46, 252));
        text(c, F::Bold, 10.0f, x + w * 0.5f, y + 7.0f, col::kText, "Press a key", kCenter);
        const std::string what = cursor < static_cast<int>(rows.size()) ? rows[static_cast<std::size_t>(cursor)].label : "";
        text(c, F::Medium, 7.4f, x + w * 0.5f, y + 20.0f, col::kTextDim, "for " + what + "   \xc2\xb7   Esc to cancel", kCenter);
        if (!sv->notice.empty()) text(c, F::Bold, 7.0f, x + w * 0.5f, y + 33.0f, col::kBad, sv->notice, kCenter, w - 10.0f);
        return;
    }
    if (controls) hints(c, c.vw - 10.0f, 149.5f, {{6, "Select", 7}, {0, "Change"}, {9, "Category", 8}, {1, "Back"}});
    else if (iface || extras) hints(c, c.vw - 10.0f, 149.5f, {{6, "Select", 7}, {5, "Change", 4}, {9, "Category", 8}, {1, "Back"}});
    else if (setup) hints(c, c.vw - 10.0f, 149.5f, {{6, "Select", 7}, {5, "Change", 4}, {0, "Confirm"}});
    else if (system && sv->mode == 1) hints(c, c.vw - 10.0f, 149.5f, {{6, "Select", 7}, {5, "Change", 4}, {9, "Category", 8}, {1, "Close"}});
    else if (system) hints(c, c.vw - 10.0f, 149.5f, {{6, "Select", 7}, {5, "Change", 4}, {9, "Category", 8}, {1, "Resume"}});
    else hints(c, c.vw - 10.0f, 149.5f, {{6, "Select", 7}, {5, "Change", 4}, {1, "Close"}});
}

}  // namespace

void draw_settings_page(Ctx& c, const SettingsHdView& sv) { draw_options(c, &sv); }

bool screens_active(const Mem& m, const Scene& s) {
    (void)m;
    return s.cb2 == kCB2_PartyMenu || s.cb2 == kCB2_Bag || s.cb2 == kCB2_Summary ||
           (s.cb2 == kCB2_MainMenu && s.main_task >= 0);
}

bool screens_draw(Ctx& c) {
    const Scene& s = *c.sc;
    if (s.cb2 == kCB2_MainMenu) {
        if (!hd_ui_part(kHdPartTitle) || s.main_task < 0) return false;
        draw_main_menu(c);
        return true;
    }
    if (screens2_draw(c)) return true;
    if (!hd_ui_part(kHdPartScreens)) return false;
    if (map_draw(c)) return true;
    if (naming_draw(c)) return true;
    if (s.cb2 == kCB2_PartyMenu) { draw_party(c); return true; }
    if (s.cb2 == kCB2_Bag) { draw_bag(c); return true; }
    if (s.cb2 == kCB2_Summary) { draw_summary(c); return true; }
    if (s.cb2 == kCB2_OptionMenu) {
        const std::uint32_t o = c.m.u32(kOptionMenuPtr);
        if (c.m.valid(o + 0x12, 2) && c.m.u8(o + 0x11) >= 10) { draw_options(c, nullptr); return true; }
    }
    return false;
}

}  // namespace frlg::hd
