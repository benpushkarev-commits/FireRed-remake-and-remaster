// fr_settings_ini.h — pure helpers for the settings pages' persistence and
// input guards, shared with tools/py/check_settings_nav.py (no game or window).
#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace frlg::ini {

// Merge `kv` into `[section]` of an ini file given as lines: existing keys are
// replaced in place, missing keys are appended to the section (created at the
// end when absent), every other line (comments, other sections, unknown keys)
// is kept as is. Applying it twice gives the same result.
inline std::vector<std::string> merge_section(const std::vector<std::string>& lines, const std::string& section,
                                              const std::vector<std::pair<std::string, std::string>>& kv) {
    const std::string header = "[" + section + "]";
    std::vector<bool> done(kv.size(), false);
    std::vector<std::string> out;
    bool in_section = false, saw_section = false;
    auto flush = [&]() {
        for (std::size_t i = 0; i < kv.size(); ++i)
            if (!done[i]) { out.push_back(kv[i].first + " = " + kv[i].second); done[i] = true; }
    };
    for (const std::string& l : lines) {
        const std::size_t p = l.find_first_not_of(" \t");
        if (p != std::string::npos && l[p] == '[') {
            if (in_section) flush();
            in_section = l.compare(p, header.size(), header) == 0;
            saw_section = saw_section || in_section;
            out.push_back(l);
            continue;
        }
        if (in_section && p != std::string::npos && l[p] != ';' && l[p] != '#') {
            const std::size_t eq = l.find('=');
            if (eq != std::string::npos) {
                std::string key = l.substr(p, eq - p);
                while (!key.empty() && (key.back() == ' ' || key.back() == '\t')) key.pop_back();
                bool replaced = false;
                for (std::size_t i = 0; i < kv.size(); ++i)
                    if (key == kv[i].first) {
                        if (!done[i]) out.push_back(kv[i].first + " = " + kv[i].second);   // a duplicate key line is dropped
                        done[i] = true;
                        replaced = true;
                        break;
                    }
                if (replaced) continue;
            }
        }
        out.push_back(l);
    }
    if (in_section) flush();
    if (!saw_section) { out.push_back(header); flush(); }
    return out;
}

