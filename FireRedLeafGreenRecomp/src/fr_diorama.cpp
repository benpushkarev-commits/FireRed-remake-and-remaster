// fr_diorama.cpp — tilted "diorama" 3D view of the overworld. See fr_diorama.h.
//
// Geometry comes from an explicit tile table, not from guessing at pixels:
// every metatile of a known tileset is assigned a model (flat ground, a
// standing object, part of a tree, part of a building, tall grass, ...).
// Unknown tiles stay flat ground, so a map without a table still renders
// cleanly (with standing sprites). Art always comes live from VRAM.
//
// World space is art pixels: X right, Y south, Z up, in the padded map grid
// (the space of fr_widescreen's camera). The camera looks north and down at
// kAlpha degrees; Y is stretched by 1/sin(alpha) and Z by 1/cos(alpha), so an
// orthographic view would reproduce the native picture exactly. Perspective
// adds the depth. Everything is drawn by a small software rasterizer with a
// depth buffer, so overlaps are always correct and no painter sorting exists.

#include "fr_diorama.h"

#include <SDL.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <string>
#include <thread>
#include <vector>

#include "fr_guest.h"
#include "fr_hd_draw.h"
#include "fr_pc_input.h"
#include "fr_settings_menu.h"
#include "fr_widescreen.h"
#include "gba_ppu.h"
#include "host_overlay.h"
#include "host_window.h"
#include "runtime.h"
#include "imgui.h"
#include "imgui_internal.h"

