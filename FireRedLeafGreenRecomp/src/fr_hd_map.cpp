// fr_hd_map.cpp — HD chrome for the region map (Town Map and Fly).
//
// The map artwork itself stays the game's (BG0 plus its icon and cursor
// sprites); the device frame, top bar and name boxes around it are covered
// by a modern header, side panels and a location tab, and every map cell is
// a pointer target (hover moves the game's cursor, click also presses A).

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "fr_hd_capture.h"
#include "fr_hd_draw.h"
#include "fr_hd_screens.h"

namespace frlg::hd {
namespace {

constexpr float kMapL = 24.0f, kMapR = 216.0f, kMapT = 16.0f;  // native picture rect
constexpr float kRadius = 6.0f;

struct MapAnim {
    float alpha = 0;
    double last_t = -1;
} g_map;

// Fill the area between a square corner and its rounded arc.
void fillet(Ctx& c, float cx, float cy, float r, int corner, ImU32 color) {
    // corner: 0 top-left, 1 top-right
    const float sx = corner == 0 ? 1.0f : -1.0f;
    const ImVec2 sq = c.P(cx, cy);
    const ImVec2 centre = c.P(cx + sx * r, cy + r);
    const float a0 = corner == 0 ? 3.14159265f : 1.5f * 3.14159265f;
    const ImDrawListFlags saved = c.dl->Flags;
    c.dl->Flags &= ~ImDrawListFlags_AntiAliasedFill;
    c.dl->PathClear();
    c.dl->PathLineTo(sq);
    c.dl->PathArcTo(centre, r * c.s, a0, a0 + 0.5f * 3.14159265f, 10);
    c.dl->PathFillConvex(color);
    c.dl->Flags = saved;
}

float hint_row_width(std::initializer_list<Hint> list) {
    float w = 0;
    for (const Hint& h : list) {
        w += text_w(F::Medium, 6.6f, h.label) + 2.5f + 6.0f;
        for (int b : {h.bit, h.bit2}) {
            if (b < 0) continue;
            Ctx dummy;
            w += keycap_for(dummy, 0, 0, 7.5f, b, true) + 1.5f;
        }
    }
    return w;
}

const char* region_name(int r) {
    switch (r) {
        case 1: return "Sevii Islands 1\xe2\x80\x93" "3";
        case 2: return "Sevii Islands 4\xe2\x80\x93" "5";
        case 3: return "Sevii Islands 6\xe2\x80\x93" "7";
        default: return "Kanto";
    }
}

void draw_region_map(Ctx& c) {
    const Mem& m = c.m;
    const std::uint32_t rm = m.u32(rmap::RegionMapPtr);
    if (!m.valid(rm, 0x47C0)) return;
    const std::uint32_t task = rmap::task(m);
    const std::uint32_t mc = m.u32(rmap::MapCursorPtr);
    const bool cursor_ok = m.valid(mc, 0x20);
    const bool fly = m.u8(rm + rmap::kType) == 2 || task == rmap::Task_FlyMap;
    const bool anim = task == rmap::Task_MapOpenAnim || task == rmap::Task_MapCloseAnim || task == 0;
    const bool steady = rmap::steady(m);

    // Fade the chrome in after the opening animation and out for the closing one.
    if (g_map.last_t < 0 || c.t - g_map.last_t > 0.25) g_map.alpha = 0;
    g_map.last_t = c.t;
    const float goal = anim ? 0.0f : 1.0f;
    g_map.alpha += (goal - g_map.alpha) * (1.0f - std::exp(-c.dt * 16.0f));
    if (std::fabs(goal - g_map.alpha) < 0.01f) g_map.alpha = goal;
    const float a = g_map.alpha;
    if (a <= 0.0f) return;
    auto A = [a](ImU32 col_) { return col::alpha(col_, a); };

    const float L = c.nx(static_cast<int>(kMapL)), R = c.nx(static_cast<int>(kMapR)), T = kMapT;
    const ImU32 bg_top = col::rgba(20, 25, 42), bg = col::rgba(12, 15, 26);

    // ── frame: header band + side panels ────────────────────────────────
    grad_v(c, 0, 0, c.vw, T, A(bg_top), A(col::rgba(16, 20, 34)));
    grad_v(c, 0, T, L, c.vh - T, A(col::rgba(16, 20, 34)), A(bg));
    grad_v(c, R, T, c.vw - R, c.vh - T, A(col::rgba(16, 20, 34)), A(bg));
    for (float x = -160.0f; x < c.vw; x += 9.0f) {   // pinstripes, sides only
        push_clip(c, 0, T, L, c.vh - T);
        line(c, x, c.vh, x + 160.0f, 0, A(col::rgba(255, 255, 255, 5)), 0.35f);
        pop_clip(c);
        push_clip(c, R, T, c.vw - R, c.vh - T);
        line(c, x, c.vh, x + 160.0f, 0, A(col::rgba(255, 255, 255, 5)), 0.35f);
        pop_clip(c);
    }
    // depth under the header
    grad_v(c, L, T, R - L, 5.0f, A(col::rgba(0, 0, 0, 80)), col::rgba(0, 0, 0, 0));

    // ── location tab (covers the game's darkened name boxes) ────────────
    const std::uint16_t dispcnt = m.io16(0x00);
    const bool name_box = (dispcnt & 0x2000) && (m.io16(0x40) >> 8) < (m.io16(0x40) & 0xFF);
    const bool dungeon_box = (dispcnt & 0x4000) && (m.io16(0x42) >> 8) < (m.io16(0x42) & 0xFF);
    const std::string name = rom_string(m, rm + rmap::kMapName, 19, true);
    const std::string dungeon = rom_string(m, rm + rmap::kDungeonName, 19, true);
    const bool main_task = task == rmap::Task_RegionMap || task == rmap::Task_FlyMap ||
                           task == rmap::Task_DungeonPreview || task == rmap::Task_DungeonPreviewText;
    if (main_task && ((name_box && !name.empty()) || dungeon_box)) {
        const float tw = 122.0f, th = dungeon_box ? 34.0f : 18.0f;
        rect(c, L, T, tw, th, A(col::rgba(14, 18, 30)), kRadius,
             ImDrawFlags_RoundCornersTopLeft | ImDrawFlags_RoundCornersBottomRight);
        // map pin
        const float px = L + 8.0f, py = T + 8.5f;
        circle(c, px, py - 1.2f, 3.0f, A(col::kAccent), 20);
        tri(c, px - 2.6f, py - 0.2f, px + 2.6f, py - 0.2f, px, py + 3.8f, A(col::kAccent));
        circle(c, px, py - 1.2f, 1.1f, A(col::rgba(14, 18, 30)), 12);
        text(c, F::Bold, 8.6f, L + 15.0f, T + 4.2f, A(col::kText), name.empty() ? "\xe2\x80\x94" : name, kLeft, tw - 20.0f);
        if (dungeon_box && !dungeon.empty()) {
            const int dtype = cursor_ok ? m.u16(mc + 24) : 0;
            const bool visited = dtype == 2;
            line(c, L + 6.0f, T + 17.5f, L + tw - 6.0f, T + 17.5f, A(col::kStroke), 0.4f);
            rect(c, L + 6.0f, T + 22.0f, 6.0f, 6.0f, A(visited ? col::kGood : col::kTextFaint), 1.6f);
            text(c, F::Medium, 7.4f, L + 15.0f, T + 21.0f, A(visited ? col::kText : col::kTextDim), dungeon,
                 kLeft, tw - 20.0f);
        }
    }
    // area guide: the flavour text the game prints over the preview picture
    if (task == rmap::Task_DungeonPreviewText || task == rmap::Task_DungeonPreview) {
        std::string head, body;
        for (const TextRun& r : cap_runs(2, modern_text())) {
            if (r.arrow || r.keypad >= 0 || r.text.empty()) continue;
            std::string& dst = r.y < 12 ? head : body;   // title row, then the description
            dst += (dst.empty() ? "" : " ") + r.text;
        }
        if (!head.empty() || !body.empty()) {
            const float cw = R - L - 16.0f;
            const int lines = std::min(5, wrap_count(F::Medium, 7.6f, cw - 18.0f, body));
            const float ch = 16.0f + 9.4f * static_cast<float>(lines) + 4.0f;
            const float cx = L + 8.0f, cy = 132.0f - ch;
            shadow(c, cx, cy, cw, ch, 6.0f, 4.0f, a);
            rect(c, cx, cy, cw, ch, A(col::rgba(12, 15, 26, 228)), 6.0f);
            rect_line(c, cx, cy, cw, ch, A(col::kStroke), 6.0f, 0.45f);
            rect(c, cx + 6.0f, cy + 5.0f, 2.0f, 8.0f, A(col::kGood), 1.0f);
            text(c, F::Bold, 9.0f, cx + 11.0f, cy + 4.2f, A(col::kText), head, kLeft, cw - 16.0f);
            wrap_text(c, F::Medium, 7.6f, cx + 11.0f, cy + 16.5f, cw - 18.0f, 9.4f, A(col::rgba(214, 220, 232)), body, 5);
        }
    }
    // rounded picture corners + border
    fillet(c, L, T, kRadius, 0, A(col::rgba(16, 20, 34)));
    fillet(c, R, T, kRadius, 1, A(col::rgba(16, 20, 34)));
    rect_line(c, L, T, R - L, c.vh - T + 10.0f, A(col::kStrokeHi), kRadius, 0.5f);

    // ── header: title + key hints ────────────────────────────────────────
    rect(c, 10.0f, 4.0f, 2.0f, 8.5f, A(col::kAccent), 1.0f);
    const char* ttl = fly ? "Fly" : "Town Map";
    text(c, F::Bold, 10.0f, 15.0f, 2.6f, A(col::kText), ttl);
    text(c, F::Medium, 7.0f, 20.0f + text_w(F::Bold, 10.0f, ttl), 5.0f, A(col::kTextDim),
         region_name(m.u8(rm + rmap::kSelectedRegion)));

    // the A action under the cursor
    std::string act;
    if (steady && cursor_ok) {
        const int x = m.s16(mc), y = m.s16(mc + 2);
        const int type = m.u16(mc + 22), dtype = m.u16(mc + 24);
        const bool perm_switch = m.u8(rm + rmap::kPerms + 0) == 1;
        const bool perm_preview = m.u8(rm + rmap::kPerms + 1) == 1;
        if (x == rmap::kCancelX && y == rmap::kCancelY) act = "Close";
        else if (x == rmap::kSwitchX && y == rmap::kSwitchY && perm_switch) act = "Switch Map";
        else if (fly && (type == 2 || type == 4)) act = "Fly Here";
        else if (!fly && dtype == 2 && perm_preview) act = "Area Guide";
    }
    if (a > 0.5f) {
        const float hy = 4.2f;
        if (task == rmap::Task_SwitchMapMenu) {
            hints(c, c.vw - 8.0f, hy, {{6, "Pick", 7}, {0, "Switch"}, {1, "Cancel"}});
        } else if (task == rmap::Task_DungeonPreview || task == rmap::Task_DungeonPreviewText) {
            hints(c, c.vw - 8.0f, hy, {{1, "Back"}});
        } else {
            float w;
            if (!act.empty()) {
                hints(c, c.vw - 8.0f, hy, {{0, act.c_str()}, {1, "Close"}});
                w = hint_row_width({{0, act.c_str()}, {1, "Close"}});
            } else {
                hints(c, c.vw - 8.0f, hy, {{1, "Close"}});
                w = hint_row_width({{1, "Close"}});
            }
            // "Move" with the four direction keys
            float x = c.vw - 8.0f - w - text_w(F::Medium, 6.6f, "Move");
            if (x > 110.0f) {
                text(c, F::Medium, 6.6f, x, hy + 0.6f, A(col::kTextDim), "Move");
                x -= 2.5f;
                for (int b : {4, 7, 5, 6}) {
                    x -= keycap_for(c, 0, 0, 7.5f, b, true);
                    keycap_for(c, x, hy, 7.5f, b);
                    x -= 1.0f;
                }
            }
        }
    }

    // ── pointer: every cursor cell ───────────────────────────────────────
    if (steady) {
        for (int y = 0; y < rmap::kHeight; ++y)
            for (int x = 0; x < rmap::kWidth; ++x)
                target(c.nx(rmap::kCellX0 + 8 * x), static_cast<float>(rmap::kCellY0 + 8 * y), 8.0f, 8.0f,
                       kHdHitMapCell, y * 32 + x);
    }
}

}  // namespace

bool map_draw(Ctx& c) {
    if (c.sc->cb2 != rmap::CB2_RegionMap) return false;
    draw_region_map(c);
    return true;
}

}  // namespace frlg::hd
