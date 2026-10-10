// fr_hd_screens2.cpp — more HD full-screen menus: Poké Mart, PC storage,
// TM case. Drawn from the game's own state, like fr_hd_screens.cpp.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "fr_hd_capture.h"
#include "fr_hd_draw.h"
#include "fr_hd_screens.h"
#include "fr_hd_text.h"

namespace frlg::hd {
namespace {

constexpr std::uint32_t kCB2_BuyMenu   = 0x0809ADE4;
constexpr std::uint32_t kShopData      = 0x02039934;  // sShopData
constexpr std::uint32_t kItems         = 0x083DB028;

// ── Poké Mart (buy) ──────────────────────────────────────────────────────
struct MartAnim { float hl = -1; } g_mart;

void draw_mart(Ctx& c) {
    const Mem& m = c.m;
    const Scene& s = *c.sc;
    screen_bg(c, col::kBlue);
    const std::uint32_t list = m.u32(kShopData + 4);
    const int count = std::clamp<int>(m.u16(kShopData + 0x10), 0, 64);
    const bool tm_mart = (m.u16(kShopData + 0x16) & 0xF) == 1;
    title(c, "Pok\xc3\xa9 Mart", tm_mart ? "TMs" : "");
    // money chip
    const std::uint32_t sb1 = m.u32(g::SaveBlock1Ptr), sb2 = m.u32(g::SaveBlock2Ptr);
    const unsigned money = m.u32(sb1 + 0x290) ^ m.u32(sb2 + 0xF20);
    {
        const std::string ms = money_str(money > 9999999 ? 0 : money);
        const float w = text_w(F::Bold, 8.4f, ms) + 16.0f;
        rect(c, c.vw - 10.0f - w, 5.5f, w, 12.0f, col::rgba(255, 255, 255, 16), 6.0f);
        text(c, F::Bold, 8.4f, c.vw - 10.0f - w * 0.5f, 7.2f, col::kGold, ms, kCenter);
    }
    const int total = count + 1;
    const int rows = std::max(1, s.list ? s.list_max : std::min<int>(m.u16(kShopData + 0x12), tm_mart ? 5 : 6));
    const int scroll = s.list ? s.list_scroll : m.u16(kShopData + 0x0E);
    const int sel = s.list ? s.list_scroll + s.list_sel : scroll + m.u16(kShopData + 0x0C);
    const float lx = 10.0f, ly = 22.0f, lw = std::min(160.0f, c.vw - 118.0f), lh = 122.0f;
    panel(c, lx, ly, lw, lh, 6.0f, col::rgba(17, 22, 35, 235));
    const float row = (lh - 6.0f) / rows;
    const float hl_t = ly + 3.0f + (sel - scroll) * row;
    if (g_mart.hl < 0) g_mart.hl = hl_t;
    g_mart.hl = hl_t + (g_mart.hl - hl_t) * std::exp(-c.dt * 24.0f);
    pill_select(c, lx + 3.0f, g_mart.hl, lw - 6.0f, row - 0.5f, std::min(6.0f, row * 0.4f), s.list ? 1.0f : 0.3f);
    static ScrollAnim g_mart_scroll;
    const float sp_ = scroll_anim(g_mart_scroll, 0x200u, scroll, c.dt);
    push_clip(c, lx, ly + 2.0f, lw, lh - 4.0f);
    for (int r = -1; r <= rows; ++r) {
        const int idx = scroll + r;
        if (idx < 0) continue;
        if (idx >= total) break;
        const float ry = ly + 3.0f + (static_cast<float>(idx) - sp_) * row;
        const bool is_sel = idx == sel;
        const ImU32 fg = is_sel ? col::kWhite : col::kText;
        if (idx == count) {
            icon(c, Icon::Back, lx + 13.0f, ry + row * 0.5f, 7.0f, is_sel ? col::kWhite : col::kTextDim);
            text(c, is_sel ? F::Bold : F::Medium, 8.6f, lx + 24.0f, ry + (row - 8.6f) * 0.5f, fg, "Done Shopping");
        } else {
            const int item = m.u16(list + idx * 2);
            image(c, tex_item_icon(m, item), lx + 5.0f, ry + (row - 14.0f) * 0.5f, 14.0f, 14.0f);
            text(c, is_sel ? F::Bold : F::Medium, 8.6f, lx + 24.0f, ry + (row - 8.6f) * 0.5f, fg,
                 item_name(m, item), kLeft, lw - 66.0f);
            const unsigned price = m.u16(kItems + item * 44 + 16);
            const bool afford = price <= money;
            text(c, F::Bold, 7.8f, lx + lw - 8.0f, ry + (row - 7.8f) * 0.5f,
                 is_sel ? col::kWhite : (afford ? col::kGold : col::kBad), money_str(price), kRight);
        }
        if (r >= 0 && r < rows) target(lx, ly + 3.0f + r * row, lw, row, kHdHitList, idx);
    }
    pop_clip(c);
    if (total > rows) {
        const float th = lh - 10.0f, frac = static_cast<float>(rows) / total;
        const float pos = static_cast<float>(scroll) / std::max(1, total - rows);
        rect(c, lx + lw + 2.0f, ly + 5.0f, 1.6f, th, col::rgba(255, 255, 255, 18), 0.8f);
        rect(c, lx + lw + 2.0f, ly + 5.0f + (th - th * frac) * pos, 1.6f, th * frac, col::kBlue, 0.8f);
    }
    // detail
    const float dx = lx + lw + 8.0f, dw = c.vw - dx - 10.0f;
    panel(c, dx, ly, dw, lh, 6.0f, col::rgba(17, 22, 35, 235));
    if (sel >= 0 && sel < count) {
        const int item = m.u16(list + sel * 2);
        for (int g = 4; g >= 1; --g) circle(c, dx + dw * 0.5f, ly + 26.0f, 10.0f + g * 4.0f, col::alpha(col::kBlue, 0.04f), 32);
        image(c, tex_item_icon(m, item), dx + dw * 0.5f - 18.0f, ly + 8.0f, 36.0f, 36.0f);
        text(c, F::Bold, 9.6f, dx + dw * 0.5f, ly + 46.0f, col::kText, item_name(m, item), kCenter, dw - 10.0f);
        char inbag[32];
        std::snprintf(inbag, sizeof inbag, "In bag: %d", bag_count(m, item));
        text(c, F::Medium, 6.8f, dx + dw * 0.5f, ly + 57.0f, col::kTextDim, inbag, kCenter);
        wrap_text(c, F::Medium, 7.4f, dx + 7.0f, ly + 68.0f, dw - 14.0f, 9.0f, col::kText,
                  rom_string(m, m.u32(kItems + item * 44 + 0x14), 200, true), 5);
    } else {
        icon(c, Icon::Back, dx + dw * 0.5f, ly + 30.0f, 22.0f, col::kTextDim);
        text(c, F::Bold, 9.0f, dx + dw * 0.5f, ly + 50.0f, col::kText, "Done Shopping", kCenter);
        text(c, F::Medium, 7.2f, dx + dw * 0.5f, ly + 62.0f, col::kTextDim, "Leave the shop.", kCenter);
    }
    // quantity picker (window 3) and messages (window 2)
    if (window_shown(m, 3) && !cap_window(3).glyphs.empty()) {
        const std::string q = window_message(3);
        const float qw = 104.0f, qh = 20.0f, qx = dx + (dw - qw) * 0.5f, qy = ly + lh - qh - 6.0f;
        shadow(c, qx, qy, qw, qh, 6.0f, 4.0f, 1.2f);
        grad_v(c, qx, qy, qw, qh, col::rgba(34, 42, 64), col::rgba(22, 27, 42), 6.0f);
        rect_line(c, qx, qy, qw, qh, col::kStrokeHi, 6.0f, 0.45f);
        const float bs = qh - 6.0f;
        for (int side = 0; side < 2; ++side) {
            const float bx = side ? qx + qw - 3.0f - bs : qx + 3.0f;
            rect(c, bx, qy + 3.0f, bs, bs, col::rgba(255, 255, 255, 22), bs * 0.5f);
            line(c, bx + bs * 0.3f, qy + 3.0f + bs * 0.5f, bx + bs * 0.7f, qy + 3.0f + bs * 0.5f, col::kText, 0.9f);
            if (side) line(c, bx + bs * 0.5f, qy + 3.0f + bs * 0.3f, bx + bs * 0.5f, qy + 3.0f + bs * 0.7f, col::kText, 0.9f);
            target(bx, qy, bs + 3.0f, qh, kHdHitKey, side ? 6 : 7);  // Up raises, Down lowers
        }
        text(c, F::Bold, 9.0f, qx + qw * 0.5f, qy + (qh - 9.0f) * 0.5f, col::kText, q, kCenter, qw - 2 * bs - 12.0f);
    }
    std::string msg = window_shown(m, 2) ? window_message(2) : std::string();
    if (!msg.empty()) {
        const CapWindow& cw = cap_window(2);
        message_bar(c, 10.0f, 146.0f, c.vw - 20.0f, 12.0f, msg, cw.arrow);
        target(10.0f, 146.0f, c.vw - 20.0f, 12.0f, kHdHitAdvance, 0);
    } else {
        hints(c, c.vw - 10.0f, 150.0f, {{6, "Choose", 7}, {0, "Buy"}, {1, "Leave"}});
    }
    if (s.menu) context_menu(c, s, dx + dw - 60.0f, ly + lh - 2.0f, 56.0f);
}


// ── PC storage ───────────────────────────────────────────────────────────
constexpr std::uint32_t kCB2_PokeStorage  = 0x0808CDC4;
constexpr std::uint32_t kStoragePtrVar    = 0x020397B0;  // gStorage
constexpr std::uint32_t kStoragePtr       = 0x03005010;  // gPokemonStoragePtr (the block moves)
constexpr std::uint32_t kCursorArea       = 0x02039820;
constexpr std::uint32_t kCursorPos        = 0x02039821;
constexpr std::uint32_t kMonBeingMoved    = 0x02039822;
constexpr int           kStorageMovingMon = 0x20A0;      // gStorage->movingMon
constexpr std::uint32_t kMovingItemId     = 0x020397BA;
constexpr std::uint32_t kPlayerParty      = 0x02024284;
constexpr std::uint32_t kPlayerPartyCnt   = 0x02024029;

// Storage tasks the HD box view covers; anything else (box jump / deposit
// box picker, wallpapers, markings) keeps the original sprite-based screen.
bool storage_task_supported(const Mem& m) {
    static const std::uint32_t kSupported[] = {
        0x0808D2BC, 0x0808D7BC, 0x0808D7FC, 0x0808D878, 0x0808DB94, 0x0808DBF0, 0x0808DC50,
        0x0808DC9C, 0x0808DECC, 0x0808E140, 0x0808E1F4, 0x0808E2B0, 0x0808E3C8, 0x0808E4B0,
        0x0808E564, 0x0808E688, 0x0808E6D0, 0x0808E744, 0x0808EBB0, 0x0808ECE4, 0x0808EAF0,
        0x0808EB50, 0x0808EE18, 0x0808D26C, 0x0808D220,
    };
    for (int i = 0; i < 16; ++i) {
        const std::uint32_t t = g::Tasks + i * 0x28;
        if (!m.u8(t + 4)) continue;
        const std::uint32_t f = m.func(t);
        if (f < 0x0808D000 || f > 0x0808EF00) continue;
        for (std::uint32_t s : kSupported) if (f == s) return true;
        return false;
    }
    return false;
}

ImU32 wallpaper_color(int w) {
    static const ImU32 k[16] = {
        col::rgba(76, 160, 92), col::rgba(150, 120, 200), col::rgba(214, 170, 90), col::rgba(190, 160, 80),
        col::rgba(150, 110, 80), col::rgba(220, 90, 60), col::rgba(150, 200, 240), col::rgba(110, 100, 150),
        col::rgba(90, 190, 220), col::rgba(60, 110, 190), col::rgba(80, 160, 210), col::rgba(140, 190, 250),
        col::rgba(240, 130, 170), col::rgba(240, 110, 110), col::rgba(130, 140, 160), col::rgba(160, 170, 185),
    };
    return k[w & 15];
}

struct StorageAnim {
    int box = -1;
    double box_since = 0;
    int box_dir = 0;
    float cx = -1, cy = -1;
    float party_k = 0;
} g_st;

void mon_card(Ctx& c, const MonView& mv, float x, float y, float w, float h, bool item_mode, int moving_item) {
    panel(c, x, y, w, h, 6.0f, col::rgba(17, 22, 35, 235));
    if (!mv.valid) {
        text(c, F::Medium, 7.4f, x + w * 0.5f, y + h * 0.42f, col::kTextFaint, "Empty", kCenter);
        return;
    }
    const ImU32 t1 = col::type_color(species_type(c.m, mv.species, 0));
    for (int g = 4; g >= 1; --g) circle(c, x + w * 0.5f, y + 34.0f, 12.0f + g * 5.0f, col::alpha(t1, 0.045f), 36);
    const float bob = std::sin(static_cast<float>(c.t) * 2.0f) * 1.0f;
    image(c, tex_mon_front(c.m, mv.egg ? 412 : mv.species, mv.shiny, mv.personality), x + w * 0.5f - 30.0f, y + 4.0f + bob, 60.0f, 60.0f);
    std::string name = mv.egg ? std::string("Egg") : mv.nickname;
    const float nw = std::min(w - 18.0f, text_w(F::Bold, 9.4f, name));
    text(c, F::Bold, 9.4f, x + 7.0f, y + 66.0f, col::kText, name, kLeft, w - 18.0f);
    if (!mv.egg && mv.gender != 2)
        text(c, F::Bold, 8.6f, x + 8.5f + nw, y + 66.4f, mv.gender ? col::kPink : col::kBlue, mv.gender ? "\xe2\x99\x80" : "\xe2\x99\x82");
    if (mv.egg) return;
    char lv[48];
    std::snprintf(lv, sizeof lv, "Lv %d  \xc2\xb7  %s", mv.level, species_name(c.m, mv.species).c_str());
    text(c, F::Medium, 6.8f, x + 7.0f, y + 77.5f, col::kTextDim, lv, kLeft, w - 12.0f);
    float w1 = 0;
    const int ty1 = species_type(c.m, mv.species, 0), ty2 = species_type(c.m, mv.species, 1);
    type_badge(c, ty1, x + 7.0f, y + 87.0f, 7.6f, &w1);
    if (ty2 != ty1) type_badge(c, ty2, x + 9.0f + w1, y + 87.0f, 7.6f);
    const int item = item_mode && moving_item ? moving_item : mv.item;
    if (item) {
        image(c, tex_item_icon(c.m, item), x + 5.0f, y + h - 16.0f, 12.0f, 12.0f);
        text(c, F::Medium, 6.8f, x + 18.0f, y + h - 13.5f, col::kText, item_name(c.m, item), kLeft, w - 22.0f);
    } else {
        text(c, F::Medium, 6.6f, x + 7.0f, y + h - 13.5f, col::kTextFaint, "No held item");
    }
}

void draw_storage(Ctx& c) {
    const Mem& m = c.m;
    const std::uint32_t kPokemonStorage = m.u32(kStoragePtr);
    if (!m.valid(kPokemonStorage, 0x83D0)) return;
    const Scene& s = *c.sc;
    const std::uint32_t st = m.u32(kStoragePtrVar);
    const int option = m.valid(st, 4) ? m.u8(st + 1) : 2;
    const int box = std::clamp<int>(m.u8(kPokemonStorage), 0, 13);
    const int area = m.s8(kCursorArea), pos = m.s8(kCursorPos);
    const bool carrying = m.u8(kMonBeingMoved) != 0;
    const bool item_mode = option == 3;
    const int moving_item = item_mode ? m.u16(kMovingItemId) : 0;
    const int wallpaper = m.u8(kPokemonStorage + 0x83C2 + box) & 15;
    const ImU32 wc = wallpaper_color(wallpaper);
    screen_bg(c, wc);
    static const char* const kModes[4] = {"Withdraw", "Deposit", "Move Pok\xc3\xa9mon", "Move Items"};
    title(c, "Pok\xc3\xa9mon Storage", kModes[option & 3]);

    // top-right buttons: Party / Close
    const char* kButtons[2] = {"Party", "Close"};
    float bx = c.vw - 10.0f;
    float btn_x[2], btn_w[2];
    for (int i = 1; i >= 0; --i) {
        btn_w[i] = text_w(F::Bold, 7.6f, kButtons[i]) + 18.0f;
        bx -= btn_w[i];
        btn_x[i] = bx;
        bx -= 3.0f;
    }
    for (int i = 0; i < 2; ++i) {
        const bool sel = area == 3 && pos == i;
        if (sel) pill_select(c, btn_x[i], 5.0f, btn_w[i], 12.0f, 6.0f, 0.7f);
        else rect(c, btn_x[i], 5.0f, btn_w[i], 12.0f, col::rgba(255, 255, 255, 16), 6.0f);
        icon(c, i ? Icon::Exit : Icon::Party, btn_x[i] + 7.5f, 11.0f, 6.5f, sel ? col::kWhite : col::kTextDim);
        text(c, F::Bold, 7.6f, btn_x[i] + 12.5f, 7.2f, sel ? col::kWhite : col::kTextDim, kButtons[i]);
        target(btn_x[i], 5.0f, btn_w[i], 12.0f, kHdHitBoxCell, 3 * 64 + i);
    }

    // box panel
    const float px = 112.0f, py = 22.0f, pw = c.vw - px - 10.0f, ph = 124.0f;
    if (g_st.box != box) {
        g_st.box_dir = g_st.box < 0 ? 0 : (((box - g_st.box + 14) % 14) <= 7 ? 1 : -1);
        g_st.box = box;
        g_st.box_since = c.t;
    }
    const float bk = std::clamp(static_cast<float>((c.t - g_st.box_since) / 0.18), 0.0f, 1.0f);
    const float slide = (1.0f - (1.0f - bk) * (1.0f - bk)) ;
    panel(c, px, py, pw, ph, 7.0f, col::rgba(17, 22, 35, 238));
    // header
    const float hh = 15.0f;
    const bool title_sel = area == 2;
    grad_h(c, px + 3.0f, py + 3.0f, pw - 6.0f, hh, col::alpha(wc, title_sel ? 0.75f : 0.45f), col::alpha(wc, 0.12f), 5.0f);
    if (title_sel) rect_line(c, px + 3.0f, py + 3.0f, pw - 6.0f, hh, col::alpha(col::kWhite, 0.8f), 5.0f, 0.6f);
    std::string bname = decode_gba(m.ptr(kPokemonStorage + 0x8344 + box * 9, 9), 9, 0);
    if (modern_text()) bname = modern_case(bname);
    if (bname.size() > 3 && bname.compare(0, 3, "BOX") == 0 && bname[3] >= '0' && bname[3] <= '9')
        bname = "Box " + bname.substr(3);
    int filled = 0;
    for (int i = 0; i < 30; ++i) filled += (m.u8(kPokemonStorage + 4 + (box * 30 + i) * 80 + 0x13) & 2) ? 1 : 0;
    text(c, F::Bold, 8.8f, px + pw * 0.5f, py + 3.0f + (hh - 8.8f) * 0.5f, col::kWhite, bname, kCenter);
    char cnt[16];
    std::snprintf(cnt, sizeof cnt, "%d/30", filled);
    text(c, F::Medium, 6.4f, px + pw - 22.0f, py + 3.0f + (hh - 6.4f) * 0.5f, col::alpha(col::kWhite, 0.75f), cnt, kRight);
    // arrows
    for (int d = 0; d < 2; ++d) {
        const float ax = d ? px + pw - 13.0f : px + 6.0f, ay = py + 3.0f + hh * 0.5f;
        if (d) tri(c, ax + 6.0f, ay, ax + 1.5f, ay - 4.0f, ax + 1.5f, ay + 4.0f, col::kWhite);
        else tri(c, ax + 1.0f, ay, ax + 5.5f, ay - 4.0f, ax + 5.5f, ay + 4.0f, col::kWhite);
        target(ax - 3.0f, py + 2.0f, 12.0f, hh + 2.0f, kHdHitBoxScroll, d);
    }
    target(px + 16.0f, py + 2.0f, pw - 32.0f, hh + 2.0f, kHdHitBoxCell, 2 * 64);
    // grid
    const float gx = px + 4.0f, gy = py + hh + 6.0f, gw = pw - 8.0f, gh = ph - hh - 9.0f;
    const float cw = gw / 6.0f, ch = gh / 5.0f;
    const float off = g_st.box_dir * (1.0f - slide) * 24.0f;
    const float galpha = 0.35f + 0.65f * slide;
    // cursor highlight (animated): glow under the icons, outline over them
    float tcx = -1, tcy = -1;
    if (area == 0 && pos >= 0 && pos < 30) { tcx = gx + (pos % 6) * cw; tcy = gy + (pos / 6) * ch; }
    if (tcx >= 0) {
        if (g_st.cx < 0) { g_st.cx = tcx; g_st.cy = tcy; }
        g_st.cx = tcx + (g_st.cx - tcx) * std::exp(-c.dt * 28.0f);
        g_st.cy = tcy + (g_st.cy - tcy) * std::exp(-c.dt * 28.0f);
        for (int gl = 3; gl >= 1; --gl)
            rect(c, g_st.cx + 0.5f - gl, g_st.cy + 0.5f - gl, cw - 1.0f + 2 * gl, ch - 1.0f + 2 * gl, col::alpha(col::kAccent, 0.07f), 4.5f + gl);
        rect(c, g_st.cx + 0.5f, g_st.cy + 0.5f, cw - 1.0f, ch - 1.0f, col::alpha(col::kAccent, 0.22f), 4.5f);
    }
    push_clip(c, gx, gy - 1.0f, gw, gh + 2.0f);
    for (int i = 0; i < 30; ++i) {
        const float x = gx + (i % 6) * cw + off, y = gy + (i / 6) * ch;
        const bool sel = area == 0 && pos == i;
        if (!sel) rect(c, x + 1.0f, y + 1.0f, cw - 2.0f, ch - 2.0f, col::rgba(255, 255, 255, 7), 4.0f);
        const MonView mv = read_mon(m, kPokemonStorage + 4 + (box * 30 + i) * 80, false);
        if (mv.valid) {
            const bool lift = sel && !carrying;
            const Tex ic = tex_mon_icon(m, mv.egg ? 412 : mv.species, lift && std::fmod(c.t, 0.5) < 0.25 ? 1 : 0);
            const float is = std::min(cw, ch) * 1.12f;
            image(c, ic, x + (cw - is) * 0.5f, y + (ch - is) * 0.5f - 1.5f - (lift ? 1.0f : 0.0f), is, is,
                  col::alpha(col::kWhite, galpha));
            if (item_mode && mv.item) {
                circle(c, x + cw - 5.0f, y + ch - 5.0f, 2.6f, col::kGold, 12);
                circle(c, x + cw - 5.0f, y + ch - 5.0f, 1.1f, col::rgba(120, 80, 10), 8);
            }
        }
        target(x, y, cw, ch, kHdHitBoxCell, i);
    }
    pop_clip(c);
    if (tcx >= 0) rect_line(c, g_st.cx + 0.5f, g_st.cy + 0.5f, cw - 1.0f, ch - 1.0f, col::kAccent2, 4.5f, 0.9f);
    // party panel
    const int pcount = std::clamp<int>(m.u8(kPlayerPartyCnt), 0, 6);
    const bool party_open = area == 1 || option == 1;
    g_st.party_k += (party_open ? 1.0f : -1.0f) * c.dt * 7.0f;
    g_st.party_k = std::clamp(g_st.party_k, 0.0f, 1.0f);
    const float lx = 10.0f, ly = 22.0f, lw = 96.0f, lh = 124.0f;
    // carried / selected Pokémon card
    MonView shown;
    if (carrying) shown = read_mon(m, st + kStorageMovingMon, true);
    else if (area == 0 && pos >= 0 && pos < 30) shown = read_mon(m, kPokemonStorage + 4 + (box * 30 + pos) * 80, false);
    else if (area == 1 && pos >= 0 && pos < pcount) shown = read_mon(m, kPlayerParty + pos * 100, true);
    if (g_st.party_k < 1.0f) {
        if (!carrying && (area == 2 || area == 3)) {
            // cursor on the box title / top buttons: say what they do
            panel(c, lx, ly, lw, lh, 6.0f, col::rgba(17, 22, 35, 235));
            const float cx = lx + lw * 0.5f;
            if (area == 2) {
                for (int g = 4; g >= 1; --g) circle(c, cx, ly + 30.0f, 10.0f + g * 4.0f, col::alpha(wc, 0.06f), 32);
                rect(c, cx - 13.0f, ly + 18.0f, 26.0f, 22.0f, col::alpha(wc, 0.8f), 4.0f);
                for (int r = 0; r < 2; ++r)
                    for (int q = 0; q < 3; ++q)
                        rect(c, cx - 10.0f + q * 7.0f, ly + 21.0f + r * 8.0f, 5.5f, 6.0f, col::rgba(255, 255, 255, 70), 1.5f);
                text(c, F::Bold, 10.0f, cx, ly + 50.0f, col::kText, bname, kCenter, lw - 10.0f);
                char pc[32];
                std::snprintf(pc, sizeof pc, "%d of 30 spaces used", filled);
                text(c, F::Medium, 6.8f, cx, ly + 62.0f, col::kTextDim, pc, kCenter);
                wrap_text(c, F::Medium, 6.8f, lx + 8.0f, ly + 80.0f, lw - 16.0f, 8.6f, col::kText,
                          "Select for Jump, Wallpaper and Name. Use the arrows to change box.", 4);
            } else {
                icon(c, pos ? Icon::Exit : Icon::Party, cx, ly + 32.0f, 20.0f, col::kTextDim);
                text(c, F::Bold, 10.0f, cx, ly + 50.0f, col::kText, pos ? "Close" : "Party", kCenter);
                wrap_text(c, F::Medium, 6.8f, lx + 8.0f, ly + 66.0f, lw - 16.0f, 8.6f, col::kText,
                          pos ? "Leave the PC." : "Show the Pok\xc3\xa9mon in your party.", 4);
            }
        } else {
            mon_card(c, shown, lx, ly, lw, lh, item_mode, moving_item);
        }
    }
    if (g_st.party_k > 0.0f) {
        const float e = 1.0f - (1.0f - g_st.party_k) * (1.0f - g_st.party_k);
        const float x = lx - (1.0f - e) * (lw + 12.0f);
        panel(c, x, ly, lw, lh, 6.0f, col::rgba(22, 28, 44, 250));
        text(c, F::Bold, 7.6f, x + 7.0f, ly + 3.5f, col::kText, "Party");
        const float row = (lh - 16.0f) / 7.0f;
        for (int i = 0; i < 7; ++i) {
            const float ry = ly + 13.0f + i * row;
            const bool sel = area == 1 && pos == i;
            if (sel) pill_select(c, x + 3.0f, ry, lw - 6.0f, row - 1.0f, (row - 1.0f) * 0.4f, 0.6f);
            if (i == 6) {
                icon(c, Icon::Back, x + 11.0f, ry + row * 0.5f, 6.0f, sel ? col::kWhite : col::kTextDim);
                text(c, F::Bold, 7.4f, x + 20.0f, ry + (row - 7.4f) * 0.5f, sel ? col::kWhite : col::kTextDim, "Back");
            } else if (i < pcount && read_mon(m, kPlayerParty + i * 100, true).level > 0) {
                const MonView mv = read_mon(m, kPlayerParty + i * 100, true);
                image(c, tex_mon_icon(m, mv.egg ? 412 : mv.species, sel && std::fmod(c.t, 0.5) < 0.25 ? 1 : 0),
                      x + 2.0f, ry + row * 0.5f - 10.0f, 20.0f, 20.0f);
                text(c, sel ? F::Bold : F::Medium, 7.2f, x + 22.0f, ry + 0.6f, sel ? col::kWhite : col::kText,
                     mv.egg ? std::string("Egg") : mv.nickname, kLeft, lw - 26.0f);
                if (!mv.egg) {
                    char lv[12];
                    std::snprintf(lv, sizeof lv, "Lv %d", mv.level);
                    text(c, F::Medium, 5.8f, x + 22.0f, ry + row - 7.2f, sel ? col::alpha(col::kWhite, 0.85f) : col::kTextDim, lv);
                    if (item_mode && mv.item) circle(c, x + lw - 9.0f, ry + row * 0.5f, 2.4f, col::kGold, 12);
                }
            } else {
                text(c, F::Medium, 6.6f, x + 22.0f, ry + (row - 6.6f) * 0.5f, col::kTextFaint, "\xe2\x80\x94");
            }
            if (party_open) target(x, ry, lw, row, kHdHitBoxCell, 1 * 64 + i);
        }
    }
    // carried Pokémon floats over the cursor
    if (carrying && shown.valid) {
        float hx = g_st.cx, hy = g_st.cy;
        if (area == 1) { hx = lx + 2.0f; hy = ly + 13.0f + pos * ((lh - 16.0f) / 7.0f) - 6.0f; }
        else if (area == 2) { hx = px + pw * 0.5f - cw * 0.5f; hy = py + 2.0f; }
        else if (area == 3) { hx = btn_x[std::clamp(pos, 0, 1)]; hy = 10.0f; }
        const float bob = std::sin(static_cast<float>(c.t) * 6.0f) * 1.2f;
        c.dl->AddCircleFilled(c.P(hx + cw * 0.5f + 3.0f, hy + ch - 3.0f), cw * 0.3f * c.s, col::rgba(0, 0, 0, 110), 20);
        const float is = std::min(cw, ch) * 1.3f;
        image(c, tex_mon_icon(m, shown.egg ? 412 : shown.species, std::fmod(c.t, 0.4) < 0.2 ? 1 : 0),
              hx + (cw - is) * 0.5f + 3.0f, hy + (ch - is) * 0.5f - 11.0f + bob, is, is);
    }
    if (item_mode && moving_item) {
        const float hx = area == 1 ? lx + lw - 16.0f : g_st.cx + cw - 10.0f;
        const float hy = area == 1 ? ly + 13.0f + pos * ((lh - 16.0f) / 7.0f) : g_st.cy - 6.0f;
        image(c, tex_item_icon(m, moving_item), hx, hy + std::sin(static_cast<float>(c.t) * 6.0f), 14.0f, 14.0f);
    }

    // game messages + context menu
    std::string msg;
    for (int id = 0; id < 32; ++id) {
        if (s.menu && id == s.menu_window) continue;
        if (!window_shown(m, id)) continue;
        const std::string t = window_message(id);
        if (t.size() > msg.size()) msg = t;
    }
    if (!msg.empty()) {
        message_bar(c, 10.0f, 147.5f, c.vw - 20.0f, 11.0f, msg, false);
    } else {
        if (item_mode) hints(c, c.vw - 10.0f, 150.0f, {{0, "Item"}, {1, "Back"}, {5, "Box", 4}});
        else hints(c, c.vw - 10.0f, 150.0f, {{0, "Select"}, {2, "Quick move"}, {1, "Back"}});
    }
    if (s.menu) context_menu(c, s, std::min(c.vw - 92.0f, (tcx >= 0 ? tcx + cw + 4.0f : px + pw - 88.0f)), 146.0f, 82.0f);
}


// ── TM Case / Berry Pouch: a pocket list with a detail panel ─────────────
constexpr std::uint32_t kCB2_TMCase       = 0x08131864;  // tm_case.c CB2_Idle
constexpr std::uint32_t kTMCaseStatic     = 0x0203B10C;  // sTMCaseStaticResources
constexpr std::uint32_t kCB2_BerryPouch   = 0x0813CE00;  // CB2_BerryPouchIdle
constexpr std::uint32_t kBerryPouchStatic = 0x0203F370;  // sStaticCnt
constexpr std::uint32_t kBagPockets       = 0x0203988C;
constexpr std::uint32_t kBattleMoves      = 0x08250C04;

struct PocketAnim { float hl = -1; std::uint32_t screen = 0; } g_pocket;

int tm_move(const Mem& m, int item) {
    const int i = item - 289;  // ITEM_TM01
    return (i >= 0 && i < 58) ? m.u16(0x0845A80C + i * 2) : 0;  // sTMHMMoves
}

void draw_pocket_screen(Ctx& c, bool tm) {
    const Mem& m = c.m;
    const Scene& s = *c.sc;
    const ImU32 accent = tm ? col::rgba(96, 165, 250) : col::rgba(236, 120, 150);
    screen_bg(c, accent);
    title(c, tm ? "TM Case" : "Berry Pouch");
    const int pocket = tm ? 3 : 4;
    const std::uint32_t slots = m.u32(kBagPockets + pocket * 8);
    const int cap = m.u8(kBagPockets + pocket * 8 + 4);
    const std::uint16_t key = static_cast<std::uint16_t>(m.u32(m.u32(g::SaveBlock2Ptr) + 0xF20) & 0xFFFF);
    struct Entry { int item; int qty; };
    std::vector<Entry> items;
    for (int i = 0; i < cap && i < 64; ++i) {
        const int id = m.u16(slots + i * 4);
        if (id) items.push_back({id, static_cast<int>(m.u16(slots + i * 4 + 2) ^ key)});
    }
    const int total = static_cast<int>(items.size()) + 1;
    const std::uint32_t st = tm ? kTMCaseStatic : kBerryPouchStatic;
    const int rows = std::max(1, s.list ? s.list_max : std::min(total, tm ? 5 : 7));
    const int scroll = s.list ? s.list_scroll : m.u16(st + 10);
    const int sel = s.list ? s.list_scroll + s.list_sel : scroll + m.u16(st + 8);
    const float lx = 10.0f, ly = 22.0f, lw = std::min(160.0f, c.vw - 118.0f), lh = 122.0f;
    panel(c, lx, ly, lw, lh, 6.0f, col::rgba(17, 22, 35, 235));
    const float row = (lh - 6.0f) / rows;
    const float hl_t = ly + 3.0f + (sel - scroll) * row;
    if (g_pocket.screen != (tm ? 1u : 2u)) { g_pocket.screen = tm ? 1u : 2u; g_pocket.hl = -1; }
    if (g_pocket.hl < 0) g_pocket.hl = hl_t;
    g_pocket.hl = hl_t + (g_pocket.hl - hl_t) * std::exp(-c.dt * 24.0f);
    pill_select(c, lx + 3.0f, g_pocket.hl, lw - 6.0f, row - 0.5f, std::min(6.0f, row * 0.4f), s.list ? 1.0f : 0.3f);
    static ScrollAnim g_pocket_scroll;
    const float sp_ = scroll_anim(g_pocket_scroll, tm ? 0x301u : 0x302u, scroll, c.dt);
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
            text(c, is_sel ? F::Bold : F::Medium, 8.6f, lx + 24.0f, ry + (row - 8.6f) * 0.5f, fg, "Close");
        } else {
            const Entry& e = items[static_cast<std::size_t>(idx)];
            if (tm) {
                const int mv = tm_move(m, e.item);
                const bool hm = e.item >= 339;
                char no[8];
                std::snprintf(no, sizeof no, "%s%02d", hm ? "HM" : "TM", hm ? e.item - 338 : e.item - 288);
                const ImU32 tc = col::type_color(move_type(m, mv));
                rect(c, lx + 6.0f, ry + (row - 10.0f) * 0.5f, 22.0f, 10.0f, tc, 5.0f);
                text(c, F::Bold, 6.6f, lx + 17.0f, ry + (row - 6.6f) * 0.5f, col::kWhite, no, kCenter, 0, false);
                text(c, is_sel ? F::Bold : F::Medium, 8.6f, lx + 32.0f, ry + (row - 8.6f) * 0.5f, fg, move_name(m, mv), kLeft, lw - 62.0f);
                if (!hm) {
                    char q[12];
                    std::snprintf(q, sizeof q, "\xc3\x97%d", e.qty);
                    text(c, F::Bold, 7.4f, lx + lw - 8.0f, ry + (row - 7.4f) * 0.5f, is_sel ? col::kWhite : col::kTextDim, q, kRight);
                }
            } else {
                image(c, tex_item_icon(m, e.item), lx + 5.0f, ry + (row - 14.0f) * 0.5f, 14.0f, 14.0f);
                text(c, is_sel ? F::Bold : F::Medium, 8.6f, lx + 24.0f, ry + (row - 8.6f) * 0.5f, fg, item_name(m, e.item), kLeft, lw - 58.0f);
                char q[12];
                std::snprintf(q, sizeof q, "\xc3\x97%d", e.qty);
                text(c, F::Bold, 7.4f, lx + lw - 8.0f, ry + (row - 7.4f) * 0.5f, is_sel ? col::kWhite : col::kTextDim, q, kRight);
            }
        }
        if (r >= 0 && r < rows) target(lx, ly + 3.0f + r * row, lw, row, kHdHitList, idx);
    }
    pop_clip(c);
    if (total > rows) {
        const float th = lh - 10.0f, frac = static_cast<float>(rows) / total;
        const float pos = static_cast<float>(scroll) / std::max(1, total - rows);
        rect(c, lx + lw + 2.0f, ly + 5.0f, 1.6f, th, col::rgba(255, 255, 255, 18), 0.8f);
        rect(c, lx + lw + 2.0f, ly + 5.0f + (th - th * frac) * pos, 1.6f, th * frac, accent, 0.8f);
    }
    // detail panel
    const float dx = lx + lw + 8.0f, dw = c.vw - dx - 10.0f;
    panel(c, dx, ly, dw, lh, 6.0f, col::rgba(17, 22, 35, 235));
    if (sel >= 0 && sel < total - 1) {
        const Entry& e = items[static_cast<std::size_t>(sel)];
        if (tm) {
            const int mv = tm_move(m, e.item);
            const int ty = move_type(m, mv);
            const ImU32 tc = col::type_color(ty);
            for (int g = 4; g >= 1; --g) circle(c, dx + dw * 0.5f, ly + 22.0f, 10.0f + g * 4.0f, col::alpha(tc, 0.05f), 32);
            // disc
            circle(c, dx + dw * 0.5f, ly + 22.0f, 14.0f, col::mix(tc, col::kBlack, 0.25f), 40);
            circle(c, dx + dw * 0.5f, ly + 22.0f, 13.0f, tc, 40);
            ring(c, dx + dw * 0.5f, ly + 22.0f, 9.0f, col::alpha(col::kWhite, 0.25f), 0.6f);
            circle(c, dx + dw * 0.5f, ly + 22.0f, 3.2f, col::rgba(20, 24, 36), 20);
            text(c, F::Bold, 9.6f, dx + dw * 0.5f, ly + 40.0f, col::kText, move_name(m, mv), kCenter, dw - 10.0f);
            float bw = 0;
            type_badge(c, ty, 0, -100, 7.6f, &bw);
            type_badge(c, ty, dx + (dw - bw) * 0.5f, ly + 52.0f, 7.6f);
            const int power = move_power(m, mv), acc = move_accuracy(m, mv), pp = move_pp(m, mv);
            const char* kL[3] = {"Power", "Accuracy", "PP"};
            const std::string kV[3] = {power > 1 ? std::to_string(power) : "\xe2\x80\x94",
                                       acc ? std::to_string(acc) : "\xe2\x80\x94", std::to_string(pp)};
            for (int k = 0; k < 3; ++k) {
                const float cx = dx + dw * (k + 0.5f) / 3.0f;
                text(c, F::Medium, 6.0f, cx, ly + 63.0f, col::kTextDim, kL[k], kCenter);
                text(c, F::Bold, 8.6f, cx, ly + 70.0f, col::kText, kV[k], kCenter);
            }
            if (!s.menu)
                wrap_text(c, F::Medium, 7.0f, dx + 7.0f, ly + 84.0f, dw - 14.0f, 8.4f, col::kText,
                          rom_string(m, m.u32(0x084886E8 + (mv - 1) * 4), 120, true), 4);
        } else {
            for (int g = 4; g >= 1; --g) circle(c, dx + dw * 0.5f, ly + 26.0f, 10.0f + g * 4.0f, col::alpha(accent, 0.04f), 32);
            image(c, tex_item_icon(m, e.item), dx + dw * 0.5f - 18.0f, ly + 8.0f, 36.0f, 36.0f);
            text(c, F::Bold, 9.6f, dx + dw * 0.5f, ly + 46.0f, col::kText, item_name(m, e.item), kCenter, dw - 10.0f);
            char q[24];
            std::snprintf(q, sizeof q, "In pouch: %d", e.qty);
            text(c, F::Medium, 6.8f, dx + dw * 0.5f, ly + 57.0f, col::kTextDim, q, kCenter);
            if (!s.menu)
                wrap_text(c, F::Medium, 7.4f, dx + 7.0f, ly + 68.0f, dw - 14.0f, 9.0f, col::kText,
                          rom_string(m, m.u32(kItems + e.item * 44 + 0x14), 200, true), 5);
        }
    } else {
        icon(c, Icon::Back, dx + dw * 0.5f, ly + 30.0f, 22.0f, col::kTextDim);
        text(c, F::Bold, 9.0f, dx + dw * 0.5f, ly + 50.0f, col::kText, "Close", kCenter);
    }
    // messages / prompts the game prints (not the list or its description/title windows)
    std::string msg, qty;
    for (int id = tm ? 6 : 2; id < 32; ++id) {
        if (s.menu && id == s.menu_window) continue;
        if (s.list && id == s.list_window) continue;
        if (!window_shown(m, id)) continue;
        const std::string t = window_message(id);
        if (is_quantity_text(t)) { qty = t; continue; }
        const Rect r = window_rect(m, id);
        if (r.y < 96) continue;
        if (t.size() > msg.size()) msg = t;
    }
    if (!qty.empty()) quantity_stepper(c, dx + 6.0f, ly + lh - 26.0f, dw - 12.0f, qty);
    if (!msg.empty()) message_bar(c, 10.0f, 147.0f, c.vw - 20.0f, 11.0f, msg, false);
    else hints(c, c.vw - 10.0f, 150.0f, {{6, "Choose", 7}, {0, "Select"}, {1, "Close"}});
    if (s.menu) context_menu(c, s, dx + 4.0f, ly + lh - 2.0f, dw - 8.0f);
}