// SDL scancodes the fast-forward key must not take: Escape (cancel), Backspace,
// Return / keypad Enter (menus), F1..F12 (save states, 3D view, fullscreen); nor any key
// that is bound to a GBA input (it would fire both).
// Refusal code for the fast-forward key: 0 ok, 1 reserved key, 2 already bound to a GBA input.
inline int ff_key_refusal(int scancode, const int* gba_bound, int n) {
    if (scancode <= 0 || scancode >= 512) return 1;
    if (scancode >= 58 && scancode <= 69) return 1;   // F1..F12: save states, 3D view, fullscreen, ...
    for (int reserved : {41, 42, 40, 88}) if (scancode == reserved) return 1;
    for (int i = 0; i < n; ++i) if (gba_bound[i] == scancode) return 2;
    return 0;
}
inline bool ff_key_allowed(int scancode, const int* gba_bound, int n) { return ff_key_refusal(scancode, gba_bound, n) == 0; }
// Refusal code when binding a GBA input to `scancode`: 0 ok, 1 F1..F12 (host hotkeys: save states, 3D view,
// fullscreen), 2 the current fast-forward key. Enter / Backspace / Space stay bindable (context keys); Esc cancels
// the capture in the host before this is asked.
inline int gba_key_refusal(int scancode, int ff_key) {
    if (scancode >= 58 && scancode <= 69) return 1;
    if (ff_key > 0 && scancode == ff_key) return 2;
    return 0;
}
// Runtime precedence between the fast-forward key and the GBA inputs: a key that is ALSO bound to a game input
// (an older controls.ini binding such as Confirm alt = Space with ff_key = Space) belongs to the game; fast forward
// is inactive for it until one of them is changed. Nothing is deleted from the user's files.
inline int ff_conflict_bit(int ff_key, const int* bound_scancodes, const int* bound_bits, int n) {
    if (ff_key <= 0) return -1;
    for (int i = 0; i < n; ++i) if (bound_scancodes[i] == ff_key) return bound_bits[i];
    return -1;
}
inline bool ff_runtime_active(int ff_key, const int* bound_scancodes, const int* bound_bits, int n) {
    return ff_key > 0 && ff_conflict_bit(ff_key, bound_scancodes, bound_bits, n) < 0;
}
// One frame of the fast-forward key logic. `blocked` = a game menu is open, a name is being typed or a key capture is
// pending; `conflict` = the key is also a game input. Both suppress the key press AND the final activation, so a toggle
// latched earlier cannot keep running through a naming screen or a capture (the latch itself is kept and resumes after).
struct FfState { bool latched, down, on; };
inline FfState ff_step(bool latched, bool key_was_down, bool key_down, bool toggle, bool blocked, bool conflict) {
    const bool suppressed = blocked || conflict;
    FfState r;
    r.down = !suppressed && key_down;
    r.latched = toggle ? (r.down && !key_was_down ? !latched : latched) : r.down;
    r.on = r.latched && !suppressed;
    return r;
}
// Name of a GBA input bit as the settings pages call it.
inline const char* gba_input_name(int bit) {
    static const char* const k[] = {"Confirm", "Back", "Use Item", "Menu", "Right", "Left", "Up", "Down", "R Button", "L Button"};
    return bit >= 0 && bit < 10 ? k[bit] : "?";
}
// Classic value text of the fast-forward key row: the key, plus (OFF) when a game input owns the key.
inline std::string ff_key_row_label(const std::string& key, bool conflict) {
    if (!conflict) return key;
    return key.size() + 6 <= 11 ? key + " (OFF)" : "OFF";
}
// The HD help bar shrinks a one-line text down to min_scale (0.72) of its size; a text that is still wider than the bar then overflows the
// panel (and the screen edge in the 240-wide view), so it is drawn as two small lines instead.
inline bool help_needs_wrap(float width_at_full_size, float max_w, float min_scale = 0.72f) {
    return max_w > 0.0f && width_at_full_size * min_scale > max_w;
}
// Greedy word wrap exactly as the HD help bar's wrap_text does it (a word wider than the line stays alone on its line), with the
// text measure supplied by the caller (the real font at draw time, a fake in the fixtures). Returns the number of lines.
template <class Measure>
int greedy_lines(const std::string& s, float max_w, Measure measure) {
    int lines = 0;
    std::string line, word;
    auto add_word = [&]() {
        if (word.empty()) return;
        const std::string cand = line.empty() ? word : line + " " + word;
        if (!line.empty() && measure(cand) > max_w) { ++lines; line = word; } else line = cand;
        word.clear();
    };
    for (char ch : s) { if (ch == ' ') add_word(); else word += ch; }
    add_word();
    if (!line.empty()) ++lines;
    return lines;
}
// True when the text fits max_lines AND no single word is wider than a line (an unbreakable word would run past the panel even though it
// counts as one line).
template <class Measure>
bool fits_lines(const std::string& s, int max_lines, float max_w, Measure measure) {
    if (greedy_lines(s, max_w, measure) > max_lines) return false;
    std::string word;
    for (std::size_t i = 0; i <= s.size(); ++i) {
        if (i == s.size() || s[i] == ' ') { if (!word.empty() && measure(word) > max_w) return false; word.clear(); }
        else word += s[i];
    }
    return true;
}
// The text that is actually drawn: unchanged when it fits max_lines, otherwise cut at a word boundary and ended with "..." so the tail is
// never silently dropped by the line limit (a pixel-width edge case, not a character count).
template <class Measure>
std::string clip_to_lines(const std::string& s, int max_lines, float max_w, Measure measure) {
    if (fits_lines(s, max_lines, max_w, measure)) return s;
    std::vector<std::string> words;
    std::string w;
    for (char ch : s) { if (ch == ' ') { if (!w.empty()) words.push_back(w); w.clear(); } else w += ch; }
    if (!w.empty()) words.push_back(w);
    while (!words.empty()) {
        words.pop_back();
        std::string t;
        for (const std::string& x : words) t += (t.empty() ? "" : " ") + x;
        t += "...";
        if (fits_lines(t, max_lines, max_w, measure)) return t;
    }
    return "...";
}
// Classic hint bar while a key capture is pending (GBA binding OR fast-forward key): 0 none, 1 "PRESS A KEY", 2 refused.
inline int capture_hint(bool engine_capture, bool ff_capture, int gba_refusal, int ff_refusal) {
    if (!engine_capture && !ff_capture) return 0;
    return (engine_capture && gba_refusal) || (ff_capture && ff_refusal) ? 2 : 1;
}
inline const char* refusal_text(bool for_ff, int code) {
    if (for_ff) return code == 2 ? "That key is already used by a game button" : "That key is reserved";
    return code == 2 ? "That key is Fast Forward" : "F1-F12 are reserved for hotkeys";
}

// Second press within `window_ms` confirms (QUIT GAME). Arms on the first press.
inline bool confirm_press(std::uint32_t now, std::uint32_t* armed_until, std::uint32_t window_ms) {
    if (now < *armed_until) { *armed_until = 0; return true; }
    *armed_until = now + window_ms;
    return false;
}

// Armed state of the two-press QUIT GAME confirm. The arm only holds while the
// cursor stays on the row it was armed on and in the same category: observe()
// is called at EVERY place the cursor / category can change (keys, mouse hover
// and click, wheel, category tabs, L/R), so leaving and coming back never
// confirms.
struct QuitArm {
    std::uint32_t until = 0;
    int cursor = -1, sub = -1;
    bool armed(std::uint32_t now) const { return until != 0 && now < until; }
    void observe(int cur, int sub_now) { if (until != 0 && (cur != cursor || sub_now != sub)) until = 0; }
    bool press(std::uint32_t now, int cur, int sub_now, std::uint32_t window_ms) {
        observe(cur, sub_now);
        if (armed(now)) { until = 0; return true; }
        until = now + window_ms; cursor = cur; sub = sub_now;
        return false;
    }
};

// How a mouse event is treated while a key capture is pending (GBA binding or
// fast-forward key): everything is swallowed, a right click cancels.
enum class CaptureMouse { kPass, kBlock, kCancel };
inline CaptureMouse capture_mouse(bool capturing, unsigned pressed) {
    if (!capturing) return CaptureMouse::kPass;
    return (pressed & 2) ? CaptureMouse::kCancel : CaptureMouse::kBlock;
}

// Label of a key binding row: both keys when they fit, "NONE" when the input has no key at all.
inline std::string binding_label(const std::string& first, const std::string& second, std::size_t max_len = 11) {
    if (first.empty() && second.empty()) return "NONE";
    if (first.empty()) return second;
    if (!second.empty() && first.size() + second.size() + 1 <= max_len) return first + " " + second;
    return first;
}

// Volume rows change only from the value area: a click on the label just selects.
inline bool value_area_click(float x, float value_left) { return x >= value_left; }

}  // namespace frlg::ini
