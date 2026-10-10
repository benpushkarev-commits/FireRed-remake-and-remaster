// fr_settings_nav.h — pure navigation rules of the settings pages.
//
// No game or window state in here, so tools/py/check_settings_nav.py can
// compile and test it without a ROM: which category L/R reach, what B / Esc
// do from each page, list wrapping and scrolling.
#pragma once

namespace frlg::nav {

// Same numbering as the settings menu's Sub (0 main, 1 controls, ...).
enum Cat { kMain = 0, kControls = 1, kInterface = 2, kExtras = 3 };
constexpr int kCatCount = 4;
// L/R and the category tabs walk this ring (the order they are drawn in).
constexpr int kCatRing[kCatCount] = {kMain, kInterface, kExtras, kControls};

enum Mode { kNone = 0, kPage2 = 1, kStandalone = 2, kSetup = 3 };

inline int cat_cycle(int cat, int dir) {
    int i = 0;
    while (i < kCatCount && kCatRing[i] != cat) ++i;
    return kCatRing[((i + (dir < 0 ? -1 : 1)) % kCatCount + kCatCount) % kCatCount];
}

enum Action {
    kNothing,
    kLeaveSub,     // back to the main list, cursor on the category's row
    kLeavePage2,   // to the game's own OPTION page 1
    kExitOption,   // close the OPTION screen (the game's own exit)
    kClose,        // close the stand-alone page (resume the game)
    kJumpStart,    // first-run setup: cursor to "Start"
    kCycle,        // switch to the next / previous category
};

// B (close_key = false) goes back one level; Esc / Guide (close_key = true)
// resumes the game from anywhere. First-run setup cannot be closed that way:
// it steps back, then jumps to Start.
inline Action back_action(int mode, int cat, bool close_key) {
    if (mode == kNone) return kNothing;
    if (mode == kSetup) return cat != kMain ? kLeaveSub : kJumpStart;
    if (cat != kMain && !close_key) return kLeaveSub;
    return mode == kPage2 ? kExitOption : kClose;
}

// L / R (dir -1 / +1). The tabs read Game | General | Interface | Extras |
// Controls on page 2: Game is the game's own OPTION page 1, so stepping off
// either end of the four settings categories turns to it. Stand-alone and
// first-run setup have no Game tab and wrap among the four.
inline Action page_action(int mode, int cat, int dir) {
    if (mode == kNone) return kNothing;
    if (mode == kPage2 && ((cat == kMain && dir < 0) || (cat == kControls && dir > 0))) return kLeavePage2;
    return kCycle;
}
// L / R pressed on the game's page 1 lands on the nearest end of that ring.
inline int game_enter_cat(int dir) { return dir < 0 ? kControls : kMain; }
// Clicking the Game tab: valid from page 2 in any category.
inline Action game_tab_action(int mode) { return mode == kPage2 ? kLeavePage2 : kNothing; }

inline int wrap_cursor(int cursor, int count, int dir) {
    return count <= 0 ? 0 : ((cursor + (dir < 0 ? -1 : 1)) % count + count) % count;
}
inline int scroll_for(int cursor, int scroll, int visible, int count) {
    if (cursor < scroll) scroll = cursor;
    if (cursor >= scroll + visible) scroll = cursor - visible + 1;
    const int max_scroll = count > visible ? count - visible : 0;
    return scroll < 0 ? 0 : scroll > max_scroll ? max_scroll : scroll;
}

// Cursor and scroll remembered per category, so switching away and back
// lands on the row you left.
struct CursorMemory {
    int cursor[kCatCount] = {};
    int scroll[kCatCount] = {};
    void reset() { for (int i = 0; i < kCatCount; ++i) cursor[i] = scroll[i] = 0; }
    void save(int cat, int c, int s) { cursor[cat] = c; scroll[cat] = s; }
};

}  // namespace frlg::nav
