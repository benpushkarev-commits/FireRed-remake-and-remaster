// fr_widescreen.cpp — FireRed overworld widescreen. See fr_widescreen.h.
//
// Structure follows EmeraldRecomp's FieldView / ObjectView / UiView; the
// FireRed-specific formats come from pret/pokefirered (fieldmap.c,
// field_camera.c, field_door.c, include/global.fieldmap.h), used purely as a
// data-format description.

#include "fr_widescreen.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "gba_bus.h"
#include "gba_ppu.h"
#include "runtime.h"
#include "runtime_bus_bridge.h"
#include "runtime_arm.h"

namespace frlg {
namespace {

// ── FireRed USA rev0 (pokefirered symbol map) ────────────────────────────
constexpr std::uint32_t kMain          = 0x030030F0;  // gMain (+4 = callback2)
constexpr std::uint32_t kOverworld     = 0x080565B4;  // CB2_Overworld
constexpr std::uint32_t kMapHeader     = 0x02036DFC;  // gMapHeader
constexpr std::uint32_t kBackupMap     = 0x03005040;  // VMap {Xsize, Ysize, map*}
constexpr std::uint32_t kSaveBlock1Ptr = 0x03005008;  // gSaveBlock1Ptr
constexpr std::uint32_t kCameraOffset  = 0x03000E90;  // sFieldCameraOffset
constexpr std::uint32_t kMapGroups     = 0x083526A8;  // gMapGroups
constexpr std::uint32_t kDoorGraphics  = 0x0835B5D8;  // sDoorGraphics[]
constexpr int kDoorGraphicsMax         = 40;
constexpr std::uint32_t kObjects       = 0x02036E38;  // gObjectEvents[16]
constexpr std::uint32_t kSprites       = 0x0202063C;  // gSprites[64]
constexpr std::uint32_t kOffsetX       = 0x02021BC8;  // gSpriteCoordOffsetX
constexpr std::uint32_t kOffsetY       = 0x02021BCA;  // gSpriteCoordOffsetY
constexpr std::uint32_t kWindows       = 0x020204B4;  // gWindows[32]
constexpr int kMapPadding              = 7;           // MAP_OFFSET
constexpr unsigned kPrimaryMetatiles   = 640;         // NUM_METATILES_IN_PRIMARY
constexpr unsigned kDoorTileStart      = 1016;        // NUM_TILES_TOTAL - 8
constexpr int kMaxView = kWidescreenWidth;

int floor8(int v) { return v >= 0 ? v / 8 : -((-v + 7) / 8); }
int floor2(int v) { return v >= 0 ? v / 2 : -((-v + 1) / 2); }
int wrap(int v, int m) { return (v % m + m) % m; }
int signed_ring_delta(int v) { return wrap(v + 128, 256) - 128; }
unsigned read16(const std::uint8_t* p) { return p[0] | (unsigned(p[1]) << 8); }
int s16(const std::uint8_t* p) { return static_cast<std::int16_t>(read16(p)); }
int s8(std::uint8_t b) { return static_cast<std::int8_t>(b); }
int unwrap(int raw, int reference, int modulus) {
    const int delta = ((raw - reference + modulus / 2) % modulus + modulus) % modulus;
    return reference + delta - modulus / 2;
}

// Borrowed, read-only guest regions (never the emulated bus).
struct ViewMemory {
    const std::uint8_t* ewram = nullptr;
    const std::uint8_t* iwram = nullptr;
    const std::uint8_t* rom = nullptr;
    std::size_t rom_size = 0;
    const std::uint8_t* vram = nullptr;
    const std::uint8_t* io = nullptr;
    const std::uint8_t* oam = nullptr;
    const std::uint8_t* pal = nullptr;
    const std::uint8_t* bytes(std::uint32_t a, std::size_t size) const {
        const std::uint8_t* base = nullptr;
        std::size_t off = 0, cap = 0;
        if (a >= 0x02000000 && a < 0x02040000) { base = ewram; off = a - 0x02000000; cap = 0x40000; }
        else if (a >= 0x03000000 && a < 0x03008000) { base = iwram; off = a - 0x03000000; cap = 0x8000; }
        else if (a >= 0x08000000 && a < 0x0A000000) { base = rom; off = a - 0x08000000; cap = rom_size; }
        return base && off <= cap && size <= cap - off ? base + off : nullptr;
    }
    std::uint16_t u16(std::uint32_t a) const {
        const auto* p = bytes(a, 2);
        return p ? static_cast<std::uint16_t>(read16(p)) : 0;
    }
    std::uint32_t u32(std::uint32_t a) const {
        const auto* p = bytes(a, 4);
        return p ? read16(p) | (read16(p + 2) << 16) : 0;
    }
};

enum class ViewStatus { Native, NonField, Unsupported, Unverified, Ready };
const char* status_name(ViewStatus s) {
    switch (s) {
        case ViewStatus::Native: return "native";
        case ViewStatus::NonField: return "non-field";
        case ViewStatus::Unsupported: return "unsupported-field";
        case ViewStatus::Unverified: return "unverified-camera";
        case ViewStatus::Ready: return "verified-field";
    }
    return "?";
}

struct FieldMapRegion {
    std::uint32_t header = 0;
    int x = 0, y = 0;
    std::uint8_t group = 0, number = 0;
};

// ── map access (fieldmap.c semantics) ────────────────────────────────────
struct Map {
    const ViewMemory& m;
    std::uint32_t data = 0, border = 0;
    int border_w = 0, border_h = 0;
    std::uint32_t metatiles[2]{}, attributes[2]{};
    int width = 0, height = 0;
    struct Connected {
        std::uint32_t layout = 0, header = 0, data = 0;
        int x = 0, y = 0, width = 0, height = 0;
        std::uint8_t group = 0, number = 0;
    };
    std::array<Connected, 32> connected{};
    int connected_count = 0;

    bool layout_at(std::uint32_t header, Connected& out) const {
        if (!m.bytes(header, 16)) return false;
        const auto layout = m.u32(header);
        if (!m.bytes(layout, 26)) return false;
        const int w = static_cast<int>(m.u32(layout));
        const int h = static_cast<int>(m.u32(layout + 4));
        const auto d = m.u32(layout + 12);
        if (w < 1 || h < 1 || w > 10240 || h > 10240 || w * h > 10240 ||
            !m.bytes(d, w * h * 2)) return false;
        out = {layout, header, d, 0, 0, w, h};
        return true;
    }

