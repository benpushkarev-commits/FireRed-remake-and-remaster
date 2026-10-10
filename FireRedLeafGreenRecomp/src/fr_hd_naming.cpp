// fr_hd_naming.cpp — HD naming screen (player / rival / nickname / box name).
//
// The game's own keyboard state drives everything: the page, the cursor
// key and the text typed so far are read from sNamingScreen and redrawn as
// a text field plus an on-screen keyboard. Typing on the PC keyboard and
// clicking keys go through fr_pc_input's naming support.

#include <algorithm>
#include <cmath>
#include <string>

#include "fr_hd_capture.h"
#include "fr_hd_draw.h"
#include "fr_hd_screens.h"
#include "fr_hd_text.h"

namespace frlg::hd {
namespace {

constexpr std::uint32_t kCB2_NamingScreen  = 0x0809FB70;
constexpr std::uint32_t kNamingScreenPtr   = 0x0203998C;  // sNamingScreen
constexpr std::uint32_t kKeyboardChars     = 0x083E22D0;  // [3 kb][4 rows][8 cols]
constexpr std::uint32_t kPageColumnCounts  = 0x083E2330;  // [3 kb]
constexpr std::uint32_t kPageToKeyboardId  = 0x083E2351;  // [3 page]

struct NamingAnim { float hl_x = -1, hl_y = -1; } g_nm;

std::string glyph(int code) {
    if (code == 0x00) return " ";
    const char* u = gba_glyph_utf8(static_cast<unsigned>(code));
    return u ? std::string(u) : std::string();
}

void draw_naming(Ctx& c) {
    const Mem& m = c.m;
    const std::uint32_t p = m.u32(kNamingScreenPtr);
    if (!m.valid(p + 0x1E3C, 4)) return;
    const int page = m.u8(p + 0x1E22);
    if (page > 2) return;
    const std::uint32_t tmpl = m.u32(p + 0x1E28);
    const int max_chars = std::clamp<int>(m.u8(tmpl + 1), 1, 15);
    const int icon_fn = m.u8(tmpl + 2);
    const int template_num = m.u8(p + 0x1E2C);
    const int kb = m.u8(kPageToKeyboardId + page);
    const int columns = std::clamp<int>(m.u8(kPageColumnCounts + kb), 1, 8);
    // cursor sprite data[0]/[1] = column / row
    int ccol = -1, crow = -1;
    const int spr_id = m.u8(p + 0x1E23);
    if (spr_id < 64) {
        const std::uint32_t spr = g::Sprites + spr_id * 0x44;
        ccol = m.s16(spr + 0x2E);
        crow = m.s16(spr + 0x30);
    }

    screen_bg(c, col::kAccent);
    // ── header: icon + prompt ────────────────────────────────────────────
    std::string prompt;
    const int title_win = m.u8(p + 0x1E11 + 3);   // windows[WIN_TEXT_ENTRY_BOX]
    for (const TextRun& r : cap_runs(title_win, modern_text())) {
        if (r.arrow || r.keypad >= 0) continue;
        prompt += (prompt.empty() ? "" : " ") + r.text;
    }
    if (prompt.empty()) {
        static const char* const kPrompt[] = {"Your name?", "Box name?", "Nickname?", "Nickname?", "Rival's name?"};
        prompt = kPrompt[std::min(template_num, 4)];
    }
    const float fw = std::min(200.0f, c.vw - 40.0f), fx = (c.vw - fw) * 0.5f;
    float tx = fx;
    if (icon_fn == 3) {   // Pokémon icon
        const int species = m.u16(p + 0x1E34);
        const float bob = std::sin(static_cast<float>(c.t) * 5.0f) > 0 ? 1 : 0;
        image(c, tex_mon_icon(m, species, bob > 0 ? 1 : 0), fx - 4.0f, 2.0f, 22.0f, 22.0f);
        tx += 20.0f;
    } else if (icon_fn == 2) {
        // PC: a little monitor
        rect(c, fx + 1.0f, 6.0f, 14.0f, 10.0f, col::rgba(120, 200, 140), 2.0f);
        rect(c, fx + 3.0f, 8.0f, 10.0f, 6.0f, col::rgba(30, 60, 50), 1.0f);
        rect(c, fx + 5.0f, 16.5f, 6.0f, 2.0f, col::rgba(170, 180, 200), 1.0f);
        tx += 19.0f;
    } else {
        rect(c, fx, 7.0f, 2.0f, 11.0f, col::kAccent, 1.0f);
        tx += 6.0f;
    }
    text(c, F::Bold, 11.5f, tx, 6.5f, col::kText, prompt, kLeft, fx + fw - tx);

    // ── text field ───────────────────────────────────────────────────────
    const float fy = 24.0f, fh = 24.0f;
    panel(c, fx, fy, fw, fh, 7.0f, col::rgba(22, 28, 44, 245));
    int len = 0;
    while (len < 16 && m.u8(p + 0x1800 + len) != 0xFF) ++len;
    len = std::min(len, max_chars);
    std::string typed;
    for (int i = 0; i < len; ++i) typed += glyph(m.u8(p + 0x1800 + i));
    const float tsz = 13.0f;
    const float tw = text_w(F::Bold, tsz, typed);
    const float tx0 = c.vw * 0.5f - tw * 0.5f;
    if (!typed.empty()) text(c, F::Bold, tsz, tx0, fy + 3.2f, col::kText, typed);
    if (std::fmod(c.t, 1.0) < 0.55 && len < max_chars)
        rect(c, tx0 + tw + 1.0f, fy + 4.5f, 1.2f, 12.5f, col::kAccent2, 0.6f);
    // one dot per character slot
    const float dot_gap = 5.0f, dots_w = dot_gap * (max_chars - 1);
    for (int i = 0; i < max_chars; ++i)
        circle(c, c.vw * 0.5f - dots_w * 0.5f + i * dot_gap, fy + fh - 4.0f, 1.2f,
               i < len ? col::kAccent2 : col::rgba(255, 255, 255, 40), 10);
    char cnt[16];
    std::snprintf(cnt, sizeof cnt, "%d/%d", len, max_chars);
    text(c, F::Medium, 6.0f, fx + fw - 6.0f, fy + fh - 8.5f, col::kTextFaint, cnt, kRight);

    // ── keyboard ─────────────────────────────────────────────────────────
    const float bw_side = 46.0f, gap = 2.5f;
    const float kh = 17.0f;
    const float kw_total = std::min(c.vw - 40.0f - bw_side - 8.0f, 196.0f);
    const float kw = (kw_total - gap * (columns - 1)) / columns;
    const float kx0 = (c.vw - (kw_total + 8.0f + bw_side)) * 0.5f, ky0 = 56.0f;
    panel(c, kx0 - 5.0f, ky0 - 5.0f, kw_total + 10.0f, 4 * kh + 3 * gap + 10.0f, 7.0f, col::rgba(17, 22, 35, 235));
    // highlight (animated)
    float hx = -1, hy = -1, hw = kw, hhgt = kh;
    if (ccol >= 0 && ccol < columns && crow >= 0 && crow < 4) {
        hx = kx0 + ccol * (kw + gap);
        hy = ky0 + crow * (kh + gap);
    }
    const float bx = kx0 + kw_total + 8.0f;
    const float bh = (4 * kh + 3 * gap - 2 * gap) / 3.0f;
    if (ccol >= columns && crow >= 0 && crow < 3) {
        hx = bx; hy = ky0 + crow * (bh + gap); hw = bw_side; hhgt = bh;
    }
    if (hx >= 0) {
        if (g_nm.hl_x < 0) { g_nm.hl_x = hx; g_nm.hl_y = hy; }
        const float k = 1.0f - std::exp(-c.dt * 26.0f);
        g_nm.hl_x += (hx - g_nm.hl_x) * k;
        g_nm.hl_y += (hy - g_nm.hl_y) * k;
    }
    for (int r = 0; r < 4; ++r) {
        for (int col_i = 0; col_i < columns; ++col_i) {
            const int code = m.u8(kKeyboardChars + kb * 32 + r * 8 + col_i);
            const float x = kx0 + col_i * (kw + gap), y = ky0 + r * (kh + gap);
            const bool is_space = code == 0x00 && kb != 2 && r < 2 && col_i == 6;
            if (code == 0x00 && !is_space) continue;   // padding cell
            rect(c, x, y, kw, kh, col::rgba(255, 255, 255, 12), 4.0f);
            target(x, y, kw, kh, kHdHitNamingKey, r * 16 + col_i);
            if (is_space) {
                line(c, x + kw * 0.3f, y + kh * 0.62f, x + kw * 0.7f, y + kh * 0.62f, col::kTextDim, 0.7f);
                continue;
            }
        }
    }
    if (hx >= 0 && ccol < columns)
        pill_select(c, g_nm.hl_x, g_nm.hl_y, kw, kh, 4.0f);
    for (int r = 0; r < 4; ++r) {
        for (int col_i = 0; col_i < columns; ++col_i) {
            const int code = m.u8(kKeyboardChars + kb * 32 + r * 8 + col_i);
            if (code == 0x00) continue;
            const float x = kx0 + col_i * (kw + gap), y = ky0 + r * (kh + gap);
            const bool sel = col_i == ccol && r == crow;
            text(c, F::Bold, 9.6f, x + kw * 0.5f, y + (kh - 9.6f) * 0.5f, sel ? col::kWhite : col::kText, glyph(code),
                 kCenter, 0, sel);
        }
    }
    // side buttons: page swap (Select), back (B), OK (Start)
    static const char* const kNext[3] = {"ABC", "abc", "Symbols"};   // page 0 -> 1 -> 2 -> 0
    const char* next_page = kNext[page];
    struct Btn { const char* label; const char* sub; int key; ImU32 tint; };
    const Btn btns[3] = {{next_page, "Switch", 2, col::kBlue}, {"Delete", "", 1, col::kBad}, {"OK", "", 3, col::kGood}};
    for (int i = 0; i < 3; ++i) {
        const float y = ky0 + i * (bh + gap);
        const bool sel = ccol >= columns && crow == i;
        if (sel) pill_select(c, bx, y, bw_side, bh, 5.0f);
        else {
            rect(c, bx, y, bw_side, bh, col::rgba(30, 37, 56, 245), 5.0f);
            rect_line(c, bx, y, bw_side, bh, col::alpha(btns[i].tint, 0.6f), 5.0f, 0.5f);
        }
        text(c, F::Bold, 8.2f, bx + bw_side * 0.5f, y + bh * 0.5f - 6.6f, sel ? col::kWhite : col::kText, btns[i].label,
             kCenter);
        const float kw2 = keycap_for(c, 0, 0, 6.2f, btns[i].key, true);
        keycap_for(c, bx + (bw_side - kw2) * 0.5f, y + bh * 0.5f + 2.6f, 6.2f, btns[i].key);
        target(bx, y, bw_side, bh, kHdHitNamingKey, i == 2 ? 0xFF : (i == 0 ? 0xFE : 0xFD));
    }
    // ── footer ───────────────────────────────────────────────────────────
    text(c, F::Medium, 7.0f, c.vw * 0.5f, 145.0f, col::kTextDim,
         "Type on your keyboard  \xc2\xb7  Enter to finish  \xc2\xb7  Backspace to delete", kCenter);
}

}  // namespace

bool naming_draw(Ctx& c) {
    if (c.sc->cb2 != kCB2_NamingScreen) return false;
    draw_naming(c);
    return true;
}

}  // namespace frlg::hd