// ── Pokédex ──────────────────────────────────────────────────────────────
constexpr std::uint32_t kCB2_Pokedex       = 0x081024D4;
constexpr std::uint32_t kDexDataPtr        = 0x0203ACF0;  // sPokedexScreenData
constexpr std::uint32_t kTask_DexTop       = 0x0810287C;
constexpr std::uint32_t kTask_DexNumerical = 0x08102F80;
constexpr std::uint32_t kTask_DexCharOrder = 0x08103238;
constexpr std::uint32_t kTask_DexMonPage   = 0x081042EC;
constexpr std::uint32_t kDexEntries        = 0x0844E850;
constexpr std::uint32_t kNatDexTable       = 0x08251FEE;

std::uint32_t dex_task(const Mem& m) {
    for (int i = 0; i < 16; ++i) {
        const std::uint32_t t = g::Tasks + i * 0x28;
        if (!m.u8(t + 4)) continue;
        const std::uint32_t f = m.func(t);
        if (f == kTask_DexTop || f == kTask_DexNumerical || f == kTask_DexCharOrder || f == kTask_DexMonPage ||
            (f >= 0x08103A00 && f < 0x08104300))
            return f;
    }
    return 0;
}

int dex_number(const Mem& m, int species) { return species > 0 ? m.u16(kNatDexTable + (species - 1) * 2) : 0; }

