// fr_hd_battle.cpp — HD battle HUD: Pokémon cards (name, level, gender,
// status, animated HP / EXP bars), the message box, the command buttons and
// type-coloured move buttons with PP and type effectiveness. The game's own
// healthbox sprites and BG0 text layer are hidden only while this HUD is in
// charge (ordinary wild / trainer battles); tutorial, Safari and link
// battles keep the original presentation.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

#include "fr_hd_capture.h"
#include "fr_hd_draw.h"
#include "fr_hd_text.h"

namespace frlg::hd {
namespace {

constexpr std::uint32_t kBattleMons        = 0x02023BE4;  // 4 x 0x58
constexpr std::uint32_t kBattlersCount     = 0x02023BCC;
constexpr std::uint32_t kBattlerPositions  = 0x02023BD6;
constexpr std::uint32_t kBattlerPartyIdx   = 0x02023BCE;
constexpr std::uint32_t kBattleTypeFlags   = 0x02022B4C;
constexpr std::uint32_t kActionCursor      = 0x02023FF8;
constexpr std::uint32_t kMoveCursor        = 0x02023FFC;
constexpr std::uint32_t kCtrlFuncs         = 0x03004FE0;
constexpr std::uint32_t kHealthboxIds      = 0x03004FF0;
constexpr std::uint32_t kSpritesDataPtr    = 0x02024018;
constexpr std::uint32_t kPlayerParty       = 0x02024284;
constexpr std::uint32_t kHandleChooseAction = 0x0802E438;
constexpr std::uint32_t kHandleChooseMove   = 0x0802EA10;

constexpr unsigned kTypeDouble = 1u << 0, kTypeLink = 1u << 1, kTypeTrainer = 1u << 3,
                   kTypeFirst = 1u << 4, kTypeSafari = 1u << 7, kTypeOldMan = 1u << 9,
                   kTypeGhost = 1u << 15, kTypeGhostUnveiled = 1u << 13, kTypePokedude = 1u << 16;

struct BattlerView {
    bool present = false;
    bool player = false;
    int position = 0;
    int species = 0, level = 0, hp = 0, max_hp = 0, status = 0, type1 = 0, type2 = 0;
    std::uint32_t personality = 0, exp = 0;
    std::string name;
    int moves[4] = {0, 0, 0, 0}, pp[4] = {0, 0, 0, 0}, pp_bonuses = 0;
    bool box_visible = false;
    float box_x = 0, box_y = 0;
};

// Per-battler presentation state.
struct CardAnim {
    std::uint32_t personality = 0xFFFFFFFFu;
    bool shown = false;
    double since = 0;
    float hp = -1;           // displayed HP (smoothed)
    float exp = -1;          // displayed EXP fraction
    bool bar_stale = false;
    std::int32_t stale_max = 0, stale_old = 0, stale_recv = 0;
} g_card[4];

struct {
    std::uint16_t lo[16], hi[16];
    int count = 0;
} g_hide;

bool hud_supported(unsigned type) {
    return !(type & (kTypeLink | kTypeSafari | kTypeOldMan | kTypePokedude | kTypeFirst));
}

BattlerView read_battler(const Mem& m, int b) {
    BattlerView v;
    const int count = std::min<int>(4, m.u8(kBattlersCount));
    if (b >= count) return v;
    const std::uint32_t a = kBattleMons + b * 0x58;
    v.species = m.u16(a);
    v.position = m.u8(kBattlerPositions + b);
    v.player = (v.position & 1) == 0;
    v.hp = m.u16(a + 0x28);
    v.level = m.u8(a + 0x2A);
    v.max_hp = m.u16(a + 0x2C);
    v.type1 = m.u8(a + 0x21);
    v.type2 = m.u8(a + 0x22);
    v.pp_bonuses = m.u8(a + 0x3B);
    v.exp = m.u32(a + 0x44);
    v.personality = m.u32(a + 0x48);
    v.status = static_cast<int>(m.u32(a + 0x4C));
    for (int i = 0; i < 4; ++i) { v.moves[i] = m.u16(a + 0x0C + i * 2); v.pp[i] = m.u8(a + 0x24 + i); }
    v.name = decode_gba(m.ptr(a + 0x30, 11), 11, 0);
    if (modern_text()) v.name = modern_case(v.name);
    const int sprite = m.u8(kHealthboxIds + b);
    if (sprite < 64) {
        const std::uint32_t s = g::Sprites + sprite * 0x44;
        const std::uint8_t fl = m.u8(s + 0x3E);
        const int x = m.s16(s + 0x20) + m.s16(s + 0x24);
        const int y = m.s16(s + 0x22) + m.s16(s + 0x26);
        v.box_visible = (fl & 1) && !(fl & 4) && x > -40 && x < 280;
        v.box_x = static_cast<float>(x);
        v.box_y = static_cast<float>(y);
    }
    v.present = v.species > 0 && v.species < 440 && v.max_hp > 0;
    return v;
}

// HP the game's own bar currently shows (bar animation aware).
float displayed_hp(const Mem& m, int b, const BattlerView& v, CardAnim& a) {
    const std::uint32_t data = m.u32(kSpritesDataPtr);
    const std::uint32_t bars = data ? m.u32(data + 0x0C) : 0;
    float target = static_cast<float>(v.hp);
    if (bars && m.valid(bars + b * 20, 20)) {
        const std::int32_t maxv = static_cast<std::int32_t>(m.u32(bars + b * 20 + 4));
        const std::int32_t oldv = static_cast<std::int32_t>(m.u32(bars + b * 20 + 8));
        const std::int32_t recv = static_cast<std::int32_t>(m.u32(bars + b * 20 + 12));
        const std::int32_t cur = static_cast<std::int32_t>(m.u32(bars + b * 20 + 16));
        if (a.bar_stale && (maxv != a.stale_max || oldv != a.stale_old || recv != a.stale_recv))
            a.bar_stale = false;
        if (!a.bar_stale && maxv == v.max_hp && maxv > 0 && cur != -32768) {
            const float val = maxv < 48 ? cur / 256.0f : static_cast<float>(cur);
            if (val >= 0 && val <= maxv) target = val;
        }
    }
    if (a.hp < 0) a.hp = target;
    a.hp = target;  // the game animates; follow it exactly
    return a.hp;
}

float exp_fraction(const Mem& m, int b, const BattlerView& v) {
    if (v.level >= 100) return 1.0f;
    const int lo = exp_for_level(m, v.species, v.level), hi = exp_for_level(m, v.species, v.level + 1);
    if (hi <= lo) return 0.0f;
    // EXP bar animation in progress (battleBars holds the EXP range then).
    const std::uint32_t data = m.u32(kSpritesDataPtr);
    const std::uint32_t bars = data ? m.u32(data + 0x0C) : 0;
    if (bars) {
        const std::int32_t maxv = static_cast<std::int32_t>(m.u32(bars + b * 20 + 4));
        const std::int32_t cur = static_cast<std::int32_t>(m.u32(bars + b * 20 + 16));
        if (maxv == hi - lo && cur != -32768 && maxv > 0) {
            const float val = maxv < 64 ? cur / 256.0f : static_cast<float>(cur);
            if (val >= 0 && val <= maxv) return val / maxv;
        }
    }
    const int party = m.u16(kBattlerPartyIdx + b * 2);
    std::uint32_t exp = v.exp;
    if (party < 6) {
        const MonView mon = read_mon(m, kPlayerParty + party * 100, true);
        if (mon.valid && mon.species == v.species) exp = mon.exp;
    }
    return std::clamp((static_cast<float>(exp) - lo) / static_cast<float>(hi - lo), 0.0f, 1.0f);
}

ImU32 hp_color(float frac) {
    if (frac > 0.5f) return col::kGood;
    if (frac > 0.2f) return col::kWarn;
    return col::kBad;
}

bool owned(const Mem& m, int species) {
    const int dex = m.u16(0x08251FEE + (species - 1) * 2);
    if (dex <= 0 || dex > 386) return false;
    const std::uint32_t sb2 = m.u32(g::SaveBlock2Ptr);
    return (m.u8(sb2 + 0x18 + 0x10 + (dex - 1) / 8) >> ((dex - 1) % 8)) & 1;
}

void gender_mark(Ctx& c, float x, float y, float size, int species, std::uint32_t personality) {
    const int ratio = c.m.u8(0x08254784 + species * 28 + 16);
    if (ratio == 255) return;
    const bool female = ratio == 254 || (ratio != 0 && (personality & 0xFF) < static_cast<std::uint32_t>(ratio));
    text(c, F::Bold, size, x, y, female ? col::kPink : col::kBlue, female ? "\xe2\x99\x80" : "\xe2\x99\x82");
}

void draw_card(Ctx& c, int b, const BattlerView& v, float x, float y, float w, bool compact,
               unsigned type) {
    CardAnim& a = g_card[b];
    if (a.personality != v.personality) {
        // a different Pokémon: until the game sets up a new bar animation,
        // the bar structure still describes the previous one
        const std::uint32_t data = c.m.u32(kSpritesDataPtr);
        const std::uint32_t bars = data ? c.m.u32(data + 0x0C) : 0;
        a = CardAnim{};
        a.personality = v.personality;
        if (bars) {
            a.bar_stale = true;
            a.stale_max = static_cast<std::int32_t>(c.m.u32(bars + b * 20 + 4));
            a.stale_old = static_cast<std::int32_t>(c.m.u32(bars + b * 20 + 8));
            a.stale_recv = static_cast<std::int32_t>(c.m.u32(bars + b * 20 + 12));
        }
    }
    if (!v.box_visible) { a.shown = false; return; }
    if (!a.shown) { a.shown = true; a.since = c.t; }
    const float k = std::clamp(static_cast<float>((c.t - a.since) / 0.22), 0.0f, 1.0f);
    const float e = 1.0f - (1.0f - k) * (1.0f - k);
    x += (v.player ? 18.0f : -18.0f) * (1.0f - e);
    const float h = compact ? 24.0f : (v.player ? 34.0f : 27.0f);
    const float al = e;
    shadow(c, x, y, w, h, 6.0f, 3.0f, al);
    grad_v(c, x, y, w, h, col::alpha(col::rgba(26, 33, 50, 240), al), col::alpha(col::rgba(13, 17, 27, 244), al), 6.0f);
    rect_line(c, x, y, w, h, col::alpha(col::kStroke, al), 6.0f, 0.45f);
    // accent stripe
    const ImU32 accent = v.player ? col::kAccent : col::rgba(130, 150, 190);
    rect(c, x + 2.2f, y + 4, 1.4f, h - 8, col::alpha(accent, al), 0.7f);
    const bool ghost = !v.player && (type & kTypeGhost) && !(type & kTypeGhostUnveiled);
    const std::string name = ghost ? "Ghost" : v.name;
    const float nsize = compact ? 8.0f : 9.4f;
    const float name_w = std::min(text_w(F::Bold, nsize, name), w - 44.0f);
    text(c, F::Bold, nsize, x + 7, y + 2.6f, col::alpha(col::kText, al), name, kLeft, w - 44.0f);
    float gx = x + 7 + name_w + 2.0f;
    if (!ghost) gender_mark(c, gx, y + 2.6f, nsize, v.species, v.personality);
    // level
    char lv[16];
    std::snprintf(lv, sizeof lv, "Lv %d", v.level);
    text(c, F::Bold, compact ? 6.6f : 7.4f, x + w - 6, y + (compact ? 3.4f : 3.6f), col::alpha(col::kTextDim, al),
         ghost ? std::string("Lv ?") : std::string(lv), kRight);
    // status / owned
    const std::string st = status_label(v.status);
    float row_y = y + (compact ? 12.5f : 14.0f);
    float bx = x + 7;
    if (!st.empty() && v.hp > 0) {
        const float tw = text_w(F::Bold, 5.8f, st) + 4.0f;
        rect(c, bx, row_y - 0.4f, tw, 6.4f, col::alpha(status_color(v.status), al), 3.2f);
        text(c, F::Bold, 5.8f, bx + tw * 0.5f, row_y - 0.1f, col::alpha(col::kWhite, al), st, kCenter, 0, false);
        bx += tw + 2.5f;
    } else if (v.hp == 0) {
        const float tw = text_w(F::Bold, 5.8f, "FNT") + 4.0f;
        rect(c, bx, row_y - 0.4f, tw, 6.4f, col::alpha(col::rgba(120, 50, 60), al), 3.2f);
        text(c, F::Bold, 5.8f, bx + tw * 0.5f, row_y - 0.1f, col::alpha(col::kWhite, al), "FNT", kCenter, 0, false);
        bx += tw + 2.5f;
    }
    if (!v.player && !(type & kTypeTrainer) && !ghost && owned(c.m, v.species))
        icon(c, Icon::Pokeball, x + w - 9.5f, row_y + 2.6f, 5.5f, col::alpha(col::kAccent2, al));
    // HP bar
    const float hp = displayed_hp(c.m, b, v, a);
    const float frac = v.max_hp ? hp / v.max_hp : 0.0f;
    const float bar_x = bx + 9.0f, bar_w = x + w - 7 - (v.player || compact ? 0.0f : 10.0f) - bar_x;
    text(c, F::Bold, 5.4f, bx, row_y + 0.2f, col::alpha(col::kGold, al), "HP", kLeft, 0, false);
    bar(c, bar_x, row_y + 1.0f, bar_w, 3.6f, frac, col::alpha(hp_color(frac), al), col::alpha(col::rgba(0, 0, 0, 150), al));
    if (v.player && !compact) {
        char hpt[24];
        std::snprintf(hpt, sizeof hpt, "%d / %d", static_cast<int>(std::lround(hp)), v.max_hp);
        text(c, F::Bold, 7.0f, x + w - 6, y + 20.0f, col::alpha(col::kText, al), hpt, kRight);
        const float ef = exp_fraction(c.m, b, v);
        text(c, F::Bold, 5.0f, x + 7, y + 27.3f, col::alpha(col::kBlue, al), "EXP", kLeft, 0, false);
        bar(c, x + 18, y + 28.4f, w - 25, 2.0f, ef, col::alpha(col::kBlue, al), col::alpha(col::rgba(0, 0, 0, 150), al));
    } else if (v.player && compact) {
        char hpt[24];
        std::snprintf(hpt, sizeof hpt, "%d/%d", static_cast<int>(std::lround(hp)), v.max_hp);
        text(c, F::Medium, 5.6f, x + w - 6, y + 17.4f, col::alpha(col::kTextDim, al), hpt, kRight);
    }
}

// ── bottom band: message / prompt, commands, moves ──────────────────────
void draw_text_box(Ctx& c, int window, float x, float y, float w, float h, bool waiting) {
    shadow(c, x, y, w, h, 6.0f, 4.0f, 1.0f);
    grad_v(c, x, y, w, h, col::rgba(24, 30, 46, 246), col::rgba(12, 15, 24, 248), 6.0f);
    rect_line(c, x, y, w, h, col::kStroke, 6.0f, 0.45f);
    grad_v(c, x + 3.0f, y + 7.0f, 1.6f, h - 14.0f, col::kAccent2, col::kAccent, 0.8f);
    if (window >= 0) {
        push_clip(c, x + 4, y + 3, w - 8, h - 6);
        const auto runs = cap_runs(window, modern_text());
        for (const TextRun& r : runs) {
            if (r.arrow || r.keypad >= 0) continue;
            text(c, F::Medium, 10.6f, x + 11.0f + r.x, y + 2.6f + r.y + r.h * 0.5f - 5.3f, col::kText, r.text);
        }
        pop_clip(c);
    }
    if (waiting) {
        const float bob = std::sin(static_cast<float>(c.t) * 6.0f) * 1.1f;
        chevron_down(c, x + w - 10.0f, y + h - 8.0f + bob, 5.0f, col::kAccent2);
    }
}

struct ActionStyle { const char* label; ImU32 color; Icon icon; };

void draw_actions(Ctx& c, int cursor, float x, float y, float w, float h) {
    static const ActionStyle kActions[4] = {
        {"Fight", col::rgba(240, 84, 64), Icon::Fight},
        {"Bag", col::rgba(236, 170, 40), Icon::Bag},
        {"Pok\xc3\xa9mon", col::rgba(60, 190, 110), Icon::Pokeball},
        {"Run", col::rgba(80, 140, 240), Icon::Run},
    };
    const float gap = 3.0f;
    const float bw = (w - gap) * 0.5f, bh = (h - gap) * 0.5f;
    for (int i = 0; i < 4; ++i) {
        const float bx = x + (i & 1) * (bw + gap), by = y + (i >> 1) * (bh + gap);
        const bool sel = i == cursor;
        const ActionStyle& st = kActions[i];
        const float grow = sel ? 0.8f : 0.0f;
        if (sel) {
            for (int g = 3; g >= 1; --g)
                rect(c, bx - grow - g, by - grow - g, bw + 2 * (grow + g), bh + 2 * (grow + g),
                     col::alpha(st.color, 0.08f), 5.0f + g);
        }
        shadow(c, bx, by, bw, bh, 5.0f, 2.0f, 0.8f);
        const ImU32 top = sel ? col::mix(st.color, col::kWhite, 0.18f) : col::mix(st.color, col::rgba(16, 20, 30), 0.62f);
        const ImU32 bot = sel ? st.color : col::mix(st.color, col::rgba(10, 13, 20), 0.72f);
        grad_v(c, bx - grow, by - grow, bw + 2 * grow, bh + 2 * grow, top, bot, 5.0f);
        rect_line(c, bx - grow, by - grow, bw + 2 * grow, bh + 2 * grow,
                  sel ? col::alpha(col::kWhite, 0.85f) : col::alpha(col::kWhite, 0.10f), 5.0f, sel ? 0.7f : 0.45f);
        icon(c, st.icon, bx + 10.0f, by + bh * 0.5f, bh * 0.52f, sel ? col::kWhite : col::alpha(col::kWhite, 0.75f));
        text(c, F::Bold, 9.0f, bx + 19.0f, by + (bh - 9.0f) * 0.5f, sel ? col::kWhite : col::alpha(col::kWhite, 0.82f),
             st.label, kLeft, bw - 22.0f);
        target(bx, by, bw, bh, kHdHitBattleAction, i);
    }
}

void draw_moves(Ctx& c, const BattlerView& me, const BattlerView* foe, int cursor, float x, float y, float w,
                float h, unsigned type) {
    const float gap = 3.0f;
    const float bw = (w - gap) * 0.5f, bh = (h - gap) * 0.5f;
    for (int i = 0; i < 4; ++i) {
        const float bx = x + (i & 1) * (bw + gap), by = y + (i >> 1) * (bh + gap);
        const int mv = me.moves[i];
        const bool sel = i == cursor;
        if (!mv) {
            rect(c, bx, by, bw, bh, col::rgba(12, 15, 24, 170), 5.0f);
            rect_line(c, bx, by, bw, bh, col::alpha(col::kWhite, 0.06f), 5.0f, 0.45f);
            text(c, F::Medium, 8.0f, bx + bw * 0.5f, by + (bh - 8.0f) * 0.5f, col::kTextFaint, "\xe2\x80\x94", kCenter);
            continue;
        }
        const int t = move_type(c.m, mv);
        const ImU32 tc = col::type_color(t);
        const float grow = sel ? 0.8f : 0.0f;
        if (sel) {
            for (int g = 3; g >= 1; --g)
                rect(c, bx - grow - g, by - grow - g, bw + 2 * (grow + g), bh + 2 * (grow + g),
                     col::alpha(tc, 0.10f), 5.0f + g);
        }
        shadow(c, bx, by, bw, bh, 5.0f, 2.0f, 0.8f);
        const ImU32 top = sel ? col::mix(tc, col::kWhite, 0.15f) : col::mix(tc, col::rgba(16, 20, 30), 0.66f);
        const ImU32 bot = sel ? col::mix(tc, col::kBlack, 0.15f) : col::mix(tc, col::rgba(10, 13, 20), 0.78f);
        grad_v(c, bx - grow, by - grow, bw + 2 * grow, bh + 2 * grow, top, bot, 5.0f);
        rect(c, bx - grow, by - grow + 3.0f, 2.2f, bh + 2 * grow - 6.0f, col::mix(tc, col::kWhite, 0.25f), 1.1f);
        rect_line(c, bx - grow, by - grow, bw + 2 * grow, bh + 2 * grow,
                  sel ? col::alpha(col::kWhite, 0.85f) : col::alpha(col::kWhite, 0.10f), 5.0f, sel ? 0.7f : 0.45f);
        text(c, F::Bold, 8.2f, bx + 7.0f, by + 1.4f, col::kWhite, move_name(c.m, mv), kLeft, bw - 44.0f);
        // PP
        const int base = move_pp(c.m, mv);
        const int bonus = (me.pp_bonuses >> (2 * i)) & 3;
        const int maxpp = base * (5 + bonus) / 5;
        char pp[16];
        std::snprintf(pp, sizeof pp, "%d/%d", me.pp[i], maxpp);
        const ImU32 ppc = me.pp[i] == 0 ? col::kBad : (me.pp[i] * 4 <= maxpp ? col::kWarn : col::alpha(col::kWhite, 0.92f));
        text(c, F::Bold, 7.0f, bx + bw - 5.0f, by + 2.0f, ppc, pp, kRight);
        text(c, F::Medium, 5.2f, bx + bw - 6.5f - text_w(F::Bold, 7.0f, pp), by + 3.4f, col::alpha(col::kWhite, 0.65f), "PP", kRight);
        // type tag + effectiveness on the second line
        const std::string tn = type_name(t);
        const float tw = text_w(F::Bold, 5.2f, tn) + 5.0f;
        const float ty = by + bh - 7.2f;
        rect(c, bx + 7.0f, ty, tw, 5.8f, col::alpha(col::kBlack, 0.30f), 2.9f);
        text(c, F::Bold, 5.2f, bx + 7.0f + tw * 0.5f, ty + 0.2f, col::kWhite, tn, kCenter, 0, false);
        if (foe && foe->present && move_power(c.m, mv) > 0 && !(type & kTypeGhost && !(type & kTypeGhostUnveiled))) {
            const int eff = type_effectiveness(c.m, t, foe->type1, foe->type2);
            const char* label = eff == 0 ? "No effect" : eff < 10 ? "Not very effective" : eff > 10 ? "Super effective" : "";
            const ImU32 ec = eff == 0 ? col::rgba(200, 200, 210) : eff < 10 ? col::rgba(220, 205, 205) : col::rgba(150, 255, 170);
            if (*label) {
                const float lw = text_w(F::Bold, 5.2f, label) + 5.0f;
                rect(c, bx + bw - 5.0f - lw, ty, lw, 5.8f, col::alpha(col::kBlack, 0.30f), 2.9f);
                text(c, F::Bold, 5.2f, bx + bw - 5.0f - lw * 0.5f, ty + 0.2f, ec, label, kCenter, 0, false);
            }
        }
        target(bx, by, bw, bh, kHdHitBattleMove, i);
    }
}

}  // namespace

bool battle_active(const Mem& m, const Scene&) {
    return hud_supported(m.u32(kBattleTypeFlags));
}

void battle_guest_frame(const Mem& m, const Scene& s, unsigned* bg_mask, bool* hide_objs) {
    if (!battle_active(m, s)) return;
    *bg_mask |= 1u;   // the battle text layer (message box, menus)
    // healthbox / party-summary sprite sheets → hidden tile ranges
    g_hide.count = 0;
    for (int i = 0; i < 64 && g_hide.count < 16; ++i) {
        const int tag = m.u16(g::SpriteTileTags + i * 2);
        const bool hud = (tag >= 55039 && tag <= 55047) || tag == 55051 || tag == 55052 ||
                         tag == 55053 || tag == 55060 || tag == 55061;
        if (!hud) continue;
        const int start = m.u16(g::SpriteTileRanges + i * 4);
        const int count = m.u16(g::SpriteTileRanges + i * 4 + 2);
        g_hide.lo[g_hide.count] = static_cast<std::uint16_t>(start);
        g_hide.hi[g_hide.count] = static_cast<std::uint16_t>(start + count);
        ++g_hide.count;
    }
    *hide_objs = g_hide.count > 0;
}

int battle_obj_hide(int, std::uint16_t a0, std::uint16_t, std::uint16_t a2) {
    if ((a0 & 0x0300) == 0x0200) return 0;  // already disabled
    const int tile = a2 & 0x3FF;
    for (int i = 0; i < g_hide.count; ++i)
        if (tile >= g_hide.lo[i] && tile < g_hide.hi[i]) return 1;
    return 0;
}

void draw_battle(Ctx& c) {
    const Mem& m = c.m;
    const Scene& s = *c.sc;
    const unsigned type = m.u32(kBattleTypeFlags);
    const bool doubles = (type & kTypeDouble) != 0;
    BattlerView bv[4];
    for (int b = 0; b < 4; ++b) bv[b] = read_battler(m, b);

    // The game's text box covered the bottom rows; a tray replaces it.
    grad_v(c, 0, 102.0f, c.vw, 11.0f, col::rgba(8, 10, 17, 0), col::rgba(8, 10, 17, 235), 0);
    grad_v(c, 0, 113.0f, c.vw, 47.0f, col::rgba(8, 10, 17, 245), col::rgba(5, 7, 12, 255), 0);

    // cards
    for (int b = 0; b < 4; ++b) {
        const BattlerView& v = bv[b];
        if (!v.present) { g_card[b].shown = false; continue; }
        const bool right_slot = (v.position & 2) != 0;
        if (v.player) {
            const float w = doubles ? 104.0f : 122.0f;
            const float x = c.vw - 8.0f - w;
            const float y = doubles ? (right_slot ? 87.0f : 61.0f) : 76.0f;
            draw_card(c, b, v, x, y, w, doubles, type);
        } else {
            const float w = doubles ? 104.0f : 116.0f;
            const float y = doubles ? (right_slot ? 6.0f : 32.0f) : 8.0f;
            draw_card(c, b, v, 8.0f, y, w, doubles, type);
        }
    }

    // bottom band
    const float band_y = 116.0f, band_h = 38.0f;
    const CapWindow& msg = cap_window(0);
    const PrinterInfo pr = printer_info(m, 0);
    const bool waiting = msg.arrow || (pr.active && (pr.state == 2 || pr.state == 3));
    if (s.battle == BattleCtl::Action) {
        const int cursor = m.u8(kActionCursor + s.battler);
        const float aw = 132.0f;
        draw_text_box(c, 1, 8.0f, band_y, c.vw - 16.0f - aw - 4.0f, band_h, false);
        draw_actions(c, cursor, c.vw - 8.0f - aw, band_y, aw, band_h);
    } else if (s.battle == BattleCtl::Move) {
        const int cursor = m.u8(kMoveCursor + s.battler);
        const BattlerView* foe = nullptr;
        for (int b = 0; b < 4; ++b) if (bv[b].present && !bv[b].player && bv[b].hp > 0) { foe = &bv[b]; break; }
        draw_moves(c, bv[s.battler], doubles ? nullptr : foe, cursor, 8.0f, band_y - 4.0f, c.vw - 16.0f, 39.0f, type);
        // back hint under the buttons
        const float kw = keycap_for(c, 0, 0, 7.0f, 1, true);
        const float hx = c.vw - 10.0f - text_w(F::Medium, 6.4f, "Back") - 2.5f - kw;
        keycap_for(c, hx, 152.0f, 6.6f, 1);
        text(c, F::Medium, 6.2f, c.vw - 10.0f, 152.3f, col::kTextDim, "Back", kRight);
        target(hx, 151.0f, c.vw - 10.0f - hx, 9.0f, kHdHitKey, 1);
    } else {
        const bool has_text = !msg.glyphs.empty() && window_shown(m, 0);
        if (has_text) {
            draw_text_box(c, 0, 8.0f, band_y, c.vw - 16.0f, band_h, waiting);
            target(8.0f, band_y, c.vw - 16.0f, band_h, kHdHitAdvance, 0);
        }
    }
    // other battle windows (level-up stats, yes/no, switch prompt, ...)
    for (int id = 10; id < 25; ++id) {
        if (!window_shown(m, id)) continue;
        const Rect r = window_rect(m, id);
        if (r.y >= 160 || r.y + r.h <= 0) continue;
        const CapWindow& cw = cap_window(id);
        if (cw.glyphs.empty() && !cw.gfx) continue;
        const float x = c.margin + r.x, y = static_cast<float>(r.y);
        panel(c, x - 3.5f, y - 3.5f, r.w + 7.0f, r.h + 7.0f, 5.0f);
        for (const TextRun& tr : cap_runs(id, modern_text())) {
            if (tr.arrow) {
                pill_select(c, x - 1.5f, y + tr.y - 1.0f, r.w + 3.0f, tr.h + 1.0f, 4.0f);
                continue;
            }
            text(c, F::Medium, 9.6f, x + tr.x, y + tr.y + tr.h * 0.5f - 4.8f, col::text_color(tr.fg), tr.text);
        }
    }
}

}  // namespace frlg::hd
