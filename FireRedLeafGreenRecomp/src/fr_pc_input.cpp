// fr_pc_input.cpp — FireRed Remaster PC controls. See fr_pc_input.h.
//
// Structure follows EmeraldRecomp's touch policy (src/touch/): per-frame scene
// classification from read-only guest state plus observe-only function-entry
// probes (game.toml [[mod_function_hook]]), and verified key macros driven
// through the engine KeySynth. Addresses/offsets are FireRed USA rev0
// (pret/pokefirered symbol map), used as a data-format description only.

#include "fr_pc_input.h"

#include <SDL.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

#include "fr_settings_menu.h"
#include "fr_settings_ini.h"
#include "fr_guest.h"
#include "fr_hd_ui.h"
#include "gba_bus.h"
#include "host_window.h"
#include "input_synth.h"
#include "mod_function_hooks.h"
#include "runtime.h"
#include "runtime_arm.h"  // bus_write_* (Infinite Rare Candy)
#include "runtime_bus_bridge.h"
#include "touch_input.h"

namespace frlg {
namespace {

namespace k = gbarecomp;

// ── FireRed USA rev0 ─────────────────────────────────────────────────────
constexpr std::uint32_t kMain               = 0x030030F0;  // callback1 +0, callback2 +4
constexpr std::uint32_t kTasks              = 0x03005090;  // 16 x 0x28
constexpr std::uint32_t kWindows            = 0x020204B4;  // 32 x 12
constexpr std::uint32_t kTextPrinters       = 0x02020034;  // 32 x 0x24
constexpr std::uint32_t kPaletteFade        = 0x02037AB8;
constexpr std::uint32_t kLockFieldControls  = 0x03000F9C;
constexpr std::uint32_t kMenu               = 0x0203ADE4;  // menu.c sMenu (12 bytes)
constexpr std::uint32_t kOptionMenuPtr      = 0x02039620;
constexpr std::uint32_t kBattlerCtrlFuncs   = 0x03004FE0;  // 4 x fn ptr
constexpr std::uint32_t kBattlersCount      = 0x02023BCC;
constexpr std::uint32_t kBattlerPositions   = 0x02023BD6;
constexpr std::uint32_t kActionCursor       = 0x02023FF8;  // [4]
constexpr std::uint32_t kMoveCursor         = 0x02023FFC;  // [4]
constexpr std::uint32_t kPartyMenu          = 0x0203B0A0;
constexpr std::uint32_t kPlayerPartyCount   = 0x02024029;  // gPlayerPartyCount (symbol map is 1 low)
constexpr std::uint32_t kKeypadIconTable    = 0x081EA6CC;  // sKeypadIcons
constexpr std::uint32_t kKeypadIconTiles    = 0x081EA700;  // gKeypadIconTiles (128px wide)
constexpr std::uint32_t kSprites            = 0x0202063C;  // gSprites, 64 x 0x44
constexpr std::uint32_t kNamingScreenPtr    = 0x0203998C;  // sNamingScreen
constexpr std::uint32_t kKeyboardChars      = 0x083E22D0;  // [3 kb][4 rows][8 cols]
constexpr std::uint32_t kPageColumnCounts   = 0x083E2330;  // [3 kb]
constexpr std::uint32_t kPageColumnXPos     = 0x083E2333;  // [3 kb][8] (symbol map is 1 low)
constexpr std::uint32_t kPageToKeyboardId   = 0x083E2351;  // [3 page] (symbol map is 1 low)
constexpr std::uint32_t CB2_NamingScreen    = 0x0809FB70;
constexpr std::uint32_t CB2_PokeStorage     = 0x0808CDC4;
constexpr std::uint32_t kStorageCursorArea  = 0x02039820;
constexpr std::uint32_t kStorageGlobalPtr   = 0x020397B0;  // gStorage
constexpr std::uint32_t Task_PokeStorageMain = 0x0808D2BC;
constexpr std::uint32_t kStorageCursorPos   = 0x02039821;

constexpr std::uint32_t CB1_Overworld           = 0x08056534;
constexpr std::uint32_t CB2_Overworld           = 0x080565B4;
constexpr std::uint32_t BattleMainCB2           = 0x08011100;
constexpr std::uint32_t CB2_TitleScreenRun      = 0x08078B9C;
constexpr std::uint32_t CB2_MainMenu            = 0x0800C2D4;
constexpr std::uint32_t CB2_OptionMenuRun       = 0x0808835C;  // CB2_InitOptionMenu (runs the screen)
constexpr std::uint32_t CB2_BagMenuRun          = 0x08107EE0;
constexpr std::uint32_t CB2_UpdatePartyMenu     = 0x0811EBA0;
constexpr std::uint32_t CB2_RunSummaryScreen    = 0x08137EE8;
constexpr std::uint32_t kSummaryPtr             = 0x0203B140;  // sMonSummaryScreen
constexpr std::uint32_t kSummaryMoveCur         = 0x0203B16D;  // sMoveSelectionCursorPos
constexpr std::uint32_t Task_MainMenuInput      = 0x0800CA54;  // main_menu.c Task_HandleMenuInput
constexpr std::uint32_t HandleInputChooseAction = 0x0802E438;
constexpr std::uint32_t HandleInputChooseMove   = 0x0802EA10;

// Observe-only probes (declared in variants/firered/game.toml).
constexpr std::uint32_t Menu_ProcessInput                    = 0x0810F998;
constexpr std::uint32_t Menu_ProcessInputNoWrapAround        = 0x0810FA04;
constexpr std::uint32_t Menu_ProcessInput_other              = 0x0810FA84;
constexpr std::uint32_t Menu_ProcessInputNoWrapAround_other  = 0x0810FAF0;
constexpr std::uint32_t Menu_ProcessInputNoWrapClearOnChoose = 0x081100C8;
constexpr std::uint32_t Menu_ProcessInputGridLayout          = 0x08110650;
constexpr std::uint32_t Menu_ProcessGridInput_NoSoundLimit   = 0x081105A0;
constexpr std::uint32_t Menu_ProcessGridInputRepeat_NoSound  = 0x081106F4;
constexpr std::uint32_t Menu_ProcessGridInputRepeat          = 0x081107A0;
constexpr std::uint32_t ListMenu_ProcessInput                = 0x08107078;
constexpr std::uint32_t DoCB1_Overworld                      = 0x0805644C;
constexpr std::uint32_t StartCB_HandleInput                  = 0x0806F280;
constexpr std::uint32_t Storage_HandleMenuInput              = 0x08094F94;

// SDL scancodes (stable SDL2 values; this file does not include SDL).
enum Sc : int {
    SC_A = 4, SC_C = 6, SC_D = 7, SC_E = 8, SC_F = 9, SC_Q = 20, SC_S = 22,
    SC_W = 26, SC_Z = 29, SC_RETURN = 40, SC_ESCAPE = 41, SC_BACKSPACE = 42,
    SC_TAB = 43, SC_SPACE = 44, SC_PAGEUP = 75, SC_PAGEDOWN = 78,
    SC_RIGHT = 79, SC_LEFT = 80, SC_DOWN = 81, SC_UP = 82,
    SC_LSHIFT = 225, SC_RSHIFT = 229,
};

// GBA KEYINPUT bit order: A B Select Start Right Left Up Down R L.
const int kPcDefaults[10][k::HostWindow::kKeySlots] = {
    {SC_E, SC_RETURN, SC_SPACE},  // A      confirm
    {SC_Q, SC_BACKSPACE, 0},      // B      back
    {SC_F, 0, 0},                 // Select registered item
    {SC_TAB, 0, 0},               // Start  pause menu
    {SC_D, SC_RIGHT, 0},          // Right
    {SC_A, SC_LEFT, 0},           // Left
    {SC_W, SC_UP, 0},             // Up
    {SC_S, SC_DOWN, 0},           // Down
    {SC_C, SC_PAGEDOWN, 0},       // R
    {SC_Z, SC_PAGEUP, 0},         // L
};

// ── probes ───────────────────────────────────────────────────────────────
enum MenuProbe : unsigned { kProbeWrap = 1, kProbeNoWrap = 2, kProbeGrid = 4 };
unsigned g_menu_probe = 0;
bool g_list_probe = false;
std::uint8_t g_list_task = 0xFF;
bool g_field_probe = false;
bool g_start_probe = false;

struct ProbeSpec { const char* id; std::uint32_t addr; };
const ProbeSpec kProbes[] = {
    {"frlg.pc.menu_input", Menu_ProcessInput},
    {"frlg.pc.menu_input_nowrap", Menu_ProcessInputNoWrapAround},
    {"frlg.pc.menu_input_other", Menu_ProcessInput_other},
    {"frlg.pc.menu_input_nowrap_other", Menu_ProcessInputNoWrapAround_other},
    {"frlg.pc.menu_input_clear", Menu_ProcessInputNoWrapClearOnChoose},
    {"frlg.pc.menu_grid", Menu_ProcessInputGridLayout},
    {"frlg.pc.menu_grid_nosound", Menu_ProcessGridInput_NoSoundLimit},
    {"frlg.pc.menu_grid_repeat_nosound", Menu_ProcessGridInputRepeat_NoSound},
    {"frlg.pc.menu_grid_repeat", Menu_ProcessGridInputRepeat},
    {"frlg.pc.list_input", ListMenu_ProcessInput},
    {"frlg.pc.field_input", DoCB1_Overworld},
    {"frlg.pc.start_menu_input", StartCB_HandleInput},
    {"frlg.pc.storage_menu_input", Storage_HandleMenuInput},
};

int probe_callback(std::uint32_t addr, int /*thumb*/, ArmCpuState* cpu) {
    switch (addr) {
        case ListMenu_ProcessInput:
            g_list_probe = true;
            g_list_task = cpu ? static_cast<std::uint8_t>(cpu->R[0]) : 0xFF;
            break;
        case DoCB1_Overworld: g_field_probe = true; break;
        case StartCB_HandleInput: g_start_probe = true; break;
        case Menu_ProcessInput:
        case Storage_HandleMenuInput:
        case Menu_ProcessInput_other: g_menu_probe |= kProbeWrap; break;
        case Menu_ProcessInputGridLayout:
        case Menu_ProcessGridInput_NoSoundLimit:
        case Menu_ProcessGridInputRepeat_NoSound:
        case Menu_ProcessGridInputRepeat: g_menu_probe |= kProbeGrid; break;
        default: g_menu_probe |= kProbeNoWrap; break;
    }
    return 0;  // observe only: the guest body always runs
}

struct ProbeRegistrar {
    ProbeRegistrar() {
        for (const auto& p : kProbes)
            gba_mod_register_function_entry_plugin(p.id, p.addr, 1, probe_callback);
    }
} g_probe_registrar;

// ── scene ────────────────────────────────────────────────────────────────
int font_height(unsigned id) {
    static const int h[] = {13, 14, 14, 14, 14, 14, 16, 8};
    return id < 8 ? h[id] : 14;
}

// Lists and menus only report themselves (probes) on frames their input
// routine runs; screens skip it for a frame or two after every move while
// they redraw. Keep the last one alive across those gaps so neither the HD
// layer nor the pointer flickers.
struct StickyUi {
    int list_task = -1;
    std::uint32_t list_items = 0;
    int list_age = 0;
    int menu_window = -1;
    unsigned menu_flags = 0;
    bool menu_start = false;
    int menu_age = 0;
} g_sticky;
constexpr int kStickyFrames = 20;

Scene classify(const Mem& m) {
    Scene s;
    s.cb1 = m.func(kMain);
    s.cb2 = m.func(kMain + 4);
    s.field = s.cb2 == CB2_Overworld;
    const bool fading = (m.u8(kPaletteFade + 7) & 0x80) != 0;
    s.field_free = s.field && s.cb1 == CB1_Overworld && g_field_probe &&
                   !m.u8(kLockFieldControls) && !fading;
    // A printer waiting for A/B (the ▼ arrow): RENDER_STATE_WAIT / CLEAR.
    for (int i = 0; i < 32; ++i) {
        const std::uint32_t p = kTextPrinters + i * 0x24;
        const int state = m.u8(p + 0x1C);
        if (m.u8(p + 0x1B) && (state == 1 || state == 2)) { s.text_active = true; break; }
    }
    unsigned menu_probe = g_menu_probe;
    bool start_probe = g_start_probe;
    if (menu_probe || start_probe) {
        g_sticky.menu_window = m.u8(kMenu + 5);
        g_sticky.menu_flags = menu_probe;
        g_sticky.menu_start = start_probe;
        g_sticky.menu_age = 0;
    } else if (g_sticky.menu_window >= 0 && ++g_sticky.menu_age <= kStickyFrames &&
               m.u8(kMenu + 5) == g_sticky.menu_window && window_shown(m, g_sticky.menu_window)) {
        menu_probe = g_sticky.menu_flags;
        start_probe = g_sticky.menu_start;
    } else {
        g_sticky.menu_window = -1;
    }
    if (menu_probe || start_probe) {
        s.menu = true;
        s.menu_grid = (menu_probe & kProbeGrid) != 0;
        s.menu_wrap = start_probe || (menu_probe & kProbeWrap) != 0;
        s.start_menu = start_probe;
        s.menu_left = m.u8(kMenu + 0);
        s.menu_top = m.u8(kMenu + 1);
        s.menu_cursor = m.s8(kMenu + 2);
        s.menu_min = m.s8(kMenu + 3);
        s.menu_max = m.s8(kMenu + 4);
        s.menu_window = m.u8(kMenu + 5);
        s.menu_ow = m.u8(kMenu + 7);
        s.menu_oh = m.u8(kMenu + 8);
        s.menu_cols = s.menu_grid ? std::max<int>(1, m.u8(kMenu + 9)) : 1;
        if (s.menu_oh <= 0) s.menu_oh = 16;
        s.menu_rect = window_rect(m, s.menu_window);
        if (s.menu_rect.empty() || s.menu_max < s.menu_min) s.menu = false;
    }
    int list_task = -1;
    if (g_list_probe && g_list_task < 16) {
        list_task = g_list_task;
        g_sticky.list_task = list_task;
        g_sticky.list_items = m.u32(kTasks + list_task * 0x28 + 8);
        g_sticky.list_age = 0;
    } else if (g_sticky.list_task >= 0 && !s.menu && ++g_sticky.list_age <= kStickyFrames) {
        const std::uint32_t t = kTasks + g_sticky.list_task * 0x28;
        if (m.u8(t + 4) && m.u32(t + 8) == g_sticky.list_items) list_task = g_sticky.list_task;
        else g_sticky.list_task = -1;
    } else if (g_sticky.list_age > kStickyFrames) {
        g_sticky.list_task = -1;
    }
    if (list_task >= 0) {
        const std::uint32_t t = kTasks + list_task * 0x28;
        s.list_task = list_task;
        s.list_total = m.u16(t + 0x14);
        s.list_max = m.u16(t + 0x16);
        const int window = m.u8(t + 0x18);
        s.list_window = window;
        s.list_up = m.u8(t + 0x1C) & 0x0F;
        const int padding = (m.u8(t + 0x1E) >> 3) & 7;
        s.list_row_h = font_height(m.u8(t + 0x1F) & 0x3F) + padding;
        s.list_scroll = m.u16(t + 0x20);
        s.list_sel = m.u16(t + 0x22);
        s.list_rect = window_rect(m, window);
        s.list = !s.list_rect.empty() && s.list_total > 0 && s.list_max > 0;
    }
    if (s.cb2 == BattleMainCB2) {
        const int count = std::min<int>(4, m.u8(kBattlersCount));
        for (int i = 0; i < count; ++i) {
            if (m.u8(kBattlerPositions + i) & 1) continue;  // opponent side
            const std::uint32_t f = m.func(kBattlerCtrlFuncs + 4 * i);
            if (f == HandleInputChooseAction) s.battle = BattleCtl::Action;
            else if (f == HandleInputChooseMove) s.battle = BattleCtl::Move;
            else continue;
            s.battler = i;
            break;
        }
    }
    if (s.cb2 == CB2_MainMenu) {
        for (int i = 0; i < 16; ++i) {
            const std::uint32_t t = kTasks + i * 0x28;
            const std::uint32_t f = m.u8(t + 4) ? m.func(t) : 0;
            // input + the display states around it (cursor redraw, fade in /
            // out): the HD menu stays up instead of flashing the original
            if (f == Task_MainMenuInput || f == 0x0800CA28 || f == 0x0800C9CC || f == 0x0800CA94) {
                s.main_task = i;
                s.main_type = static_cast<std::int16_t>(m.u16(t + 8));
                break;
            }
        }
    }
    if (s.cb2 == CB2_OptionMenuRun) {
        const std::uint32_t o = m.u32(kOptionMenuPtr);
        s.option_page = o >= 0x02000000u && o < 0x0203FF00u &&
                        m.u8(o + 0x10) == 2 && m.u8(o + 0x11) >= 10;
    }
    s.party = s.cb2 == CB2_UpdatePartyMenu;
    g_menu_probe = 0;
    g_list_probe = false;
    g_list_task = 0xFF;
    g_field_probe = false;
    g_start_probe = false;
    return s;
}


// ── verified key macros ──────────────────────────────────────────────────
// Navigate: press the direction `choose` returns until `read` reports the
// target, re-reading after every press; then optionally confirm with A. A
// target the game refuses to reach (an empty move slot, a locked row) ends
// the macro without pressing A.
struct Macro {
    bool active = false;
    std::function<int(const Mem&)> read;
    std::function<std::uint16_t(int, int)> choose;
    int target = 0;
    bool confirm = false;
    std::uint16_t confirm_key = 0;   // key pressed on arrival (default A)
    // Arrived: the confirm key stays down until the game's ReadKeys has seen
    // it (a busy screen can skip input reads for a frame or two, which would
    // swallow a fixed-length tap).
    bool confirming = false;
    int confirm_frames = 0;
    // A navigation press in flight: held until the game's ReadKeys has seen
    // it (phase 0), then released until the release is seen (phase 1).
    std::uint16_t pressing = 0;
    int phase = 0, phase_frames = 0;
    int presses = 0, max_presses = 40;
    int frames = 0, stale = 0;
    std::uint32_t signature = 0;
    const char* label = "";
};

struct Core {
    k::KeySynth synth;
    Macro macro;
    // keyboard context
    bool esc_was = false;
    std::uint16_t esc_keys = 0;      // GBA keys Escape is holding this press
    bool menu_was_open = false;
    // mouse
    int hover_key = -1;              // last hovered item identity
    std::uint32_t hover_sig = 0;
    float last_nx = -1, last_ny = -1;
    bool swallow_left = false;       // left button pressed for the game menu
    // A click that landed while its menu was still opening is retried for
    // a few frames once the menu takes input.
    int pending_click_frames = 0;
    float pending_nx = 0, pending_ny = 0;
} g_core;

Scene g_last_scene;
bool g_auto_run = false;
bool g_mouse_on = true;
bool g_pad_swap_ab = false;     // controller: Nintendo layout
bool g_pad_stick = true;        // controller: left stick = D-pad
bool g_pad_applied = false;
bool g_setup_done = false;      // first-run setup finished
// Fast forward (game-owned: the runtime asks fast_forward_policy each frame)
int g_ff_speed = 3;             // 2..10
int g_ff_key = SDL_SCANCODE_SPACE;
bool g_ff_toggle = true;        // true: press to toggle, false: hold
bool g_ff_latched = false;      // toggle state
bool g_ff_key_was = false;
std::atomic<int> g_ff_now{0};   // multiplier while running fast, else 0
bool g_rare_candy = true;       // Infinite Rare Candy
bool g_diorama = true;          // experimental 3D view (fr_diorama.cpp)
std::string g_config_dir = ".";
std::uint8_t* g_rom = nullptr;
std::size_t g_rom_size = 0;

void macro_cancel() {
    g_core.macro.active = false;
    g_core.synth.clear_taps();
}

void macro_start(const char* label, std::function<int(const Mem&)> read, int target,
                 std::function<std::uint16_t(int, int)> choose, bool confirm,
                 std::uint32_t signature, int max_presses = 40) {
    Macro& mc = g_core.macro;
    g_core.synth.clear_taps();
    mc.active = true;
    mc.read = std::move(read);
    mc.choose = std::move(choose);
    mc.target = target;
    mc.confirm = confirm;
    mc.confirm_key = 0;
    mc.confirming = false;
    mc.confirm_frames = 0;
    mc.pressing = 0;
    mc.phase = 0;
    mc.phase_frames = 0;
    mc.presses = 0;
    mc.max_presses = max_presses;
    mc.frames = 0;
    mc.stale = 0;
    mc.signature = signature;
    mc.label = label;
}

// Returns keys to hold this frame (a confirm press waiting to be seen).
std::uint16_t macro_frame(const Mem& m, const Scene& s) {
    Macro& mc = g_core.macro;
    if (!mc.active) return 0;
    if (mc.confirming) {
        const std::uint16_t key = mc.confirm_key ? mc.confirm_key : k::kGbaKeyA;
        // gMain.heldKeysRaw (+0x28): the game has read the press.
        if ((m.u16(kMain + 0x28) & key) == key || ++mc.confirm_frames > 12) {
            mc.active = false;
            mc.confirming = false;
            return 0;
        }
        return key;
    }
    if (mc.pressing) {
        const bool down = (m.u16(kMain + 0x28) & mc.pressing) != 0;   // gMain.heldKeysRaw
        if (mc.phase == 0) {
            if (!down && ++mc.phase_frames <= 8) return mc.pressing;
            mc.phase = 1;
            mc.phase_frames = 0;
            return 0;
        }
        if (down && ++mc.phase_frames <= 8) return 0;
        mc.pressing = 0;
    }
    if (++mc.frames > 300) { macro_cancel(); return 0; }
    if (!g_core.synth.idle()) return 0;
    // Let each press settle (the game reads input once per frame and some
    // menus redraw for a frame or two) before reading the cursor back.
    // Menus skip their input routine for a frame while they redraw (the
    // Start menu reprints its description): wait those frames out, but give
    // up if the screen really changed.
    if (s.signature() != mc.signature) {
        if (++mc.stale > 20) mc.active = false;
        return 0;
    }
    mc.stale = 0;
    const int cur = mc.read(m);
    if (cur == -9999) { mc.active = false; return 0; }
    if (cur == -9998) return 0;   // the cursor is still gliding: wait for it
    if (cur == mc.target) {
        if (!mc.confirm) { mc.active = false; return 0; }
        mc.confirming = true;
        mc.confirm_frames = 0;
        return mc.confirm_key ? mc.confirm_key : k::kGbaKeyA;
    }
    if (mc.presses >= mc.max_presses) { mc.active = false; return 0; }
    const std::uint16_t key = mc.choose(cur, mc.target);
    if (!key) { mc.active = false; return 0; }
    mc.pressing = key;
    mc.phase = 0;
    mc.phase_frames = 0;
    ++mc.presses;
    return key;
}

std::uint16_t vertical_toward(int cur, int target, bool wrap, int min, int max) {
    if (cur == target) return 0;
    if (!wrap) return target < cur ? k::kGbaKeyUp : k::kGbaKeyDown;
    const int span = max - min + 1;
    const int down = ((target - cur) % span + span) % span;
    return (span - down) < down ? k::kGbaKeyUp : k::kGbaKeyDown;
}

std::uint16_t grid2x2_toward(int cur, int target) {
    if ((cur >> 1) != (target >> 1)) return (target >> 1) < (cur >> 1) ? k::kGbaKeyUp : k::kGbaKeyDown;
    if ((cur & 1) != (target & 1)) return (target & 1) < (cur & 1) ? k::kGbaKeyLeft : k::kGbaKeyRight;
    return 0;
}

// ── mouse: per-scene targets ─────────────────────────────────────────────
// A hit is an item identity + how to reach it. `hover` moves there, `click`
// moves there and confirms.
struct Hit {
    int id = -1;  // < 0 = nothing under the pointer
    std::function<int(const Mem&)> read;
    std::function<std::uint16_t(int, int)> choose;
    int target = 0;
    int max_presses = 40;
    const char* label = "";
    std::uint16_t confirm_key = 0;   // HD targets: key pressed on arrival instead of A
};

Hit hit_menu(const Scene& s, float nx, float ny) {
    Hit h;
    if (!s.menu) return h;
    for (int i = s.menu_min; i <= s.menu_max; ++i) {
        if (!s.menu_item(i).contains(nx, ny, 0.5f)) continue;
        const bool grid = s.menu_grid, wrap = s.menu_wrap;
        const int cols = s.menu_cols, mn = s.menu_min, mx = s.menu_max, window = s.menu_window;
        h.id = 0x1000 + i;
        h.target = i;
        h.label = "menu";
        h.read = [window](const Mem& mm) {
            return mm.u8(kMenu + 5) == window ? static_cast<int>(mm.s8(kMenu + 2)) : -9999;
        };
        h.choose = [grid, cols, wrap, mn, mx](int cur, int t) -> std::uint16_t {
            if (grid && cols > 0) {
                if (cur / cols != t / cols) return t / cols < cur / cols ? k::kGbaKeyUp : k::kGbaKeyDown;
                return t % cols < cur % cols ? k::kGbaKeyLeft : k::kGbaKeyRight;
            }
            return vertical_toward(cur, t, wrap, mn, mx);
        };
        return h;
    }
    return h;
}

Hit hit_list(const Scene& s, float nx, float ny) {
    Hit h;
    if (!s.list) return h;
    const int visible = std::min(s.list_max, s.list_total - s.list_scroll);
    for (int r = 0; r < visible; ++r) {
        if (!s.list_row(r).contains(nx, ny)) continue;
        const int target = s.list_scroll + r;
        const int task = s.list_task;
        h.id = 0x2000 + target;
        h.target = target;
        h.max_presses = 64;
        h.label = "list";
        h.read = [task](const Mem& mm) {
            const std::uint32_t t = kTasks + task * 0x28;
            if (!mm.u8(t + 4)) return -9999;
            return static_cast<int>(mm.u16(t + 0x20) + mm.u16(t + 0x22));
        };
        h.choose = [](int cur, int t) -> std::uint16_t {
            return t < cur ? k::kGbaKeyUp : k::kGbaKeyDown;
        };
        return h;
    }
    return h;
}

Hit hit_battle(const Mem& m, const Scene& s, float nx, float ny) {
    Hit h;
    if (s.battle == BattleCtl::None) return h;
    const int battler = s.battler;
    if (s.battle == BattleCtl::Action) {
        const Rect r = window_rect(m, 2);  // B_WIN_ACTION_MENU (FIGHT BAG / POKéMON RUN)
        if (!r.contains(nx, ny, 2)) return h;
        const int col = nx < r.x + r.w / 2 ? 0 : 1, row = ny < r.y + r.h / 2 ? 0 : 1;
        h.target = row * 2 + col;
        h.id = 0x3000 + h.target;
        h.label = "battle-action";
        h.read = [battler](const Mem& mm) {
            if (mm.func(kBattlerCtrlFuncs + 4 * battler) != HandleInputChooseAction) return -9999;
            return static_cast<int>(mm.u8(kActionCursor + battler));
        };
    } else {
        for (int i = 0; i < 4; ++i) {
            const Rect r = window_rect(m, 3 + i);  // B_WIN_MOVE_NAME_1..4
            if (!r.contains(nx, ny, 3)) continue;
            h.target = i;
            h.id = 0x3100 + i;
            h.label = "battle-move";
            h.read = [battler](const Mem& mm) {
                if (mm.func(kBattlerCtrlFuncs + 4 * battler) != HandleInputChooseMove) return -9999;
                return static_cast<int>(mm.u8(kMoveCursor + battler));
            };
            break;
        }
        if (h.id < 0) return h;
    }
    h.choose = grid2x2_toward;
    h.max_presses = 6;
    return h;
}

Hit hit_main_menu(const Scene& s, float nx, float ny) {
    Hit h;
    if (s.main_task < 0) return h;
    // main_menu.c sWindowTemplate boxes (BG0, unscrolled), frame included.
    struct Box { int y0, y1; };
    std::vector<Box> boxes;
    if (s.main_type == 0) boxes = {{0, 32}};                       // NEW GAME only
    else boxes = {{0, 96}, {96, 128}};                              // CONTINUE, NEW GAME
    if (s.main_type == 2) boxes.push_back({128, 160});              // third box (OPTION)
    for (int i = 0; i < static_cast<int>(boxes.size()); ++i) {
        if (nx < 16 || nx >= 224 || ny < boxes[i].y0 || ny >= boxes[i].y1) continue;
        const int task = s.main_task;
        h.id = 0x4000 + i;
        h.target = i;
        h.label = "main-menu";
        h.read = [task](const Mem& mm) {
            const std::uint32_t t = kTasks + task * 0x28;
            if (!mm.u8(t + 4) || mm.func(t) != Task_MainMenuInput) return -9999;
            return static_cast<int>(static_cast<std::int16_t>(mm.u16(t + 10)));
        };
        h.choose = [](int cur, int t) -> std::uint16_t {
            return t < cur ? k::kGbaKeyUp : t > cur ? k::kGbaKeyDown : 0;
        };
        h.max_presses = 4;
        return h;
    }
    return h;
}

// OPTION page 1 rows (option_menu.c): 13 px apart from y = 58.
Hit hit_option_page(const Scene& s, float nx, float ny) {
    Hit h;
    if (!s.option_page || nx < 16 || nx >= 224 || ny < 56) return h;
    const int row = static_cast<int>((ny - 58) / 13);
    if (row < 0 || row > 6) return h;
    h.id = 0x5000 + row;
    h.target = row;
    h.label = "option";
    h.read = [](const Mem& mm) {
        const std::uint32_t o = mm.u32(kOptionMenuPtr);
        if (o < 0x02000000u || o >= 0x0203FF00u || mm.u8(o + 0x10) != 2) return -9999;
        return static_cast<int>(mm.u16(o + 0x0E));
    };
    h.choose = [](int cur, int t) -> std::uint16_t {
        return t < cur ? k::kGbaKeyUp : t > cur ? k::kGbaKeyDown : 0;
    };
    h.max_presses = 8;
    return h;
}

// Party slots are windows 0..5; CANCEL sits bottom-right (slot id 7).
Hit hit_party(const Mem& m, const Scene& s, float nx, float ny) {
    Hit h;
    if (!s.party || s.menu || s.list) return h;
    const int count = std::min<int>(6, m.u8(kPlayerPartyCount));
    int target = -1;
    for (int i = 0; i < count && target < 0; ++i)
        if (window_rect(m, i).contains(nx, ny, 1)) target = i;
    if (target < 0 && nx >= 176 && ny >= 140) target = 7;
    if (target < 0) return h;
    h.id = 0x6000 + target;
    h.target = target;
    h.label = "party";
    h.read = [](const Mem& mm) {
        if ((mm.func(kMain + 4)) != CB2_UpdatePartyMenu) return -9999;
        const int action = mm.u8(kPartyMenu + 11);
        const std::uint32_t slot = (action == 8 || action == 9) ? kPartyMenu + 10 : kPartyMenu + 9;
        return static_cast<int>(mm.s8(slot));
    };
    // Up/Down walk the slots in order (…, last Pokémon, CANCEL): short way.
    h.choose = [count](int cur, int t) -> std::uint16_t {
        auto pos = [count](int v) { return v == 7 ? count : v; };
        if (pos(t) == pos(cur)) return 0;
        return pos(t) < pos(cur) ? k::kGbaKeyUp : k::kGbaKeyDown;
    };
    h.max_presses = 9;
    return h;
}

Hit hit_naming(const Mem& m, const Scene& s, float nx, float ny);
int naming_page_columns(const Mem& m, const Scene& s);   // -1 when the keyboard is not live
int naming_cursor(const Mem& m);
std::uint16_t naming_toward(int cur, int target, int columns);

float g_view_margin = 0;

// HD layout targets → the same verified macros the native layout uses.
Hit hit_from_hd(const Mem& m, const Scene& s, const HdHit& hd) {
    Hit h;
    switch (hd.kind) {
        case kHdHitMenu: {
            if (!s.menu || hd.index < s.menu_min || hd.index > s.menu_max) return h;
            const bool grid = s.menu_grid, wrap = s.menu_wrap;
            const int cols = s.menu_cols, mn = s.menu_min, mx = s.menu_max, window = s.menu_window;
            h.id = 0x1000 + hd.index;
            h.target = hd.index;
            h.label = "menu";
            h.read = [window](const Mem& mm) {
                return mm.u8(kMenu + 5) == window ? static_cast<int>(mm.s8(kMenu + 2)) : -9999;
            };
            h.choose = [grid, cols, wrap, mn, mx](int cur, int t) -> std::uint16_t {
                if (grid && cols > 0) {
                    if (cur / cols != t / cols) return t / cols < cur / cols ? k::kGbaKeyUp : k::kGbaKeyDown;
                    return t % cols < cur % cols ? k::kGbaKeyLeft : k::kGbaKeyRight;
                }
                return vertical_toward(cur, t, wrap, mn, mx);
            };
            return h;
        }
        case kHdHitList: {
            if (!s.list) return h;
            const int task = s.list_task;
            h.id = 0x2000 + hd.index;
            h.target = hd.index;
            h.max_presses = 64;
            h.label = "list";
            h.read = [task](const Mem& mm) {
                const std::uint32_t t = kTasks + task * 0x28;
                if (!mm.u8(t + 4)) return -9999;
                return static_cast<int>(mm.u16(t + 0x20) + mm.u16(t + 0x22));
            };
            h.choose = [](int cur, int t) -> std::uint16_t { return t < cur ? k::kGbaKeyUp : k::kGbaKeyDown; };
            return h;
        }
        case kHdHitBattleAction:
        case kHdHitBattleMove: {
            const bool action = hd.kind == kHdHitBattleAction;
            if (s.battle != (action ? BattleCtl::Action : BattleCtl::Move)) return h;
            const int battler = s.battler;
            h.target = hd.index;
            h.id = (action ? 0x3000 : 0x3100) + hd.index;
            h.label = action ? "battle-action" : "battle-move";
            h.read = [battler, action](const Mem& mm) {
                const std::uint32_t f = mm.func(kBattlerCtrlFuncs + 4 * battler);
                if (f != (action ? HandleInputChooseAction : HandleInputChooseMove)) return -9999;
                return static_cast<int>(mm.u8((action ? kActionCursor : kMoveCursor) + battler));
            };
            h.choose = grid2x2_toward;
            h.max_presses = 6;
            return h;
        }
        case kHdHitParty: {
            if (!s.party || s.menu || s.list) return h;
            h.id = 0x6000 + hd.index;
            h.target = hd.index;
            h.label = "party";
            h.read = [](const Mem& mm) {
                if ((mm.func(kMain + 4)) != CB2_UpdatePartyMenu) return -9999;
                const int action = mm.u8(kPartyMenu + 11);
                const std::uint32_t slot = (action == 8 || action == 9) ? kPartyMenu + 10 : kPartyMenu + 9;
                return static_cast<int>(mm.s8(slot));
            };
            // Up/Down walk the slots in order (…, last Pokémon, CANCEL): take
            // the short way instead of cycling through every slot.
            const int count = std::clamp<int>(m.u8(kPlayerPartyCount), 1, 6);
            h.choose = [count](int cur, int t) -> std::uint16_t {
                auto pos = [count](int v) { return v == 7 ? count : v; };
                if (pos(t) == pos(cur)) return 0;
                return pos(t) < pos(cur) ? k::kGbaKeyUp : k::kGbaKeyDown;
            };
            h.max_presses = 9;
            return h;
        }
        case kHdHitMainMenu: {
            if (s.main_task < 0) return h;
            const int task = s.main_task;
            h.id = 0x4000 + hd.index;
            h.target = hd.index;
            h.label = "main-menu";
            h.read = [task](const Mem& mm) {
                const std::uint32_t t = kTasks + task * 0x28;
                if (!mm.u8(t + 4) || mm.func(t) != Task_MainMenuInput) return -9999;
                return static_cast<int>(static_cast<std::int16_t>(mm.u16(t + 10)));
            };
            h.choose = [](int cur, int t) -> std::uint16_t {
                return t < cur ? k::kGbaKeyUp : t > cur ? k::kGbaKeyDown : 0;
            };
            h.max_presses = 4;
            return h;
        }
        case kHdHitOption: {
            if (!s.option_page) return h;
            h.id = 0x5000 + hd.index;
            h.target = hd.index;
            h.label = "option";
            h.read = [](const Mem& mm) {
                const std::uint32_t o = mm.u32(kOptionMenuPtr);
                if (o < 0x02000000u || o >= 0x0203FF00u || mm.u8(o + 0x10) != 2) return -9999;
                return static_cast<int>(mm.u16(o + 0x0E));
            };
            h.choose = [](int cur, int t) -> std::uint16_t {
                return t < cur ? k::kGbaKeyUp : t > cur ? k::kGbaKeyDown : 0;
            };
            h.max_presses = 8;
            return h;
        }
        case kHdHitKey:
            h.id = 0x8000 + hd.index;
            h.target = hd.index;
            h.label = "key";
            return h;
        case kHdHitAdvance:
            if (s.menu || s.list) return h;
            h.id = 0x8100;
            h.label = "advance";
            return h;
        case kHdHitBoxCell:
        case kHdHitBoxScroll: {
            if (s.cb2 != CB2_PokeStorage) return h;
            const bool scroll = hd.kind == kHdHitBoxScroll;
            h.id = (scroll ? 0x8400 : 0x8300) + hd.index;
            h.target = scroll ? 2 * 64 : hd.index;
            h.label = scroll ? "box-scroll" : "box";
            h.max_presses = 24;
            if (scroll) h.confirm_key = hd.index ? k::kGbaKeyRight : k::kGbaKeyLeft;
            h.read = [](const Mem& mm) {
                if (mm.func(kMain + 4) != CB2_PokeStorage) return -9999;
                // gStorage->state != 0 in Task_PokeStorageMain: the cursor is
                // still gliding (or a box scrolls) and input is ignored.
                const std::uint32_t st = mm.u32(kStorageGlobalPtr);
                if (mm.valid(st, 8)) {
                    const int task = mm.u8(st + 4);
                    if (task < 16 && mm.func(kTasks + task * 0x28) == Task_PokeStorageMain && mm.u8(st) != 0)
                        return -9998;
                }
                return mm.s8(kStorageCursorArea) * 64 + mm.s8(kStorageCursorPos);
            };
            h.choose = [](int cur, int t) -> std::uint16_t {
                const int ca = cur >> 6, cp = cur & 63, ta = t >> 6, tp = t & 63;
                if (ca == 1) {                                   // party list
                    if (ta == 1) return tp < cp ? k::kGbaKeyUp : k::kGbaKeyDown;
                    return cp == 0 ? k::kGbaKeyRight : k::kGbaKeyLeft;  // leave via slot 0 → box
                }
                if (ta == 1) return 0;                           // the party opens with its button
                if (ca == ta) {
                    if (ca == 0) {
                        const int cr = cp / 6, cc = cp % 6, tr = tp / 6, tc = tp % 6;
                        if (cr != tr) return tr < cr ? k::kGbaKeyUp : k::kGbaKeyDown;
                        const int right = (tc - cc + 6) % 6;
                        return right <= 3 ? k::kGbaKeyRight : k::kGbaKeyLeft;
                    }
                    if (ca == 3) return k::kGbaKeyRight;         // two buttons: toggle
                    return 0;
                }
                if (ca == 0) return ta == 2 ? k::kGbaKeyUp : k::kGbaKeyDown;
                if (ca == 2) return ta == 3 ? k::kGbaKeyUp : k::kGbaKeyDown;
                if (ca == 3) return ta == 2 ? k::kGbaKeyDown : k::kGbaKeyUp;
                return 0;
            };
            return h;
        }
        case kHdHitMapCell: {
            if (s.cb2 != rmap::CB2_RegionMap) return h;
            h.id = 0x8500 + hd.index;
            h.target = hd.index;
            h.label = "map";
            h.max_presses = 48;
            h.read = [](const Mem& mm) { return rmap::cursor(mm); };
            h.choose = [](int cur, int t) -> std::uint16_t {
                const int cx = cur & 31, cy = cur >> 5, tx = t & 31, ty = t >> 5;
                const int dx = tx - cx, dy = ty - cy;
                if (dx == 0 && dy == 0) return 0;
                if (std::abs(dx) >= std::abs(dy)) return dx < 0 ? k::kGbaKeyLeft : k::kGbaKeyRight;
                return dy < 0 ? k::kGbaKeyUp : k::kGbaKeyDown;
            };
            return h;
        }
        case kHdHitSummaryMove: {
            if (s.cb2 != CB2_RunSummaryScreen) return h;
            h.id = 0x8600 + hd.index;
            h.target = hd.index;
            h.label = "summary-move";
            h.max_presses = 12;
            h.read = [](const Mem& mm) {
                const std::uint32_t p = mm.u32(kSummaryPtr);
                if (mm.func(kMain + 4) != CB2_RunSummaryScreen || !mm.valid(p + 0x3290, 4)) return -9999;
                if (mm.u8(p + 0x3214) != 3) return -9999;            // not on the move list
                // input handler idle: state 0 (normal) / 2 (learning, forgetting)
                const int mode = mm.u8(p + 0x3208);
                if (mm.u8(p + 0x3288) != ((mode == 2 || mode == 3) ? 2 : 0)) return -9998;
                return static_cast<int>(mm.u8(kSummaryMoveCur));
            };
            h.choose = [](int cur, int t) -> std::uint16_t {
                return t < cur ? k::kGbaKeyUp : t > cur ? k::kGbaKeyDown : 0;
            };
            return h;
        }
        case kHdHitNamingKey: {
            if (s.cb2 != CB2_NamingScreen) return h;
            if (hd.index >= 0xFD) {   // side buttons: plain presses
                h.id = 0x8700 + hd.index;
                if (hd.index == 0xFF) { h.label = "naming-ok"; return h; }
                h.target = hd.index == 0xFE ? 2 : 1;   // Select cycles pages, B deletes
                h.label = "key";
                return h;
            }
            const int columns = naming_page_columns(m, s);
            if (columns <= 0) return h;
            h.id = 0x7000 + hd.index;
            h.target = hd.index;
            h.label = "naming-key";
            h.read = naming_cursor;
            h.choose = [columns](int c, int t) { return naming_toward(c, t, columns); };
            h.max_presses = 16;
            return h;
        }
        case kHdHitOptionsTab:
            h.id = 0x8200 + hd.index;
            h.target = hd.index;
            h.label = "tab";
            return h;
        default: return h;
    }
}

Hit hit_scene(const Mem& m, const Scene& s, float nx, float ny) {
    if (hd_ui_owns_pointer()) {
        HdHit hd;
        if (hd_ui_hit(nx + g_view_margin, ny, &hd)) return hit_from_hd(m, s, hd);
        Hit none;
        return none;
    }
    Hit h = hit_menu(s, nx, ny);
    if (h.id < 0) h = hit_list(s, nx, ny);
    if (h.id < 0 && !s.menu && !s.list) {
        h = hit_battle(m, s, nx, ny);
        if (h.id < 0) h = hit_main_menu(s, nx, ny);
        if (h.id < 0) h = hit_option_page(s, nx, ny);
        if (h.id < 0) h = hit_party(m, s, nx, ny);
        if (h.id < 0) h = hit_naming(m, s, nx, ny);
    }
    return h;
}

void naming_request_ok();

bool scene_has_controls(const Scene& s) {
    return s.menu || s.list || s.battle != BattleCtl::None || s.main_task >= 0 ||
           s.option_page || s.party || s.cb2 == CB2_NamingScreen;
}

// list_menu.c ListMenuUpdateSelectedRowIndexAndScrollOffset: with more
// items below, the cursor only walks down to row ceil(max/2) before the list
// scrolls instead (and up to row max - ceil(max/2) - 1 with items above).
bool list_hover_reachable(const Scene& s, int target) {
    if (!s.list) return false;
    const int max = std::max(1, s.list_max), total = s.list_total;
    const int r = target - s.list_scroll, sel = s.list_sel;
    if (r < 0 || r >= max) return false;
    if (r == sel) return true;
    const int half = max == 1 ? 0 : (max / 2 + max % 2);
    if (r > sel) return s.list_scroll >= total - max || r <= half;
    return s.list_scroll == 0 || r >= (max == 1 ? 0 : max - half - 1);
}

void handle_mouse(const Mem& m, const Scene& s, k::HostWindow* w, std::uint16_t* hold) {
    k::HostWindow::MouseState ms = w->take_mouse();
    if (!g_mouse_on) return;
    const float nx = ms.view_x - static_cast<float>(ms.margin_left);
    g_view_margin = static_cast<float>(ms.margin_left);
    const float ny = ms.view_y;

    // The game-drawn settings pages own the pointer while they are open.
    if (w->game_menu_open()) {
        if (settings_menu_uses_hd()) {
            // HD settings page: rows / value segments / tabs from the HD layout
            int row = -1, dir = 0;
            HdHit hd;
            unsigned pressed = ms.pressed;
            if (ms.inside && hd_ui_hit(ms.view_x, ms.view_y, &hd)) {
                if (hd.kind == kHdHitSetting) { row = hd.index & 0xFF; dir = ((hd.index >> 8) & 3) - 1; }
                else if (hd.kind == kHdHitOptionsTab && hd.index == 0 && (pressed & 1)) { settings_hd_page1(); pressed = 0; }
                else if (hd.kind == kHdHitKey && hd.index == 1 && (pressed & 1)) pressed = 2;  // "Back" hint
                else if (hd.kind == kHdHitSettingsCat && (pressed & 1)) { settings_hd_category(hd.index); pressed = 0; }
                else if (hd.kind == kHdHitKey && (hd.index == 8 || hd.index == 9) && (pressed & 1)) {
                    settings_hd_category_step(hd.index == 9 ? -1 : +1);   // "Category" hint (L / R)
                    pressed = 0;
                }
            }
            settings_hd_mouse(row, dir, ms.moved, pressed, ms.wheel);
        } else {
            settings_menu_mouse(nx, ny, ms.inside, ms.moved, ms.pressed, ms.wheel);
        }
        g_core.hover_key = -1;
        if (ms.pressed & 1) g_core.swallow_left = true;
        return;
    }
    if (g_core.swallow_left) {
        if (!(ms.buttons & 1)) g_core.swallow_left = false;
        ms.pressed &= ~1u;
    }
    (void)hold;

    // Right-click: back out — or open the pause menu while walking.
    if (ms.pressed & 2) {
        macro_cancel();
        g_core.synth.tap(s.field_free ? k::kGbaKeyStart : k::kGbaKeyB, 2, 2);
        return;
    }
    // Wheel: step the live cursor (lists scroll as it moves).
    if (ms.wheel != 0 && (s.menu || s.list || s.option_page || s.party || s.main_task >= 0)) {
        macro_cancel();
        const int n = std::min(8, std::abs(ms.wheel));
        for (int i = 0; i < n; ++i)
            g_core.synth.tap(ms.wheel > 0 ? k::kGbaKeyUp : k::kGbaKeyDown, 2, 2);
        g_core.hover_key = -1;
        return;
    }
    if (!ms.inside) {
        if (ms.pressed & 1) { /* clicks outside the picture do nothing */ }
        return;
    }
    const Hit hit = hit_scene(m, s, nx, ny);
    const std::uint32_t sig = s.signature();
    if (!(ms.pressed & 1) && g_core.pending_click_frames > 0) {
        --g_core.pending_click_frames;
        const Hit late = hit_scene(m, s, g_core.pending_nx, g_core.pending_ny);
        if (late.id >= 0 && late.read) {
            g_core.pending_click_frames = 0;
            macro_start(late.label, late.read, late.target, late.choose, true, sig,
                        late.max_presses);
            g_core.macro.confirm_key = late.confirm_key;
            g_core.hover_key = late.id;
            g_core.hover_sig = sig;
            return;
        }
    }
    if (ms.pressed & 1) {
        g_core.pending_click_frames = 0;
        if (hit.id >= 0 && !hit.read) {
            // HD buttons that are plain key presses (page tabs, advance text).
            if (std::strcmp(hit.label, "key") == 0) g_core.synth.tap(static_cast<std::uint16_t>(1u << hit.target), 2, 2);
            else if (std::strcmp(hit.label, "advance") == 0 && !s.field_free) g_core.synth.tap(k::kGbaKeyA, 2, 2);
            else if (std::strcmp(hit.label, "tab") == 0 && hit.target == 1) settings_menu_open_page2();
            else if (std::strcmp(hit.label, "naming-ok") == 0) naming_request_ok();
            return;
        }
        if (hit.id >= 0) {
            macro_start(hit.label, hit.read, hit.target, hit.choose, true, sig, hit.max_presses);
            g_core.macro.confirm_key = hit.confirm_key;
            // box arrows: already on the title → just press the direction
            if (hit.confirm_key && hit.read(m) == hit.target) { macro_cancel(); g_core.synth.tap(hit.confirm_key, 2, 2); }
            g_core.hover_key = hit.id;
            g_core.hover_sig = sig;
            return;
        }
        // Page flips on full-screen views.
        // Bag: the pocket name and the backpack (with its ◀ ▶ arrows) flip
        // pockets — left half back, right half forward.
        if (s.cb2 == CB2_BagMenuRun && s.list && ny < 104 && nx < 104) {
            g_core.synth.tap(nx < 52 ? k::kGbaKeyLeft : k::kGbaKeyRight, 2, 2);
            return;
        }
        if (s.cb2 == CB2_RunSummaryScreen && !s.menu) {
            if (nx < 60 || nx >= 180) {
                g_core.synth.tap(nx < 60 ? k::kGbaKeyLeft : k::kGbaKeyRight, 2, 2);
                return;
            }
        }
        if (s.cb2 == CB2_TitleScreenRun) { g_core.synth.tap(k::kGbaKeyA, 2, 2); return; }
        // Naming screen buttons on the right: page swap, BACK, OK.
        if (s.cb2 == CB2_NamingScreen && nx >= 186 && ny >= 72) {
            if (ny < 98) g_core.synth.tap(k::kGbaKeySelect, 2, 2);
            else if (ny < 124) g_core.synth.tap(k::kGbaKeyB, 2, 2);
            else naming_request_ok();
            return;
        }
        // OPTION page 1: the "1/2" tag (or the hint bar) opens page 2.
        if (s.option_page && ((ny >= 20 && ny < 44 && nx >= 176) || ny < 16)) {
            settings_menu_open_page2();
            return;
        }
        // Anywhere else: advance text (never on the free overworld, and never
        // where a click could pick an option the pointer is not on).
        if (!s.field_free && !scene_has_controls(s) && s.text_active) {
            g_core.synth.tap(k::kGbaKeyA, 2, 2);
            return;
        }
        if (!s.field_free) {
            g_core.pending_click_frames = 20;
            g_core.pending_nx = nx;
            g_core.pending_ny = ny;
        }
        return;
    }
    // Hover: follow the pointer, but only when it actually moves onto a new
    // item — a resting mouse never fights the keyboard — and never so far
    // that a list would scroll (that moves another row under the pointer and
    // runs away); scrolling is the wheel's job.
    if (ms.moved && hit.id >= 0 && hit.read && !hit.confirm_key &&
        std::strcmp(hit.label, "list") == 0 && !list_hover_reachable(s, hit.target)) {
        g_core.hover_key = hit.id;
        g_core.hover_sig = sig;
        return;
    }
    if (ms.moved && hit.id >= 0 && hit.read && !hit.confirm_key &&
        (hit.id != g_core.hover_key || sig != g_core.hover_sig)) {
        g_core.hover_key = hit.id;
        g_core.hover_sig = sig;
        if (hit.read(m) != hit.target)
            macro_start(hit.label, hit.read, hit.target, hit.choose, false, sig, hit.max_presses);
    } else if (ms.moved && hit.id < 0) {
        g_core.hover_key = -1;
    }
}

// ── naming screen: type with the keyboard, click the on-screen keys ──────
// naming_screen.c: sNamingScreen->state (+0x1E10, 2 = STATE_HANDLE_INPUT),
// currentPage (+0x1E22: 0 symbols, 1 upper, 2 lower; Select cycles them),
// cursorSpriteId (+0x1E23) whose sprite data[0]/[1] are the key column/row
// (column == the page's column count is the PAGE/BACK/OK button column).
struct Naming {
    bool active = false;
    std::uint32_t ptr = 0;
    int state = 0, page = 0, cursor_sprite = 0;
};

bool naming_tables_ok(const Mem& m) {
    // sPageColumnCounts {8,8,6}, sPageToKeyboardId {2,1,0}, "abcdef" row.
    return m.u8(kPageColumnCounts) == 8 && m.u8(kPageColumnCounts + 2) == 6 &&
           m.u8(kPageToKeyboardId) == 2 && m.u8(kPageToKeyboardId + 1) == 1 &&
           m.u8(kPageToKeyboardId + 2) == 0 && m.u8(kKeyboardChars) == 0xD5 &&
           m.u8(kPageColumnXPos + 7) == 123;
}

Naming read_naming(const Mem& m, const Scene& s) {
    Naming n;
    if (s.cb2 != CB2_NamingScreen || !naming_tables_ok(m)) return n;
    n.ptr = m.u32(kNamingScreenPtr);
    if (n.ptr < 0x02000000u || n.ptr >= 0x0203E000u) return n;
    n.state = m.u8(n.ptr + 0x1E10);
    n.page = m.u8(n.ptr + 0x1E22);
    n.cursor_sprite = m.u8(n.ptr + 0x1E23);
    n.active = n.page < 3 && n.cursor_sprite < 64;
    return n;
}

int naming_columns(const Mem& m, int page) {
    return m.u8(kPageColumnCounts + m.u8(kPageToKeyboardId + page));
}

int naming_page_columns(const Mem& m, const Scene& s) {
    const Naming n = read_naming(m, s);
    return n.active ? naming_columns(m, n.page) : -1;
}

// Cursor (col, row) packed as row * 16 + col.
int naming_cursor(const Mem& m) {
    const std::uint32_t p = m.u32(kNamingScreenPtr);
    if ((m.func(kMain + 4)) != CB2_NamingScreen || p < 0x02000000u || p >= 0x0203E000u)
        return -9999;
    const int id = m.u8(p + 0x1E23);
    if (id >= 64) return -9999;
    const std::uint32_t spr = kSprites + id * 0x44;
    const int x = static_cast<std::int16_t>(m.u16(spr + 0x2E));
    const int y = static_cast<std::int16_t>(m.u16(spr + 0x30));
    return y * 16 + x;
}

// Game charset code for a typed character (0xFF = not typeable here).
std::uint8_t game_char(std::uint32_t cp) {
    if (cp >= 'a' && cp <= 'z') return static_cast<std::uint8_t>(0xD5 + (cp - 'a'));
    if (cp >= 'A' && cp <= 'Z') return static_cast<std::uint8_t>(0xBB + (cp - 'A'));
    if (cp >= '0' && cp <= '9') return static_cast<std::uint8_t>(0xA1 + (cp - '0'));
    switch (cp) {
        case ' ': return 0x00;
        case '!': return 0xAB;
        case '?': return 0xAC;
        case '.': return 0xAD;
        case '-': return 0xAE;
        case ',': return 0xB8;
        case '/': return 0xBA;
        case '\'': return 0xB4;
        case '"': return 0xB1;
        case 0x2026: return 0xB0;  // …
        case 0x2642: return 0xB5;  // ♂
        case 0x2640: return 0xB6;  // ♀
        default: return 0xFF;
    }
}

// Find a character on the three keyboards: page, row, column.
bool naming_find(const Mem& m, std::uint8_t ch, int prefer_page, int* page, int* row, int* col) {
    const int order[3] = {prefer_page, (prefer_page + 1) % 3, (prefer_page + 2) % 3};
    for (int pi = 0; pi < 3; ++pi) {
        const int pg = order[pi];
        const int kb = m.u8(kPageToKeyboardId + pg);
        const int cols = m.u8(kPageColumnCounts + kb);
        for (int r = 0; r < 4; ++r)
            for (int c = 0; c < cols && c < 8; ++c) {
                const std::uint8_t v = m.u8(kKeyboardChars + kb * 32 + r * 8 + c);
                // A 0x00 cell is a space key only in its real slot (letters,
                // rows 0-1, column 6); elsewhere it is row padding.
                if (v == 0x00 && !(ch == 0x00 && kb != 2 && r < 2 && c == 6)) continue;
                if (v != ch) continue;
                *page = pg; *row = r; *col = c;
                return true;
            }
    }
    return false;
}

std::uint16_t naming_toward(int cur, int target, int columns) {
    const int cx = cur & 15, cy = cur >> 4, tx = target & 15, ty = target >> 4;
    if (cx >= columns) return k::kGbaKeyLeft;  // off the button column first
    if (cy != ty) {
        const int down = ((ty - cy) % 4 + 4) % 4;
        return down <= 2 ? k::kGbaKeyDown : k::kGbaKeyUp;
    }
    if (cx != tx) return tx < cx ? k::kGbaKeyLeft : k::kGbaKeyRight;
    return 0;
}

struct NamingInput {
    std::vector<std::uint8_t> queue;  // game chars still to type
    bool want_ok = false;             // Enter: move to OK, then press it
    int ok_wait = 0;
    int cooldown = 0;                 // frames to let the keyboard settle
    bool text_on = false;
} g_naming;

void naming_request_ok() {
    g_naming.want_ok = true;
    g_naming.ok_wait = 0;
    g_naming.queue.clear();
}

std::vector<std::uint32_t> utf8_codepoints(const std::string& s) {
    std::vector<std::uint32_t> out;
    for (std::size_t i = 0; i < s.size();) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        std::uint32_t cp = c;
        int n = 1;
        if (c >= 0xF0) { cp = c & 7; n = 4; }
        else if (c >= 0xE0) { cp = c & 15; n = 3; }
        else if (c >= 0xC0) { cp = c & 31; n = 2; }
        for (int k = 1; k < n && i + k < s.size(); ++k)
            cp = (cp << 6) | (static_cast<unsigned char>(s[i + k]) & 63);
        out.push_back(cp);
        i += n;
    }
    return out;
}

void naming_frame(const Mem& m, const Scene& s, k::HostWindow* w) {
    const Naming n = read_naming(m, s);
    if (n.active != g_naming.text_on) {
        w->set_text_input(n.active);
        g_naming = {};
        g_naming.text_on = n.active;
    }
    if (!n.active) return;
    for (std::uint32_t cp : utf8_codepoints(w->take_text())) {
        const std::uint8_t ch = game_char(cp);
        if (ch != 0xFF && g_naming.queue.size() < 16) g_naming.queue.push_back(ch);
    }
    static bool enter_was = false;
    const bool enter = w->key_down(40) || w->key_down(88);  // Return / keypad Enter
    if (enter && !enter_was) naming_request_ok();
    enter_was = enter;
    if (g_core.macro.active || !g_core.synth.idle() || n.state != 2) {
        g_naming.cooldown = std::max(g_naming.cooldown, 4);
        return;
    }
    // The page-swap animation and each key press keep input disabled for a
    // few frames after the screen reports it is handling input again.
    if (g_naming.cooldown > 0) { --g_naming.cooldown; return; }
    const int columns = naming_columns(m, n.page);
    if (g_naming.want_ok) {
        // Start jumps the cursor to OK; A presses it once it is there.
        const int cur = naming_cursor(m);
        if (cur != -9999 && (cur & 15) >= columns && (cur >> 4) == 2) {
            g_core.synth.tap(k::kGbaKeyA, 2, 2);
            g_naming.want_ok = false;
        } else if (++g_naming.ok_wait % 8 == 1) {
            g_core.synth.tap(k::kGbaKeyStart, 2, 2);
        }
        if (g_naming.ok_wait > 60) g_naming.want_ok = false;
        return;
    }
    if (g_naming.queue.empty()) return;
    // A full name takes no more letters: drop the rest instead of paging.
    const std::uint32_t tmpl = m.u32(n.ptr + 0x1E28);
    const int max_chars = m.u8(tmpl + 1);
    int len = 0;
    while (len < 16 && m.u8(n.ptr + 0x1800 + len) != 0xFF) ++len;
    if (max_chars > 0 && len >= max_chars) { g_naming.queue.clear(); return; }
    int page = 0, row = 0, col = 0;
    if (!naming_find(m, g_naming.queue.front(), n.page, &page, &row, &col)) {
        g_naming.queue.erase(g_naming.queue.begin());
        return;
    }
    if (page != n.page) {
        g_core.synth.tap(k::kGbaKeySelect, 2, 2);  // cycle keyboard pages
        g_naming.cooldown = 20;
        return;
    }
    g_naming.queue.erase(g_naming.queue.begin());
    macro_start("naming:type", naming_cursor, row * 16 + col,
                [columns](int cur, int t) { return naming_toward(cur, t, columns); },
                true, s.signature(), 16);
}

// Mouse over the on-screen keyboard: the cursor sprite's live position and
// the page's column table give the key grid.
Hit hit_naming(const Mem& m, const Scene& s, float nx, float ny) {
    Hit h;
    const Naming n = read_naming(m, s);
    if (!n.active || n.state != 2) return h;
    const int cur = naming_cursor(m);
    if (cur == -9999) return h;
    const int columns = naming_columns(m, n.page);
    const int kb = m.u8(kPageToKeyboardId + n.page);
    const int cx = cur & 15, cy = cur >> 4;
    if (cx >= columns) return h;  // geometry comes from a key position
    const std::uint32_t spr = kSprites + n.cursor_sprite * 0x44;
    const int sx = static_cast<std::int16_t>(m.u16(spr + 0x20));
    const int sy = static_cast<std::int16_t>(m.u16(spr + 0x22));
    const int base_x = sx - m.u8(kPageColumnXPos + kb * 8 + cx);
    const int base_y = sy - cy * 16;
    const int row = static_cast<int>(std::floor((ny - base_y + 8) / 16.0f));
    if (row < 0 || row > 3) return h;
    int best = -1, best_d = 1000;
    for (int c = 0; c < columns; ++c) {
        const int d = std::abs(static_cast<int>(nx) - (base_x + m.u8(kPageColumnXPos + kb * 8 + c)));
        if (d < best_d) { best_d = d; best = c; }
    }
    if (best < 0 || best_d > 14) return h;
    h.id = 0x7000 + row * 16 + best;
    h.target = row * 16 + best;
    h.label = "naming-key";
    h.read = naming_cursor;
    h.choose = [columns](int c, int t) { return naming_toward(c, t, columns); };
    h.max_presses = 16;
    return h;
}

// ── keyboard context: Escape and Shift ───────────────────────────────────
std::uint16_t keyboard_context(const Scene& s, k::HostWindow* w) {
    const bool menu_open = w->game_menu_open();
    const bool esc = w->key_down(SC_ESCAPE);
    std::uint16_t held = 0;
    if (menu_open || g_core.menu_was_open) {
        // The settings pages consume Escape themselves; whatever is held
        // when they close stays inert until released.
        g_core.esc_keys = 0;
        g_core.esc_was = esc;
        if (!menu_open && !esc) g_core.menu_was_open = false;
        else g_core.menu_was_open = menu_open || esc;
        return 0;
    }
    if (esc && !g_core.esc_was)
        g_core.esc_keys = s.field_free ? k::kGbaKeyStart : k::kGbaKeyB;
    if (!esc) g_core.esc_keys = 0;
    g_core.esc_was = esc;
    held |= g_core.esc_keys;
    const bool shift = w->key_down(SC_LSHIFT) || w->key_down(SC_RSHIFT);
    if (s.field_free && (g_auto_run ? !shift : shift)) held |= k::kGbaKeyB;
    return held;
}

// ── fast forward ─────────────────────────────────────────────────────────
// Hold or toggle the bound key; never while the settings pages are open or
// a name is being typed (the key is then a letter / space).
void fast_forward_frame(const Scene& s, k::HostWindow* w) {
    const bool blocked = w->game_menu_open() || s.cb2 == CB2_NamingScreen || w->key_capture_bit() >= 0;
    // a key that is also bound to a game input is that input's, not fast forward's (no double activation); blocked and
    // conflicting states also cancel the activation of an earlier toggle latch (ini::ff_step)
    const bool conflict = g_ff_key > 0 && pc_ff_conflict_bit() >= 0;
    const ini::FfState st = ini::ff_step(g_ff_latched, g_ff_key_was, g_ff_key > 0 && w->key_down(g_ff_key), g_ff_toggle, blocked, conflict);
    g_ff_latched = st.latched;
    g_ff_key_was = st.down;
    const bool on = st.on;
    g_ff_now.store(on ? g_ff_speed : 0, std::memory_order_relaxed);
}

int fast_forward_policy() { return g_ff_now.load(std::memory_order_relaxed); }

// ── Infinite Rare Candy ──────────────────────────────────────────────────
// Keeps a full stack (99) of Rare Candy in the Items pocket: the count is
// topped back up after every use, and a stack is added (outside the Bag
// screen, so its list never changes under it) when there is none.
constexpr int kItemRareCandy = 68;
constexpr std::uint32_t kBagPocketsArr = 0x0203988C;  // gBagPockets[5] {slots*, capacity}
void rare_candy_frame(const Mem& m, const Scene& s) {
    if (!g_rare_candy) return;
    const std::uint32_t sb2 = m.u32(g::SaveBlock2Ptr);
    if (!m.valid(sb2 + 0xF20, 4)) return;
    const std::uint32_t slots = m.u32(kBagPocketsArr);
    const int cap = m.u8(kBagPocketsArr + 4);
    if (!m.valid(slots, 4) || cap <= 0 || cap > 64) return;
    const std::uint16_t key = static_cast<std::uint16_t>(m.u32(sb2 + 0xF20) & 0xFFFF);
    const std::uint16_t full = static_cast<std::uint16_t>(99 ^ key);
    int empty = -1;
    for (int i = 0; i < cap; ++i) {
        const std::uint32_t a = slots + i * 4;
        const int id = m.u16(a);
        if (id == kItemRareCandy) {
            if (m.u16(a + 2) != full) bus_write_u16(a + 2, full);
            return;
        }
        if (id == 0 && empty < 0) empty = i;
    }
    if (empty >= 0 && s.field) {
        bus_write_u16(slots + empty * 4, kItemRareCandy);
        bus_write_u16(slots + empty * 4 + 2, full);
    }
}

std::uint16_t input_frame(const k::TouchFrameInfo* /*frame*/) {
    auto* w = k::active_host_window();
    const Mem m = Mem::current();
    if (!w || !m.ok()) return 0x03FF;
    const Scene s = classify(m);
    g_last_scene = s;
    hd_ui_guest_frame();
    if (!g_pad_applied) { w->set_pad_options(g_pad_swap_ab, g_pad_stick); g_pad_applied = true; }
    fast_forward_frame(s, w);
    rare_candy_frame(m, s);
    if (w->game_menu_open()) {
        g_core.menu_was_open = true;
        macro_cancel();
    }
    std::uint16_t held = keyboard_context(s, w);
    naming_frame(m, s, w);
    handle_mouse(m, s, w, &held);
    held |= macro_frame(m, s);
    g_core.synth.set_held(held);
    return g_core.synth.frame();
}

// ── button prompts: keyboard keycaps in the game's keypad icon sheet ─────
const std::uint8_t kGlyph3x5[][5] = {
    // A-Z
    {2,5,5,7,5},{6,5,6,5,6},{3,4,4,4,3},{6,5,5,5,6},{7,4,6,4,7},{7,4,6,4,4},{3,4,5,5,3},
    {5,5,7,5,5},{7,2,2,2,7},{1,1,1,5,2},{5,5,6,5,5},{4,4,4,4,7},{5,7,7,5,5},{6,5,5,5,5},
    {2,5,5,5,2},{6,5,6,4,4},{2,5,5,6,3},{6,5,6,5,5},{3,4,2,1,6},{7,2,2,2,2},{5,5,5,5,7},
    {5,5,5,5,2},{5,5,7,7,5},{5,5,2,5,5},{5,5,2,2,2},{7,1,2,4,7},
    // 0-9
    {7,5,5,5,7},{2,6,2,2,7},{6,1,2,4,7},{6,1,2,1,6},{5,5,7,1,1},{7,4,6,1,6},{3,4,6,5,2},
    {7,1,2,2,2},{2,5,2,5,2},{2,5,3,1,6},
};
const std::uint8_t* glyph(char c) {
    if (c >= 'A' && c <= 'Z') return kGlyph3x5[c - 'A'];
    if (c >= '0' && c <= '9') return kGlyph3x5[26 + (c - '0')];
    return nullptr;
}

std::string short_label(const char* sdl_name, int max_chars) {
    std::string n = sdl_name ? sdl_name : "";
    std::string u;
    for (char ch : n) u += static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    struct Abbr { const char* name; const char* l5; const char* l3; const char* l1; };
    static const Abbr kAbbr[] = {
        {"RETURN", "ENTER", "ENT", "E"}, {"KEYPAD ENTER", "ENTER", "ENT", "E"},
        {"SPACE", "SPACE", "SPC", "S"}, {"TAB", "TAB", "TAB", "T"},
        {"BACKSPACE", "BKSP", "BSP", "B"}, {"ESCAPE", "ESC", "ESC", "E"},
        {"LEFT SHIFT", "SHIFT", "SHF", "S"}, {"RIGHT SHIFT", "SHIFT", "SHF", "S"},
        {"LEFT CTRL", "CTRL", "CTL", "C"}, {"RIGHT CTRL", "CTRL", "CTL", "C"},
        {"LEFT ALT", "ALT", "ALT", "A"}, {"RIGHT ALT", "ALT", "ALT", "A"},
        {"PAGEUP", "PGUP", "PGU", "U"}, {"PAGEDOWN", "PGDN", "PGD", "D"},
        {"PAGE UP", "PGUP", "PGU", "U"}, {"PAGE DOWN", "PGDN", "PGD", "D"},
        {"UP", "UP", "UP", "U"}, {"DOWN", "DOWN", "DN", "D"},
        {"LEFT", "LEFT", "LFT", "L"}, {"RIGHT", "RIGHT", "RGT", "R"},
        {"DELETE", "DEL", "DEL", "D"}, {"INSERT", "INS", "INS", "I"},
        {"HOME", "HOME", "HOM", "H"}, {"END", "END", "END", "E"},
    };
    for (const auto& a : kAbbr)
        if (u == a.name) return max_chars >= 5 ? a.l5 : max_chars >= 3 ? a.l3 : a.l1;
    std::string out;
    for (char ch : u) if (glyph(ch)) out += ch;
    if (out.empty()) out = "?";
    if (static_cast<int>(out.size()) > max_chars) out.resize(max_chars);
    return out;
}

void draw_keycap(std::uint8_t* rom, int tile_offset, int width, const std::string& label) {
    std::uint8_t px[12][24] = {};
    const int inset = width == 8 ? 0 : 1;
    for (int x = inset + 1; x <= width - 3; ++x) px[4][x] = 1;
    px[4][width - 2] = 2;
    for (int y = 5; y <= 9; ++y) {
        for (int x = inset; x <= width - 2; ++x) px[y][x] = 1;
        px[y][width - 1] = 2;
    }
    px[10][inset] = 2;
    for (int x = inset + 1; x <= width - 3; ++x) px[10][x] = 1;
    px[10][width - 2] = 2;
    if (width > 8) px[10][width - 1] = 2;
    for (int x = inset + 1; x <= width - 3; ++x) px[11][x] = 2;
    const int n = static_cast<int>(label.size());
    const int interior0 = inset + 1, interior_w = width - 3 - inset;
    int x0 = interior0 + std::max(0, (interior_w - (4 * n - 1)) / 2);
    for (char ch : label) {
        if (const std::uint8_t* g = glyph(ch))
            for (int r = 0; r < 5; ++r)
                for (int c = 0; c < 3; ++c)
                    if (g[r] & (4 >> c)) px[5 + r][x0 + c] = 2;
        x0 += 4;
    }
    for (int y = 0; y < 12; ++y)
        for (int x = 0; x < width; ++x) {
            const int tile = tile_offset + x / 8 + (y / 8) * 16;
            const std::uint32_t off = (kKeypadIconTiles - 0x08000000u) + tile * 32 +
                                      (y % 8) * 4 + (x % 8) / 2;
            std::uint8_t& b = rom[off];
            if (x & 1) b = static_cast<std::uint8_t>((b & 0x0F) | (px[y][x] << 4));
            else b = static_cast<std::uint8_t>((b & 0xF0) | px[y][x]);
        }
}

bool prompts_ready() {
    if (!g_rom || g_rom_size < 0x01000000u) return false;
    static const std::uint8_t kTable[] = {0x00, 0x00, 0x08, 0x0C, 0x01, 0x00, 0x08, 0x0C,
                                          0x02, 0x00, 0x10, 0x0C, 0x04, 0x00, 0x10, 0x0C,
                                          0x06, 0x00, 0x18, 0x0C, 0x09, 0x00, 0x18, 0x0C};
    return std::memcmp(g_rom + (kKeypadIconTable - 0x08000000u), kTable, sizeof(kTable)) == 0;
}

void redraw_prompts() {
    if (!prompts_ready()) return;
    auto* w = k::active_host_window();
    auto name = [&](int bit) -> std::string {
        const char* n = w ? w->key_binding_name(bit, 0) : "";
        if (!n || !*n) {
            const int sc = kPcDefaults[bit][0];
            static const char* const kNames[] = {"E", "Q", "F", "TAB", "D", "A", "W", "S", "C", "Z"};
            (void)sc;
            return kNames[bit];
        }
        return n;
    };
    struct Icon { int bit, tile, width; };
    static const Icon kIcons[] = {{0, 0x0, 8}, {1, 0x1, 8}, {9, 0x2, 16}, {8, 0x4, 16},
                                  {3, 0x6, 24}, {2, 0x9, 24}};
    for (const auto& ic : kIcons) {
        const int max_chars = ic.width == 8 ? 1 : ic.width == 16 ? 3 : 5;
        draw_keycap(g_rom, ic.tile, ic.width, short_label(name(ic.bit).c_str(), max_chars));
    }
}

// ── config.ini [Remaster] ────────────────────────────────────────────────
void load_remaster_settings() {
    std::FILE* f = std::fopen((g_config_dir + "/config.ini").c_str(), "r");
    if (!f) return;
    char buf[512];
    bool in = false;
    while (std::fgets(buf, sizeof(buf), f)) {
        std::string l = buf;
        while (!l.empty() && (l.back() == '\n' || l.back() == '\r' || l.back() == ' ')) l.pop_back();
        const std::size_t p = l.find_first_not_of(" \t");
        if (p == std::string::npos) continue;
        if (l[p] == '[') { in = l.compare(p, 10, "[Remaster]") == 0; continue; }
        if (!in) continue;
        const std::size_t eq = l.find('=');
        if (eq == std::string::npos) continue;
        std::string key = l.substr(p, eq - p);
        while (!key.empty() && (key.back() == ' ' || key.back() == '\t')) key.pop_back();
        const int v = std::atoi(l.c_str() + eq + 1);
        if (key == "auto_run") g_auto_run = v != 0;
        else if (key == "mouse") g_mouse_on = v != 0;
        else if (key == "hd_interface") set_hd_ui_enabled(v != 0);   // older single switch
        else if (key == "hd_battle") set_hd_ui_part(kHdPartBattle, v != 0);
        else if (key == "hd_text") { set_hd_ui_part(kHdPartText, v != 0); set_hd_ui_part(kHdPartDialogue, v != 0); }
        else if (key == "hd_dialogue") set_hd_ui_part(kHdPartDialogue, v != 0);
        else if (key == "hd_title") set_hd_ui_part(kHdPartTitle, v != 0);
        else if (key == "hd_dex") set_hd_ui_part(kHdPartDex, v != 0);
        else if (key == "hd_screens") set_hd_ui_part(kHdPartScreens, v != 0);
        else if (key == "modern_text") set_hd_ui_modern_text(v != 0);
        else if (key == "pad_layout") g_pad_swap_ab = v == 1;
        else if (key == "pad_stick") g_pad_stick = v != 0;
        else if (key == "setup_done") g_setup_done = v != 0;
        else if (key == "ff_speed") g_ff_speed = std::clamp(v, 2, 10);
        else if (key == "ff_key") g_ff_key = (v > 0 && v < SDL_NUM_SCANCODES) ? v : SDL_SCANCODE_SPACE;
        else if (key == "ff_toggle") g_ff_toggle = v != 0;
        else if (key == "rare_candy") g_rare_candy = v != 0;
        else if (key == "diorama") g_diorama = v != 0;
    }
    std::fclose(f);
}

void save_remaster_settings() {
    const std::string path = g_config_dir + "/config.ini";
    std::vector<std::string> lines;
    if (std::FILE* f = std::fopen(path.c_str(), "r")) {
        char buf[512];
        while (std::fgets(buf, sizeof(buf), f)) {
            std::string l = buf;
            while (!l.empty() && (l.back() == '\n' || l.back() == '\r')) l.pop_back();
            lines.push_back(l);
        }
        std::fclose(f);
    }
    std::vector<std::string> out;
    bool in = false, saw = false;
    const std::string rows[] = {std::string("auto_run = ") + (g_auto_run ? "1" : "0"),
                                std::string("mouse = ") + (g_mouse_on ? "1" : "0"),
                                std::string("hd_battle = ") + (hd_ui_part(kHdPartBattle) ? "1" : "0"),
                                std::string("hd_text = ") + (hd_ui_part(kHdPartText) ? "1" : "0"),
                                std::string("hd_dialogue = ") + (hd_ui_part(kHdPartDialogue) ? "1" : "0"),
                                std::string("hd_title = ") + (hd_ui_part(kHdPartTitle) ? "1" : "0"),
                                std::string("hd_dex = ") + (hd_ui_part(kHdPartDex) ? "1" : "0"),
                                std::string("hd_screens = ") + (hd_ui_part(kHdPartScreens) ? "1" : "0"),
                                std::string("modern_text = ") + (hd_ui_modern_text() ? "1" : "0"),
                                std::string("pad_layout = ") + (g_pad_swap_ab ? "1" : "0"),
                                std::string("pad_stick = ") + (g_pad_stick ? "1" : "0"),
                                std::string("setup_done = ") + (g_setup_done ? "1" : "0"),
                                "ff_speed = " + std::to_string(g_ff_speed),
                                "ff_key = " + std::to_string(g_ff_key),
                                std::string("ff_toggle = ") + (g_ff_toggle ? "1" : "0"),
                                std::string("rare_candy = ") + (g_rare_candy ? "1" : "0"),
                                std::string("diorama = ") + (g_diorama ? "1" : "0")};
    auto flush = [&]() { for (const auto& r : rows) out.push_back(r); };
    for (const std::string& l : lines) {
        const std::size_t p = l.find_first_not_of(" \t");
        if (p != std::string::npos && l[p] == '[') {
            if (in) flush();
            in = l.compare(p, 10, "[Remaster]") == 0;
            saw = saw || in;
            out.push_back(l);
            continue;
        }
        if (in) continue;  // rewritten from the current values
        out.push_back(l);
    }
    if (in) flush();
    if (!saw) { out.push_back("[Remaster]"); flush(); }
    if (std::FILE* f = std::fopen(path.c_str(), "w")) {
        for (const auto& l : out) std::fprintf(f, "%s\n", l.c_str());
        std::fclose(f);
    }
}

}  // namespace

