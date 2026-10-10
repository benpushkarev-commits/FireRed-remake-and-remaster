// fr_settings_menu.h — FireRed-styled native settings screen.
//
// Replaces the runtime's generic settings overlay with a screen drawn in
// FireRed's own style: the cartridge's font, the player's chosen window
// frame and the standard menu palette, all decoded live from the
// hash-verified ROM (no Nintendo assets ship with the binary). It is reached
// with Esc / pad Guide, or from a SETTINGS entry that takes EXIT's slot in the
// in-game Start menu (B / START still close that menu).
//
// FireRed USA rev0 only: every address below comes from the pokefirered
// symbol map and is re-verified against the ROM before anything is enabled.

#pragma once

#include <string>
#include <vector>

namespace gbarecomp { struct RunOptions; }

namespace frlg {

// Wires the screen into `opts` (game menu hooks + ROM-loaded patch step).
// `exe_dir` is where config.ini lives, so display/audio choices persist.
void install_settings_menu(gbarecomp::RunOptions& opts,
                           const std::string& exe_dir);

// Mouse over the open settings pages (native 240x160 coordinates): hover
// selects a row, left-click activates it (cycles a value), right-click goes
// back, the wheel moves the cursor. `pressed` bit0 = left, bit1 = right.
void settings_menu_mouse(float nx, float ny, bool inside, bool moved,
                         unsigned pressed, int wheel);

// The game's OPTION page 1 is up: switch to page 2 (mouse click on the page
// tag). Returns false when the OPTION screen is not ready for input.
bool settings_menu_open_page2();

// ── HD interface view of the settings pages ─────────────────────────────
struct SettingsHdRow {
    std::string label, value;
    int kind = 0;          // 0 action, 1 choice (cycles), 2 slider (0..100), 3 key binding
    int choice = 0, choices = 0;
    const char* const* names = nullptr;  // choice names (kind 1)
    bool enabled = true;
    std::string note;      // help text that replaces the row's usual description (e.g. a key conflict)
};
struct SettingsHdView {
    int mode = 0;          // 0 closed, 1 page 2 of the OPTION screen, 2 stand-alone (Esc), 3 first-run setup
    bool controls = false; // the key-binding list
    int sub = 0;           // 0 main list, 1 controls, 2 interface
    int cursor = 0;
    bool capturing = false;
    std::string notice;    // why the pressed key was refused while capturing (empty = none)
    float bright = 1.0f;   // stand-alone fade: page brightness 0..1
    float black = 0.0f;    // stand-alone fade: black over the game behind 0..1
    std::vector<SettingsHdRow> rows;
};
bool settings_hd_view(SettingsHdView* out);
// Pointer on the HD settings page: row under the pointer (-1 none), dir
// -1/+1 for a value segment (0 = the row itself).
void settings_hd_mouse(int row, int dir, bool moved, unsigned pressed, int wheel);
// Build identification (compile date/time, + FRLG_BUILD_ID when defined): a startup stderr diagnostic only,
// never shown in the game UI.
const char* settings_build_id();
// True while a key capture is pending (GBA binding or fast-forward key): other hotkeys polled from the
// keyboard state (F3 = 3D view) must ignore the key press.
bool settings_capture_active();
// Jump to a category (0 main, 1 controls, 2 interface, 3 extras): the HD
// category tabs. Ignored while a key is being captured or the page closes.
void settings_hd_category(int sub);
// L/R equivalent for the HD hint keys: previous (-1) / next (+1) category.
void settings_hd_category_step(int dir);
// Leave page 2 back to the game's page 1 (the OPTION "Game" tab).
void settings_hd_page1();
// The open settings page is drawn by the HD interface (pointer targets
// come from the HD layout).
bool settings_menu_uses_hd();

}  // namespace frlg