namespace frlg {
namespace hd { bool fonts_ready(); }
namespace {

constexpr std::uint32_t kCB2_Overworld = 0x080565B4;
constexpr std::uint32_t kMapHeader = 0x02036DFC;
// Tilesets with a model table (pret symbol addresses, FireRed USA rev0).
constexpr std::uint32_t kTilesetGeneral = 0x082D4A94;
constexpr std::uint32_t kTilesetPalletTown = 0x082D4AAC;
constexpr std::uint32_t kTilesetViridianCity = 0x082D4AC4;
constexpr std::uint32_t kTilesetBuilding = 0x082D4BB4;
constexpr std::uint32_t kTilesetGenericBuilding2 = 0x082D4E84;   // secondary of the Route 2 / Forest gate rooms (layout 08320990)
constexpr std::uint32_t kTilesetGenericBuilding1 = 0x082D4C74;
constexpr std::uint32_t kTilesetPokemonCenter = 0x082D4BE4;
constexpr std::uint32_t kTilesetPewterCity = 0x082D4ADC;   // secondary of Pewter (measured: prim General, layout 082DF308)
constexpr std::uint32_t kTilesetViridianForest = 0x082D4DC4;   // secondary of Viridian Forest (measured: prim General, layout 082FE46C)

// ── tunables (FRLG_3D_ALPHA / FOV / ZOOM / SCALE for testing) ────────────
float env_float(const char* name, float def, float lo, float hi) {
    const char* v = std::getenv(name);
    if (!v || !*v) return def;
    char* end = nullptr;
    const float parsed = std::strtof(v, &end);
    if (end == v || *end || !std::isfinite(parsed)) return def;
    return std::clamp(parsed, lo, hi);
}
struct Tune {
    float alpha = 45.0f;   // view elevation (degrees below the horizon)
    float fov = 28.0f;     // vertical field of view (degrees)
    float zoom = 0.76f;    // view units per art pixel at the focus
    float scale = 8.0f;    // framebuffer pixels per view unit (upper bound; the pixel budget usually decides)
    float budget = 1.7e6f; // framebuffer pixel budget (about 1704x960)
    bool loaded = false;
} g_tune;
void load_tune() {
    if (g_tune.loaded) return;
    g_tune.alpha = env_float("FRLG_3D_ALPHA", g_tune.alpha, 25.0f, 70.0f);
    g_tune.fov = env_float("FRLG_3D_FOV", g_tune.fov, 10.0f, 70.0f);
    g_tune.zoom = env_float("FRLG_3D_ZOOM", g_tune.zoom, 0.6f, 2.0f);
    g_tune.scale = env_float("FRLG_3D_SCALE", g_tune.scale, 1.0f, 12.0f);
    g_tune.budget = env_float("FRLG_3D_PIXELS", g_tune.budget, 2.0e5f, 8.0e6f);
    g_tune.loaded = true;
}

// ── colours ──────────────────────────────────────────────────────────────
inline std::uint32_t rgb555(std::uint16_t c) {   // -> ABGR8888 (IM_COL32 layout)
    const std::uint32_t r = (c & 31) * 255 / 31, g = ((c >> 5) & 31) * 255 / 31, b = ((c >> 10) & 31) * 255 / 31;
    return 0xFF000000u | (b << 16) | (g << 8) | r;
}
inline ImU32 shade(float k, float a = 1.0f) {
    const int v = static_cast<int>(std::clamp(k, 0.0f, 1.0f) * 255.0f + 0.5f);
    return IM_COL32(v, v, v, static_cast<int>(std::clamp(a, 0.0f, 1.0f) * 255.0f + 0.5f));
}
inline std::uint32_t scale_rgb(std::uint32_t p, float k) {
    const auto ch = [&](int s) { return static_cast<std::uint32_t>(std::min(255.0f, ((p >> s) & 255) * k)) << s; };
    return (p & 0xFF000000u) | ch(0) | ch(8) | ch(16);
}

// ── SDL output ───────────────────────────────────────────────────────────
SDL_Renderer* renderer() {
    auto* hw = gbarecomp::active_host_window();
    return hw ? static_cast<SDL_Renderer*>(hw->sdl_renderer()) : nullptr;
}
ImTextureID tex_id(SDL_Texture* t) { return static_cast<ImTextureID>(reinterpret_cast<std::intptr_t>(t)); }
SDL_Texture* make_texture(int w, int h, bool linear, bool streaming) {
    SDL_Renderer* r = renderer();
    if (!r) return nullptr;
    SDL_Texture* t = SDL_CreateTexture(r, SDL_PIXELFORMAT_ABGR8888,
                                       streaming ? SDL_TEXTUREACCESS_STREAMING : SDL_TEXTUREACCESS_STATIC, w, h);
    if (!t) return nullptr;
    SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
#if SDL_VERSION_ATLEAST(2, 0, 12)
    SDL_SetTextureScaleMode(t, linear ? SDL_ScaleModeLinear : SDL_ScaleModeNearest);
#endif
    return t;
}
SDL_Texture* g_bg0 = nullptr;     // classic text windows layered over the scene
SDL_Texture* g_frame = nullptr;   // the rendered 3D picture
int g_frame_w = 0, g_frame_h = 0;

// ── camera ───────────────────────────────────────────────────────────────
struct Cam {
    float tx = 0, ty = 0;          // focus (art px)
    float sa = 0.7f, ca = 0.7f;    // sin / cos of the elevation
    float sy = 1.4f, sz = 1.4f;    // Y / Z stretch
    float d = 300, f = 300;        // distance to focus, focal length (view units)
    float cx = 142, cy = 80;       // focus on screen (view units)
    // art (X, Y, Z) -> view units and camera depth; false behind the near plane
    bool project(float X, float Y, float Z, float* x, float* y, float* depth) const {
        const float vx = X - tx;
        const float vy = (Y - ty) * sy - d * ca;
        const float vz = Z * sz - d * sa;
        const float zc = -(vy * ca + vz * sa);
        if (zc < 16.0f) return false;
        const float yc = -vy * sa + vz * ca;
        *x = cx + f * vx / zc;
        *y = cy - f * yc / zc;
        *depth = zc;
        return true;
    }
    // view point -> ground (Z = 0) in art px
    void unproject(float sx, float syv, float max_dist, float* X, float* Y) const {
        const float dx = (sx - cx) / f, dy = -(syv - cy) / f;
        const float dirx = dx, diry = -sa * dy - ca, dirz = ca * dy - sa;
        float t = dirz < -1e-4f ? (-d * sa) / dirz : max_dist;
        t = std::min(t, max_dist);
        *X = tx + dirx * t;
        *Y = ty + (d * ca + diry * t) / sy;
    }
};

// ── software rasterizer ──────────────────────────────────────────────────
// Textures are plain RGBA arrays; texels with alpha 0 are discarded. Opaque
// triangles write depth; blended ones (shadows, semi-transparent sprites)
// only test it. Depth is 1/z (larger = nearer), perspective-correct UVs.
// Triangles are queued and rasterized in horizontal bands on several threads;
// each band keeps submission order, so blending stays deterministic.
struct Tex {
    int w = 0, h = 0;
    const std::uint32_t* px = nullptr;
};
struct Raster {
    int w = 0, h = 0;
    float s = 1;                      // framebuffer pixels per view unit
    std::vector<std::uint32_t> color;
    std::vector<float> depth;
    struct V { float x, y, iz, u, v; };   // u, v already divided by z
    struct Tri { V a, b, c; Tex t; int ka, aa; bool blend; std::uint32_t quad; bool affine_u; };   // affine_u: u is plain (not divided by z); v and depth stay perspective-correct
    std::vector<Tri> tris;
    std::vector<std::uint32_t> blend_stamp;   // per pixel: the last blended quad that wrote it
    std::uint32_t quads = 0;                    // last quad id handed out (ids keep growing across frames; 0 = no seam protection)
    void reset(int nw, int nh, float ns, std::uint32_t clear) {
        w = nw; h = nh; s = ns;
        color.assign(static_cast<std::size_t>(w) * h, clear);
        depth.assign(static_cast<std::size_t>(w) * h, 0.0f);
        // Quad ids only grow, so stamps left from earlier frames can never equal a new id: no per-frame clear of a full-frame buffer
        // (it was a 4-byte-per-pixel memset each frame); it is zeroed on a resize and before the id counter could wrap.
        if (blend_stamp.size() != static_cast<std::size_t>(w) * h || quads > 0xFFFF0000u) {
            blend_stamp.assign(static_cast<std::size_t>(w) * h, 0u);
            quads = 0;
        }
        tris.clear();
    }
    // quad: both triangles of one translucent quad share an id so a pixel on their shared edge blends once
    void tri(const V& a, const V& b, const V& c, const Tex& t, float k, float alpha, std::uint32_t quad = 0, bool affine_u = false) {
        if (!t.px || t.w <= 0 || t.h <= 0) return;
        tris.push_back({a, b, c, t, static_cast<int>(std::clamp(k, 0.0f, 2.0f) * 256.0f),
                        static_cast<int>(std::clamp(alpha, 0.0f, 1.0f) * 256.0f), alpha < 0.999f, quad, affine_u});
    }
    void band(const Tri& T, int ylo, int yhi) {
        const V &a = T.a, &b = T.b, &c = T.c;
        const float area = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
        if (std::fabs(area) < 1e-6f) return;
        const int x0 = std::max(0, static_cast<int>(std::floor(std::min({a.x, b.x, c.x}))));
        const int x1 = std::min(w - 1, static_cast<int>(std::ceil(std::max({a.x, b.x, c.x}))));
        const int y0 = std::max(ylo, static_cast<int>(std::floor(std::min({a.y, b.y, c.y}))));
        const int y1 = std::min(yhi - 1, static_cast<int>(std::ceil(std::max({a.y, b.y, c.y}))));
        if (x0 > x1 || y0 > y1) return;
        const float inv = 1.0f / area;
        // barycentrics and the interpolated values are linear in screen x
        const float d0 = (b.y - c.y) * inv, d1 = (c.y - a.y) * inv, d2 = -(d0 + d1);
        const float diz = d0 * a.iz + d1 * b.iz + d2 * c.iz;
        const float du = d0 * a.u + d1 * b.u + d2 * c.u, dv = d0 * a.v + d1 * b.v + d2 * c.v;
        const Tex& t = T.t;
        for (int py = y0; py <= y1; ++py) {
            const float fy = py + 0.5f, fx = x0 + 0.5f;
            float w0 = ((b.x - fx) * (c.y - fy) - (b.y - fy) * (c.x - fx)) * inv;
            float w1 = ((c.x - fx) * (a.y - fy) - (c.y - fy) * (a.x - fx)) * inv;
            float w2 = 1.0f - w0 - w1;
            float iz = w0 * a.iz + w1 * b.iz + w2 * c.iz;
            float uz = w0 * a.u + w1 * b.u + w2 * c.u, vz = w0 * a.v + w1 * b.v + w2 * c.v;
            std::uint32_t* crow = &color[static_cast<std::size_t>(py) * w];
            float* drow = &depth[static_cast<std::size_t>(py) * w];
            std::uint32_t* brow = &blend_stamp[static_cast<std::size_t>(py) * w];
            for (int px = x0; px <= x1; ++px, w0 += d0, w1 += d1, w2 += d2, iz += diz, uz += du, vz += dv) {
                // small tolerance: the stepped barycentrics drift, and a shared
                // edge must never be claimed by neither triangle (cracks)
                if (w0 < -1e-4f || w1 < -1e-4f || w2 < -1e-4f || iz <= drow[px]) continue;
                if (T.quad && T.blend && brow[px] == T.quad) continue;   // the sibling triangle already blended this pixel
                const float zf = 1.0f / iz;
                const int tu = std::clamp(static_cast<int>(T.affine_u ? uz : uz * zf), 0, t.w - 1);
                const int tv = std::clamp(static_cast<int>(vz * zf), 0, t.h - 1);
                const std::uint32_t texel = t.px[tv * t.w + tu];
                const int ta = static_cast<int>(texel >> 24);
                if (!ta) continue;
                const int mix = T.blend || ta < 255 ? (T.aa * ta) >> 8 : 256;
                std::uint32_t out = 0xFF000000u;
                for (int sh = 0; sh < 24; sh += 8) {
                    const int src = std::min(255, ((static_cast<int>(texel >> sh) & 255) * T.ka) >> 8);
                    const int dst = static_cast<int>(crow[px] >> sh) & 255;
                    out |= static_cast<std::uint32_t>((src * mix + dst * (256 - mix)) >> 8) << sh;
                }
                crow[px] = out;
                if (mix >= 256) drow[px] = iz;
                else if (T.quad && T.blend) brow[px] = T.quad;
            }
        }
    }
    void flush() {
        const int n = static_cast<int>(std::clamp(std::thread::hardware_concurrency(), 1u, 8u));
        const int rows = (h + n - 1) / n;
        // Opaque triangles first, then the blended ones (each group in submission order): a blended triangle only tests depth, so a NEAR translucent surface
        // submitted before a FAR opaque one used to be overwritten by it (the order decided the picture). Opaque-then-blended is order independent for that case
        // and identical to the old order whenever the blended surface was submitted last.
        auto run = [this](int lo, int hi) {
            for (const Tri& t : tris) if (!t.blend) band(t, lo, hi);
            for (const Tri& t : tris) if (t.blend) band(t, lo, hi);
        };
        std::vector<std::thread> workers;
        for (int i = 1; i < n; ++i)
            workers.emplace_back([&run, i, rows, this] { run(i * rows, std::min(h, (i + 1) * rows)); });
        run(0, std::min(h, rows));
        for (auto& th : workers) th.join();
        tris.clear();
    }
};
// Screen-space offset that moves a coordinate (framebuffer pixels) onto the pixel grid. A sprite is translated by this for its
// feet-left corner: size, shape and depth are untouched (a pure 2D shift of at most half a pixel), but the sprite's texel boundaries then
// sit at the same sub-pixel phase every frame, so its pixel widths do not swim as it moves.
inline float snap_delta(float fb_coord) { return std::floor(fb_coord + 0.5f) - fb_coord; }
Raster g_raster;

// One textured quad in world space: corners p (X, Y, Z), texture corners uv.
// rectify (characters only, test-only FRLG_3D_SPRITE_RECT=1): the quad's top corners take the x of its bottom corners, so an upright sprite is a screen-rectangle
// instead of the perspective trapezoid (measured: a 16 x 32 sprite is 36 px wide at the feet and 40 px at the head). y, depth (1/z) and the texture rows of every
// vertex are unchanged, so the feet, the occlusion decisions and the row mapping are exactly the same; only the per-row width is made constant.
void draw_quad(const Cam& cam, const float p[4][3], const float uv[4][2], const Tex& t, float k, float alpha = 1.0f, bool snap_feet = false, bool rectify = false) {
    Raster::V v[4];
    for (int i = 0; i < 4; ++i) {
        float x, y, z;
        if (!cam.project(p[i][0], p[i][1], p[i][2], &x, &y, &z)) return;
        const float iz = 1.0f / z;
        v[i] = {x * g_raster.s, y * g_raster.s, iz, uv[i][0] * iz, uv[i][1] * iz};
    }
    if (rectify) {   // p[0..1] are the top corners, p[3..2] the bottom corners below them
        v[0].x = v[3].x; v[1].x = v[2].x;
        // the texel column must be an exact function of screen x: with u/z interpolated over two triangles the interior columns bend across the diagonal
        // (measured: 10 % of the pixels of a 16 x 32 sprite sample another column); u is therefore interpolated affinely (v and depth stay perspective-correct)
        for (int i = 0; i < 4; ++i) v[i].u = uv[i][0];
    }
    if (snap_feet) {   // p[3] is the bottom-left corner (the feet of an upright sprite)
        const float dx = snap_delta(v[3].x), dy = snap_delta(v[3].y);
        for (auto& q : v) { q.x += dx; q.y += dy; }
    }
    const std::uint32_t id = alpha < 0.999f ? ++g_raster.quads : 0u;   // translucent quads blend each pixel once
    g_raster.tri(v[0], v[1], v[2], t, k, alpha, id, rectify);
    g_raster.tri(v[0], v[2], v[3], t, k, alpha, id, rectify);
}
// Upright quad facing the camera (normal -Y) from (X0, Y) to (X1, Y), Z0..Z1,
// texture rows v0..v1 (top to bottom).
void draw_upright(const Cam& cam, float X0, float X1, float Y, float Z0, float Z1, const Tex& t,
                  float u0, float u1, float v0, float v1, float k, float alpha = 1.0f, bool snap_feet = false, bool rectify = false) {
    const float p[4][3] = {{X0, Y, Z1}, {X1, Y, Z1}, {X1, Y, Z0}, {X0, Y, Z0}};
    const float uv[4][2] = {{u0, v0}, {u1, v0}, {u1, v1}, {u0, v1}};
    draw_quad(cam, p, uv, t, k, alpha, snap_feet, rectify);
}

// ── drawing helpers for the ImGui layer (text windows, toast) ───────────
struct Out {
    ImDrawList* dl = nullptr;
    float ox = 0, oy = 0, s = 1;
    ImVec2 P(const ImVec2& v) const { return ImVec2(ox + v.x * s, oy + v.y * s); }
};

// ── guest decoding ───────────────────────────────────────────────────────
inline int bg_index(const std::uint8_t* vram, unsigned char_base, std::uint16_t e, int x, int y) {
    if (e & 0x400) x = 7 - x;
    if (e & 0x800) y = 7 - y;
    const unsigned a = char_base + (e & 1023u) * 32 + y * 4 + x / 2;
    if (a >= 0x10000) return 0;
    return (vram[a] >> ((x & 1) * 4)) & 15;
}
inline std::uint16_t pal16(const Mem& m, int i) { return static_cast<std::uint16_t>(m.pal[i * 2] | (m.pal[i * 2 + 1] << 8)); }

enum Layers { kBoth, kBottom, kTop };
// 16x16 metatile art. kBoth/kBottom fill empty pixels with the backdrop
// colour (like the GBA); kTop leaves them transparent.
void metatile_art(const Mem& m, unsigned cb, const std::uint16_t* entries, Layers which, std::uint32_t* out) {
    const std::uint32_t back = which == kTop ? 0u : rgb555(pal16(m, 0));
    std::fill(out, out + 256, back);
    for (int layer = which == kTop ? 1 : 0; layer < (which == kBottom ? 1 : 2); ++layer)
        for (int q = 0; q < 4; ++q) {
            const std::uint16_t e = entries[layer * 4 + q];
            const int pal = (e >> 12) * 16;
            for (int y = 0; y < 8; ++y) for (int x = 0; x < 8; ++x) {
                const int idx = bg_index(m.vram, cb, e, x, y);
                if (idx) out[((q >> 1) * 8 + y) * 16 + (q & 1) * 8 + x] = rgb555(pal16(m, pal + idx));
            }
        }
}
bool has_top(const std::uint16_t* entries) {
    return entries[4] || entries[5] || entries[6] || entries[7];
}

// ── model table ──────────────────────────────────────────────────────────
// kStand: an object standing upright at its front edge. A vertical run of
//         kStand cells is one object (so a tile used in different stacks in
//         different rooms still works); b = 1 starts a new object even with
//         another stand above. Art: the top layer when the tile has one, else
//         the full art minus ground colours.
// kTree:  part of a 2x3 pine (tip row, upper and lower crown). a = 0 left /
//         1 right half, b = row. Every tree is drawn as one clean model at its
//         position, so dense forests become individual trees.
// kSeat:  a low stool / cushion: one independent cell, a horizontal seat at kSeatHeight with short solid sides
//         (the tile's own art: top rows on the seat, bottom rows on the front, its rim on the sides).
// kLedge: ground cell whose top layer is a thin raised strip (the far edge of a counter top): the bottom layer is the
//         floor, art rows b..15 of the top layer lie flat at height a (see ledge_quad).
// kLedge with fg > 0 also stands the tile's first fg rows (both layers composited) up as the lower part of a machine; the tile above it
//         (kMachineTop, a back-wall row) supplies the upper part from its top layer, on the SAME plane (see machine_spans). No lid.
// kCounterTop: the counter's far end: its bottom layer is the full counter top (lies flat at height a), the top layer's rows
//         b..15 are an object (a plant) standing on it; end faces from the face rows of the tile in front.
// kPlanter: the front of a low planter below a wall row (Viridian town house 2): art rows 0..b-1 are the planter top lying flat at height a
//         (continuing the wall cell above's band rows), rows b..b+a-1 its vertical front face; fg = the band row of the wall cell above (for the end caps).
// kMulti: a cell of a multi-tile object that is drawn as ONE upright billboard composed from the TOP layers of all its cells (the forest's 3x3 tree):
//         the cell only counts when the whole pattern (MultiDef) is present in the snapshot; a partial one falls back to flat.
// kGrass: tall grass; kRoof / kWall: part of a building or piece of furniture
//         (walls stand, roofs slope back outdoors and lie flat indoors; one box
//         per connected group).
// ground: metatile drawn flat underneath instead of the cell's own art.
enum Kind : std::uint8_t { kFlat, kStand, kTree, kGrass, kRoof, kWall, kSeat, kLedge, kCounterTop, kMachineTop, kPlanter, kMulti, kCanopy, kTrunk, kGateRoof };
enum TreeRow : std::uint8_t { kTip, kUpper, kLower };
// kStand with a = kFence: a fence piece. Its orientation comes from the fence
// cells around it (see fence_orient), so one id works for either direction.
constexpr std::uint8_t kFence = 2;
// Pewter brown fence (TEST ONLY, FRLG_3D_PEWTER_FENCE=1): fixed orientation per id (the neighbours do not decide): a front-facing strip / an end-on N-S panel.
constexpr std::uint8_t kFenceBrownFront = 3, kFenceBrownNS = 4;
struct TileModel {
    std::uint16_t id;
    std::uint8_t kind, a = 0, b = 0;
    std::uint16_t ground = 0xFFFF;
    std::uint8_t fg = 0;   // kStand: first art row that is foreground; rows above are ground, not object
    std::uint8_t art = 0;  // kRoof / kWall: 0 keyed full art, 1 top layer only (its bottom layer is background)
    std::uint8_t planter = 0;  // kWall: bottom art rows that are a decorative front planter (not side material)
    std::uint8_t face = 0;     // kWall: bottom art rows from the first vertical-front row to the cell bottom (a table rim / bed foot); the rows above lie flat on the top plane. 0 = the whole cell
    std::uint8_t skip = 0;     // kWall: of those, the last rows that are a baked floor shadow: not part of the face (the floor stays), the face stands on the row above
    std::uint8_t band = 0;     // kWall: first art row that is NOT wall (a flower / planter top lying flat at height a); rows 0..band-1 are the wall. 0 = the whole cell
};
constexpr std::uint16_t kGrassL = 0x010, kGrassR = 0x011;   // plain grass halves under trees
constexpr TileModel kGeneralModels[] = {
    {0x002, kStand, 0, 1}, {0x003, kStand, 0, 1},                          // metal and wooden signs
    {0x004, kStand, 0, 1},                                     // flowers
    {0x00D, kGrass},                                           // tall grass
    {0x00E, kTree, 0, kTip, kGrassL}, {0x00F, kTree, 1, kTip, kGrassR},
    {0x01C, kTree, 0, kUpper, kGrassL}, {0x01D, kTree, 1, kUpper, kGrassR},
    {0x01E, kTree, 0, kUpper, kGrassL}, {0x01F, kTree, 1, kUpper, kGrassR},
    {0x014, kTree, 0, kLower, kGrassL}, {0x015, kTree, 1, kLower, kGrassR},
    {0x016, kTree, 0, kLower, kGrassL}, {0x017, kTree, 1, kLower, kGrassR},
    {0x024, kTree, 0, kLower, kGrassL}, {0x025, kTree, 1, kLower, kGrassR},
    {0x026, kTree, 0, kLower, kGrassL}, {0x027, kTree, 1, kLower, kGrassR},
    {0x087, kStand, 0, 1}, {0x097, kStand, 0, 1}, {0x0B0, kStand, 0, 1}, {0x0B1, kStand, 0, 1},   // ledges
    {0x0C0, kStand, 0, 1}, {0x0C1, kStand, 0, 1}, {0x0C8, kStand, 0, 1}, {0x0C9, kStand, 0, 1},
    {0x0E7, kStand, kFence, 1}, {0x0EC, kStand, kFence, 1}, {0x0ED, kStand, kFence, 1},                          // white fence
};
// Pewter's brown fence around the flower bed (General primary ids, measured on layout 082DF308 world x33..35 y32..37; TOP layer rows, bottom layer = grass):
//   0E8 north-west corner (y32 x33), 0E6 east-west run (y32 x34..), 0F0 north-south run (x33 y33..36), 0F8 south-west corner (x33 y37); all blocking.
//   0E6 / 0E8 / 0F8: rows 0..1 empty (0F8: the post of the run above, cols 1..6), rows 2..13 the rail face (12 high, cols 1..14, colours 4A4A7B / 41395A / A4C5DE),
//   rows 14..15 a baked grey ground shadow (837B7B, not a ground colour). The face stands on row 13 (12 high); rows 14..15 stay flat on the ground (skip 2).
//   0F0: a 6-7 px wide strip, rows 0..15, the rail seen end-on, drawn as the end-on N-S panel the white fence already uses (columns -> height).
// Existing fence ids (0E7 0EC 0ED and the ledges 0B0 / 087 next to the corner) are untouched.
constexpr TileModel kPewterFenceModels[] = {
    {0x0E6, kStand, kFenceBrownFront, 1, 0xFFFF, 0, 0, 0, 0, 2}, {0x0E8, kStand, kFenceBrownFront, 1, 0xFFFF, 0, 0, 0, 0, 2},
    {0x0F8, kStand, kFenceBrownFront, 1, 0xFFFF, 0, 0, 0, 0, 2}, {0x0F0, kStand, kFenceBrownNS, 1},
};
constexpr TileModel kPalletModels[] = {
    {0x281, kRoof}, {0x282, kRoof}, {0x283, kRoof}, {0x289, kRoof}, {0x28A, kRoof}, {0x28B, kRoof},   // houses
    {0x291, kRoof}, {0x292, kRoof}, {0x293, kRoof}, {0x294, kRoof},
    {0x298, kWall}, {0x299, kWall}, {0x29A, kWall}, {0x29B, kWall}, {0x29C, kWall},
    {0x2A0, kWall}, {0x2A1, kWall}, {0x2A2, kWall}, {0x2A3, kWall}, {0x2A4, kWall},
    {0x2A8, kRoof}, {0x2A9, kRoof}, {0x2B5, kRoof}, {0x2BD, kRoof},                                  // Oak's lab
    {0x2B0, kRoof}, {0x2B1, kRoof}, {0x2B3, kRoof}, {0x2B4, kRoof},
    {0x2B8, kRoof}, {0x2B9, kRoof}, {0x2BB, kRoof}, {0x2BC, kRoof},
    {0x2C0, kWall}, {0x2C1, kWall}, {0x2C2, kWall}, {0x2C3, kWall}, {0x2C4, kWall}, {0x2C5, kWall}, {0x2D0, kWall},
    {0x2C8, kWall}, {0x2C9, kWall}, {0x2AC, kWall}, {0x2CB, kWall}, {0x2CC, kWall}, {0x2CD, kWall}, {0x2D8, kWall},
    {0x2A5, kStand, 0, 1}, {0x2AD, kStand},                                                          // mailbox
    {0x284, kStand, kFence, 1}, {0x287, kStand, kFence, 1},                                                    // fences
    {0x2AB, kTree, 1, kTip, kGrassR}, {0x2AA, kTree, 0, kTip, kGrassL},                              // Route 1 gate trees (2AB = right half of the left tree)
    {0x2B2, kTree, 0, kUpper, kGrassL}, {0x2BA, kTree, 1, kUpper, kGrassR},
};
// Viridian City (secondary 0x082D4AC4). Every id below is from the measured
// component grids of ViridianCity_Layout (General primary ids < 0x280 and
// Viridian secondary ids >= 0x280 mix inside one building, so the groups are
// scoped to this tileset pair). Ids not listed keep the generic door-component
// fallback: cliff / tree-edge cells (28E, 28F, 295, 2BC) and the door-approach
// ground 0CE are deliberately NOT classified. (187/18F/197/186/19F are NOT trees:
// they are the Pokémon Center's right roof / facade column, measured from the sheet.)
//   Gym (x33..38, y6..10): roof cap 2AD-2AF, roof 141-143 / 149-14B, front 150-154,156 / 158-15C,15E
//     (badge 154, door 15A); rows 6..8 roof, 9..10 wall.
//   House 1 (x24..28, y9..11): roof 288-28C / 290-294, one wall row 29D 299(door) 2A5-2A7.
//   House 2 (x24..28, y16..19): same roof, two wall rows 298-29C / 2A0 [0CE ground] 2A2-2A4.
//   Mart (x34..37, y16..19): roof 028-02B / 030-033 / 038-03B, wall 040 041 062(door) 063.
//   Pokémon Center (x24..28, y22..26): cap 2B0-2B4, roof 048-04B,187 / 050-053,18F, wall 058-05B,197 / 060 061 062(door) 186 19F.
//   Town house cap row (above the roof): 280-284 (top layer only, the bottom layer is grass).
constexpr TileModel kViridianModels[] = {
    {0x2AD, kRoof}, {0x2AE, kRoof}, {0x2AF, kRoof},                                                  // gym roof cap
    {0x141, kRoof}, {0x142, kRoof}, {0x143, kRoof}, {0x149, kRoof}, {0x14A, kRoof}, {0x14B, kRoof},  // gym roof
    {0x150, kWall}, {0x151, kWall}, {0x152, kWall}, {0x153, kWall}, {0x154, kWall}, {0x156, kWall},  // gym front
    {0x158, kWall}, {0x159, kWall}, {0x15A, kWall}, {0x15B, kWall}, {0x15C, kWall}, {0x15E, kWall},
    {0x288, kRoof}, {0x289, kRoof}, {0x28A, kRoof}, {0x28B, kRoof}, {0x28C, kRoof},                  // town houses, roof
    {0x290, kRoof}, {0x291, kRoof}, {0x292, kRoof}, {0x293, kRoof}, {0x294, kRoof},
    // House 2 front (measured rows): the wall row 298 / 299 (door, the whole tile) / 29A-29C is the 16-unit wall; 298 / 29A / 29C rows 0..7 are wall and
    // rows 8..15 the top of a low planter (flowers) lying flat at height 6; 2A0 / 2A2-2A4 (walkable foreground, collision 0) rows 0..1 are the rest of that
    // top and rows 2..7 its 6-row vertical front, rows 8..15 plain grass. 29B (measured: window rows 0..7, dark rim row 8, glaze 9..14 = the same blue as
    // the 2A planter front, flowers row 15) is part of the same planter band (the whole band is one object); there is no 2A1 in this house.
    {0x298, kWall, 6, 0, 0xFFFF, 0, 0, 6, 0, 0, 8}, {0x299, kWall}, {0x29A, kWall, 6, 0, 0xFFFF, 0, 0, 6, 0, 0, 8},
    {0x29B, kWall, 6, 0, 0xFFFF, 0, 0, 6, 0, 0, 8}, {0x29C, kWall, 6, 0, 0xFFFF, 0, 0, 6, 0, 0, 8}, {0x29D, kWall},
    {0x2A0, kPlanter, 6, 2, 0xFFFF, 8}, {0x2A2, kPlanter, 6, 2, 0xFFFF, 8},
    {0x2A3, kPlanter, 6, 2, 0xFFFF, 8}, {0x2A4, kPlanter, 6, 2, 0xFFFF, 8},
    {0x2A5, kWall}, {0x2A6, kWall}, {0x2A7, kWall},
    {0x028, kRoof}, {0x029, kRoof}, {0x02A, kRoof}, {0x02B, kRoof},                                  // Mart roof
    {0x030, kRoof}, {0x031, kRoof}, {0x032, kRoof}, {0x033, kRoof},
    {0x038, kRoof}, {0x039, kRoof}, {0x03A, kRoof}, {0x03B, kRoof},
    {0x040, kWall}, {0x041, kWall}, {0x062, kWall}, {0x063, kWall},                                  // Mart front (062 = door, shared with the Center)
    {0x2B0, kRoof, 0, 0, 0xFFFF, 0, 1}, {0x2B1, kRoof, 0, 0, 0xFFFF, 0, 1}, {0x2B2, kRoof, 0, 0, 0xFFFF, 0, 1},   // Pokémon Center roof cap:
    {0x2B3, kRoof, 0, 0, 0xFFFF, 0, 1}, {0x2B4, kRoof, 0, 0, 0xFFFF, 0, 1},                          // only the top layer is the rim (measured: rows 10..15)
    {0x048, kRoof}, {0x049, kRoof}, {0x04A, kRoof}, {0x04B, kRoof}, {0x187, kRoof},                  // Pokémon Center roof, right column 187 / 18F
    {0x050, kRoof}, {0x051, kRoof}, {0x052, kRoof}, {0x053, kRoof}, {0x18F, kRoof},
    {0x058, kWall}, {0x059, kWall}, {0x05A, kWall}, {0x05B, kWall}, {0x197, kWall},                  // Pokémon Center front, right column 197 / 186 / 19F
    {0x060, kWall}, {0x061, kWall}, {0x186, kWall}, {0x19F, kWall},
    {0x280, kRoof, 0, 0, 0xFFFF, 0, 1}, {0x281, kRoof, 0, 0, 0xFFFF, 0, 1}, {0x282, kRoof, 0, 0, 0xFFFF, 0, 1},   // town house roof cap:
    {0x283, kRoof, 0, 0, 0xFFFF, 0, 1}, {0x284, kRoof, 0, 0, 0xFFFF, 0, 1},                          // bottom layer is grass, the top layer is the ridge
    {0x285, kStand, 0, 1}, {0x286, kStand, 0, 1}, {0x287, kStand, 0, 1}, {0x28D, kStand, 0, 1},     // bushes: grounded foliage, not roof
    {0x2A8, kStand, kFence, 1}, {0x2A9, kStand, kFence, 1}, {0x2AA, kStand, kFence, 1},             // low white / green fences
    {0x2AB, kStand, kFence, 1}, {0x2AC, kStand, kFence, 1},
};
constexpr std::uint16_t kViridianGround[] = {0x001};
// Indoor: the player's house (Building + GenericBuilding1).
constexpr TileModel kBuildingModels[] = {
    {0x02E, kStand}, {0x02F, kStand}, {0x036, kStand}, {0x037, kStand}, {0x03E, kStand}, {0x03F, kStand},   // cabinet
    {0x031, kStand}, {0x032, kStand}, {0x039, kStand}, {0x03A, kStand},                                     // sink
    {0x02D, kStand}, {0x035, kStand}, {0x03D, kStand},                                                      // fridge / TV
    {0x02B, kStand}, {0x02C, kStand}, {0x033, kStand}, {0x034, kStand}, {0x03B, kStand}, {0x03C, kStand},   // bookshelf
    {0x030, kStand}, {0x038, kStand}, {0x086, kStand}, {0x05A, kStand},                                     // chair, desk
    {0x04B, kStand}, {0x04E, kStand},                                                                       // dining chairs
    {0x057, kStand}, {0x05F, kStand}, {0x047, kStand}, {0x04F, kStand},                                     // potted plants
    {0x04C, kRoof}, {0x04D, kRoof}, {0x054, kWall, 0, 0, 0xFFFF, 0, 0, 0, 9}, {0x055, kWall, 0, 0, 0xFFFF, 0, 0, 0, 9},   // table (measured rows): 0..6 cloth = top plane, 7..11 rim and 12..15 legs = the front face (9 rows)
};
constexpr TileModel kGenericBuilding1Models[] = {
    {0x287, kStand}, {0x28F, kStand}, {0x297, kStand},                                                      // PC
    {0x286, kStand, 0, 0, 0xFFFF, 13}, {0x28E, kStand}, {0x296, kStand, 0, 1},                                                      // TV: 286 (wood floor above its thin top, rows 13..15) / 035 / 28E; 296 is a SEPARATE small console (measured: top art rows 0..6 only, floor 2018 below), its own object
    {0x283, kRoof}, {0x284, kRoof}, {0x285, kRoof}, {0x28B, kRoof}, {0x28C, kRoof}, {0x28D, kRoof},         // bed
    {0x293, kWall, 0, 0, 0xFFFF, 0, 0, 0, 5, 2}, {0x294, kWall, 0, 0, 0xFFFF, 0, 0, 0, 5, 2}, {0x295, kWall, 0, 0, 0xFFFF, 0, 0, 0, 5, 2},   // bed (measured 294: blanket 0..8, grey fold 9..10 = top plane; wood foot 11..13 = the face; 14..15 baked shadow stays floor)
};
// Pokémon Center interior (Building primary + PokemonCenter secondary), measured
// from PokemonCenter_1F_Layout (15x10): the round pale-blue table is a 2x2 box
// (top 29C 29D, front 2A4 2A5: its top layer only, the bottom layer is the native
// floor), the stools 2F5 (yellow) and 2FD (blue) are independent low seats (kSeat).
// The nurse counter front strip 2BC / 298 (world y10, x12..16) is a 6-pixel-high block whose top is art rows 0..9
// (top layer; 2BC's bottom layer is EMPTY, so the plain floor 281 is the ground under it) and whose front is rows 10..15.
// Corners 2B9 / 2BD: counter block from their bottom layer + the flower pot (top layer rows 0..4) upright on top of it. 290 / 363 (the nurse's
// cells): floor + a raised 4-row strip continuing the counter top. Far ends 2B1 / 2B5: the counter top (bottom layer) at height 6 + the plant
// (top rows 6..15). Machine 2B2 / 2B3 (+ 2AA / 2AB above): one 28-row upright plane, no lid.
// NOT modelled on purpose: the floor 281 and its faded motif 289-28B / 291-293 /
// 299-29B (floor decoration, stays flat), the nurse counter 2B1-2B9 / 290 / 298 /
// 2BC / 2BD / 363 (the nurse stands inside that footprint: a box would hide her),
// and the stairs 2D0-2E1 (no verified safe model).
constexpr TileModel kPokemonCenterModels[] = {
    {0x29C, kRoof, 0, 0, 0xFFFF, 0, 1}, {0x29D, kRoof, 0, 0, 0xFFFF, 0, 1},                          // table top
    {0x2A4, kWall, 0, 0, 0xFFFF, 0, 1}, {0x2A5, kWall, 0, 0, 0xFFFF, 0, 1},                          // table front (grey base at the bottom)
    {0x2F5, kSeat}, {0x2FD, kSeat},                                                                   // stools: low horizontal seats
    {0x2BC, kSeat, 6, 0, 0x281}, {0x298, kSeat, 6, 0, 0x281},                                        // nurse counter front: 6 art rows of face (10..15), red top 0..9; floor 281 under it
    // corners: the BOTTOM layer is the whole counter tile (art = 2), the top layer rows 0..4 are the flower pot (b = 5 rows,
    // an upright billboard standing on the counter top); top rows 8..15 repeat the bottom art and are not used
    {0x2B9, kSeat, 6, 5, 0x281, 0, 2}, {0x2BD, kSeat, 6, 5, 0x281, 0, 2},
    // behind the nurse's counter: floor (bottom layer) + a 4-row top-layer strip (rows 12..15) that continues the counter top
    {0x290, kLedge, 6, 12}, {0x363, kLedge, 6, 12},
    // healing machine row (measured): top layer rows 0..7 + bottom layer rows 8..11 = the machine (12 rows, no bottom layer above row 8),
    // top rows 12..15 = the counter strip (same as 290 / 363). 2B2 / 2B3 = machine (the terminal 2B4 + 2AC is the same construction with 20 rows). Floor 281 under them.
    {0x2B2, kLedge, 6, 12, 0x281, 12}, {0x2B3, kLedge, 6, 12, 0x281, 12},
    {0x2B4, kLedge, 6, 12, 0x281, 12},            // the terminal beside it: top rows 0..7 + bottom rows 8..11 (white housing, blue section), ledge rows 12..15
    // upper body in the back-wall row: its TOP layer joins the machine plane, its bottom layer stays the wall. b = first top-layer row:
    // 2AA / 2AB (machine) 0 = all 16 rows; 2AC (terminal) 8 = only rows 8..15 (rows 0..7 of its top layer are empty)
    {0x2AA, kMachineTop}, {0x2AB, kMachineTop}, {0x2AC, kMachineTop, 0, 8},
    // far ends of the counter: bottom layer = full counter top (red), top layer rows 6..15 = a plant standing on it
    {0x2B1, kCounterTop, 6, 6, 0x281}, {0x2B5, kCounterTop, 6, 6, 0x281},
};
// Viridian Forest (General primary + secondary 082D4DC4). MEASURED (native crops, id grids): ONLY the isolated round tree, 3 wide x 4 tall:
//        .   281  .          the rounded tip (walkable, top layer 82 px) over the centre column
//       288 289 28A          crown (blocking; 288 / 28A top 174 px, rounded corners)
//       28E 2A1 28F
//       2A3 2A4 2A5          trunk row (top layer opaque 256 incl. grass and a baked ground-shadow oval: not all foreground)
// The tree's art is the keyed art of each cell (both layers, the known ground colours removed: the grass, the dark shadow oval and the teal floor border are
// ground-coloured and vanish; measured: 289 / 2A1 lose nothing, 2A3 / 2A5 lose their lower three rows, so leaf / trunk colours are not shared with the ground).
// Exact pattern only. NOT measured, therefore NOT modelled (stays flat): the dense boundary (3 x 2 repeating blocks 292 290 291 / 29A 298 299 under a
// 28A 288 299 top row) and the gate roof (04x, a large flat grey roof by design).
constexpr TileModel kForestModels[] = {
    {0x281, kMulti}, {0x288, kMulti}, {0x289, kMulti}, {0x28A, kMulti}, {0x28E, kMulti}, {0x2A1, kMulti}, {0x28F, kMulti}, {0x2A3, kMulti}, {0x2A4, kMulti}, {0x2A5, kMulti},
};
constexpr std::uint16_t kWild = 0xFFFF;   // a pattern cell that is not part of the object (stays transparent / flat)
// A multi-tile object: w x h cells with exactly these metatile ids (row-major; kWild = not part of it). Drawn as one billboard.
struct MultiDef { std::uint8_t w, h; std::uint16_t ids[12]; };
constexpr MultiDef kForestMulti[] = {{3, 4, {kWild, 0x281, kWild, 0x288, 0x289, 0x28A, 0x28E, 0x2A1, 0x28F, 0x2A3, 0x2A4, 0x2A5}}};
struct MultiHit { int x, y, def; };
// Exact matching on a snapshot (ids < 0 = no cell), row-major, cells claimed once: a hit needs every non-wild cell of the pattern, so a block cut by the
// snapshot edge, a missing cell or another object's cells is never a hit. Returns the number of hits written to `out` (<= max).
int find_multi(const int* ids, int mw, int mh, const MultiDef* defs, int nd, MultiHit* out, int max) {
    std::vector<unsigned char> used(static_cast<std::size_t>(mw) * mh, 0);
    int n = 0;
    for (int y = 0; y < mh; ++y) for (int x = 0; x < mw; ++x)
        for (int d = 0; d < nd && n < max; ++d) {
            const MultiDef& m = defs[d];
            if (x + m.w > mw || y + m.h > mh) continue;
            bool ok = true;
            for (int dy = 0; dy < m.h && ok; ++dy) for (int dx = 0; dx < m.w && ok; ++dx) {
                if (m.ids[dy * m.w + dx] == kWild) continue;
                const std::size_t i = static_cast<std::size_t>(y + dy) * mw + x + dx;
                ok = !used[i] && ids[i] == m.ids[dy * m.w + dx];
            }
            if (!ok) continue;
            for (int dy = 0; dy < m.h; ++dy) for (int dx = 0; dx < m.w; ++dx)
                if (m.ids[dy * m.w + dx] != kWild) used[static_cast<std::size_t>(y + dy) * mw + x + dx] = 1;
            out[n++] = {x, y, d};
        }
    return n;
}
// The forest tree's trunk row (2A3 / 2A4 / 2A5) is a mixed tile: its art is opaque down to row 12..13 because the baked ground-shadow OVAL is painted into it.
// Measured (keyed rows, native): the side cells 2A3 / 2A5 hold only that oval (rows 0..6 14-15 px wide, then narrowing to 4 px at row 12 = the lower half of an
// ellipse; nothing else), the centre cell 2A4 holds the trunk (bark + outline) in rows 0..4 inside the oval's dark green 004129 and the oval below (rows 5..13,
// bands 105A29 / 207329). The oval stays a horizontal shadow (the renderer draws its own), so: the side cells contribute nothing, the centre keeps rows 0..4
// without the dark oval green and nothing below. No colour is keyed anywhere else (the canopy greens are untouched).
constexpr std::uint32_t kTreeOvalDark = 0xFF004129u;
constexpr int kTreeFootRow = 5;
void mask_tree_foot(const std::uint32_t* art, bool centre, std::uint32_t* out) {
    for (int y = 0; y < 16; ++y) for (int x = 0; x < 16; ++x) {
        const std::uint32_t q = art[y * 16 + x];
        out[y * 16 + x] = !centre || y >= kTreeFootRow || q == kTreeOvalDark ? 0u : q;
    }
}
// Empty rows at the bottom of a w x h image (the masked tree's last trunk row is far above its cell bottom: without this the object would float).
int trailing_empty_rows_w(const std::uint32_t* px, int w, int h) {
    int n = 0;
    for (int y = h - 1; y >= 0; --y) {
        bool any = false;
        for (int x = 0; x < w && !any; ++x) any = (px[y * w + x] >> 24) != 0;
        if (any) break;
        ++n;
    }
    return n;
}
// ── Viridian Forest dense canopy (default ON for General + Viridian Forest only; FRLG_3D_CANOPY=0 = the flat boundary, diagnostic fallback) ──────────────────────────────────
// Measured grid (native, world x24..43 y69..73): a top rim `28A 288 299` (one row), then 3 x 2 blocks `292 290 291 / 29A 298 299` repeating every two rows and
// every three columns, down to the map bottom; one continuous overlapping foliage mass, NOT isolated trees. Measured art (keyed): foliage greens only; 290 / 292
// open their left / right lower edge (ground showing, 13-15 px per row), 298 / 29A cols 0..1, 288 / 28A the round corner of the rim, 291 / 299 fully opaque.
// Decided per CELL from its own id and its 4-neighbours (never from the extent of a connected component), so it cannot change while the camera moves:
//   canopy cell = a body id (290 291 292 298 299 29A), or a rim id (288 28A 299) with a body cell directly below it; cells already claimed by an isolated
//   tree (kMulti hit) are never canopy; a cell outside the snapshot counts as canopy (no edge at the window border), an invalid cell as not canopy.
// Surface (first version, 78347ae, FAILED natively: flat slab at H with single-colour walls = tall pillars and comb holes). Now a HEIGHT FIELD with no walls:
//   the surface height is 0 on every vertex touching a non-canopy cell (rim, exposed edges, the gate roof's side, the footprint's corners) and rises smoothly
//   (smoothstep over `ramp` cells) to H inside the mass, so the edges are rounded slopes made of the foliage art itself, the gate roof / path / floor stay at
//   ground level and nothing stands over them, and a single canopy cell stays flat. Each cell is one quad with its four vertex heights; every vertex keeps
//   Y - Z = ytop + row (Y = ytop + v + h), so the reference view is unchanged. The keyed gaps stay transparent texels of the surface (ground shows through,
//   no vertical hole seams: the surface is continuous, only its texture has gaps).
constexpr std::uint16_t kCanopyBody[] = {0x290, 0x291, 0x292, 0x298, 0x299, 0x29A, 0x2A2};   // 2A2 = the side end of the mass (replaces 292, measured at x32)
constexpr std::uint16_t kCanopyRim[] = {0x28A, 0x288, 0x299};
constexpr float kCanopyHeight = 16.0f;   // native verified at 16 (FRLG_CANOPY_H overrides it for trials)
constexpr int kCanopyRamp = 2;           // slope length in vertices (native verified at 2); FRLG_CANOPY_R (test only) is clamped to 1..kCanopyMaxRamp
constexpr int kCanopyMaxRamp = 3;        // keeps the ramp's reach inside the 4-cell Forest snapshot margin (reach = ramp + 1 cells)
int canopy_clamp_ramp(int r) { return std::min(std::max(r, 1), kCanopyMaxRamp); }
bool canopy_body_id(int id) { return id >= 0 && std::find(kCanopyBody, kCanopyBody + 7, static_cast<std::uint16_t>(id)) != kCanopyBody + 7; }
bool canopy_rim_id(int id) { return id >= 0 && std::find(kCanopyRim, kCanopyRim + 3, static_cast<std::uint16_t>(id)) != kCanopyRim + 3; }
// Roles: 0 none, 1 canopy surface, 2 trunk row of a terminal front (stands on the ground, the baked oval stays flat).
// Terminal front of the mass (measured, world y60/y61): `290 2A1 292` over the trunk row `2A3 2A4 2A5` (the same trunk row as the isolated tree; the side end of the
// mass uses 2A2 for 292). The lower-leaf centre 2A1 is canopy only between two canopy cells with the trunk 2A4 below it, so the isolated tree (28E 2A1 28F) is never
// touched; a trunk-row cell is a trunk only when the cell above it is canopy.
int canopy_cells(const int* ids, const unsigned char* claimed, int w, int h, unsigned char* role) {
    auto at = [&](int x, int y) { return ids[static_cast<std::size_t>(y) * w + x]; };
    auto taken = [&](int x, int y) { return claimed && claimed[static_cast<std::size_t>(y) * w + x]; };
    auto idx = [&](int x, int y) { return static_cast<std::size_t>(y) * w + x; };
    int n = 0;
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
        bool c = false;
        if (!taken(x, y)) {
            if (canopy_body_id(at(x, y))) c = true;
            else if (canopy_rim_id(at(x, y)) && y + 1 < h && canopy_body_id(at(x, y + 1)) && !taken(x, y + 1)) c = true;
        }
        role[idx(x, y)] = c ? 1 : 0;
    }
    for (int y = 0; y < h; ++y) for (int x = 1; x + 1 < w; ++x)   // the lower-leaf centre of a terminal front
        if (!taken(x, y) && at(x, y) == 0x2A1 && role[idx(x - 1, y)] == 1 && role[idx(x + 1, y)] == 1 && y + 1 < h && at(x, y + 1) == 0x2A4 && !taken(x, y + 1))
            role[idx(x, y)] = 3;   // provisional marker: becomes canopy below (kept apart so neighbours are judged on the first pass only)
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x)
        if (role[idx(x, y)] == 3) role[idx(x, y)] = 1;
    for (int y = 1; y < h; ++y) for (int x = 0; x < w; ++x) {
        const int id = at(x, y);
        if ((id == 0x2A3 || id == 0x2A4 || id == 0x2A5) && !taken(x, y) && role[idx(x, y - 1)] == 1) role[idx(x, y)] = 2;
    }
    for (int i = 0; i < w * h; ++i) n += role[i] == 1 ? 1 : 0;
    return n;
}
// ── Viridian Forest gate roof (default ON for General + Viridian Forest only; FRLG_3D_GATE_ROOF=0 = the flat roof, diagnostic fallback) ──────────────────────────────────────────────────
// Measured (native ROM map, world x30..41, y65..75, Forest layout 082FE46C = 54 x 69 + the 7-cell border): the roof is 11 columns (x31..41) x 6 rows (y70..y75, the map
// bottom): `044 045 x9 046` then five rows of `04C 04D x9 04E`, canopy columns (292 / 29A) on both sides, the entry path above it (y69: 294 295 179 29C 29D, warps at
// x35..37 = native 29 / 28 / 30, behaviour 065). There is no southern facade in this map. Measured art (keyed): one flat grey roof (A4948B), 044 / 045 / 046 row 0 a dark
// outline (6A6262) and row 1 a light bevel (BDB4AC); the lip over the entry (294 / 295 / 29C / 29D, top layer rows 12..15) is NOT part of this structure and stays flat.
// Structure: the same wall-less height field as the canopy: every vertex touching a non-roof cell (the entry path, the canopy columns, the cells beyond the map
// bottom) is at ground level, the roof rises (smoothstep over `ramp` vertices) to H inside, so the footprint, the entry path and its visibility are untouched, the
// exposed edges are closed continuous slopes made of the roof art, and no deep building is invented beyond the map. Decided per cell from the id alone.
constexpr std::uint16_t kGateRoofIds[] = {0x044, 0x045, 0x046, 0x04C, 0x04D, 0x04E};
constexpr float kGateRoofHeight = 16.0f;   // native verified at 16 (FRLG_GATE_H for trials)
constexpr int kGateRoofRamp = 2;           // native verified at 2 (FRLG_GATE_R for trials, clamped to 1..3 like the canopy)
int gate_roof_cells(const int* ids, const unsigned char* claimed, int w, int h, unsigned char* role) {
    int n = 0;
    for (int i = 0; i < w * h; ++i)
        if (!(claimed && claimed[i]) && ids[i] >= 0 && std::find(kGateRoofIds, kGateRoofIds + 6, static_cast<std::uint16_t>(ids[i])) != kGateRoofIds + 6) { role[i] = 4; ++n; }
    return n;
}
// The trunk cell's ground: both layers with the bark of rows 0..4 painted over by the oval's own dark green, so the baked oval stays one flat ground shadow
// under the standing trunk (no flat bark duplicating the standing one).
void trunk_ground_art(const std::uint32_t* both, std::uint32_t* out) {
    std::copy(both, both + 256, out);
    for (int y = 0; y < kTreeFootRow; ++y) for (int x = 0; x < 16; ++x) out[y * 16 + x] = kTreeOvalDark;
}
// Heights of the (w + 1) x (h + 1) vertices of a w x h cell grid (vertex (i, j) is the top-left corner of cell (i, j)). A vertex is a boundary (height 0) when any
// of its in-window cells is not canopy; every other vertex rises with its 8-neighbour distance s (in vertices) from the nearest boundary vertex:
// height = H * smoothstep(min(1, s / ramp)). Cells outside the window are ignored, so the heights depend only on boundaries within `ramp` vertices.
void canopy_vertex_heights(const unsigned char* canopy, int w, int h, float H, int ramp, float* out, int raised = 1) {
    const int vw = w + 1, vh = h + 1;
    std::vector<int> dist(static_cast<std::size_t>(vw) * vh, 1 << 20);
    std::vector<int> queue;
    auto is_c = [&](int x, int y) { return canopy[static_cast<std::size_t>(y) * w + x] == raised; };
    for (int j = 0; j < vh; ++j) for (int i = 0; i < vw; ++i) {
        bool boundary = false;
        for (int dy = -1; dy <= 0; ++dy) for (int dx = -1; dx <= 0; ++dx) {
            const int cx = i + dx, cy = j + dy;
            if (cx >= 0 && cy >= 0 && cx < w && cy < h && !is_c(cx, cy)) boundary = true;
        }
        if (boundary) { dist[static_cast<std::size_t>(j) * vw + i] = 0; queue.push_back(j * vw + i); }
    }
    for (std::size_t q = 0; q < queue.size(); ++q) {
        const int i = queue[q] % vw, j = queue[q] / vw, d = dist[queue[q]];
        if (d >= ramp) continue;
        for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) {
            const int ni = i + dx, nj = j + dy;
            if (ni < 0 || nj < 0 || ni >= vw || nj >= vh || dist[static_cast<std::size_t>(nj) * vw + ni] <= d + 1) continue;
            dist[static_cast<std::size_t>(nj) * vw + ni] = d + 1;
            queue.push_back(nj * vw + ni);
        }
    }
    for (std::size_t k = 0; k < dist.size(); ++k) {
        const float t = ramp <= 0 ? 1.0f : std::min(1.0f, static_cast<float>(dist[k]) / static_cast<float>(ramp));
        out[k] = H * t * t * (3.0f - 2.0f * t);
    }
}
// One canopy cell as a quad (corners top-left, top-right, bottom-right, bottom-left) from its four vertex heights: Y = ytop + v + h, so Y - Z = ytop + v.
void canopy_cell_quad(float X0, float ytop, float h00, float h10, float h11, float h01, float p[4][3], float uv[4][2]) {
    const float q[4][3] = {{X0, ytop + h00, h00}, {X0 + 16, ytop + h10, h10}, {X0 + 16, ytop + 16.0f + h11, h11}, {X0, ytop + 16.0f + h01, h01}};
    const float t[4][2] = {{0, 0}, {16, 0}, {16, 16}, {0, 16}};
    std::copy(&q[0][0], &q[0][0] + 12, &p[0][0]);
    std::copy(&t[0][0], &t[0][0] + 8, &uv[0][0]);
}
// Multi-tile models only exist for these tileset pairs: the extra snapshot margin is paid only there (no extra work, no changed boundary inference elsewhere).
bool uses_multi(std::uint32_t primary, std::uint32_t secondary) { return primary == kTilesetGeneral && secondary == kTilesetViridianForest; }
// A multi-tile object that touches the visible cells must lie wholly inside the snapshot, or it would flip between upright and flat as the camera
// moves: the snapshot window is grown by this many cells on every side (the largest object is 4 cells tall, so 3 would do; 4 keeps a spare cell).
constexpr int kMultiMargin = 4;
void expand_cell_window(int* x0, int* y0, int* x1, int* y1, int margin) { *x0 -= margin; *y0 -= margin; *x1 += margin; *y1 += margin; }
// Route 2 / Forest south gate interior (Building primary + GenericBuilding2 secondary, layout 08320990, 15 x 12), MEASURED (native ids, TOP-layer rows):
//   plants: 2EA (y1, in the lower back-wall row) over the pot 2F1 (y2), and the double plants 2E9 / 2E8 over 2F1 / 2F0: the plant is the TOP layers only
//   (2EA foliage rows 4..15, a bush 2 px wide at row 4 widening to the full 16 at rows 11..12; 2F1 / 2F0 the pot, rows 0..15, ending on row 15), the bottom layers are
//   the authored wall (2EA, identical to the plain wall 28F) and floor (2F1): each background stays its own surface, the plant stands on the floor in front of the wall.
//   chairs 2E6 (left, top layer 149 px) / 2E7 (right): independent standing objects (b = 1: the stacked 2E6 at x10 y4 / y5 are two chairs).
//   table 2D6 2D7 (top row) over 2DE 2DF, TOP layer rows: 2D6 / 2D7 row 0 empty, row 1 the grey outline, rows 2..15 the cloth (rounded corners: cols 5..15 at row 1 widening
//   to 1..15 at row 5, mirrored on 2D7); 2DE / 2DF rows 0..9 cloth (the last two rows white), rows 10..13 the front (grey band 10..11, brown rim 12, grey 13), row 14 only the
//   leg tips (2DE cols 2..4, 2DF cols 10..13), row 15 empty. So: top plane = everything above row 10 of the lower row; front face = rows 10..15 of 2DE / 2DF with the
//   empty last row skipped (face 6, skip 1: it stands on row 14, 5 units high). The authored floor beneath is the bottom layer (floor entries) and stays untouched; art = TOP
//   layer only (art 1) because the 7B7B7B outline grey is also a ground colour and a keyed art would punch holes in it.
//   NOT modelled yet: the rug 29A-2C7, the back wall doors / windows, the exit mat 2CD-2CF.
constexpr TileModel kGenericBuilding2Models[] = {
    {0x2EA, kStand}, {0x2F1, kStand}, {0x2E9, kStand}, {0x2E8, kStand}, {0x2F0, kStand},     // plants and pots
    {0x2E6, kStand, 0, 1}, {0x2E7, kStand, 0, 1},                                               // chairs
    {0x2D6, kRoof, 0, 0, 0xFFFF, 0, 1}, {0x2D7, kRoof, 0, 0, 0xFFFF, 0, 1},                      // table top (TOP layer only)
    {0x2DE, kWall, 0, 0, 0xFFFF, 0, 1, 0, 6, 1}, {0x2DF, kWall, 0, 0, 0xFFFF, 0, 1, 0, 6, 1},   // table front: rows 10..14 stand, row 15 is empty
};
constexpr std::uint16_t kPokemonCenterGround[] = {0x281};   // plain yellow floor: keyed out of stand art without a top layer
// Plain ground (floor, rugs) of these tilesets: its colours are keyed out of
// standing art that has no separate top layer.
constexpr std::uint16_t kGeneralGround[] = {0x001, 0x008, 0x009, 0x010, 0x011, 0x0DC};
// Plain outdoor grass of the General tileset: what sits under a building's footprint (default 0x001).
constexpr std::uint16_t kOutdoorGrass[] = {0x001, 0x010, 0x011};
constexpr std::uint16_t kPalletGround[] = {0x296, 0x28E, 0x29E};
constexpr std::uint16_t kBuildingGround[] = {0x001, 0x009, 0x043, 0x044, 0x045, 0x046, 0x053, 0x056, 0x05B, 0x05C, 0x05E};

