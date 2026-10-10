// fr_guest.h — read-only access to FireRed (USA rev0) guest memory, shared by
// the PC input policy and the HD UI. Addresses come from the pokefirered
// symbol map and are used only as a description of the data layout.

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>

#include "gba_bus.h"
#include "runtime_bus_bridge.h"

namespace frlg {

namespace g {
constexpr std::uint32_t Main             = 0x030030F0;  // gMain: cb1 +0, cb2 +4
constexpr std::uint32_t Tasks            = 0x03005090;  // 16 x 0x28
constexpr std::uint32_t Windows          = 0x020204B4;  // 32 x 12
constexpr std::uint32_t TextPrinters     = 0x02020034;  // 32 x 0x24
constexpr std::uint32_t PaletteFade      = 0x02037AB8;
constexpr std::uint32_t PlttUnfaded      = 0x020371F8;
constexpr std::uint32_t PlttFaded        = 0x020375F8;
constexpr std::uint32_t Menu             = 0x0203ADE4;  // menu.c sMenu
constexpr std::uint32_t Sprites          = 0x0202063C;  // 64 x 0x44
constexpr std::uint32_t SpriteTileTags   = 0x03000AE8;  // u16[64]
constexpr std::uint32_t SpriteTileRanges = 0x03000B68;  // {u16 start, u16 count}[64]
constexpr std::uint32_t SaveBlock1Ptr    = 0x03005008;
constexpr std::uint32_t SaveBlock2Ptr    = 0x0300500C;
constexpr std::uint32_t BackupMap        = 0x03005040;  // VMap {width, height, map*}
constexpr std::uint32_t GlyphInfo        = 0x03003DA0;  // pixels[0x80], width, height
}  // namespace g

struct Mem {
    const std::uint8_t* ewram = nullptr;
    const std::uint8_t* iwram = nullptr;
    const std::uint8_t* rom = nullptr;
    std::size_t rom_size = 0;
    const std::uint8_t* vram = nullptr;
    const std::uint8_t* io = nullptr;
    const std::uint8_t* pal = nullptr;
    const std::uint8_t* oam = nullptr;