    // Walk the visible connection graph (N/S/W/E only) for scenery beyond
    // the seven-metatile strip the guest copies into its padded grid.
    void extend_connections(int min_x, int min_y, int max_x, int max_y) {
        Connected root;
        if (!layout_at(kMapHeader, root)) return;
        root.x = root.y = kMapPadding;
        if (const auto* loc = m.bytes(m.u32(kSaveBlock1Ptr) + 4, 2)) {
            root.group = loc[0];
            root.number = loc[1];
        }
        connected[connected_count++] = root;
        for (int next = 0; next < connected_count; ++next) {
            const auto cur = connected[next];
            const auto list = m.u32(cur.header + 12);
            if (!list || !m.bytes(list, 8)) continue;
            const auto count = m.u32(list), entries = m.u32(list + 4);
            if (count > 64 || !m.bytes(entries, count * 12)) continue;
            for (unsigned i = 0; i < count && connected_count < int(connected.size()); ++i) {
                const auto* e = m.bytes(entries + i * 12, 12);
                const unsigned dir = e[0];
                if (dir < 1 || dir > 4) continue;
                const int offset = static_cast<std::int32_t>(m.u32(entries + i * 12 + 4));
                if (offset < -10240 || offset > 10240) continue;
                const auto group = m.u32(kMapGroups + e[8] * 4);
                Connected nb;
                if (!layout_at(m.u32(group + e[9] * 4), nb)) continue;
                nb.group = e[8];
                nb.number = e[9];
                // Only tiles already resident in VRAM can be shown.
                if (m.u32(nb.layout + 16) != m.u32(root.layout + 16) ||
                    m.u32(nb.layout + 20) != m.u32(root.layout + 20)) continue;
                nb.x = cur.x;
                nb.y = cur.y;
                switch (dir) {
                    case 1: nb.x += offset; nb.y += cur.height; break;      // south
                    case 2: nb.x += offset; nb.y -= nb.height; break;       // north
                    case 3: nb.x -= nb.width; nb.y += offset; break;        // west
                    case 4: nb.x += cur.width; nb.y += offset; break;       // east
                }
                if (nb.x > max_x || nb.y > max_y || nb.x + nb.width <= min_x ||
                    nb.y + nb.height <= min_y) continue;
                const auto dup = std::find_if(connected.begin(), connected.begin() + connected_count,
                    [&](const Connected& s) { return s.layout == nb.layout && s.x == nb.x && s.y == nb.y; });
                if (dup == connected.begin() + connected_count) connected[connected_count++] = nb;
            }
        }
    }

    bool load() {
        const auto layout = m.u32(kMapHeader);
        if (!m.bytes(layout, 26)) return false;
        width = static_cast<int>(m.u32(kBackupMap));
        height = static_cast<int>(m.u32(kBackupMap + 4));
        data = m.u32(kBackupMap + 8);
        border = m.u32(layout + 8);
        const auto* bdim = m.bytes(layout + 24, 2);
        border_w = bdim ? bdim[0] : 0;
        border_h = bdim ? bdim[1] : 0;
        if (width < 1 || height < 1 || width > 10240 || height > 10240 ||
            width * height > 10240 || !m.bytes(data, width * height * 2) ||
            border_w < 1 || border_h < 1 || border_w > 32 || border_h > 32 ||
            !m.bytes(border, border_w * border_h * 2)) return false;
        for (int i = 0; i < 2; ++i) {
            const auto ts = m.u32(layout + 16 + i * 4);
            if (!m.bytes(ts, 24)) return false;
            // FireRed's Tileset: metatiles at +0x0C, callback at +0x10 and
            // metatileAttributes at +0x14 (Emerald swaps the last two).
            metatiles[i] = m.u32(ts + 12);
            attributes[i] = m.u32(ts + 20);
            if (!m.bytes(metatiles[i], 16) || !m.bytes(attributes[i], 4)) return false;
        }
        return true;
    }

    unsigned metatile(int x, int y, bool extended = false) const {
        unsigned block = 0x3ff;
        if (x >= 0 && y >= 0 && x < width && y < height)
            block = m.u16(data + 2 * (x + y * width));
        if (block == 0x3ff && extended && connected_count) {
            const auto& root = connected[0];
            if (x < root.x || y < root.y || x >= root.x + root.width || y >= root.y + root.height) {
                for (int i = 1; i < connected_count; ++i) {
                    const auto& c = connected[i];
                    const int mx = x - c.x, my = y - c.y;
                    if (mx >= 0 && my >= 0 && mx < c.width && my < c.height) {
                        block = m.u16(c.data + 2 * (mx + my * c.width));
                        break;
                    }
                }
            }
        }
        if (block == 0x3ff) {
            // GetBorderBlockAt: borderWidth x borderHeight tiling from MAP_OFFSET.
            const int bx = wrap(x - kMapPadding, border_w);
            const int by = wrap(y - kMapPadding, border_h);
            block = m.u16(border + 2 * (bx + by * border_w));
        }
        return block & 0x3ff;
    }

    // Full grid value (metatile | collision << 10 | elevation << 12); the
    // border block reports collision like GetBorderBlockAt does.
    unsigned raw(int x, int y, bool* is_border) const {
        unsigned block = 0x3ff;
        if (x >= 0 && y >= 0 && x < width && y < height)
            block = m.u16(data + 2 * (x + y * width));
        if ((block & 0x3ff) == 0x3ff && connected_count) {
            const auto& root = connected[0];
            if (x < root.x || y < root.y || x >= root.x + root.width || y >= root.y + root.height) {
                for (int i = 1; i < connected_count; ++i) {
                    const auto& c = connected[i];
                    const int mx = x - c.x, my = y - c.y;
                    if (mx >= 0 && my >= 0 && mx < c.width && my < c.height) {
                        block = m.u16(c.data + 2 * (mx + my * c.width));
                        break;
                    }
                }
            }
        }
        if (is_border) *is_border = (block & 0x3ff) == 0x3ff;
        if ((block & 0x3ff) == 0x3ff) {
            const int bx = wrap(x - kMapPadding, border_w);
            const int by = wrap(y - kMapPadding, border_h);
            block = (m.u16(border + 2 * (bx + by * border_w)) & 0x3ff) | 0x0C00;
        }
        return block;
    }

    // DrawMetatile: BG1 = top layer, BG2 = middle, BG3 = bottom.
    bool entries(int tile_x, int tile_y, std::uint16_t out[3], bool extended = false) const {
        const unsigned id = metatile(floor2(tile_x), floor2(tile_y), extended);
        const unsigned set = id < kPrimaryMetatiles ? 0 : 1;
        const unsigned index = set ? id - kPrimaryMetatiles : id;
        const auto tiles = metatiles[set] + index * 16;
        const auto attr = attributes[set] + index * 4;
        if (!m.bytes(tiles, 16) || !m.bytes(attr, 4)) return false;
        const unsigned layer = (m.u32(attr) >> 29) & 3;
        const int q = wrap(tile_x, 2) + 2 * wrap(tile_y, 2);
        const auto bottom = m.u16(tiles + q * 2);
        const auto top = m.u16(tiles + 8 + q * 2);
        switch (layer) {
            case 0: out[0] = top; out[1] = bottom; out[2] = 0x3014; break;  // NORMAL
            case 1: out[0] = 0;   out[1] = top;    out[2] = bottom; break;  // COVERED
            case 2: out[0] = top; out[1] = 0;      out[2] = bottom; break;  // SPLIT
            default: return false;
        }
        return true;
    }