bool dex_flag(const Mem& m, int dexnum, bool owned) {
    if (dexnum <= 0 || dexnum > 386) return false;
    const std::uint32_t sb2 = m.u32(g::SaveBlock2Ptr);
    return (m.u8(sb2 + 0x18 + (owned ? 0x10 : 0x44) + (dexnum - 1) / 8) >> ((dexnum - 1) % 8)) & 1;
}

// List items of a ListMenu task: {label*, index} pairs.
struct LItem { std::uint32_t label; std::int32_t index; };
std::vector<LItem> list_items(const Mem& m, int task) {
    std::vector<LItem> out;
    if (task < 0 || task >= 16) return out;
    const std::uint32_t t = g::Tasks + task * 0x28;
    const std::uint32_t items = m.u32(t + 8);
    const int total = m.u16(t + 0x14);
    for (int i = 0; i < total && i < 512; ++i)
        out.push_back({m.u32(items + i * 8), static_cast<std::int32_t>(m.u32(items + i * 8 + 4))});
    return out;
}

void dex_progress(Ctx& c, float x, float y, float w, const char* label, int seen, int owned, int total, ImU32 color) {
    text(c, F::Bold, 7.6f, x, y, col::kText, label);
    char b[32];
    std::snprintf(b, sizeof b, "%d / %d", owned, total);
    text(c, F::Bold, 7.6f, x + w, y, col::kText, b, kRight);
    bar(c, x, y + 10.0f, w, 3.4f, static_cast<float>(seen) / total, col::alpha(color, 0.35f), col::rgba(255, 255, 255, 14));
    bar(c, x, y + 10.0f, w, 3.4f, static_cast<float>(owned) / total, color, col::rgba(0, 0, 0, 0));
    std::snprintf(b, sizeof b, "Seen %d", seen);
    text(c, F::Medium, 6.0f, x, y + 15.5f, col::kTextDim, b);
}