    static Mem current() {
        Mem m;
        auto* bus = gbarecomp::active_bus();
        if (!bus) return m;
        m.ewram = bus->ewram_ptr();
        m.iwram = bus->iwram_ptr();
        m.rom = bus->rom_ptr();
        m.rom_size = bus->rom_size();
        m.vram = bus->vram_ptr();
        m.io = bus->io().raw();
        m.pal = bus->pal_ptr();
        m.oam = bus->oam_ptr();
        return m;
    }
    bool ok() const { return ewram && iwram && io && vram; }
    const std::uint8_t* ptr(std::uint32_t a, std::size_t n) const {
        if (a >= 0x02000000u && a + n <= 0x02040000u) return ewram + (a - 0x02000000u);
        if (a >= 0x03000000u && a + n <= 0x03008000u) return iwram + (a - 0x03000000u);
        if (a >= 0x06000000u && a + n <= 0x06018000u) return vram + (a - 0x06000000u);
        if (a >= 0x05000000u && a + n <= 0x05000400u && pal) return pal + (a - 0x05000000u);
        if (a >= 0x07000000u && a + n <= 0x07000400u && oam) return oam + (a - 0x07000000u);
        if (a >= 0x08000000u && rom && a - 0x08000000u + n <= rom_size) return rom + (a - 0x08000000u);
        return nullptr;
    }
    bool valid(std::uint32_t a, std::size_t n = 1) const { return ptr(a, n) != nullptr; }
    std::uint8_t u8(std::uint32_t a) const { auto* p = ptr(a, 1); return p ? p[0] : 0; }
    std::int8_t s8(std::uint32_t a) const { return static_cast<std::int8_t>(u8(a)); }
    std::uint16_t u16(std::uint32_t a) const {
        auto* p = ptr(a, 2);
        return p ? static_cast<std::uint16_t>(p[0] | (p[1] << 8)) : 0;
    }
    std::int16_t s16(std::uint32_t a) const { return static_cast<std::int16_t>(u16(a)); }
    std::uint32_t u32(std::uint32_t a) const {
        auto* p = ptr(a, 4);
        return p ? (p[0] | (p[1] << 8) | (p[2] << 16) | (std::uint32_t(p[3]) << 24)) : 0;
    }
    std::uint32_t func(std::uint32_t a) const { return u32(a) & ~1u; }
    std::uint16_t io16(std::uint32_t off) const {
        return static_cast<std::uint16_t>(io[off] | (io[off + 1] << 8));
    }
    std::uint32_t cb1() const { return func(g::Main); }
    std::uint32_t cb2() const { return func(g::Main + 4); }
};

struct Rect {
    int x = 0, y = 0, w = 0, h = 0;
    bool contains(float px, float py, float pad = 0) const {
        return w > 0 && h > 0 && px >= x - pad && py >= y - pad && px < x + w + pad &&
               py < y + h + pad;
    }
    bool empty() const { return w <= 0 || h <= 0; }
};

// gWindows[i]: {bg, tilemapLeft, tilemapTop, width, height, paletteNum,
// u16 baseBlock} + u8* tileData.
struct WindowInfo {
    int bg = 0, left = 0, top = 0, width = 0, height = 0, palette = 0, base = 0;
    std::uint32_t tiles = 0;
    bool live() const { return width > 0 && height > 0 && tiles != 0 && bg < 4; }
};

inline WindowInfo window_info(const Mem& m, int window) {
    WindowInfo w;
    if (window < 0 || window >= 32) return w;
    const std::uint32_t a = g::Windows + window * 12;
    w.bg = m.u8(a);
    w.left = m.u8(a + 1);
    w.top = m.u8(a + 2);
    w.width = m.u8(a + 3);
    w.height = m.u8(a + 4);
    w.palette = m.u8(a + 5);
    w.base = m.u16(a + 6);
    w.tiles = m.u32(a + 8);
    if (w.bg == 0xFF) w.width = 0;
    return w;
}

// Screen rectangle (native coordinates) of an allocated window from its
// template + live BG scroll.
inline Rect window_rect(const Mem& m, int window) {
    const WindowInfo w = window_info(m, window);
    if (!w.live()) return {};
    const int bg = w.bg;
    const std::uint16_t cnt = m.io16(0x08 + 2 * bg);
    const int map_w = (cnt & 0x4000) ? 512 : 256, map_h = (cnt & 0x8000) ? 512 : 256;
    const int hofs = m.io16(0x10 + 4 * bg) & 0x1FF, vofs = m.io16(0x12 + 4 * bg) & 0x1FF;
    auto place = [](int pos, int size) {
        pos = ((pos % size) + size) % size;
        return pos >= size - 16 ? pos - size : pos;
    };
    return {place(w.left * 8 - hofs, map_w), place(w.top * 8 - vofs, map_h), w.width * 8,
            w.height * 8};
}

// True when the window's tiles are on screen: its BG is enabled and the VRAM
// tilemap holds the window's first tile at its top-left cell.
inline bool window_shown(const Mem& m, int window) {
    const WindowInfo w = window_info(m, window);
    if (!w.live()) return false;
    const std::uint16_t dispcnt = m.io16(0);
    if (!(dispcnt & (0x100u << w.bg))) return false;
    const std::uint16_t cnt = m.io16(0x08 + 2 * w.bg);
    const std::uint32_t screen = 0x06000000u + ((cnt >> 8) & 31) * 0x800;
    const bool wide = (cnt & 0x4000) != 0;
    int tx = w.left, ty = w.top;
    std::uint32_t block = 0;
    if (wide && tx >= 32) { block += 1; tx -= 32; }
    if (ty >= 32) { block += wide ? 2 : 1; ty -= 32; }
    const std::uint16_t entry = m.u16(screen + block * 0x800 + (ty * 32 + tx) * 2);
    return (entry & 0x3FF) == (w.base & 0x3FF);
}

// Text printer i (one per window): template + state.
struct PrinterInfo {
    bool active = false;
    int state = 0, font = 0, x = 0, y = 0, cur_x = 0, cur_y = 0;
    std::uint32_t current = 0;
};
inline PrinterInfo printer_info(const Mem& m, int window) {
    PrinterInfo p;
    const std::uint32_t a = g::TextPrinters + window * 0x24;
    p.active = m.u8(a + 0x1B) != 0;
    p.state = m.u8(a + 0x1C);
    p.current = m.u32(a);
    p.font = m.u8(a + 5);
    p.x = m.u8(a + 6);
    p.y = m.u8(a + 7);
    p.cur_x = m.u8(a + 8);
    p.cur_y = m.u8(a + 9);
    return p;
}

// ── scene snapshot (classified once per frame by the PC input policy) ─────
enum class BattleCtl { None, Action, Move };

struct Scene {
    std::uint32_t cb1 = 0, cb2 = 0;
    bool field = false, field_free = false;
    bool text_active = false;
    // sMenu
    bool menu = false, menu_grid = false, menu_wrap = false, start_menu = false;
    int menu_left = 0, menu_top = 0, menu_cursor = 0, menu_min = 0, menu_max = 0;
    int menu_window = -1, menu_ow = 0, menu_oh = 16, menu_cols = 1;
    Rect menu_rect;
    // ListMenu
    bool list = false;
    int list_task = -1, list_total = 0, list_max = 0, list_scroll = 0, list_sel = 0;
    int list_window = -1, list_row_h = 16, list_up = 0;
    Rect list_rect;
    // battle
    BattleCtl battle = BattleCtl::None;
    int battler = 0;
    // title main menu / option page 1 / party
    int main_task = -1, main_type = 0;
    bool option_page = false;
    bool party = false;