    // Door animation frames replace the published ring without touching the
    // map grid (field_door.c): COVERED layer, BG3 = door tiles 1016.. (1020..
    // for the lower half of a 1x2 door) with per-quadrant palettes, BG2 = tile
    // 0 with the upper palette set, BG1 = 0. Accept only that exact pattern.
    bool door_entries(int tile_x, int tile_y, const std::uint16_t actual[3]) const {
        if (actual[0] != 0 || (actual[1] & 0x0FFF) != 0 || (actual[2] & 1023) < kDoorTileStart)
            return false;
        const int mx = floor2(tile_x), my = floor2(tile_y);
        const int q = wrap(tile_x, 2) + wrap(tile_y, 2) * 2;
        for (int down = 0; down <= 1; ++down) {
            const unsigned id = metatile(mx, my + down);
            for (int i = 0; i < kDoorGraphicsMax; ++i) {
                const auto* g = m.bytes(kDoorGraphics + i * 12, 12);
                if (!g || !m.u32(kDoorGraphics + i * 12 + 4)) break;  // tiles == NULL ends
                if (read16(g) != id) continue;
                const bool tall = g[3] == 1;  // DOOR_SIZE_1x2
                if (down && !tall) continue;
                const bool lower_half = tall && !down;
                const int pal_off = lower_half ? 4 : 0;
                const unsigned base = kDoorTileStart + (lower_half ? 4 : 0);
                const auto* pals = m.bytes(m.u32(kDoorGraphics + i * 12 + 8), 12);
                if (!pals) continue;
                const std::uint16_t expected[3] = {
                    0,
                    static_cast<std::uint16_t>(pals[pal_off + 4 + q] << 12),
                    static_cast<std::uint16_t>((pals[pal_off + q] << 12) | (base + q))};
                if (std::equal(expected, expected + 3, actual)) return true;
            }
        }
        return false;
    }
};

// ── expanded field tiles ─────────────────────────────────────────────────
class FieldView {
public:
    ViewStatus prepare(const ViewMemory& m, int width, int height);
    bool tile(int bg, int hx, int sy, std::uint16_t* entry) const {
        if (status_ != ViewStatus::Ready || bg < 1 || bg > 3 || !entry ||
            hx < -left_ || hx >= width_ - left_ || sy < -top_ || sy >= height_ - top_)
            return false;
        const int x = floor8(hx + phase_x_) - floor8(-left_ + phase_x_);
        const int y = floor8(sy + phase_y_) - floor8(-top_ + phase_y_);
        *entry = tiles_[bg - 1][y * kColumns + x];
        return true;
    }
    ViewStatus status() const { return status_; }
    int origin_x() const { return origin_x_; }   // world px of native screen (0, 0)
    int origin_y() const { return origin_y_; }
    int reason() const { return reason_; }
    int compared() const { return compared_; }
    int matched() const { return matched_; }
private:
    static constexpr int kColumns = 40;  // 284px plus a partial tile either side
    static constexpr int kRows = 22;
    std::array<std::array<std::uint16_t, kColumns * kRows>, 3> tiles_{};
    ViewStatus status_ = ViewStatus::Native;
    int left_ = 0, top_ = 0, width_ = 240, height_ = 160, phase_x_ = 0, phase_y_ = 0;
    int compared_ = 0, matched_ = 0;
    int origin_x_ = 0, origin_y_ = 0;
    int reason_ = 0;
};

ViewStatus FieldView::prepare(const ViewMemory& m, int width, int height) {
    status_ = ViewStatus::Native;
    compared_ = matched_ = 0;
    width_ = std::clamp(width, 240, kMaxView);
    height_ = 160;
    (void)height;
    left_ = (width_ - 240) / 2;
    top_ = 0;
    if (width_ == 240) return status_;
    status_ = ViewStatus::NonField;
    if ((m.u32(kMain + 4) & ~1u) != kOverworld) return status_;
    status_ = ViewStatus::Unsupported;
    reason_ = 1;
    if (!m.io || !m.vram || !m.iwram) return status_;
    const unsigned dispcnt = read16(m.io);
    // Mode 0, BG1-3 on, no forced blank. BG0 (windows/text) stays native.
    reason_ = 0x1000 | dispcnt;
    if ((dispcnt & 0x87) || (dispcnt & 0xE00) != 0xE00) return status_;
    const int hofs = read16(m.io + 0x14) & 255;
    const int vofs = read16(m.io + 0x16) & 255;
    unsigned screen_base[3]{};
    for (int bg = 1; bg <= 3; ++bg) {
        const auto cnt = read16(m.io + 8 + bg * 2);
        reason_ = 0x20000 | (bg << 16) | cnt;
        if ((cnt & 0xC080) || ((cnt & 0x40) && (m.io[0x4C] != 0)) ||
            int(read16(m.io + 0x10 + bg * 4) & 255) != hofs ||
            int(read16(m.io + 0x12 + bg * 4) & 255) != vofs) return status_;
        screen_base[bg - 1] = ((cnt >> 8) & 31) * 0x800;
    }
    Map map{m};
    const auto save = m.u32(kSaveBlock1Ptr);
    reason_ = 3;
    if (!map.load() || !m.bytes(save, 4)) return status_;
    const auto* camera = m.bytes(kCameraOffset, 4);
    reason_ = 4;
    if (!camera || camera[2] >= 32 || camera[3] >= 32) return status_;
    reason_ = 0;
    const int nominal_x = static_cast<std::int16_t>(m.u16(save)) * 16 +
        signed_ring_delta(hofs - camera[2] * 8);
    const int nominal_y = static_cast<std::int16_t>(m.u16(save + 2)) * 16 +
        signed_ring_delta(vofs - camera[3] * 8);
    const int otx = floor8(nominal_x), oty = floor8(nominal_y);
    phase_x_ = hofs & 7;
    phase_y_ = vofs & 7;

    // Every sampled native tile/layer must match the published ring.
    const auto score = [&](int dx, int dy) {
        int matches = 0, total = 0;
        for (int y = 0; y < 20; y += 3) for (int x = 0; x < 30; x += 3) {
            std::uint16_t expected[3];
            if (!map.entries(otx + x + dx, oty + y + dy, expected)) return -1;
            const int ring = wrap((hofs >> 3) + x, 32) + 32 * wrap((vofs >> 3) + y, 32);
            std::uint16_t actual[3];
            for (int bg = 0; bg < 3; ++bg) actual[bg] = read16(m.vram + screen_base[bg] + ring * 2);
            const bool door = !std::equal(expected, expected + 3, actual) &&
                map.door_entries(otx + x + dx, oty + y + dy, actual);
            for (int bg = 0; bg < 3; ++bg) {
                ++total;
                matches += door || expected[bg] == actual[bg];
            }
        }
        compared_ = total;
        return matches;
    };
    status_ = ViewStatus::Unverified;
    int best_x = 0, best_y = 0;
    matched_ = score(0, 0);
    for (int dy : {0, -2, 2}) for (int dx : {0, -2, 2}) {
        if (matched_ == compared_ && compared_ > 0) break;
        const int s = score(dx, dy);
        if (s > matched_) { matched_ = s; best_x = dx; best_y = dy; }
    }
    if (compared_ == 0 || matched_ != compared_) return status_;
    origin_x_ = nominal_x + best_x * 8;
    origin_y_ = nominal_y + best_y * 8;
    const int first_x = floor8(-left_ + phase_x_);
    const int last_x = floor8(width_ - left_ - 1 + phase_x_);
    const int first_y = floor8(-top_ + phase_y_);
    const int last_y = floor8(height_ - top_ - 1 + phase_y_);
    if (last_x - first_x >= kColumns || last_y - first_y >= kRows) return status_;
    map.extend_connections(floor2(otx + best_x + first_x), floor2(oty + best_y + first_y),
                           floor2(otx + best_x + last_x), floor2(oty + best_y + last_y));
    for (int y = first_y; y <= last_y; ++y) for (int x = first_x; x <= last_x; ++x) {
        std::uint16_t e[3];
        if (!map.entries(otx + best_x + x, oty + best_y + y, e, true)) return status_;
        if (x >= 0 && x < 30 && y >= 0 && y < 20) {
            const int ring = wrap((hofs >> 3) + x, 32) + 32 * wrap((vofs >> 3) + y, 32);
            std::uint16_t actual[3];
            for (int bg = 0; bg < 3; ++bg) actual[bg] = read16(m.vram + screen_base[bg] + ring * 2);
            if (map.door_entries(otx + best_x + x, oty + best_y + y, actual))
                std::copy_n(actual, 3, e);
        }
        for (int bg = 0; bg < 3; ++bg) tiles_[bg][(y - first_y) * kColumns + x - first_x] = e[bg];
    }
    status_ = ViewStatus::Ready;
    return status_;
}

// ── BG0 windows anchored to the view edges (Start menu → right edge) ─────
class UiView {
public:
    void prepare(const ViewMemory& m, const FieldView& field, int width) {
        count_ = 0;
        spans_count_ = 0;
        active_ = field.status() == ViewStatus::Ready && m.io && m.vram;
        if (!active_) return;
        const unsigned cnt = read16(m.io + 8);
        if (cnt & 0xC080) { active_ = false; return; }
        const int left = (width - 240) / 2, right = width - 240 - left;
        auto sscroll = [](unsigned n) { return int((n + 256) & 511) - 256; };
        const int scroll_x = sscroll(read16(m.io + 0x10));
        const int scroll_y = sscroll(read16(m.io + 0x12));
        const unsigned screen = ((cnt >> 8) & 31) * 0x800;
        for (int i = 0; i < 32; ++i) {
            const auto* w = m.bytes(kWindows + i * 12, 12);
            if (!w || w[0] != 0 || !w[3] || !w[4] || w[1] + w[3] > 30 ||
                w[2] + w[4] > 20 || !m.u32(kWindows + i * 12 + 8)) continue;
            const auto base = read16(w + 6);
            bool visible = false;
            for (int ty = 0; ty < w[4] && !visible; ++ty) for (int tx = 0; tx < w[3]; ++tx) {
                const auto entry = read16(m.vram + screen + ((w[2] + ty) * 32 + w[1] + tx) * 2);
                if ((entry & 1023) == base + ty * w[3] + tx && (entry >> 12) == w[5]) {
                    visible = true;
                    break;
                }
            }
            if (!visible) continue;
            const bool at_left = w[1] <= 2, at_right = w[1] + w[3] >= 28;
            // Edge-to-edge bars (quest-log "Previously on your quest…",
            // full-width banners) have no frame of their own: continue them
            // across both margins instead of cutting them at the 240px edge.
            if (at_left && at_right && w[1] == 0 && w[3] >= 30 && spans_count_ < 8) {
                spans_[spans_count_++] = {(int(w[2]) * 8) - scroll_y,
                                          (int(w[2]) + w[4]) * 8 - scroll_y};
                continue;
            }
            const int dx = at_left == at_right ? 0 : at_right ? right : -left;
            if (!dx) continue;
            int frame_left = 1;
            if (w[1] >= 2) {
                const auto edge = read16(m.vram + screen + (w[2] * 32 + w[1] - 2) * 2);
                bool repeated = (edge & 1023) != 0;
                for (int ty = 1; ty < w[4] && repeated; ++ty)
                    repeated = read16(m.vram + screen + ((w[2] + ty) * 32 + w[1] - 2) * 2) == edge;
                if (repeated) frame_left = 2;
            }
            const int x = std::max(0, (int(w[1]) - frame_left) * 8);
            const int y = std::max(0, (int(w[2]) - 1) * 8);
            windows_[count_++] = {x - scroll_x, y - scroll_y,
                std::min(240, (w[1] + w[3] + 1) * 8) - x,
                std::min(160, (w[2] + w[4] + 1) * 8) - y, dx, 0};
        }
    }
    int sample(int bg, int x, int y, int* sx, int* sy) const {
        if (!active_ || bg != 0) return 0;
        const auto inside = [](const Rect& r, int px, int py) {
            return px >= r.x && px < r.x + r.w && py >= r.y && py < r.y + r.h;
        };
        for (int i = count_ - 1; i >= 0; --i) {
            const auto& r = windows_[i];
            if (inside(r, x - r.dx, y - r.dy)) { *sx = x - r.dx; *sy = y - r.dy; return 1; }
        }
        for (int i = 0; i < count_; ++i) if (inside(windows_[i], x, y)) return -1;
        if ((x < 0 || x >= 240) && y >= 0 && y < 160) {
            for (int i = 0; i < spans_count_; ++i) {
                if (y >= spans_[i].y0 && y < spans_[i].y1) {
                    *sx = x < 0 ? 0 : 239;
                    *sy = y;
                    return 1;
                }
            }
        }
        return x < 0 || x >= 240 || y < 0 || y >= 160 ? -1 : 0;
    }
private:
    struct Rect { int x, y, w, h, dx, dy; };
    struct Span { int y0, y1; };
    std::array<Rect, 32> windows_{};
    std::array<Span, 8> spans_{};
    int spans_count_ = 0;
    int count_ = 0;
    bool active_ = false;
};

// ── live overworld NPCs in the margins, bound to published OAM ───────────
class ObjectView {
public:
    bool prepare(const ViewMemory& m, const FieldView& field, int width);
    const gba::WsMarginObjPixel* row(int y, int* left, int* width) const {
        if (!ready_ || y < 0 || y >= 160 || !left || !width) return nullptr;
        *left = left_;
        *width = width_;
        return pixels_.data() + y * width_;
    }
private:
    std::vector<gba::WsMarginObjPixel> pixels_;
    int left_ = 0, width_ = 0;
    bool ready_ = false;
};

constexpr int kObjW[3][4] = {{8, 16, 32, 64}, {16, 32, 32, 64}, {8, 8, 16, 32}};
constexpr int kObjH[3][4] = {{8, 16, 32, 64}, {8, 8, 16, 32}, {16, 32, 32, 64}};

bool ObjectView::prepare(const ViewMemory& m, const FieldView& field, int width) {
    ready_ = false;
    width_ = std::clamp(width, 240, kMaxView);
    left_ = -(width_ - 240) / 2;
    const int top = 0, height = 160;
    if (field.status() != ViewStatus::Ready || !m.oam || !m.pal || !m.vram || !m.io) return false;
    pixels_.assign(width_ * height, {});
    const int offset_x = static_cast<std::int16_t>(m.u16(kOffsetX));
    const int offset_y = static_cast<std::int16_t>(m.u16(kOffsetY));
    const bool mapping_1d = (read16(m.io) & 0x40) != 0;
    struct Part { int x, y; unsigned a0, a1, a2; };
    for (int id = 0; id < 16; ++id) {
        const auto* o = m.bytes(kObjects + id * 36, 36);
        if (!o || !(o[0] & 1) || (o[1] & 0x20)) continue;  // inactive / invisible
        if (o[4] >= 64) return false;
        const auto address = kSprites + o[4] * 68;
        const auto* s = m.bytes(address, 68);
        if (!s || !(s[62] & 1)) return false;
        if ((s[62] & 4) && !(o[1] & 0x40)) continue;
        const unsigned a0 = read16(s), a1 = read16(s + 2), a2 = read16(s + 4);
        if ((a0 & 0x3F00) || (a0 >> 14) >= 3) continue;  // affine/blend/window effects
        int x = s16(s + 32) + s16(s + 36) + s8(s[40]);
        int y = s16(s + 34) + s16(s + 38) + s8(s[41]);
        if (s[62] & 2) { x += offset_x; y += offset_y; }
        if (!(o[1] & 0x40)) {
            x = unwrap(a1 & 511, x, 512);
            y = unwrap(a0 & 255, y, 256);
        }
        const bool hflip = (a1 & 0x1000) != 0, vflip = (a1 & 0x2000) != 0;
        std::array<Part, 16> parts{};
        int count = 1;
        parts[0] = {x, y, a0, a1, a2};
        const auto tables = m.u32(address + 24);
        const int mode = s[66] >> 6;
        if (tables && mode) {
            const auto* table = m.bytes(tables + (s[66] & 63) * 8, 8);
            if (!table || table[0] > parts.size()) return false;
            count = table[0];
            const auto* d = m.bytes(m.u32(tables + (s[66] & 63) * 8 + 4), count * 4);
            if (!d && count) return false;
            for (int n = 0; n < count; ++n) {
                const auto* p = d + n * 4;
                const unsigned bits = read16(p + 2), shape = bits & 3, size = (bits >> 2) & 3;
                if (shape >= 3) return false;
                const int dx = hflip ? -s8(p[0]) - kObjW[shape][size] : s8(p[0]);
                const int dy = vflip ? -s8(p[1]) - kObjH[shape][size] : s8(p[1]);
                parts[n] = {x - s8(s[40]) + dx, y - s8(s[41]) + dy,
                    (a0 & 0x3FFF) | (shape << 14), (a1 & 0x3FFF) | (size << 14),
                    (a2 & 0xFC00) | (((a2 & 1023) + ((bits >> 4) & 1023)) & 1023)};
                if (mode != 2) parts[n].a2 = (parts[n].a2 & ~0xC00u) | ((bits >> 14) << 10);
            }
        }
        for (int n = 0; n < count; ++n) {
            auto& p = parts[n];
            p.a0 = (p.a0 & ~255u) | (p.y & 255);
            p.a1 = (p.a1 & ~511u) | (p.x & 511);
            int order = 127;
            bool found = false;
            for (int idx = 0; idx < 128; ++idx) {
                const auto* hw = m.oam + idx * 8;
                const unsigned hw0 = read16(hw), hw1 = read16(hw + 2), hw2 = read16(hw + 4);
                const int hx = unwrap(hw1 & 511, p.x, 512), hy = unwrap(hw0 & 255, p.y, 256);
                if ((hw0 & 0xFF00) == (p.a0 & 0xFF00) && (hw1 & 0xC000) == (p.a1 & 0xC000) &&
                    hw2 == p.a2 && std::abs(hx - p.x) <= 8 && std::abs(hy - p.y) <= 8) {
                    if (found) return false;  // ambiguous identity: draw nothing
                    order = idx;
                    found = true;
                }
            }
            const int pw = kObjW[p.a0 >> 14][p.a1 >> 14], ph = kObjH[p.a0 >> 14][p.a1 >> 14];
            const bool touches_native = p.x < 240 && p.x + pw > 0 && p.y < 160 && p.y + ph > 0;
            if (!(o[1] & 0x40) && touches_native && !found) return false;
            if (found) {
                const auto* hw = m.oam + order * 8;
                p.a0 = read16(hw); p.a1 = read16(hw + 2); p.a2 = read16(hw + 4);
                p.x = unwrap(p.a1 & 511, p.x, 512);
                p.y = unwrap(p.a0 & 255, p.y, 256);
            }
            const int shape = p.a0 >> 14, size = p.a1 >> 14;
            const int w = kObjW[shape][size], h = kObjH[shape][size];
            const bool phf = (p.a1 & 0x1000) != 0, pvf = (p.a1 & 0x2000) != 0;
            const int priority = (p.a2 >> 10) & 3, palette = p.a2 >> 12;
            for (int py = std::max(0, top - p.y); py < h && p.y + py < top + height; ++py) {
                const int ty = pvf ? h - 1 - py : py;
                for (int px = 0; px < w; ++px) {
                    const int hx = p.x + px, out_x = hx - left_;
                    if (out_x < 0 || out_x >= width_ ||
                        (hx >= 0 && hx < 240 && p.y + py >= 0 && p.y + py < 160)) continue;
                    const int tx = phf ? w - 1 - px : px;
                    const unsigned tile = (p.a2 & 1023) + (ty / 8) * (mapping_1d ? w / 8 : 32) + tx / 8;
                    const unsigned off = 0x10000 + tile * 32 + (ty & 7) * 4 + (tx & 7) / 2;
                    if (off >= 0x18000) return false;
                    const unsigned index = (m.vram[off] >> ((tx & 1) * 4)) & 15;
                    if (!index) continue;
                    auto& dest = pixels_[(p.y + py - top) * width_ + out_x];
                    if (!(dest.color & 0x8000) &&
                        dest.priority * 256 + dest.order <= priority * 256 + order) continue;
                    dest = {static_cast<std::uint16_t>(read16(m.pal + 0x200 + palette * 32 + index * 2) & 0x7FFF),
                            static_cast<std::uint8_t>(priority), static_cast<std::uint8_t>(order)};
                }
            }
        }
    }
    ready_ = true;
    return true;
}

// ── GBA LZ77 (BIOS LZ77UnComp) for reading compressed ROM tilemaps ───────
bool lz77(const std::uint8_t* src, std::size_t avail, std::vector<std::uint8_t>& out) {
    if (!src || avail < 4 || src[0] != 0x10) return false;
    const std::size_t size = src[1] | (src[2] << 8) | (src[3] << 16);
    if (size == 0 || size > 0x10000) return false;
    out.clear();
    out.reserve(size);
    std::size_t p = 4;
    while (out.size() < size) {
        if (p >= avail) return false;
        const std::uint8_t flags = src[p++];
        for (int bit = 7; bit >= 0 && out.size() < size; --bit) {
            if (!(flags & (1 << bit))) {
                if (p >= avail) return false;
                out.push_back(src[p++]);
                continue;
            }
            if (p + 1 >= avail) return false;
            const int len = (src[p] >> 4) + 3;
            const std::size_t disp = (((src[p] & 15) << 8) | src[p + 1]) + 1;
            p += 2;
            if (disp > out.size()) return false;
            for (int i = 0; i < len && out.size() < size; ++i)
                out.push_back(out[out.size() - disp]);
        }
    }
    return true;
}

// ── battles: terrain/animation layers continue, the textbox stretches ─────
// The battle terrain (BG3) is a 512px-wide map whose off-screen columns are
// real sky/grass filler, and animation layers (BG1/BG2) are authored to wrap
// as they scroll, so the hardware continuation is the right content there.
// The textbox (BG0, gBattleInterface_Textbox_Tilemap) is re-laid out so its
// left/right frames sit at the edges of the wide view and the interior fill
// (taken from the ROM tilemap, never from text windows) bridges the gap.
constexpr std::uint32_t kBattleMainCB2         = 0x08011100;
constexpr std::uint32_t kCB2_HandleStartBattle = 0x08010508;
constexpr std::uint32_t kBattleTextboxTilemap  = 0x08D0051C;
constexpr int kVirtualBase = -8192;  // sample x below this = synthesized BG0 cell

bool battle_callback(std::uint32_t cb2) {
    return cb2 == kBattleMainCB2 || cb2 == kCB2_HandleStartBattle;
}

class BattleView {
public:
    bool prepare(const ViewMemory& m, const std::uint8_t* io, int width) {
        active_ = false;
        if (!io || !m.vram || width <= 240) return false;
        if (!battle_callback(m.u32(kMain + 4) & ~1u)) return false;
        io_ = io;
        vram_ = m.vram;
        left_ = (width - 240) / 2;
        right_ = width - 240 - left_;
        if (!orig_ok_) {
            std::vector<std::uint8_t> raw;
            const auto* src = m.bytes(kBattleTextboxTilemap, 4);
            const std::size_t avail = src ? m.rom_size - (kBattleTextboxTilemap - 0x08000000) : 0;
            if (!lz77(src, avail, raw) || raw.size() < 32 * 64 * 2) return false;
            for (int i = 0; i < 32 * 64; ++i) orig_[i] = raw[i * 2] | (raw[i * 2 + 1] << 8);
            orig_ok_ = true;
        }
        const unsigned cnt = read16(io + 8);
        box_.fill(false);
        // BG0 must still be the battle textbox layer: 32x64 map at block 24.
        if (((cnt >> 8) & 31) == 24 && (cnt >> 14) == 2 && !(cnt & 0x80)) {
            const auto* map = m.vram + 24 * 0x800;
            for (int r = 0; r < 64; ++r) {
                const unsigned o0 = orig_[r * 32], o29 = orig_[r * 32 + 29];
                if (!(o0 & 1023) || !(o29 & 1023)) continue;
                const unsigned l = read16(map + (r * 32) * 2), rr = read16(map + (r * 32 + 29) * 2);
                if ((l & 0x0FFF) != (o0 & 0x0FFF) || (rr & 0x0FFF) != (o29 & 0x0FFF)) continue;
                box_[r] = true;
                frame_l_[r] = l;
                frame_r_[r] = rr;
                fill_l_[r] = (orig_[r * 32 + 2] & 0x0FFF) | (l & 0xF000);
                fill_r_[r] = (orig_[r * 32 + 27] & 0x0FFF) | (rr & 0xF000);
            }
        }
        active_ = true;
        return true;
    }
    bool active() const { return active_; }