// The Pewter fence models exist for General + Pewter City only (where they were measured).
inline bool pewter_fence_applies(std::uint32_t primary, std::uint32_t secondary) { return primary == kTilesetGeneral && secondary == kTilesetPewterCity; }
struct Models {
    std::uint32_t primary = 0, secondary = 0;
    std::array<TileModel, 1024> at{};
    std::vector<std::uint16_t> ground;   // plain ground ids for colour keying
    bool known = false;                  // at least one table applies
    template <std::size_t N, std::size_t G>
    void add(const TileModel (&t)[N], const std::uint16_t (&g)[G]) {
        for (const auto& e : t) at[e.id] = e;
        ground.insert(ground.end(), g, g + G);
        known = true;
    }
    void add_pewter_fence() { for (const auto& e : kPewterFenceModels) at[e.id] = e; known = true; }
    void load(std::uint32_t p, std::uint32_t s) {
        primary = p; secondary = s; known = false;
        ground.clear();
        for (int i = 0; i < 1024; ++i) at[i] = {static_cast<std::uint16_t>(i), kFlat};
        if (p == kTilesetGeneral) add(kGeneralModels, kGeneralGround);
        if (pewter_fence_applies(p, s)) {   // TEST ONLY, off by default
            const char* e = std::getenv("FRLG_3D_PEWTER_FENCE");
            if (e && e[0] != '0') add_pewter_fence();
        }
        if (s == kTilesetPalletTown) add(kPalletModels, kPalletGround);
        if (s == kTilesetViridianCity) add(kViridianModels, kViridianGround);
        if (p == kTilesetBuilding) add(kBuildingModels, kBuildingGround);
        if (s == kTilesetGenericBuilding1) add(kGenericBuilding1Models, kBuildingGround);
        if (p == kTilesetBuilding && s == kTilesetGenericBuilding2) {   // the pair only; FRLG_3D_GATE_ROOM=0 is the diagnostic fallback (every tile flat, as before)
            const char* e = std::getenv("FRLG_3D_GATE_ROOM");
            if (!(e && e[0] == '0')) add(kGenericBuilding2Models, kBuildingGround);
        }
        if (p == kTilesetBuilding && s == kTilesetPokemonCenter) add(kPokemonCenterModels, kPokemonCenterGround);   // the pair, never the secondary alone
        if (p == kTilesetGeneral && s == kTilesetViridianForest) add(kForestModels, kViridianGround);
        // FRLG_FACE_ROWS=<hex id>=<rows>,... (test only): override the front-face height of mixed wall metatiles (054=5,293=6) to try boundaries natively
        if (const char* fr = std::getenv("FRLG_FACE_ROWS"))
            for (const char* q = fr; *q;) {
                char* end = nullptr;
                const long id = std::strtol(q, &end, 16);
                if (end == q || *end != '=') { ++q; continue; }
                const long rows = std::strtol(end + 1, &end, 10);
                if (id >= 0 && id < 1024 && at[id].kind == kWall && rows >= 0 && rows < 16) at[id].face = static_cast<std::uint8_t>(rows);
                q = end;
            }
    }
} g_models;

// Roof slope: walls stand upright, the roof rows above lean back so every
// art row keeps Y - Z constant (the reference view still sees the picture).
// lean 0.75 for house roofs; 1 = a flat top (furniture).
void wall_surface(float base, float wall_h, float lean, float u, float* Y, float* Z) {
    *Z = u <= wall_h ? u : wall_h + (u - wall_h) * (1.0f - lean);
    *Y = base - u + *Z;
}

// Fence orientation from which of the four neighbours are fence cells: a
// piece with fence only above/below is a north-south run (a side panel); any
// east/west neighbour, or none, keeps the usual front-facing strip.
enum FenceDir : std::uint8_t { kFenceFront, kFenceNS };
FenceDir fence_orient(bool up, bool down, bool left, bool right) {
    return (up || down) && !left && !right ? kFenceNS : kFenceFront;
}
// Orientation of one fence piece: the brown Pewter ids carry a fixed orientation, the white fence ids follow their neighbours.
bool fence_piece_is_ns(const TileModel& m, bool up, bool down, bool left, bool right) {
    if (m.a == kFenceBrownNS) return true;
    if (m.a == kFenceBrownFront) return false;
    return fence_orient(up, down, left, right) == kFenceNS;
}
// Foot split of a stand piece whose last `skip` art rows are a baked ground shadow: `upright` keeps the rows above them (the object, which stands on the row above
// the shadow), `shadow` only those rows (drawn flat at their own position, a pure ground decal). The two never share a pixel and together are the original art.
void fence_foot_split(const std::uint32_t* art, int skip, std::uint32_t* upright, std::uint32_t* shadow) {
    const int first = skip > 0 && skip < 16 ? 16 - skip : 16;
    for (int i = 0; i < 256; ++i) {
        const bool foot = i / 16 >= first;
        upright[i] = foot ? 0u : art[i];
        shadow[i] = foot ? art[i] : 0u;
    }
}
// Opaque column range of a 16x16 fence art (inclusive). A N-S panel maps
// these columns onto its height so the panel stands on the ground instead of
// floating with the art's transparent padding.
bool fence_columns(const std::uint32_t* px, int* c0, int* c1) {
    *c0 = 16; *c1 = -1;
    for (int y = 0; y < 16; ++y) for (int x = 0; x < 16; ++x)
        if (px[y * 16 + x] >> 24) { *c0 = std::min(*c0, x); *c1 = std::max(*c1, x); }
    return *c1 >= 0;
}
// Side panel corners (back-top, front-top, front-bottom, back-bottom) at X for
// a cell spanning Y0..Y0+16, and the matching texture corners. The art is
// turned a quarter turn: art rows run along Y, art columns become height.
void fence_panel(float X, float Y0, int c0, int c1, float h, float p[4][3], float uv[4][2]) {
    const float ut = static_cast<float>(c0), ub = static_cast<float>(c1 + 1);
    const float pp[4][3] = {{X, Y0, h}, {X, Y0 + 16, h}, {X, Y0 + 16, 0}, {X, Y0, 0}};
    const float tt[4][2] = {{ut, 0}, {ut, 16}, {ub, 16}, {ub, 0}};
    std::copy(&pp[0][0], &pp[0][0] + 12, &p[0][0]);
    std::copy(&tt[0][0], &tt[0][0] + 8, &uv[0][0]);
}

// Route 1 gate: the N-S fence beside each pine is part of the tree cells' own
// art (measured from the ROM): the left stripe is the right four columns of
// the left tree's right half (tip 0x2AB, upper 0x2BA), the right stripe the
// left four columns of the right tree's left half (tip 0x2AA, upper 0x2B2).
// The tree model replaces these cells, so the stripe is redrawn as a panel
// that joins the 0x0ED / 0x0EC corner pieces in the row above.
struct GateStripe { std::uint16_t id; std::uint8_t c0, c1; };
constexpr GateStripe kGateStripes[] = {{0x2AB, 12, 15}, {0x2BA, 12, 15}, {0x2AA, 0, 3}, {0x2B2, 0, 3}};
const GateStripe* gate_stripe(std::uint16_t id) {
    for (const GateStripe& g : kGateStripes) if (g.id == id) return &g;
    return nullptr;
}
// The gate fence lives in the BOTTOM layer (the top layer there is pine
// foliage). Copy only the stripe columns of that layer into out (16x16, rest
// transparent), dropping empty pixels, the backdrop fill and the plain ground
// colours, so the native fence colours survive and floor/foliage do not.
void isolate_stripe(const std::uint32_t* bottom, int c0, int c1, const std::uint32_t* keys, std::size_t nkeys,
                    std::uint32_t backdrop, std::uint32_t* out) {
    std::fill(out, out + 256, 0u);
    for (int y = 0; y < 16; ++y) for (int x = c0; x <= c1; ++x) {
        const std::uint32_t c = bottom[y * 16 + x];
        if (!(c >> 24) || c == backdrop || std::find(keys, keys + nkeys, c) != keys + nkeys) continue;
        out[y * 16 + x] = c;
    }
}
// ── building / furniture sides ───────────────────────────────────────────
// A side face is the front art extruded along the depth: one pixel column of
// the edge cell's own art, stretched across the object. A column close to the
// edge is used (the outline column itself is near-black and made the old
// stretched sides look like dark slabs), but it must be mostly opaque so
// transparent padding and corner cut-outs are never sampled.
int column_top(const std::uint32_t* art, int col) {   // first opaque row, 16 if none
    for (int y = 0; y < 16; ++y) if (art[y * 16 + col] >> 24) return y;
    return 16;
}
int column_count(const std::uint32_t* art, int col) {
    int n = 0;
    for (int y = 0; y < 16; ++y) n += (art[y * 16 + col] >> 24) ? 1 : 0;
    return n;
}
// Column order tried for the left (low X) or right edge of a cell.
bool pick_side_column(const std::uint32_t* art, bool left, int* col) {
    static const int kOrder[] = {3, 2, 4, 1, 5, 6};
    int best = -1, best_n = 0;
    for (int k : kOrder) {
        const int c = left ? k : 15 - k;
        const int n = column_count(art, c);
        if (n >= 12) { *col = c; return true; }
        if (n > best_n) { best = c; best_n = n; }
    }
    if (best < 0) return false;
    *col = best;
    return true;
}
// Height of a side segment of one cell: its art column's opaque extent, so a
// tile that is mostly empty above a plant or a TV gets a short side, not a
// tile-tall slab. 0 when the column is empty.
float side_segment_height(const std::uint32_t* art, int col) {
    return static_cast<float>(16 - column_top(art, col));
}
// A stand cell joins the object above it unless its table entry starts a new object.
bool continues_stand_run(const TileModel& below) { return below.kind == kStand && below.b != 1; }
// Lowest contiguous building cell of each cell's column (row index within a
// w x h grid, -1 for non-building cells), measured once bottom-up. A recessed
// door (town house 2: door one row above the trim, ground 0CE in front) ends
// its column above the box front.
void column_bottoms(const unsigned char* bld, int w, int h, int* out) {
    for (int y = h - 1; y >= 0; --y) for (int x = 0; x < w; ++x) {
        const int at = y * w + x;
        out[at] = !bld[at] ? -1 : (y + 1 < h && bld[at + w]) ? out[at + w] : y;
    }
}
// Which plane a building cell's front face is measured from. Only WALL
// faces that stop above the box front (a recessed door / trim column) use
// their own base and height; every roof cell and every other wall cell shares
// the box's base and wall height, so the roof stays one continuous plane
// whatever the wall below it does.
struct FaceParams { float base, wall_h; };
FaceParams face_params(bool wall, bool recessed, float base, float wall_h, float cbase, float cwall_h) {
    return wall && recessed ? FaceParams{cbase, cwall_h} : FaceParams{base, wall_h};
}
// Front quad of one cell (X0..X0+16, art rows u0..u0+16 above the base), corners
// top-left, top-right, bottom-right, bottom-left. Y - Z follows the art rows.
void front_quad(float base, float wall_h, float lean, float X0, float u0, float p[4][3]);
// Flat ground drawn under an outdoor roof / wall cell. The cell's own layers
// can hold the building's roof or wall pixels, and the tile in front of a
// building is often a road or path, so neither is used: the ground is plain
// grass taken from beside / behind the footprint when one is there, else the
// tileset's default grass. ids / flat: candidate cells in priority order (left
// of the footprint, right of it, behind it).
int pick_box_ground(const int* ids, const bool* flat, int n, const std::uint16_t* grass, std::size_t ng) {
    for (int i = 0; i < n; ++i)
        if (flat[i] && std::find(grass, grass + ng, ids[i]) != grass + ng) return ids[i];
    return ng > 0 ? grass[0] : -1;
}
// Art mode of a roof / wall cell's front face: the top layer alone when the
// model says its bottom layer is background (Pokémon Center roof cap).
bool box_art_top_only(const TileModel& m, bool has_top_layer) { return m.art == 1 && has_top_layer; }
// Reveal of a recessed wall face (door / trim column that stops above the box
// front): the vertical plane at X from the face's own front plane (cbase) out
// to the box front (base). It is built from one segment per 16-unit wall row
// (k = 0 at the floor), so each row can take the neighbouring wall cell of
// that row: from z0 to z1 at the front, and at the back up to the face's top
// (a rectangle for rows the face covers, a triangle above it). Corners:
// back-top, front-top, front-bottom, back-bottom.
int reveal_segments(float wall_h) { return wall_h <= 0 ? 0 : static_cast<int>((wall_h + 15.99f) / 16.0f); }
void reveal_segment(float X, float cbase, float cwall_h, float base, float wall_h, int k, float p[4][3]) {
    const float z0 = 16.0f * k, z1 = std::min(z0 + 16.0f, wall_h);
    const float zb = std::min(z1, std::max(z0, cwall_h));
    const float q[4][3] = {{X, cbase, zb}, {X, base, z1}, {X, base, z0}, {X, cbase, z0}};
    std::copy(&q[0][0], &q[0][0] + 12, &p[0][0]);
}
// Texture rows of a segment: the neighbour's art rows above its front planter
// (planter rows at the bottom are decoration of the front plane, not of the
// side), stretched over the segment's 16-unit row; u is the chosen column.
void reveal_segment_uv(const float p[4][3], int k, int planter, float col_u, float uv[4][2]) {
    const float vb = 16.0f - static_cast<float>(planter), z0 = 16.0f * k;
    for (int i = 0; i < 4; ++i) { uv[i][0] = col_u; uv[i][1] = vb * (1.0f - (p[i][2] - z0) / 16.0f); }
}
// Lintel above a recessed face: from the face's top edge to the roof front edge.
void lintel_quad(const float door[4][3], float X0, float base, float wall_h, float q[4][3]) {
    const float t[4][3] = {{door[0][0], door[0][1], door[0][2]}, {door[1][0], door[1][1], door[1][2]},
                           {X0 + 16, base, wall_h}, {X0, base, wall_h}};
    std::copy(&t[0][0], &t[0][0] + 12, &q[0][0]);
}
// ── BG0 overlay gating ───────────────────────────────────────────────────
// BG0 carries the field's text windows (dialogue, Start menu, pop-ups) and
// their frames. The overlay draws only inside the live windows' rectangles,
// each padded by one tile for the frame the game draws around a window, so
// stray BG0 content can never resurrect map art over the 3D scene.
struct BgRect { int x0, y0, x1, y1; };
inline BgRect pad_window_rect(int x, int y, int w, int h, int pad) { return {x - pad, y - pad, x + w + pad, y + h + pad}; }
// A window rectangle is usable when it has area and is not absurdly larger than a screen (a stale entry).
inline bool bg_rect_valid(const BgRect& r) {
    return r.x1 > r.x0 && r.y1 > r.y0 && (r.x1 - r.x0) <= 240 + 64 && (r.y1 - r.y0) <= 160 + 64;
}
inline bool bg_point_in_rects(const BgRect* rects, int n, int x, int y) {
    for (int i = 0; i < n; ++i)
        if (x >= rects[i].x0 && x < rects[i].x1 && y >= rects[i].y0 && y < rects[i].y1) return true;
    return false;
}
// ── 3D frame presentation ────────────────────────────────────────────────
// The framebuffer is rendered at (about) the output's own resolution so the
// picture is shown 1:1 with NO resampling (the old fixed 3x frame was stretched
// to the window with a linear filter, which softened every sprite). A pixel
// budget keeps big / fullscreen windows affordable; beyond it the existing
// Smoothing setting decides the scaling filter (ON = linear, OFF = nearest).
// The camera zoom is nudged (<= 4 %) toward a whole number of framebuffer pixels per art
// pixel at the focus; when the nearest whole number is further away (e.g. Scale 6.1) it
// is declined and the nominal fractional size stays. Perspective changes the size away
// from the focus (and with height), so this is never a guarantee for every sprite.
struct FramePlan { float rs; int fw, fh; bool linear; float zoom; };
inline float snap_zoom(float zoom, float fb_scale, float tol = 0.04f) {
    const float k = std::floor(zoom * fb_scale + 0.5f);
    if (k < 1.0f) return zoom;
    const float z = k / fb_scale;
    return std::fabs(z - zoom) / zoom <= tol ? z : zoom;
}
inline FramePlan plan_frame(float out_scale, int vw, int vh, bool smoothing, float max_scale, float budget_px, float zoom) {
    float rs = std::min(out_scale, max_scale);
    if (vw > 0 && vh > 0) rs = std::min(rs, std::sqrt(budget_px / (static_cast<float>(vw) * vh)));
    rs = std::max(1.0f, rs);
    FramePlan p;
    p.rs = rs;
    p.fw = static_cast<int>(vw * rs + 0.5f);
    p.fh = static_cast<int>(vh * rs + 0.5f);
    p.linear = smoothing;
    p.zoom = snap_zoom(zoom, rs);
    return p;
}
// True when the framebuffer maps onto the screen pixel for pixel (within a pixel): drawn at an integer origin.
inline bool one_to_one(int fw, float out_px_w) { return std::fabs(static_cast<float>(fw) - out_px_w) < 1.0f; }