    Rect menu_item(int i) const {
        if (menu_grid && menu_cols > 0)
            return {menu_rect.x + menu_left + (i % menu_cols) * menu_ow,
                    menu_rect.y + menu_top + (i / menu_cols) * menu_oh, menu_ow, menu_oh};
        return {menu_rect.x + menu_left, menu_rect.y + menu_top + (i - menu_min) * menu_oh,
                menu_rect.w - menu_left, menu_oh};
    }
    Rect list_row(int r) const {
        return {list_rect.x, list_rect.y + list_up + r * list_row_h, list_rect.w, list_row_h};
    }
    std::uint32_t signature() const {
        std::uint32_t s = cb2 * 2654435761u;
        if (menu) s ^= 0x100u | static_cast<unsigned>(menu_window);
        if (list) s ^= 0x10000u | (static_cast<unsigned>(list_task) << 8);
        s ^= static_cast<unsigned>(battle) << 20;
        if (main_task >= 0) s ^= 0x400000u;
        if (option_page) s ^= 0x800000u;
        return s;
    }
};

// Most recent classification (updated at the start of every guest frame).
const Scene& pc_scene();

// ── region map (Town Map / Fly) — region_map.c ──────────────────────────
namespace rmap {
constexpr std::uint32_t CB2_RegionMap        = 0x080C08B4;
constexpr std::uint32_t RegionMapPtr         = 0x020399D4;  // sRegionMap
constexpr std::uint32_t MapCursorPtr         = 0x020399E4;  // sMapCursor
constexpr std::uint32_t FlyMapPtr            = 0x020399FC;  // sFlyMap
constexpr std::uint32_t Task_RegionMap       = 0x080C04E4;
constexpr std::uint32_t Task_FlyMap          = 0x080C4F08;
constexpr std::uint32_t Task_SwitchMapMenu   = 0x080C1098;
constexpr std::uint32_t Task_DungeonPreview  = 0x080C1A94;
constexpr std::uint32_t Task_DungeonPreviewText = 0x080C1BE0;
constexpr std::uint32_t Task_MapOpenAnim     = 0x080C267C;
constexpr std::uint32_t Task_MapCloseAnim    = 0x080C2C7C;
constexpr std::uint32_t MoveMapCursor        = 0x080C3348;
// struct RegionMap: mapName[19] +0, dungeonName[19] +0x13, type +0x4796,
// permissions[4] +0x4797, selectedRegion +0x479B, mainState +0x47A0.
constexpr std::uint32_t kMapName = 0x00, kDungeonName = 0x13, kType = 0x4796, kPerms = 0x4797,
                        kSelectedRegion = 0x479B, kMainState = 0x47A0;
// struct MapCursor: x +0, y +2, inputHandler +16, selectedMapsec +20,
// selectedMapsecType +22, selectedDungeonType +24.
constexpr int kWidth = 22, kHeight = 15;          // cursor grid; cell (x, y) is
constexpr int kCellX0 = 32, kCellY0 = 32;          // native (32 + 8x, 32 + 8y), 8 px
constexpr int kCancelX = 21, kCancelY = 13, kSwitchX = 21, kSwitchY = 11;

// Function of the region map's live task (0 when none).
inline std::uint32_t task(const Mem& m) {
    for (int i = 0; i < 16; ++i) {
        const std::uint32_t t = g::Tasks + i * 0x28;
        if (!m.u8(t + 4)) continue;
        const std::uint32_t f = m.func(t);
        if (f == Task_RegionMap || f == Task_FlyMap || f == Task_SwitchMapMenu ||
            f == Task_DungeonPreview || f == Task_DungeonPreviewText || f == Task_MapOpenAnim ||
            f == Task_MapCloseAnim)
            return f;
    }
    return 0;
}

// The map takes cursor input (Task_RegionMap state 3 / Task_FlyMap state 4).
inline bool steady(const Mem& m) {
    if (m.cb2() != CB2_RegionMap) return false;
    const std::uint32_t rm = m.u32(RegionMapPtr);
    if (!m.valid(rm, 0x47C0) || !m.valid(m.u32(MapCursorPtr), 0x20)) return false;
    const std::uint32_t f = task(m);
    if (f == Task_RegionMap) return m.u8(rm + kMainState) == 3;
    if (f == Task_FlyMap) return m.valid(m.u32(FlyMapPtr), 3) && m.u8(m.u32(FlyMapPtr)) == 4;
    return false;
}

// Cursor cell as y * 32 + x; -9998 while it glides between cells, -9999
// when the map is not taking input.
inline int cursor(const Mem& m) {
    if (!steady(m)) return -9999;
    const std::uint32_t mc = m.u32(MapCursorPtr);
    if (m.func(mc + 16) == MoveMapCursor) return -9998;
    return m.s16(mc + 2) * 32 + m.s16(mc);
}
}  // namespace rmap

}  // namespace frlg