struct DexAnim { float hl = -1; std::uint32_t screen = 0; } g_dex;

void dex_list_frame(Ctx& c, float target_y, std::uint32_t screen) {
    if (g_dex.screen != screen) { g_dex.screen = screen; g_dex.hl = -1; }
    if (g_dex.hl < 0) g_dex.hl = target_y;
    g_dex.hl = target_y + (g_dex.hl - target_y) * std::exp(-c.dt * 24.0f);
}

void draw_dex_top(Ctx& c, std::uint32_t dd) {
    const Mem& m = c.m;
    const Scene& s = *c.sc;
    screen_bg(c, col::kAccent);
    title(c, "Pok\xc3\xa9" "dex");
    if (!s.list) return;
    const auto items = list_items(m, s.list_task);
    const int rows = std::max(1, s.list_max);
    const int scroll = s.list_scroll, sel = s.list_scroll + s.list_sel;
    const float lx = 10.0f, ly = 22.0f, lw = std::min(150.0f, c.vw - 128.0f), lh = 124.0f;
    panel(c, lx, ly, lw, lh, 6.0f, col::rgba(17, 22, 35, 235));
    const float row = (lh - 6.0f) / rows;
    dex_list_frame(c, ly + 3.0f + (sel - scroll) * row, 1);
    pill_select(c, lx + 3.0f, g_dex.hl, lw - 6.0f, row - 0.5f, std::min(6.0f, row * 0.4f));
    const std::uint32_t unlocked = m.u32(dd + 0x08);
    for (int r = 0; r < rows; ++r) {
        const int idx = scroll + r;
        if (idx >= static_cast<int>(items.size())) break;
        const LItem& it = items[static_cast<std::size_t>(idx)];
        const float ry = ly + 3.0f + r * row;
        const std::string label = rom_string(m, it.label, 40, true);
        if (it.index == -3) {
            text(c, F::Bold, 6.4f, lx + 8.0f, ry + (row - 6.4f) * 0.5f + 1.0f, col::kAccent2, label);
            line(c, lx + 12.0f + text_w(F::Bold, 6.4f, label), ry + row * 0.5f + 1.0f, lx + lw - 8.0f, ry + row * 0.5f + 1.0f, col::kStroke, 0.4f);
            continue;
        }
        const bool is_sel = idx == sel;
        const bool locked = it.index >= 0 && it.index < 9 && !((unlocked >> it.index) & 1);
        const ImU32 fg = is_sel ? col::kWhite : locked ? col::kTextFaint : col::kText;
        text(c, is_sel ? F::Bold : F::Medium, 8.2f, lx + 14.0f, ry + (row - 8.2f) * 0.5f, fg, label, kLeft, lw - 20.0f);
        target(lx, ry, lw, row, kHdHitList, idx);
    }
    // right: progress
    const float dx = lx + lw + 8.0f, dw = c.vw - dx - 10.0f;
    panel(c, dx, ly, dw, lh, 6.0f, col::rgba(17, 22, 35, 235));
    for (int g = 5; g >= 1; --g) circle(c, dx + dw * 0.5f, ly + 30.0f, 8.0f + g * 5.0f, col::alpha(col::kAccent, 0.035f), 36);
    icon(c, Icon::Pokedex, dx + dw * 0.5f, ly + 30.0f, 26.0f, col::kText);
    const bool national = m.u16(dd + 0x6A) > 0 || m.u16(dd + 0x6C) > 0;
    dex_progress(c, dx + 8.0f, ly + 56.0f, dw - 16.0f, "Kanto", m.u16(dd + 0x66), m.u16(dd + 0x68), 151, col::kAccent2);
    if (national)
        dex_progress(c, dx + 8.0f, ly + 84.0f, dw - 16.0f, "National", m.u16(dd + 0x6A), m.u16(dd + 0x6C), 386, col::kGold);
    hints(c, c.vw - 10.0f, 150.0f, {{6, "Choose", 7}, {0, "Open"}, {1, "Close"}});
}

