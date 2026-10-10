#!/usr/bin/env python3
"""Compile and test the settings pages' navigation rules (fr_settings_nav.h).

Usage: python tools/py/check_settings_nav.py [c++ compiler]
No ROM, game or window is needed.
"""
import os, pathlib, shutil, subprocess, sys, tempfile

root = pathlib.Path(__file__).resolve().parents[2]
header = root / "FireRedLeafGreenRecomp/src/fr_settings_nav.h"

checks = r'''
#include "fr_settings_nav.h"
#include "fr_settings_ini.h"
#include <iostream>
#include <stdexcept>
using namespace frlg::nav;
static void require(bool ok, const char* msg) {
    if (!ok) { std::cerr << "FAILED: " << msg << std::endl; throw std::runtime_error(msg); }
}
int main() {
    // L/R visit every category once and come back (both directions)
    for (int dir : {-1, +1}) {
        int seen[kCatCount] = {}, cat = kMain;
        for (int i = 0; i < kCatCount; ++i) { cat = cat_cycle(cat, dir); ++seen[cat]; }
        require(cat == kMain, "category ring does not close");
        for (int i = 0; i < kCatCount; ++i) require(seen[i] == 1, "category skipped or repeated");
    }
    require(cat_cycle(kMain, +1) == kInterface && cat_cycle(kInterface, +1) == kExtras &&
            cat_cycle(kExtras, +1) == kControls && cat_cycle(kControls, +1) == kMain, "ring order is not the tab order");
    require(cat_cycle(kMain, -1) == kControls, "previous from main");

    // page 2 ring: Game | General | Interface | Extras | Controls
    require(page_action(kPage2, kMain, -1) == kLeavePage2, "L from General must reach Game");
    require(page_action(kPage2, kMain, +1) == kCycle, "R from General must reach Interface, not Game");
    require(page_action(kPage2, kControls, +1) == kLeavePage2, "R from Controls must reach Game");
    require(page_action(kPage2, kControls, -1) == kCycle, "L from Controls must reach Extras");
    for (int cat : {kInterface, kExtras})
        for (int dir : {-1, +1}) require(page_action(kPage2, cat, dir) == kCycle, "middle categories must cycle");
    {   // walk the whole page-2 ring both ways: Game is visited exactly once per lap
        for (int dir : {-1, +1}) {
            int game = 0, cat = game_enter_cat(dir), steps = 0;   // arrive from Game
            while (steps++ < 20) {
                const Action a = page_action(kPage2, cat, dir);
                if (a == kLeavePage2) { ++game; break; }
                cat = cat_cycle(cat, dir);
            }
            require(game == 1 && steps == kCatCount, "page-2 ring is not Game + four categories");
        }
    }
    require(game_enter_cat(+1) == kMain && game_enter_cat(-1) == kControls, "Game entry end");
    for (int cat : {kMain, kInterface, kExtras, kControls}) { (void)cat; require(game_tab_action(kPage2) == kLeavePage2, "Game tab dead"); }
    require(game_tab_action(kStandalone) == kNothing && game_tab_action(kSetup) == kNothing && game_tab_action(kNone) == kNothing,
            "Game tab acts outside page 2");
    for (int dir : {-1, +1}) {
        for (int mode : {kStandalone, kSetup}) {   // four-category ring, never Game
            int cat = kMain;
            for (int i = 0; i < kCatCount; ++i) {
                require(page_action(mode, cat, dir) == kCycle, "stand-alone ring left a category");
                cat = cat_cycle(cat, dir);
            }
            require(cat == kMain, "stand-alone ring does not close");
        }
    }
    require(page_action(kNone, kMain, +1) == kNothing, "closed menu reacts");

    // B goes back one level, Esc resumes from anywhere
    for (int cat : {kControls, kInterface, kExtras}) {
        require(back_action(kStandalone, cat, false) == kLeaveSub, "B in a category must return to the list");
        require(back_action(kPage2, cat, false) == kLeaveSub, "B in a page-2 category must return to the list");
        require(back_action(kStandalone, cat, true) == kClose, "Esc in a category must resume the game");
        require(back_action(kPage2, cat, true) == kExitOption, "Esc in a page-2 category must close OPTION");
        require(back_action(kSetup, cat, true) == kLeaveSub, "setup cannot be closed from a category");
    }
    for (bool close_key : {false, true}) {
        require(back_action(kStandalone, kMain, close_key) == kClose, "stand-alone main does not resume");
        require(back_action(kPage2, kMain, close_key) == kExitOption, "page 2 main does not close OPTION");
        require(back_action(kSetup, kMain, close_key) == kJumpStart, "setup main must jump to Start");
    }
    require(back_action(kNone, kMain, false) == kNothing, "closed menu reacts to B");

    // list cursor and scroll
    require(wrap_cursor(0, 5, -1) == 4 && wrap_cursor(4, 5, +1) == 0 && wrap_cursor(2, 5, +1) == 3, "cursor wrap");
    require(wrap_cursor(0, 0, +1) == 0, "empty list cursor");
    require(scroll_for(7, 0, 7, 14) == 1, "cursor below the window must scroll");
    require(scroll_for(0, 5, 7, 14) == 0, "cursor above the window must scroll back");
    require(scroll_for(13, 0, 7, 14) == 7, "last row must be the last visible row");
    require(scroll_for(2, 3, 7, 4) == 0, "scroll beyond a short list");
    int sc = 0;
    for (int cur = 0; cur < 14; ++cur) {
        sc = scroll_for(cur, sc, 7, 14);
        require(cur >= sc && cur < sc + 7, "cursor left the visible rows");
    }

    // per-category memory
    CursorMemory mem;
    mem.save(kExtras, 3, 0); mem.save(kControls, 9, 3);
    require(mem.cursor[kExtras] == 3 && mem.cursor[kControls] == 9 && mem.scroll[kControls] == 3, "memory lost a row");
    require(mem.cursor[kInterface] == 0, "unvisited category not at the top");
    mem.reset();
    require(mem.cursor[kControls] == 0 && mem.scroll[kControls] == 0, "memory not reset on open");
    // ── persistence: merging the [Launcher] keys into an existing config.ini ──
    {
        using L = std::vector<std::string>;
        const std::vector<std::pair<std::string, std::string>> kv = {{"fullscreen", "1"}, {"scale", "4"}, {"volume", "30"}};
        const L in = {"; my config", "[Launcher]", "fullscreen = 0", "unknown = keep", "# c", "scale=3", "[Remaster]", "diorama = 1", "ff_key = 44"};
        L out = frlg::ini::merge_section(in, "Launcher", kv);
        auto find = [&](const L& v, const std::string& x) { for (const auto& l : v) if (l == x) return true; return false; };
        require(find(out, "fullscreen = 1") && find(out, "scale = 4") && !find(out, "fullscreen = 0") && !find(out, "scale=3"), "keys not replaced in place");
        require(find(out, "volume = 30"), "missing key not appended");
        require(find(out, "unknown = keep") && find(out, "# c") && find(out, "; my config"), "unknown keys / comments lost");
        require(find(out, "[Remaster]") && find(out, "diorama = 1") && find(out, "ff_key = 44"), "another section damaged");
        // the appended key stays inside [Launcher] (before [Remaster])
        int launcher = -1, remaster = -1, vol = -1;
        for (std::size_t i = 0; i < out.size(); ++i) { if (out[i] == "[Launcher]") launcher = int(i); if (out[i] == "[Remaster]") remaster = int(i); if (out[i] == "volume = 30") vol = int(i); }
        require(launcher < vol && vol < remaster, "appended key outside its section");
        require(frlg::ini::merge_section(out, "Launcher", kv) == out, "saving twice changes the file");
        L none = frlg::ini::merge_section({}, "Launcher", kv);
        require(none.size() == 4 && none[0] == "[Launcher]", "a missing file / section must be created");
        const L dup = {"[Launcher]", "volume = 10", "volume = 20"};
        const L dd = frlg::ini::merge_section(dup, "Launcher", kv);
        int vcount = 0; for (const auto& l : dd) if (l.rfind("volume", 0) == 0) ++vcount;
        require(vcount == 1, "duplicate key lines kept");
        const L before_remaster = {"[Remaster]", "a = 1"};
        require(find(frlg::ini::merge_section(before_remaster, "Launcher", kv), "a = 1"), "other section lost when Launcher is added");
    }
    // ── input guards ──
    {
        const int bound[] = {26, 22, 4, 7, 29};   // W S A D Z
        for (int reserved : {0, 41, 42, 40, 88, 60, 68}) require(!frlg::ini::ff_key_allowed(reserved, bound, 5), "reserved key accepted as fast-forward");
        require(!frlg::ini::ff_key_allowed(29, bound, 5) && !frlg::ini::ff_key_allowed(4, bound, 5), "a bound key accepted as fast-forward");
        require(frlg::ini::ff_key_allowed(44, bound, 5) && frlg::ini::ff_key_allowed(225, bound, 5), "space / shift rejected");
        require(!frlg::ini::ff_key_allowed(600, bound, 5), "out-of-range scancode");
        std::uint32_t armed = 0;
        require(!frlg::ini::confirm_press(1000, &armed, 3000) && armed == 4000, "first press must only arm");
        require(frlg::ini::confirm_press(2500, &armed, 3000) && armed == 0, "second press inside the window must confirm");
        require(!frlg::ini::confirm_press(2600, &armed, 3000), "confirm must disarm");
        armed = 4000;
        require(!frlg::ini::confirm_press(4000, &armed, 3000) && armed == 7000, "an expired arm must re-arm, not confirm");
        for (int f = 58; f <= 69; ++f) require(!frlg::ini::ff_key_allowed(f, bound, 5), "F1..F12 accepted as fast-forward");
        require(frlg::ini::ff_key_allowed(70, bound, 5) == true, "PrintScreen reserved by mistake");
        // QUIT GAME arm: route independent (keys, mouse hover / click, wheel, tabs all call observe)
        {
            frlg::ini::QuitArm q;
            const std::uint32_t t = 10000;
            require(!q.press(t, 11, 0, 3000) && q.armed(t + 1), "first press must arm");
            q.observe(11, 0);
            require(q.armed(t + 100), "observing the same row must keep the arm");
            // mouse hover to another row (observed), then back to Quit inside the window: no confirm
            q.observe(3, 0);
            require(!q.armed(t + 200), "leaving the row must disarm");
            q.observe(11, 0);
            require(!q.armed(t + 300) && !q.press(t + 400, 11, 0, 3000), "returning to the row must not confirm");
            require(q.press(t + 500, 11, 0, 3000), "a fresh arm then a second press confirms");
            // category change between the presses
            frlg::ini::QuitArm c;
            require(!c.press(t, 11, 0, 3000), "arm");
            require(!c.press(t + 10, 11, 2, 3000), "same index in another category must not confirm");
            // expiry
            frlg::ini::QuitArm e;
            require(!e.press(t, 11, 0, 3000) && !e.press(t + 3000, 11, 0, 3000), "an expired arm must not confirm");
            // a move observed only when it happens: the arm is also honest without any observe call in between
            frlg::ini::QuitArm n;
            require(!n.press(t, 11, 0, 3000) && n.press(t + 50, 11, 0, 3000), "double press on the row confirms");
        }
        // capture blocks the mouse; a right click cancels
        using frlg::ini::CaptureMouse;
        require(frlg::ini::capture_mouse(false, 1) == CaptureMouse::kPass && frlg::ini::capture_mouse(false, 2) == CaptureMouse::kPass, "idle mouse blocked");
        require(frlg::ini::capture_mouse(true, 0) == CaptureMouse::kBlock && frlg::ini::capture_mouse(true, 1) == CaptureMouse::kBlock, "capture lets clicks through");
        require(frlg::ini::capture_mouse(true, 2) == CaptureMouse::kCancel && frlg::ini::capture_mouse(true, 3) == CaptureMouse::kCancel, "right click must cancel a capture");
        // GBA binding policy (symmetric with the fast-forward capture)
        for (int f = 58; f <= 69; ++f) require(frlg::ini::gba_key_refusal(f, 44) == 1, "F-key bindable to a GBA input");
        require(frlg::ini::gba_key_refusal(44, 44) == 2 && frlg::ini::gba_key_refusal(44, 225) == 0, "fast-forward key stolen / over-banned");
        for (int ok : {4, 29, 27, 40, 42, 44, 79, 80, 81, 82, 70})   // letters, Enter, Backspace, Space, arrows, PrintScreen
            require(frlg::ini::gba_key_refusal(ok, 225) == 0, "context key over-banned");
        require(frlg::ini::gba_key_refusal(225, 225) == 2, "the fast-forward key itself must be refused");
        require(frlg::ini::gba_key_refusal(44, 0) == 0, "no fast-forward key set must ban nothing");
        for (int sc = 1; sc < 512; ++sc) {
            if (sc >= 58 && sc <= 69) require(frlg::ini::gba_key_refusal(sc, 44) == 1 && !frlg::ini::ff_key_allowed(sc, nullptr, 0), "F-key policy asymmetric");
            const int bound1[] = {sc};
            require(frlg::ini::ff_key_refusal(sc, bound1, 1) != 0, "a GBA-bound key accepted as fast-forward");
        }
        require(frlg::ini::ff_key_refusal(44, bound, 5) == 0 && frlg::ini::ff_key_refusal(29, bound, 5) == 2 && frlg::ini::ff_key_refusal(60, bound, 5) == 1, "ff refusal codes");
        require(std::string(frlg::ini::refusal_text(false, 1)).find("F1-F12") != std::string::npos &&
                std::string(frlg::ini::refusal_text(false, 2)).find("Fast Forward") != std::string::npos &&
                std::string(frlg::ini::refusal_text(true, 2)).find("game button") != std::string::npos &&
                std::string(frlg::ini::refusal_text(true, 1)).find("reserved") != std::string::npos, "refusal reasons");
        // classic hint bar while capturing: the fast-forward capture counts too (it has no engine capture bit)
        require(frlg::ini::capture_hint(false, false, 0, 0) == 0 && frlg::ini::capture_hint(false, false, 1, 1) == 0, "hint shown without a capture");
        require(frlg::ini::capture_hint(true, false, 0, 0) == 1 && frlg::ini::capture_hint(false, true, 0, 0) == 1, "PRESS A KEY missing for a capture");
        require(frlg::ini::capture_hint(true, false, 1, 0) == 2 && frlg::ini::capture_hint(false, true, 0, 2) == 2, "refusal not shown");
        require(frlg::ini::capture_hint(false, true, 1, 0) == 1 && frlg::ini::capture_hint(true, false, 0, 2) == 1, "a refusal of the other capture leaked");
        // a pre-existing binding that equals the fast-forward key (controls.ini: Confirm alt = Space, ff_key = Space)
        {
            const int sc[] = {40, 44, 29}, bits[] = {0, 0, 1};   // Confirm = Enter + Space, Back = Z
            require(frlg::ini::ff_conflict_bit(44, sc, bits, 3) == 0, "conflicting input not found");
            require(!frlg::ini::ff_runtime_active(44, sc, bits, 3), "fast forward fires on a key a game input owns (double activation)");
            require(frlg::ini::ff_runtime_active(225, sc, bits, 3) && frlg::ini::ff_conflict_bit(225, sc, bits, 3) == -1, "a free key must work");
            require(!frlg::ini::ff_runtime_active(0, sc, bits, 3), "no key must not be active");
            require(frlg::ini::ff_conflict_bit(40, sc, bits, 3) == 0 && frlg::ini::ff_conflict_bit(29, sc, bits, 3) == 1, "binding bit lookup");
            require(std::string(frlg::ini::gba_input_name(0)) == "Confirm" && std::string(frlg::ini::gba_input_name(1)) == "Back" &&
                    std::string(frlg::ini::gba_input_name(9)) == "L Button" && std::string(frlg::ini::gba_input_name(10)) == "?", "input names");
            require(frlg::ini::ff_key_row_label("SPACE", true) == "SPACE (OFF)" && frlg::ini::ff_key_row_label("SPACE", false) == "SPACE", "row label");
            require(frlg::ini::ff_key_row_label("LEFT SHIFT", true) == "OFF", "long key name must not overflow the classic row");
            // the default Enter / Backspace context bindings stay bindable and are not touched by the policy
            require(frlg::ini::gba_key_refusal(40, 44) == 0 && frlg::ini::gba_key_refusal(42, 44) == 0, "Enter / Backspace over-banned");
        }
        // fast forward: a latch from before must not run through naming / capture / a conflict
        {
            using frlg::ini::ff_step;
            // toggle mode, already latched, then a naming screen (blocked): off, but the latch is kept and resumes
            auto a = ff_step(true, false, false, true, true, false);
            require(a.latched && !a.on && !a.down, "latched fast forward kept running during naming / capture");
            auto b = ff_step(a.latched, a.down, false, true, false, false);
            require(b.on && b.latched, "the latch must resume after the block");
            // pressing the key while blocked does not flip the latch
            auto c = ff_step(false, false, true, true, true, false);
            require(!c.latched && !c.on && !c.down, "key press counted while blocked");
            // conflict (key owned by a game input): suppressed too, pre-existing latch inactive
            auto d = ff_step(true, false, true, true, false, true);
            require(d.latched && !d.on && !d.down, "latched fast forward active on a key a game input owns");
            // normal toggle: press latches, release keeps, press again unlatches
            auto e1 = ff_step(false, false, true, true, false, false);
            auto e2 = ff_step(e1.latched, e1.down, false, true, false, false);
            auto e3 = ff_step(e2.latched, e2.down, true, true, false, false);
            require(e1.on && e2.on && !e3.on && !e3.latched, "toggle cycle broken");
            // hold mode: follows the key; blocked / conflict force it off
            require(ff_step(false, false, true, false, false, false).on && !ff_step(false, false, true, false, true, false).on &&
                    !ff_step(false, false, true, false, false, true).on && !ff_step(true, true, false, false, false, false).on, "hold mode");
            // hold mode never keeps a stale latch from toggle mode
            require(!ff_step(true, false, false, false, false, false).latched, "hold mode kept a toggle latch");
        }
        // help bar overflow: shrinking stops at 0.72, so a text still wider than the bar must wrap
        require(!frlg::ini::help_needs_wrap(198.0f, 198.0f) && !frlg::ini::help_needs_wrap(270.0f, 198.0f) && frlg::ini::help_needs_wrap(280.0f, 198.0f), "wrap threshold");
        require(!frlg::ini::help_needs_wrap(500.0f, 0.0f) && !frlg::ini::help_needs_wrap(0.0f, 198.0f), "degenerate widths");
        // help bar pixel-width budget with a fake proportional font (3 px / char, 'W' and 'M' 5 px, 'i' 'l' '.' 1.5 px)
        {
            auto fake = [](const std::string& t) { float w = 0; for (char c : t) w += (c == 'W' || c == 'M') ? 5.0f : (c == 'i' || c == 'l' || c == '.') ? 1.5f : 3.0f; return w; };
            const std::string two = "aaaa bbbb cccc dddd";                       // 4 * 12 + 3 * 3 = 57 px: two lines of 40
            require(frlg::ini::greedy_lines(two, 40.0f, fake) == 2 && frlg::ini::greedy_lines(two, 57.0f, fake) == 1 && frlg::ini::greedy_lines(two, 56.9f, fake) == 2, "greedy line count at the exact boundary");
            require(frlg::ini::greedy_lines("", 40.0f, fake) == 0 && frlg::ini::greedy_lines("supercalifragilistic", 10.0f, fake) == 1, "empty / unbreakable word");
            require(frlg::ini::clip_to_lines(two, 2, 40.0f, fake) == two, "a text that fits must be unchanged");
            const std::string wide = "WWWW MMMM WWWW MMMM WWWW MMMM WWWW MMMM";     // wide glyphs: 20 px per word: needs 8 lines at 25 px
            const std::string clipped = frlg::ini::clip_to_lines(wide, 2, 45.0f, fake);
            require(frlg::ini::greedy_lines(clipped, 45.0f, fake) <= 2, "wide text not cut to the line budget (tail would be dropped silently)");
            require(clipped.size() >= 3 && clipped.substr(clipped.size() - 3) == "..." && clipped.size() < wide.size(), "clipped text must end with an ellipsis");
            require(frlg::ini::clip_to_lines("WWWWWWWWWWWWWWWWWWWW", 2, 10.0f, fake) == "...", "one unbreakable too-wide word");
            // a long dynamic note like the key-conflict text, narrow bar, many 'm' like glyphs
            const std::string note = "Fast forward is off for this key: it is also the Confirm button. Pick another key here, or rebind that button.";
            require(frlg::ini::greedy_lines(frlg::ini::clip_to_lines(note, 2, 120.0f, fake), 120.0f, fake) <= 2, "dynamic note exceeds two lines in a narrow bar");
        }
        require(frlg::ini::binding_label("", "") == "NONE", "unbound input needs a label");
        require(frlg::ini::binding_label("Z", "") == "Z" && frlg::ini::binding_label("", "X") == "X", "single key label");
        require(frlg::ini::binding_label("Z", "X") == "Z X" && frlg::ini::binding_label("LEFT SHIFT", "RIGHT SHIFT") == "LEFT SHIFT", "second key must not overflow the row");
        require(!frlg::ini::value_area_click(40.0f, 144.0f) && frlg::ini::value_area_click(150.0f, 144.0f), "volume label click changes the value");
    }
    std::cout << "settings navigation checks passed\n";
}
'''
import re as _re
_src = (root / "FireRedLeafGreenRecomp/src/fr_hd_screens.cpp").read_text(encoding="utf-8")
for _d in _re.findall(r'r\.desc = "((?:[^"\\]|\\.)*)"', _src):
    _plain = _re.sub(r'\\x[0-9a-fA-F]{2}', 'x', _d)
    if len(_plain) > 118:   # ~3.1 px per character at 6.2 pt, two lines of 198 px: keep every help text within it
        raise SystemExit("HD help text too long for two lines (%d chars): %s" % (len(_plain), _d[:60]))
with tempfile.TemporaryDirectory(prefix="firered-nav-", dir=root / "tools") as folder:
    cpp = pathlib.Path(folder) / "check.cpp"
    exe = pathlib.Path(folder) / "check.exe"
    cpp.write_text(checks, encoding="utf-8")
    compiler = sys.argv[1] if len(sys.argv) > 1 else os.environ.get("CXX", "c++")
    compiler = shutil.which(compiler) or compiler
    env = dict(os.environ, TMP=folder, TEMP=folder,
               PATH=str(pathlib.Path(compiler).resolve().parent) + os.pathsep + os.environ.get("PATH", ""))
    flags = ["-std=c++17", "-O1", "-Wall", "-I", str(header.parent)]
    if os.name != "nt" and os.environ.get("FRLG_NO_SANITIZE") is None:
        flags += ["-fsanitize=address,undefined", "-fno-sanitize-recover=all"]
    if os.name == "nt":
        flags.append("-static")
    subprocess.run([compiler, *flags, str(cpp), "-o", str(exe)], check=True, env=env)
    subprocess.run([str(exe)], check=True)