const Scene& pc_scene() { return g_last_scene; }

std::string pc_key_name(const char* sdl_name) {
    std::string u;
    for (const char* p = sdl_name ? sdl_name : ""; *p; ++p)
        u += static_cast<char>(std::toupper(static_cast<unsigned char>(*p)));
    if (u.empty()) return "-";
    if (u == "RETURN") return "ENTER";
    if (u == "ESCAPE") return "ESC";
    if (u == "LEFT SHIFT") return "L SHIFT";
    if (u == "RIGHT SHIFT") return "R SHIFT";
    if (u == "LEFT CTRL") return "L CTRL";
    if (u == "RIGHT CTRL") return "R CTRL";
    if (u == "PAGEUP") return "PAGE UP";
    if (u == "PAGEDOWN") return "PAGE DOWN";
    if (u == "KEYPAD ENTER") return "NUM ENTER";
    return u;
}

void save_remaster_config() { save_remaster_settings(); }

bool pc_pad_swap_ab() { return g_pad_swap_ab; }
void set_pc_pad_swap_ab(bool on) { g_pad_swap_ab = on; g_pad_applied = false; save_remaster_settings(); }
bool pc_pad_stick() { return g_pad_stick; }
void set_pc_pad_stick(bool on) { g_pad_stick = on; g_pad_applied = false; save_remaster_settings(); }
bool pc_controller_connected() {
    auto* w = k::active_host_window();
    return w && w->controller_connected();
}
int pc_ff_speed() { return g_ff_speed; }
void set_pc_ff_speed(int x) { g_ff_speed = std::clamp(x, 2, 10); save_remaster_settings(); }
bool pc_ff_toggle() { return g_ff_toggle; }
void set_pc_ff_toggle(bool on) { g_ff_toggle = on; g_ff_latched = false; save_remaster_settings(); }
int pc_ff_key() { return g_ff_key; }
void set_pc_ff_key(int scancode) {
    if (scancode <= 0 || scancode >= SDL_NUM_SCANCODES) return;
    g_ff_key = scancode;
    g_ff_latched = false;
    save_remaster_settings();
}
int pc_ff_conflict_bit() {
    auto* w = k::active_host_window();
    if (!w) return -1;
    int sc[10 * k::HostWindow::kKeySlots], bits[10 * k::HostWindow::kKeySlots], n = 0;
    for (int bit = 0; bit < 10; ++bit)
        for (int slot = 0; slot < k::HostWindow::kKeySlots; ++slot)
            if (const int v = w->key_binding(bit, slot)) { sc[n] = v; bits[n] = bit; ++n; }
    return ini::ff_conflict_bit(g_ff_key, sc, bits, n);
}
std::string pc_ff_key_name() { return pc_key_name(SDL_GetScancodeName(static_cast<SDL_Scancode>(g_ff_key))); }
int pc_fast_forward_now() { return g_ff_now.load(std::memory_order_relaxed); }
bool pc_rare_candy() { return g_rare_candy; }
bool pc_diorama() { return g_diorama; }
void set_pc_diorama(bool on) { g_diorama = on; save_remaster_settings(); }
void set_pc_rare_candy(bool on) { g_rare_candy = on; save_remaster_settings(); }