// ── indoor back wall / side wall joins ───────────────────────────────────
// Plain wall tile of a back-wall row: the id used at least twice (ties: the
// smallest id, as before). A row of all-different tiles has no plain tile and
// the caller gets `fallback_id` (-1 = none): never an arbitrary smallest id
// (Pokémon Center 1F's lower row picked the door tile 062 that way) and never
// a neighbouring decoration (plants, counters, machines) repeated along a wall.
// Candidate flags for the plain wall of a back-wall row. A tile with a top layer is a decoration over the wall (a plant, a window, a banner), never the plain wall,
// even where it is the commonest tile of the row (the gate room's plants repeated along its side walls); with prefer_bare (Building + GenericBuilding2) only tiles
// without a top layer qualify, and if the row has none the model-flat flags are kept unchanged.
void plain_wall_candidates(const unsigned char* flat_model, const unsigned char* has_top, int n, bool prefer_bare, unsigned char* out) {
    int bare = 0;
    for (int i = 0; i < n; ++i) bare += flat_model[i] && !has_top[i] ? 1 : 0;
    for (int i = 0; i < n; ++i) out[i] = (prefer_bare && bare > 0) ? (flat_model[i] && !has_top[i] ? 1 : 0) : flat_model[i];
}
int pick_plain_wall_id(const int* ids, const unsigned char* flat, int n, int fallback_id) {
    if (!ids || !flat || n <= 0) return fallback_id;
    int count[1024] = {};
    for (int i = 0; i < n; ++i) if (flat[i] && ids[i] >= 0 && ids[i] < 1024) ++count[ids[i]];
    int best = 0;
    for (int id = 1; id < 1024; ++id) if (count[id] > count[best]) best = id;
    return count[best] >= 2 ? best : fallback_id;
}
// A modest flat wall built from a plain wall tile when the room has no plain tile
// for that row (lower back wall of the Pokémon Center: counters and machines only):
// the tile's own body row repeated, without its top trim or its base line.
// The body row is the most frequent identical row from row 4 down to the last
// opaque row (ties: the later one); nothing opaque -> the tile unchanged.
void flat_wall_from(const std::uint32_t* plain, std::uint32_t* out) {
    if (!plain || !out) return;
    int last = -1;
    for (int y = 0; y < 16 && last < 15; ++y) for (int x = 0; x < 16; ++x) if (plain[y * 16 + x] >> 24) { last = y; break; }
    if (last < 0) { std::copy(plain, plain + 256, out); return; }
    const int first = std::min(4, last);
    int best_row = last, best_count = 0;
    auto has_pixels = [&](int y) { for (int x = 0; x < 16; ++x) if (plain[y * 16 + x] >> 24) return true; return false; };
    for (int y = first; y <= last; ++y) {
        if (!has_pixels(y)) continue;   // a fully see-through row is never the wall body
        int count = 0;
        for (int y2 = first; y2 <= last; ++y2)
            count += std::equal(plain + y * 16, plain + y * 16 + 16, plain + y2 * 16) ? 1 : 0;
        if (count >= best_count) { best_count = count; best_row = y; }
    }
    for (int y = 0; y < 16; ++y) std::copy(plain + best_row * 16, plain + best_row * 16 + 16, out + y * 16);
}
// Cells of back-wall row `ry` inside the room range rx0..rx1 on a snapshot that is `mh` rows tall (0 when the row is outside it,
// e.g. room_top + 1 on a one-row snapshot, which the caller must not read).
int plain_row_cells(int ry, int mh, int rx0, int rx1) {
    if (ry < 0 || ry >= mh) return 0;
    const int a = std::max(0, rx0);
    return rx1 >= a ? rx1 - a + 1 : 0;
}
// ── scene classification helpers (pure; used by the draw pass and the FRLG_SCENE_AUDIT diagnostic) ──
// Any non-transparent pixel in a 16x16 tile. The audit and the generic rule use this, never has_top(): has_top only says the tile ENTRIES are nonzero
// (a top layer made of empty tiles, or a fully transparent one, is not a foreground).
bool has_opaque(const std::uint32_t* art) {
    for (int i = 0; i < 256; ++i) if (art[i] >> 24) return true;
    return false;
}
// Native metatile layer type (attribute bits 29..30): 0 normal, 1 covered, 2 split. The top layer (entries 4..7) is drawn OVER sprites
// only for normal and split metatiles; a covered metatile puts both layers under them, so its "top" art is ground, never a foreground.
constexpr int kLayerCovered = 1;
bool top_is_foreground(int layer_type, bool has_top) { return has_top && layer_type != kLayerCovered; }
// Experimental generic rule for tilesets without a model table (FRLG_3D_GENERIC=1): a blocking, non-door, non-water, non-ledge cell whose top layer
// is a real foreground is a tall object (tree crown, pillar, rock face): it stands upright instead of lying flat. Everything else stays flat.
bool generic_stand_cell(bool door, int collision, int layer_type, bool has_top, int behavior) {
    const bool water = behavior >= 0x10 && behavior <= 0x1F, ledge = behavior >= 0x38 && behavior <= 0x3F;
    return !door && collision != 0 && !water && !ledge && top_is_foreground(layer_type, has_top);
}
// Split objects: one metatile of a building or furniture piece that no table lists (kind kFlat) but whose row neighbours (or column neighbours)
// are the same box kind lies flat on the ground in the middle of a standing wall. -1 = no cell there. Returns the kind to adopt, or kFlat.
std::uint8_t adopt_box_kind(int left, int right, int up, int down) {
    auto box = [](int k) { return k == kRoof || k == kWall; };
    if (box(left) && left == right) return static_cast<std::uint8_t>(left);   // flanked in its row (a missing wall / roof tile)
    if (box(up) && up == down) return static_cast<std::uint8_t>(up);          // sandwiched in its column
    return kFlat;
}
// kinds: the effective kind of every cell (model or fallback), solid: blocking / door cells. Writes the adopted kinds into `out`
// (a copy of `kinds` first) from the unmodified input, so the result never depends on the scan order. Returns how many cells were adopted.
int adopt_orphans(const std::uint8_t* kinds, const std::uint8_t* solid, int w, int h, std::uint8_t* out) {
    std::copy(kinds, kinds + static_cast<std::size_t>(w) * h, out);
    int n = 0;
    auto at = [&](int x, int y) { return x < 0 || y < 0 || x >= w || y >= h ? -1 : static_cast<int>(kinds[y * w + x]); };
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
        if (kinds[y * w + x] != kFlat || !solid[y * w + x]) continue;
        const std::uint8_t k = adopt_box_kind(at(x - 1, y), at(x + 1, y), at(x, y - 1), at(x, y + 1));
        if (k != kFlat) { out[y * w + x] = k; ++n; }
    }
    return n;
}
// Cells whose kind differs between two snapshots of the same world (rectangle [x0,x1) x [y0,y1) in world cells). A stable scene has none
// while the camera walks; every flip is a visible pop. Cells missing from either snapshot are skipped.
int kind_flips(const std::uint8_t* prev, int px0, int py0, int pw, int ph, const std::uint8_t* cur, int cx0, int cy0, int cw, int ch,
               int x0, int y0, int x1, int y1, int* first_x = nullptr, int* first_y = nullptr) {
    int n = 0;
    for (int y = std::max({y0, py0, cy0}); y < std::min({y1, py0 + ph, cy0 + ch}); ++y)
        for (int x = std::max({x0, px0, cx0}); x < std::min({x1, px0 + pw, cx0 + cw}); ++x)
            if (prev[(y - py0) * pw + (x - px0)] != cur[(y - cy0) * cw + (x - cx0)]) {
                if (n++ == 0) { if (first_x) *first_x = x; if (first_y) *first_y = y; }
            }
    return n;
}
// Number of see-through (void) pixels of a 16x16 tile and whether they touch the left / right edge column.
struct VoidInfo { int count = 0; bool left_edge = false, right_edge = false; };
VoidInfo void_info(const std::uint32_t* art) {
    VoidInfo v;
    if (!art) return v;
    for (int y = 0; y < 16; ++y) for (int x = 0; x < 16; ++x)
        if (!(art[y * 16 + x] >> 24)) {
            ++v.count;
            v.left_edge |= x == 0;
            v.right_edge |= x == 15;
        }
    return v;
}
// A back-wall corner tile that carries the 2D view's side-wall wedge: a see-through
// triangle (about 6..50 % of the tile) reaching the outer edge column.
bool is_corner_wedge(const std::uint32_t* art, bool left_corner) {
    if (!art) return false;
    const VoidInfo v = void_info(art);
    return v.count >= 16 && v.count <= 128 && (left_corner ? v.left_edge : v.right_edge);
}
// Close the wedge's see-through gap with the plain wall behind it. Only the see-through pixels take the
// donor's pixel; every opaque pixel of the corner tile (its authentic diagonal edge, bands, trim) is kept.
void fill_void_from(const std::uint32_t* tile, const std::uint32_t* donor, std::uint32_t* out) {
    if (!tile || !out) return;
    if (!donor) { std::copy(tile, tile + 256, out); return; }   // no wall to take pixels from: the tile unchanged
    for (int i = 0; i < 256; ++i) out[i] = (tile[i] >> 24) ? tile[i] : donor[i];
}
// Opt-in pass isolation (diagnostic): FRLG_3D_LAYERS=<hex mask>, default all.
// 1 ground, 2 trees + their shadows, 4 stand / grass / back walls, 8 building boxes, 10 indoor shell, 20 sprites.
// FRLG_GROUND_MODE=bottom|top|keyed draws plain ground cells from that layer only (diagnostic).
inline unsigned parse_layer_mask(const char* e) {
    return e && *e ? static_cast<unsigned>(std::strtoul(e, nullptr, 16)) : 0xFFFFFFFFu;
}
inline unsigned layer_mask() {
    static const unsigned mask = parse_layer_mask(std::getenv("FRLG_3D_LAYERS"));
    return mask;
}
// Opt-in one-frame geometry dump: FRLG_BOX_DIAG=1 (all) or "x0,y0,x1,y1" (world cell rect).
bool box_diag_cell(int wx, int wy) {
    static const char* spec = std::getenv("FRLG_BOX_DIAG");
    static int r[4] = {-100000, -100000, 100000, 100000};
    static bool parsed = false, all = false;
    if (!spec || !*spec) return false;
    if (!parsed) {
        parsed = true;
        if (std::sscanf(spec, "%d,%d,%d,%d", &r[0], &r[1], &r[2], &r[3]) != 4) all = true;
    }
    return all || (wx >= r[0] && wx <= r[2] && wy >= r[1] && wy <= r[3]);
}
// ── low seats (Pokémon Center stools) ────────────────────────────────────
// Raised strip (kLedge): art rows first_row..15 of the tile's top layer lie flat at height H. In the reference view row r of the
// tile sits at Y - Z = Ytop + r, so on the plane z = H it is at Y = Ytop + r + H: contiguous with the front block's top face.
// Corners: back-left, back-right, front-right, front-left; uv rows first_row..16.
void ledge_quad(float X0, float Ytop, float H, int first_row, float p[4][3], float uv[4][2]) {
    const float ya = Ytop + first_row + H, yb = Ytop + 16.0f + H;
    const float q[4][3] = {{X0, ya, H}, {X0 + 16, ya, H}, {X0 + 16, yb, H}, {X0, yb, H}};
    const float t[4][2] = {{0, static_cast<float>(first_row)}, {16, static_cast<float>(first_row)}, {16, 16}, {0, 16}};
    std::copy(&q[0][0], &q[0][0] + 12, &p[0][0]);
    std::copy(&t[0][0], &t[0][0] + 8, &uv[0][0]);
}
// Upright billboard of the top `rows` art rows standing on a block's top face (a flower pot on the counter): its foot is the
// boundary below the last row; Y - Z of art row r stays Ybot - 16 + r. Returns foot Y; z runs from H to H + rows.
float flower_foot_y(float Ybot, float H, int rows) { return Ybot - 16.0f + static_cast<float>(rows) + H; }
// Machine (kLedge with fg rows below a kMachineTop tile): ONE upright plane at Y = foot = lower tile top + fg. The lower tile's first fg
// rows (both layers) stand on the floor (z 0..fg), the upper tile's 16 rows (top layer) continue above them (z fg..fg+16). Row r of a tile
// whose top edge is at Y0 has z = foot - (Y0 + r), so Y - Z of every row is unchanged and the reference view sees the original picture.
// There is no lid: any cap at z = fg + 16 would project onto the wall tiles above the machine and replace their art.
float machine_foot_y(float lower_top_y, int rows) { return lower_top_y + static_cast<float>(rows); }
struct MachineSpan { float z0, z1; int v0, v1; bool upper; };
int machine_spans(float lower_top_y, int rows, int upper_first_row, MachineSpan out[2]) {
    const float foot = machine_foot_y(lower_top_y, rows);
    const float upper_top = lower_top_y - 16.0f;
    const int first = std::clamp(upper_first_row, 0, 15);   // the upper tile's top layer starts at this row (2AC: 8)
    out[0] = {0.0f, static_cast<float>(rows), 0, rows, false};                                   // lower: rows 0..rows-1
    out[1] = {foot - (upper_top + 16.0f), foot - (upper_top + first), first, 16, true};          // upper: rows first..15 above it
    return 2;
}
// Rows of the reference view [Y - Z] that an upright plane at foot covers between z0 and z1.
struct RefRange { float lo, hi; };
RefRange ref_range(float foot, float z0, float z1) { return {foot - z1, foot - z0}; }
// A back-wall tile that is the upper part of a machine draws only its BOTTOM layer in the wall pass (the genuine wall); its top layer is
// part of the machine plane. Anything else keeps the combined art.
bool wall_pass_uses_bottom_only(int kind) { return kind == kMachineTop; }
// One continuous plant over two tiles (counter far end above, corner below): both parts only when both have plant rows.
bool plant_continues(int upper_first_row, int lower_rows) { return upper_first_row > 0 && upper_first_row < 16 && lower_rows > 0 && lower_rows < 16; }
// Far end of the counter (kCounterTop): the plant stands on the counter top; its foot is the cell's bottom row boundary on that plane.
float plant_foot_y(float Ytop, float H) { return Ytop + 16.0f + H; }
// End face of the counter top: the vertical plane at X spanning the top's depth (Ytop + H .. Ytop + 16 + H), floor to H.
void counter_end_cap(float X, float Ytop, float H, float p[4][3]) {
    const float ya = Ytop + H, yb = Ytop + 16.0f + H;
    const float q[4][3] = {{X, ya, H}, {X, yb, H}, {X, yb, 0.0f}, {X, ya, 0.0f}};
    std::copy(&q[0][0], &q[0][0] + 12, &p[0][0]);
}
// Side face of a low block is drawn only on the side the camera sees and only at the END of a run: a side shared with
// an equally tall neighbouring block is internal (the nurse counter strip) and stays hidden.
bool seat_draw_side(bool left, float cam_tx, float X0, bool joined_neighbour) {
    if (joined_neighbour) return false;
    return left ? cam_tx <= X0 : cam_tx >= X0 + 16.0f;
}
// Height of a block: the model's own (a, art pixels) or the stool default.
inline float seat_height_of(int a) { return a > 0 && a < 16 ? static_cast<float>(a) : 4.0f; }
// A seat is one independent cell: a flat top at kSeatHeight (the art's upper rows,
// laid flat so the reference view still sees the picture: Y - Z follows the art
// rows), a front face from the art's bottom rows, and short side faces extruded
// from a rim column of those bottom rows. Nothing joins neighbouring cells.
constexpr float kSeatHeight = 4.0f;   // art pixels
struct SeatFaces { float front[4][3], top[4][3], left[4][3], right[4][3]; };
// Corners: front/left/right = (back-)top, (front-)top, bottom, bottom; top = back-left, back-right, front-right, front-left.
SeatFaces seat_geometry(float X0, float Ybot, float H) {
    const float Yback = Ybot - (16.0f - H);
    SeatFaces f;
    const float fr[4][3] = {{X0, Ybot, H}, {X0 + 16, Ybot, H}, {X0 + 16, Ybot, 0}, {X0, Ybot, 0}};
    const float tp[4][3] = {{X0, Yback, H}, {X0 + 16, Yback, H}, {X0 + 16, Ybot, H}, {X0, Ybot, H}};
    const float lf[4][3] = {{X0, Yback, H}, {X0, Ybot, H}, {X0, Ybot, 0}, {X0, Yback, 0}};
    const float rt[4][3] = {{X0 + 16, Yback, H}, {X0 + 16, Ybot, H}, {X0 + 16, Ybot, 0}, {X0 + 16, Yback, 0}};
    std::copy(&fr[0][0], &fr[0][0] + 12, &f.front[0][0]);
    std::copy(&tp[0][0], &tp[0][0] + 12, &f.top[0][0]);
    std::copy(&lf[0][0], &lf[0][0] + 12, &f.left[0][0]);
    std::copy(&rt[0][0], &rt[0][0] + 12, &f.right[0][0]);
    return f;
}
// Rim column for a side face: the first column from the outer edge inward whose bottom `rows` art rows are all opaque
// (a round base is narrower than the tile, so the outer columns are skipped); false when there is none.
bool seat_side_column(const std::uint32_t* art, bool left, int rows, int* col) {
    if (!art || !col || rows < 1 || rows > 16) return false;   // rows outside the tile would read before / after it
    for (int k = 0; k < 8; ++k) {
        const int c = left ? k : 15 - k;
        bool all = true;
        for (int y = 16 - rows; y < 16; ++y) all &= (art[y * 16 + c] >> 24) != 0;
        if (all) { *col = c; return true; }
    }
    return false;
}
// First art row with any opaque pixel (16 for empty art).
int first_opaque_row(const std::uint32_t* art) {
    for (int y = 0; y < 16; ++y) for (int x = 0; x < 16; ++x) if (art[y * 16 + x] >> 24) return y;
    return 16;
}
// Height of a box's front wall: whole rows below, plus the top wall row's
// opaque extent. A low piece (a bed's footboard) is as tall as its art, not a
// full tile with the flat top floating above it; the top plane's art stays
// aligned because wall_surface keeps Y - Z constant.
// top_row = smallest first_opaque_row over the top wall row's cells.
float box_wall_height(int wall_rows, int top_row) {
    if (wall_rows <= 0) return 0.0f;
    return (wall_rows - 1) * 16.0f + static_cast<float>(16 - std::min(top_row, 15));
}
// Indoor room front edge: the most common last floor row. A column that runs
// on past it (a doorway mat below the threshold) must not carry its own lip,
// or a detached slab hangs under the doorway; its lip stays on this line.
int common_bottom_row(const int* last_row, int n) {
    int count[64] = {};
    for (int i = 0; i < n; ++i) if (last_row[i] >= 0 && last_row[i] < 64) ++count[last_row[i]];
    return static_cast<int>(std::max_element(count, count + 64) - count);
}
// Doorway mats below a room's threshold (measured: player's house 1F, y9
// x3..5 = Building 0x01A/0x01B/0x01C). They are exit tiles outside the room
// shell, not floor: no ground quad and no lip of their own. Anything else that
// extends past the modal row (an irregular, deeper room) is real floor.
bool is_doorway_mat(std::uint32_t primary, int id) {
    return primary == kTilesetBuilding && (id == 0x01A || id == 0x01B || id == 0x01C);
}
// Last floor row of one column: rows top..n-1 with art, not a doorway mat.
int last_floor_row_col(const int* ids, const unsigned char* has_art, int top, int n, std::uint32_t primary) {
    int b = -1;
    for (int y = top; y < n; ++y) if (has_art[y] && !is_doorway_mat(primary, ids[y])) b = y;
    return b;
}
// Upstairs TV / console column (GenericBuilding1 0x286, Building 0x035,
// GenericBuilding1 0x28E / 0x296): the parts below 0x286 are drawn with their
// bottom layer for ground, but for 0x035 that layer is the native wall backdrop
// hidden behind the opaque TV (entries 1002/1012). Lifted into the run it would
// show as a wall patch on the floor, so these parts take the floor/rug of the
// beside cell instead (measured: 044 upper border, 045 middle).
bool run_needs_floor_ground(const int* ids, int n, std::uint32_t secondary) {
    if (secondary != kTilesetGenericBuilding1) return false;
    for (int i = 0; i < n; ++i) if (ids[i] == 0x286) return true;
    return false;
}
// Ground id from the beside cells: the left one if it is plain known ground
// (a rug or floor tile of this tileset), else the right one, else -1.
int beside_ground(int left_id, bool left_ok, int right_id, bool right_ok, const std::uint16_t* ground, std::size_t n) {
    auto known = [&](int id) { return std::find(ground, ground + n, id) != ground + n; };
    if (left_ok && known(left_id)) return left_id;
    if (right_ok && known(right_id)) return right_id;
    return -1;
}
// Foreground mask for a stand tile whose upper rows are plain floor (fg_top > 0):
// only rows from fg_top down belong to the standing object.
void mask_foreground(const std::uint32_t* in, int fg_top, std::uint32_t* out) {
    for (int y = 0; y < 16; ++y) for (int x = 0; x < 16; ++x)
        out[y * 16 + x] = y < fg_top ? 0u : in[y * 16 + x];
}
// Soft contact shadow centre/radii for a box standing on the floor (front edge
// at base, width from x0 to x1): a strip hugging the front.
struct ContactShadow { float cx, cy, rx, ry; };
ContactShadow contact_shadow(float x0, float x1, float base) {
    return {(x0 + x1) * 0.5f, base + 0.5f, (x1 - x0) * 0.5f + 3.0f, 4.5f};
}

// Height of an ordinary fence strip: from its first opaque art row to the
// ground (the gate panel matches it instead of shrinking with the stripe).
float fence_height(const std::uint32_t* px) {
    for (int y = 0; y < 16; ++y) for (int x = 0; x < 16; ++x)
        if (px[y * 16 + x] >> 24) return static_cast<float>(std::max(6, 16 - y));
    return 12.0f;
}

// Empty art rows at the bottom of a 16x16 tile (a small object drawn in the upper part of its cell, like the bedroom console: art rows 0..6).
int trailing_empty_rows(const std::uint32_t* art) {
    int n = 0;
    for (int y = 15; y >= 0; --y) {
        bool any = false;
        for (int x = 0; x < 16 && !any; ++x) any = (art[y * 16 + x] >> 24) != 0;
        if (any) break;
        ++n;
    }
    return n;
}
// A stand object stands on the floor at its own lowest art row. With `gap` empty rows under the bottom cell's art (only when 3 or more: smaller
// gaps are padding / shadow room) the whole run moves back by `gap` so it does not float. dy = rows of the cell above the run's bottom cell.
// Returns the foot offset to subtract from the front plane; z0 / z1 / v1 (art rows used) for that cell.
float stand_foot_gap(int gap) { return gap >= 3 ? static_cast<float>(gap) : 0.0f; }
void stand_span(int dy, float gap, float* z0, float* z1, float* v1) {
    if (dy == 0) { *z0 = 0; *z1 = 16.0f - gap; *v1 = 16.0f - gap; }
    else { *z0 = dy * 16.0f - gap; *z1 = *z0 + 16.0f; *v1 = 16.0f; }
}
// First wall-face art row of a wall cell: a mixed metatile (face > 0: top surface above, vertical face below) starts its face at row 16 - face,
// everything else at the first opaque row of its keyed art.
int wall_cell_top_row(int face, int first_opaque) { return face > 0 && face < 16 ? 16 - face : first_opaque; }
// A box front cell covers art heights u0..u0 + 16 above the base. Where the wall top (wall_h) falls inside that range the cell is two planes (the
// vertical face and the top / roof surface), so it is split there: one linear quad across the boundary would be a ramp, not a corner.
struct FrontPiece { float ua, ub; };
// Heights below the base plane (u < 0, a baked floor shadow row under a raised-base face) are cropped: nothing is drawn under the ground.
int front_pieces(float u0, float wall_h, FrontPiece out[2]) {
    const float lo = std::max(u0, 0.0f), u1 = u0 + 16.0f;
    if (u1 <= lo + 0.01f) return 0;
    if (wall_h > lo + 0.01f && wall_h < u1 - 0.01f) { out[0] = {lo, wall_h}; out[1] = {wall_h, u1}; return 2; }
    out[0] = {lo, u1};
    return 1;
}
// Side wall of a cell with a planter band: z_top = the wall's top, its rows top_row..band-1 are siding (band - top_row units from the top); the
// wall below them is hidden behind the planter in the picture, so the last siding row repeats DOWN TO THE FLOOR (z_floor) - the side stays closed
// along the whole house body, whatever the planter's own end cap covers. out[0] = the siding, out[1] (when there is wall below) the repeat.
struct SideSeg { float z0, z1, v0, v1; };
int side_band_segments(float z_top, int top_row, int band, float z_floor, SideSeg out[2]) {
    const float zb = std::max(z_top - static_cast<float>(band - top_row), z_floor);
    int n = 0;
    if (z_top > zb + 0.01f) out[n++] = {zb, z_top, static_cast<float>(top_row), static_cast<float>(band)};
    if (zb > z_floor + 0.01f) out[n++] = {z_floor, zb, band - 0.5f, band - 0.5f};
    return n;
}
// Planter (house 2): the flat top of art rows r0..r1 of a cell whose top edge is world y `ytop`, at height H. Y - Z follows the art rows (the
// reference view still sees the picture). Corners back-left, back-right, front-right, front-left.
void planter_top_quad(float X0, float ytop, float r0, float r1, float H, float p[4][3], float uv[4][2]) {
    const float q[4][3] = {{X0, ytop + r0 + H, H}, {X0 + 16, ytop + r0 + H, H}, {X0 + 16, ytop + r1 + H, H}, {X0, ytop + r1 + H, H}};
    const float t[4][2] = {{0, r0}, {16, r0}, {16, r1}, {0, r1}};
    std::copy(&q[0][0], &q[0][0] + 12, &p[0][0]);
    std::copy(&t[0][0], &t[0][0] + 8, &uv[0][0]);
}
// The planter's vertical front: art rows b..b+H at the plane where the top ends (Y = ytop + b + H), from z = H down to 0.
void planter_face_quad(float X0, float ytop, float b, float H, float p[4][3], float uv[4][2]) {
    const float Y = ytop + b + H;
    const float q[4][3] = {{X0, Y, H}, {X0 + 16, Y, H}, {X0 + 16, Y, 0}, {X0, Y, 0}};
    const float t[4][2] = {{0, b}, {16, b}, {16, b + H}, {0, b + H}};
    std::copy(&q[0][0], &q[0][0] + 12, &p[0][0]);
    std::copy(&t[0][0], &t[0][0] + 8, &uv[0][0]);
}
// End cap of a planter run at x = X: from the top's back edge (Y_back) to its front edge (Y_front), z 0..H.
void planter_end_quad(float X, float y_back, float y_front, float H, float p[4][3]) {
    const float q[4][3] = {{X, y_back, H}, {X, y_front, H}, {X, y_front, 0}, {X, y_back, 0}};
    std::copy(&q[0][0], &q[0][0] + 12, &p[0][0]);
}
// A column near the run's end whose face rows b..b+H-1 are all opaque (the box's own side colour). left = the run's left end. False when none.
bool planter_side_column(const std::uint32_t* art, int b, int H, bool left, int* col) {
    for (int k = 0; k < 6; ++k) {
        const int c = left ? k : 15 - k;
        bool ok = true;
        for (int r = b; r < b + H && r < 16; ++r) ok = ok && (art[r * 16 + c] >> 24) != 0;
        if (ok) { *col = c; return true; }
    }
    return false;
}
// Indoor floor under a bed (or table): each 8x8 quarter of the cell's bottom layer is floor when its tile entry occurs on a plain floor cell of this
// room (mask bit q); the others (frame, pillow) are replaced by a floor quarter of the same cell (side, then above / below, then the diagonal).
// Returns false when the cell has no floor quarter at all (the caller then borrows a neighbouring cell's floor or keeps the layer).
bool floor_fill_quads(const std::uint32_t* bottom, unsigned floor_mask, std::uint32_t* out) {
    std::copy(bottom, bottom + 256, out);
    if (!(floor_mask & 15)) return false;
    for (int q = 0; q < 4; ++q) {
        if (floor_mask >> q & 1) continue;
        const int partners[3] = {q ^ 1, q ^ 2, q ^ 3};
        int from = -1;
        for (int pq : partners) if (floor_mask >> pq & 1) { from = pq; break; }
        for (int y = 0; y < 8; ++y) for (int x = 0; x < 8; ++x)
            out[((q >> 1) * 8 + y) * 16 + (q & 1) * 8 + x] = bottom[((from >> 1) * 8 + y) * 16 + (from & 1) * 8 + x];
    }
    return true;
}
// One piece of a front cell: the quad between art heights ua (bottom) and ub (top); same corner order as front_quad.
void front_piece_quad(float base, float wall_h, float lean, float X0, float ua, float ub, float p[4][3]) {
    float Ya, Za, Yb, Zb;
    wall_surface(base, wall_h, lean, ub, &Ya, &Za);
    wall_surface(base, wall_h, lean, ua, &Yb, &Zb);
    const float q[4][3] = {{X0, Ya, Za}, {X0 + 16, Ya, Za}, {X0 + 16, Yb, Zb}, {X0, Yb, Zb}};
    std::copy(&q[0][0], &q[0][0] + 12, &p[0][0]);
}
void front_quad(float base, float wall_h, float lean, float X0, float u0, float p[4][3]) {
    float Ya, Za, Yb, Zb;
    wall_surface(base, wall_h, lean, u0 + 16, &Ya, &Za);
    wall_surface(base, wall_h, lean, u0, &Yb, &Zb);
    const float q[4][3] = {{X0, Ya, Za}, {X0 + 16, Ya, Za}, {X0 + 16, Yb, Zb}, {X0, Yb, Zb}};
    std::copy(&q[0][0], &q[0][0] + 12, &p[0][0]);
}

// ── actor sprites (player / NPC ObjectEvents) ─────────────────────────────────────────────────────────────────────────────────────────────────────────
// Which hardware OAM entries are characters. gObjectEvents[16] (36 bytes each, byte 0 bit 0 = active, byte 1 bit 5 = invisible, byte 4 = spriteId) point into
// gSprites[64] (68 bytes each, byte 62 bit 0 = in use) whose OamData (attr0..2 at +0..+4) is what the game copies into hardware OAM. A hardware entry is an actor when
// its tile / palette / priority word and its shape and size bits equal those of an active, visible, in-use actor sprite; effects, doors, icons and every other OAM slot
// have their own tiles and do not match. Read only (no guest writes); anything out of range or unreadable matches nothing (the sprite keeps the trapezoid).
struct ActorSig { unsigned shape, size, attr2; };
int actor_signatures(const std::uint8_t* objects, const std::uint8_t* sprites, ActorSig* out, int max) {
    if (!objects || !sprites) return 0;
    int n = 0;
    for (int id = 0; id < 16 && n < max; ++id) {
        const std::uint8_t* o = objects + id * 36;
        if (!(o[0] & 1) || (o[1] & 0x20)) continue;                 // inactive / invisible object event
        if (o[4] >= 64) continue;                                   // invalid sprite id
        const std::uint8_t* s = sprites + o[4] * 68;
        if (!(s[62] & 1)) continue;                                 // sprite slot not in use
        const unsigned a0 = s[0] | (s[1] << 8), a1 = s[2] | (s[3] << 8), a2 = s[4] | (s[5] << 8);
        if (a0 & 0x0300) continue;                                  // affine / disabled OAM: not a plain character
        out[n++] = {a0 & 0xC000u, a1 & 0xC000u, a2};
    }
    return n;
}
bool oam_is_actor(unsigned hw0, unsigned hw1, unsigned hw2, const ActorSig* sigs, int n) {
    if (hw0 & 0x0300) return false;
    for (int i = 0; i < n; ++i) if (sigs[i].attr2 == hw2 && sigs[i].shape == (hw0 & 0xC000u) && sigs[i].size == (hw1 & 0xC000u)) return true;
    return false;
}
constexpr int kObjW[3][4] = {{8, 16, 32, 64}, {16, 32, 32, 64}, {8, 8, 16, 32}};
constexpr int kObjH[3][4] = {{8, 16, 32, 64}, {8, 8, 16, 32}, {16, 32, 32, 64}};

struct SpriteImg {
    int x = 0, y = 0, w = 0, h = 0;   // native screen rect
    int index = 0;
    bool decal = false, half = false;
    ImVec2 uv0, uv1;
};

bool decode_sprite(const Mem& m, int index, SpriteImg* s, std::vector<std::uint32_t>& px) {
    const std::uint8_t* o = m.oam + index * 8;
    const unsigned a0 = o[0] | (o[1] << 8), a1 = o[2] | (o[3] << 8), a2 = o[4] | (o[5] << 8);
    if (!(m.io16(0) & 0x1000) || (gba::g_hd_obj_hide && gba::g_hd_obj_hide(index, a0, a1, a2))) return false;
    if ((a0 & 0x300) == 0x200) return false;          // disabled
    const unsigned shape = a0 >> 14, size = a1 >> 14, mode = (a0 >> 10) & 3;
    if (shape >= 3 || mode >= 2) return false;        // OBJ window / prohibited mode
    const int w = kObjW[shape][size], h = kObjH[shape][size];
    int y = a0 & 255, x = a1 & 511;
    if (y >= 160) y -= 256;
    if (x >= 256) x -= 512;
    const bool affine = (a0 & 0x100) != 0, dbl = affine && (a0 & 0x200);
    const int bw = dbl ? w * 2 : w, bh = dbl ? h * 2 : h;
    if (x >= 300 || x + bw <= -60 || y + bh <= -60) return false;
    const bool hf = !affine && (a1 & 0x1000), vf = !affine && (a1 & 0x2000);
    const bool c256 = (a0 & 0x2000) != 0;
    const bool map1d = (m.io16(0) & 0x40) != 0;
    const unsigned base = (a2 & 1023) & (c256 ? ~1u : ~0u), palette = a2 >> 12;
    int pa = 256, pb = 0, pc = 0, pd = 256;
    if (affine) {
        const std::uint8_t* ag = m.oam + ((a1 >> 9) & 31) * 32;
        auto param = [&](int off) { return static_cast<std::int16_t>(ag[off] | (ag[off + 1] << 8)); };
        pa = param(6); pb = param(14); pc = param(22); pd = param(30);
    }
    px.assign(static_cast<std::size_t>(bw) * bh, 0u);
    bool any = false;
    for (int py = 0; py < bh; ++py) {
        for (int qx = 0; qx < bw; ++qx) {
            int tx = hf ? w - 1 - qx : qx, ty = vf ? h - 1 - py : py;
            if (affine) {
                const int dx = qx - bw / 2, dy = py - bh / 2;
                tx = ((pa * dx + pb * dy) >> 8) + w / 2;
                ty = ((pc * dx + pd * dy) >> 8) + h / 2;
            }
            if (tx < 0 || ty < 0 || tx >= w || ty >= h) continue;
            unsigned off, idx;
            if (c256) {
                const unsigned tile = base + ((ty / 8) * (map1d ? w / 8 * 2 : 32) + (tx / 8) * 2);
                off = 0x10000 + tile * 32 + (ty & 7) * 8 + (tx & 7);
                if (off >= 0x18000) continue;
                idx = m.vram[off];
            } else {
                const unsigned tile = base + (ty / 8) * (map1d ? w / 8 : 32) + tx / 8;
                off = 0x10000 + tile * 32 + (ty & 7) * 4 + (tx & 7) / 2;
                if (off >= 0x18000) continue;
                idx = (m.vram[off] >> ((tx & 1) * 4)) & 15;
            }
            if (!idx) continue;
            const int pi = c256 ? 256 + static_cast<int>(idx) : 256 + static_cast<int>(palette * 16 + idx);
            px[static_cast<std::size_t>(py) * bw + qx] = rgb555(pal16(m, pi));
            any = true;
        }
    }
    if (!any) return false;
    s->x = x; s->y = y; s->w = bw; s->h = bh;
    if (affine) {
        // Double-size affine bounds contain transparent padding, not extra height.
        // Anchor the visible feet so scaled sprites cannot float above their shadow.
        int left = bw, top = bh, right = 0, bottom = 0;
        for (int py = 0; py < bh; ++py) for (int qx = 0; qx < bw; ++qx) {
            if (!(px[static_cast<std::size_t>(py) * bw + qx] >> 24)) continue;
            left = std::min(left, qx); top = std::min(top, py);
            right = std::max(right, qx + 1); bottom = std::max(bottom, py + 1);
        }
        const int cw = right - left, ch = bottom - top;
        std::vector<std::uint32_t> cropped(static_cast<std::size_t>(cw) * ch);
        for (int py = 0; py < ch; ++py)
            std::copy_n(px.data() + (top + py) * bw + left, cw, cropped.data() + py * cw);
        px.swap(cropped);
        s->x += left; s->y += top; s->w = cw; s->h = ch;
    }
    s->index = index;
    s->decal = vf || (affine && pa > 0 && pd < 0 && pb == 0 && pc == 0);
    s->half = mode == 1;
    return true;
}


// ── state ────────────────────────────────────────────────────────────────
float g_mix = 0.0f;          // 0 = 2D picture, 1 = 3D view
double g_last_t = -1.0;
int g_last_ox = 0, g_last_oy = 0, g_stale = 1000;
std::uint32_t g_camera_layout = 0;
bool g_f3_down = false;
float g_toast = 0.0f;
bool g_announced = false;

bool usable_scene(const Mem& m) {
    if ((m.u32(g::Main + 4) & ~1u) != kCB2_Overworld) return false;
    if (m.io16(0) & 0x80) return false;                // forced blank
    const int type = m.u8(kMapHeader + 0x17);          // MAP_TYPE_*
    return type == 1 || type == 2 || type == 3 || type == 6 || type == 8;   // outdoor fields and rooms
}