void draw_dex_list(Ctx& c, std::uint32_t dd) {
    const Mem& m = c.m;
    const Scene& s = *c.sc;
    screen_bg(c, col::kAccent);
    static const char* const kOrders[] = {"Kanto", "A to Z", "By Type", "Lightest", "Smallest", "National"};
    const int order = m.u8(dd + 0x42);
    title(c, "Pok\xc3\xa9" "dex", order < 6 ? kOrders[order] : "");
    if (!s.list) return;
    const auto items = list_items(m, s.list_task);
    const int rows = std::max(1, s.list_max);
    const int scroll = s.list_scroll, sel = s.list_scroll + s.list_sel;
    const float lx = 10.0f, ly = 22.0f, lw = std::min(150.0f, c.vw - 128.0f), lh = 124.0f;
    panel(c, lx, ly, lw, lh, 6.0f, col::rgba(17, 22, 35, 235));
    const float row = (lh - 6.0f) / rows;
    dex_list_frame(c, ly + 3.0f + (sel - scroll) * row, 2);
    pill_select(c, lx + 3.0f, g_dex.hl, lw - 6.0f, row - 0.5f, std::min(6.0f, row * 0.4f));
    static ScrollAnim g_dexlist_scroll;
    const float sp_ = scroll_anim(g_dexlist_scroll, 0x400u + static_cast<std::uint32_t>(order), scroll, c.dt);
    push_clip(c, lx, ly + 2.0f, lw, lh - 4.0f);
    for (int r = -1; r <= rows; ++r) {
        const int idx = scroll + r;
        if (idx < 0) continue;
        if (idx >= static_cast<int>(items.size())) break;
        const LItem& it = items[static_cast<std::size_t>(idx)];
        const float ry = ly + 3.0f + (static_cast<float>(idx) - sp_) * row;
        const bool is_sel = idx == sel;
        const int species = it.index & 0xFFFF;
        const bool seen = (it.index >> 16) & 1, caught = (it.index >> 17) & 1;
        const ImU32 fg = is_sel ? col::kWhite : seen ? col::kText : col::kTextFaint;
        char no[8];
        std::snprintf(no, sizeof no, "%03d", dex_number(m, species));
        text(c, F::Bold, 6.8f, lx + 8.0f, ry + (row - 6.8f) * 0.5f, is_sel ? col::alpha(col::kWhite, 0.85f) : col::kTextDim, no);
        if (seen) image(c, tex_mon_icon(m, species, is_sel && std::fmod(c.t, 0.5) < 0.25 ? 1 : 0), lx + 22.0f, ry + row * 0.5f - 10.5f, 20.0f, 20.0f);
        text(c, is_sel ? F::Bold : F::Medium, 8.2f, lx + 44.0f, ry + (row - 8.2f) * 0.5f, fg,
             seen ? species_name(m, species) : std::string("\xe2\x80\x94\xe2\x80\x94\xe2\x80\x94"), kLeft, lw - 60.0f);
        if (caught) icon(c, Icon::Pokeball, lx + lw - 10.0f, ry + row * 0.5f, 6.5f, is_sel ? col::kWhite : col::kAccent2);
        if (r >= 0 && r < rows) target(lx, ly + 3.0f + r * row, lw, row, kHdHitList, idx);
    }
    pop_clip(c);
    if (static_cast<int>(items.size()) > rows) {
        const float th = lh - 10.0f, frac = static_cast<float>(rows) / items.size();
        const float pos = static_cast<float>(scroll) / std::max<int>(1, static_cast<int>(items.size()) - rows);
        rect(c, lx + lw + 2.0f, ly + 5.0f, 1.6f, th, col::rgba(255, 255, 255, 18), 0.8f);
        rect(c, lx + lw + 2.0f, ly + 5.0f + (th - th * frac) * pos, 1.6f, std::max(4.0f, th * frac), col::kAccent2, 0.8f);
    }
    // right: preview
    const float dx = lx + lw + 8.0f, dw = c.vw - dx - 10.0f;
    panel(c, dx, ly, dw, lh, 6.0f, col::rgba(17, 22, 35, 235));
    if (sel >= 0 && sel < static_cast<int>(items.size())) {
        const LItem& it = items[static_cast<std::size_t>(sel)];
        const int species = it.index & 0xFFFF;
        const bool seen = (it.index >> 16) & 1, caught = (it.index >> 17) & 1;
        const int dexno = dex_number(m, species);
        const ImU32 t1 = seen ? col::type_color(species_type(m, species, 0)) : col::rgba(80, 90, 110);
        for (int g = 5; g >= 1; --g) circle(c, dx + dw * 0.5f, ly + 36.0f, 12.0f + g * 5.0f, col::alpha(t1, 0.04f), 40);
        if (seen) image(c, tex_mon_front(m, species, false, 0), dx + dw * 0.5f - 32.0f, ly + 4.0f, 64.0f, 64.0f,
                        caught ? col::kWhite : col::rgba(30, 36, 52));
        else text(c, F::Bold, 26.0f, dx + dw * 0.5f, ly + 22.0f, col::kTextFaint, "?", kCenter);
        char hdr[48];
        std::snprintf(hdr, sizeof hdr, "No. %03d", dexno);
        text(c, F::Medium, 6.8f, dx + dw * 0.5f, ly + 70.0f, col::kTextDim, hdr, kCenter);
        text(c, F::Bold, 9.8f, dx + dw * 0.5f, ly + 78.0f, col::kText, seen ? species_name(m, species) : std::string("Unknown"), kCenter, dw - 10.0f);
        if (caught) {
            float w1 = 0, w2 = 0;
            const int ty1 = species_type(m, species, 0), ty2 = species_type(m, species, 1);
            type_badge(c, ty1, 0, -100, 7.6f, &w1);
            if (ty2 != ty1) type_badge(c, ty2, 0, -100, 7.6f, &w2);
            float bx = dx + (dw - (w1 + (ty2 != ty1 ? w2 + 2.0f : 0.0f))) * 0.5f;
            type_badge(c, ty1, bx, ly + 91.0f, 7.6f);
            if (ty2 != ty1) type_badge(c, ty2, bx + w1 + 2.0f, ly + 91.0f, 7.6f);
            const std::uint32_t e = kDexEntries + dexno * 0x24;
            const std::string cat = rom_string(m, e, 12, true);
            char hw[64];
            std::snprintf(hw, sizeof hw, "%s Pok\xc3\xa9mon  \xc2\xb7  %.1f m  \xc2\xb7  %.1f kg", cat.c_str(), m.u16(e + 0x0C) / 10.0, m.u16(e + 0x0E) / 10.0);
            text(c, F::Medium, 6.2f, dx + dw * 0.5f, ly + 104.0f, col::kTextDim, hw, kCenter, dw - 8.0f);
        } else if (seen) {
            text(c, F::Medium, 6.6f, dx + dw * 0.5f, ly + 94.0f, col::kTextFaint, "Seen, not caught", kCenter);
        }
    }
    hints(c, c.vw - 10.0f, 150.0f, {{6, "Choose", 7}, {0, "View"}, {1, "Back"}});
}

