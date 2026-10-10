// fr_settings_menu.cpp — native settings woven into FireRed's own OPTION
// screen. See fr_settings_menu.h for the overview.
//
//  * In-game OPTION screen: the game draws page 1 itself. Page 2 (screen,
//    widescreen, size, smoothing, volume, controls) continues the same list:
//    Down from CANCEL, Up from TEXT SPEED, or L/R. It is drawn inside the
//    game's own list frame with the screen's live palette, so the OPTION
//    screen's fades and the darkened-row highlight carry over exactly.
//  * Title screen: the main menu gains an OPTION box (the slot FireRed keeps
//    for MYSTERY GIFT) that opens the same OPTION screen and returns.
//  * Esc anywhere else: the same page as a stand-alone OPTION screen, with
//    the game's fade to and from black. The game keeps running underneath
//    (input withheld), so music never stops.

#include "fr_settings_menu.h"
#include "fr_settings_nav.h"
#include "fr_settings_ini.h"
#include "fr_pc_input.h"
#include "fr_hd_ui.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include <SDL.h>

#include "gba_bus.h"
#include "host_overlay.h"
#include "host_window.h"
#include "mod_function_hooks.h"
#include "runtime.h"
#include "runtime_arm.h"  // bus_read_* / bus_write_*
#include "runtime_bus_bridge.h"

namespace frlg {
namespace {

using gbarecomp::OverlayColor;

// ── FireRed (USA rev0) addresses — pokefirered symbol map ────────────────
constexpr uint32_t kRomBase               = 0x08000000u;
// text
constexpr uint32_t kFontNormalGlyphs      = 0x081FF300u;  // sFontNormalLatinGlyphs
constexpr uint32_t kFontNormalWidths      = 0x08207300u;
constexpr uint32_t kFontSmallGlyphs       = 0x081EAF00u;  // sFontSmallLatinGlyphs
constexpr uint32_t kFontSmallWidths       = 0x081EEF00u;
constexpr uint32_t kFontHalfRowOffsets    = 0x081EA044u;  // byte -> base-3 index
constexpr uint32_t kKeypadIconTiles       = 0x081EA700u;  // 128px-wide 4bpp sheet
constexpr uint32_t kKeypadIcons           = 0x081EA6CCu;  // {u16 tile, u8 w, u8 h}[13]
// windows / palettes
constexpr uint32_t kOptionMenuPalette     = 0x083CC2E4u;  // sOptionMenuPalette (BG pal 1)
constexpr uint32_t kTextWindowPalettes    = 0x08471DECu;  // gTextWindowPalettes[5]
constexpr uint32_t kStdTextWindowGfx      = 0x08471A4Cu;  // title frame tiles
constexpr uint32_t kUserFrames            = 0x08471E8Cu;  // {tiles*, palette*}[10]
constexpr int      kUserFrameCount        = 10;
constexpr uint32_t kSaveBlock2PtrVar      = 0x0300500Cu;  // gSaveBlock2Ptr
constexpr uint32_t kTextOption            = 0x08419DCCu;  // "OPTION"
// OPTION screen (option_menu.c)
constexpr uint32_t kOptionMenuPtrVar      = 0x02039620u;  // sOptionMenuPtr
constexpr int      kOptCursor = 0x0E, kOptLoadState = 0x10, kOptState = 0x11;
constexpr int      kOptFrameOption = 0x0A;                // option[MENUITEM_FRAMETYPE]
// title-screen main menu (main_menu.c)
constexpr uint32_t kMain                  = 0x030030F0u;  // gMain: +4 cb2, +8 savedCallback
constexpr uint32_t kCB2_MainMenu          = 0x0800C2D4u;
constexpr uint32_t kCB2_InitMainMenu      = 0x0800C300u;
constexpr uint32_t kTaskExecuteMainMenu   = 0x0800CA94u;  // Task_ExecuteMainMenuSelection
constexpr uint32_t kTaskExecuteMainMenuEnd= 0x0800CB90u;
constexpr uint32_t kMysteryGiftCb2Literal = 0x0800CB8Cu;  // =CB2_InitMysteryGift in its pool
constexpr uint32_t kCB2_InitMysteryGift   = 0x081422ACu;
constexpr uint32_t kCB2_OptionsMenu       = 0x08088388u;  // CB2_OptionsMenuFromStartMenu
constexpr uint32_t kIsMysteryGiftEnabled  = 0x0806E2BCu;
constexpr uint32_t kIsWirelessConnected   = 0x08009480u;  // IsWirelessAdapterConnected
constexpr uint32_t kTextMysteryGift       = 0x08415A97u;  // "MYSTERY GIFT"

constexpr const char* kHookMgEnabled = "frlg.option.main_menu_slot";
constexpr const char* kHookMgSelect  = "frlg.option.main_menu_select";

// TEXT_COLOR_* palette indices.
constexpr int kWhite = 1, kDarkGray = 2, kLightGray = 3, kRed = 4, kLightRed = 5,
              kBlue = 8, kLightBlue = 9;

// ── ROM access ───────────────────────────────────────────────────────────
const uint8_t* g_rom = nullptr;
std::size_t g_rom_size = 0;
bool g_ready = false;

uint8_t rom8(uint32_t a) {
    a -= kRomBase;
    return (g_rom && a < g_rom_size) ? g_rom[a] : 0;
}
uint16_t rom16(uint32_t a) { return static_cast<uint16_t>(rom8(a) | (rom8(a + 1) << 8)); }
uint32_t rom32(uint32_t a) { return rom16(a) | (static_cast<uint32_t>(rom16(a + 2)) << 16); }

OverlayColor bgr555(uint16_t v) {
    auto c5 = [](int x) { return static_cast<uint8_t>((x << 3) | (x >> 2)); };
    return {c5(v & 31), c5((v >> 5) & 31), c5((v >> 10) & 31), 255};
}

struct Palette {
    OverlayColor c[16];
};
Palette rom_palette(uint32_t addr) {
    Palette p;
    for (int i = 0; i < 16; ++i) p.c[i] = bgr555(rom16(addr + 2u * i));
    return p;
}
// Live BG palette from PAL RAM: already carries the game's current fade.
Palette live_palette(int bank) {
    Palette p;
    auto* bus = gbarecomp::active_bus();
    const uint8_t* pal = bus ? bus->pal_ptr() : nullptr;
    for (int i = 0; i < 16; ++i) {
        const int o = bank * 32 + i * 2;
        p.c[i] = pal ? bgr555(static_cast<uint16_t>(pal[o] | (pal[o + 1] << 8))) : OverlayColor{0, 0, 0, 255};
    }
    return p;
}

OverlayColor darken(OverlayColor c, int num, int den) {
    return {static_cast<uint8_t>(c.r * num / den), static_cast<uint8_t>(c.g * num / den),
            static_cast<uint8_t>(c.b * num / den), c.a};
}

// ── FireRed text encoding (subset) ───────────────────────────────────────
uint8_t encode_char(char c) {
    if (c >= 'A' && c <= 'Z') return static_cast<uint8_t>(0xBB + (c - 'A'));
    if (c >= 'a' && c <= 'z') return static_cast<uint8_t>(0xD5 + (c - 'a'));
    if (c >= '0' && c <= '9') return static_cast<uint8_t>(0xA1 + (c - '0'));
    switch (c) {
        case ' ': return 0x00;  case '!': return 0xAB;  case '?': return 0xAC;
        case '.': return 0xAD;  case '-': return 0xAE;  case ',': return 0xB8;
        case '/': return 0xBA;  case ':': return 0xF0;  case '%': return 0x5B;
        case '(': return 0x5C;  case ')': return 0x5D;  case '&': return 0x2D;
        case '+': return 0x2E;  case '=': return 0x35;  case ';': return 0x36;
        case '>': return 0xEF;  // ▶
        case '*': return 0xB9;  // ×
        case '\'': return 0xB4;
        default: return 0xAC;
    }
}
std::vector<uint8_t> encode(const std::string& s) {
    std::vector<uint8_t> out;
    for (char ch : s) out.push_back(encode_char(ch));
    return out;
}
// Keypad icon control code ({A_BUTTON} etc.): F8 nn.
constexpr uint8_t kIconA = 0x00, kIconB = 0x01, kIconL = 0x02, kIconR = 0x03,
                  kIconUpDown = 0x0A, kIconLeftRight = 0x0B;

// ── glyph cache ──────────────────────────────────────────────────────────
struct Font {
    uint32_t glyphs = 0, widths = 0;
    uint32_t stride = 64;
    int tile_count = 4;
    int tile_xy[4][2] = {{0, 0}, {8, 0}, {0, 8}, {8, 8}};
    bool built = false;
    uint8_t px[256][16][16] = {};
    uint8_t width[256] = {};
};
Font g_font_normal{kFontNormalGlyphs, kFontNormalWidths, 64, 4, {{0, 0}, {8, 0}, {0, 8}, {8, 8}}};
Font g_font_small{kFontSmallGlyphs, kFontSmallWidths, 32, 2, {{0, 0}, {0, 8}, {0, 0}, {0, 0}}};

void decode_half(uint8_t byte, uint8_t out[4]) {
    const int idx = rom8(kFontHalfRowOffsets + byte);
    out[0] = static_cast<uint8_t>(idx / 27);
    out[1] = static_cast<uint8_t>((idx / 9) % 3);
    out[2] = static_cast<uint8_t>((idx / 3) % 3);
    out[3] = static_cast<uint8_t>(idx % 3);
}

void build_font(Font& f) {
    if (f.built) return;
    for (int g = 0; g < 256; ++g) {
        f.width[g] = rom8(f.widths + g);
        const uint32_t base = f.glyphs + static_cast<uint32_t>(g) * f.stride;
        for (int t = 0; t < f.tile_count; ++t)
            for (int row = 0; row < 8; ++row) {
                const uint16_t v = rom16(base + t * 16 + row * 2);
                uint8_t left[4], right[4];
                decode_half(static_cast<uint8_t>(v >> 8), left);
                decode_half(static_cast<uint8_t>(v & 0xFF), right);
                for (int x = 0; x < 4; ++x) {
                    f.px[g][f.tile_xy[t][1] + row][f.tile_xy[t][0] + x] = left[x];
                    f.px[g][f.tile_xy[t][1] + row][f.tile_xy[t][0] + 4 + x] = right[x];
                }
            }
    }
    f.built = true;
}

// ── 240x160 canvas, flushed to the overlay as horizontal runs ────────────
constexpr int kW = 240, kH = 160;

struct Canvas {
    OverlayColor px[kH][kW];
    bool set[kH][kW];