    int sample(int bg, int x, int y, int* sx, int* sy) const {
        if (!active_) return 0;
        if (bg != 0) return 0;  // natural continuation (see tile())
        if (x >= 8 && x < 232) return 0;
        if (y < 0 || y >= 160) return -1;
        const int hofs = read16(io_ + 0x10) & 511, vofs = read16(io_ + 0x12) & 511;
        const int row = ((y + vofs) & 511) >> 3;
        if (!box_[row]) return (x < 0 || x >= 240) ? -1 : 0;
        int region, px;
        if (x < -left_ + 8)               { region = 0; px = x + left_; }
        else if (x < 8)                   { region = 1; px = x & 7; }
        else if (x >= 240 + right_ - 8)   { region = 3; px = x - (240 + right_ - 8); }
        else                              { region = 2; px = x & 7; }
        *sx = kVirtualBase + region * 64 + px - (hofs & 7);
        *sy = y;
        return 1;
    }
    int tile(int bg, int x, int y, std::uint16_t* entry) const {
        if (!active_) return gba::kWsTilemapUnavailable;
        if (bg != 0) return gba::kWsTilemapKeepWrapped;
        if (x >= kVirtualBase / 2 || y < 0 || y >= 160) return gba::kWsTilemapUnavailable;
        const int region = (x - kVirtualBase + 32) / 64;
        const int vofs = read16(io_ + 0x12) & 511;
        const int row = ((y + vofs) & 511) >> 3;
        if (!box_[row] || region < 0 || region > 3) return gba::kWsTilemapUnavailable;
        const std::uint16_t e[4] = {frame_l_[row], fill_l_[row], fill_r_[row], frame_r_[row]};
        *entry = e[region];
        return gba::kWsTilemapReplace;
    }
private:
    std::array<std::uint16_t, 32 * 64> orig_{};
    bool orig_ok_ = false;
    std::array<bool, 64> box_{};
    std::array<std::uint16_t, 64> frame_l_{}, frame_r_{}, fill_l_{}, fill_r_{};
    const std::uint8_t* io_ = nullptr;
    const std::uint8_t* vram_ = nullptr;
    int left_ = 22, right_ = 22;
    bool active_ = false;
};

// ── every other full-screen scene (title, menus, bag, party, …) ──────────
// Each text BG continues into the margins scanline by scanline: content whose
// edge pixels repeat with a short period (solid fills, stripes, patterns,
// window interiors) keeps repeating; a sparse layer (objects) ends at the
// edge; a scrolling or 512px-wide map continues with its own off-screen
// columns; anything else extends its outermost pixel column (UI panels).
// Sprites stay inside the native 240 columns, where the game placed them.
class GenericView {
public:
    void prepare(const std::uint8_t* io, const std::uint8_t* vram, int width) {
        io_ = io;
        vram_ = vram;
        active_ = io && vram && width > 240;
        cache_y_.fill(-1000);
        if (!io) return;
        // A layer that scrolls horizontally is a wrapping texture (intro
        // forest, credits skies): its own hardware wrap is the continuation.
        for (int bg = 0; bg < 4; ++bg) {
            const int hofs = read16(io + 0x10 + bg * 4) & 511;
            if (hofs != last_hofs_[bg]) scrolling_[bg] = 90;
            else if (scrolling_[bg] > 0) --scrolling_[bg];
            last_hofs_[bg] = hofs;
        }
    }
    int sample(int bg, int x, int y, int* sx, int* sy) {
        if (!active_ || (x >= 0 && x < 240)) return 0;
        if (y < 0 || y >= 160) return -1;
        const unsigned mode = read16(io_) & 7;
        if (mode > 1 || (mode == 1 && bg >= 2)) return 0;
        if (cache_y_[bg] != y) { cache_[bg] = describe(bg, y); cache_y_[bg] = y; }
        const Desc& d = cache_[bg];
        const int p = x < 0 ? d.left : d.right;
        if (p == kNatural) return 0;  // hardware continuation of a wide map
        if (p == kTransparent) return -1;
        if (p == kConnected) {
            // Objects crossing the edge continue only as far as their own
            // pixels reach; wrapped-around content never appears detached.
            const int dist = x < 0 ? -x : x - 239;
            return dist <= (x < 0 ? d.left_run : d.right_run) ? 0 : -1;
        }
        int s = x;
        if (p > 0) {
            if (x < 0) { while (s < 0) s += p; } else { while (s >= 240) s -= p; }
        } else {
            s = x < 0 ? 0 : 239;  // extend the outermost pixel column
        }
        *sx = s;
        *sy = y;
        return 1;
    }
private:
    static constexpr int kNatural = -2, kTransparent = -3, kClamp = -1, kConnected = -4;
    struct Desc { int left = 0, right = 0, left_run = 0, right_run = 0; };
    // Palette-qualified pixel (0 = transparent) of this BG at native x.
    unsigned pixel(unsigned cnt, int hofs, int texy, int wt, int x) const {
        const unsigned screen = ((cnt >> 8) & 31) * 0x800;
        const unsigned chars = ((cnt >> 2) & 3) * 0x4000;
        const int texx = (x + hofs) & (wt * 8 - 1);
        const int col = texx >> 3, ty = texy >> 3;
        const unsigned block = col / 32 + (ty / 32) * (wt / 32);
        const unsigned off = screen + block * 0x800 + ((ty & 31) * 32 + (col & 31)) * 2;
        if (off + 1 >= 0x10000) return 0;
        const unsigned e = read16(vram_ + off);
        int px = texx & 7, py = texy & 7;
        if (e & 0x400) px = 7 - px;
        if (e & 0x800) py = 7 - py;
        if (cnt & 0x80) {
            const unsigned a = chars + (e & 1023) * 64 + py * 8 + px;
            return a < 0x10000 ? vram_[a] : 0;
        }
        const unsigned a = chars + (e & 1023) * 32 + py * 4 + px / 2;
        if (a >= 0x10000) return 0;
        const unsigned idx = (vram_[a] >> ((px & 1) * 4)) & 15;
        return idx ? idx | ((e >> 12) << 4) : 0;
    }
    Desc describe(int bg, int y) const {
        Desc d;
        const unsigned cnt = read16(io_ + 8 + bg * 2);
        const int hofs = read16(io_ + 0x10 + bg * 4) & 511;
        const int vofs = read16(io_ + 0x12 + bg * 4) & 511;
        const int size = cnt >> 14;
        const int wt = (size & 1) ? 64 : 32, ht = (size & 2) ? 64 : 32;
        const int texy = (y + vofs) & (ht * 8 - 1);
        unsigned a[64], b[64];
        for (int i = 0; i < 64; ++i) {
            a[i] = pixel(cnt, hofs, texy, wt, i);
            b[i] = pixel(cnt, hofs, texy, wt, 239 - i);
        }
        auto period = [](const unsigned* v) {
            for (int p = 1; p <= 32; ++p) {
                bool ok = true;
                for (int i = 0; i < 32 && ok; ++i) ok = v[i] == v[i + p];
                if (ok) return p;
            }
            return 0;
        };
        d.left = period(a);
        d.right = period(b);
        // Non-repeating edges: a dense layer is scenery (continue it); a
        // sparse one holds objects that simply end at the screen edge.
        auto fallback = [&](const unsigned* v) {
            int opaque = 0;
            for (int i = 0; i < 64; ++i) opaque += v[i] != 0;
            if (opaque < 48) return v[0] ? kConnected : kTransparent;
            return (wt == 64 || scrolling_[bg] > 0) ? kNatural : kClamp;
        };
        if (!d.left) d.left = fallback(a);
        if (!d.right) d.right = fallback(b);
        // Length of the opaque run continuing past each edge (hardware map).
        auto run = [&](int from, int step) {
            int n = 0;
            for (int x = from; n < 32 && pixel(cnt, hofs, texy, wt, x); x += step) ++n;
            return n;
        };
        if (d.left == kConnected) d.left_run = run(-1, -1);
        if (d.right == kConnected) d.right_run = run(240, 1);
        return d;
    }
    const std::uint8_t* io_ = nullptr;
    const std::uint8_t* vram_ = nullptr;
    bool active_ = false;
    std::array<int, 4> last_hofs_{{-1, -1, -1, -1}};
    std::array<int, 4> scrolling_{};
    std::array<int, 4> cache_y_{};
    std::array<Desc, 4> cache_{};
};

// ── engine glue ──────────────────────────────────────────────────────────
enum class Scene { Native, Field, Battle, Generic, Pillarbox };
const char* scene_name(Scene s) {
    switch (s) {
        case Scene::Native: return "native";
        case Scene::Field: return "field";
        case Scene::Battle: return "battle";
        case Scene::Generic: return "screen";
        case Scene::Pillarbox: return "pillarbox";
    }
    return "?";
}
Scene g_scene = Scene::Native;
Scene g_last_scene = Scene::Native;
BattleView g_battle;
GenericView g_generic;
FieldView g_view;
ObjectView g_objects;
UiView g_ui;
ViewStatus g_last_status = ViewStatus::Native;
bool g_last_objects_ready = true;

int tile_provider(int bg, int x, int y, std::uint16_t* entry) {
    switch (g_scene) {
        case Scene::Field:
            return g_view.tile(bg, x, y, entry) ? gba::kWsTilemapReplace
                                                : gba::kWsTilemapUnavailable;
        case Scene::Battle: return g_battle.tile(bg, x, y, entry);
        case Scene::Generic: return gba::kWsTilemapKeepWrapped;
        default: return gba::kWsTilemapUnavailable;
    }
}
const gba::WsMarginObjPixel* object_provider(int y, int* left, int* width) {
    return g_scene == Scene::Field ? g_objects.row(y, left, width) : nullptr;
}
int xy_provider(int bg, int x, int y, int* sx, int* sy) {
    switch (g_scene) {
        case Scene::Field: return g_ui.sample(bg, x, y, sx, sy);
        case Scene::Battle: return g_battle.sample(bg, x, y, sx, sy);
        case Scene::Generic: return g_generic.sample(bg, x, y, sx, sy);
        default: return 0;
    }
}

constexpr std::uint32_t kOverworldBasic = 0x080565A8;  // CB2_OverworldBasic (battle transitions)
constexpr std::uint32_t kCB2_TitleScreenRun = 0x08078B9C;

}  // namespace

void install_extended_view(std::uint32_t, std::uint32_t) {
    gba::g_ws_tilemap_provider = tile_provider;
    gba::g_ws_authored_margin_layers = 1;
    gba::g_ws_obj_native_clip = 1;
    gba::g_ws_obj_margin_provider = object_provider;
    gba::g_ws_bg_xy_provider = xy_provider;
    gba::g_ws_bg_xy_provider_layers = 0xF;
    gba::g_ws_pillarbox = 1;
}

void update_extended_view(const gbarecomp::ExtendedViewFrameInfo* frame) {
    if (!frame) return;
    auto* bus = gbarecomp::active_bus();
    if (!bus) return;
    const ViewMemory memory{bus->ewram_ptr(), bus->iwram_ptr(), bus->rom_ptr(), bus->rom_size(),
                            bus->vram_ptr(), frame->io, bus->oam_ptr(), bus->pal_ptr()};
    const int width = static_cast<int>(frame->view_width);
    const auto status = g_view.prepare(memory, width, 160);
    const bool objects_ready = g_objects.prepare(memory, g_view, width);
    g_ui.prepare(memory, g_view, width);
    Scene scene = Scene::Native;
    if (width > 240) {
        const std::uint32_t cb2 = memory.u32(kMain + 4) & ~1u;
        if (status == ViewStatus::Ready) {
            scene = Scene::Field;
        } else if (cb2 == kOverworld || cb2 == kOverworldBasic) {
            // An unverified field frame or a battle transition wipe: keep
            // the centred image rather than guess at the scenery.
            scene = Scene::Pillarbox;
        } else if (g_battle.prepare(memory, frame->io, width)) {
            scene = Scene::Battle;
        } else {
            g_generic.prepare(frame->io, bus->vram_ptr(), width);
            scene = Scene::Generic;
        }
    }
    g_scene = scene;
    gba::g_ws_pillarbox = (scene == Scene::Native || scene == Scene::Pillarbox) ? 1 : 0;
    // Battles slide trainers/Pokemon/health boxes in from off-screen; let
    // them travel through the margins. Elsewhere sprites stay native.
    // Window effects outside the overworld (battle intro shutters, menu
    // highlights) span the whole native width: margins follow the edge.
    gba::g_ws_margin_window_clamp = (scene == Scene::Battle || scene == Scene::Generic) ? 1 : 0;
    const std::uint32_t cb2_now = memory.u32(kMain + 4) & ~1u;
    gba::g_ws_obj_native_clip =
        (scene == Scene::Battle || (scene == Scene::Generic && cb2_now == kCB2_TitleScreenRun)) ? 0 : 1;
    if (scene != g_last_scene) {
        std::fprintf(stderr, "[frlg:view] scene=%s frame=%llu\n", scene_name(scene),
                     static_cast<unsigned long long>(frame->frame_count));
        g_last_scene = scene;
    }
    if (status != g_last_status) {
        std::fprintf(stderr, "[frlg:view] %s frame=%llu width=%u reason=0x%x tile-check=%d/%d%s\n",
                     status_name(status), static_cast<unsigned long long>(frame->frame_count),
                     frame->view_width, g_view.reason(), g_view.matched(), g_view.compared(),
                     status == ViewStatus::Unverified ? " DEGRADED: native fallback" : "");
        g_last_status = status;
    }
    if (status == ViewStatus::Ready && objects_ready != g_last_objects_ready) {
        std::fprintf(stderr, "[frlg:objects] %s frame=%llu\n",
                     objects_ready ? "verified" : "DEGRADED: native objects only",
                     static_cast<unsigned long long>(frame->frame_count));
        g_last_objects_ready = objects_ready;
    }
}


// ── field snapshot for the 3D view (fr_diorama.cpp) ─────────────────────
namespace {
ViewMemory live_memory() {
    auto* bus = gbarecomp::active_bus();
    if (!bus) return {};
    return ViewMemory{bus->ewram_ptr(), bus->iwram_ptr(), bus->rom_ptr(), bus->rom_size(),
                      bus->vram_ptr(), bus->io().raw(), bus->oam_ptr(), bus->pal_ptr()};
}
}  // namespace

bool field_camera(int* origin_x, int* origin_y) {
    const ViewMemory m = live_memory();
    if (!m.ewram || !m.io) return false;
    static FieldView view;   // large; verification only
    if (view.prepare(m, kWidescreenWidth, 160) != ViewStatus::Ready) return false;
    if (origin_x) *origin_x = view.origin_x();
    if (origin_y) *origin_y = view.origin_y();
    return true;
}

bool field_cells(int mx0, int my0, int mw, int mh, std::vector<FieldCell>* out) {
    const ViewMemory m = live_memory();
    if (!out || !m.ewram || mw <= 0 || mh <= 0) return false;
    Map map{m};
    if (!map.load()) return false;
    map.extend_connections(mx0, my0, mx0 + mw - 1, my0 + mh - 1);
    std::vector<unsigned> doors, tall_doors;
    for (int i = 0; i < kDoorGraphicsMax; ++i) {
        const auto* d = m.bytes(kDoorGraphics + i * 12, 12);
        if (!d || !m.u32(kDoorGraphics + i * 12 + 4)) break;
        doors.push_back(read16(d));
        if (d[3] == 1) tall_doors.push_back(read16(d));
    }
    auto door_behavior = [&](unsigned id) {
        const unsigned set = id < kPrimaryMetatiles ? 0 : 1;
        const unsigned index = set ? id - kPrimaryMetatiles : id;
        return (m.u32(map.attributes[set] + index * 4) & 0x1ff) == 0x69;  // MB_ANIMATED_DOOR
    };
    out->assign(static_cast<std::size_t>(mw) * mh, FieldCell{});
    for (int y = 0; y < mh; ++y) for (int x = 0; x < mw; ++x) {
        FieldCell& c = (*out)[static_cast<std::size_t>(y) * mw + x];
        bool border = false;
        const unsigned g = map.raw(mx0 + x, my0 + y, &border);
        const unsigned id = g & 0x3ff;
        const unsigned set = id < kPrimaryMetatiles ? 0 : 1;
        const unsigned index = set ? id - kPrimaryMetatiles : id;
        const auto* tiles = m.bytes(map.metatiles[set] + index * 16, 16);
        const auto* attr = m.bytes(map.attributes[set] + index * 4, 4);
        if (!tiles || !attr) continue;
        const unsigned a = read16(attr) | (read16(attr + 2) << 16);
        c.valid = true;
        c.border = border;
        c.id = static_cast<std::uint16_t>(id);
        const unsigned below = map.metatile(mx0 + x, my0 + y + 1, true);
        // Secondary metatile ids are reused by different tilesets (including plain grass).
        c.door = (door_behavior(id) && std::find(doors.begin(), doors.end(), id) != doors.end()) ||
            (door_behavior(below) && std::find(tall_doors.begin(), tall_doors.end(), below) != tall_doors.end());
        c.collision = static_cast<std::uint8_t>((g >> 10) & 3);
        c.elevation = static_cast<std::uint8_t>(g >> 12);
        c.behavior = static_cast<std::uint16_t>(a & 0x1ff);
        c.layer = static_cast<std::uint8_t>((a >> 29) & 3);
        for (int i = 0; i < 8; ++i) c.entries[i] = static_cast<std::uint16_t>(read16(tiles + i * 2));
    }
    return true;
}

void field_door_frames(int origin_x, int origin_y, int mx0, int my0, int mw, int mh,
                       std::vector<FieldCell>* cells) {
    const ViewMemory m = live_memory();
    if (!cells || mw <= 0 || mh <= 0 || cells->size() != static_cast<std::size_t>(mw) * mh ||
        !m.io || !m.vram) return;
    Map map{m};
    if (!map.load()) return;
    const int hofs = read16(m.io + 0x14) & 255, vofs = read16(m.io + 0x16) & 255;
    const int otx = floor8(origin_x), oty = floor8(origin_y);
    unsigned sb[3];
    for (int bg = 0; bg < 3; ++bg) sb[bg] = ((read16(m.io + 10 + bg * 2) >> 8) & 31) * 0x800;
    for (int y = 0; y < mh; ++y) for (int x = 0; x < mw; ++x) {
        FieldCell& c = (*cells)[static_cast<std::size_t>(y) * mw + x];
        if (!c.valid || !c.door) continue;
        std::uint16_t bottom[4], top[4];
        bool verified = true;
        for (int q = 0; q < 4 && verified; ++q) {
            const int tx = (mx0 + x) * 2 + (q & 1), ty = (my0 + y) * 2 + (q >> 1);
            const int sx = tx - otx, sy = ty - oty;
            // Outside the native view the ring may contain stale tiles.
            if (sx < 0 || sx >= 30 || sy < 0 || sy >= 20) { verified = false; break; }
            const int ring = wrap((hofs >> 3) + sx, 32) + 32 * wrap((vofs >> 3) + sy, 32);
            std::uint16_t actual[3];
            for (int bg = 0; bg < 3; ++bg) actual[bg] = read16(m.vram + sb[bg] + ring * 2);
            if (!map.door_entries(tx, ty, actual)) { verified = false; break; }
            bottom[q] = actual[2]; top[q] = actual[1];
        }
        if (!verified) continue;
        std::copy_n(bottom, 4, c.entries);
        std::copy_n(top, 4, c.entries + 4);
        c.animated = true;
    }
}

}  // namespace frlg