void draw_dex_page(Ctx& c, std::uint32_t dd) {
    const Mem& m = c.m;
    const int species = m.u16(dd + 0x5A);
    const int dexno = dex_number(m, species);
    if (dexno <= 0) return;
    const bool caught = dex_flag(m, dexno, true);
    const ImU32 t1 = col::type_color(species_type(m, species, 0));
    screen_bg(c, t1);
    char ttl[48];
    std::snprintf(ttl, sizeof ttl, "No. %03d", dexno);
    title(c, species_name(m, species), ttl);
    const float lw = std::min(118.0f, c.vw * 0.42f);
    grad_h(c, 0, 0, lw + 30.0f, c.vh, col::alpha(col::mix(t1, col::kBlack, 0.55f), 0.45f), col::alpha(col::mix(t1, col::kBlack, 0.55f), 0.0f), 0);
    for (int g = 6; g >= 1; --g) circle(c, lw * 0.5f + 5.0f, 70.0f, 18.0f + g * 7.0f, col::alpha(t1, 0.045f), 48);
    const float bob = std::sin(static_cast<float>(c.t) * 2.0f) * 1.2f;
    image(c, tex_mon_front(m, species, false, 0), lw * 0.5f + 5.0f - 40.0f, 30.0f + bob, 80.0f, 80.0f);
    float w1 = 0, w2 = 0;
    const int ty1 = species_type(m, species, 0), ty2 = species_type(m, species, 1);
    type_badge(c, ty1, 0, -100, 8.4f, &w1);
    if (ty2 != ty1) type_badge(c, ty2, 0, -100, 8.4f, &w2);
    float bx = lw * 0.5f + 5.0f - (w1 + (ty2 != ty1 ? w2 + 2.0f : 0.0f)) * 0.5f;
    type_badge(c, ty1, bx, 116.0f, 8.4f);
    if (ty2 != ty1) type_badge(c, ty2, bx + w1 + 2.0f, 116.0f, 8.4f);
    // info panel
    const float rx = lw + 16.0f, rw = c.vw - rx - 10.0f, py = 24.0f, ph = 122.0f;
    panel(c, rx, py, rw, ph, 6.0f, col::rgba(17, 22, 35, 235));
    const std::uint32_t e = kDexEntries + dexno * 0x24;
    if (caught) {
        const std::string cat = rom_string(m, e, 12, true) + " Pok\xc3\xa9mon";
        text(c, F::Bold, 9.0f, rx + 8.0f, py + 6.0f, col::kText, cat, kLeft, rw - 16.0f);
        struct KV { const char* k; std::string v; };
        char h[24], wt[24];
        std::snprintf(h, sizeof h, "%.1f m", m.u16(e + 0x0C) / 10.0);
        std::snprintf(wt, sizeof wt, "%.1f kg", m.u16(e + 0x0E) / 10.0);
        const KV kv[2] = {{"Height", h}, {"Weight", wt}};
        for (int k = 0; k < 2; ++k) {
            const float kx = rx + 8.0f + k * (rw - 16.0f) * 0.5f;
            rect(c, kx, py + 19.0f, (rw - 16.0f) * 0.5f - 3.0f, 17.0f, col::rgba(255, 255, 255, 10), 4.0f);
            text(c, F::Medium, 6.2f, kx + 5.0f, py + 21.0f, col::kTextDim, kv[k].k);
            text(c, F::Bold, 8.4f, kx + 5.0f, py + 27.5f, col::kText, kv[k].v);
        }
        line(c, rx + 8.0f, py + 41.0f, rx + rw - 8.0f, py + 41.0f, col::kStroke, 0.4f);
        wrap_text(c, F::Medium, 7.6f, rx + 8.0f, py + 45.0f, rw - 16.0f, 9.6f, col::kText,
                  rom_string(m, m.u32(e + 0x10), 400, true), 8);
    } else {
        text(c, F::Medium, 8.0f, rx + 8.0f, py + 10.0f, col::kTextDim, "Catch this Pok\xc3\xa9mon to see its entry.", kLeft, rw - 16.0f);
    }
    hints(c, c.vw - 10.0f, 150.0f, {{6, "Pok\xc3\xa9mon", 7}, {0, "Area"}, {3, "Cry"}, {1, "Back"}});
}