bool pc_setup_done() { return g_setup_done; }
void set_pc_setup_done(bool on) { g_setup_done = on; save_remaster_settings(); }

bool pc_auto_run() { return g_auto_run; }
void set_pc_auto_run(bool on) { g_auto_run = on; save_remaster_settings(); }
bool pc_mouse_enabled() { return g_mouse_on; }
void set_pc_mouse_enabled(bool on) {
    g_mouse_on = on;
    if (auto* w = k::active_host_window()) w->set_cursor_autohide(true);
    save_remaster_settings();
}

void pc_input_rom_loaded(std::uint8_t* rom, std::size_t size) {
    g_rom = rom;
    g_rom_size = size;
    for (const auto& p : kProbes) gba_mod_set_function_hook_enabled(p.id, 1);
    redraw_prompts();
    std::fprintf(stderr, "[frlg-pc] PC controls ready (prompts %s)\n",
                 prompts_ready() ? "redrawn" : "unchanged");
}

void pc_input_bindings_changed() { redraw_prompts(); }

void install_pc_input(gbarecomp::RunOptions& opts, const std::string& exe_dir) {
    g_config_dir = exe_dir.empty() ? std::string(".") : exe_dir;
    load_remaster_settings();
    k::set_keybinds_file("controls.ini");
    k::set_default_key_bindings(kPcDefaults);
    // Tab is the pause menu and F the registered item here; fast-forward
    // stays on the Assist key (2) and the FPS readout is not a PC control.
    k::set_hotkey_default("Turbo", "");
    k::set_hotkey_default("DisplayPerf", "");
    // No emulator save-state slots on F1..F9: the game's own SAVE is the save.
    k::set_savestate_hotkeys_enabled(false);
    opts.input_frame = input_frame;
    opts.fast_forward_policy = fast_forward_policy;
    opts.touch_gesture_claims = 0;
    opts.game_menu_esc_opens = false;
    opts.cursor_autohide = true;
}

}  // namespace frlg