void draw_toast(gbarecomp::HostOverlay* ov, const Mem& m, float dt) {
    if (g_toast <= 0.0f) return;
    g_toast -= dt;
    if (!hd::fonts_ready()) return;
    hd::Ctx c;
    c.dl = ImGui::GetBackgroundDrawList();
    float x0, y0, x1, y1;
    ov->to_drawable(0, 0, gbarecomp::OverlaySpace::View, &x0, &y0);
    ov->to_drawable(1, 1, gbarecomp::OverlaySpace::View, &x1, &y1);
    c.ox = x0; c.oy = y0; c.s = std::max(0.5f, x1 - x0);
    c.vw = static_cast<float>(ov->view_width());
    c.vh = static_cast<float>(ov->view_height());
    c.m = m;
    const float a = std::clamp(g_toast / 0.3f, 0.0f, 1.0f);
    const char* label = pc_diorama() ? "3D view on  \xc2\xb7  F3 to switch" : "3D view off  \xc2\xb7  F3 to switch";
    const float tw = hd::text_w(hd::F::Bold, 7.0f, label);
    const float w = tw + 16.0f, h = 12.0f, x = (c.vw - w) * 0.5f, y = 6.0f;
    hd::rect(c, x, y, w, h, hd::col::alpha(hd::col::rgba(12, 15, 26, 225), a), h * 0.5f);
    hd::rect_line(c, x, y, w, h, hd::col::alpha(hd::col::kAccent2, 0.7f * a), h * 0.5f, 0.5f);
    hd::text(c, hd::F::Bold, 7.0f, x + 8.0f, y + (h - 7.0f) * 0.5f, hd::col::alpha(hd::col::kText, a), label);
}

// Classic (non-HD) text windows: BG0 decoded and laid over the scene.
void draw_bg0(const Out& o, const Mem& m, float margin, float alpha) {
    static const bool hide_all = std::getenv("FRLG_BG0_HIDE") != nullptr;       // diagnostic: no BG0 overlay at all
    static const bool legacy = std::getenv("FRLG_BG0_GATE") == nullptr;         // default: the original whole-layer overlay; FRLG_BG0_GATE=1 limits it to live windows
    static const bool diag = std::getenv("FRLG_BG0_DIAG") != nullptr;           // diagnostic: report BG0 pixels outside windows
    if (hide_all) return;
    if (gba::g_hd_bg_mask & 1u) return;                 // the HD interface draws every field window
    const unsigned dispcnt = m.io16(0);
    if (!(dispcnt & 0x100)) return;
    const unsigned cnt = m.io16(8);
    if (cnt & 0x80) return;                              // 256-colour text layer: not a window layer
    const unsigned cb = ((cnt >> 2) & 3) * 0x4000, sb = ((cnt >> 8) & 31) * 0x800;
    const int hofs = m.io16(0x10) & 511, vofs = m.io16(0x12) & 511;
    const int wt = (cnt >> 14) & 1 ? 64 : 32, ht = (cnt >> 14) & 2 ? 64 : 32;
    static std::vector<std::uint32_t> px(240 * 160);
    bool any = false;
    // live BG0 windows (guarded readers: allocated, tiles present, layer on, first tile in the tilemap), framed
    BgRect wins[32];
    int nwin = 0;
    for (int i = 0; i < 32; ++i) {
        const WindowInfo wi = window_info(m, i);
        if (!wi.live() || wi.bg != 0 || !window_shown(m, i)) continue;
        const Rect r = window_rect(m, i);
        const BgRect br = pad_window_rect(r.x, r.y, r.w, r.h, 8);
        if (bg_rect_valid(br)) wins[nwin++] = br;
    }
    long diag_in = 0, diag_out = 0;
    int ox0 = 999, oy0 = 999, ox1 = -1, oy1 = -1;
    for (int y = 0; y < 160; ++y) {
        const int ty = (y + vofs) & (ht * 8 - 1);
        for (int x = 0; x < 240; ++x) {
            std::uint32_t out = 0;
            bool masked = !legacy && !bg_point_in_rects(wins, nwin, x, y) && !diag;   // diag still decodes to count
            for (int i = 0; i < gba::g_hd_mask_count && !masked; ++i) {
                const auto& r = gba::g_hd_mask_rects[i];
                masked = (r.layers & 1) && x >= r.x0 && x < r.x1 && y >= r.y0 && y < r.y1;
            }
            if (!masked) {
                const int tx = (x + hofs) & (wt * 8 - 1);
                const unsigned block = (tx >> 8) + ((ty >> 8) * (wt >> 5));
                const unsigned off = sb + block * 0x800 + (((ty >> 3) & 31) * 32 + ((tx >> 3) & 31)) * 2;
                if (off + 1 < 0x10000) {
                    const std::uint16_t e = static_cast<std::uint16_t>(m.vram[off] | (m.vram[off + 1] << 8));
                    const int idx = bg_index(m.vram, cb, e, tx & 7, ty & 7);
                    if (idx) {
                        const bool in_win = bg_point_in_rects(wins, nwin, x, y);
                        if (legacy || in_win) { out = rgb555(pal16(m, (e >> 12) * 16 + idx)); any = true; }
                        if (in_win) ++diag_in;
                        else { ++diag_out; ox0 = std::min(ox0, x); oy0 = std::min(oy0, y); ox1 = std::max(ox1, x); oy1 = std::max(oy1, y); }
                    }
                }
            }
            px[y * 240 + x] = out;
        }
    }
    if (diag) {
        static int frames = 0;
        if (++frames % 120 == 1)
            std::fprintf(stderr, "[bg0-diag] windows=%d inside_px=%ld outside_px=%ld outside_bbox=(%d,%d)-(%d,%d) cnt=%04X hofs=%d vofs=%d "
                         "dispcnt=%04X mask=%u\n", nwin, diag_in, diag_out, ox0, oy0, ox1, oy1, cnt, hofs, vofs, dispcnt, gba::g_hd_bg_mask);
        std::fflush(stderr);
    }
    if (!any) return;
    if (!g_bg0) g_bg0 = make_texture(240, 160, false, false);
    if (!g_bg0) return;
    SDL_UpdateTexture(g_bg0, nullptr, px.data(), 240 * 4);
    o.dl->AddImage(tex_id(g_bg0), o.P(ImVec2(margin, 0)), o.P(ImVec2(margin + 240, 160)), ImVec2(0, 0),
                   ImVec2(1, 1), shade(1.0f, alpha));
}

}  // namespace

static void diorama_draw_impl(gbarecomp::HostOverlay* ov);
void diorama_draw(gbarecomp::HostOverlay* ov) {
    static const bool timing = std::getenv("FRLG_3D_TIMING") != nullptr;
    if (!timing) { diorama_draw_impl(ov); return; }
    const Uint64 t0 = SDL_GetPerformanceCounter();
    diorama_draw_impl(ov);
    static double acc = 0; static int n = 0;
    acc += static_cast<double>(SDL_GetPerformanceCounter() - t0) / SDL_GetPerformanceFrequency() * 1000.0;
    if (++n == 120) { std::fprintf(stderr, "[frlg-3d] draw %.2f ms/frame (cpu)\n", acc / n); acc = 0; n = 0; }
}