void draw_dex_category(Ctx& c, std::uint32_t dd) {
    const Mem& m = c.m;
    static const char* const kCats[9] = {"Grassland", "Forest", "Water's-edge", "Sea", "Cave", "Mountain",
                                         "Rough-terrain", "Urban", "Rare"};
    const int cat = std::min<int>(m.u8(dd + 0x28), 8);
    const int page = m.u8(dd + 0x2B), first = m.u8(dd + 0x29), last = m.u8(dd + 0x2A);
    const int n = std::clamp<int>(m.u8(dd + 0x2C), 0, 4);
    const int cur = m.u8(dd + 0x2D);
    screen_bg(c, col::kGood);
    char sub[48];
    std::snprintf(sub, sizeof sub, "Page %d / %d", page - first + 1, std::max(1, last - first));
    title(c, std::string(kCats[cat]) + " Pok\xc3\xa9mon", sub);
    const float gap = 5.0f, x0 = 10.0f, y0 = 24.0f, h = 118.0f;
    const float w = (c.vw - 20.0f - gap * 3) / 4.0f;
    for (int i = 0; i < n; ++i) {
        const int species = m.u16(dd + 0x18 + i * 2);
        const int dexno = dex_number(m, species);
        const bool seen = dex_flag(m, dexno, false), caught = dex_flag(m, dexno, true);
        const float x = x0 + i * (w + gap);
        const bool sel = i == cur;
        const ImU32 t1 = seen ? col::type_color(species_type(m, species, 0)) : col::rgba(80, 90, 110);
        if (sel) for (int g = 3; g >= 1; --g) rect(c, x - g, y0 - g, w + 2 * g, h + 2 * g, col::alpha(col::kAccent, 0.08f), 7.0f + g);
        shadow(c, x, y0, w, h, 7.0f, 3.0f, 0.9f);
        grad_v(c, x, y0, w, h, col::mix(t1, col::rgba(20, 25, 38), sel ? 0.55f : 0.78f), col::rgba(14, 18, 28, 245), 7.0f);
        rect_line(c, x, y0, w, h, sel ? col::alpha(col::kWhite, 0.8f) : col::kStroke, 7.0f, sel ? 0.7f : 0.4f);
        const float bob = sel ? std::sin(static_cast<float>(c.t) * 3.0f) * 1.2f : 0.0f;
        if (seen) image(c, tex_mon_front(m, species, false, 0), x + w * 0.5f - 30.0f, y0 + 6.0f + bob, 60.0f, 60.0f,
                        caught ? col::kWhite : col::rgba(30, 36, 52));
        char no[12];
        std::snprintf(no, sizeof no, "No. %03d", dexno);
        text(c, F::Medium, 6.4f, x + w * 0.5f, y0 + 72.0f, col::kTextDim, no, kCenter);
        text(c, F::Bold, 8.6f, x + w * 0.5f, y0 + 80.0f, col::kText, seen ? species_name(m, species) : std::string("???"), kCenter, w - 6.0f);
        if (caught) {
            float w1 = 0, w2 = 0;
            const int ty1 = species_type(m, species, 0), ty2 = species_type(m, species, 1);
            type_badge(c, ty1, 0, -100, 6.8f, &w1);
            if (ty2 != ty1) type_badge(c, ty2, 0, -100, 6.8f, &w2);
            float bx = x + (w - (w1 + (ty2 != ty1 ? w2 + 2.0f : 0.0f))) * 0.5f;
            type_badge(c, ty1, bx, y0 + 93.0f, 6.8f);
            if (ty2 != ty1) type_badge(c, ty2, bx + w1 + 2.0f, y0 + 93.0f, 6.8f);
            icon(c, Icon::Pokeball, x + w - 9.0f, y0 + 9.0f, 7.0f, col::kAccent2);
        }
    }
    hints(c, c.vw - 10.0f, 150.0f, {{5, "Choose", 4}, {0, "View"}, {1, "Back"}});
}

bool draw_pokedex(Ctx& c) {
    const Mem& m = c.m;
    const std::uint32_t dd = m.u32(kDexDataPtr);
    if (!m.valid(dd, 0x70)) return false;
    const std::uint32_t task = dex_task(m);
    const int state = m.u8(dd + 1);
    if (task == kTask_DexTop && state >= 3 && state <= 6) { draw_dex_top(c, dd); return true; }
    if ((task == kTask_DexNumerical || task == kTask_DexCharOrder) && c.sc->list) { draw_dex_list(c, dd); return true; }
    if (task == kTask_DexMonPage && state >= 2 && state <= 6) { draw_dex_page(c, dd); return true; }
    if (task == 0x08103AC8) {  // Task_DexScreen_CategorySubmenu
        if ((state >= 3 && state <= 5) || (state >= 8 && state <= 11) || state == 13 || (state >= 18 && state <= 20)) { draw_dex_category(c, dd); return true; }
        if ((state >= 14 && state <= 17) || state == 24 || state == 25) { draw_dex_page(c, dd); return true; }
    }
    // The Area / Size page keeps the game's own look.
    const bool area = (task == kTask_DexMonPage && state >= 7) || (task == 0x08103AC8 && state >= 21 && state <= 23);
    if (area) return false;
    // Anything else is a transition (lists being rebuilt, page fades): hold
    // the HD backdrop instead of flashing the original screens for a frame.
    screen_bg(c, col::kAccent);
    title(c, "Pok\xc3\xa9" "dex");
    return true;
}


// ── Trainer card ─────────────────────────────────────────────────────────
constexpr std::uint32_t kCB2_TrainerCard = 0x08089070;
constexpr std::uint32_t kTrainerCardPtr  = 0x020397A4;

struct CardFlip { int side = -1; double since = 0; } g_card;