    void clear() { std::memset(set, 0, sizeof(set)); }
    void put(int x, int y, OverlayColor c) {
        if (x < 0 || y < 0 || x >= kW || y >= kH) return;
        px[y][x] = c;
        set[y][x] = true;
    }
    void fill(int x, int y, int w, int h, OverlayColor c) {
        for (int yy = y; yy < y + h; ++yy)
            for (int xx = x; xx < x + w; ++xx) put(xx, yy, c);
    }
    int icon_width(uint8_t id) const { return id < 13 ? rom8(kKeypadIcons + id * 4 + 2) : 0; }
    int width_of(const Font& f, const std::vector<uint8_t>& s) const {
        int w = 0;
        for (std::size_t i = 0; i < s.size(); ++i) {
            if (s[i] == 0xFF) break;
            if (s[i] == 0xF8 && i + 1 < s.size()) { w += icon_width(s[++i]); continue; }
            w += f.width[s[i]];
        }
        return w;
    }
    void keypad_icon(int x, int y, uint8_t id, const Palette& pal) {
        if (id >= 13) return;
        const int tile_offset = rom16(kKeypadIcons + id * 4);
        const int w = rom8(kKeypadIcons + id * 4 + 2), h = rom8(kKeypadIcons + id * 4 + 3);
        for (int yy = 0; yy < h; ++yy)
            for (int xx = 0; xx < w; ++xx) {
                const int col = (tile_offset % 16) + xx / 8;
                const int row = (tile_offset / 16) + yy / 8;
                const uint32_t tile = kKeypadIconTiles + static_cast<uint32_t>(row * 16 + col) * 32u;
                const uint8_t b = rom8(tile + (yy % 8) * 4 + (xx % 8) / 2);
                const int idx = (xx & 1) ? (b >> 4) : (b & 15);
                if (idx) put(x + xx, y + yy, pal.c[idx]);
            }
    }
    // Renders FireRed-encoded text (with keypad icons) in the given colours.
    int text(Font& f, int x, int y, const std::vector<uint8_t>& s, OverlayColor fg,
             OverlayColor shadow, const Palette& icon_pal) {
        build_font(f);
        for (std::size_t i = 0; i < s.size(); ++i) {
            const uint8_t g = s[i];
            if (g == 0xFF) break;
            if (g == 0xF8 && i + 1 < s.size()) {
                const uint8_t id = s[++i];
                keypad_icon(x, y, id, icon_pal);
                x += icon_width(id);
                continue;
            }
            const int w = f.width[g];
            for (int yy = 0; yy < 16; ++yy)
                for (int xx = 0; xx < std::min(w, 16); ++xx) {
                    const uint8_t p = f.px[g][yy][xx];
                    if (p == 1) put(x + xx, y + yy, fg);
                    else if (p == 2) put(x + xx, y + yy, shadow);
                }
            x += w;
        }
        return x;
    }
};
Canvas g_canvas;

// 3x3-tile frame around an inner tile rectangle (tiles 0..8, 4 = interior).
void draw_frame(Canvas& c, int tx, int ty, int tw, int th, uint32_t tiles, const Palette& fpal,
                const OverlayColor* interior) {
    for (int y = -1; y <= th; ++y)
        for (int x = -1; x <= tw; ++x) {
            int t;
            if (y == -1) t = x == -1 ? 0 : (x == tw ? 2 : 1);
            else if (y == th) t = x == -1 ? 6 : (x == tw ? 8 : 7);
            else t = x == -1 ? 3 : (x == tw ? 5 : 4);
            const int px0 = (tx + x) * 8, py0 = (ty + y) * 8;
            if (t == 4) {
                if (interior) c.fill(px0, py0, 8, 8, *interior);
                continue;
            }
            for (int yy = 0; yy < 8; ++yy)
                for (int xx = 0; xx < 8; ++xx) {
                    const uint8_t b = rom8(tiles + t * 32u + yy * 4u + xx / 2u);
                    const int idx = (xx & 1) ? (b >> 4) : (b & 15);
                    if (idx) c.put(px0 + xx, py0 + yy, fpal.c[idx]);
                }
        }
}

// ── OPTION screen state (option_menu.c, struct OptionMenu) ───────────────
struct OptionScreen {
    bool present = false;   // allocated and fully built
    uint32_t ptr = 0;
    int cursor = 0, load_state = 0, frame = 0;
};
OptionScreen read_option_screen() {
    OptionScreen o;
    o.ptr = bus_read_u32(kOptionMenuPtrVar);
    if (o.ptr < 0x02000000u || o.ptr >= 0x0203FF00u) return o;
    const int state = bus_read_u8(o.ptr + kOptState);
    o.load_state = bus_read_u8(o.ptr + kOptLoadState);
    o.cursor = bus_read_u16(o.ptr + kOptCursor);
    o.frame = bus_read_u16(o.ptr + kOptFrameOption);
    o.present = state >= 10 && o.load_state <= 5;
    return o;
}

int saved_frame_type() {
    const uint32_t sb2 = bus_read_u32(kSaveBlock2PtrVar);
    if (sb2 < 0x02000000u || sb2 >= 0x02040000u) return 0;
    const int t = (bus_read_u16(sb2 + 0x14) >> 3) & 0x1F;
    return t < kUserFrameCount ? t : 0;
}

// ── settings model (page 2) ──────────────────────────────────────────────
enum class Row { Screen, Widescreen, WindowSize, Smoothing, Interface, ModernText, Volume, AutoRun, Mouse,
                 Controls, Quit, Cancel, Bind, ResetBinds, Back,
                 UiBattle, UiText, UiTitle, UiDex, UiScreens, UiDialogue, PadLayout, PadStick, StartGame,
                 Extras, FfSpeed, FfMode, FfKey, RareCandy, Diorama };
struct Item { Row row; int bit; const char* label; };
const Item kControlItems[] = {
    {Row::PadLayout, -1, "CONTROLLER"}, {Row::PadStick, -1, "LEFT STICK"},
    {Row::Bind, 0, "CONFIRM"},    {Row::Bind, 1, "BACK"},
    {Row::Bind, 3, "MENU"},       {Row::Bind, 2, "USE ITEM"},
    {Row::Bind, 6, "UP"},         {Row::Bind, 7, "DOWN"},
    {Row::Bind, 5, "LEFT"},       {Row::Bind, 4, "RIGHT"},
    {Row::Bind, 9, "L BUTTON"},   {Row::Bind, 8, "R BUTTON"},
    {Row::ResetBinds, -1, "DEFAULT KEYS"},
    {Row::Back, -1, "DONE"},
};
const Item kInterfaceItems[] = {
    {Row::UiBattle, -1, "BATTLES"},       {Row::UiDialogue, -1, "DIALOGUE"},
    {Row::UiText, -1, "FIELD MENUS"},
    {Row::UiTitle, -1, "TITLE MENU"},     {Row::UiDex, -1, "POKEDEX"},
    {Row::UiScreens, -1, "SCREENS"},      {Row::ModernText, -1, "TEXT STYLE"},
    {Row::Back, -1, "DONE"},
};
const Item kExtrasItems[] = {
    {Row::FfSpeed, -1, "FAST FORWARD"},  {Row::FfKey, -1, "FAST KEY"},
    {Row::FfMode, -1, "KEY MODE"},       {Row::RareCandy, -1, "RARE CANDY"},
    {Row::Diorama, -1, "3D VIEW"},
    {Row::Back, -1, "DONE"},
};
constexpr int kVisibleRows = 7;
constexpr int kFfSpeeds[] = {2, 3, 4, 5, 6, 8, 10};
constexpr int kFfSpeedCount = 7;
int ff_speed_index() {
    int best = 0;
    for (int i = 0; i < kFfSpeedCount; ++i) if (kFfSpeeds[i] <= frlg::pc_ff_speed()) best = i;
    return best;
}
// Fast-forward key capture (our own key, not one of the ten GBA inputs).
bool g_ff_capture = false;
int g_ff_refusal = 0;               // why the last pressed key was refused for the fast-forward capture (0 none)
bool g_ff_capture_armed = false;    // all keys were released since it began
bool g_wait_release = false;        // swallow menu input until keys go up

bool g_widescreen_supported = false;
std::string g_config_dir;

enum class Mode { None, Page2, Standalone, Setup };
enum class Sub { None, Controls, Interface, Extras };
static_assert(static_cast<int>(Sub::Controls) == nav::kControls && static_cast<int>(Sub::Interface) == nav::kInterface &&
              static_cast<int>(Sub::Extras) == nav::kExtras && static_cast<int>(Mode::Setup) == nav::kSetup &&
              static_cast<int>(Mode::Page2) == nav::kPage2 && static_cast<int>(Mode::Standalone) == nav::kStandalone,
              "settings navigation enums out of step with fr_settings_nav.h");
Mode g_mode = Mode::None;
Sub g_sub = Sub::None;
bool g_controls = false;            // g_sub == Sub::Controls (kept for the classic renderer)
int g_cursor = 0, g_scroll = 0;
bool g_dirty = false;
bool g_exiting = false;             // page 2 asked the OPTION screen to close
// stand-alone fades (16 GBA frames ≈ 267 ms, like BeginNormalPaletteFade)
constexpr uint32_t kFadeMs = 267;
uint32_t g_fade_start = 0;
bool g_fade_started = false;
bool g_closing = false;
float g_fade_bright = 1.0f, g_fade_black = 0.0f;

gbarecomp::HostWindow* window() { return gbarecomp::active_host_window(); }

// Game policy for GBA key binding (see HostWindow::set_key_bind_filter): F1-F12 and the fast-forward key stay out.
int gba_bind_filter(int, int scancode) { return ini::gba_key_refusal(scancode, frlg::pc_ff_key()); }

// Cancels every pending key capture (GBA binding and fast-forward key).
void end_captures() {
    if (auto* w = window()) { w->begin_key_capture(-1); w->set_raw_key_capture(false); }
    g_ff_capture = false;
    g_ff_refusal = 0;
}

int setting(const char* key, int fallback = 0) {
    int v = fallback;
    if (!gbarecomp::runtime_setting_get(key, &v)) return fallback;
    return v;
}

void set_sub(Sub sub) {
    g_sub = sub;
    g_controls = sub == Sub::Controls;
}
long g_pump_overlay = 0, g_pump_render = 0, g_input_calls = 0;   // diagnostics
nav::CursorMemory g_mem;   // cursor / scroll per category while the menu is open
Sub sub_of(int cat) { return static_cast<Sub>(cat); }
int cat_of(Sub sub) { return static_cast<int>(sub); }
const char* category_name(Sub sub) {
    return sub == Sub::Controls ? "CONTROLS" : sub == Sub::Interface ? "INTERFACE" : sub == Sub::Extras ? "EXTRAS" : "";
}

std::vector<Item> items_for(Sub sub) {
    std::vector<Item> v;
    if (sub == Sub::Controls) { v.assign(std::begin(kControlItems), std::end(kControlItems)); return v; }
    if (sub == Sub::Interface) { v.assign(std::begin(kInterfaceItems), std::end(kInterfaceItems)); return v; }
    if (sub == Sub::Extras) { v.assign(std::begin(kExtrasItems), std::end(kExtrasItems)); return v; }
    if (g_mode == Mode::Setup) {
        // First run: the choices that matter most, then off you go.
        v.push_back({Row::Screen, -1, "SCREEN"});
        if (g_widescreen_supported) v.push_back({Row::Widescreen, -1, "WIDESCREEN"});
        v.push_back({Row::UiBattle, -1, "BATTLES"});
        v.push_back({Row::UiDialogue, -1, "DIALOGUE"});
        v.push_back({Row::UiText, -1, "FIELD MENUS"});
        v.push_back({Row::UiTitle, -1, "TITLE MENU"});
        v.push_back({Row::UiDex, -1, "POKEDEX"});
        v.push_back({Row::UiScreens, -1, "SCREENS"});
        v.push_back({Row::ModernText, -1, "TEXT STYLE"});
        v.push_back({Row::AutoRun, -1, "AUTO RUN"});
        v.push_back({Row::Mouse, -1, "MOUSE"});
        v.push_back({Row::Extras, -1, "EXTRAS"});
        v.push_back({Row::Controls, -1, "CONTROLS"});
        v.push_back({Row::StartGame, -1, "START"});
        return v;
    }
    v.push_back({Row::Screen, -1, "SCREEN"});
    if (g_widescreen_supported) v.push_back({Row::Widescreen, -1, "WIDESCREEN"});
    v.push_back({Row::WindowSize, -1, "WINDOW SIZE"});
    v.push_back({Row::Smoothing, -1, "SMOOTHING"});
    v.push_back({Row::Interface, -1, "INTERFACE"});
    v.push_back({Row::Volume, -1, "VOLUME"});
    v.push_back({Row::AutoRun, -1, "AUTO RUN"});
    v.push_back({Row::Mouse, -1, "MOUSE"});
    v.push_back({Row::Extras, -1, "EXTRAS"});
    v.push_back({Row::Controls, -1, "CONTROLS"});
    v.push_back({Row::Quit, -1, "QUIT GAME"});
    v.push_back({Row::Cancel, -1, "CANCEL"});
    return v;
}
std::vector<Item> page_items() { return items_for(g_sub); }
// QUIT GAME asks for a second press within a few seconds.
ini::QuitArm g_quit;   // two-press QUIT GAME confirm, see ini::QuitArm
uint32_t g_now_ms = 0;
// Row index of `row` on the main list (where a sub-page returns to).
int parent_row_index(Row row) {
    const auto items = items_for(Sub::None);
    for (std::size_t i = 0; i < items.size(); ++i)
        if (items[i].row == row) return static_cast<int>(i);
    return 0;
}
int controls_row_index() { return parent_row_index(Row::Controls); }

frlg::HdPart ui_part(Row r) {
    switch (r) {
        case Row::UiBattle: return frlg::kHdPartBattle;
        case Row::UiText: return frlg::kHdPartText;
        case Row::UiTitle: return frlg::kHdPartTitle;
        case Row::UiDex: return frlg::kHdPartDex;
        case Row::UiDialogue: return frlg::kHdPartDialogue;
        default: return frlg::kHdPartScreens;
    }
}
bool is_ui_row(Row r) {
    return r == Row::UiBattle || r == Row::UiText || r == Row::UiTitle || r == Row::UiDex || r == Row::UiScreens ||
           r == Row::UiDialogue;
}
// Rows whose value changes in place (A cycles them too).
bool is_value_row(Row r) {
    return r == Row::Screen || r == Row::Widescreen || r == Row::WindowSize || r == Row::Smoothing ||
           r == Row::Volume || r == Row::AutoRun || r == Row::Mouse || r == Row::ModernText ||
           r == Row::PadLayout || r == Row::PadStick || r == Row::FfSpeed || r == Row::FfMode ||
           r == Row::RareCandy || r == Row::Diorama || is_ui_row(r);
}
const char* interface_summary() {
    int on = 0;
    for (int i = 0; i < frlg::kHdPartCount; ++i) on += frlg::hd_ui_part(static_cast<frlg::HdPart>(i)) ? 1 : 0;
    return on == frlg::kHdPartCount ? "Modern" : on == 0 ? "Classic" : "Mixed";
}

bool row_enabled(const Item& it) {
    if (it.row == Row::WindowSize) return setting("display.fullscreen") == 0;
    if (it.row == Row::ModernText) return frlg::hd_ui_enabled();  // HD text only
    return true;
}

std::string upper(std::string s) {
    for (char& ch : s) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    return s;
}

std::string row_value(const Item& it) {
    switch (it.row) {
        case Row::Screen: {
            static const char* k[] = {"WINDOW", "BORDERLESS", "FULL"};
            return k[std::clamp(setting("display.fullscreen"), 0, 2)];
        }
        case Row::Widescreen: return setting("display.view_mode") ? "16:9" : "OFF";
        case Row::WindowSize: return std::to_string(setting("display.window_scale", 3)) + "*";
        case Row::Smoothing:  return setting("graphics.linear_filter") ? "ON" : "OFF";
        case Row::Volume:     return std::to_string(setting("audio.volume", 100));
        case Row::Quit:       return g_quit.armed(g_now_ms) ? "PRESS AGAIN" : "";
        case Row::AutoRun:    return frlg::pc_auto_run() ? "ON" : "OFF";
        case Row::Mouse:      return frlg::pc_mouse_enabled() ? "ON" : "OFF";
        case Row::Interface:  return upper(interface_summary());
        case Row::ModernText: return frlg::hd_ui_modern_text() ? "MODERN" : "CLASSIC";
        case Row::UiBattle: case Row::UiText: case Row::UiTitle: case Row::UiDex: case Row::UiScreens:
        case Row::UiDialogue:
            return frlg::hd_ui_part(ui_part(it.row)) ? "MODERN" : "CLASSIC";
        case Row::PadLayout:  return frlg::pc_pad_swap_ab() ? "NINTENDO" : "XBOX";
        case Row::PadStick:   return frlg::pc_pad_stick() ? "ON" : "OFF";
        case Row::Extras:     return "";
        case Row::FfSpeed:    return std::to_string(frlg::pc_ff_speed()) + "*";
        case Row::FfMode:     return frlg::pc_ff_toggle() ? "TOGGLE" : "HOLD";
        case Row::FfKey:      return g_ff_capture ? "..." : ini::ff_key_row_label(frlg::pc_ff_key_name(), frlg::pc_ff_conflict_bit() >= 0);
        case Row::RareCandy:  return frlg::pc_rare_candy() ? "ON" : "OFF";
        case Row::Diorama:    return frlg::pc_diorama() ? "ON" : "OFF";
        case Row::Bind: {
            auto* w = window();
            if (!w) return "-";
            const char* k0 = w->key_binding_name(it.bit, 0);
            const char* k1 = w->key_binding_name(it.bit, 1);
            return ini::binding_label(k0 && *k0 ? frlg::pc_key_name(k0) : "", k1 && *k1 ? frlg::pc_key_name(k1) : "");
        }
        default: return "";
    }
}

// ── persistence: config.ini [Launcher] ───────────────────────────────────
void save_launcher_settings() {
    const std::string path = g_config_dir + "/config.ini";
    std::vector<std::pair<std::string, std::string>> kv = {
        {"fullscreen", std::to_string(setting("display.fullscreen"))},
        {"scale", std::to_string(setting("display.window_scale", 3))},
        {"linear_filter", std::to_string(setting("graphics.linear_filter"))},
        {"volume", std::to_string(setting("audio.volume", 100))},
    };
    if (g_widescreen_supported)
        kv.push_back({"widescreen", std::to_string(setting("display.view_mode") ? 1 : 0)});
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
    const std::vector<std::string> out = ini::merge_section(lines, "Launcher", kv);
    if (std::FILE* f = std::fopen(path.c_str(), "w")) {
        for (const std::string& l : out) std::fprintf(f, "%s\n", l.c_str());
        std::fclose(f);
    }
}

// True when navigation input must be ignored: a key capture is pending, or the page is fading in / closing.
// Every input route (keys, HD mouse, classic mouse) asks this, not only menu_input.
bool input_blocked() {
    auto* w = window();
    if (w && w->key_capture_bit() >= 0) return true;                 // engine owns the next key
    if (g_ff_capture || g_wait_release) return true;                  // fast-forward key capture
    if ((g_mode == Mode::Standalone || g_mode == Mode::Setup) && (g_closing || !g_fade_started)) return true;
    if (g_mode == Mode::Page2 && g_exiting) return true;
    return false;
}
// Mouse events while a capture is pending are swallowed; a right click cancels it. True = consumed.
bool mouse_capture_gate(unsigned pressed) {
    auto* w = window();
    const bool capturing = (w && w->key_capture_bit() >= 0) || g_ff_capture;
    switch (ini::capture_mouse(capturing, pressed)) {
        case ini::CaptureMouse::kCancel: end_captures(); g_wait_release = false; return true;
        case ini::CaptureMouse::kBlock: return true;
        default: return false;
    }
}
void fix_scroll();
// The single place the mouse moves the cursor: keeps scroll right and the Quit arm honest.
void mouse_cursor(int row) {
    g_cursor = row;
    fix_scroll();
    g_quit.observe(g_cursor, static_cast<int>(g_sub));
}

// ── mode transitions ─────────────────────────────────────────────────────
void reset_list(int cursor = 0) {
    g_mem.reset();
    set_sub(Sub::None);
    g_cursor = cursor;
    g_scroll = 0;
}
void fix_scroll();
void switch_category(Sub to);
// Back out of a sub-page to its row on the main list.
void leave_sub() {
    const Row from = g_sub == Sub::Interface ? Row::Interface : g_sub == Sub::Extras ? Row::Extras : Row::Controls;
    g_quit.until = 0;
    g_mem.save(cat_of(g_sub), g_cursor, g_scroll);
    set_sub(Sub::None);
    g_cursor = parent_row_index(from);
    g_scroll = 0;
    fix_scroll();
}
// Jump to another category (L/R, the category tabs, or a row on the main list).
void switch_category(Sub to) {
    g_quit.until = 0;   // a category change always disarms QUIT GAME
    g_mem.save(cat_of(g_sub), g_cursor, g_scroll);
    set_sub(to);
    const int n = static_cast<int>(items_for(to).size());
    g_cursor = std::clamp(g_mem.cursor[cat_of(to)], 0, std::max(0, n - 1));
    g_scroll = g_mem.scroll[cat_of(to)];
    fix_scroll();
}

void finish_mode() {
    end_captures();
    g_mode = Mode::None;
    set_sub(Sub::None);
    g_exiting = false;
    g_closing = false;
    g_fade_started = false;
    if (g_dirty) save_launcher_settings();
    g_dirty = false;
}

void enter_page2(int cursor) {
    g_mode = Mode::Page2;
    g_exiting = false;
    reset_list(cursor);
}

void leave_page2() {  // back to the game's page 1 (OPTION screen stays up)
    end_captures();
    g_mode = Mode::None;
    set_sub(Sub::None);
    if (g_dirty) save_launcher_settings();
    g_dirty = false;
}

// Closing the OPTION screen from page 2 is the game's own A/B exit: advance
// Task_OptionMenu to its fade-out state, which then saves page 1.
void exit_option_screen() {
    const OptionScreen o = read_option_screen();
    if (o.present && o.load_state == 2) bus_write_u8(o.ptr + kOptLoadState, 3);
    g_exiting = true;
    end_captures();
}

void open_standalone() {
    g_mode = Mode::Standalone;
    g_closing = false;
    g_fade_started = false;  // fade clock starts at the first rendered frame
    reset_list();
}

void open_setup() {
    g_mode = Mode::Setup;
    g_closing = false;
    g_fade_started = false;
    reset_list();
}

void finish_setup() {
    frlg::set_pc_setup_done(true);
    save_launcher_settings();
    g_dirty = false;
    end_captures();
    g_closing = true;
    g_fade_started = false;
}

void close_standalone() {
    if (g_closing) return;
    end_captures();
    g_closing = true;
    g_fade_started = false;
}

// ── engine hooks ─────────────────────────────────────────────────────────
bool menu_is_open() { return g_mode != Mode::None; }

void open_menu() {  // Esc / pad Guide while closed
    if (!g_ready || g_mode != Mode::None) return;
    const OptionScreen o = read_option_screen();
    if (o.ptr && o.ptr >= 0x02000000u && o.ptr < 0x0203FF00u) {
        if (o.present && o.load_state == 2) enter_page2(0);
        return;  // OPTION screen still building / fading: ignore
    }
    open_standalone();
}

// Page from the game's own OPTION list into page 2.
bool menu_peek(int kind, bool repeat) {
    using HW = gbarecomp::HostWindow;
    if (!g_ready || g_mode != Mode::None || repeat) return false;
    const OptionScreen o = read_option_screen();
    if (!o.present || o.load_state != 2) return false;
    const int last = static_cast<int>(page_items().size()) - 1;
    switch (kind) {
        case HW::kMenuPageLeft:
        case HW::kMenuPageRight:   // Game -> the nearest end of the page-2 ring
            enter_page2(0);
            switch_category(sub_of(nav::game_enter_cat(kind == HW::kMenuPageLeft ? -1 : +1)));
            return true;
        case HW::kMenuDown: if (o.cursor == 6) { enter_page2(0); return true; } break;
        case HW::kMenuUp:   if (o.cursor == 0) { enter_page2(last); return true; } break;
        default: break;
    }
    return false;
}

void change(const Item& it, int dir) {
    if (!row_enabled(it)) return;
    switch (it.row) {
        case Row::Screen:
            gbarecomp::runtime_setting_set("display.fullscreen", (setting("display.fullscreen") + dir + 3) % 3);
            break;
        case Row::Widescreen:
            gbarecomp::runtime_setting_set("display.view_mode", setting("display.view_mode") ? 0 : 1);
            break;
        case Row::WindowSize:
            gbarecomp::runtime_setting_set("display.window_scale",
                                           std::clamp(setting("display.window_scale", 3) + dir, 1, 8));
            break;
        case Row::Smoothing:
            gbarecomp::runtime_setting_set("graphics.linear_filter", setting("graphics.linear_filter") ? 0 : 1);
            break;
        case Row::Volume:
            gbarecomp::runtime_setting_set("audio.volume",
                                           std::clamp(setting("audio.volume", 100) + dir * 10, 0, 100));
            break;
        case Row::AutoRun: frlg::set_pc_auto_run(!frlg::pc_auto_run()); return;
        case Row::Mouse:   frlg::set_pc_mouse_enabled(!frlg::pc_mouse_enabled()); return;
        case Row::UiBattle: case Row::UiText: case Row::UiTitle: case Row::UiDex: case Row::UiScreens:
        case Row::UiDialogue:
            frlg::set_hd_ui_part(ui_part(it.row), !frlg::hd_ui_part(ui_part(it.row)));
            frlg::save_remaster_config();
            return;
        case Row::PadLayout: frlg::set_pc_pad_swap_ab(!frlg::pc_pad_swap_ab()); return;
        case Row::FfSpeed: {
            const int i = std::clamp(ff_speed_index() + dir, 0, kFfSpeedCount - 1);
            frlg::set_pc_ff_speed(kFfSpeeds[i]);
            return;
        }
        case Row::FfMode: frlg::set_pc_ff_toggle(!frlg::pc_ff_toggle()); return;
        case Row::RareCandy: frlg::set_pc_rare_candy(!frlg::pc_rare_candy()); return;
        case Row::Diorama: frlg::set_pc_diorama(!frlg::pc_diorama()); return;
        case Row::PadStick:  frlg::set_pc_pad_stick(!frlg::pc_pad_stick()); return;
        case Row::ModernText:
            if (!frlg::hd_ui_enabled()) return;
            frlg::set_hd_ui_modern_text(!frlg::hd_ui_modern_text());
            frlg::save_remaster_config();
            return;
        default: return;
    }
    g_dirty = true;
    save_launcher_settings();   // saved at once: closing the window or quitting from a menu must not lose it
    g_dirty = false;
}

void fix_scroll() {
    if (g_cursor < g_scroll) g_scroll = g_cursor;
    if (g_cursor >= g_scroll + kVisibleRows) g_scroll = g_cursor - kVisibleRows + 1;
}

void menu_input(int kind, bool repeat) {
    using HW = gbarecomp::HostWindow;
    if (g_mode == Mode::None) return;
    ++g_input_calls;
    auto* w = window();
    if (input_blocked()) return;
    g_quit.observe(g_cursor, cat_of(g_sub));
    const bool page2 = g_mode == Mode::Page2;
    const bool sub = g_sub != Sub::None;
    const auto items = page_items();
    const int n = static_cast<int>(items.size());
    g_cursor = std::clamp(g_cursor, 0, n - 1);
    const Item& it = items[g_cursor];
    const int cursor_before = g_cursor;
    const Sub sub_before = g_sub;
    switch (kind) {
        case HW::kMenuUp:
            if (page2 && !sub && g_cursor == 0) { leave_page2(); return; }
            g_cursor = (g_cursor + n - 1) % n;
            break;
        case HW::kMenuDown:
            if (page2 && !sub && g_cursor == n - 1) { leave_page2(); return; }
            g_cursor = (g_cursor + 1) % n;
            break;
        case HW::kMenuLeft:  if (is_value_row(it.row)) change(it, -1); break;
        case HW::kMenuRight: if (is_value_row(it.row)) change(it, +1); break;
        case HW::kMenuPageLeft:
        case HW::kMenuPageRight:
            if (repeat) break;
            if (nav::page_action(static_cast<int>(g_mode), cat_of(g_sub), kind == HW::kMenuPageLeft ? -1 : +1) == nav::kLeavePage2) { leave_page2(); return; }
            switch_category(sub_of(nav::cat_cycle(cat_of(g_sub), kind == HW::kMenuPageLeft ? -1 : +1)));
            return;
        case HW::kMenuAccept:
        case HW::kMenuStart:
            if (repeat) break;
            if (it.row == Row::Bind) { if (w) w->begin_key_capture(it.bit, 0); }
            else if (it.row == Row::ResetBinds) {
                if (w) {
                    w->reset_key_bindings();
                    w->save_key_bindings();
                    frlg::pc_input_bindings_changed();
                }
            }
            else if (it.row == Row::Back) leave_sub();
            else if (it.row == Row::Controls) switch_category(Sub::Controls);
            else if (it.row == Row::Interface) switch_category(Sub::Interface);
            else if (it.row == Row::Extras) switch_category(Sub::Extras);
            else if (it.row == Row::FfKey) {
                g_ff_capture = true; g_ff_capture_armed = false; g_ff_refusal = 0;
                if (w) w->set_raw_key_capture(true);   // F-keys / Enter / fullscreen cannot fire while we read raw keys
            }
            else if (it.row == Row::StartGame) finish_setup();
            else if (it.row == Row::Quit) {
                if (g_quit.press(g_now_ms, g_cursor, cat_of(g_sub), 3000)) {
                    if (g_dirty) save_launcher_settings();
                    if (w) w->request_quit();
                }
            }
            else if (is_value_row(it.row)) change(it, +1);
            else if (page2) exit_option_screen();       // CANCEL
            else close_standalone();
            break;
        case HW::kMenuBack:
        case HW::kMenuClose:
            if (repeat) break;
            switch (nav::back_action(static_cast<int>(g_mode), cat_of(g_sub), kind == HW::kMenuClose)) {
                case nav::kLeaveSub: leave_sub(); break;
                case nav::kJumpStart: g_cursor = n - 1; break;   // "Start Playing"
                case nav::kExitOption: exit_option_screen(); break;
                case nav::kClose: close_standalone(); break;
                default: break;
            }
            break;
        default: break;
    }
    g_quit.observe(g_cursor, cat_of(g_sub));   // PRESS AGAIN only holds on the Quit row (every route calls observe)
    (void)cursor_before; (void)sub_before;
    fix_scroll();
}

// ── drawing ──────────────────────────────────────────────────────────────
// OPTION screen geometry (option_menu.c): hint bar on tiles 0-1 (BG2, pal
// 15), title window inside (2,3)+26x2 and list window inside (2,7)+26x12
// (pal 1), rows 13px apart from y=2, labels at x=8, values at x=130 in
// LIGHT_RED on RED, selection = WIN0 row; everything else in the list is
// darkened by BLDY 2/16.
constexpr int kListX = 16, kListY = 56, kListW = 208, kListH = 96;
constexpr int kTitleX = 16, kTitleY = 24, kTitleW = 208, kTitleH = 16;

struct Look {
    Palette list;   // BG palette 1 (sOptionMenuPalette)
    Palette bar;    // BG palette 15 (gTextWindowPalettes[2])
    OverlayColor bar_fill;
};

Look live_look() {
    Look l;
    l.list = live_palette(1);
    l.bar = live_palette(15);
    l.bar_fill = l.bar.c[15];
    return l;
}
Look rom_look() {
    Look l;
    l.list = rom_palette(kOptionMenuPalette);
    l.bar = rom_palette(kTextWindowPalettes + 2 * 32);
    l.bar_fill = l.bar.c[15];
    return l;
}

void draw_hint_bar(Canvas& c, const Look& look, bool on_page2_or_standalone, bool paging) {
    c.fill(0, 0, kW, 16, look.bar_fill);
    auto* w = window();
    std::vector<uint8_t> s;
    const bool engine_capture = w && w->key_capture_bit() >= 0;
    if (const int hint = ini::capture_hint(engine_capture, g_ff_capture, w ? w->key_capture_refusal() : 0, g_ff_refusal)) {
        s = encode(hint == 2 ? "KEY NOT ALLOWED  ESC CANCEL" : "PRESS A KEY   ESC CANCEL");
    } else {
        s = {0xF8, kIconUpDown};
        auto add = [&](const char* t) { for (uint8_t b : encode(t)) s.push_back(b); };
        add("PICK ");
        if (on_page2_or_standalone && g_controls) {
            s.push_back(0xF8); s.push_back(kIconA); add("SET KEY ");
            s.push_back(0xF8); s.push_back(kIconB); add("BACK");
        } else {
            s.push_back(0xF8); s.push_back(kIconLeftRight); add("SWITCH ");
            if (paging) { s.push_back(0xF8); s.push_back(kIconL); s.push_back(0xF8); s.push_back(kIconR); add("PAGE "); }
            s.push_back(0xF8); s.push_back(kIconA); s.push_back(0xF8); s.push_back(kIconB); add("CANCEL");
        }
    }
    const int x = 0xE4 - c.width_of(g_font_small, s);
    c.text(g_font_small, x, 0, s, look.bar.c[kWhite], look.bar.c[kDarkGray], look.bar);
}

void draw_title_tag(Canvas& c, const Look& look, const std::string& tag) {
    if (tag.empty()) return;
    const auto s = encode(tag);
    const int x = kTitleX + kTitleW - 6 - c.width_of(g_font_normal, s);
    c.fill(x - 2, kTitleY, kTitleX + kTitleW - (x - 2), kTitleH, look.list.c[kWhite]);
    c.text(g_font_normal, x, kTitleY + 1, s, look.list.c[kDarkGray], look.list.c[kLightGray], look.list);   // same colours as the title
}

void draw_arrow(Canvas& c, int cx, int y, bool up, OverlayColor col) {
    for (int i = 0; i < 4; ++i) c.fill(cx - i, up ? y + i : y + 3 - i, 2 * i + 1, 1, col);
}

void draw_list(Canvas& c, const Look& look) {
    const OverlayColor white = look.list.c[kWhite];
    const OverlayColor dim = darken(white, 14, 16);
    const auto items = page_items();
    const int n = static_cast<int>(items.size());
    const int sel = g_cursor - g_scroll;
    // Interior: darkened except the selected row (WIN0 x 16..224).
    c.fill(kListX, kListY, kListW, kListH, dim);
    c.fill(kListX, kListY + 2 + sel * 13, kListW, 14, white);
    auto* w = window();
    for (int r = 0; r < kVisibleRows && g_scroll + r < n; ++r) {
        const Item& it = items[g_scroll + r];
        const bool lit = r == sel;
        auto shade = [&](OverlayColor col) { return lit ? col : darken(col, 14, 16); };
        const int y = kListY + 2 + r * 13;
        const bool enabled = row_enabled(it);
        c.text(g_font_normal, kListX + 8, y, encode(it.label),
               shade(look.list.c[enabled ? kDarkGray : kLightGray]),
               shade(look.list.c[enabled ? kLightGray : kWhite]), look.list);
        const bool capture_row = w && it.row == Row::Bind && w->key_capture_bit() == it.bit;
        if (capture_row) {
            static int blink = 0;
            if ((++blink / 16) % 2 == 0)
                c.text(g_font_normal, kListX + 130, y, encode("PRESS KEY"),
                       shade(look.list.c[kBlue]), shade(look.list.c[kLightBlue]), look.list);
            continue;
        }
        const std::string v = row_value(it);
        if (!v.empty())
            c.text(g_font_normal, kListX + 130, y, encode(v),
                   shade(look.list.c[enabled ? kLightRed : kLightGray]),
                   shade(look.list.c[enabled ? kRed : kWhite]), look.list);
    }
    const OverlayColor arrow = look.list.c[kRed];
    if (g_scroll > 0) draw_arrow(c, kListX + kListW - 8, kListY + 1, true, arrow);
    if (g_scroll + kVisibleRows < n) draw_arrow(c, kListX + kListW - 8, kListY + kListH - 5, false, arrow);
}

// Flush the canvas into the overlay, multiplying by `bright` (0..256) to
// follow a fade, and paint the hint bar across any widescreen margins.
void flush(gbarecomp::HostOverlay* ov, Canvas& c, int bright, bool bar_margins,
           OverlayColor bar_fill) {
    const int vw = ov->view_width(), vh = ov->view_height();
    const float ox = static_cast<float>((vw - kW) / 2);
    const float oy = static_cast<float>((vh - kH) / 2);
    auto scale = [&](OverlayColor col) {
        return OverlayColor{static_cast<uint8_t>(col.r * bright / 256),
                            static_cast<uint8_t>(col.g * bright / 256),
                            static_cast<uint8_t>(col.b * bright / 256), 255};
    };
    if (bar_margins && ox > 0) {
        const OverlayColor b = scale(bar_fill);
        ov->fill_rect(0, oy, ox, 16, 0, b, gbarecomp::OverlaySpace::View);
        ov->fill_rect(ox + kW, oy, static_cast<float>(vw) - ox - kW, 16, 0, b,
                      gbarecomp::OverlaySpace::View);
    }
    for (int y = 0; y < kH; ++y) {
        int x = 0;
        while (x < kW) {
            if (!c.set[y][x]) { ++x; continue; }
            const OverlayColor col = c.px[y][x];
            int e = x + 1;
            while (e < kW && c.set[y][e] && c.px[y][e].r == col.r && c.px[y][e].g == col.g &&
                   c.px[y][e].b == col.b)
                ++e;
            ov->fill_rect(ox + x, oy + y, static_cast<float>(e - x), 1.0f, 0, scale(col),
                          gbarecomp::OverlaySpace::View);
            x = e;
        }
    }
}

// Page 1 (the game's own list): add the PAGE hint and tag only.
// ── diagnostics (off unless the env var is set) ──────────────────────────
// FRLG_SETTINGS_OPEN=<standalone|setup>[:<main|interface|extras|controls>[:<cursor>]]
//   opens once, exactly as Esc / first run would.
// FRLG_SETTINGS_SEQ="open=standalone;wait=30;cat=extras;step=1;back;close"
//   runs live actions through the same entry points as mouse / keys, one per
//   FRLG_SETTINGS_GAP pumps (default 20). Steps: open=<standalone|setup|page2>,
//   cat=<main|interface|extras|controls|game>, step=<-1|1> (L/R), back (B),
//   close (Esc), wait=<pumps>. Every step logs one line to stderr:
//   [settings-seq] #n step pumps=overlay+render ... mode sub cursor
// Both are opened-once and tick from the overlay and the menu renderer, so the
// counts keep advancing while the menu is open.
const char* mode_name() {
    return g_mode == Mode::Page2 ? "page2" : g_mode == Mode::Standalone ? "standalone" : g_mode == Mode::Setup ? "setup" : "closed";
}
int sub_from_name(const std::string& n) {
    return n == "interface" ? 2 : n == "extras" ? 3 : n == "controls" ? 1 : 0;
}
void diag_log(int index, const std::string& step) {
    auto* w = window();
    std::string row_text = "-";
    if (g_mode != Mode::None) {
        const auto items = page_items();
        if (g_cursor >= 0 && g_cursor < static_cast<int>(items.size())) row_text = items[g_cursor].label + std::string("=") + row_value(items[g_cursor]);
    }
    std::fprintf(stderr, "[settings-seq] #%d %s pumps=%ld+%ld inputs=%ld mode=%s sub=%d cursor=%d scroll=%d closing=%d "
                 "quit_armed=%d ff_capture=%d raw_capture=%d key_capture=%d refusal=%d/%d wait_release=%d volume=%d fullscreen=%d diorama=%d row=[%s]\n", index,
                 step.c_str(), g_pump_overlay, g_pump_render, g_input_calls, mode_name(), static_cast<int>(g_sub), g_cursor,
                 g_scroll, (g_closing || g_exiting) ? 1 : 0, g_quit.armed(g_now_ms) ? 1 : 0, g_ff_capture ? 1 : 0,
                 (w && w->raw_key_capture()) ? 1 : 0, (w && w->key_capture_bit() >= 0) ? 1 : 0, w ? w->key_capture_refusal() : 0,
                 g_ff_refusal, g_wait_release ? 1 : 0,
                 setting("audio.volume", 100), setting("display.fullscreen"), frlg::pc_diorama() ? 1 : 0, row_text.c_str());
    std::fflush(stderr);
}
void diag_run_step(const std::string& step) {
    using HW = gbarecomp::HostWindow;
    const std::string key = step.substr(0, step.find('='));
    const std::string val = step.find('=') == std::string::npos ? "" : step.substr(step.find('=') + 1);
    if (key == "open") {
        if (val == "page2") settings_menu_open_page2();
        else if (val == "setup") open_setup();
        else open_standalone();
    } else if (key == "cat") {
        if (val == "game") settings_hd_page1();
        else {
            if (g_mode == Mode::None) settings_menu_open_page2();   // from the game's page 1, like a click on the tag
            settings_hd_category(sub_from_name(val));
        }
    } else if (key == "step") {
        const int dir = std::atoi(val.c_str()) < 0 ? -1 : +1;
        // From the game's own OPTION page the real route is menu_peek.
        if (g_mode == Mode::None) menu_peek(dir < 0 ? HW::kMenuPageLeft : HW::kMenuPageRight, false);
        else settings_hd_category_step(dir);
    }
    else if (key == "mouse") {   // mouse=row,dir,pressed,moved  (HD page route: row index, dir -1/0/1, pressed bits 1 left 2 right)
        int row = -1, dir = 0, pressed = 0, moved = 1;
        std::sscanf(val.c_str(), "%d,%d,%d,%d", &row, &dir, &pressed, &moved);
        settings_hd_mouse(row, dir, moved != 0, static_cast<unsigned>(pressed), 0);
    } else if (key == "event") {
        // event=key,<sdl scancode>,<1 down|0 up>   or   event=pad,<button name or number>,<1|0>
        // Pushed into the SDL queue, so they travel the REAL HostWindow event loop (capture swallowing, hotkeys,
        // controller cancel). They do NOT change SDL_GetKeyboardState, which is how raw capture polls the keyboard.
        char kind[8] = {}, what[24] = {};
        int down = 1;
        if (std::sscanf(val.c_str(), "%7[^,],%23[^,],%d", kind, what, &down) >= 2) {
            SDL_Event ev;
            SDL_zero(ev);
            if (!std::strcmp(kind, "key")) {
                const int sc = std::atoi(what);
                ev.type = down ? SDL_KEYDOWN : SDL_KEYUP;
                ev.key.state = down ? SDL_PRESSED : SDL_RELEASED;
                ev.key.repeat = 0;
                ev.key.keysym.scancode = static_cast<SDL_Scancode>(sc);
                ev.key.keysym.sym = SDL_GetKeyFromScancode(static_cast<SDL_Scancode>(sc));
                SDL_PushEvent(&ev);
            } else if (!std::strcmp(kind, "pad")) {
                int button = SDL_GameControllerGetButtonFromString(what);
                if (button == SDL_CONTROLLER_BUTTON_INVALID) button = std::atoi(what);
                ev.type = down ? SDL_CONTROLLERBUTTONDOWN : SDL_CONTROLLERBUTTONUP;
                ev.cbutton.state = down ? SDL_PRESSED : SDL_RELEASED;
                ev.cbutton.button = static_cast<Uint8>(button);
                SDL_PushEvent(&ev);
            }
        }
    } else if (key == "wheel") settings_hd_mouse(-1, 0, false, 0, std::atoi(val.c_str()));
    else if (key == "cmouse") {  // cmouse=nx,ny,pressed,moved  (classic page route, native 240x160 coordinates)
        float nx = 0, ny = 0; int pressed = 0, moved = 1;
        std::sscanf(val.c_str(), "%f,%f,%d,%d", &nx, &ny, &pressed, &moved);
        settings_menu_mouse(nx, ny, true, moved != 0, static_cast<unsigned>(pressed), 0);
    } else if (key == "action") {   // action=up|down|left|right|accept|back|close|pageleft|pageright through the real key route
        const int kinds[] = {HW::kMenuUp, HW::kMenuDown, HW::kMenuLeft, HW::kMenuRight, HW::kMenuAccept, HW::kMenuBack,
                                      HW::kMenuClose, HW::kMenuPageLeft, HW::kMenuPageRight};
        const char* names[] = {"up", "down", "left", "right", "accept", "back", "close", "pageleft", "pageright"};
        for (int i = 0; i < 9; ++i) if (val == names[i]) menu_input(kinds[i], false);
    }
    else if (key == "back") menu_input(HW::kMenuBack, false);
    else if (key == "close") menu_input(HW::kMenuClose, false);
}
void diagnostic_tick() {
    static const char* open_spec = std::getenv("FRLG_SETTINGS_OPEN");
    static const char* seq_spec = std::getenv("FRLG_SETTINGS_SEQ");
    static std::vector<std::string> steps;
    static std::size_t next = 0;
    static long wait_until = -1;
    static bool open_done = false, parsed = false;
    const long pumps = g_pump_overlay + g_pump_render;
    if (seq_spec && *seq_spec) {
        if (!parsed) {
            parsed = true;
            std::string cur;
            for (const char* q = seq_spec;; ++q) {
                if (*q == ';' || !*q) { if (!cur.empty()) steps.push_back(cur); cur.clear(); if (!*q) break; }
                else cur += *q;
            }
            static const char* g = std::getenv("FRLG_SETTINGS_GAP");
            wait_until = pumps + (g && *g ? std::atoi(g) : 20) + 30;   // let the game start first
        }
        if (next < steps.size() && pumps >= wait_until) {
            const std::string& step = steps[next];
            static const char* g = std::getenv("FRLG_SETTINGS_GAP");
            const long gap = g && *g ? std::atoi(g) : 20;
            if (step.compare(0, 5, "wait=") == 0) wait_until = pumps + std::atoi(step.c_str() + 5);
            else { diag_run_step(step); wait_until = pumps + gap; }
            diag_log(static_cast<int>(next), step);
            ++next;
        }
        return;
    }
    if (!open_spec || !*open_spec || open_done || g_mode != Mode::None) return;
    static const char* at = std::getenv("FRLG_SETTINGS_OPEN_FRAME");
    if (pumps < (at && *at ? std::atoi(at) : 30)) return;
    open_done = true;
    std::string s = open_spec;
    const std::string mode = s.substr(0, s.find(':'));
    s = s.find(':') == std::string::npos ? "" : s.substr(s.find(':') + 1);
    const std::string cat = s.substr(0, s.find(':'));
    const int cursor = s.find(':') == std::string::npos ? 0 : std::atoi(s.c_str() + s.find(':') + 1);
    if (mode == "setup") open_setup(); else open_standalone();
    if (const int to = sub_from_name(cat)) switch_category(sub_of(to));
    const int n = static_cast<int>(page_items().size());
    g_cursor = std::clamp(cursor, 0, std::max(0, n - 1));
    fix_scroll();
}

void page1_overlay(gbarecomp::HostOverlay* ov) {
    if (g_ready) { ++g_pump_overlay; diagnostic_tick(); }
    if (!g_ready || g_mode != Mode::None || !ov) return;
    if (frlg::hd_ui_part(frlg::kHdPartScreens)) return;  // the HD OPTION screen draws page 1
    const OptionScreen o = read_option_screen();
    if (!o.present) return;
    const Look look = live_look();
    Canvas& c = g_canvas;
    c.clear();
    draw_hint_bar(c, look, false, true);
    draw_title_tag(c, look, "1/2");
    flush(ov, c, 256, true, look.bar_fill);
}

void page2_render(gbarecomp::HostOverlay* ov) {
    const OptionScreen o = read_option_screen();
    if (!o.ptr || o.ptr < 0x02000000u || o.ptr >= 0x0203FF00u) {  // screen gone
        finish_mode();
        return;
    }
    if (!o.present) return;
    g_fade_bright = 1.0f;
    g_fade_black = 0.0f;
    if (frlg::hd_ui_part(frlg::kHdPartScreens)) { frlg::hd_ui_render_menu(ov); return; }
    const Look look = live_look();
    Canvas& c = g_canvas;
    c.clear();
    draw_hint_bar(c, look, true, true);
    draw_title_tag(c, look, g_sub == Sub::None ? "2/2" : category_name(g_sub));
    draw_list(c, look);
    flush(ov, c, 256, true, look.bar_fill);
}

void standalone_render(gbarecomp::HostOverlay* ov) {
    const uint32_t now = ov->host_ms();
    if (!g_fade_started) { g_fade_start = now; g_fade_started = true; }
    const uint32_t t = now - g_fade_start;
    // Opening: the game fades to black, then the screen fades in from black.
    // Closing: the reverse. Each half is half of a 16-frame palette fade.
    const uint32_t half = kFadeMs / 2;
    int black_alpha = 255, bright = 256;
    if (!g_closing) {
        if (t < half) { black_alpha = static_cast<int>(255 * t / half); bright = 0; }
        else if (t < kFadeMs) bright = static_cast<int>(256 * (t - half) / half);
    } else {
        if (t < half) bright = static_cast<int>(256 - 256 * t / half);
        else if (t < kFadeMs) { bright = 0; black_alpha = static_cast<int>(255 - 255 * (t - half) / half); }
        else { finish_mode(); return; }
    }
    g_fade_bright = bright / 256.0f;
    g_fade_black = black_alpha / 255.0f;
    if (frlg::hd_ui_part(frlg::kHdPartScreens)) { frlg::hd_ui_render_menu(ov); return; }
    const int vw = ov->view_width(), vh = ov->view_height();
    ov->fill_rect(0, 0, static_cast<float>(vw), static_cast<float>(vh), 0,
                  {0, 0, 0, static_cast<uint8_t>(black_alpha)}, gbarecomp::OverlaySpace::View);
    if (bright <= 0) return;
    const Look look = rom_look();
    Canvas& c = g_canvas;
    c.clear();
    draw_hint_bar(c, look, true, false);
    // Title window: the standard text window frame (gTextWindowPalettes[3]).
    const Palette std_pal = rom_palette(kTextWindowPalettes + 3 * 32);
    draw_frame(c, 2, 3, 26, 2, kStdTextWindowGfx, std_pal, &look.list.c[kWhite]);
    std::vector<uint8_t> title;
    for (uint32_t a = kTextOption; rom8(a) != 0xFF && title.size() < 16; ++a) title.push_back(rom8(a));
    c.text(g_font_normal, kTitleX + 8, kTitleY + 1, title, look.list.c[kDarkGray],
           look.list.c[kLightGray], look.list);
    draw_title_tag(c, look, category_name(g_sub));
    // List window: the player's chosen frame.
    const int ft = saved_frame_type();
    draw_frame(c, 2, 7, 26, 12, rom32(kUserFrames + 8u * ft),
               rom_palette(rom32(kUserFrames + 8u * ft + 4u)), nullptr);
    draw_list(c, look);
    flush(ov, c, bright, true, look.bar_fill);
}

// First-run setup: up over a black screen from the first frame; leaving
// fades it out like the stand-alone page.
void setup_render(gbarecomp::HostOverlay* ov) {
    const uint32_t now = ov->host_ms();
    if (!g_fade_started) { g_fade_start = now; g_fade_started = true; }
    const uint32_t t = now - g_fade_start;
    const uint32_t half = kFadeMs / 2;
    int black_alpha = 255, bright = 256;
    if (!g_closing) {
        if (t < kFadeMs) bright = static_cast<int>(256 * t / kFadeMs);
    } else {
        if (t < half) bright = static_cast<int>(256 - 256 * t / half);
        else if (t < kFadeMs) { bright = 0; black_alpha = static_cast<int>(255 - 255 * (t - half) / half); }
        else { finish_mode(); return; }
    }
    g_fade_bright = bright / 256.0f;
    g_fade_black = black_alpha / 255.0f;
    frlg::hd_ui_render_menu(ov);
}

// Any key currently down (SDL scancodes 4..), or 0.
int key_down_any(gbarecomp::HostWindow* w) {
    for (int sc = 4; sc < 290; ++sc) if (w->key_down(sc)) return sc;
    return 0;
}

void ff_capture_frame() {
    auto* w = window();
    if (!w) { g_ff_capture = false; g_wait_release = false; return; }
    const int down = key_down_any(w);
    if (g_wait_release) { if (!down) g_wait_release = false; return; }
    if (!g_ff_capture) return;
    if (!w->raw_key_capture()) { g_ff_capture = false; g_wait_release = true; return; }   // cancelled (pad B / Start, or end_captures)
    if (!g_ff_capture_armed) { if (!down) g_ff_capture_armed = true; return; }
    if (!down) return;
    if (down != 41 /* Escape cancels */) {
        // reserved keys (menus, 3D toggle, fullscreen) and keys bound to a GBA input are ignored: keep waiting
        int bound[10 * gbarecomp::HostWindow::kKeySlots], n = 0;
        for (int bit = 0; bit < 10; ++bit)
            for (int slot = 0; slot < gbarecomp::HostWindow::kKeySlots; ++slot)
                if (const int sc = w->key_binding(bit, slot)) bound[n++] = sc;
        if (const int why = ini::ff_key_refusal(down, bound, n)) { g_ff_refusal = why; g_ff_capture_armed = false; return; }
        frlg::set_pc_ff_key(down);
    }
    g_ff_capture = false;
    g_ff_refusal = 0;
    w->set_raw_key_capture(false);
    g_wait_release = true;   // the key that was pressed must not also act in the menu
}

void menu_render(gbarecomp::HostOverlay* ov) {
    if (!ov || !g_ready) return;
    g_now_ms = ov->host_ms();
    if (auto* w0 = window()) { static bool filter_set = false; if (!filter_set) { w0->set_key_bind_filter(gba_bind_filter); filter_set = true; } }
    g_quit.observe(g_cursor, cat_of(g_sub));
    ++g_pump_render;
    diagnostic_tick();
    ff_capture_frame();
    static bool capturing = false;
    if (auto* w = window()) {
        const bool now = w->key_capture_bit() >= 0;
        if (capturing && !now) frlg::pc_input_bindings_changed();
        capturing = now;
    }
    if (g_mode == Mode::Page2) page2_render(ov);
    else if (g_mode == Mode::Standalone) standalone_render(ov);
    else if (g_mode == Mode::Setup) setup_render(ov);
}

// ── title-screen main menu: OPTION in the MYSTERY GIFT slot ──────────────
bool on_main_menu() { return (bus_read_u32(kMain + 4) & ~1u) == kCB2_MainMenu; }

// IsMysteryGiftEnabled(): TRUE on the main menu so its third box appears.
int main_menu_slot_hook(uint32_t, int, ArmCpuState* cpu) {
    if (!g_ready || !on_main_menu()) return 0;
    cpu->R[0] = 1;
    cpu->R[15] = cpu->R[14] & ~1u;
    return 1;
}

// IsWirelessAdapterConnected() from Task_ExecuteMainMenuSelection: report
// connected so the game takes its own clean exit path (free windows,
// destroy task, SetMainCallback2) — whose target literal now points at the
// OPTION screen — and make that screen return to the main menu.
int main_menu_select_hook(uint32_t, int, ArmCpuState* cpu) {
    if (!g_ready || !on_main_menu()) return 0;
    const uint32_t lr = cpu->R[14] & ~1u;
    if (lr < kTaskExecuteMainMenu || lr >= kTaskExecuteMainMenuEnd) return 0;
    bus_write_u32(kMain + 8, kCB2_InitMainMenu | 1u);  // gMain.savedCallback
    cpu->R[0] = 1;
    cpu->R[15] = cpu->R[14] & ~1u;
    return 1;
}

struct HookRegistrar {
    HookRegistrar() {
        gba_mod_register_function_entry_plugin(kHookMgEnabled, kIsMysteryGiftEnabled, 1,
                                               main_menu_slot_hook);
        gba_mod_register_function_entry_plugin(kHookMgSelect, kIsWirelessConnected, 1,
                                               main_menu_select_hook);
    }
} g_hook_registrar;

void write_rom_string(uint8_t* rom, uint32_t addr, const char* s) {
    uint32_t a = addr - kRomBase;
    for (const char* p = s; *p; ++p) rom[a++] = encode_char(*p);
    rom[a] = 0xFF;
}
void put32(uint8_t* rom, uint32_t addr, uint32_t v) {
    const uint32_t a = addr - kRomBase;
    for (int i = 0; i < 4; ++i) rom[a + i] = static_cast<uint8_t>(v >> (8 * i));
}

void on_rom_loaded(uint8_t* rom, std::size_t size) {
    g_rom = rom;
    g_rom_size = size;
    static const char kMg[] = "MYSTERY GIFT";
    bool ok = size >= 0x01000000u && rom8(kFontNormalWidths + 0xBB) > 0 &&
              rom32(kUserFrames) >= kRomBase &&
              rom32(kMysteryGiftCb2Literal) == (kCB2_InitMysteryGift | 1u) &&
              rom8(kTextOption) == encode_char('O');
    for (int i = 0; ok && kMg[i]; ++i) {
        const uint8_t want = kMg[i] == ' ' ? 0x00 : encode_char(kMg[i]);
        ok = rom8(kTextMysteryGift + i) == want;
    }
    if (!ok) {
        std::fprintf(stderr, "[frlg-option] ROM layout not recognised; extras disabled\n");
        g_ready = false;
        return;
    }
    // In-memory presentation patch for the title menu's third box.
    write_rom_string(rom, kTextMysteryGift, "OPTION");
    put32(rom, kMysteryGiftCb2Literal, kCB2_OptionsMenu | 1u);
    gba_mod_set_function_hook_enabled(kHookMgEnabled, 1);
    gba_mod_set_function_hook_enabled(kHookMgSelect, 1);
    frlg::pc_input_rom_loaded(rom, size);
    frlg::hd_ui_rom_loaded();
    g_ready = true;
    if (!frlg::pc_setup_done()) open_setup();   // first run: ask how to play
    std::fprintf(stderr, "[frlg-option] OPTION page 2 + title-menu OPTION ready\n");
}

}  // namespace

bool settings_hd_view(SettingsHdView* out) {
    if (!out) return false;
    *out = SettingsHdView{};
    if (!g_ready) return false;
    out->mode = g_mode == Mode::Page2 ? 1 : g_mode == Mode::Standalone ? 2 : g_mode == Mode::Setup ? 3 : 0;
    out->controls = g_controls;
    out->sub = g_sub == Sub::Controls ? 1 : g_sub == Sub::Interface ? 2 : g_sub == Sub::Extras ? 3 : 0;
    out->cursor = g_cursor;
    auto* w = window();
    out->capturing = (w && w->key_capture_bit() >= 0) || g_ff_capture;
    if (w && w->key_capture_bit() >= 0 && w->key_capture_refusal()) out->notice = ini::refusal_text(false, w->key_capture_refusal());
    else if (g_ff_capture && g_ff_refusal) out->notice = ini::refusal_text(true, g_ff_refusal);
    out->bright = g_fade_bright;
    out->black = g_fade_black;
    static const char* const kScreen[] = {"Window", "Borderless", "Fullscreen"};
    static const char* const kOnOff[] = {"Off", "On"};
    static const char* const kWide[] = {"Off", "16:9"};
    static const char* const kStyle[] = {"Classic", "Modern"};
    for (const Item& it : page_items()) {
        SettingsHdRow r;
        r.enabled = row_enabled(it);
        switch (it.row) {
            case Row::Screen: r.label = "Display Mode"; r.kind = 1; r.names = kScreen; r.choices = 3;
                r.choice = std::clamp(setting("display.fullscreen"), 0, 2); break;
            case Row::Widescreen: r.label = "Widescreen"; r.kind = 1; r.names = kWide; r.choices = 2;
                r.choice = setting("display.view_mode") ? 1 : 0; break;
            case Row::WindowSize: r.label = "Window Size"; r.kind = 0;
                r.value = std::to_string(setting("display.window_scale", 3)) + "\xc3\x97"; r.choice = -2; break;
            case Row::Smoothing: r.label = "Smoothing"; r.kind = 1; r.names = kOnOff; r.choices = 2;
                r.choice = setting("graphics.linear_filter") ? 1 : 0; break;
            case Row::Interface: r.label = "Interface"; r.value = interface_summary(); break;
            case Row::UiBattle: case Row::UiText: case Row::UiTitle: case Row::UiDex: case Row::UiScreens:
            case Row::UiDialogue: {
                static const char* const kUiLabel[] = {"Battles", "Field Menus", "Title Menu", "Pok\xc3\xa9" "dex",
                                                       "Menus & Screens", "Dialogue Boxes"};
                r.label = kUiLabel[ui_part(it.row)];
                r.kind = 1; r.names = kStyle; r.choices = 2;
                r.choice = frlg::hd_ui_part(ui_part(it.row)) ? 1 : 0;
                break;
            }
            case Row::PadLayout: {
                static const char* const kPad[] = {"Xbox", "Nintendo"};
                r.label = "Controller Buttons"; r.kind = 1; r.names = kPad; r.choices = 2;
                r.choice = frlg::pc_pad_swap_ab() ? 1 : 0;
                break;
            }
            case Row::PadStick: r.label = "Left Stick Moves"; r.kind = 1; r.names = kOnOff; r.choices = 2;
                r.choice = frlg::pc_pad_stick() ? 1 : 0; break;
            case Row::StartGame: r.label = "Start Playing"; break;
            case Row::Extras: r.label = "Extras"; r.value = "Fast forward, Rare Candy, 3D"; break;
            case Row::FfSpeed: {
                static const char* const kSp[] = {"2\xc3\x97", "3\xc3\x97", "4\xc3\x97", "5\xc3\x97", "6\xc3\x97",
                                                  "8\xc3\x97", "10\xc3\x97"};
                r.label = "Fast Forward Speed"; r.kind = 1; r.names = kSp; r.choices = kFfSpeedCount;
                r.choice = ff_speed_index();
                break;
            }
            case Row::FfKey: {
                r.label = "Fast Forward Key"; r.kind = 3; r.choice = -1;
                const int conflict = frlg::pc_ff_conflict_bit();
                r.value = g_ff_capture ? "..." : frlg::pc_ff_key_name();
                if (conflict >= 0)
                    r.note = std::string("Fast forward is off for this key: it is also the ") + ini::gba_input_name(conflict) +
                             " button. Pick another key here, or rebind that button.";
                break;
            }
            case Row::FfMode: {
                static const char* const kMode[] = {"Hold", "Toggle"};
                r.label = "Key Mode"; r.kind = 1; r.names = kMode; r.choices = 2;
                r.choice = frlg::pc_ff_toggle() ? 1 : 0;
                break;
            }
            case Row::RareCandy: r.label = "Infinite Rare Candy"; r.kind = 1; r.names = kOnOff; r.choices = 2;
                r.choice = frlg::pc_rare_candy() ? 1 : 0; break;
            case Row::Diorama: r.label = "3D View (test)"; r.kind = 1; r.names = kOnOff; r.choices = 2;
                r.choice = frlg::pc_diorama() ? 1 : 0; break;
            case Row::ModernText: r.label = "Text Style"; r.kind = 1; r.names = kStyle; r.choices = 2;
                r.choice = frlg::hd_ui_modern_text() ? 1 : 0; break;
            case Row::Volume: r.label = "Volume"; r.kind = 2; r.choice = setting("audio.volume", 100); break;
            case Row::AutoRun: r.label = "Auto Run"; r.kind = 1; r.names = kOnOff; r.choices = 2;
                r.choice = frlg::pc_auto_run() ? 1 : 0; break;
            case Row::Mouse: r.label = "Mouse Control"; r.kind = 1; r.names = kOnOff; r.choices = 2;
                r.choice = frlg::pc_mouse_enabled() ? 1 : 0; break;
            case Row::Controls: r.label = "Controls"; r.value = "Keys & controller"; break;
            case Row::Quit: r.label = "Quit Game";
                r.value = g_quit.armed(g_now_ms) ? "Press again to quit" : ""; break;
            case Row::Cancel: r.label = "Close"; break;
            case Row::Bind: {
                static const char* const kBind[] = {"Confirm", "Back", "Use Item", "Menu", "Right",
                                                    "Left", "Up", "Down", "R Button", "L Button"};
                r.label = (it.bit >= 0 && it.bit < 10) ? kBind[it.bit] : it.label;
                r.kind = 3;
                r.value = row_value(it);
                r.choice = it.bit;
                break;
            }
            case Row::ResetBinds: r.label = "Restore Default Keys"; break;
            case Row::Back: r.label = "Done"; break;
        }
        out->rows.push_back(std::move(r));
    }
    return out->mode != 0;
}

void settings_hd_mouse(int row, int dir, bool moved, unsigned pressed, int wheel) {
    using HW = gbarecomp::HostWindow;
    if (!g_ready || g_mode == Mode::None) return;
    if (mouse_capture_gate(pressed) || input_blocked()) return;
    if (pressed & 2) { menu_input(HW::kMenuBack, false); return; }
    if (wheel) {
        for (int i = 0; i < std::min(8, std::abs(wheel)); ++i)
            menu_input(wheel > 0 ? HW::kMenuUp : HW::kMenuDown, false);
        return;
    }
    const auto items = page_items();
    if (row < 0 || row >= static_cast<int>(items.size())) return;
    if (moved && row != g_cursor) mouse_cursor(row);
    if (!(pressed & 1)) return;
    mouse_cursor(row);
    const Item& it = items[row];
    if (it.row == Row::Volume && dir == 0) return;   // the slider halves step it; the row body only selects
    if (is_value_row(it.row)) change(it, dir ? dir : +1);
    else menu_input(HW::kMenuAccept, false);
}

bool settings_menu_uses_hd() {
    if (!g_ready) return false;
    return g_mode == Mode::Setup || (g_mode != Mode::None && frlg::hd_ui_part(frlg::kHdPartScreens));
}

const char* settings_build_id() {
#ifdef FRLG_BUILD_ID
    return "build " FRLG_BUILD_ID " " __DATE__ " " __TIME__;
#else
    return "build " __DATE__ " " __TIME__;
#endif
}

bool settings_capture_active() {
    auto* w = window();
    return g_ff_capture || (w && (w->key_capture_bit() >= 0 || w->raw_key_capture()));
}

void settings_hd_category(int sub) {
    if (!g_ready || g_mode == Mode::None || g_closing || g_exiting || sub < 0 || sub >= nav::kCatCount) return;
    auto* w = window();
    if (w && w->key_capture_bit() >= 0) return;
    if (g_ff_capture || g_wait_release) return;
    if (sub_of(sub) != g_sub) switch_category(sub_of(sub));
}

void settings_hd_category_step(int dir) {
    using HW = gbarecomp::HostWindow;
    menu_input(dir < 0 ? HW::kMenuPageLeft : HW::kMenuPageRight, false);
}

void settings_hd_page1() {
    // The Game tab works from every page-2 category, with the same guards as
    // the other HD actions: not while a key is being captured or the screen closes.
    if (nav::game_tab_action(static_cast<int>(g_mode)) != nav::kLeavePage2 || !g_ready || g_exiting) return;
    auto* w = window();
    if (w && w->key_capture_bit() >= 0) return;
    if (g_ff_capture || g_wait_release) return;
    leave_page2();
}

void settings_menu_mouse(float nx, float ny, bool inside, bool moved, unsigned pressed,
                         int wheel) {
    using HW = gbarecomp::HostWindow;
    if (!g_ready || g_mode == Mode::None) return;
    if (mouse_capture_gate(pressed) || input_blocked()) return;   // waiting for a key press / fading
    if (pressed & 2) { menu_input(HW::kMenuBack, false); return; }
    if (wheel) {
        for (int i = 0; i < std::min(8, std::abs(wheel)); ++i)
            menu_input(wheel > 0 ? HW::kMenuUp : HW::kMenuDown, false);
        return;
    }
    if (!inside) return;
    // The "2/2" tag in the title window flips back to the game's page 1.
    if ((pressed & 1) && g_mode == Mode::Page2 && g_sub == Sub::None && ny >= kTitleY - 4 &&
        ny < kTitleY + kTitleH + 4 && nx >= kTitleX + kTitleW - 48 && nx < kTitleX + kTitleW) {
        leave_page2();
        return;
    }
    const auto items = page_items();
    const int n = static_cast<int>(items.size());
    int row = -1;
    if (nx >= kListX && nx < kListX + kListW && ny >= kListY + 2) {
        const int r = static_cast<int>((ny - kListY - 2) / 13);
        if (r >= 0 && r < kVisibleRows && g_scroll + r < n) row = g_scroll + r;
    }
    // Scroll arrows at the list's top/bottom edge.
    if (row < 0 && (pressed & 1) && nx >= kListX && nx < kListX + kListW) {
        if (ny >= kListY - 4 && ny < kListY + 2 && g_scroll > 0) { --g_scroll; mouse_cursor(std::max(g_cursor - 1, 0)); }
        return;
    }
    if (row < 0) return;
    if (moved && row != g_cursor) mouse_cursor(row);
    if (!(pressed & 1)) return;
    mouse_cursor(row);
    const Item& it = items[row];
    switch (it.row) {
        case Row::Volume:
            // Left half of the value turns it down, right half up; a click on the label only selects.
            if (ini::value_area_click(nx, kListX + 128.0f)) change(it, nx < kListX + 130 + 20 ? -1 : +1);
            break;
        default:
            if (is_value_row(it.row)) change(it, +1);
            else menu_input(HW::kMenuAccept, false);
            break;
    }
}

bool settings_menu_open_page2() {
    if (!g_ready || g_mode != Mode::None) return false;
    const OptionScreen o = read_option_screen();
    if (!o.present || o.load_state != 2) return false;
    enter_page2(0);
    return true;
}

void install_settings_menu(gbarecomp::RunOptions& opts, const std::string& exe_dir) {
    std::fprintf(stderr, "[frlg] %s\n", settings_build_id());
    std::fflush(stderr);
    g_config_dir = exe_dir.empty() ? std::string(".") : exe_dir;
    g_widescreen_supported = opts.widescreen_view_width > 240 && opts.launcher_expose_widescreen;
    opts.rom_loaded = on_rom_loaded;
    opts.game_menu_is_open = menu_is_open;
    opts.game_menu_open = open_menu;
    opts.game_menu_input = menu_input;
    opts.game_menu_render = menu_render;
    opts.game_menu_peek = menu_peek;
    opts.game_menu_pauses = false;
    opts.host_overlay = page1_overlay;
}

}  // namespace frlg