static void diorama_draw_impl(gbarecomp::HostOverlay* ov) {
    if (!ov) return;
    load_tune();
    const double now = ov->host_ms() / 1000.0;
    const float dt = g_last_t < 0 ? 0.0f : static_cast<float>(std::clamp(now - g_last_t, 0.0, 0.1));
    g_last_t = now;
    const Mem m = Mem::current();

    // F3: switch the view (edge-triggered)
    if (const Uint8* ks = SDL_GetKeyboardState(nullptr)) {
        const bool down = ks[SDL_SCANCODE_F3] != 0;
        if (down && !g_f3_down && !settings_capture_active()) {   // F3 pressed to bind it must not also switch the view
            set_pc_diorama(!pc_diorama());
            g_toast = 1.6f;
        }
        g_f3_down = down;
    }

    bool usable = m.ok() && m.pal && m.oam && usable_scene(m);
    const bool indoor = m.ok() && m.u8(kMapHeader + 0x17) == 8;
    const std::uint32_t layout = usable ? m.u32(kMapHeader) : 0;
    if (!usable || layout != g_camera_layout) {
        // A warp can reuse the overworld callback before the new tile ring is ready.
        g_stale = 1000;
        g_camera_layout = layout;
    }
    bool camera_verified = false;
    int origin_x = 0, origin_y = 0;
    if (usable) {
        if (field_camera(&origin_x, &origin_y)) {
            g_last_ox = origin_x; g_last_oy = origin_y; g_stale = 0;
            camera_verified = true;
        } else if (++g_stale <= 8) {
            origin_x = g_last_ox; origin_y = g_last_oy;   // brief mismatch (tile animation): keep going
        } else {
            usable = false;
        }
    }
    if (!usable) { g_mix = 0.0f; draw_toast(ov, m, dt); return; }
    const float target = pc_diorama() ? 1.0f : 0.0f;
    g_mix += (target - g_mix) * std::min(1.0f, dt * 7.0f);
    if (std::fabs(target - g_mix) < 0.01f) g_mix = target;
    if (g_mix <= 0.0f) { draw_toast(ov, m, dt); return; }
    if (!g_announced) {
        std::fprintf(stderr, "[frlg-3d] diorama view active\n");
        g_announced = true;
        if (g_toast <= 0.0f) g_toast = 3.0f;   // say once per launch how to switch it
    }

    // ── frame geometry ───────────────────────────────────────────────────
    Out o;
    o.dl = ImGui::GetBackgroundDrawList();
    float x0, y0, x1, y1;
    ov->to_drawable(0, 0, gbarecomp::OverlaySpace::View, &x0, &y0);
    ov->to_drawable(1, 1, gbarecomp::OverlaySpace::View, &x1, &y1);
    o.ox = x0; o.oy = y0; o.s = std::max(0.5f, x1 - x0);
    const float vw = static_cast<float>(ov->view_width()), vh = static_cast<float>(ov->view_height());
    const float margin = std::max(0.0f, std::floor((vw - 240.0f) * 0.5f));
    int smooth_on = 0;
    gbarecomp::runtime_setting_get("graphics.linear_filter", &smooth_on);   // the existing Smoothing setting
    const FramePlan plan = plan_frame(o.s, static_cast<int>(vw), static_cast<int>(vh), smooth_on != 0, g_tune.scale, g_tune.budget, g_tune.zoom);

    Cam cam;
    const float al = g_tune.alpha * 3.14159265f / 180.0f;
    cam.sa = std::sin(al); cam.ca = std::cos(al);
    cam.sy = 1.0f / cam.sa; cam.sz = 1.0f / cam.ca;
    cam.f = (vh * 0.5f) / std::tan(g_tune.fov * 3.14159265f / 360.0f);
    cam.d = cam.f / plan.zoom;
    cam.cx = vw * 0.5f;
    cam.cy = vh * 0.5f;
    cam.tx = static_cast<float>(origin_x) + 120.0f;
    cam.ty = static_cast<float>(origin_y) + 80.0f;

    // visible cells
    float gx0 = 1e9f, gx1 = -1e9f, gy0 = 1e9f, gy1 = -1e9f;
    const float corners[4][2] = {{0, 0}, {vw, 0}, {0, vh}, {vw, vh}};
    for (const auto& cn : corners) {
        float X, Y;
        cam.unproject(cn[0], cn[1], 1600.0f, &X, &Y);
        gx0 = std::min(gx0, X); gx1 = std::max(gx1, X);
        gy0 = std::min(gy0, Y); gy1 = std::max(gy1, Y);
    }
    gy1 += 64.0f;   // things standing in front of the bottom edge reach up into view
    gx0 -= 32.0f; gx1 += 32.0f;
    int mx0 = static_cast<int>(std::floor(gx0 / 16.0f)), mx1 = static_cast<int>(std::floor(gx1 / 16.0f));
    int my0 = static_cast<int>(std::floor(gy0 / 16.0f)), my1 = static_cast<int>(std::floor(gy1 / 16.0f));
    const int fx = (origin_x + 120) / 16, fy = (origin_y + 80) / 16;
    mx0 = std::max(mx0, fx - 40); mx1 = std::min(mx1, fx + 40);
    my0 = std::max(my0, fy - 36); my1 = std::min(my1, fy + 14);
    if (usable && uses_multi(m.u32(layout + 16), m.u32(layout + 20)))   // only where multi-tile models exist
        expand_cell_window(&mx0, &my0, &mx1, &my1, kMultiMargin);   // multi-tile objects touching the screen are never cut by the snapshot edge
    const int mw = mx1 - mx0 + 1, mh = my1 - my0 + 1;
    static std::vector<FieldCell> cells;
    if (mw <= 0 || mh <= 0 || !field_cells(mx0, my0, mw, mh, &cells)) { g_mix = 0.0f; return; }
    if (indoor) for (FieldCell& c : cells) if (c.border) c.valid = false;   // the black void around rooms
    if (camera_verified) field_door_frames(origin_x, origin_y, mx0, my0, mw, mh, &cells);
    auto cell = [&](int x, int y) -> const FieldCell& { return cells[static_cast<std::size_t>(y) * mw + x]; };
    auto inside = [&](int x, int y) { return x >= 0 && y >= 0 && x < mw && y < mh; };

    // ── tileset, models and art ──────────────────────────────────────────
    const std::uint32_t prim = m.u32(layout + 16), sec = m.u32(layout + 20);
    if (prim != g_models.primary || sec != g_models.secondary) g_models.load(prim, sec);
    const unsigned cb = ((m.io16(0xC) >> 2) & 3) * 0x4000;   // BG2 (map) character base
    // Metatile entries straight from the tileset (for ground and tree parts
    // that need not be on screen).
    auto entries_of = [&](int id, std::uint16_t* out) {
        const std::uint32_t ts = id < 640 ? prim : sec;
        const std::uint8_t* p = m.ptr(m.u32(ts + 12) + (id < 640 ? id : id - 640) * 16, 16);
        if (!p) return false;
        for (int i = 0; i < 8; ++i) out[i] = static_cast<std::uint16_t>(p[i * 2] | (p[i * 2 + 1] << 8));
        return true;
    };
    // Per-frame art cache: one slot per (metatile id, layers); animated door
    // cells get their own slots.
    static std::vector<std::uint32_t> art_px;
    static std::vector<int> art_stamp;
    static int frame_no = 0;
    ++frame_no;
    const std::size_t slots = (1024 + cells.size()) * 4;
    if (art_stamp.size() < slots) { art_stamp.assign(slots, -1); art_px.resize(slots * 256); }
    // Colours of plain ground, keyed out of standing art without a top layer.
    static std::vector<std::uint32_t> ground_cols;
    ground_cols.clear();
    auto art = [&](int slot_id, const std::uint16_t* entries, int mode) -> const std::uint32_t* {
        const std::size_t s = static_cast<std::size_t>(slot_id) * 4 + mode;
        std::uint32_t* out = &art_px[s * 256];
        if (art_stamp[s] == frame_no) return out;
        art_stamp[s] = frame_no;
        if (mode < 3) {
            metatile_art(m, cb, entries, static_cast<Layers>(mode), out);
            // Indoors pure black is the void around the room: see-through.
            if (indoor) for (int i = 0; i < 256; ++i) if (!(out[i] & 0xFFFFFFu)) out[i] = 0;
        } else {   // full art minus ground colours
            metatile_art(m, cb, entries, kBoth, out);
            for (int i = 0; i < 256; ++i)
                if (std::find(ground_cols.begin(), ground_cols.end(), out[i]) != ground_cols.end()) out[i] = 0;
        }
        return out;
    };
    constexpr int kKeyed = 3;
    auto id_art = [&](int id, int mode) -> const std::uint32_t* {
        std::uint16_t e[8];
        if (!entries_of(id, e)) return nullptr;
        return art(id, e, mode);
    };
    auto cell_art = [&](int x, int y, int mode) {
        const FieldCell& c = cell(x, y);
        const int s = c.animated ? 1024 + y * mw + x : c.id;
        return art(s, c.entries, mode);
    };
    for (std::uint16_t g : g_models.ground) {
        if (const std::uint32_t* p = id_art(g, kBoth))
            for (int i = 0; i < 256; ++i)
                if (std::find(ground_cols.begin(), ground_cols.end(), p[i]) == ground_cols.end()) ground_cols.push_back(p[i]);
    }
    // Experimental (FRLG_3D_GENERIC=1, off by default): blocking cells with a real foreground layer in unmodelled tilesets stand upright.
    static const bool generic_on = std::getenv("FRLG_3D_GENERIC") != nullptr;
    static std::vector<std::uint8_t> generic_stand;   // per cell: 0 = the table model, 1 = generic standing object, 2 = forced flat (a multi-tile pattern that is not complete)
    generic_stand.assign(cells.size(), 0);
    static const TileModel kGenericStand{0, kStand}, kForcedFlat{0, kFlat}, kCanopyModel{0, kCanopy}, kTrunkModel{0, kTrunk}, kGateRoofModel{0, kGateRoof};
    auto model = [&](int x, int y) -> const TileModel& {
        const std::uint8_t o = generic_stand[static_cast<std::size_t>(y) * mw + x];
        return o == 1 ? kGenericStand : o == 2 ? kForcedFlat : o == 3 ? kCanopyModel : o == 4 ? kTrunkModel : o == 5 ? kGateRoofModel : g_models.at[cell(x, y).id & 1023];
    };
    static std::vector<float> canopy_vh, gate_vh;   // canopy / gate roof vertex heights, (mw + 1) x (mh + 1), valid when canopy_vh_on / gate_vh non-empty
    gate_vh.clear();
    bool canopy_vh_on = false;
    // Multi-tile objects (forest 3x3 tree): complete patterns stay kMulti, any other kMulti cell (cut by the snapshot edge, a different arrangement) is flat.
    static std::vector<MultiHit> multi_hits;
    multi_hits.clear();
    {
        bool any = false;
        any = uses_multi(g_models.primary, g_models.secondary);   // only the forest pair has multi-tile / canopy structures
        if (any) {
            std::vector<int> mids(cells.size());
            for (std::size_t i = 0; i < cells.size(); ++i) mids[i] = cells[i].valid ? cells[i].id & 1023 : -1;
            multi_hits.resize(256);
            multi_hits.resize(static_cast<std::size_t>(find_multi(mids.data(), mw, mh, kForestMulti, 1, multi_hits.data(), 256)));
            std::vector<unsigned char> keep(cells.size(), 0);
            for (const MultiHit& h : multi_hits)
                for (int dy = 0; dy < kForestMulti[h.def].h; ++dy) for (int dx = 0; dx < kForestMulti[h.def].w; ++dx)
                    if (kForestMulti[h.def].ids[dy * kForestMulti[h.def].w + dx] != kWild) keep[static_cast<std::size_t>(h.y + dy) * mw + h.x + dx] = 1;
            for (std::size_t i = 0; i < cells.size(); ++i)
                if (cells[i].valid && g_models.at[cells[i].id & 1023].kind == kMulti && !keep[i]) generic_stand[i] = 2;
            // EXPERIMENTAL dense canopy (FRLG_3D_CANOPY=1): per cell, from the cell's own id and its neighbours; trees already claimed are never canopy
            // default ON for the Forest pair (native verified, c48cc61); FRLG_3D_CANOPY=0 is the explicit diagnostic flat fallback
            static const bool canopy_on = [] { const char* e = std::getenv("FRLG_3D_CANOPY"); return !(e && e[0] == '0'); }();
            if (canopy_on) {
                std::vector<unsigned char> is_canopy(cells.size());
                canopy_cells(mids.data(), keep.data(), mw, mh, is_canopy.data());
                for (std::size_t i = 0; i < cells.size(); ++i) if (!cells[i].valid) is_canopy[i] = 0;
                for (std::size_t i = 0; i < cells.size(); ++i) if (is_canopy[i] == 1) generic_stand[i] = 3; else if (is_canopy[i] == 2) generic_stand[i] = 4;
                static const float ch = [] { const char* e = std::getenv("FRLG_CANOPY_H"); return e ? std::max(0.0f, static_cast<float>(std::atof(e))) : kCanopyHeight; }();
                static const int cr = [] { const char* e = std::getenv("FRLG_CANOPY_R"); return e ? canopy_clamp_ramp(std::atoi(e)) : kCanopyRamp; }();
                // EXPERIMENTAL gate roof (FRLG_3D_GATE_ROOF=1): role 4 cells, their own height field; canopy cells treat them as non-canopy (ground-level boundary)
                // default ON for the Forest pair (native verified, e359f68); FRLG_3D_GATE_ROOF=0 is the explicit diagnostic flat fallback
                static const bool gate_on = [] { const char* e = std::getenv("FRLG_3D_GATE_ROOF"); return !(e && e[0] == '0'); }();
                if (gate_on) {
                    gate_roof_cells(mids.data(), keep.data(), mw, mh, is_canopy.data());
                    for (std::size_t i = 0; i < cells.size(); ++i) if (is_canopy[i] == 4 && !cells[i].valid) is_canopy[i] = 0;
                    for (std::size_t i = 0; i < cells.size(); ++i) if (is_canopy[i] == 4) generic_stand[i] = 5;
                }
                canopy_vh.assign(static_cast<std::size_t>(mw + 1) * (mh + 1), 0.0f);
                canopy_vertex_heights(is_canopy.data(), mw, mh, ch, cr, canopy_vh.data());
                canopy_vh_on = true;
                if (gate_on) {
                    static const float gh = [] { const char* e = std::getenv("FRLG_GATE_H"); return e ? std::max(0.0f, static_cast<float>(std::atof(e))) : kGateRoofHeight; }();
                    static const int gr = [] { const char* e = std::getenv("FRLG_GATE_R"); return e ? canopy_clamp_ramp(std::atoi(e)) : kGateRoofRamp; }();
                    gate_vh.assign(static_cast<std::size_t>(mw + 1) * (mh + 1), 0.0f);
                    canopy_vertex_heights(is_canopy.data(), mw, mh, gh, gr, gate_vh.data(), 4);
                }
            }
        }
    }
    // One-shot numeric art diagnostic (off unless FRLG_ART_DIAG=<hex ids, comma separated> is set):
    // per native id on this map - layer opaque counts, top-layer bounds, rows with
    // opaque art, and how many pixels match the known ground colours.
    {
        static const char* want = std::getenv("FRLG_ART_DIAG");
        static std::vector<int> done;
        if (want && *want) {
            for (int i = 0; i < static_cast<int>(cells.size()); ++i) {
                const FieldCell& dc = cells[i];
                const int id = dc.id & 1023;
                if (!dc.valid || std::find(done.begin(), done.end(), id) != done.end()) continue;
                bool listed = false;
                for (const char* q = want; *q;) {
                    char* end = nullptr;
                    const long v = std::strtol(q, &end, 16);
                    if (end == q) { ++q; continue; }
                    listed |= v == id;
                    q = end;
                }
                if (!listed) continue;
                done.push_back(id);
                const std::uint32_t* both = cell_art(i % mw, i / mw, kBoth);
                const std::uint32_t* bot = cell_art(i % mw, i / mw, kBottom);
                const std::uint32_t* top = cell_art(i % mw, i / mw, kTop);
                const std::uint32_t* key = cell_art(i % mw, i / mw, kKeyed);
                auto count = [](const std::uint32_t* a) { int n = 0; for (int k = 0; k < 256; ++k) n += (a[k] >> 24) ? 1 : 0; return n; };
                int t0 = 16, t1 = -1, k0 = 16, k1 = -1, ground_hits = 0;
                for (int k = 0; k < 256; ++k) {
                    if (top[k] >> 24) { t0 = std::min(t0, k / 16); t1 = std::max(t1, k / 16); }
                    if (key[k] >> 24) { k0 = std::min(k0, k / 16); k1 = std::max(k1, k / 16); }
                    if (std::find(ground_cols.begin(), ground_cols.end(), both[k]) != ground_cols.end()) ++ground_hits;
                }
                std::fprintf(stderr, "[art-diag] id=%03X entries=%04X,%04X,%04X,%04X/%04X,%04X,%04X,%04X has_top=%d kind=%d fg=%d "
                             "opaque both=%d bottom=%d top=%d keyed=%d top_rows=%d..%d keyed_rows=%d..%d ground_matches=%d ground_cols=%zu\n",
                             id, dc.entries[0], dc.entries[1], dc.entries[2], dc.entries[3], dc.entries[4], dc.entries[5],
                             dc.entries[6], dc.entries[7], has_top(dc.entries) ? 1 : 0, g_models.at[id].kind, g_models.at[id].fg,
                             count(both), count(bot), count(top), count(key), t0, t1, k0, k1, ground_hits, ground_cols.size());
                // FRLG_ART_ROWS=1: per art row of the top layer, bottom layer and keyed art: opaque pixels, distinct colours, the dominant colour and
                // its count, how many opaque pixels are known ground colours, and the first / last opaque column. The top / front boundary of a
                // mixed metatile (tabletop above, rim below) is where these rows change character.
                static const bool rows_diag = std::getenv("FRLG_ART_ROWS") != nullptr;
                if (rows_diag) {
                    const std::uint32_t* layers[3] = {top, bot, key};
                    const char* names[3] = {"top", "bottom", "keyed"};
                    for (int li = 0; li < 3; ++li)
                        for (int r = 0; r < 16; ++r) {
                            std::uint32_t seen[16] = {};
                            int seen_n[16] = {}, distinct = 0, opaque = 0, ground = 0, c0 = 16, c1 = -1;
                            for (int xx = 0; xx < 16; ++xx) {
                                const std::uint32_t q = layers[li][r * 16 + xx];
                                if (!(q >> 24)) continue;
                                ++opaque; c0 = std::min(c0, xx); c1 = std::max(c1, xx);
                                ground += std::find(ground_cols.begin(), ground_cols.end(), q) != ground_cols.end() ? 1 : 0;
                                int k = 0;
                                while (k < distinct && seen[k] != q) ++k;
                                if (k == distinct && distinct < 16) { seen[distinct] = q; seen_n[distinct++] = 0; }
                                if (k < 16) ++seen_n[k];
                            }
                            int best = 0;
                            for (int k = 1; k < distinct; ++k) if (seen_n[k] > seen_n[best]) best = k;
                            std::fprintf(stderr, "[art-rows] id=%03X layer=%-6s row=%2d opaque=%2d colours=%2d dominant=%08X x%d ground=%2d cols=%d..%d\n", id, names[li], r,
                                         opaque, distinct, distinct ? seen[best] : 0u, distinct ? seen_n[best] : 0, ground, c0, c1);
                        }
                }
                std::fflush(stderr);
            }
        }
    }
    // Buildings not in a table yet: a connected block of solid tiles that
    // contains a door (game data, not pixels) is a box; its bottom two rows
    // are wall.
    static std::vector<std::uint8_t> fallback;
    fallback.assign(cells.size(), kFlat);
    if (!indoor) {
        std::vector<std::uint8_t> seen(cells.size(), 0);
        auto solid = [&](int x, int y) {
            if (!inside(x, y)) return false;
            const FieldCell& c = cell(x, y);
            return c.valid && (c.collision || c.door) && !(c.behavior >= 0x10 && c.behavior <= 0x1F) &&
                model(x, y).kind == kFlat;
        };
        for (int sy = 0; sy < mh; ++sy) for (int sx = 0; sx < mw; ++sx) {
            if (!solid(sx, sy) || seen[sy * mw + sx]) continue;
            std::vector<int> comp{sy * mw + sx};
            seen[sy * mw + sx] = 1;
            bool door = false;
            int bx0 = sx, bx1 = sx, by0 = sy, by1 = sy;
            for (std::size_t h = 0; h < comp.size(); ++h) {
                const int x = comp[h] % mw, y = comp[h] / mw;
                door |= cell(x, y).door;
                bx0 = std::min(bx0, x); bx1 = std::max(bx1, x); by0 = std::min(by0, y); by1 = std::max(by1, y);
                const int nb[4][2] = {{x - 1, y}, {x + 1, y}, {x, y - 1}, {x, y + 1}};
                for (const auto& n : nb)
                    if (solid(n[0], n[1]) && !seen[n[1] * mw + n[0]]) { seen[n[1] * mw + n[0]] = 1; comp.push_back(n[1] * mw + n[0]); }
            }
            const int bw = bx1 - bx0 + 1, bh = by1 - by0 + 1;
            if (!door || bw < 3 || bh < 3 || bw > 12 || bh > 10) continue;
            for (int i : comp) fallback[i] = i / mw >= by1 - 1 ? kWall : kRoof;
            // Roof edges above it are walkable tiles drawn over sprites (a top
            // layer); they belong to the roof, not to the ground behind.
            for (int x = bx0; x <= bx1; ++x)
                for (int y = by0 - 1; y >= std::max(0, by0 - 2); --y) {
                    const FieldCell& c = cell(x, y);
                    if (!c.valid || c.collision || !has_top(c.entries) || model(x, y).kind != kFlat ||
                        fallback[(y + 1) * mw + x] != kRoof) break;
                    fallback[y * mw + x] = kRoof;
                }
        }
    }
    auto kind_at = [&](int x, int y) -> int {
        const int k = model(x, y).kind;
        return k == kFlat ? fallback[static_cast<std::size_t>(y) * mw + x] : k;
    };
    // Split objects: an unlisted solid cell flanked by the same box kind could join that box. Unverified inference (it can swallow an adjacent
    // prop), so it is TEST ONLY: candidates are always computed for the audit, applied only with FRLG_3D_ORPHANS=1.
    static const bool orphans_on = std::getenv("FRLG_3D_ORPHANS") != nullptr;
    static std::vector<std::uint8_t> eff_kind, solid_cell, adopted_kind;
    eff_kind.assign(cells.size(), kFlat); solid_cell.assign(cells.size(), 0); adopted_kind.assign(cells.size(), kFlat);
    int adopted_n = 0;
    if (!indoor) {
        for (int y = 0; y < mh; ++y) for (int x = 0; x < mw; ++x) {
            const FieldCell& c = cell(x, y);
            const std::size_t i = static_cast<std::size_t>(y) * mw + x;
            eff_kind[i] = static_cast<std::uint8_t>(c.valid ? kind_at(x, y) : kFlat);
            solid_cell[i] = c.valid && (c.collision || c.door) && !(c.behavior >= 0x10 && c.behavior <= 0x1F);
        }
        adopted_n = adopt_orphans(eff_kind.data(), solid_cell.data(), mw, mh, adopted_kind.data());
        if (adopted_n && orphans_on)
            for (std::size_t i = 0; i < cells.size(); ++i)
                if (adopted_kind[i] != eff_kind[i]) fallback[i] = adopted_kind[i];
    }
    if (generic_on && !indoor)
        for (int y = 0; y < mh; ++y) for (int x = 0; x < mw; ++x) {
            const FieldCell& c = cell(x, y);
            if (c.valid && kind_at(x, y) == kFlat && generic_stand_cell(c.door, c.collision, c.layer, has_top(c.entries) && has_opaque(cell_art(x, y, kTop)), c.behavior))
                generic_stand[static_cast<std::size_t>(y) * mw + x] = 1;
        }
    auto tex16 = [](const std::uint32_t* px) { return Tex{16, 16, px}; };
    // FRLG_SCENE_AUDIT=1 (off by default): numbers that establish scene-wide classification errors from a normal play session.
    //   tiles : once per (tileset pair, id, class) on screen: native layer type / behaviour / collision, art opacity, and whether it is
    //           a flat solid cell (an unmodelled blocking tile) or flat foreground (top layer that the game draws over sprites, lying on the floor here)
    //   frame : every 60 frames: how the on-screen cells were classified, orphan cells adopted into boxes, invalid / border cells (map edge)
    //   flip  : a cell on screen changed class from the previous frame at the same world position (a visible pop while walking)
    //   origin: the camera origin jumped by more than 4 px in one frame without a layout change, or reused a stale origin
    {
        static const char* audit_env = std::getenv("FRLG_SCENE_AUDIT");
        static const bool audit = audit_env != nullptr;
        static const int audit_level = audit_env && audit_env[0] >= '2' ? 2 : 1;
        if (audit) {
            static std::vector<std::uint8_t> prev_kind;
            static int p_mx0 = 0, p_my0 = 0, p_mw = 0, p_mh = 0, frames = 0;
            static std::uint32_t p_layout = 0;
            static std::vector<std::uint64_t> seen_cls;
            static int last_ox = 0, last_oy = 0;
            ++frames;
            const int sx0 = static_cast<int>(std::floor((origin_x - margin) / 16.0f)), sx1 = static_cast<int>(std::floor((origin_x + 240 + margin) / 16.0f)) + 1;
            const int sy0 = static_cast<int>(std::floor(origin_y / 16.0f)), sy1 = static_cast<int>(std::floor((origin_y + 160) / 16.0f)) + 1;
            int n_table = 0, n_fallback = 0, n_adopt = 0, n_generic = 0, n_flat = 0, n_invalid = 0, n_border = 0, n_flat_solid = 0, n_flat_fg = 0;
            std::vector<std::uint8_t> now_kind(cells.size());
            for (int y = 0; y < mh; ++y) for (int x = 0; x < mw; ++x) {
                const std::size_t i = static_cast<std::size_t>(y) * mw + x;
                const FieldCell& c = cells[i];
                const bool generic = generic_stand[i] == 1, table = !generic && generic_stand[i] != 2 && g_models.at[c.id & 1023].kind != kFlat;
                const bool candidate = !indoor && adopted_kind[i] != eff_kind[i];
                const bool adopt = candidate && orphans_on;
                const int kind = c.valid ? kind_at(x, y) : -1;
                now_kind[i] = static_cast<std::uint8_t>(kind + 1);
                const int wx = mx0 + x, wy = my0 + y;
                if (wx < sx0 || wx > sx1 || wy < sy0 || wy > sy1) continue;
                if (!c.valid) { ++n_invalid; continue; }
                if (c.border) ++n_border;
                const bool solid = (c.collision || c.door) && !(c.behavior >= 0x10 && c.behavior <= 0x1F);
                const bool fg = top_is_foreground(c.layer, has_top(c.entries) && has_opaque(cell_art(x, y, kTop)));   // opaque pixels, not nonzero entries
                const char* cls = generic ? "generic" : table ? "table" : adopt ? "adopted" : fallback[i] != kFlat ? "fallback" : candidate ? "flat(orphan-candidate)" : "flat";
                (generic ? n_generic : table ? n_table : adopt ? n_adopt : fallback[i] != kFlat ? n_fallback : n_flat)++;
                const bool flat = !generic && !table && !adopt && fallback[i] == kFlat;
                n_flat_solid += flat && solid;
                n_flat_fg += flat && fg;
                const std::uint64_t key = (static_cast<std::uint64_t>(g_models.primary) << 32) ^ (static_cast<std::uint64_t>(g_models.secondary) * 2654435761u) ^
                                          (static_cast<std::uint64_t>(c.id & 1023) << 3) ^ (flat ? 1 : 0) ^ (static_cast<std::uint64_t>(solid) << 1) ^ (static_cast<std::uint64_t>(fg) << 2);
                if (std::find(seen_cls.begin(), seen_cls.end(), key) != seen_cls.end()) continue;
                seen_cls.push_back(key);
                const std::uint32_t* both = cell_art(x, y, kBoth);
                const std::uint32_t* bot = cell_art(x, y, kBottom);
                const std::uint32_t* top = cell_art(x, y, kTop);
                int ob = 0, obt = 0, ot = 0;
                for (int k = 0; k < 256; ++k) { ob += (both[k] >> 24) ? 1 : 0; obt += (bot[k] >> 24) ? 1 : 0; ot += (top[k] >> 24) ? 1 : 0; }
                std::fprintf(stderr, "[scene-audit] tiles prim=%08X sec=%08X id=%03X class=%s kind=%d solid=%d layer=%d beh=%03X coll=%d elev=%d door=%d "
                             "has_top=%d fg=%d opaque both=%d bottom=%d top=%d first_world=(%d,%d)%s\n", g_models.primary, g_models.secondary, c.id & 1023, cls,
                             kind, solid ? 1 : 0, c.layer, c.behavior, c.collision, c.elevation, c.door ? 1 : 0, has_top(c.entries) ? 1 : 0, fg ? 1 : 0,
                             ob, obt, ot, wx, wy, flat && solid ? (candidate ? "  <-- flat blocking tile flanked by a box (orphan candidate)" : "  <-- unmodelled blocking tile (flat)") : flat && fg ? "  <-- foreground art lying flat" : "");
            }
            if (layout != p_layout) { prev_kind.clear(); p_layout = layout; }
            if (!prev_kind.empty()) {
                int fx = 0, fy = 0;
                const int flips = kind_flips(prev_kind.data(), p_mx0, p_my0, p_mw, p_mh, now_kind.data(), mx0, my0, mw, mh, sx0, sy0, sx1 + 1, sy1 + 1, &fx, &fy);
                if (flips) {
                    const int pxl = fx - p_mx0, pyl = fy - p_my0, cxl = fx - mx0, cyl = fy - my0;
                    std::fprintf(stderr, "[scene-audit] flip frame=%d cells=%d first_world=(%d,%d) id=%03X kind %d -> %d origin=(%d,%d)\n", frames, flips, fx, fy,
                                 cell(cxl, cyl).id & 1023, static_cast<int>(prev_kind[pyl * p_mw + pxl]) - 1, static_cast<int>(now_kind[cyl * mw + cxl]) - 1, origin_x, origin_y);
                }
                if (std::abs(origin_x - last_ox) > 4 || std::abs(origin_y - last_oy) > 4)
                    std::fprintf(stderr, "[scene-audit] origin frame=%d jump=(%d,%d) now=(%d,%d) camera_verified=%d stale=%d\n", frames, origin_x - last_ox, origin_y - last_oy,
                                 origin_x, origin_y, camera_verified ? 1 : 0, g_stale);
            }
            if (!camera_verified) std::fprintf(stderr, "[scene-audit] origin frame=%d stale-origin reused=(%d,%d) stale=%d\n", frames, origin_x, origin_y, g_stale);
            last_ox = origin_x; last_oy = origin_y;
            prev_kind = now_kind; p_mx0 = mx0; p_my0 = my0; p_mw = mw; p_mh = mh;
            // FRLG_SCENE_AUDIT=2: the id grid of the screen, so multi-tile objects can be read off without art: per cell
            // <id hex><'*' blocking / '-' walkable><layer type 0 normal 1 covered 2 split><'t' top layer has opaque pixels / '-'>, one line per row (world y).
            // Printed when the layout changes and again after the view moved 8 cells, at most 24 times.
            if (audit_level >= 2) {
                static std::uint32_t grid_layout = 0xFFFFFFFFu;
                static int grid_x = 0, grid_y = 0, grid_prints = 0;
                const int gx = origin_x / 16, gy = origin_y / 16;
                if (grid_prints < 24 && (grid_layout != layout || std::abs(gx - grid_x) >= 8 || std::abs(gy - grid_y) >= 8)) {
                    grid_layout = layout; grid_x = gx; grid_y = gy; ++grid_prints;
                    std::fprintf(stderr, "[scene-audit] grid layout=%08X prim=%08X sec=%08X world x=%d..%d y=%d..%d\n", layout, g_models.primary, g_models.secondary, sx0, sx1, sy0, sy1);
                    for (int wy = sy0; wy <= sy1; ++wy) {
                        std::string line;
                        char tok[16];
                        for (int wx = sx0; wx <= sx1; ++wx) {
                            const int lx = wx - mx0, ly = wy - my0;
                            if (!inside(lx, ly) || !cell(lx, ly).valid) { line += " ---- "; continue; }
                            const FieldCell& gc = cell(lx, ly);
                            std::snprintf(tok, sizeof tok, " %03X%c%d%c", gc.id & 1023, (gc.collision || gc.door) ? '*' : '-', gc.layer,
                                          has_top(gc.entries) && has_opaque(cell_art(lx, ly, kTop)) ? 't' : '-');
                            line += tok;
                        }
                        std::fprintf(stderr, "[scene-audit] grid y=%d:%s\n", wy, line.c_str());
                    }
                }
            }
            if (frames % 60 == 1)
                std::fprintf(stderr, "[scene-audit] frame=%d layout=%08X prim=%08X sec=%08X indoor=%d origin=(%d,%d) screen cells: table=%d fallback=%d adopted=%d generic=%d flat=%d "
                             "(flat_solid=%d flat_foreground=%d) invalid=%d border=%d\n", frames, layout, g_models.primary, g_models.secondary, indoor ? 1 : 0, origin_x, origin_y,
                             n_table, n_fallback, n_adopt, n_generic, n_flat, n_flat_solid, n_flat_fg, n_invalid, n_border);
            std::fflush(stderr);
        }
    }

    // ── framebuffer ──────────────────────────────────────────────────────
    const float rs = plan.rs;
    const int fw = plan.fw, fh = plan.fh;
    const std::uint32_t backdrop = indoor ? IM_COL32(17, 22, 30, 255) : rgb555(pal16(m, 0));
    g_raster.reset(fw, fh, rs, backdrop);

    // ── room shell rows (indoor): the top two room rows are the back wall ─
    int room_top = -1;
    if (indoor)
        for (int y = 0; y < mh && room_top < 0; ++y)
            for (int x = 0; x < mw; ++x) {
                const std::uint32_t* p = cell(x, y).valid ? cell_art(x, y, kBoth) : nullptr;
                if (p && std::any_of(p, p + 256, [](std::uint32_t q) { return q >> 24; })) { room_top = y; break; }
            }
    auto back_wall = [&](int y) { return indoor && room_top >= 0 && (y == room_top || y == room_top + 1); };
    // Indoor floor extent: columns with floor art, the front row of the room.
    auto floor_cell = [&](int x, int y) {
        if (!inside(x, y) || !cell(x, y).valid) return false;
        if (is_doorway_mat(g_models.primary, cell(x, y).id & 1023)) return false;
        const std::uint32_t* p = cell_art(x, y, kBoth);
        return std::any_of(p, p + 256, [](std::uint32_t q) { return q >> 24; });
    };
    auto last_floor_row = [&](int x) { int b = -1; for (int y = room_top; y < mh; ++y) if (floor_cell(x, y)) b = y; return b; };
    int room_rx0 = mw, room_rx1 = -1, room_bottom = mh;
    if (indoor && room_top >= 0) {
        for (int y = room_top; y < mh; ++y) for (int x = 0; x < mw; ++x)
            if (floor_cell(x, y)) { room_rx0 = std::min(room_rx0, x); room_rx1 = std::max(room_rx1, x); }
        std::vector<int> lasts;
        for (int x = room_rx0; x <= room_rx1; ++x) lasts.push_back(last_floor_row(x));
        if (!lasts.empty()) room_bottom = common_bottom_row(lasts.data(), static_cast<int>(lasts.size()));
    }

    // Indoor floor entries: the bottom-layer tile entries (flips ignored) of the room's plain floor cells (flat, no top layer). A bed quarter whose entry is
    // one of them is floor; anything else (frame, pillow) is object art and never painted on the floor.
    static std::vector<std::uint16_t> floor_entries;
    static std::deque<std::array<std::uint32_t, 256>> floor_px;   // stable until the flush (the ground quads are queued)
    floor_px.clear();
    floor_entries.clear();
    if (indoor)
        for (int y = 0; y < mh; ++y) for (int x = 0; x < mw; ++x) {
            const FieldCell& c = cell(x, y);
            if (!c.valid || kind_at(x, y) != kFlat || has_top(c.entries)) continue;
            for (int q = 0; q < 4; ++q) {
                const std::uint16_t e = c.entries[q] & 0xF3FF;
                if (std::find(floor_entries.begin(), floor_entries.end(), e) == floor_entries.end()) floor_entries.push_back(e);
            }
        }
    auto floor_mask_of = [&](const FieldCell& c) {
        unsigned mask = 0;
        for (int q = 0; q < 4; ++q)
            if (std::find(floor_entries.begin(), floor_entries.end(), static_cast<std::uint16_t>(c.entries[q] & 0xF3FF)) != floor_entries.end()) mask |= 1u << q;
        return mask;
    };
    // The floor under an indoor box cell: its own bottom layer when every quarter is floor (nothing to change); with some frame quarters the floor
    // quarters fill the rest; with none, the nearest box cell in the row / column that has any (a bed's middle row). False = keep the plain rule.
    auto box_floor = [&](int x, int y, const std::uint32_t** px) -> bool {
        const unsigned own = floor_mask_of(cell(x, y));
        if (own == 15 || floor_entries.empty()) return false;
        floor_px.emplace_back();
        std::uint32_t* out = floor_px.back().data();
        if (floor_fill_quads(cell_art(x, y, kBottom), own, out)) { *px = out; return true; }
        static const int kDx[8] = {-1, 1, -2, 2, 0, 0, 0, 0}, kDy[8] = {0, 0, 0, 0, -1, 1, -2, 2};
        for (int k = 0; k < 8; ++k) {
            const int nx = x + kDx[k], ny = y + kDy[k];
            if (!inside(nx, ny) || !cell(nx, ny).valid) continue;
            const unsigned nm = floor_mask_of(cell(nx, ny));
            if (!nm) continue;
            if (floor_fill_quads(cell_art(nx, ny, kBottom), nm, out)) { *px = out; return true; }
        }
        floor_px.pop_back();
        return false;
    };
    // ── ground ───────────────────────────────────────────────────────────
    for (int y = 0; y < mh; ++y) for (int x = 0; x < mw; ++x) {
        const FieldCell& c = cell(x, y);
        if (!c.valid || back_wall(y) || !(layer_mask() & 1)) continue;
        if (indoor && is_doorway_mat(g_models.primary, c.id & 1023)) continue;   // exit mat below the threshold: outside the room shell
        const TileModel& md = model(x, y);
        const bool object = md.kind == kStand || md.kind == kRoof || md.kind == kWall || md.kind == kSeat || md.kind == kPlanter || md.kind == kMulti || md.kind == kCanopy || md.kind == kGateRoof;
        const std::uint32_t* px = nullptr;
        int beside = -1;
        if (md.kind == kStand && md.fg == 0 && has_top(c.entries) && g_models.secondary == kTilesetGenericBuilding1) {
            // the TV / console run: ground comes from the rug beside it, not the hidden wall backdrop
            auto is_stand = [&](int xx, int yy) { return inside(xx, yy) && cell(xx, yy).valid && model(xx, yy).kind == kStand; };
            int top = y, bottom = y;
            while (is_stand(x, top - 1) && continues_stand_run(model(x, top))) --top;
            while (is_stand(x, bottom + 1) && continues_stand_run(model(x, bottom + 1))) ++bottom;
            std::vector<int> run_ids;
            for (int yy = top; yy <= bottom; ++yy) run_ids.push_back(cell(x, yy).id & 1023);
            if (run_needs_floor_ground(run_ids.data(), static_cast<int>(run_ids.size()), g_models.secondary)) {
                auto flat_ok = [&](int xx) { return inside(xx, y) && cell(xx, y).valid && model(xx, y).kind == kFlat; };
                beside = beside_ground(inside(x - 1, y) ? cell(x - 1, y).id & 1023 : -1, flat_ok(x - 1),
                                       inside(x + 1, y) ? cell(x + 1, y).id & 1023 : -1, flat_ok(x + 1),
                                       g_models.ground.data(), g_models.ground.size());
            }
        }
        const bool explicit_box = !indoor && (md.kind == kRoof || md.kind == kWall) && md.ground == 0xFFFF;
        if (beside >= 0) px = id_art(beside, kBoth);
        else if (explicit_box) {
            // known grass from beside / behind the footprint, else default grass; never the cell's own
            // layers or the tile in front (a road / path)
            int lx = x - 1, rx = x + 1, ty = y - 1;
            while (inside(lx, y) && (kind_at(lx, y) == kRoof || kind_at(lx, y) == kWall)) --lx;
            while (inside(rx, y) && (kind_at(rx, y) == kRoof || kind_at(rx, y) == kWall)) ++rx;
            while (inside(x, ty) && (kind_at(x, ty) == kRoof || kind_at(x, ty) == kWall)) --ty;
            auto flat_at = [&](int xx, int yy) { return inside(xx, yy) && cell(xx, yy).valid && kind_at(xx, yy) == kFlat; };
            const int cand_ids[3] = {inside(lx, y) ? cell(lx, y).id & 1023 : -1, inside(rx, y) ? cell(rx, y).id & 1023 : -1,
                                     inside(x, ty) ? cell(x, ty).id & 1023 : -1};
            const bool cand_flat[3] = {flat_at(lx, y), flat_at(rx, y), flat_at(x, ty)};
            const int gid = pick_box_ground(cand_ids, cand_flat, 3, kOutdoorGrass, 3);
            if (gid >= 0) px = id_art(gid, kBoth);
            static std::vector<long> seen_ground_diag;
            const long gkey = (static_cast<long>(mx0 + x) << 20) ^ static_cast<long>(my0 + y);
            if (box_diag_cell(mx0 + x, my0 + y) && std::find(seen_ground_diag.begin(), seen_ground_diag.end(), gkey) == seen_ground_diag.end() &&
                (seen_ground_diag.push_back(gkey), true)) std::fprintf(stderr, "[box-diag] ground cell=(%d,%d) id=%03X ground_id=%03X cands=%03X,%03X,%03X flat=%d%d%d\n", mx0 + x, my0 + y, c.id & 1023, gid, cand_ids[0] & 0xFFF, cand_ids[1] & 0xFFF, cand_ids[2] & 0xFFF, cand_flat[0], cand_flat[1], cand_flat[2]);
        }
        else if (md.kind == kLedge) px = md.ground != 0xFFFF ? id_art(md.ground, kBoth) : cell_art(x, y, kBottom);   // the floor; the strip is drawn raised
        else if (md.ground != 0xFFFF) px = id_art(md.ground, kBoth);
        else if (indoor && (md.kind == kRoof || md.kind == kWall) && has_top(c.entries) && box_floor(x, y, &px)) {
            // a bed whose bottom layer mixes floor with the frame / pillow: floor quarters kept as authored (rug or wood), the others replaced
        }
        else if (md.kind == kTrunk) {
            // trunk row of a terminal front: the baked oval stays flat ground; the centre cell's bark is painted over (the standing trunk replaces it)
            px = cell_art(x, y, kBoth);
            if ((c.id & 1023) == 0x2A4) {
                floor_px.emplace_back();
                trunk_ground_art(px, floor_px.back().data());
                px = floor_px.back().data();
            }
        }
        else if ((md.kind == kMulti || md.kind == kCanopy || md.kind == kGateRoof) && has_top(c.entries)) {
            // forest tree: the ground is the grass quarters (palette 0) of the bottom layer; a tree-art quarter kept in the bottom layer belongs to the billboard
            unsigned grass = 0;
            for (int q = 0; q < 4; ++q) if ((c.entries[q] >> 12) == 0) grass |= 1u << q;
            px = cell_art(x, y, kBottom);
            if (grass != 15 && grass != 0) {
                floor_px.emplace_back();
                if (floor_fill_quads(px, grass, floor_px.back().data())) px = floor_px.back().data(); else floor_px.pop_back();
            }
        }
        else if (object && has_top(c.entries)) px = cell_art(x, y, kBottom);
        else if (kind_at(x, y) == kRoof || kind_at(x, y) == kWall) {
            // under a box: the ground in front of it (seen through roof gaps)
            int gy = y;
            while (gy + 1 < mh && (kind_at(x, gy) == kRoof || kind_at(x, gy) == kWall)) ++gy;
            px = cell(x, gy).valid ? cell_art(x, gy, kBoth) : nullptr;
        }
        else {
            static const char* gm = std::getenv("FRLG_GROUND_MODE");
            const int mode_g = gm && !std::strcmp(gm, "bottom") ? kBottom : gm && !std::strcmp(gm, "top") ? kTop : gm && !std::strcmp(gm, "keyed") ? kKeyed : kBoth;
            px = cell_art(x, y, mode_g);
            if (box_diag_cell(mx0 + x, my0 + y)) {
                static std::vector<long> seen_flat;
                const long fkey = (static_cast<long>(mx0 + x) << 20) ^ static_cast<long>(my0 + y);
                if (std::find(seen_flat.begin(), seen_flat.end(), fkey) == seen_flat.end()) {
                    seen_flat.push_back(fkey);
                    auto cnt = [](const std::uint32_t* a) { int n = 0; for (int k = 0; k < 256; ++k) n += (a[k] >> 24) ? 1 : 0; return n; };
                    std::fprintf(stderr, "[box-diag] flat-ground cell=(%d,%d) id=%03X kind=%d model_kind=%d entries=%04X,%04X,%04X,%04X/%04X,%04X,%04X,%04X has_top=%d "
                                 "opaque both=%d bottom=%d top=%d mode=%d\n", mx0 + x, my0 + y, c.id & 1023, kind_at(x, y), md.kind,
                                 c.entries[0], c.entries[1], c.entries[2], c.entries[3], c.entries[4], c.entries[5], c.entries[6], c.entries[7],
                                 has_top(c.entries) ? 1 : 0, cnt(cell_art(x, y, kBoth)), cnt(cell_art(x, y, kBottom)), cnt(cell_art(x, y, kTop)), mode_g);
                    std::fflush(stderr);
                }
            }
        }
        if (!px) continue;
        const float X0 = (mx0 + x) * 16.0f, Y0 = (my0 + y) * 16.0f;
        const float p[4][3] = {{X0, Y0, 0}, {X0 + 16, Y0, 0}, {X0 + 16, Y0 + 16, 0}, {X0, Y0 + 16, 0}};
        const float uv[4][2] = {{0, 0}, {16, 0}, {16, 16}, {0, 16}};
        draw_quad(cam, p, uv, tex16(px), 0.96f);
    }

    // ── sprites (decoded once; shadows first, models later) ──────────────
    static std::vector<SpriteImg> sprites;
    static std::vector<std::vector<std::uint32_t>> sprite_px;
    sprites.clear();
    sprite_px.resize(128);
    for (int i = 0; i < 128; ++i) {
        SpriteImg s;
        if (!decode_sprite(m, i, &s, sprite_px[sprites.size()])) continue;
        sprites.push_back(s);
    }

    // FRLG_COUNTER_DIAG=1 (off by default; Pokémon Center interior only): once per room, the numbers needed to model the
    // nurse counter safely: per counter tile its layers and every art row's opacity / colour count / dominant colour /
    // grey count (the base rows are grey, the surface red), and every sprite's foot position in tile coordinates.
    {
        static const bool counter_diag = std::getenv("FRLG_COUNTER_DIAG") != nullptr;
        static std::uint32_t counter_layout = 0xFFFFFFFFu;
        if (counter_diag && indoor && g_models.primary == kTilesetBuilding && g_models.secondary == kTilesetPokemonCenter &&
            counter_layout != layout) {
            counter_layout = layout;
            std::fprintf(stderr, "[counter-diag] layout=%08X origin=(%d,%d) room_top=%d rx=[%d..%d] cam_origin=(%d,%d)\n", layout, mx0, my0,
                         room_top, room_rx0, room_rx1, origin_x, origin_y);
            static const int kCounterIds[] = {0x2B1, 0x2B2, 0x2B3, 0x290, 0x363, 0x2B4, 0x2B5, 0x2B9, 0x2BC, 0x298, 0x2BD, 0x2AA, 0x2AB, 0x2AC};
            for (int id : kCounterIds) {
                int cx = -1, cy = -1, n = 0;
                for (int i = 0; i < static_cast<int>(cells.size()); ++i)
                    if (cells[i].valid && (cells[i].id & 1023) == id) { if (n++ == 0) { cx = i % mw; cy = i / mw; } }
                if (n == 0) continue;
                const FieldCell& dc = cell(cx, cy);
                const std::uint32_t* both = cell_art(cx, cy, kBoth);
                const std::uint32_t* bot = cell_art(cx, cy, kBottom);
                const std::uint32_t* top = cell_art(cx, cy, kTop);
                auto cnt = [](const std::uint32_t* a) { int k = 0; for (int q = 0; q < 256; ++q) k += (a[q] >> 24) ? 1 : 0; return k; };
                std::fprintf(stderr, "[counter-diag] id=%03X uses=%d first_world=(%d,%d) entries=%04X,%04X,%04X,%04X/%04X,%04X,%04X,%04X has_top=%d "
                             "opaque both=%d bottom=%d top=%d\n", id, n, mx0 + cx, my0 + cy, dc.entries[0], dc.entries[1], dc.entries[2], dc.entries[3],
                             dc.entries[4], dc.entries[5], dc.entries[6], dc.entries[7], has_top(dc.entries) ? 1 : 0, cnt(both), cnt(bot), cnt(top));
                // the face the camera sees: the top layer when the tile has one, else everything
                // FRLG_COUNTER_DIAG=2: also the exact pixels (16 hex colours, 00000000 = transparent) of the rows where the machine's
                // upper tiles (2AA / 2AB), lower tiles (2B2 / 2B3) and the counter strip meet, per layer
                static const char* detail = std::getenv("FRLG_COUNTER_DIAG");
                if (detail && detail[0] == '2' && (id == 0x2AA || id == 0x2AB || id == 0x2AC || id == 0x2B2 || id == 0x2B3 || id == 0x2B4)) {
                    static const int kRowsOfInterest[] = {0, 1, 7, 8, 9, 11, 12, 15};
                    const std::uint32_t* layers[2] = {top, bot};
                    const char* names[2] = {"top", "bottom"};
                    for (int li = 0; li < 2; ++li)
                        for (int rr : kRowsOfInterest) {
                            std::fprintf(stderr, "[counter-diag]   id=%03X px layer=%s row=%2d:", id, names[li], rr);
                            for (int xx = 0; xx < 16; ++xx) std::fprintf(stderr, " %08X", layers[li][rr * 16 + xx]);
                            std::fprintf(stderr, "\n");
                        }
                }
                // corner tiles (2B9 / 2BD): the bottom layer's rows too, to separate the flowers from the counter surface
                const bool both_layers = id != 0x2BC && id != 0x298;   // every tile but the plain strip: both layers
                for (int pass = 0; pass < (both_layers ? 2 : 1); ++pass) {
                const std::uint32_t* face = pass == 1 ? bot : (has_top(dc.entries) ? top : both);
                const char* layer = pass == 1 ? "bottom" : (has_top(dc.entries) ? "top" : "both");
                for (int r = 0; r < 16; ++r) {
                    std::uint32_t dom = 0;
                    int opaque = 0, grey = 0, distinct = 0, best = 0;
                    std::uint32_t seen[16] = {};
                    int seen_n[16] = {};
                    for (int x = 0; x < 16; ++x) {
                        const std::uint32_t q = face[r * 16 + x];
                        if (!(q >> 24)) continue;
                        ++opaque;
                        const int r8 = q & 255, g8 = (q >> 8) & 255, b8 = (q >> 16) & 255;
                        if (std::max({r8, g8, b8}) - std::min({r8, g8, b8}) <= 12) ++grey;
                        int k = 0;
                        while (k < distinct && seen[k] != q) ++k;
                        if (k == distinct && distinct < 16) { seen[distinct] = q; seen_n[distinct++] = 0; }
                        if (k < 16) ++seen_n[k];
                    }
                    for (int k = 0; k < distinct; ++k) if (seen_n[k] > best) { best = seen_n[k]; dom = seen[k]; }
                    std::fprintf(stderr, "[counter-diag]   id=%03X layer=%s row=%2d opaque=%2d colours=%2d dominant=%08X grey=%2d\n", id, layer, r, opaque, distinct, dom, grey);
                }
                }
            }
            for (std::size_t i = 0; i < sprites.size(); ++i) {
                const SpriteImg& sp = sprites[i];
                const float fx = origin_x + sp.x + sp.w * 0.5f, fy = origin_y + sp.y + sp.h;
                std::fprintf(stderr, "[counter-diag] sprite=%zu oam=%d native=(%d,%d) size=%dx%d foot_world=(%.1f,%.1f) foot_cell=(%d,%d) decal=%d half=%d\n", i, sp.index,
                             sp.x, sp.y, sp.w, sp.h, fx, fy, static_cast<int>(std::floor(fx / 16.0f)), static_cast<int>(std::floor((fy - 1.0f) / 16.0f)),
                             sp.decal ? 1 : 0, sp.half ? 1 : 0);
            }
            std::fflush(stderr);
        }
    }

    // soft round shadows (blended onto the ground)
    static std::vector<std::uint32_t> shadow_px;
    if (shadow_px.empty()) {
        shadow_px.resize(32 * 32);
        for (int y = 0; y < 32; ++y) for (int x = 0; x < 32; ++x) {
            const float dx = (x + 0.5f - 16) / 16, dy = (y + 0.5f - 16) / 16;
            const float a = std::clamp(1.0f - (dx * dx + dy * dy), 0.0f, 1.0f);
            shadow_px[y * 32 + x] = static_cast<std::uint32_t>(a * a * 255) << 24 | 0x1E140Au;
        }
    }
    const Tex shadow_tex{32, 32, shadow_px.data()};
    auto ground_shadow = [&](float cx, float cy, float rx, float ry, float strength) {
        const float p[4][3] = {{cx - rx, cy - ry, 0.05f}, {cx + rx, cy - ry, 0.05f}, {cx + rx, cy + ry, 0.05f}, {cx - rx, cy + ry, 0.05f}};
        const float uv[4][2] = {{0, 0}, {32, 0}, {32, 32}, {0, 32}};
        draw_quad(cam, p, uv, shadow_tex, 1.0f, strength);
    };

    // ── trees: one clean model per tree ──────────────────────────────────
    // The pine is composed from the standalone tree's tiles: tip and upper
    // crown are top layers over grass; the lower crown is keyed.
    static std::vector<std::uint32_t> tree_px(32 * 48);
    bool tree_ok = g_models.known && g_models.primary == kTilesetGeneral;
    if (tree_ok) {
        const int parts[3][2] = {{0x00E, 0x00F}, {0x01E, 0x01F}, {0x024, 0x025}};
        for (int r = 0; r < 3 && tree_ok; ++r)
            for (int h = 0; h < 2 && tree_ok; ++h) {
                const std::uint32_t* p = id_art(parts[r][h], r < 2 ? kTop : kKeyed);
                if (!p) { tree_ok = false; break; }
                for (int y = 0; y < 16; ++y) std::copy_n(p + y * 16, 16, &tree_px[(r * 16 + y) * 32 + h * 16]);
            }
    }
    const Tex tree_tex{32, 48, tree_px.data()};
    std::vector<std::pair<int, int>> trees;   // world x, y of the upper-left crown cell
    auto tree_part = [&](int x, int y, int dx, int row) {
        if (!inside(x, y)) return false;
        const TileModel& t = model(x, y);
        return cell(x, y).valid && t.kind == kTree && t.a == dx && t.b == row;
    };
    for (int y = 0; y < mh; ++y) for (int x = 0; x < mw; ++x) {
        if (tree_part(x, y, 0, kUpper)) trees.push_back({mx0 + x, my0 + y});
        // trees cut by the edge of the snapshot still get drawn
        else if (tree_part(x, y, 0, kLower) && y == 0) trees.push_back({mx0 + x, my0 + y - 1});
        else if (tree_part(x, y, 0, kTip) && y == mh - 1) trees.push_back({mx0 + x, my0 + y + 1});
    }
    if (layer_mask() & 2) for (const auto& t : trees) ground_shadow(t.first * 16.0f + 16, (t.second + 2) * 16.0f - 3, 13, 6, 0.55f);
    for (const SpriteImg& s : sprites) {
        if (s.decal || s.h < 32) continue;   // people and Pokémon, not effects
        ground_shadow(origin_x + s.x + s.w * 0.5f, origin_y + s.y + s.h - 1.5f, s.w * 0.36f, 3.2f, 0.45f);
    }
    if (tree_ok && (layer_mask() & 2))
        for (const auto& t : trees)
            draw_upright(cam, t.first * 16.0f, t.first * 16.0f + 32, (t.second + 2) * 16.0f - 2, 0, 48,
                         tree_tex, 0, 32, 0, 48, 1.0f);

    // ── multi-tile objects: one billboard per complete pattern, composed from the TOP layers (the native art of the object) ──
    static std::deque<std::vector<std::uint32_t>> multi_px;   // stable until the flush
    multi_px.clear();
    if (layer_mask() & 2)
        for (const MultiHit& h : multi_hits) {
            const MultiDef& md = kForestMulti[h.def];
            const int tw = md.w * 16, th = md.h * 16;
            multi_px.emplace_back(static_cast<std::size_t>(tw) * th, 0u);
            std::vector<std::uint32_t>& buf = multi_px.back();
            for (int dy = 0; dy < md.h; ++dy) for (int dx = 0; dx < md.w; ++dx) {
                if (md.ids[dy * md.w + dx] == kWild) continue;
                // keyed art: both layers with the ground colours removed (grass, baked shadow oval, floor border), the tree's own colours kept
                const std::uint32_t* one = cell_art(h.x + dx, h.y + dy, kKeyed);
                std::uint32_t foot[256];
                const int fid = md.ids[dy * md.w + dx];
                if (fid == 0x2A3 || fid == 0x2A4 || fid == 0x2A5) { mask_tree_foot(one, fid == 0x2A4, foot); one = foot; }   // measured trunk-row mask
                for (int ry = 0; ry < 16; ++ry) std::copy_n(one + ry * 16, 16, &buf[static_cast<std::size_t>(dy * 16 + ry) * tw + dx * 16]);
            }
            const Tex mt{tw, th, buf.data()};
            // stand on the object's own last art row: the masked tree's trunk ends far above the cell bottom (empty rows below would float it)
            const float gap = stand_foot_gap(trailing_empty_rows_w(buf.data(), tw, th));
            const float mx = (mx0 + h.x) * 16.0f, front = (my0 + h.y + md.h) * 16.0f - gap, hh = static_cast<float>(th) - gap;
            ground_shadow(mx + tw * 0.5f, front - 2.0f, tw * 0.30f, 5.0f, 0.5f);
            draw_upright(cam, mx, mx + tw, front, 0, hh, mt, 0, static_cast<float>(tw), 0, hh, 1.0f);
        }

    // ── standing objects, tall grass, back walls ─────────────────────────
    auto stand = [&](int x, int y) { return inside(x, y) && cell(x, y).valid && model(x, y).kind == kStand; };
    static std::deque<std::array<std::uint32_t, 256>> grass_px;   // stable until the flush
    grass_px.clear();
    // Wall material of an indoor back-wall row (room_top = upper, room_top + 1 = lower) for furniture gaps,
    // corner fills and the side walls: the row's plain tile; if the row has none (all counters / machines),
    // a flat wall built from the upper row's plain tile; if there is no plain tile at all, nothing.
    auto plain_id_of_row = [&](int ry) {
        if (plain_row_cells(ry, mh, room_rx0, room_rx1) == 0) return -1;
        std::vector<int> ids;
        std::vector<unsigned char> flat, tops, cand;
        for (int xx = std::max(0, room_rx0); xx <= room_rx1 && xx < mw && inside(xx, ry); ++xx) {   // ry can lie outside a short snapshot
            ids.push_back(cell(xx, ry).valid ? cell(xx, ry).id & 1023 : -1);
            flat.push_back(cell(xx, ry).valid && model(xx, ry).kind == kFlat ? 1 : 0);
            tops.push_back(cell(xx, ry).valid && has_top(cell(xx, ry).entries) ? 1 : 0);
        }
        cand.resize(ids.size());
        plain_wall_candidates(flat.data(), tops.data(), static_cast<int>(ids.size()), g_models.primary == kTilesetBuilding && g_models.secondary == kTilesetGenericBuilding2, cand.data());
        return pick_plain_wall_id(ids.data(), cand.data(), static_cast<int>(ids.size()), -1);
    };
    const std::uint32_t* wall_memo[2] = {nullptr, nullptr};   // built once per frame per back-wall row
    bool wall_memo_done[2] = {false, false};
    auto wall_material_build = [&](int ry) -> const std::uint32_t* {
        const int own = plain_id_of_row(ry);
        if (own >= 0) return id_art(own, kBoth);
        int upper = ry == room_top + 1 ? plain_id_of_row(room_top) : -1;
        if (upper < 0) {   // no plain tile anywhere: a flat colour from the tile beside the first corner, never the decoration itself
            const int bx = room_rx0 + 1 <= room_rx1 ? room_rx0 + 1 : room_rx0;
            if (!inside(bx, ry) || !cell(bx, ry).valid) return nullptr;
            upper = cell(bx, ry).id & 1023;
        }
        const std::uint32_t* up = id_art(upper, kBoth);
        if (!up) return nullptr;
        grass_px.emplace_back();   // stable until the flush
        flat_wall_from(up, grass_px.back().data());
        return grass_px.back().data();
    };
    auto wall_material = [&](int ry) -> const std::uint32_t* {
        const int slot = ry - room_top;
        if (slot < 0 || slot > 1) return wall_material_build(ry);
        if (!wall_memo_done[slot]) { wall_memo[slot] = wall_material_build(ry); wall_memo_done[slot] = true; }
        return wall_memo[slot];
    };
    for (int y = 0; y < mh; ++y) for (int x = 0; x < mw; ++x) {
        const FieldCell& c = cell(x, y);
        if (!c.valid || !(layer_mask() & 4)) continue;
        const TileModel& md = model(x, y);
        const float X0 = (mx0 + x) * 16.0f, wy = static_cast<float>(my0 + y);
        if (back_wall(y)) {
            // Furniture in the wall rows stands on its own; the wall behind it
            // uses the row's commonest (plain) tile.
            const float Yw = (my0 + room_top + 2) * 16.0f, z0 = (room_top + 1 - y) * 16.0f;
            const std::uint32_t* px = cell_art(x, y, kBoth);
            const std::uint32_t* plain_px = wall_material(y);
            if (wall_pass_uses_bottom_only(md.kind)) px = cell_art(x, y, kBottom);   // the machine's upper body is drawn on its own plane
            else if (md.kind != kFlat) px = plain_px;
            else if (indoor && (x == room_rx0 || x == room_rx1) && px && is_corner_wedge(px, x == room_rx0)) {
                // corner tile with the 2D side-wall wedge: keep its art, close the see-through gap with the plain wall
                if (plain_px) {
                    grass_px.emplace_back();   // stable until the flush
                    fill_void_from(px, plain_px, grass_px.back().data());
                    px = grass_px.back().data();
                }
            }
            if (px) draw_upright(cam, X0, X0 + 16, Yw, z0, z0 + 16, tex16(px), 0, 16, 0, 16, 0.92f);
        }
        if (md.kind == kTree && g_models.secondary == kTilesetPalletTown) {
            if (const GateStripe* g = gate_stripe(c.id & 1023)) {
                grass_px.emplace_back();   // stable until the flush
                std::uint32_t* gpx = grass_px.back().data();
                isolate_stripe(cell_art(x, y, kBottom), g->c0, g->c1, ground_cols.data(), ground_cols.size(),
                               rgb555(pal16(m, 0)), gpx);
                const std::uint32_t* ref = id_art(0x0EC, kKeyed);
                float p[4][3], uv[4][2];
                fence_panel(X0 + (g->c0 + g->c1 + 1) * 0.5f, (my0 + y) * 16.0f, g->c0, g->c1,
                            ref ? fence_height(ref) : 12.0f, p, uv);
                draw_quad(cam, p, uv, tex16(gpx), 0.85f);
            }
        }
        if (md.kind == kMachineTop) {
            // upper part of the machine: its top layer on the plane shared with the lower tile below (kLedge, fg > 0)
            if (inside(x, y + 1) && cell(x, y + 1).valid && model(x, y + 1).kind == kLedge && model(x, y + 1).fg > 0 && has_top(c.entries)) {
                const float lower_top = (my0 + y + 1) * 16.0f;
                MachineSpan sp[2];
                machine_spans(lower_top, model(x, y + 1).fg, md.b, sp);
                draw_upright(cam, X0, X0 + 16, machine_foot_y(lower_top, model(x, y + 1).fg), sp[1].z0, sp[1].z1,
                             tex16(cell_art(x, y, kTop)), 0, 16, static_cast<float>(sp[1].v0), static_cast<float>(sp[1].v1), 1.0f);
            }
            continue;
        }
        if (md.kind == kLedge) {
            const float Ytop = (my0 + y) * 16.0f;
            if (has_top(c.entries)) {
                float lp[4][3], luv[4][2];
                ledge_quad(X0, Ytop, seat_height_of(md.a), md.b, lp, luv);
                draw_quad(cam, lp, luv, tex16(cell_art(x, y, kTop)), 1.0f);
            }
            if (md.fg > 0 && md.fg < 16) {   // machine, lower part: rows 0..fg-1 of both layers on the shared plane (no lid)
                MachineSpan sp[2];
                machine_spans(Ytop, md.fg, 0, sp);
                draw_upright(cam, X0, X0 + 16, machine_foot_y(Ytop, md.fg), sp[0].z0, sp[0].z1, tex16(cell_art(x, y, kBoth)), 0, 16,
                             static_cast<float>(sp[0].v0), static_cast<float>(sp[0].v1), 1.0f);
            }
            continue;
        }
        if (md.kind == kCounterTop) {
            const float Ytop = (my0 + y) * 16.0f, H = seat_height_of(md.a);
            const std::uint32_t* top_art = cell_art(x, y, kBottom);   // the counter top (the whole tile)
            float sp[4][3], suv[4][2];
            ledge_quad(X0, Ytop, H, 0, sp, suv);
            draw_quad(cam, sp, suv, tex16(top_art), 1.0f);
            const bool lower_plant = inside(x, y + 1) && cell(x, y + 1).valid && model(x, y + 1).kind == kSeat && has_top(cell(x, y + 1).entries) &&
                                     plant_continues(md.b, model(x, y + 1).b);
            if (has_top(c.entries) && md.b > 0 && md.b < 16 && !lower_plant)   // the plant alone (the combined one is drawn by the corner below)
                draw_upright(cam, X0, X0 + 16, plant_foot_y(Ytop, H), H, H + (16 - md.b), tex16(cell_art(x, y, kTop)), 0, 16,
                             static_cast<float>(md.b), 16, 1.0f);
            // end faces: from the face rows (bottom layer) of the tile in front; only at the end of the run, on the camera's side
            const std::uint32_t* face = inside(x, y + 1) && cell(x, y + 1).valid ? cell_art(x, y + 1, kBottom) : nullptr;
            for (int side = 0; side < 2 && face; ++side) {
                const int nbx = side == 0 ? x - 1 : x + 1;
                const bool joined = inside(nbx, y) && cell(nbx, y).valid &&
                                    (model(nbx, y).kind == kLedge || model(nbx, y).kind == kCounterTop);
                if (!seat_draw_side(side == 0, cam.tx, X0, joined)) continue;
                int col;
                if (!seat_side_column(face, side == 0, static_cast<int>(H), &col)) continue;
                float cp[4][3];
                counter_end_cap(side == 0 ? X0 : X0 + 16, Ytop, H, cp);
                const float u = col + 0.5f, fv = 16.0f - H;
                const float cuv[4][2] = {{u, fv}, {u, fv}, {u, 16}, {u, 16}};
                draw_quad(cam, cp, cuv, tex16(face), 0.74f);
            }
            continue;
        }
        if (md.kind == kTrunk) {
            // standing bark of a terminal front (centre cell only): rows 0..4 of the keyed art without the oval green, grounded on its own last row like the isolated tree
            if ((c.id & 1023) == 0x2A4) {
                grass_px.emplace_back();
                std::uint32_t* bark = grass_px.back().data();
                mask_tree_foot(cell_art(x, y, kKeyed), true, bark);
                const float gap = stand_foot_gap(trailing_empty_rows_w(bark, 16, 16));
                const float front = (my0 + y + 1) * 16.0f - gap, hh = 16.0f - gap;
                if (hh > 0.0f) {
                    draw_upright(cam, X0, X0 + 16, front, 0, hh, tex16(bark), 0, 16, 0, hh, 1.0f);
                }
            }
            continue;
        }
        if (md.kind == kGateRoof) {
            // gate roof cell (experimental): the roof art as a height-field quad over the grass floor
            const std::size_t vw = static_cast<std::size_t>(mw) + 1, o = static_cast<std::size_t>(y) * vw + x;
            if (gate_vh.empty()) continue;
            float tp[4][3], tuv[4][2];
            canopy_cell_quad(X0, (my0 + y) * 16.0f, gate_vh[o], gate_vh[o + 1], gate_vh[o + vw + 1], gate_vh[o + vw], tp, tuv);
            draw_quad(cam, tp, tuv, tex16(cell_art(x, y, kBoth)), 1.0f);
            continue;
        }
        if (md.kind == kCanopy) {
            // dense canopy cell (experimental): the cell's art as a height-field quad, no walls (see canopy_vertex_heights)
            const std::uint32_t* art = cell_art(x, y, kKeyed);
            const std::size_t vw = static_cast<std::size_t>(mw) + 1, o = static_cast<std::size_t>(y) * vw + x;
            float tp[4][3], tuv[4][2];
            if (!canopy_vh_on) continue;
            canopy_cell_quad(X0, (my0 + y) * 16.0f, canopy_vh[o], canopy_vh[o + 1], canopy_vh[o + vw + 1], canopy_vh[o + vw], tp, tuv);
            draw_quad(cam, tp, tuv, tex16(art), 1.0f);
            continue;
        }
        if (md.kind == kPlanter) {
            // low planter below a wall row: its top (art rows 0..b-1) lies flat at height a, its front (rows b..b+a-1) stands; the ends are closed
            const float Ytop = (my0 + y) * 16.0f, Hp = md.a > 0 ? static_cast<float>(md.a) : 6.0f, bp = static_cast<float>(md.b);
            const std::uint32_t* art = cell_art(x, y, kKeyed);
            float tp[4][3], tuv[4][2], fq[4][3], fuv[4][2];
            planter_top_quad(X0, Ytop, 0.0f, bp, Hp, tp, tuv);
            draw_quad(cam, tp, tuv, tex16(art), 1.0f);
            planter_face_quad(X0, Ytop, bp, Hp, fq, fuv);
            draw_quad(cam, fq, fuv, tex16(art), 0.92f);
            for (int side = 0; side < 2; ++side) {
                const int nbx = side == 0 ? x - 1 : x + 1;
                const bool joined = inside(nbx, y) && cell(nbx, y).valid && model(nbx, y).kind == kPlanter;
                if (!seat_draw_side(side == 0, cam.tx, X0, joined)) continue;
                int col;
                if (!planter_side_column(art, md.b, static_cast<int>(Hp), side == 0, &col)) continue;
                float cp[4][3];
                planter_end_quad(side == 0 ? X0 : X0 + 16, Ytop + static_cast<float>(md.fg) - 16.0f + Hp, Ytop + bp + Hp, Hp, cp);
                const float cu = col + 0.5f;
                const float cuv[4][2] = {{cu, bp}, {cu, bp}, {cu, bp + Hp}, {cu, bp + Hp}};
                draw_quad(cam, cp, cuv, tex16(art), 0.74f);
            }
            continue;
        }
        if (md.kind == kSeat) {
            const float Ybot = (my0 + y + 1) * 16.0f;
            const std::uint32_t* art = cell_art(x, y, md.art == 2 ? kBottom : (has_top(c.entries) ? kTop : kKeyed));
            const float seat_h = seat_height_of(md.a);
            const SeatFaces sf = seat_geometry(X0, Ybot, seat_h);
            const int fv = 16 - static_cast<int>(seat_h);
            const float fuv[4][2] = {{0, static_cast<float>(fv)}, {16, static_cast<float>(fv)}, {16, 16}, {0, 16}};
            const float tuv[4][2] = {{0, 0}, {16, 0}, {16, static_cast<float>(fv)}, {0, static_cast<float>(fv)}};
            draw_quad(cam, sf.top, tuv, tex16(art), 1.0f);       // the seat: the art's upper rows laid flat
            draw_quad(cam, sf.front, fuv, tex16(art), 0.90f);    // its front: the bottom rows (base / rim)
            for (int side = 0; side < 2; ++side) {
                const int nbx = side == 0 ? x - 1 : x + 1;
                const bool joined = md.a > 0 && inside(nbx, y) && cell(nbx, y).valid && model(nbx, y).kind == kSeat && model(nbx, y).a == md.a;
                if (!seat_draw_side(side == 0, cam.tx, X0, joined)) continue;   // the camera's side, ends of a run only
                int col;
                if (!seat_side_column(art, side == 0, static_cast<int>(seat_h), &col)) continue;
                const float u = col + 0.5f;
                const float suv[4][2] = {{u, static_cast<float>(fv)}, {u, static_cast<float>(fv)}, {u, 16}, {u, 16}};
                draw_quad(cam, side == 0 ? sf.left : sf.right, suv, tex16(art), 0.74f);
            }
            if (md.b > 0 && md.b < 16 && has_top(c.entries)) {   // the flower pot: top layer rows 0..b-1, upright on the top face
                grass_px.emplace_back();   // stable until the flush
                std::uint32_t* fl = grass_px.back().data();
                std::fill(fl, fl + 256, 0u);
                const std::uint32_t* tp = cell_art(x, y, kTop);
                std::copy(tp, tp + md.b * 16, fl);
                const float foot = flower_foot_y(Ybot, seat_h, md.b);
                draw_upright(cam, X0, X0 + 16, foot, seat_h, seat_h + md.b, tex16(fl), 0, 16, 0, static_cast<float>(md.b), 1.0f);
                // the counter end above carries the rest of the same plant (its rows b..15): one plane, one plant
                if (inside(x, y - 1) && cell(x, y - 1).valid && model(x, y - 1).kind == kCounterTop && has_top(cell(x, y - 1).entries) &&
                    plant_continues(model(x, y - 1).b, md.b))
                    draw_upright(cam, X0, X0 + 16, foot, seat_h + md.b, seat_h + md.b + (16 - model(x, y - 1).b), tex16(cell_art(x, y - 1, kTop)), 0, 16,
                                 static_cast<float>(model(x, y - 1).b), 16, 1.0f);
            }
            if (indoor && md.a == 0) {   // stools only: a shadow per cell along the counter strip would stack unevenly
                const ContactShadow cs = contact_shadow(X0 + 1, X0 + 15, Ybot);
                ground_shadow(cs.cx, cs.cy, cs.rx, cs.ry, 0.28f);
            }
            continue;
        }
        if (md.kind == kStand) {
            // One object per vertical run of stand cells, upright at its front.
            if (md.a == kFence || md.a == kFenceBrownFront || md.a == kFenceBrownNS) {
                auto fence = [&](int xx, int yy) { return inside(xx, yy) && cell(xx, yy).valid && model(xx, yy).kind == kStand && model(xx, yy).a == kFence; };
                int c0, c1;
                const std::uint32_t* fpx = cell_art(x, y, has_top(c.entries) ? kTop : kKeyed);
                if (fence_piece_is_ns(md, fence(x, y - 1), fence(x, y + 1), fence(x - 1, y), fence(x + 1, y)) && fence_columns(fpx, &c0, &c1)) {
                    float p[4][3], uv[4][2];
                    fence_panel(X0 + 8, (my0 + y) * 16.0f, c0, c1, std::min(14.0f, static_cast<float>(c1 - c0 + 1)), p, uv);
                    draw_quad(cam, p, uv, tex16(fpx), 0.85f);   // shaded like the building sides
                    continue;
                }
            }
            int bottom = y;
            while (stand(x, bottom + 1) && continues_stand_run(model(x, bottom + 1))) ++bottom;
            // a small object drawn in the upper rows of its cell (the bedroom console) stands on the floor at its own lowest art row
            const FieldCell& bc = cell(x, bottom);
            const int foot_skip = model(x, bottom).skip;   // baked ground shadow rows (Pewter brown fence): the object stands above them
            const float gap = foot_skip > 0 ? static_cast<float>(foot_skip) : stand_foot_gap(trailing_empty_rows(cell_art(x, bottom, has_top(bc.entries) ? kTop : kKeyed)));
            const float front = (my0 + bottom + 1) * 16.0f - gap;
            float z0, z1, v1;
            stand_span(bottom - y, gap, &z0, &z1, &v1);
            const std::uint32_t* px = cell_art(x, y, has_top(c.entries) ? kTop : kKeyed);
            if (md.fg > 0) {   // measured: only the rows from fg are the object, the rest is floor
                grass_px.emplace_back();
                mask_foreground(px, md.fg, grass_px.back().data());
                px = grass_px.back().data();
            }
            if (md.skip > 0 && bottom == y) {   // the shadow rows become a flat decal on the ground, the rest the upright object
                grass_px.emplace_back(); grass_px.emplace_back();
                std::uint32_t* up = grass_px[grass_px.size() - 2].data();
                std::uint32_t* sh = grass_px[grass_px.size() - 1].data();
                fence_foot_split(px, md.skip, up, sh);
                px = up;
                const float fy = (my0 + y) * 16.0f + 16.0f - md.skip;
                const float dp[4][3] = {{X0, fy, 0.02f}, {X0 + 16, fy, 0.02f}, {X0 + 16, fy + md.skip, 0.02f}, {X0, fy + md.skip, 0.02f}};
                const float duv[4][2] = {{0, 16.0f - md.skip}, {16, 16.0f - md.skip}, {16, 16}, {0, 16}};
                draw_quad(cam, dp, duv, tex16(sh), 1.0f);
            }
            draw_upright(cam, X0, X0 + 16, front, z0, z1, tex16(px), 0, 16, 0, v1, 1.0f);
            // Indoor furniture sits on the floor: a soft contact shadow at the foot of the run.
            if (indoor && bottom == y && md.a != kFence) {
                const ContactShadow cs = contact_shadow(X0 + 2, X0 + 14, front);
                ground_shadow(cs.cx, cs.cy, cs.rx, cs.ry, 0.30f);
            }
        } else if (md.kind == kGrass) {
            // blades: each 8x8 tile's dominant colour (the soil between blades) keyed out
            grass_px.emplace_back();
            std::uint32_t* blades = grass_px.back().data();
            std::copy_n(cell_art(x, y, kBoth), 256, blades);
            for (int q = 0; q < 4; ++q) {
                int count[16] = {};
                for (int py = 0; py < 8; ++py) for (int px = 0; px < 8; ++px) ++count[bg_index(m.vram, cb, c.entries[q], px, py)];
                const int fill = static_cast<int>(std::max_element(count, count + 16) - count);
                if (count[fill] < 20) continue;
                for (int py = 0; py < 8; ++py) for (int px = 0; px < 8; ++px)
                    if (bg_index(m.vram, cb, c.entries[q], px, py) == fill) blades[((q >> 1) * 8 + py) * 16 + (q & 1) * 8 + px] = 0;
            }
            draw_upright(cam, X0, X0 + 16, (wy + 1) * 16.0f, 0, 14, tex16(blades), 0, 16, 0, 16, 1.0f);
        }
    }

    // ── buildings: one box per connected group of roof/wall tiles ────────
    {
        std::vector<std::uint8_t> seen(cells.size(), 0);
        auto is_building = [&](int x, int y) {
            return inside(x, y) && cell(x, y).valid && (kind_at(x, y) == kRoof || kind_at(x, y) == kWall);
        };
        for (int sy0 = 0; sy0 < mh; ++sy0) for (int sx0 = 0; sx0 < mw; ++sx0) {
            if (!is_building(sx0, sy0) || seen[sy0 * mw + sx0] || !(layer_mask() & 8)) continue;
            std::vector<int> comp{sy0 * mw + sx0};
            seen[sy0 * mw + sx0] = 1;
            int bx0 = sx0, bx1 = sx0, by0 = sy0, by1 = sy0;
            for (std::size_t h = 0; h < comp.size(); ++h) {
                const int x = comp[h] % mw, y = comp[h] / mw;
                bx0 = std::min(bx0, x); bx1 = std::max(bx1, x); by0 = std::min(by0, y); by1 = std::max(by1, y);
                const int nb[4][2] = {{x - 1, y}, {x + 1, y}, {x, y - 1}, {x, y + 1}};
                for (const auto& n : nb)
                    if (is_building(n[0], n[1]) && !seen[n[1] * mw + n[0]]) { seen[n[1] * mw + n[0]] = 1; comp.push_back(n[1] * mw + n[0]); }
            }
            int wall_rows = 0;   // wall rows counted up from the front
            for (int y = by1; y >= by0; --y) {
                bool any = false;
                for (int x = bx0; x <= bx1; ++x) any |= is_building(x, y) && kind_at(x, y) == kWall;
                if (!any) break;
                ++wall_rows;
            }
            int wall_top_row = 16;   // top wall row's highest opaque art row over its cells
            if (wall_rows > 0)
                for (int x = bx0; x <= bx1; ++x)
                    if (is_building(x, by1 - wall_rows + 1) && kind_at(x, by1 - wall_rows + 1) == kWall)
                        wall_top_row = std::min(wall_top_row, wall_cell_top_row(model(x, by1 - wall_rows + 1).face, first_opaque_row(cell_art(x, by1 - wall_rows + 1, kKeyed))));
            if (wall_top_row >= 16) wall_top_row = 0;
            // baked floor shadow rows under the front face (a bed foot): the face stands on the row above them, so the whole box is raised by them
            int box_skip = 0;
            for (int x = bx0; x <= bx1; ++x)
                if (is_building(x, by1) && kind_at(x, by1) == kWall) box_skip = std::max(box_skip, static_cast<int>(model(x, by1).skip));
            const float base = (my0 + by1 + 1) * 16.0f - box_skip, H = (by1 - by0 + 1) * 16.0f,
                        wall_h = std::max(0.0f, box_wall_height(wall_rows, wall_top_row) - box_skip);
            const float lean = indoor ? 1.0f : 0.75f;   // house roofs slope, furniture tops are flat
            // Lowest contiguous building cell of every cell's column, measured once
            // per component (bottom-up), for the recessed wall faces below.
            const int cw = bx1 - bx0 + 1, ch = by1 - by0 + 1;
            std::vector<unsigned char> in_box(static_cast<std::size_t>(cw) * ch, 0);
            std::vector<int> col_bottom(static_cast<std::size_t>(cw) * ch, -1);
            for (int y = by0; y <= by1; ++y) for (int x = bx0; x <= bx1; ++x)
                in_box[static_cast<std::size_t>(y - by0) * cw + (x - bx0)] = is_building(x, y) ? 1 : 0;
            column_bottoms(in_box.data(), cw, ch, col_bottom.data());
            for (int i : comp) {
                const int x = i % mw, y = i / mw;
                const bool wall = kind_at(x, y) == kWall;
                const int cb = by0 + col_bottom[static_cast<std::size_t>(y - by0) * cw + (x - bx0)];
                // A wall column that ends above the box front (a recessed door) is
                // measured from its own bottom; roofs never are.
                const bool recessed = wall && cb < by1 && kind_at(x, cb) == kWall;
                float cbase = base, cwall_h = wall_h;
                if (recessed) {
                    int rows = 0;
                    while (cb - rows >= by0 && is_building(x, cb - rows) && kind_at(x, cb - rows) == kWall) ++rows;
                    const int top_row = wall_cell_top_row(model(x, cb - rows + 1).face, first_opaque_row(cell_art(x, cb - rows + 1, kKeyed)));
                    cbase = (my0 + cb + 1) * 16.0f - model(x, cb).skip;
                    cwall_h = box_wall_height(rows, top_row >= 16 ? 0 : top_row);
                }
                const FaceParams fp = face_params(wall, recessed, base, wall_h, cbase, cwall_h);
                const float X0 = (mx0 + x) * 16.0f, u0 = fp.base - (my0 + y + 1) * 16.0f;
                float p[4][3];
                front_quad(fp.base, fp.wall_h, lean, X0, u0, p);
                const float uv[4][2] = {{0, 0}, {16, 0}, {16, 16}, {0, 16}};
                const std::uint32_t* face_art = cell_art(x, y, box_art_top_only(model(x, y), has_top(cell(x, y).entries)) ? kTop : kKeyed);
                FrontPiece pieces[2];
                const TileModel& fm = model(x, y);
                const int band = wall && fm.band > 0 && fm.band < 16 ? fm.band : 0;   // rows band..15 are a planter top, not wall
                const float uband = u0 + 16.0f - band;
                const int npieces = front_pieces(u0, fp.wall_h, pieces);
                for (int pc = 0; pc < npieces; ++pc) {
                    FrontPiece pcx = pieces[pc];
                    if (band) { pcx.ua = std::max(pcx.ua, uband); if (pcx.ub <= pcx.ua + 0.01f) continue; }
                    float pp[4][3];
                    front_piece_quad(fp.base, fp.wall_h, lean, X0, pcx.ua, pcx.ub, pp);
                    const float vt = 16.0f - (pcx.ub - u0), vb = 16.0f - (pcx.ua - u0);
                    const float puv[4][2] = {{0, vt}, {16, vt}, {16, vb}, {0, vb}};
                    draw_quad(cam, pp, puv, tex16(face_art), wall ? 1.0f : 0.97f);
                }
                if (band) {
                    const float H = fm.a > 0 ? fm.a : 6.0f;
                    // the wall's last art row continues down to the planter top (the bit of wall the old picture hid behind the flowers)
                    if (uband > H + 0.01f) {
                        float pp[4][3];
                        front_piece_quad(fp.base, fp.wall_h, lean, X0, H, uband, pp);
                        const float vc = band - 0.5f;
                        const float puv[4][2] = {{0, vc}, {16, vc}, {16, vc}, {0, vc}};
                        draw_quad(cam, pp, puv, tex16(face_art), 1.0f);
                    }
                    float tp[4][3], tuv[4][2];
                    planter_top_quad(X0, (my0 + y) * 16.0f, static_cast<float>(band), 16.0f, H, tp, tuv);
                    draw_quad(cam, tp, tuv, tex16(face_art), 1.0f);
                }
                if (box_diag_cell(mx0 + x, my0 + y)) {
                    static std::vector<long> seen_diag;
                    const long key = (static_cast<long>(mx0 + x) << 20) ^ static_cast<long>(my0 + y);
                    if (std::find(seen_diag.begin(), seen_diag.end(), key) == seen_diag.end()) {
                        seen_diag.push_back(key);
                        std::fprintf(stderr, "[box-diag] face cell=(%d,%d) id=%03X kind=%d art=%s recessed=%d cb=%d box=[%d..%d]x[%d..%d] "
                                     "wall_rows=%d wall_top_row=%d base=%.1f wall_h=%.1f lean=%.2f face_base=%.1f face_wall_h=%.1f "
                                     "corners=(%.1f,%.1f,%.1f)(%.1f,%.1f,%.1f)(%.1f,%.1f,%.1f)(%.1f,%.1f,%.1f)\n",
                                     mx0 + x, my0 + y, cell(x, y).id & 1023, kind_at(x, y),
                                     box_art_top_only(model(x, y), has_top(cell(x, y).entries)) ? "top" : "keyed", recessed ? 1 : 0, my0 + cb,
                                     mx0 + bx0, mx0 + bx1, my0 + by0, my0 + by1, wall_rows, wall_top_row, base, wall_h, lean, fp.base, fp.wall_h,
                                     p[0][0], p[0][1], p[0][2], p[1][0], p[1][1], p[1][2], p[2][0], p[2][1], p[2][2], p[3][0], p[3][1], p[3][2]);
                        std::fflush(stderr);
                    }
                }
                // Lintel and the two side reveals close the opening from the face's
                // top art row and the neighbouring wall-edge art; depth-tested quads.
                if (recessed && !(y > by0 && is_building(x, y - 1) && kind_at(x, y - 1) == kWall)) {
                    float q[4][3];
                    lintel_quad(p, X0, base, wall_h, q);
                    if (q[0][1] < base - 0.5f || q[0][2] < wall_h - 0.5f) {
                        const float luv[4][2] = {{0, 0}, {16, 0}, {16, 0}, {0, 0}};
                        draw_quad(cam, q, luv, tex16(face_art), 0.88f);
                    }
                }
                if (recessed && y == cb) {
                    for (int side = 0; side < 2; ++side) {
                        const int nx = side ? x + 1 : x - 1;
                        const float RX = side ? X0 + 16 : X0;
                        if (side ? cam.tx > RX : cam.tx < RX) continue;   // the reveal faces into the opening
                        for (int k = 0; k < reveal_segments(wall_h); ++k) {
                            const int yy = by1 - k;   // the neighbour in the same wall row, counted from the box front
                            if (yy < by0 || !is_building(nx, yy) || kind_at(nx, yy) != kWall) continue;
                            const std::uint32_t* nart = cell_art(nx, yy, kKeyed);
                            int ncol;
                            if (!pick_side_column(nart, side == 0 ? false : true, &ncol)) continue;
                            float rp[4][3], ruv[4][2];
                            reveal_segment(RX, cbase, cwall_h, base, wall_h, k, rp);
                            reveal_segment_uv(rp, k, model(nx, yy).planter, ncol + 0.5f, ruv);
                            draw_quad(cam, rp, ruv, tex16(nart), 0.80f);
                            if (box_diag_cell(mx0 + nx, my0 + yy)) {
                                static std::vector<long> seen_rev;
                                const long rkey = (static_cast<long>(mx0 + x) << 24) ^ (static_cast<long>(my0 + y) << 8) ^ (side * 16 + k);
                                if (std::find(seen_rev.begin(), seen_rev.end(), rkey) == seen_rev.end()) {
                                    seen_rev.push_back(rkey);
                                    std::fprintf(stderr, "[box-diag] reveal door=(%d,%d) side=%d seg=%d neighbour=(%d,%d) id=%03X col=%d planter=%d "
                                                 "v=%.1f..%.1f corners=(%.1f,%.1f,%.1f)(%.1f,%.1f,%.1f)(%.1f,%.1f,%.1f)(%.1f,%.1f,%.1f)\n",
                                                 mx0 + x, my0 + y, side, k, mx0 + nx, my0 + yy, cell(nx, yy).id & 1023, ncol, model(nx, yy).planter,
                                                 ruv[1][1], ruv[2][1], rp[0][0], rp[0][1], rp[0][2], rp[1][0], rp[1][1], rp[1][2],
                                                 rp[2][0], rp[2][1], rp[2][2], rp[3][0], rp[3][1], rp[3][2]);
                                    std::fflush(stderr);
                                }
                            }
                        }
                    }
                }
            }
            // Contact shadow on the floor/grass in front of the building.
            {
                const float bx_a = (mx0 + bx0) * 16.0f, bx_b = (mx0 + bx1 + 1) * 16.0f;
                const ContactShadow cs = contact_shadow(bx_a, bx_b, base);
                ground_shadow(cs.cx, cs.cy, cs.rx, cs.ry, indoor ? 0.38f : 0.30f);
            }
            // The visible side: a column of the edge wall art extruded over the
            // wall rows and a column of its roof art on the gable, in shade.
            const float X0 = (mx0 + bx0) * 16.0f + 1, X1 = (mx0 + bx1 + 1) * 16.0f - 1;
            if (cam.tx > X0 && cam.tx < X1) continue;
            const int ex = cam.tx <= X0 ? bx0 : bx1;
            const float X = cam.tx <= X0 ? X0 : X1;
            // The side ends where the edge column's art does: roof corners have
            // transparent padding, and a full-height gable would poke above it.
            float side_h = H;
            for (int y = by0; y <= by1; ++y) {
                if (!is_building(ex, y)) continue;
                const std::uint32_t* px = cell_art(ex, y, box_art_top_only(model(ex, y), has_top(cell(ex, y).entries)) ? kTop : kKeyed);
                const int col = ex == bx0 ? 1 : 14;
                int row = 0;
                while (row < 16 && !(px[row * 16 + col] >> 24)) ++row;
                if (row == 16) continue;
                side_h = std::max(wall_h, base - ((my0 + y) * 16.0f + row));
                break;
            }
            float back_y, top_z;
            wall_surface(base, wall_h, lean, side_h, &back_y, &top_z);
            int roof_y = by0;
            while (roof_y < by1 && !is_building(ex, roof_y)) ++roof_y;
            const bool left_edge = ex == bx0;
            const float side_k = indoor ? 0.72f : 0.78f;
            // Wall rows from the front: each row's own art column, so a two-row
            // wall is two rows of siding rather than one stretched tile.
            for (int r = 0; r < wall_rows; ++r) {
                const int yy = by1 - r;
                int xx = -1;
                for (int x = ex; x >= bx0 && x <= bx1; x += left_edge ? 1 : -1)
                    if (is_building(x, yy) && kind_at(x, yy) == kWall) { xx = x; break; }
                if (xx < 0) continue;
                const std::uint32_t* art = cell_art(xx, yy, kKeyed);
                int col;
                if (!pick_side_column(art, left_edge, &col)) continue;
                const int top_row = r == wall_rows - 1 ? std::max(wall_top_row, column_top(art, col)) : 0;
                const float seg = r == wall_rows - 1 ? static_cast<float>(16 - top_row) : 16.0f;
                if (seg < 1.0f) continue;
                float z0 = r * 16.0f - box_skip, z1 = z0 + seg, vbot = 16.0f;
                const float u = col + 0.5f;
                if (z0 < 0.0f) { vbot = 16.0f + z0; z0 = 0.0f; }   // rows under the raised base (baked floor shadow) are not drawn
                if (z1 <= z0 + 0.01f) continue;
                // a wall cell with a planter band: only its wall rows are siding (the flowers below are the planter's); the last siding row repeats
                // down to the floor so the side wall is closed all the way along the house body, not just above the planter top
                const TileModel& sm = model(xx, yy);
                const int sband = sm.band > 0 && sm.band < 16 && sm.band > top_row ? sm.band : 0;
                SideSeg segs[2];
                int nseg = 0;
                if (sband) nseg = side_band_segments(z1, top_row, sband, z0, segs);
                else { segs[0] = {z0, z1, static_cast<float>(top_row), vbot}; nseg = 1; }
                for (int sg = 0; sg < nseg; ++sg) {
                    const float sz0 = segs[sg].z0, sz1 = segs[sg].z1;
                    const float p[4][3] = {{X, back_y, sz1}, {X, base, sz1}, {X, base, sz0}, {X, back_y, sz0}};
                    const float uv[4][2] = {{u, segs[sg].v0}, {u, segs[sg].v0}, {u, segs[sg].v1}, {u, segs[sg].v1}};
                    draw_quad(cam, p, uv, tex16(art), side_k);
                }
            }
            if (top_z > wall_h + 0.5f) {
                const std::uint32_t* art = cell_art(ex, roof_y, box_art_top_only(model(ex, roof_y), has_top(cell(ex, roof_y).entries)) ? kTop : kKeyed);
                int col;
                if (pick_side_column(art, left_edge, &col)) {
                    const float u = col + 0.5f;
                    const float p[4][3] = {{X, back_y, top_z}, {X, base, wall_h}, {X, base, wall_h}, {X, back_y, wall_h}};
                    const float uv[4][2] = {{u, 0}, {u, 16}, {u, 16}, {u, 16}};
                    draw_quad(cam, p, uv, tex16(art), indoor ? 0.66f : 0.70f);
                }
            }
        }
    }

    // ── indoor shell: side walls and the floor's front edge ──────────────
    if (indoor && room_top >= 0) {
        // FRLG_ROOM_DIAG=1 (off by default): one dump per room of what the shell and back wall are made of.
        static const bool room_diag = std::getenv("FRLG_ROOM_DIAG") != nullptr;
        static std::uint32_t diag_layout = 0xFFFFFFFFu;
        if (room_diag && diag_layout != layout) {
            diag_layout = layout;
            std::fprintf(stderr, "[room-diag] layout=%08X snapshot=%dx%d origin=(%d,%d) room_top=%d rx=[%d..%d] bottom=%d (world x %d..%d)\n",
                         layout, mw, mh, mx0, my0, room_top, room_rx0, room_rx1, room_bottom, mx0 + room_rx0, mx0 + room_rx1);
            for (int ry = room_top - 1; ry <= room_top + 2 && ry < mh; ++ry) {
                if (ry < 0) continue;
                for (int xx = 0; xx < mw; ++xx) {
                    if (!cell(xx, ry).valid) continue;
                    const std::uint32_t* a = cell_art(xx, ry, kBoth);
                    const VoidInfo vi = void_info(a);
                    int opaque = 0, top_row = 16;
                    for (int k = 0; k < 256; ++k) if (a[k] >> 24) { ++opaque; top_row = std::min(top_row, k / 16); }
                    std::fprintf(stderr, "[room-diag] row=%d (%s) x=%d world=(%d,%d) id=%03X kind=%d opaque=%d void=%d void_left=%d void_right=%d top_row=%d corner_wedge=%d in_rx=%d\n",
                                 ry, back_wall(ry) ? "back wall" : "other", xx, mx0 + xx, my0 + ry, cell(xx, ry).id & 1023, kind_at(xx, ry), opaque,
                                 vi.count, vi.left_edge ? 1 : 0, vi.right_edge ? 1 : 0, top_row,
                                 (xx == room_rx0 && is_corner_wedge(a, true)) || (xx == room_rx1 && is_corner_wedge(a, false)) ? 1 : 0,
                                 xx >= room_rx0 && xx <= room_rx1 ? 1 : 0);
                }
            }
            for (int side = 0; side < 2; ++side) {
                const int ex = side ? room_rx1 : room_rx0;
                std::fprintf(stderr, "[room-diag] side=%d plane_x=%d rows=%d..%d last_floor_row=%d common_bottom=%d\n", side,
                             (mx0 + (side ? room_rx1 + 1 : room_rx0)) * 16, room_top + 2, std::min(last_floor_row(ex), room_bottom), last_floor_row(ex), room_bottom);
            }
            std::fflush(stderr);
        }
    }
    if (indoor && room_top >= 0 && (layer_mask() & 16)) {
        const int rx0 = room_rx0, rx1 = room_rx1, common_bottom = room_bottom;   // common_bottom: side walls only
        auto last_row = last_floor_row;
        // side walls reuse the commonest (plain) tile of each back-wall row
        for (int side = 0; side < 2 && rx1 >= rx0; ++side) {
            const int ex = side ? rx1 : rx0;
            const float X = side ? (mx0 + rx1 + 1) * 16.0f : (mx0 + rx0) * 16.0f;
            const int bottom = std::min(last_row(ex), common_bottom);
            for (int half = 0; half < 2; ++half) {
                const std::uint32_t* px = wall_material(room_top + 1 - half);
                if (!px) continue;
                for (int y = room_top + 2; y <= bottom; ++y) {
                    const float Y = (my0 + y) * 16.0f, za = half * 16.0f;
                    const float p[4][3] = {{X, Y, za + 16}, {X, Y + 16, za + 16}, {X, Y + 16, za}, {X, Y, za}};
                    const float uv[4][2] = {{0, 0}, {16, 0}, {16, 16}, {0, 16}};
                    draw_quad(cam, p, uv, tex16(px), side ? 0.70f : 0.80f);
                }
            }
        }
        static std::vector<std::uint32_t> lip(1, IM_COL32(48, 38, 28, 255));
        const Tex lip_tex{1, 1, lip.data()};
        for (int x = rx0; x <= rx1; ++x) {
            const int b = last_row(x);
            if (b < 0) continue;
            const float X0 = (mx0 + x) * 16.0f, Yf = (my0 + b + 1) * 16.0f;
            draw_upright(cam, X0, X0 + 16, Yf, -10, 0, lip_tex, 0, 1, 0, 1, 1.0f);
        }
    }

    // ── sprites ──────────────────────────────────────────────────────────
    ActorSig actor_sigs[16];
    const int n_actor_sigs = actor_signatures(m.ptr(0x02036E38, 16 * 36), m.ptr(0x0202063C, 64 * 68), actor_sigs, 16);   // gObjectEvents, gSprites (read only)
    // FRLG_ACTOR_DIAG=1 (off by default, read only): per drawn OAM sprite whether the actor match took it; summarised once a second (see the print below).
    static const bool actor_diag = std::getenv("FRLG_ACTOR_DIAG") != nullptr;
    int dg_matched[4] = {0, 0, 0, 0}, dg_unmatched_tall = 0;   // matched by size: 16x16, 16x32, 32x32, other
    struct DiagSusp { int oam; unsigned hw0, hw1, hw2; ActorSig near_sig; int near_bits; };
    static DiagSusp dg_susp[3];
    static int dg_n_susp = 0;
    for (std::size_t i = 0; i < sprites.size() && (layer_mask() & 32); ++i) {
        const SpriteImg& s = sprites[i];
        const Tex t{s.w, s.h, sprite_px[i].data()};
        const float X0 = origin_x + s.x, Y0 = origin_y + s.y;
        if (s.decal) {   // reflections lie flat
            const float p[4][3] = {{X0, Y0, 0.1f}, {X0 + s.w, Y0, 0.1f}, {X0 + s.w, Y0 + s.h, 0.1f}, {X0, Y0 + s.h, 0.1f}};
            const float uv[4][2] = {{0, 0}, {static_cast<float>(s.w), 0}, {static_cast<float>(s.w), static_cast<float>(s.h)}, {0, static_cast<float>(s.h)}};
            draw_quad(cam, p, uv, t, 1.0f, 0.6f);
            continue;
        }
        // FRLG_SPRITE_DIAG=1 (off by default): about once a second, how many framebuffer pixels one art pixel of each sprite covers on screen
        // (horizontal and vertical, measured at the feet / over the height). Whole numbers = even pixels; the zoom nudge only reaches an integer at the focus when
        // its 4 % tolerance allows it, and the perspective makes the head wider than the feet, so the values are expected to be fractional.
        {
            static const bool sprite_diag = std::getenv("FRLG_SPRITE_DIAG") != nullptr;
            static int diag_frame = 0;
            if (sprite_diag && i == 0) ++diag_frame;
            if (sprite_diag && diag_frame % 120 == 1) {
                const float feet_y = Y0 + s.h;
                float ax, ay, az, bx, by, bz, cx2, cy2, cz2;
                if (cam.project(X0, feet_y, 0, &ax, &ay, &az) && cam.project(X0 + s.w, feet_y, 0, &bx, &by, &bz) &&
                    cam.project(X0, feet_y, static_cast<float>(s.h), &cx2, &cy2, &cz2))
                    std::fprintf(stderr, "[sprite-diag] oam=%d native=(%d,%d) size=%dx%d fb_px_per_art_px x=%.3f y=%.3f depth=%.1f rs=%.3f\n", s.index, s.x, s.y,
                                 s.w, s.h, (bx - ax) * g_raster.s / s.w, (ay - cy2) * g_raster.s / s.h, az, g_raster.s);
            }
        }
        // default ON for characters (player / NPC object events) only; FRLG_3D_SPRITE_RECT=0 is the explicit diagnostic fallback (the perspective trapezoid)
        static const bool sprite_rect_on = [] { const char* e = std::getenv("FRLG_3D_SPRITE_RECT"); return !(e && e[0] == '0'); }();
        const std::uint8_t* hw = m.oam + s.index * 8;
        const unsigned hw0 = hw[0] | (hw[1] << 8), hw1 = hw[2] | (hw[3] << 8), hw2 = hw[4] | (hw[5] << 8);
        const bool is_actor = oam_is_actor(hw0, hw1, hw2, actor_sigs, n_actor_sigs);
        const bool sprite_rect = sprite_rect_on && is_actor;
        if (actor_diag) {
            if (is_actor) ++dg_matched[s.w == 16 && s.h == 16 ? 0 : s.w == 16 && s.h == 32 ? 1 : s.w == 32 && s.h == 32 ? 2 : 3];
            else if (s.w == 16 && s.h == 32) {   // heuristic only: a tall 16x32 sprite is usually a character, this is not proof
                ++dg_unmatched_tall;
                if (dg_n_susp < 3) {
                    DiagSusp d{s.index, hw0, hw1, hw2, ActorSig{0, 0, 0}, 99};
                    for (int k = 0; k < n_actor_sigs; ++k) {   // the guest word set that differs from the hardware words in the fewest bits
                        const unsigned diff = (actor_sigs[k].shape ^ (hw0 & 0xC000u)) | (actor_sigs[k].size ^ (hw1 & 0xC000u)) | (actor_sigs[k].attr2 ^ hw2);
                        int bits = 0;
                        for (unsigned v = diff; v; v &= v - 1) ++bits;
                        if (bits < d.near_bits) { d.near_bits = bits; d.near_sig = actor_sigs[k]; }
                    }
                    dg_susp[dg_n_susp++] = d;
                }
            }
        }
        // lower OAM index wins ties (it is drawn on top on the GBA)
        const float feet = Y0 + s.h + (128 - s.index) * 0.002f;
        draw_upright(cam, X0, X0 + s.w, feet, 0, static_cast<float>(s.h), t, 0, static_cast<float>(s.w), 0,
                     static_cast<float>(s.h), 1.0f, s.half ? 0.55f : 1.0f, true, sprite_rect);   // sprites: feet corner on the pixel grid (stable texel phase)
    }

    if (actor_diag) {
        static double t0 = -1.0;
        static int frames = 0, tall_max = 0, tall_frames = 0, last_matched[4] = {}, last_sigs = 0;
        ++frames;
        tall_max = std::max(tall_max, dg_unmatched_tall);
        tall_frames += dg_unmatched_tall > 0 ? 1 : 0;
        std::copy(dg_matched, dg_matched + 4, last_matched);
        last_sigs = n_actor_sigs;
        if (t0 < 0.0) t0 = now;
        if (now - t0 >= 1.0) {
            std::fprintf(stderr, "[actor-diag] frames=%d active_actor_sigs=%d matched_draws_last_frame(16x16=%d 16x32=%d 32x32=%d other=%d) unmatched_tall_16x32: last_frame=%d max=%d frames_with=%d\n",
                         frames, last_sigs, last_matched[0], last_matched[1], last_matched[2], last_matched[3], dg_unmatched_tall, tall_max, tall_frames);
            for (int k = 0; k < n_actor_sigs && k < 4; ++k)
                std::fprintf(stderr, "[actor-diag]   guest sig %d: shape=%04X size=%04X attr2=%04X\n", k, actor_sigs[k].shape, actor_sigs[k].size, actor_sigs[k].attr2);
            for (int k = 0; k < dg_n_susp; ++k)
                std::fprintf(stderr, "[actor-diag]   unmatched tall oam=%d hw=(%04X,%04X,%04X) nearest guest sig shape=%04X size=%04X attr2=%04X (%d bits differ; attr2 xor=%04X)\n", dg_susp[k].oam,
                             dg_susp[k].hw0, dg_susp[k].hw1, dg_susp[k].hw2, dg_susp[k].near_sig.shape, dg_susp[k].near_sig.size, dg_susp[k].near_sig.attr2, dg_susp[k].near_bits,
                             (dg_susp[k].hw2 ^ dg_susp[k].near_sig.attr2) & 0xFFFFu);
            std::fflush(stderr);
            t0 = now; frames = 0; tall_max = 0; tall_frames = 0; dg_n_susp = 0;
        }
    }

    g_raster.flush();

    // ── hardware brightness fades (palette fades are already in the art) ─
    {
        const unsigned bld = m.io16(0x50);
        const float by = std::min(16, m.io16(0x54) & 0x1F) / 16.0f;
        float black = 0, white = 0;
        if ((bld & 0x0F) == 0x0F && !(m.io16(0) & 0xE000)) {
            if (((bld >> 6) & 3) == 3) black = by;
            if (((bld >> 6) & 3) == 2) white = by;
        }
        if (black > 0.001f || white > 0.001f)
            for (auto& p : g_raster.color) {
                p = hd::col::mix(p | IM_COL32_A_MASK, hd::col::kBlack, black);
                p = hd::col::mix(p, hd::col::kWhite, white);
            }
    }

    // ── present ──────────────────────────────────────────────────────────
    if (!g_frame || g_frame_w != fw || g_frame_h != fh) {
        if (g_frame) SDL_DestroyTexture(g_frame);
        g_frame = make_texture(fw, fh, plan.linear, true);
        g_frame_w = fw; g_frame_h = fh;
    }
    if (!g_frame) { draw_toast(ov, m, dt); return; }
    SDL_UpdateTexture(g_frame, nullptr, g_raster.color.data(), fw * 4);
#if SDL_VERSION_ATLEAST(2, 0, 12)
    SDL_SetTextureScaleMode(g_frame, plan.linear ? SDL_ScaleModeLinear : SDL_ScaleModeNearest);   // Smoothing governs the 3D frame too
#endif
    // 1:1 with the screen: an integer origin and exact size, so texels land on pixels (no resample, no half-pixel shimmer)
    ImVec2 img0 = o.P(ImVec2(0, 0)), img1 = o.P(ImVec2(vw, vh));
    if (one_to_one(fw, vw * o.s) && one_to_one(fh, vh * o.s)) {
        img0 = ImVec2(std::floor(img0.x + 0.5f), std::floor(img0.y + 0.5f));
        img1 = ImVec2(img0.x + static_cast<float>(fw), img0.y + static_cast<float>(fh));
    }
    o.dl->PushClipRect(o.P(ImVec2(0, 0)), o.P(ImVec2(vw, vh)), true);
    o.dl->AddImage(tex_id(g_frame), img0, img1, ImVec2(0, 0), ImVec2(1, 1), shade(1.0f, g_mix));
    // atmosphere: haze in the distance, soft vignette
    const int a = static_cast<int>(g_mix * 255);
    auto fade = [&](int alpha) { return alpha * a / 255; };
    if (!indoor) o.dl->AddRectFilledMultiColor(o.P(ImVec2(0, 0)), o.P(ImVec2(vw, vh * 0.42f)), IM_COL32(214, 232, 255, fade(60)),
                                               IM_COL32(214, 232, 255, fade(60)), IM_COL32(214, 232, 255, 0), IM_COL32(214, 232, 255, 0));
    o.dl->AddRectFilledMultiColor(o.P(ImVec2(0, vh * 0.78f)), o.P(ImVec2(vw, vh)), IM_COL32(0, 0, 0, 0),
                                  IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, fade(46)), IM_COL32(0, 0, 0, fade(46)));
    draw_bg0(o, m, margin, g_mix);
    o.dl->PopClipRect();
    draw_toast(ov, m, dt);
}

}  // namespace frlg