void badge_icon(Ctx& c, int i, float cx, float cy, float r, bool got) {
    static const ImU32 kBadge[8] = {
        col::rgba(150, 150, 160), col::rgba(80, 160, 240), col::rgba(250, 170, 40), col::rgba(120, 200, 110),
        col::rgba(240, 110, 180), col::rgba(240, 200, 60), col::rgba(240, 90, 60), col::rgba(90, 190, 120)};
    if (!got) {
        ring(c, cx, cy, r, col::rgba(255, 255, 255, 30), 0.6f);
        return;
    }
    const ImU32 b = kBadge[i];
    circle(c, cx, cy, r + 0.6f, col::mix(b, col::kBlack, 0.35f), 24);
    c.dl->AddCircleFilled(c.P(cx, cy), r * c.s, b, 24);
    switch (i) {
        case 0: { ImVec2 p[8]; for (int k = 0; k < 8; ++k) { const float a = k * 0.785f; p[k] = c.P(cx + std::cos(a) * r * 0.6f, cy + std::sin(a) * r * 0.6f); } c.dl->AddConvexPolyFilled(p, 8, col::mix(b, col::kWhite, 0.35f)); break; }
        case 1: circle(c, cx, cy + r * 0.15f, r * 0.45f, col::mix(b, col::kWhite, 0.4f), 16); tri(c, cx - r * 0.42f, cy, cx + r * 0.42f, cy, cx, cy - r * 0.65f, col::mix(b, col::kWhite, 0.4f)); break;
        case 2: { ImVec2 p[8]; for (int k = 0; k < 8; ++k) { const float a = k * 0.785f + 0.39f; const float rr = (k & 1) ? r * 0.3f : r * 0.7f; p[k] = c.P(cx + std::cos(a) * rr, cy + std::sin(a) * rr); } c.dl->AddPolyline(p, 8, col::kWhite, ImDrawFlags_Closed, std::max(1.0f, 0.5f * c.s)); break; }
        case 3: for (int k = 0; k < 3; ++k) circle(c, cx + (k - 1) * r * 0.42f, cy, r * 0.28f, (k == 0 ? col::rgba(240, 90, 90) : k == 1 ? col::rgba(250, 220, 80) : col::rgba(90, 160, 250)), 12); break;
        case 4: circle(c, cx - r * 0.25f, cy - r * 0.15f, r * 0.3f, col::kWhite, 12); circle(c, cx + r * 0.25f, cy - r * 0.15f, r * 0.3f, col::kWhite, 12); tri(c, cx - r * 0.52f, cy - r * 0.05f, cx + r * 0.52f, cy - r * 0.05f, cx, cy + r * 0.55f, col::kWhite); break;
        case 5: ring(c, cx, cy, r * 0.5f, col::kWhite, 0.6f); ring(c, cx, cy, r * 0.22f, col::kWhite, 0.6f); break;
        case 6: tri(c, cx - r * 0.5f, cy + r * 0.45f, cx + r * 0.5f, cy + r * 0.45f, cx, cy - r * 0.6f, col::rgba(255, 220, 120)); break;
        default: rect(c, cx - r * 0.45f, cy - r * 0.45f, r * 0.9f, r * 0.9f, col::mix(b, col::kWhite, 0.4f), r * 0.15f); break;
    }
}

void draw_trainer_card(Ctx& c) {
    const Mem& m = c.m;
    const std::uint32_t td = m.u32(kTrainerCardPtr);
    if (!m.valid(td + 0x45C, 0x60)) return;
    const std::uint32_t tc = td + 0x45C;
    const int side = m.u8(td + 8) ? 1 : 0;
    if (g_card.side < 0) g_card.side = side;
    if (side != g_card.side) { g_card.side = side; g_card.since = c.t; }
    const float fk = std::clamp(static_cast<float>((c.t - g_card.since) / 0.28), 0.0f, 1.0f);
    const float squash = std::fabs(std::cos(fk * 3.14159f));     // 1 → 0 → 1
    const int shown_side = fk < 0.5f ? 1 - side : side;
    const int stars = m.u8(tc + 1);
    static const ImU32 kTiers[5] = {col::rgba(70, 90, 140), col::rgba(176, 106, 70), col::rgba(150, 160, 175),
                                    col::rgba(220, 175, 60), col::rgba(110, 200, 220)};
    const ImU32 tier = kTiers[std::clamp(stars, 0, 4)];
    screen_bg(c, tier);
    title(c, "Trainer Card");
    const float cw = std::min(232.0f, c.vw - 30.0f), ch = 122.0f;
    const float cx = (c.vw - cw) * 0.5f, cy = 22.0f;
    const float w = cw * std::max(0.02f, squash);
    const float x = cx + (cw - w) * 0.5f;
    shadow(c, x, cy, w, ch, 8.0f, 5.0f, 1.2f);
    grad_h(c, x, cy, w, ch, col::mix(tier, col::rgba(20, 24, 36), 0.35f), col::mix(tier, col::rgba(10, 12, 20), 0.75f), 8.0f);
    rect_line(c, x, cy, w, ch, col::alpha(col::kWhite, 0.25f), 8.0f, 0.6f);
    if (squash < 0.25f) { hints(c, c.vw - 10.0f, 150.0f, {{0, "Flip"}, {1, "Close"}}); return; }
    push_clip(c, x, cy, w, ch);
    const float ox = x + (w - cw) * 0.5f;  // content drawn at full width, clipped
    // sheen
    for (int k = 0; k < 6; ++k)
        line(c, ox + 120.0f + k * 6.0f, cy, ox + 60.0f + k * 6.0f, cy + ch, col::rgba(255, 255, 255, 6), 2.0f);
    const std::uint32_t sb2 = m.u32(g::SaveBlock2Ptr), sb1 = m.u32(g::SaveBlock1Ptr);
    std::string name = decode_gba(m.ptr(tc + 0x30, 8), 8, 0);
    if (modern_text()) name = modern_case(name);
    char idb[16];
    std::snprintf(idb, sizeof idb, "ID No. %05u", m.u16(tc + 0x0E));
    if (shown_side == 0) {
        text(c, F::Bold, 13.0f, ox + 12.0f, cy + 8.0f, col::kWhite, name);
        text(c, F::Medium, 7.2f, ox + 12.0f, cy + 24.0f, col::alpha(col::kWhite, 0.75f), idb);
        // stars
        for (int k = 0; k < 4; ++k)
            text(c, F::Bold, 9.0f, ox + cw - 62.0f + k * 12.0f, cy + 8.0f, k < stars ? col::kGold : col::rgba(255, 255, 255, 40), "\xe2\x98\x85");
        const int gender = m.u8(tc);
        image(c, tex_trainer_front(m, gender ? 136 : 135), ox + cw - 76.0f, cy + 22.0f, 64.0f, 64.0f);
        struct Row { const char* k; std::string v; };
        const unsigned money = m.u32(tc + 0x24);
        char t[24], dex[16];
        std::snprintf(t, sizeof t, "%u:%02u", m.u16(tc + 0x10), m.u16(tc + 0x12));
        std::snprintf(dex, sizeof dex, "%u", m.u16(tc + 0x0C));
        std::vector<Row> rows = {{"Money", money_str(money)}, {"Time", t}};
        if (m.u8(tc + 2)) rows.push_back({"Pok\xc3\xa9" "dex", dex});
        float ry = cy + 38.0f;
        for (const Row& r : rows) {
            text(c, F::Medium, 7.4f, ox + 12.0f, ry, col::alpha(col::kWhite, 0.7f), r.k);
            text(c, F::Bold, 8.8f, ox + 118.0f, ry - 0.6f, col::kWhite, r.v, kRight);
            line(c, ox + 12.0f, ry + 11.0f, ox + 118.0f, ry + 11.0f, col::rgba(255, 255, 255, 22), 0.4f);
            ry += 14.0f;
        }
        // badges
        rect(c, ox + 8.0f, cy + ch - 26.0f, cw - 16.0f, 20.0f, col::rgba(0, 0, 0, 70), 6.0f);
        for (int b = 0; b < 8; ++b) {
            const bool got = m.u8(td + 0x11 + b) != 0;
            badge_icon(c, b, ox + 22.0f + b * ((cw - 44.0f) / 7.0f), cy + ch - 16.0f, 6.0f, got);
        }
        (void)sb2; (void)sb1;
    } else {
        text(c, F::Bold, 11.0f, ox + 12.0f, cy + 8.0f, col::kWhite, name);
        text(c, F::Medium, 7.2f, ox + cw - 12.0f, cy + 11.0f, col::alpha(col::kWhite, 0.75f), idb, kRight);
        struct Row { const char* k; std::string v; bool show; };
        char hof[32], link[32], trades[16], crush[16], picked[16], jumps[16];
        std::snprintf(hof, sizeof hof, "%u:%02u:%02u", m.u16(tc + 0x06), m.u16(tc + 0x08), m.u16(tc + 0x0A));
        std::snprintf(link, sizeof link, "%u W  /  %u L", m.u16(tc + 0x14), m.u16(tc + 0x16));
        std::snprintf(trades, sizeof trades, "%u", m.u16(tc + 0x20));
        std::snprintf(crush, sizeof crush, "%u", m.u32(tc + 0x3C));
        std::snprintf(picked, sizeof picked, "%u", m.u32(tc + 0x44));
        std::snprintf(jumps, sizeof jumps, "%u", m.u32(tc + 0x48));
        const Row rows[] = {{"Hall of Fame debut", hof, m.u8(td + 0x0B) != 0}, {"Link battles", link, true},
                            {"Link trades", trades, true}, {"Berry Crush points", crush, true},
                            {"Berries picked", picked, true}, {"Pok\xc3\xa9mon Jump record", jumps, true}};
        float ry = cy + 28.0f;
        for (const Row& r : rows) {
            if (!r.show) continue;
            text(c, F::Medium, 7.4f, ox + 12.0f, ry, col::alpha(col::kWhite, 0.72f), r.k);
            text(c, F::Bold, 8.4f, ox + cw - 12.0f, ry - 0.4f, col::kWhite, r.v, kRight);
            line(c, ox + 12.0f, ry + 11.0f, ox + cw - 12.0f, ry + 11.0f, col::rgba(255, 255, 255, 22), 0.4f);
            ry += 14.5f;
        }
    }
    pop_clip(c);
    target(cx, cy, cw, ch, kHdHitKey, 0);
    hints(c, c.vw - 10.0f, 150.0f, {{0, "Flip"}, {1, "Close"}});
}

}  // namespace

bool screens2_draw(Ctx& c) {
    const Scene& s = *c.sc;
    if (s.cb2 == kCB2_Pokedex) return hd_ui_part(kHdPartDex) && draw_pokedex(c);
    if (!hd_ui_part(kHdPartScreens)) return false;
    if (s.cb2 == kCB2_BuyMenu) { draw_mart(c); return true; }
    if (s.cb2 == kCB2_TMCase) { draw_pocket_screen(c, true); return true; }
    if (s.cb2 == kCB2_TrainerCard) { draw_trainer_card(c); return true; }
    if (s.cb2 == kCB2_BerryPouch) { draw_pocket_screen(c, false); return true; }
    if (s.cb2 == kCB2_PokeStorage && storage_task_supported(c.m)) { draw_storage(c); return true; }
    return false;
}

}  // namespace frlg::hd
