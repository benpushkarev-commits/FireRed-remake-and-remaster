// fr_hd_capture.cpp — window/text mirror. See fr_hd_capture.h.
#include "fr_hd_capture.h"

#include <algorithm>
#include <cstring>

#include "fr_hd_text.h"
#include "mod_function_hooks.h"

namespace frlg::hd {
namespace {

// FireRed USA rev0 (pokefirered symbol map).
constexpr std::uint32_t CopyGlyphToWindow                  = 0x08003014;
constexpr std::uint32_t FillWindowPixelBuffer              = 0x0800445C;
constexpr std::uint32_t FillWindowPixelRect                = 0x08004378;
constexpr std::uint32_t ScrollWindow                       = 0x080044A8;
constexpr std::uint32_t CopyToWindowPixelBuffer            = 0x08004404;
constexpr std::uint32_t BlitBitmapRectToWindow             = 0x080041F0;
constexpr std::uint32_t BlitBitmapRectToWindowWithColorKey = 0x080042B0;
constexpr std::uint32_t RemoveWindow                       = 0x08003E3C;
constexpr std::uint32_t FreeAllWindowBuffers               = 0x08003ECC;
constexpr std::uint32_t InitWindows                        = 0x08003B24;
constexpr std::uint32_t DrawKeypadIcon                     = 0x080063C8;

constexpr std::uint32_t kDownArrowTiles = 0x081EA14C;  // sDownArrowTiles (0x400)
constexpr std::uint32_t kKeypadTiles    = 0x081EA700;  // gKeypadIconTiles
constexpr std::uint32_t kKeypadIcons    = 0x081EA6CC;  // {u16 tile, u8 w, u8 h}[13]

struct ProbeSpec { const char* id; std::uint32_t addr; };
const ProbeSpec kProbes[] = {
    {"frlg.hd.glyph", CopyGlyphToWindow},
    {"frlg.hd.fill", FillWindowPixelBuffer},
    {"frlg.hd.fill_rect", FillWindowPixelRect},
    {"frlg.hd.scroll", ScrollWindow},
    {"frlg.hd.copy_pixels", CopyToWindowPixelBuffer},
    {"frlg.hd.blit", BlitBitmapRectToWindow},
    {"frlg.hd.blit_key", BlitBitmapRectToWindowWithColorKey},
    {"frlg.hd.remove_window", RemoveWindow},
    {"frlg.hd.free_windows", FreeAllWindowBuffers},
    {"frlg.hd.init_windows", InitWindows},
    {"frlg.hd.keypad", DrawKeypadIcon},
};

CapWindow g_win[32];
WindowInfo g_info[32];

void reset(int id) {
    if (id < 0 || id >= 32) return;
    const std::uint32_t gen = g_win[id].gen + 1;
    g_win[id] = CapWindow{};
    g_win[id].gen = gen;
}

bool overlaps(const Glyph& g, int x, int y, int w, int h) {
    return g.x < x + w && x < g.x + g.w && g.y < y + h && y < g.y + g.h;
}

void erase_rect(CapWindow& c, int x, int y, int w, int h) {
    auto& v = c.glyphs;
    const std::size_t before = v.size();
    v.erase(std::remove_if(v.begin(), v.end(),
                           [&](const Glyph& g) { return overlaps(g, x, y, w, h); }),
            v.end());
    if (c.arrow && c.arrow_x < x + w && x < c.arrow_x + 10 && c.arrow_y < y + h &&
        y < c.arrow_y + 12)
        c.arrow = false;
    if (v.size() != before) ++c.gen;
}

void add_glyph(int win, const Glyph& g) {
    if (win < 0 || win >= 32) return;
    CapWindow& c = g_win[win];
    erase_rect(c, g.x, g.y, std::max<int>(1, g.w), std::max<int>(1, g.h));
    c.glyphs.push_back(g);
    ++c.gen;
}

std::uint32_t stack_arg(const Mem& m, const ArmCpuState* cpu, int index) {
    return m.u32(cpu->R[13] + 4u * index);
}

int probe(std::uint32_t addr, int /*thumb*/, ArmCpuState* cpu) {
    if (!cpu) return 0;
    const Mem m = Mem::current();
    if (!m.ok()) return 0;
    switch (addr) {
        case CopyGlyphToWindow: {
            const std::uint32_t p = cpu->R[0];
            const int win = m.u8(p + 4);
            const std::uint32_t cur = m.u32(p);
            unsigned code = m.u8(cur - 1);
            if (m.u8(cur - 2) == 0xF9) code = 0x100 | code;
            else if (m.u8(cur - 1) == 0x0C && m.u8(cur - 2) == 0xFC) code = m.u8(cur);
            Glyph g;
            g.x = m.u8(p + 8);
            g.y = m.u8(p + 9);
            g.w = m.u8(g::GlyphInfo + 0x80);
            g.h = m.u8(g::GlyphInfo + 0x81);
            g.code = static_cast<std::uint16_t>(code);
            g.font = m.u8(p + 0x14) & 0x0F;
            g.fg = m.u8(p + 0x0C) >> 4;
            g.bg = m.u8(p + 0x0D);
            g.shadow = m.u8(p + 0x0E);
            add_glyph(win, g);
            break;
        }
        case DrawKeypadIcon: {
            const int win = static_cast<int>(cpu->R[0] & 0xFF);
            const int icon = static_cast<int>(cpu->R[1] & 0xFF);
            if (icon > 12) break;
            Glyph g;
            g.x = static_cast<std::int16_t>(cpu->R[2] & 0xFFFF);
            g.y = static_cast<std::int16_t>(cpu->R[3] & 0xFFFF);
            g.w = m.u8(kKeypadIcons + icon * 4 + 2);
            g.h = m.u8(kKeypadIcons + icon * 4 + 3);
            g.code = static_cast<std::uint16_t>(0x200 | icon);
            add_glyph(win, g);
            break;
        }
        case FillWindowPixelBuffer: {
            const int win = static_cast<int>(cpu->R[0] & 0xFF);
            if (win >= 32) break;
            const std::uint32_t gen = g_win[win].gen + 1;
            g_win[win] = CapWindow{};
            g_win[win].gen = gen;
            g_win[win].fill = static_cast<int>(cpu->R[1] & 0x0F);
            break;
        }
        case FillWindowPixelRect: {
            const int win = static_cast<int>(cpu->R[0] & 0xFF);
            if (win >= 32) break;
            const int x = static_cast<int>(cpu->R[2] & 0xFFFF), y = static_cast<int>(cpu->R[3] & 0xFFFF);
            const int w = static_cast<int>(stack_arg(m, cpu, 0) & 0xFFFF);
            const int h = static_cast<int>(stack_arg(m, cpu, 1) & 0xFFFF);
            erase_rect(g_win[win], x, y, w, h);
            break;
        }
        case ScrollWindow: {
            const int win = static_cast<int>(cpu->R[0] & 0xFF);
            if (win >= 32) break;
            const int dir = static_cast<int>(cpu->R[1] & 0xFF);
            const int dist = static_cast<int>(cpu->R[2] & 0xFF);
            const int height = window_info(m, win).height * 8;
            CapWindow& c = g_win[win];
            for (Glyph& gl : c.glyphs) gl.y = static_cast<std::int16_t>(gl.y + (dir == 0 ? -dist : dist));
            c.glyphs.erase(std::remove_if(c.glyphs.begin(), c.glyphs.end(),
                                          [&](const Glyph& gl) { return gl.y + gl.h <= 0 || gl.y >= height; }),
                           c.glyphs.end());
            c.arrow = false;
            ++c.gen;
            break;
        }
        case CopyToWindowPixelBuffer: {
            const int win = static_cast<int>(cpu->R[0] & 0xFF);
            if (win >= 32) break;
            reset(win);
            g_win[win].gfx = true;
            break;
        }
        case BlitBitmapRectToWindow:
        case BlitBitmapRectToWindowWithColorKey: {
            const int win = static_cast<int>(cpu->R[0] & 0xFF);
            if (win >= 32) break;
            const std::uint32_t src = cpu->R[1];
            const int x = static_cast<int>(stack_arg(m, cpu, 2) & 0xFFFF);
            const int y = static_cast<int>(stack_arg(m, cpu, 3) & 0xFFFF);
            const int w = static_cast<int>(stack_arg(m, cpu, 4) & 0xFFFF);
            const int h = static_cast<int>(stack_arg(m, cpu, 5) & 0xFFFF);
            CapWindow& c = g_win[win];
            if (src >= kDownArrowTiles && src < kDownArrowTiles + 0x400) {
                c.arrow = true;
                c.arrow_x = x;
                c.arrow_y = y;
                ++c.gen;
            } else if (src >= kKeypadTiles && src < kKeypadTiles + 0x800) {
                // the keypad glyph was recorded by DrawKeypadIcon
            } else {
                erase_rect(c, x, y, w, h);
                c.gfx = true;
                ++c.gen;
            }
            break;
        }
        case RemoveWindow: reset(static_cast<int>(cpu->R[0] & 0xFF)); break;
        case FreeAllWindowBuffers:
        case InitWindows:
            for (int i = 0; i < 32; ++i) reset(i);
            break;
        default: break;
    }
    return 0;  // observe only
}

struct Registrar {
    Registrar() {
        for (const auto& p : kProbes) gba_mod_register_function_entry_plugin(p.id, p.addr, 1, probe);
    }
} g_registrar;

}  // namespace

void capture_install() { (void)g_registrar; }

void capture_enable(bool on) {
    for (const auto& p : kProbes) gba_mod_set_function_hook_enabled(p.id, on ? 1 : 0);
}

void capture_validate(const Mem& m) {
    // Window slots are reset by the RemoveWindow / FreeAllWindowBuffers /
    // InitWindows probes; a slot that is no longer allocated is dropped
    // here as a safety net. (A new window's text is printed in the same
    // frame it is created, so a template change must not wipe it.)
    for (int i = 0; i < 32; ++i) {
        const WindowInfo w = window_info(m, i);
        if (!w.live() && g_info[i].live()) reset(i);
        g_info[i] = w;
    }
}

const CapWindow& cap_window(int id) {
    static const CapWindow kEmpty;
    return (id >= 0 && id < 32) ? g_win[id] : kEmpty;
}

std::vector<TextRun> cap_runs(int id, bool modern) {
    std::vector<TextRun> runs;
    if (id < 0 || id >= 32) return runs;
    std::vector<Glyph> v = g_win[id].glyphs;
    std::stable_sort(v.begin(), v.end(), [](const Glyph& a, const Glyph& b) {
        return a.y != b.y ? a.y < b.y : a.x < b.x;
    });
    TextRun cur;
    bool open = false;
    int last_end = 0;
    std::uint16_t prev_code = 0;
    auto flush = [&]() {
        if (!open) return;
        if (modern && cur.keypad < 0 && !cur.arrow) cur.text = modern_case(cur.text);
        runs.push_back(cur);
        open = false;
    };
    for (std::size_t i = 0; i < v.size(); ++i) {
        const Glyph& g = v[i];
        const bool keypad = g.code >= 0x200;
        const bool arrow = g.code == 0xEF;
        const bool joins = open && !keypad && !arrow && cur.keypad < 0 && !cur.arrow &&
                           g.y == cur.y && std::abs(g.x - last_end) <= 1 && g.fg == cur.fg &&
                           g.font == cur.font;
        if (!joins) {
            flush();
            cur = TextRun{};
            cur.x = g.x;
            cur.y = g.y;
            cur.h = g.h;
            cur.font = g.font;
            cur.fg = g.fg;
            cur.shadow = g.shadow;
            cur.keypad = keypad ? (g.code & 0xFF) : -1;
            cur.arrow = arrow;
            open = true;
            prev_code = 0;
        }
        if (!keypad && !arrow) {
            if (prev_code == 0x53 && g.code == 0x54) {
                // PK + MN glyph pair: "PKMN"
                if (cur.text.size() >= 2) cur.text.resize(cur.text.size() - 2);
                cur.text += "Pok\xc3\xa9mon";
            } else {
                cur.text += gba_glyph_utf8(g.code);
            }
        }
        prev_code = g.code;
        cur.h = std::max<int>(cur.h, g.h);
        last_end = g.x + g.w;
        cur.w = last_end - cur.x;
    }
    flush();
    return runs;
}

std::string cap_text(int id, bool modern) {
    std::string out;
    int y = -1000;
    for (const TextRun& r : cap_runs(id, modern)) {
        if (r.arrow) continue;
        if (!out.empty()) out += (r.y != y) ? "\n" : " ";
        y = r.y;
        out += r.keypad >= 0 ? "[key]" : r.text;
    }
    return out;
}

}  // namespace frlg::hd
