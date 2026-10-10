// fr_hd_capture.h — mirror of everything the game draws into its text
// windows, captured at the window/text API level with observe-only probes
// (CopyGlyphToWindow, FillWindowPixel*, ScrollWindow, blits, keypad icons).
// The HD UI re-renders windows from this record; guest state is never
// written.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "fr_guest.h"

namespace frlg::hd {

struct Glyph {
    std::int16_t x = 0, y = 0;   // window-relative pixels
    std::uint8_t w = 0, h = 0;
    std::uint16_t code = 0;      // GBA char (0x100+n extra symbol, 0x200+n keypad icon)
    std::uint8_t font = 0, fg = 0, bg = 0, shadow = 0;
};

struct CapWindow {
    std::vector<Glyph> glyphs;
    std::uint32_t gen = 0;       // bumps on every change
    bool arrow = false;          // the game's ▼ continue arrow is drawn
    int arrow_x = 0, arrow_y = 0;
    bool gfx = false;            // graphics were blitted / copied into the window
    int fill = 0;                // last full fill (palette index)
};

// One horizontal run of contiguous glyphs with shared style.
struct TextRun {
    int x = 0, y = 0, w = 0, h = 0;
    int font = 0, fg = 0, shadow = 0;
    std::string text;            // UTF-8 (modern-cased when requested)
    int keypad = -1;             // >= 0: a lone keypad icon (CHAR_A_BUTTON ...)
    bool arrow = false;          // the ▶ selector glyph
};

void capture_install();              // register probes (static init time)
void capture_enable(bool on);        // after the ROM is loaded
void capture_validate(const Mem& m); // per frame: forget re-allocated windows
const CapWindow& cap_window(int id);
std::vector<TextRun> cap_runs(int id, bool modern);
// Plain text of a window, rows joined with '\n' (debug / accessibility).
std::string cap_text(int id, bool modern);

}  // namespace frlg::hd
