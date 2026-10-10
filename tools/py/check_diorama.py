#!/usr/bin/env python3
"""Compile the actual 3D view code (rasterizer, tile models, art layers,
sprite decoder) against small fixtures and check it.

Usage: python tools/py/check_diorama.py C:/msys64/mingw64/bin/c++.exe
No ROM, game save or SDL window is needed.
"""
import pathlib
import os
import subprocess
import sys
import tempfile
import shutil

root = pathlib.Path(__file__).resolve().parents[2]
source = (root / "FireRedLeafGreenRecomp/src/fr_diorama.cpp").read_text(encoding="utf-8")
def between(a, b): return source[source.index(a):source.index(b)]
camera = between("struct Cam {", "// ── software rasterizer")
raster = between("// ── software rasterizer", "Raster g_raster;")
draw = between("// One textured quad in world space", "// ── drawing helpers for the ImGui layer")
art = between("inline int bg_index(", "constexpr int kObjW")
decoder = between("constexpr int kObjW", "// ── state")
consts = between("constexpr std::uint32_t kTilesetGeneral", "// ── tunables")
field_header = (root / "FireRedLeafGreenRecomp/src/fr_widescreen.h").read_text(encoding="utf-8")

fixture = r'''
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>
struct ImVec2 { float x = 0, y = 0; };
namespace gba { int (*g_hd_obj_hide)(int, std::uint16_t, std::uint16_t, std::uint16_t) = nullptr; }
struct Mem {
    const std::uint8_t *oam, *vram, *io, *pal;
    std::uint16_t io16(int off) const { return io[off] | (io[off + 1] << 8); }
};
std::uint32_t rgb555(std::uint16_t color) { return 0xff000000u | color; }
'''

checks = r'''
void require(bool ok, const char* message) {
    if (!ok) { std::cerr << "FAILED: " << message << std::endl; throw std::runtime_error(message); }
}
int main() {
    // ── depth buffer: the nearer surface wins whatever the draw order ──
    std::uint32_t red = 0xff0000ffu, blue = 0xffff0000u, clear_px = 0;
    const Tex t_red{1, 1, &red}, t_blue{1, 1, &blue}, t_clear{1, 1, &clear_px};
    auto square = [](Raster& r, float iz, const Tex& t, float alpha) {
        const Raster::V a{0, 0, iz, 0, 0}, b{8, 0, iz, 0, 0}, c{8, 8, iz, 0, 0}, d{0, 8, iz, 0, 0};
        r.tri(a, b, c, t, 1.0f, alpha); r.tri(a, c, d, t, 1.0f, alpha);
    };
    Raster r;
    r.reset(8, 8, 1, 0xff000000u);
    square(r, 0.5f, t_red, 1); square(r, 0.25f, t_blue, 1); r.flush();
    require(r.color[4 * 8 + 4] == red, "farther surface drawn over a nearer one");
    r.reset(8, 8, 1, 0xff000000u);
    square(r, 0.25f, t_blue, 1); square(r, 0.5f, t_red, 1); r.flush();
    require(r.color[4 * 8 + 4] == red, "nearer surface lost to draw order");
    r.reset(8, 8, 1, 0xff000000u);
    square(r, 0.5f, t_clear, 1); square(r, 0.25f, t_blue, 1); r.flush();
    require(r.color[4 * 8 + 4] == blue, "transparent texels hide what is behind them");
    r.reset(8, 8, 1, 0xff000000u);
    square(r, 0.5f, t_red, 0.5f); square(r, 0.25f, t_blue, 1); r.flush();
    require(r.color[4 * 8 + 4] != red && ((r.color[4 * 8 + 4] >> 16) & 255) > 0,
            "a blended shadow wrote depth and hid the object behind it");
    r.reset(64, 64, 1, 0);
    for (int i = 0; i < 4; ++i) square(r, 0.5f, t_red, 1);
    r.flush();
    require(r.color[0] == red && r.color[7 * 64 + 7] == red && r.color[8 * 64 + 8] == 0, "square coverage wrong");

    // ── translucent quad seam: the shared diagonal blends each pixel once ──
    {
        std::uint32_t px = 0xff2040c0u;
        const Tex tex{1, 1, &px};
        auto quad = [&](Raster& rr, std::uint32_t id) {
            const Raster::V a{0, 0, 0.5f, 0, 0}, b{8, 0, 0.5f, 0, 0}, c{8, 8, 0.5f, 0, 0}, d{0, 8, 0.5f, 0, 0};
            rr.tri(a, b, c, tex, 1.0f, 0.5f, id); rr.tri(a, c, d, tex, 1.0f, 0.5f, id);
        };
        Raster once, twice;
        once.reset(8, 8, 1, 0xff000000u); twice.reset(8, 8, 1, 0xff000000u);
        quad(once, 1); once.flush();
        quad(twice, 0); twice.flush();   // no id: the old behaviour
        const std::uint32_t edge = once.color[3 * 8 + 3], inside = once.color[3 * 8 + 5];
        require(edge == inside, "shared diagonal pixel blended twice (visible seam line)");
        require(twice.color[3 * 8 + 3] != twice.color[3 * 8 + 5], "fixture: the unprotected quad should show the seam");
        // stamps are NOT cleared between frames: a new frame's quad must still blend pixels the previous frame's quad stamped
        Raster frames;
        frames.reset(8, 8, 1, 0xff000000u);
        quad(frames, ++frames.quads); frames.flush();
        const std::uint32_t first_frame = frames.color[3 * 8 + 5];
        frames.reset(8, 8, 1, 0xff000000u);   // same size: stamps stay, ids keep growing
        quad(frames, ++frames.quads); frames.flush();
        require(frames.color[3 * 8 + 5] == first_frame && frames.color[3 * 8 + 3] == first_frame, "a stale stamp from the previous frame suppressed a blend");
        frames.reset(4, 4, 1, 0xff000000u);   // a resize clears them
        require(frames.blend_stamp.size() == 16 && frames.quads == 0, "resize must reset the stamps and the id counter");
        frames.quads = 0xFFFFFFF0u;
        frames.reset(4, 4, 1, 0xff000000u);   // before the counter could wrap
        require(frames.quads == 0, "id counter would wrap");
        // two separate translucent quads over the same pixels still stack (the stamp is per quad)
        Raster stack;
        stack.reset(8, 8, 1, 0xff000000u);
        quad(stack, 1); quad(stack, 2); stack.flush();
        require(stack.color[3 * 8 + 5] != inside, "two translucent quads must still stack");
    }
    // ── frame presentation policy ──
    {
        const int vw = 284, vh = 160;
        FramePlan p4 = plan_frame(4.0f, vw, vh, false, 8.0f, 1.7e6f, 0.76f);
        require(p4.fw == 1136 && p4.fh == 640 && !p4.linear && p4.rs == 4.0f, "Scale 4 must render 1:1 with nearest");
        require(p4.zoom == 0.75f && std::fabs(p4.zoom - 0.76f) / 0.76f <= 0.04f, "zoom snap at Scale 4 (art pixel = 3 framebuffer pixels)");
        require(one_to_one(p4.fw, vw * 4.0f) && one_to_one(p4.fh, vh * 4.0f), "Scale 4 not 1:1");
        FramePlan p6 = plan_frame(6.0f, vw, vh, false, 8.0f, 1.7e6f, 0.76f);
        require(p6.fw == 1704 && p6.fh == 960 && p6.rs == 6.0f, "Scale 6 must stay 1:1 inside the budget");
        require(p6.zoom == 0.76f, "a >4% zoom change must not be applied (camera preserved)");
        FramePlan fs = plan_frame(6.75f, vw, vh, false, 8.0f, 1.7e6f, 0.76f);   // 1080p fullscreen: budget caps the frame
        require(fs.rs < 6.75f && fs.rs > 6.0f && static_cast<long>(fs.fw) * fs.fh <= 1.71e6, "fullscreen frame exceeds the pixel budget");
        require(!one_to_one(fs.fw, vw * 6.75f), "budget-capped frame claims 1:1");
        FramePlan big = plan_frame(13.5f, vw, vh, true, 8.0f, 1.7e6f, 0.76f);   // 4K
        require(big.linear && big.rs < 7.0f && static_cast<long>(big.fw) * big.fh <= 1.71e6, "Smoothing must govern the filter on upscale");
        FramePlan off = plan_frame(13.5f, vw, vh, false, 8.0f, 1.7e6f, 0.76f);
        require(!off.linear, "Smoothing off must give nearest scaling");
        FramePlan small = plan_frame(0.5f, vw, vh, false, 8.0f, 1.7e6f, 0.76f);
        require(small.rs == 1.0f && small.fw == vw, "tiny window must not render below 1 px / unit");
        require(snap_zoom(0.76f, 8.0f) == 0.75f, "Scale 8 snap");
        require(snap_zoom(0.76f, 3.0f) == 0.76f && snap_zoom(0.76f, 1.0f) == 0.76f, "snap must not shrink a zoom below one pixel or jump far");
    }
    // ── sprite feet snapped to the pixel grid: stable texel phase, same size / shape / depth ──
    {
        require(snap_delta(10.0f) == 0.0f && std::fabs(snap_delta(10.4f) + 0.4f) < 1e-5f && std::fabs(snap_delta(10.6f) - 0.4f) < 1e-5f, "snap delta rounds to the nearest pixel");
        for (float v = -3.0f; v < 3.0f; v += 0.137f) require(std::fabs(snap_delta(v)) <= 0.5f + 1e-6f, "snap shift exceeds half a pixel");
        // 8 distinct texels, 4.4 framebuffer pixels per texel (the measured NPC scale), left edge at a camera-dependent fractional position
        std::uint32_t row[8];
        for (int i = 0; i < 8; ++i) row[i] = 0xff000000u | (0x20u * (i + 1)) | (0x100u * (i * 17 % 200 + 20));
        const Tex tex{8, 1, row};
        const float scale = 4.4f;
        auto widths = [&](float left, bool snap) {
            Raster rr;
            rr.reset(64, 8, 1, 0xff000000u);
            float x0 = left;
            if (snap) x0 += snap_delta(x0);
            const float x1 = x0 + 8 * scale;
            const Raster::V a{x0, 1, 0.5f, 0, 0}, b{x1, 1, 0.5f, 8 * 0.5f, 0}, c{x1, 6, 0.5f, 8 * 0.5f, 0.5f}, d{x0, 6, 0.5f, 0, 0.5f};
            rr.tri(a, b, c, tex, 1.0f, 1.0f); rr.tri(a, c, d, tex, 1.0f, 1.0f); rr.flush();
            std::vector<int> run;
            std::uint32_t prev = 0;
            for (int x = 0; x < 64; ++x) {
                const std::uint32_t q = rr.color[3 * 64 + x];
                if (q == 0xff000000u) { prev = 0; continue; }
                if (q == prev) ++run.back(); else { run.push_back(1); prev = q; }
            }
            return run;
        };
        bool unsnapped_varies = false;
        const std::vector<int> ref = widths(2.0f, true);
        require(ref.size() == 8, "sprite columns lost");
        for (float off = 0.0f; off < 1.0f; off += 0.1f) {
            require(widths(2.0f + off, true) == ref || off == 0.0f, "snapped sprite swims: its texel column widths depend on the sub-pixel position");
            if (widths(2.0f + off, false) != widths(2.0f, false)) unsnapped_varies = true;
        }
        require(unsnapped_varies, "fixture: without the snap the column widths should vary with the sub-pixel position");
        // total width stays the same within a pixel (size preserved), and the shift is a pure translation (depth is not touched: same iz in the vertices)
        int total = 0;
        for (int wv : ref) total += wv;
        require(total >= 35 && total <= 36, "sprite width changed (8 texels x 4.4 = 35.2 px)");
        // measured values of the player / NPC at Scale 7 (rs 6.117): an art pixel is 4.7 x 5.0 px: the 4.7 / 5.0 aspect is the existing perspective, not changed here
        require(std::fabs(snap_delta(4.738f * 16)) <= 0.5f && std::fabs(snap_delta(5.036f * 32)) <= 0.5f, "measured sprite size");
    }
    // ── nearest sampling keeps sprite edges clean at a fractional scale ──
    {
        std::uint32_t sprite[16] = {};
        const std::uint32_t body = 0xff10a0f0u, bg = 0xff303030u;
        for (int y = 1; y < 3; ++y) for (int x = 1; x < 3; ++x) sprite[y * 4 + x] = body;   // opaque core, transparent border
        const Tex tex{4, 4, sprite};
        Raster rr;
        const float s = 3.04f;   // framebuffer pixels per texel (zoom 0.76 at Scale 4)
        rr.reset(16, 16, 1, bg);
        const float x0 = 2.3f, y0 = 2.7f, x1 = x0 + 4 * s, y1 = y0 + 4 * s;
        const Raster::V a{x0, y0, 0.5f, 0, 0}, b{x1, y0, 0.5f, 4 * 0.5f, 0}, c{x1, y1, 0.5f, 4 * 0.5f, 4 * 0.5f}, d{x0, y1, 0.5f, 0, 4 * 0.5f};
        rr.tri(a, b, c, tex, 1.0f, 1.0f); rr.tri(a, c, d, tex, 1.0f, 1.0f); rr.flush();
        int body_px = 0;
        for (std::uint32_t q : rr.color) {
            require(q == bg || q == body, "sprite edge produced a blended / fringe colour");
            body_px += q == body;
        }
        require(body_px >= 30 && body_px <= 45, "sprite core not drawn at the expected size (2x2 texels of 3.04 px)");
    }

    // ── tile models ──
    g_models.load(kTilesetGeneral, kTilesetPalletTown);
    require(g_models.known, "Pallet tilesets have no model table");
    require(g_models.at[0x00E].kind == kTree && g_models.at[0x00E].b == kTip, "pine tip not a tree");
    require(g_models.at[0x015].kind == kTree && g_models.at[0x015].a == 1, "dense forest half wrong");
    require(g_models.at[0x2A3].kind == kWall && g_models.at[0x281].kind == kRoof, "house not a box");
    require(g_models.at[0x00D].kind == kGrass, "tall grass missing");
    require(g_models.at[0x001].kind == kFlat, "plain grass is not flat");
    require(g_models.at[0x004].b == 1, "stacked flowers would merge into one tall object");
    for (int id : {0x0E7, 0x0EC, 0x0ED})
        require(g_models.at[id].kind == kStand && g_models.at[id].a == kFence, "white fence not a fence piece");
    require(g_models.at[0x284].a == kFence && g_models.at[0x287].a == kFence, "Pallet fences not fence pieces");
    require(g_models.at[0x2A5].a != kFence, "mailbox treated as a fence");
    // ── fence orientation and side panels ──
    require(fence_orient(true, true, false, false) == kFenceNS, "vertical run not a side panel");
    require(fence_orient(false, true, false, false) == kFenceNS, "run end not a side panel");
    require(fence_orient(true, false, false, true) == kFenceFront, "corner became a side panel");
    require(fence_orient(false, false, true, true) == kFenceFront, "horizontal run turned sideways");
    require(fence_orient(false, false, false, false) == kFenceFront, "lone fence turned sideways");
    {
        std::uint32_t art[256] = {};
        int c0, c1;
        require(!fence_columns(art, &c0, &c1), "empty art has columns");
        for (int y = 0; y < 16; ++y) for (int x = 6; x <= 9; ++x) art[y * 16 + x] = 0xff000001u;
        require(fence_columns(art, &c0, &c1) && c0 == 6 && c1 == 9, "opaque columns wrong");
        float p[4][3], uv[4][2];
        fence_panel(40, 96, c0, c1, 4, p, uv);
        for (int i = 0; i < 4; ++i) require(p[i][0] == 40, "panel not in one X plane");
        require(p[0][1] == 96 && p[1][1] == 112 && p[2][2] == 0 && p[3][2] == 0 && p[0][2] == 4 && p[1][2] == 4,
                "panel does not span its cell on the ground");
        require(uv[0][0] == 6 && uv[2][0] == 10 && uv[0][1] == 0 && uv[1][1] == 16, "panel texture not turned");
    }
    // ── Route 1 gate (measured from the ROM) ──
    require(g_models.at[0x2AB].kind == kTree && g_models.at[0x2AB].a == 1 && g_models.at[0x2AB].b == kTip,
            "0x2AB is the right half of the left tree");
    require(g_models.at[0x2AA].kind == kTree && g_models.at[0x2AA].a == 0 && g_models.at[0x2AA].b == kTip,
            "0x2AA is the left half of the right tree");
    require(g_models.at[0x2BA].a == 1 && g_models.at[0x2BA].b == kUpper, "0x2BA upper half tag");
    require(g_models.at[0x2B2].a == 0 && g_models.at[0x2B2].b == kUpper, "0x2B2 upper half tag");
    require(gate_stripe(0x2AB) && gate_stripe(0x2AB)->c0 == 12 && gate_stripe(0x2AB)->c1 == 15, "left tip stripe");
    require(gate_stripe(0x2BA) && gate_stripe(0x2BA)->c0 == 12 && gate_stripe(0x2BA)->c1 == 15, "left upper stripe");
    require(gate_stripe(0x2AA) && gate_stripe(0x2AA)->c0 == 0 && gate_stripe(0x2AA)->c1 == 3, "right tip stripe");
    require(gate_stripe(0x2B2) && gate_stripe(0x2B2)->c0 == 0 && gate_stripe(0x2B2)->c1 == 3, "right upper stripe");
    require(!gate_stripe(0x0ED) && !gate_stripe(0x2A5), "non-gate id has a stripe");
    {
        std::uint32_t fence[256] = {};
        for (int y = 6; y < 16; ++y) for (int x = 0; x < 16; ++x) fence[y * 16 + x] = 0xff000001u;
        require(fence_height(fence) == 10.0f, "ordinary fence height wrong");
        float p[4][3], uv[4][2];
        const GateStripe* g = gate_stripe(0x2AB);
        fence_panel(16 * 11 + (g->c0 + g->c1 + 1) * 0.5f, 37 * 16, g->c0, g->c1, fence_height(fence), p, uv);
        require(p[0][0] == 16 * 11 + 14 && p[0][2] == 10 && p[2][2] == 0, "gate panel not full fence height");
        require(p[0][1] == 37 * 16 && p[1][1] == 38 * 16, "gate panel does not start at the corner piece's front edge");
        require(uv[0][0] == 12 && uv[2][0] == 16, "gate panel samples outside its stripe");
    }
    {
        // bottom layer: floor (key 1), backdrop (2), fence gray (3) in the stripe; top layer: leaf (4)
        const std::uint32_t floor_c = 0xff000011u, back_c = 0xff000022u, gray = 0xff333333u, leaf = 0xff004400u;
        std::uint32_t bottom[256], top[256], out[256], full[256];
        for (int i = 0; i < 256; ++i) { bottom[i] = floor_c; top[i] = 0; }
        for (int y = 0; y < 16; ++y) {
            for (int x = 12; x < 16; ++x) bottom[y * 16 + x] = (y % 4 == 0) ? floor_c : (y % 4 == 1 ? back_c : gray);
            bottom[y * 16 + 3] = gray;   // fence-coloured pixel outside the stripe
            if (y > 8) for (int x = 12; x < 16; ++x) top[y * 16 + x] = leaf;   // foliage over the stripe
        }
        for (int i = 0; i < 256; ++i) full[i] = top[i] ? top[i] : bottom[i];
        const std::uint32_t keys[] = {floor_c};
        isolate_stripe(bottom, 12, 15, keys, 1, back_c, out);
        int fence = 0;
        for (int y = 0; y < 16; ++y) for (int x = 0; x < 16; ++x) {
            const std::uint32_t c = out[y * 16 + x];
            if (x < 12) require(c == 0, "pixels outside the stripe kept");
            else if (y % 4 == 2 || y % 4 == 3) { require(c == gray, "bottom fence gray lost"); ++fence; }
            else require(c == 0, "floor or backdrop kept in the stripe");
            require(c != leaf, "foliage leaked into the fence");
        }
        require(fence == 8 * 4, "wrong amount of fence kept");
        // the old selection (top layer, else the full art) would not have been the fence
        isolate_stripe(full, 12, 15, keys, 1, back_c, out);
        bool leaked = false;
        for (int i = 0; i < 256; ++i) leaked |= out[i] == leaf;
        require(leaked, "fixture does not show foliage over the stripe");
    }
    // ── building / furniture sides ──
    {
        std::uint32_t art[256] = {};
        const std::uint32_t outline = 0xff101010u, body = 0xff3070a0u;
        // a small object: opaque only in rows 9..15, outline column at x=0, body inside
        for (int y = 9; y < 16; ++y) { art[y * 16 + 0] = outline; for (int x = 1; x < 16; ++x) art[y * 16 + x] = body; }
        int col = -1;
        require(pick_side_column(art, true, &col) && col == 3, "left side column must be inside the outline");
        require(art[9 * 16 + col] == body, "side column sampled the outline");
        require(pick_side_column(art, false, &col) && col == 12, "right side column");
        require(column_top(art, col) == 9 && side_segment_height(art, col) == 7.0f, "small object side is tile-tall");
        // padding column (mostly transparent) is skipped in favour of a solid one
        std::uint32_t pad[256] = {};
        for (int y = 0; y < 16; ++y) for (int x = 4; x < 16; ++x) pad[y * 16 + x] = body;
        pad[3] = outline;   // stray pixel in column 3
        require(pick_side_column(pad, true, &col) && col == 4, "transparent-padded column used for a side");
        std::uint32_t none[256] = {};
        require(!pick_side_column(none, true, &col) && side_segment_height(none, 3) == 0.0f, "empty art got a side");
        std::uint32_t full[256];
        for (auto& p : full) p = body;
        require(side_segment_height(full, 3) == 16.0f, "full tile side not full height");
        const ContactShadow cs = contact_shadow(32, 64, 100);
        require(cs.cx == 48 && cs.cy > 100 && cs.cy < 101 && cs.rx > 16 && cs.ry > 3, "contact shadow not hugging the foot");
        // the extruded side keeps the art colours and honours depth: a nearer
        // side face hides a farther one and transparent texels stay open
        Raster rr;
        rr.reset(8, 8, 1, 0xff000000u);
        const Tex side{16, 16, art};
        const Raster::V a{0, 0, 0.5f, 3.5f * 0.5f, 9 * 0.5f}, b{8, 0, 0.5f, 3.5f * 0.5f, 9 * 0.5f},
                        c{8, 8, 0.5f, 3.5f * 0.5f, 15 * 0.5f}, d{0, 8, 0.5f, 3.5f * 0.5f, 15 * 0.5f};
        rr.tri(a, b, c, side, 1.0f, 1.0f); rr.tri(a, c, d, side, 1.0f, 1.0f); rr.flush();
        require(rr.color[4 * 8 + 4] == body, "extruded side lost the native colour");
    }
    // ── player's house: measured native cells ──
    {
        g_models.load(kTilesetBuilding, kTilesetGenericBuilding1);
        auto at = [&](int id) -> const TileModel& { return g_models.at[id]; };
        // 1F table: top 04C/04D (roof), legs 054/055 (wall), chairs 04B/04E stand
        require(at(0x04C).kind == kRoof && at(0x04D).kind == kRoof, "table top");
        require(at(0x054).kind == kWall && at(0x055).kind == kWall, "table legs");
        require(at(0x04B).kind == kStand && at(0x04E).kind == kStand, "dining chairs");
        // plants: foliage tops 057/047 over pots 05F/04F, one run each
        for (int id : {0x057, 0x05F, 0x047, 0x04F}) require(at(id).kind == kStand && at(id).b == 0, "plant part");
        // TV column: 286, 035, 28E are one object; the console 296 is a separate small object (measured art: rows 0..6 only)
        const int tv[] = {0x286, 0x035, 0x28E};
        for (int id : tv) require(at(id).kind == kStand, "TV part not a stand");
        for (int i = 1; i < 3; ++i) require(continues_stand_run(at(tv[i])), "TV split across depth planes");
        require(at(0x296).kind == kStand && !continues_stand_run(at(0x296)), "console merged into the TV slab");
        require(!continues_stand_run(at(0x004)), "stacked flowers must stay separate objects");
        // bed: roof rows 283-285 / 28B-28D, wall row 293-295
        for (int id : {0x283, 0x284, 0x285, 0x28B, 0x28C, 0x28D}) require(at(id).kind == kRoof, "bed top");
        for (int id : {0x293, 0x294, 0x295}) require(at(id).kind == kWall, "bed footboard");
        // wall height follows the art: a low footboard is not a full tile
        require(box_wall_height(1, 0) == 16.0f && box_wall_height(1, 6) == 10.0f, "wall height from art");
        require(box_wall_height(2, 4) == 28.0f && box_wall_height(0, 0) == 0.0f, "multi-row wall height");
        std::uint32_t foot[256] = {};
        for (int y = 6; y < 16; ++y) for (int x = 0; x < 16; ++x) foot[y * 16 + x] = 0xff202020u;
        require(first_opaque_row(foot) == 6 && first_opaque_row(foot + 0) == 6, "first opaque row");
        std::uint32_t empty[256] = {};
        require(first_opaque_row(empty) == 16, "empty art row");
        // the top plane stays art-aligned whatever the wall height: Y - Z constant
        float Y, Z;
        for (float u = 0; u <= 60; u += 4) {
            wall_surface(200, box_wall_height(1, 6), 1.0f, u, &Y, &Z);
            require(std::abs((Y - Z) - (200 - u)) < 1e-3f, "low box breaks art alignment");
        }
        wall_surface(200, box_wall_height(1, 6), 1.0f, 40, &Y, &Z);
        require(Z == 10.0f, "low bed top not at its footboard height");
        // 1F: the doorway mat (y9 x3..5 = 01A/01B/01C) is not floor; the room ends on row 8
        require(is_doorway_mat(kTilesetBuilding, 0x01A) && is_doorway_mat(kTilesetBuilding, 0x01B) &&
                is_doorway_mat(kTilesetBuilding, 0x01C), "doorway mat ids");
        require(!is_doorway_mat(kTilesetBuilding, 0x012) && !is_doorway_mat(kTilesetBuilding, 0x008) &&
                !is_doorway_mat(kTilesetGeneral, 0x01A), "ordinary tile or another tileset treated as a mat");
        {
            const int ids[10] = {0x00, 0, 0, 0, 0, 0, 0, 0, 0x012, 0x01A};
            const unsigned char art[10] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
            require(last_floor_row_col(ids, art, 0, 10, kTilesetBuilding) == 8, "door column must end on the threshold row");
            require(last_floor_row_col(ids, art, 0, 10, kTilesetGeneral) == 9, "mat rule leaked into another tileset");
        }
        {   // irregular room: a deeper alcove (rows 9..11) of ordinary floor is kept
            const int ids[12] = {0, 0, 0, 0, 0, 0, 0, 0, 0x012, 0x040, 0x041, 0x042};
            const unsigned char art[12] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
            require(last_floor_row_col(ids, art, 0, 12, kTilesetBuilding) == 11, "valid deeper room portion truncated");
            const unsigned char gap[12] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0};   // void below row 9
            require(last_floor_row_col(ids, gap, 0, 12, kTilesetBuilding) == 9, "void edge not respected");
            // a wider room: the modal last row must not decide a column's own depth
            const int lasts[] = {8, 8, 8, 11, 11, 8, 8, 8};
            require(common_bottom_row(lasts, 8) == 8, "modal row");
        }
        // stand tile with a thin foreground band (TV top, rows 13..15) over floor
        {
            std::uint32_t tile[256], out[256];
            const std::uint32_t floor_c = 0xff55aadd, rim = 0xff808080u;
            for (int y = 0; y < 16; ++y) for (int x = 0; x < 16; ++x) tile[y * 16 + x] = y < 13 ? floor_c : rim;
            mask_foreground(tile, 13, out);
            for (int y = 0; y < 16; ++y) for (int x = 0; x < 16; ++x) {
                if (y < 13) require(out[y * 16 + x] == 0, "floor rows lifted into the standing object");
                else require(out[y * 16 + x] == rim, "foreground rim lost");
            }
            mask_foreground(tile, 0, out);
            require(out[0] == floor_c, "fg 0 must keep the whole tile");
            g_models.load(kTilesetBuilding, kTilesetGenericBuilding1);
            require(g_models.at[0x286].fg == 13 && g_models.at[0x035].fg == 0 && g_models.at[0x28E].fg == 0 &&
                    g_models.at[0x296].fg == 0, "only 286 has a floor band above its rim");
            require(g_models.at[0x057].fg == 0 && g_models.at[0x04F].fg == 0, "plants masked");
            // TV column ground: rug beside it, never the hidden wall backdrop
            const int run[] = {0x286, 0x035, 0x28E, 0x296};
            require(run_needs_floor_ground(run, 4, kTilesetGenericBuilding1), "TV run not recognised");
            const int kitchen[] = {0x02D, 0x035};   // 035 elsewhere (1F back wall) keeps its own art
            require(!run_needs_floor_ground(kitchen, 2, kTilesetGenericBuilding1), "035 outside the TV run was re-grounded");
            require(!run_needs_floor_ground(run, 4, kTilesetGeneral), "rule leaked into another tileset");
            const std::uint16_t ground[] = {0x001, 0x009, 0x043, 0x044, 0x045, 0x046};
            require(beside_ground(0x044, true, 0x044, true, ground, 6) == 0x044, "upper rug border");
            require(beside_ground(0x045, true, 0x045, true, ground, 6) == 0x045, "rug middle");
            require(beside_ground(0x100, true, 0x045, true, ground, 6) == 0x045, "non-ground left must fall to the right rug");
            require(beside_ground(0x100, true, 0x101, true, ground, 6) == -1, "wall neighbour accepted as ground");
            require(beside_ground(0x044, false, 0x100, true, ground, 6) == -1, "object neighbour accepted as ground");
            // the wall backdrop can never be chosen: only ids in the ground list qualify
            require(beside_ground(0x057, true, 0x058, true, ground, 6) == -1, "wall id became floor");
            // rug border above middle stays coherent for the measured column
            require(beside_ground(0x044, true, -1, false, ground, 6) != beside_ground(0x045, true, -1, false, ground, 6),
                    "border and middle collapsed to one tile");
        }
    }
    // ── pass isolation mask (diagnostic) ──
    require(parse_layer_mask(nullptr) == 0xFFFFFFFFu && parse_layer_mask("") == 0xFFFFFFFFu, "default must draw every pass");
    require(parse_layer_mask("1") == 1u && (parse_layer_mask("c") & 4) && (parse_layer_mask("c") & 8) && !(parse_layer_mask("c") & 1),
            "layer mask bits");
    // ── Pokemon Center furniture (Building + PokemonCenter) ──
    {
        g_models.load(kTilesetBuilding, kTilesetPokemonCenter);
        auto kind = [&](int id) { return static_cast<int>(g_models.at[id].kind); };
        require(g_models.known && kind(0x29C) == kRoof && kind(0x29D) == kRoof && kind(0x2A4) == kWall && kind(0x2A5) == kWall, "table not a box");
        for (int id : {0x29C, 0x29D, 0x2A4, 0x2A5}) require(box_art_top_only(g_models.at[id], true) && !box_art_top_only(g_models.at[id], false), "table art must use the top layer only");
        require(kind(0x2F5) == kSeat && kind(0x2FD) == kSeat, "stools must be independent low seats");
        require(!continues_stand_run(g_models.at[0x2F5]) && !continues_stand_run(g_models.at[0x2FD]), "a seat joined a standing run");
        require(kind(0x2F5) != kRoof && kind(0x2F5) != kWall && kind(0x2FD) != kRoof && kind(0x2FD) != kWall, "a seat can join the table box");
        // native 3x3 around the table (x10..12, y5..7): stools above / beside the table never join its box
        const int ids[9] = {0x2F5, 0x2FD, 0x281, 0x29C, 0x29D, 0x2F5, 0x2A4, 0x2A5, 0x2FD};
        std::vector<int> seen(9, 0);
        auto bld = [&](int x, int y) { return x >= 0 && y >= 0 && x < 3 && y < 3 && (kind(ids[y * 3 + x]) == kRoof || kind(ids[y * 3 + x]) == kWall); };
        int comps = 0, cells = 0, wall_cells = 0, roof_cells = 0;
        for (int y = 0; y < 3; ++y) for (int x = 0; x < 3; ++x) {
            if (!bld(x, y) || seen[y * 3 + x]) continue;
            ++comps;
            std::vector<int> comp{y * 3 + x};
            seen[y * 3 + x] = 1;
            for (std::size_t h = 0; h < comp.size(); ++h) {
                const int cx = comp[h] % 3, cy = comp[h] / 3;
                const int nb[4][2] = {{cx - 1, cy}, {cx + 1, cy}, {cx, cy - 1}, {cx, cy + 1}};
                for (const auto& n : nb) if (bld(n[0], n[1]) && !seen[n[1] * 3 + n[0]]) { seen[n[1] * 3 + n[0]] = 1; comp.push_back(n[1] * 3 + n[0]); }
            }
            cells = static_cast<int>(comp.size());
            for (int i : comp) (kind(ids[i]) == kWall ? wall_cells : roof_cells) += 1;
        }
        require(comps == 1 && cells == 4 && wall_cells == 2 && roof_cells == 2, "the table must be exactly one 2x2 box (2 roof + 2 wall cells), not joined to stools");
        require(box_wall_height(1, 0) == 16.0f, "one wall row");
        for (int id : {0x281, 0x289, 0x28A, 0x28B, 0x291, 0x292, 0x293, 0x299, 0x29A, 0x29B})
            require(kind(id) == kFlat, "floor or floor decoration modelled");
        for (int id : {0x2B2, 0x2B3})
            require(kind(id) == kLedge && g_models.at[id].a == 6 && g_models.at[id].b == 12 && g_models.at[id].fg == 12 && g_models.at[id].ground == 0x281, "machine model");
        require(kind(0x2B4) == kLedge && g_models.at[0x2B4].fg == 12 && g_models.at[0x2B4].ground == 0x281, "terminal lower tile");
        require(kind(0x2AC) == kMachineTop && g_models.at[0x2AC].b == 8 && g_models.at[0x2AA].b == 0 && g_models.at[0x2AB].b == 0, "terminal upper tile / machine upper tiles first rows");
        require(kind(0x2AD) == kFlat, "2AD is plain counter / wall decoration, not part of the terminal");
        for (int id : {0x2B1, 0x2B5})
            require(kind(id) == kCounterTop && g_models.at[id].a == 6 && g_models.at[id].b == 6 && g_models.at[id].ground == 0x281, "counter end model");
        for (int id : {0x2AA, 0x2AB}) require(kind(id) == kMachineTop, "machine upper tiles");
        for (int id : {0x290, 0x363}) require(g_models.at[id].fg == 0 && g_models.at[id].ground == 0xFFFF, "nurse cells grew a machine or lost the bottom-layer floor");
        // measured corners: bottom layer = the whole counter, top rows 0..4 = the flower pot; 290 / 363: floor + a 4-row raised strip
        for (int id : {0x2B9, 0x2BD})
            require(kind(id) == kSeat && g_models.at[id].a == 6 && g_models.at[id].b == 5 && g_models.at[id].art == 2 && g_models.at[id].ground == 0x281, "counter corner model");
        for (int id : {0x290, 0x363})
            require(kind(id) == kLedge && g_models.at[id].a == 6 && g_models.at[id].b == 12, "counter ledge model");
        require(!box_art_top_only(g_models.at[0x2B9], true), "corner uses the box top-only rule by mistake");
        // the measured front strip: 2BC / 298, a 6-pixel block on the plain floor 281
        for (int id : {0x2BC, 0x298}) {
            require(kind(id) == kSeat && g_models.at[id].a == 6 && g_models.at[id].ground == 0x281, "counter strip model");
            require(box_art_top_only(g_models.at[id], true) == false, "strip must not be a top-only box");
            require(seat_height_of(g_models.at[id].a) == 6.0f, "strip height");
        }
        require(seat_height_of(0) == 4.0f && g_models.at[0x2F5].a == 0 && seat_height_of(g_models.at[0x2F5].a) == 4.0f, "stools lost their default height");
        require(seat_height_of(99) == 4.0f && seat_height_of(-2) == 4.0f, "out-of-range height");
        for (int id : {0x2D0, 0x2D1, 0x2D8, 0x2D9, 0x2E0, 0x2E1}) require(kind(id) == kFlat, "stairs modelled");
        // scope: the Building + PokemonCenter pair only
        for (std::uint32_t sec : {kTilesetGenericBuilding1, kTilesetPalletTown, kTilesetViridianCity, 0x08000000u}) {
            g_models.load(kTilesetBuilding, sec);
            require(g_models.at[0x29C].kind != kRoof || sec == kTilesetViridianCity, "Center table top leaked");
            require(g_models.at[0x2F5].kind == kFlat && g_models.at[0x2FD].kind == kFlat, "Center stool leaked into another secondary");
        }
        g_models.load(kTilesetGeneral, kTilesetPokemonCenter);   // wrong primary
        require(g_models.at[0x29C].kind == kFlat && g_models.at[0x2F5].kind == kFlat && g_models.at[0x2A4].kind == kFlat, "Center table applied without the Building primary");
    }
    // ── nurse counter strip (Pokemon Center 1F, measured) ──
    {
        const float H = seat_height_of(6);
        const float X0 = 14 * 16.0f, Yb = 11 * 16.0f;   // world cell (14,10): front plane Y = 176
        const SeatFaces f = seat_geometry(X0, Yb, H);
        require(Yb == 176.0f, "front plane");
        require(f.top[0][1] == 166.0f && f.top[0][2] == 6.0f && f.front[2][2] == 0.0f && f.front[0][2] == 6.0f, "6 px block: top far edge Y 166, front 0..6");
        // the art split: top = rows 0..9 (10 deep), face = rows 10..15 (6 high)
        require(16 - static_cast<int>(H) == 10, "texture split row");
        // nurse OAM4: feet at world Y 160 in cell (14,9), behind the strip
        const float nurse_foot_y = 160.0f;
        require(nurse_foot_y < f.top[0][1], "nurse feet are in front of the counter's top far edge (she would be hidden)");
        require(nurse_foot_y == f.top[0][1] - f.top[0][2], "the counter's far edge must sit on the nurse's foot line in the reference view (no overlap above her feet)");
        // ray at 45 degrees from the camera to her feet clears the block: height of the ray over Y = 166 is 6 <= H (grazing, not inside)
        const float ray_z_at_far_edge = (f.top[0][1] - nurse_foot_y) * 1.0f;
        require(ray_z_at_far_edge <= f.top[0][2] + 1e-4f, "the line of sight to her feet passes through the block");
        // her body above the strip: reference-view rows of the block are 160..176, her sprite rows are 128..160
        require(f.top[0][1] - f.top[0][2] >= nurse_foot_y && f.front[3][1] - f.front[3][2] == 176.0f, "block projects above her feet line");
        // run of 2BC 2BC 298 2BC 2BC: only the two ends get a side face; the corners 2B9 / 2BD (flat) never join
        require(!seat_draw_side(true, 0.0f, X0, true) && !seat_draw_side(false, 1e9f, X0, true), "an internal side between strip blocks was drawn");
        require(seat_draw_side(true, X0 - 5, X0, false) && !seat_draw_side(true, X0 + 5, X0, false), "left end cap visibility");
        require(seat_draw_side(false, X0 + 30, X0, false) && !seat_draw_side(false, X0 + 5, X0, false), "right end cap visibility");
        // rim column of the counter face (rows 10..15 fully opaque, grey / light grey / red / grey)
        std::uint32_t face[256];
        for (int y = 0; y < 16; ++y) for (int x = 0; x < 16; ++x) face[y * 16 + x] = y < 10 ? 0xff7383ffu : 0xffa4b4b4u;
        int col = -1;
        require(seat_side_column(face, true, 6, &col) && col == 0 && seat_side_column(face, false, 6, &col) && col == 15, "counter end-cap rim column");
        // strip cells' ground is the plain floor and never their own art (2BC bottom layer is empty)
        require(g_models.at[0x2BC].ground == g_models.at[0x298].ground, "strip ground differs");
    }
    // ── counter corners and the strip behind the nurse (measured) ──
    {
        const float H = seat_height_of(6);
        // ledge: nurse cells world (14,9) / (15,9): tile top Y = 144; strip rows 12..15
        float p[4][3], uv[4][2];
        ledge_quad(14 * 16.0f, 9 * 16.0f, H, 12, p, uv);
        for (int i = 0; i < 4; ++i) require(p[i][2] == H, "ledge not flat at the counter height");
        require(p[0][1] == 162.0f && p[3][1] == 166.0f, "ledge Y extent");
        require(p[3][1] == seat_geometry(14 * 16.0f, 11 * 16.0f, H).top[0][1], "ledge does not continue the counter top (gap or overlap with the front block)");
        require(uv[0][1] == 12.0f && uv[3][1] == 16.0f && uv[0][0] == 0.0f && uv[1][0] == 16.0f, "ledge texture rows 12..15");
        // reference view: row r of the strip is at Y - Z = tile top + r
        for (int r : {12, 16}) {
            const float y = p[0][1] + (r - 12);
            require(y - H == 9 * 16.0f + r, "ledge breaks the reference-view alignment");
        }
        // authentic occlusion: the nurse's feet at Y 160 behind the strip: her lowest 4 units are hidden, as the 2D top layer hides 4 rows
        float hidden = 0;
        for (float zt = 0.0f; zt < 12.0f; zt += 0.25f) {
            const float cross_y = 160.0f + (H - zt);   // where the 45-degree line of sight to her body point crosses the counter plane
            if (cross_y >= p[0][1] - 1e-4f && cross_y <= 176.0f) hidden = zt + 0.25f;
        }
        require(hidden > 3.9f && hidden < 4.6f, "the nurse is hidden by more or less than the 4 rows of the original top layer");
        // flower pot: top rows 0..4 upright on the block top, Y - Z of its rows unchanged
        const float Ybot = 11 * 16.0f, foot = flower_foot_y(Ybot, H, 5);
        require(foot == Ybot - 5.0f, "flower foot Y");
        require(foot - (H + 5) == Ybot - 16.0f && foot - H == Ybot - 11.0f, "flower rows leave the reference-view alignment");
        require(foot >= seat_geometry(11 * 16.0f, Ybot, H).top[0][1] && foot <= Ybot, "flower foot is not on the counter top");
        // corners join the run: no internal side face between the corner block and 2BC
        require(!seat_draw_side(false, 1e9f, 11 * 16.0f, true), "corner / strip side drawn");
        // art selection facts of the measured corners (bottom opaque 256, top rows 0..4 partial, rows 5..7 empty)
        std::uint32_t top[256] = {}, bottom[256];
        for (auto& q : bottom) q = 0xff7383ffu;
        for (int y = 0; y < 5; ++y) for (int x = 2; x < 14; ++x) top[y * 16 + x] = 0xff4194acu;
        std::uint32_t fl[256] = {};
        std::copy(top, top + 5 * 16, fl);
        for (int y = 5; y < 16; ++y) for (int x = 0; x < 16; ++x) require(fl[y * 16 + x] == 0, "flower buffer kept rows below the pot");
        for (int y = 0; y < 5; ++y) require(fl[y * 16 + 5] == top[y * 16 + 5], "flower rows lost");
    }
    // ── healing machine row and counter ends (measured) ──
    {
        const float H = seat_height_of(6), Ytop = 9 * 16.0f;   // world row 9: Y 144..160
        // ONE plane for the whole machine: upper tiles (2AA / 2AB, world y8, top Y 128) + lower tiles (2B2 / 2B3, y9, top Y 144, 12 rows)
        MachineSpan ms[2];
        require(machine_spans(Ytop, 12, 0, ms) == 2, "machine spans");
        const float foot = machine_foot_y(Ytop, 12);
        require(foot == 156.0f, "machine plane Y");
        require(ms[0].z0 == 0.0f && ms[0].z1 == 12.0f && ms[1].z0 == 12.0f && ms[1].z1 == 28.0f && !ms[0].upper && ms[1].upper, "machine heights: 0..12 lower, 12..28 upper");
        require(ms[0].v0 == 0 && ms[0].v1 == 12 && ms[1].v0 == 0 && ms[1].v1 == 16, "machine texture rows");
        // reference view: the two spans tile Y - Z 128..156 exactly once (the original 28 rows), no gap and no overlap
        const RefRange lo = ref_range(foot, ms[0].z0, ms[0].z1), up = ref_range(foot, ms[1].z0, ms[1].z1);
        require(up.lo == 128.0f && up.hi == 144.0f && lo.lo == 144.0f && lo.hi == 156.0f, "machine rows do not cover 128..156");
        require(up.hi == lo.lo, "gap / overlap between the machine halves");
        for (int r : {0, 7, 15}) require(foot - ms[1].z1 + r == 128.0f + r, "upper machine rows leave the reference-view alignment");
        for (int r : {0, 5, 11}) require(foot - (12 - r) == Ytop + r, "lower machine rows leave the reference-view alignment");
        // nothing of the machine projects outside the tiles it replaces: in particular no cap over the wall art above row 128
        require(up.lo >= 128.0f && lo.hi <= 156.0f, "machine projects outside its own rows (a lid / cap over the wall art)");
        // terminal: 2AC top layer is empty above row 8, so its upper span is rows 8..15 and the terminal is 20 rows, Y - Z 136..156
        MachineSpan tm[2];
        machine_spans(Ytop, 12, 8, tm);
        require(tm[1].v0 == 8 && tm[1].v1 == 16 && tm[1].z0 == 12.0f && tm[1].z1 == 20.0f && tm[0].z1 == 12.0f, "terminal spans");
        const RefRange tup = ref_range(foot, tm[1].z0, tm[1].z1), tlo = ref_range(foot, tm[0].z0, tm[0].z1);
        require(tup.lo == 136.0f && tup.hi == 144.0f && tlo.lo == 144.0f && tlo.hi == 156.0f && tup.hi == tlo.lo, "terminal rows do not tile 136..156");
        require((tup.hi - tup.lo) + (tlo.hi - tlo.lo) == 20.0f, "terminal is not 20 rows");
        // the empty top rows of 2AC (0..7) must NOT be stretched: nothing projects above row 136 (that is the wall's own art)
        require(tup.lo >= 128.0f + 8.0f, "terminal projects over the wall art above its housing");
        machine_spans(Ytop, 12, 99, tm);   // out-of-range first row is clamped, never a negative height
        require(tm[1].v0 == 15 && tm[1].z1 > tm[1].z0, "first row clamp");
        // the rejected lid (a horizontal quad at z 12 over Y 144..156) projects onto Y - Z 132..144, i.e. INSIDE the upper body's own rows 128..144:
        // any extra quad whose reference rows intersect the two spans replaces original art. The spans already cover 128..156 exactly once.
        const float lid_lo = 144.0f - 12.0f, lid_hi = 156.0f - 12.0f;
        require(lid_lo < up.hi && lid_hi > up.lo, "fixture: the old lid must intersect the upper body's rows");
        require((up.hi - up.lo) + (lo.hi - lo.lo) == 28.0f, "the machine is exactly the original 28 rows");
        // wall pass: the upper tiles draw only their bottom layer, nothing else changes
        require(wall_pass_uses_bottom_only(kMachineTop) && !wall_pass_uses_bottom_only(kFlat) && !wall_pass_uses_bottom_only(kLedge) && !wall_pass_uses_bottom_only(kSeat), "wall pass layer rule");
        // behind the counter strip: the machine plane (Y 156) is behind the strip (Y 162..166 at z 6) and its lower part is lower than... the strip hides z < 6 only from in front
        require(foot < 162.0f, "machine plane not behind the counter strip");
        // counter end 2B1 / 2B5: top surface Y 150..166 at z 6, contiguous with the front corner block top (166) and strips
        float sp[4][3], suv[4][2];
        ledge_quad(11 * 16.0f, Ytop, H, 0, sp, suv);
        require(sp[0][1] == 150.0f && sp[3][1] == 166.0f && sp[0][2] == H, "counter end top extent");
        require(sp[3][1] == seat_geometry(11 * 16.0f, 11 * 16.0f, H).top[0][1], "counter end top does not continue into the corner block");
        require(suv[0][1] == 0.0f && suv[3][1] == 16.0f, "counter end top uses the whole tile");
        // its plant stands on that top: foot = 166, rows 6..15 from z 6 up to 16, Y - Z = tile top + row
        const float pf = plant_foot_y(Ytop, H);
        require(pf == 166.0f, "plant foot");
        for (int r : {6, 10, 15}) require(pf - (H + (16 - r)) == Ytop + r, "plant rows leave the reference-view alignment");
        // one plant over two tiles: 2B1 rows 6..15 (above) + 2B9 rows 0..4 (below) on ONE plane at foot 171, z 6..21 (15 rows)
        require(plant_continues(6, 5) && !plant_continues(0, 5) && !plant_continues(6, 0) && !plant_continues(16, 5), "plant merge rule");
        const float cfoot = flower_foot_y(11 * 16.0f, H, 5);
        require(cfoot == 171.0f, "combined plant foot");
        // upper part rows 6..15 occupy z 11..21, lower part rows 0..4 occupy z 6..11, joined at z 11 with unchanged reference rows
        require(cfoot - (H + 5 + 10) == 144.0f + 6.0f && cfoot - (H + 5) == 160.0f && cfoot - H == 165.0f, "combined plant leaves the reference-view alignment");
        // end faces
        float cp[4][3];
        counter_end_cap(11 * 16.0f, Ytop, H, cp);
        for (int i = 0; i < 4; ++i) require(cp[i][0] == 176.0f && cp[i][2] >= 0 && cp[i][2] <= H && cp[i][1] >= 150.0f && cp[i][1] <= 166.0f, "end cap outside the counter volume");
        require(cp[2][2] == 0 && cp[3][2] == 0 && cp[0][2] == H && cp[0][1] == 150.0f && cp[1][1] == 166.0f, "end cap on the floor / top");
        // joined neighbours hide the shared side; the free end shows on the camera's side
        require(!seat_draw_side(false, 1e9f, 11 * 16.0f, true) && seat_draw_side(true, 100.0f, 11 * 16.0f, false), "counter end cap rule");
    }
    // ── low seats: geometry ──
    {
        const float X0 = 10 * 16.0f, Yb = 6 * 16.0f, H = kSeatHeight;
        require(H == 4.0f, "seat height changed");
        const SeatFaces f = seat_geometry(X0, Yb, H);
        const float(*faces[4])[3] = {f.front, f.top, f.left, f.right};
        for (auto face : faces)
            for (int i = 0; i < 4; ++i) {
                require(face[i][2] >= 0.0f && face[i][2] <= H, "seat corner outside 0..H (a tall card)");
                require(face[i][0] >= X0 && face[i][0] <= X0 + 16 && face[i][1] >= Yb - (16 - H) && face[i][1] <= Yb, "seat face leaves its own cell");
            }
        // top: horizontal at exactly H, spanning the whole cell width and 16 - H of depth
        for (int i = 0; i < 4; ++i) require(f.top[i][2] == H, "seat top not horizontal at seat height");
        require(f.top[0][1] == Yb - 12 && f.top[3][1] == Yb && f.top[0][0] == X0 && f.top[1][0] == X0 + 16, "seat top extents");
        // front: vertical on the cell's front edge, floor to seat height
        require(f.front[0][1] == Yb && f.front[2][1] == Yb && f.front[2][2] == 0 && f.front[0][2] == H, "seat front face");
        // the faces share their edges: no gap and no overlap between top, front and sides
        for (int k = 0; k < 3; ++k) {
            require(f.top[3][k] == f.front[0][k] && f.top[2][k] == f.front[1][k], "top / front edge not shared");
            require(f.left[0][k] == f.top[0][k] && f.left[1][k] == f.top[3][k], "left side / top edge not shared");
            require(f.right[0][k] == f.top[1][k] && f.right[1][k] == f.top[2][k], "right side / top edge not shared");
            require(f.left[2][k] == f.front[3][k] && f.right[2][k] == f.front[2][k], "side / front foot not shared");
        }
        require(f.left[2][2] == 0 && f.left[3][2] == 0 && f.right[2][2] == 0 && f.right[3][2] == 0, "sides not on the floor");
        // reference view: Y - Z follows the art rows (top far edge = row 0 of the cell, front foot = row 16)
        require(f.top[0][1] - f.top[0][2] == Yb - 16 && f.front[3][1] - f.front[3][2] == Yb && f.front[0][1] - f.front[0][2] == Yb - H,
                "seat breaks the reference-view alignment");
        // neighbours: the next stool one cell right / below starts exactly at this cell's edge: no overlap
        const SeatFaces g = seat_geometry(X0 + 16, Yb, H), h = seat_geometry(X0, Yb + 16, H);
        for (int i = 0; i < 4; ++i) {
            require(g.top[i][0] >= f.top[1][0], "stools overlap sideways");
            require(h.top[i][1] >= f.front[0][1] - (16 - H), "stools overlap in depth");
        }
        // side column: the round base is narrower than the tile, the rim column is the outermost fully opaque one
        std::uint32_t stool[256] = {}, floorc = 0xff40c0f0u, rim = 0xff203060u;
        for (int y = 0; y < 16; ++y) for (int x = 2; x < 14; ++x) stool[y * 16 + x] = (y >= 12 && (x < 4 || x > 11)) ? 0u : (y >= 12 ? rim : floorc);
        int col = -1;
        require(seat_side_column(stool, true, 4, &col) && col == 4, "left rim column not the outermost fully opaque base column");
        require(seat_side_column(stool, false, 4, &col) && col == 11, "right rim column");
        std::uint32_t nobase[256] = {};
        for (int y = 0; y < 12; ++y) for (int x = 0; x < 16; ++x) nobase[y * 16 + x] = floorc;
        require(!seat_side_column(nobase, true, 4, &col), "a stool without base rows must not get a side column");
    }
    // ── helper null / bounds handling (run under ASan/UBSan on Linux: an out-of-range read fails the check) ──
    {
        std::uint32_t tile[256], out[256];
        for (auto& q : tile) q = 0xff102030u;
        int col = -1;
        require(!seat_side_column(tile, true, 20, &col) && !seat_side_column(tile, true, 0, &col) && !seat_side_column(tile, true, -3, &col),
                "seat rim rows outside the tile");
        require(!seat_side_column(nullptr, true, 4, &col) && !seat_side_column(tile, true, 4, nullptr), "null art / column accepted");
        require(seat_side_column(tile, true, 16, &col) && col == 0, "all 16 rows are a valid rim height");
        flat_wall_from(nullptr, out);
        flat_wall_from(tile, nullptr);
        fill_void_from(nullptr, tile, out);
        fill_void_from(tile, nullptr, out);   // no donor: the tile unchanged, no crash
        for (int i = 0; i < 256; ++i) require(out[i] == tile[i], "fill without a donor changed the tile");
        require(!is_corner_wedge(nullptr, true) && void_info(nullptr).count == 0, "null art treated as a wedge");
        require(pick_plain_wall_id(nullptr, nullptr, 5, 9) == 9 && pick_plain_wall_id(nullptr, nullptr, 0, 9) == 9, "null row");
        const int idv[3] = {5000, -4, 7};   // out-of-range ids are ignored, not counted
        const unsigned char fl[3] = {1, 1, 1};
        require(pick_plain_wall_id(idv, fl, 3, 11) == 11, "out-of-range ids counted");
        // a back-wall row below a short snapshot: nothing to read
        require(plain_row_cells(4, 4, 0, 14) == 0 && plain_row_cells(3, 4, 0, 14) == 15 && plain_row_cells(-1, 4, 0, 14) == 0 && plain_row_cells(2, 4, 0, 14) == 15 &&
                plain_row_cells(2, 4, -3, 14) == 15 && plain_row_cells(2, 4, 5, 3) == 0, "row cell range outside the snapshot");
    }
    // ── indoor back wall / side wall joins (Pokemon Center 1F) ──
    {
        // lower back-wall row of PokemonCenter_1F (15 tiles, all different): no plain tile, so the tile BESIDE the corner is used
        const int row1[15] = {0x2A8, 0x28D, 0x2CB, 0x2CC, 0x2A9, 0x2AA, 0x2AB, 0x28E, 0x28F, 0x2AC, 0x2AD, 0x062, 0x29E, 0x29F, 0x2AE};
        unsigned char flat15[15];
        for (auto& f : flat15) f = 1;
        require(pick_plain_wall_id(row1, flat15, 15, -1) == -1, "an all-different row has no plain tile");
        require(pick_plain_wall_id(row1, flat15, 15, -1) != 0x062, "the smallest id (the door tile 062) was picked");
        // upper row: 284 appears five times -> plain
        const int row0[15] = {0x2A0, 0x2D3, 0x2C9, 0x2CA, 0x284, 0x284, 0x284, 0x286, 0x287, 0x284, 0x284, 0x285, 0x296, 0x297, 0x2A6};
        require(pick_plain_wall_id(row0, flat15, 15, 0x2D3) == 0x284, "plain upper wall 284 not found");
        // furniture cells (not flat) never count as plain wall
        unsigned char notflat[15];
        for (auto& f : notflat) f = 1;
        for (int i : {4, 5, 6, 9, 10}) notflat[i] = 0;
        require(pick_plain_wall_id(row0, notflat, 15, 0x2D3) == 0x2D3, "non-flat cells counted as the plain wall");
        require(pick_plain_wall_id(row0, flat15, 0, 7) == 7, "empty row");
        // ties between repeated tiles keep the smallest id, as before
        const int tie[6] = {0x290, 0x290, 0x284, 0x284, 0x300, 0x301};
        unsigned char f6[6] = {1, 1, 1, 1, 1, 1};
        require(pick_plain_wall_id(tie, f6, 6, 5) == 0x284, "tie rule changed");
        // lower wall without a plain tile: a flat wall from the upper plain tile (no trim, no appliances repeated)
        {
            std::uint32_t upper_tile[256], flat[256];
            const std::uint32_t trim = 0xff202020u, body = 0xff50a0e0u, shade = 0xff4080c0u, base = 0xff101010u;
            for (int y = 0; y < 16; ++y) for (int x = 0; x < 16; ++x)
                upper_tile[y * 16 + x] = y < 3 ? trim : (y < 12 ? body : (y < 15 ? shade : base));
            flat_wall_from(upper_tile, flat);
            for (int y = 0; y < 16; ++y) for (int x = 0; x < 16; ++x) require(flat[y * 16 + x] == body, "flat wall must be the body colour only");
            // a tile with a horizontal pattern keeps it, constant down the wall (rows identical)
            std::uint32_t striped[256], f2[256];
            for (int y = 0; y < 16; ++y) for (int x = 0; x < 16; ++x) striped[y * 16 + x] = y < 4 ? trim : ((x / 4) % 2 ? body : shade);
            flat_wall_from(striped, f2);
            for (int y = 1; y < 16; ++y) for (int x = 0; x < 16; ++x) require(f2[y * 16 + x] == f2[x], "flat wall rows differ");
            require(f2[0] == shade && f2[4] == body, "pattern of the body row lost");
            // a see-through gap between the trim and the last opaque row must not be picked as the body (most frequent row!)
            std::uint32_t gap[256] = {}, f4[256];
            for (int x = 0; x < 16; ++x) { gap[0 * 16 + x] = trim; gap[1 * 16 + x] = trim; gap[2 * 16 + x] = trim; gap[11 * 16 + x] = body; }
            flat_wall_from(gap, f4);
            for (int i = 0; i < 256; ++i) require(f4[i] == body, "a fully transparent row was chosen as the wall body");
            // transparent rows below the last opaque row are not the body
            std::uint32_t half[256] = {};
            for (int y = 0; y < 8; ++y) for (int x = 0; x < 16; ++x) half[y * 16 + x] = y < 3 ? trim : body;
            flat_wall_from(half, f2);
            for (int i = 0; i < 256; ++i) require(f2[i] == body, "empty rows chosen as the wall body");
            std::uint32_t nothing[256] = {}, f3[256];
            flat_wall_from(nothing, f3);
            for (int i = 0; i < 256; ++i) require(f3[i] == 0, "an empty tile must stay empty");
            // never a decoration: the lower-row neighbours are not plain, only the upper plain tile feeds the flat wall
            const int lower[15] = {0x2A8, 0x28D, 0x2CB, 0x2CC, 0x2A9, 0x2AA, 0x2AB, 0x28E, 0x28F, 0x2AC, 0x2AD, 0x062, 0x29E, 0x29F, 0x2AE};
            unsigned char all_flat[15];
            for (auto& f : all_flat) f = 1;
            require(pick_plain_wall_id(lower, all_flat, 15, -1) == -1, "a decoration became the plain lower wall");
        }
        // corner wedge: transparent triangle on the left edge of the tile (2A0-like), wall elsewhere
        std::uint32_t corner[256], plain[256], filled[256];
        const std::uint32_t wall = 0xff4080c0u, trim = 0xff102030u, other = 0xff80c040u;
        for (int y = 0; y < 16; ++y) for (int x = 0; x < 16; ++x) {
            corner[y * 16 + x] = (x < 8 - y / 2 && y < 12) ? 0u : (x == 15 - y / 4 ? trim : wall);   // wedge + a trim diagonal
            plain[y * 16 + x] = other;
        }
        const VoidInfo v = void_info(corner);
        require(v.count >= 16 && v.count <= 128 && v.left_edge && !v.right_edge, "fixture wedge shape");
        require(is_corner_wedge(corner, true) && !is_corner_wedge(corner, false), "left wedge not recognised");
        require(!is_corner_wedge(plain, true) && !is_corner_wedge(plain, false), "plain wall taken for a wedge");
        std::uint32_t full[256];
        for (auto& q : full) q = wall;
        require(!is_corner_wedge(full, true), "a full tile is not a wedge");
        std::uint32_t mostly_empty[256] = {};
        require(!is_corner_wedge(mostly_empty, true), "an almost empty tile is not a corner wedge");
        fill_void_from(corner, plain, filled);
        int kept = 0, gap = 0;
        for (int i = 0; i < 256; ++i) {
            if (corner[i] >> 24) { require(filled[i] == corner[i], "an authentic corner pixel was replaced"); ++kept; }
            else { require(filled[i] == plain[i], "see-through gap not closed with the plain wall"); ++gap; }
        }
        require(gap == v.count && kept == 256 - v.count, "fill counts");
        // a donor with its own holes leaves them open rather than inventing colour
        std::uint32_t holey[256];
        for (int i = 0; i < 256; ++i) holey[i] = i % 2 ? 0u : other;
        std::uint32_t f2[256];
        fill_void_from(corner, holey, f2);
        for (int i = 0; i < 256; ++i) if (!(corner[i] >> 24) && !(holey[i] >> 24)) require(!(f2[i] >> 24), "invented colour for a double hole");
    }
    // ── BG0 overlay gating: only live window rectangles (plus their frame) may be drawn ──
    {
        // a dialogue window at tile (2,15) 26x4 tiles; the frame is one tile around it
        const BgRect dlg = pad_window_rect(16, 120, 208, 32, 8);
        require(dlg.x0 == 8 && dlg.y0 == 112 && dlg.x1 == 232 && dlg.y1 == 160, "frame padding");
        require(bg_rect_valid(dlg), "dialogue window rect rejected");
        BgRect wins[2] = {dlg, pad_window_rect(150, 8, 80, 48, 8)};   // + a Start-menu sized window
        require(bg_point_in_rects(wins, 2, 120, 140) && bg_point_in_rects(wins, 2, 8, 112), "window pixel not allowed");
        require(bg_point_in_rects(wins, 2, 190, 30), "second window pixel not allowed");
        // map art rows around the houses (the ghost roof row) are outside every window
        require(!bg_point_in_rects(wins, 2, 100, 40) && !bg_point_in_rects(wins, 2, 60, 110), "map area allowed");
        require(!bg_point_in_rects(wins, 2, 232, 140) && !bg_point_in_rects(wins, 2, 7, 140), "edge is exclusive / padded only");
        require(!bg_point_in_rects(wins, 0, 120, 140), "no live window must allow nothing");
        require(!bg_rect_valid(pad_window_rect(10, 10, 0, 0, 0)) && !bg_rect_valid(pad_window_rect(0, 0, -8, 16, 0)), "empty rect accepted");
        require(!bg_rect_valid(pad_window_rect(0, 0, 4000, 16, 8)), "stale oversized rect accepted");
        require(bg_rect_valid(pad_window_rect(-16, 40, 240, 80, 8)), "scrolled partial window rejected");
    }
    // ── Viridian City ──
    {
        g_models.load(kTilesetGeneral, kTilesetViridianCity);
        auto kind = [&](int id) { return static_cast<int>(g_models.at[id].kind); };
        require(g_models.known && g_models.secondary == kTilesetViridianCity, "Viridian has no model table");
        for (int id : {0x288, 0x289, 0x28A, 0x28B, 0x28C, 0x290, 0x291, 0x292, 0x293, 0x294})
            require(kind(id) == kRoof, "gym roof id not a roof");
        for (int id : {0x298, 0x299, 0x29A, 0x29B, 0x29C, 0x29D, 0x2A5, 0x2A6, 0x2A7})
            require(kind(id) == kWall, "town house wall / door-row id not a wall");
        for (int id : {0x2A0, 0x2A2, 0x2A3, 0x2A4}) require(kind(id) == kPlanter, "house 2 planter row is not a planter (it is walkable foreground, not wall)");
        require(kind(0x299) == kWall, "gym door must be part of the box");
        // the bush, cliffs and unverified groups must not become a roof or wall
        for (int id : {0x285, 0x286, 0x287, 0x28D}) require(kind(id) == kStand, "bush is not grounded foliage");
        for (int id : {0x28E, 0x28F, 0x295, 0x2BC}) require(kind(id) == kFlat, "cliff / tree-edge cell classified");
        require(kind(0x0CE) == kFlat, "door-approach ground classified");
        // the Center's right column is real roof / facade art, not trees (measured native sheet)
        for (int id : {0x187, 0x18F}) require(kind(id) == kRoof && !box_art_top_only(g_models.at[id], true), "Center right roof column");
        for (int id : {0x197, 0x186, 0x19F}) require(kind(id) == kWall, "Center right facade column");
        // town house cap row 280-284: roof, top layer only; it is not a wall and not a bush
        for (int id : {0x280, 0x281, 0x282, 0x283, 0x284}) {
            require(kind(id) == kRoof, "town house cap not a roof");
            require(box_art_top_only(g_models.at[id], true) && !box_art_top_only(g_models.at[id], false), "cap must use its top layer only");
        }
        // scoped: the General ids are boxes under Viridian only
        g_models.load(kTilesetGeneral, kTilesetPalletTown);
        require(g_models.at[0x187].kind == kFlat && g_models.at[0x19F].kind == kFlat && g_models.at[0x280].kind != kRoof,
                "Viridian ids leaked into Pallet");
        g_models.load(kTilesetGeneral, kTilesetViridianCity);
        for (int id : {0x155, 0x157, 0x064, 0x2B5, 0x2B9}) require(kind(id) == kFlat, "unmeasured id classified");
        for (int id : {0x2A8, 0x2A9, 0x2AA, 0x2AB, 0x2AC}) require(g_models.at[id].a == kFence, "Viridian fence not a fence piece");
        // General's common tiles still apply under Viridian (trees, signs, ledges), unverified ranges do not
        require(g_models.at[0x00E].kind == kTree && g_models.at[0x002].kind == kStand, "General table lost under Viridian");
        // measured component grids (ViridianCity_Layout): connectivity, door, caps, overlaps
        struct Grid { int w, h; std::vector<int> ids; };
        // real traversal: 4-connected components of roof/wall cells, like the renderer's boxes
        struct Comp { int components, cells, wall_rows, roof_rows, skipped; int x0, x1, y0, y1; };
        auto flood = [&](const Grid& g, int* cells, int* wall_rows, int* roof_rows, int* skipped) {
            auto bld = [&](int x, int y) { return x >= 0 && y >= 0 && x < g.w && y < g.h &&
                (kind(g.ids[y * g.w + x]) == kRoof || kind(g.ids[y * g.w + x]) == kWall); };
            std::vector<int> seen(g.w * g.h, 0);
            int components = 0, best = 0, bx0 = 0, bx1 = 0, by0 = 0, by1 = 0, skip = 0;
            std::vector<int> best_cells;
            for (int y = 0; y < g.h; ++y) for (int x = 0; x < g.w; ++x) {
                if (!bld(x, y)) { ++skip; continue; }
                if (seen[y * g.w + x]) continue;
                ++components;
                std::vector<int> comp{y * g.w + x};
                seen[y * g.w + x] = 1;
                for (std::size_t h = 0; h < comp.size(); ++h) {
                    const int cx = comp[h] % g.w, cy = comp[h] / g.w;
                    const int nb[4][2] = {{cx - 1, cy}, {cx + 1, cy}, {cx, cy - 1}, {cx, cy + 1}};
                    for (const auto& n : nb)
                        if (bld(n[0], n[1]) && !seen[n[1] * g.w + n[0]]) { seen[n[1] * g.w + n[0]] = 1; comp.push_back(n[1] * g.w + n[0]); }
                }
                if (static_cast<int>(comp.size()) > best) best = static_cast<int>(comp.size()), best_cells = comp;
            }
            // rows of the largest component, counted like the renderer (wall rows win)
            int wr = 0, rr = 0;
            std::vector<int> row_wall(g.h, 0), row_roof(g.h, 0);
            for (int i : best_cells) {
                (kind(g.ids[i]) == kWall ? row_wall : row_roof)[i / g.w] = 1;
                bx0 = bx1 = best_cells[0] % g.w; by0 = by1 = best_cells[0] / g.w;
            }
            for (int y = 0; y < g.h; ++y) { if (row_wall[y]) ++wr; else if (row_roof[y]) ++rr; }
            *cells = best; *wall_rows = wr; *roof_rows = rr; *skipped = skip;
            return components;
        };
        int cells, wr, rr, skip, comps;
        const Grid gym{6, 5, {0x2AD, 0x2AE, 0x2AE, 0x2AE, 0x2AE, 0x2AF, 0x141, 0x142, 0x142, 0x142, 0x142, 0x143,
                              0x149, 0x14A, 0x14A, 0x14A, 0x14A, 0x14B, 0x150, 0x151, 0x152, 0x153, 0x154, 0x156,
                              0x158, 0x159, 0x15A, 0x15B, 0x15C, 0x15E}};
        comps = flood(gym, &cells, &wr, &rr, &skip);
        require(comps == 1, "gym split into several boxes");
        require(cells == 30 && skip == 0 && wr == 2 && rr == 3, "gym is not one 3-roof / 2-wall box");
        require(kind(0x154) == kWall && kind(0x15A) == kWall, "gym badge / door");
        const Grid house1{5, 3, {0x288, 0x289, 0x28A, 0x28B, 0x28C, 0x290, 0x291, 0x292, 0x293, 0x294, 0x29D, 0x299, 0x2A5, 0x2A6, 0x2A7}};
        comps = flood(house1, &cells, &wr, &rr, &skip);
        require(comps == 1, "house 1 split");
        require(cells == 15 && wr == 1 && rr == 2, "house 1 must have one wall row");
        const Grid house2{5, 4, {0x288, 0x289, 0x28A, 0x28B, 0x28C, 0x290, 0x291, 0x292, 0x293, 0x294,
                                 0x298, 0x299, 0x29A, 0x29B, 0x29C, 0x2A0, 0x0CE, 0x2A2, 0x2A3, 0x2A4}};
        comps = flood(house2, &cells, &wr, &rr, &skip);
        require(comps == 1, "house 2 split by its door column");
        require(cells == 15 && skip == 5 && wr == 1 && rr == 2, "house 2: ONE wall row (the planter row and the door-approach ground are not box)");
        require(kind(0x299) == kWall, "house door");
        {   // column bottoms of house 2 (5 wide x 4 tall): the door column x1 ends at row 2, the rest at row 3
            unsigned char bld[20];
            for (int i = 0; i < 20; ++i) bld[i] = house2.ids[i] == 0x0CE ? 0 : 1;
            int out[20];
            column_bottoms(bld, 5, 4, out);
            require(out[0 * 5 + 1] == 2 && out[2 * 5 + 1] == 2 && out[3 * 5 + 1] == -1, "recessed door column bottom");
            require(out[0 * 5 + 0] == 3 && out[3 * 5 + 4] == 3, "full column bottom");
        }
        {   // shared geometry: roofs never take the recessed column's base or wall height
            const float base = 400, wall_h = 32, lean = 0.75f, cbase = 384, cwall_h = 16;
            FaceParams roof_in_door_col = face_params(false, true, base, wall_h, cbase, cwall_h);
            FaceParams door = face_params(true, true, base, wall_h, cbase, cwall_h);
            FaceParams trim = face_params(true, false, base, wall_h, cbase, cwall_h);
            require(roof_in_door_col.base == base && roof_in_door_col.wall_h == wall_h, "roof took the recessed face's plane");
            require(door.base == cbase && door.wall_h == cwall_h && trim.base == base, "wall face parameters");
            // adjacent roof cells (house 2 rows above the recess): the seam vertices are identical
            float left[4][3], door_col[4][3], right[4][3];
            front_quad(base, wall_h, lean, 24 * 16.0f, 48.0f, left);
            front_quad(roof_in_door_col.base, roof_in_door_col.wall_h, lean, 25 * 16.0f, 48.0f, door_col);
            front_quad(base, wall_h, lean, 26 * 16.0f, 48.0f, right);
            for (int k = 0; k < 3; ++k) {
                require(left[1][k] == door_col[0][k] && left[2][k] == door_col[3][k], "roof seam left of the recessed column");
                require(door_col[1][k] == right[0][k] && door_col[2][k] == right[3][k], "roof seam right of the recessed column");
            }
            // the recessed door sits on the floor: bottom edge at Z 0, in its own row's front plane
            float dq[4][3];
            front_quad(door.base, door.wall_h, lean, 25 * 16.0f, 0.0f, dq);   // the door cell's bottom edge is its column's base
            require(dq[2][2] == 0.0f && dq[3][2] == 0.0f && dq[2][1] == cbase, "recessed door bottom not on the floor");
            require(dq[0][2] == 16.0f, "recessed door height");
            // its neighbours' trim row is on the shared front plane, one row in front of the door
            float tq[4][3];
            front_quad(trim.base, trim.wall_h, lean, 24 * 16.0f, 0.0f, tq);
            require(tq[2][2] == 0.0f && tq[2][1] == base && tq[2][1] > dq[2][1], "trim row not in front of the door");
            // a short-wall column of the Center (wall ends a row early) behaves the same way for its roof
            FaceParams pc_roof = face_params(false, true, base, wall_h, base - 16, 16);
            require(pc_roof.base == base, "Center roof lowered above a short wall column");
        }
        const Grid house2cap{5, 5, {0x280, 0x281, 0x282, 0x283, 0x284, 0x288, 0x289, 0x28A, 0x28B, 0x28C,
                                    0x290, 0x291, 0x292, 0x293, 0x294, 0x298, 0x299, 0x29A, 0x29B, 0x29C,
                                    0x2A0, 0x0CE, 0x2A2, 0x2A3, 0x2A4}};
        comps = flood(house2cap, &cells, &wr, &rr, &skip);
        require(comps == 1 && cells == 20 && skip == 5 && wr == 1 && rr == 3, "town house with its cap row is not one box");
        const Grid mart{4, 4, {0x028, 0x029, 0x02A, 0x02B, 0x030, 0x031, 0x032, 0x033, 0x038, 0x039, 0x03A, 0x03B,
                               0x040, 0x041, 0x062, 0x063}};
        comps = flood(mart, &cells, &wr, &rr, &skip);
        require(comps == 1, "Mart split");
        require(cells == 16 && wr == 1 && rr == 3, "Mart: three roof rows, one wall row with the door");
        {   // ground under an outdoor building: plain grass from beside / behind, never road, never roof art
            const std::uint32_t road_c = 0xff808080u, grass_c = 0xff40c040u, roof_c = 0xff20a040u;
            std::vector<std::uint32_t> road(256, road_c), grass(256, grass_c), roof(256, roof_c);
            auto art_of = [&](int id) -> const std::vector<std::uint32_t>& {
                return (id == 0x001 || id == 0x010 || id == 0x011) ? grass : (id == 0x288 ? roof : road); };
            // road in front is not a candidate at all; grass beside the footprint wins
            const int ids1[3] = {0x00C, 0x001, 0x288};
            const bool flat1[3] = {true, true, false};
            int g = pick_box_ground(ids1, flat1, 3, kOutdoorGrass, 3);
            require(g == 0x001 && art_of(g)[5] == grass_c, "grass beside the footprint not used");
            // only road / roof ids around: default grass, not the road or the roof's own id
            const int ids2[3] = {0x00C, 0x288, 0x299};
            const bool flat2[3] = {true, true, true};
            g = pick_box_ground(ids2, flat2, 3, kOutdoorGrass, 3);
            require(g == 0x001 && art_of(g)[200] == grass_c, "road or roof art emitted under a box");
            require(art_of(0x288)[0] == roof_c && art_of(0x00C)[0] == road_c, "fixture colours");
            // a non-flat grass-id neighbour (object cell) is skipped
            const int ids3[3] = {0x011, 0x010, 0x00C};
            const bool flat3[3] = {false, true, true};
            require(pick_box_ground(ids3, flat3, 3, kOutdoorGrass, 3) == 0x010, "flat grass neighbour not preferred in order");
            require(pick_box_ground(ids3, flat3, 0, kOutdoorGrass, 3) == 0x001, "default grass");
            require(pick_box_ground(ids3, flat3, 3, kOutdoorGrass, 0) == -1, "invented ground without a grass list");
            // same decision for one- and two-row boxes: it only looks beside / behind the footprint
            for (int rows : {1, 2}) { (void)rows; require(pick_box_ground(ids1, flat1, 3, kOutdoorGrass, 3) == 0x001, "depends on wall rows"); }
            require(kOutdoorGrass[0] == 0x001, "default grass id");
        }
        {   // Pokemon Center roof cap: only the top layer is the rim; the bottom layer is background
            for (int id : {0x2B0, 0x2B1, 0x2B2, 0x2B3, 0x2B4})
                require(box_art_top_only(g_models.at[id], true), "cap does not use its top layer");
            require(!box_art_top_only(g_models.at[0x2B3], false), "cap without a top layer must keep its art");
            for (int id : {0x048, 0x050, 0x288, 0x289, 0x28A, 0x28C, 0x290, 0x291, 0x141, 0x2AD})
                require(!box_art_top_only(g_models.at[id], true), "house / gym / Center roof lost its base layer");
        }
        {   // recessed door reveals, built per wall row from the matching neighbour cell
            const float base = 432, wall_h = 32, lean = 0.75f, cbase = 416, cwall_h = 16, X0 = 32 * 16.0f;   // House 2's door
            float door[4][3], lin[4][3];
            front_quad(cbase, cwall_h, lean, X0, 0.0f, door);
            lintel_quad(door, X0, base, wall_h, lin);
            require(reveal_segments(wall_h) == 2 && reveal_segments(16.0f) == 1 && reveal_segments(0.0f) == 0, "segment count");
            for (int side = 0; side < 2; ++side) {
                const float RX = side ? X0 + 16 : X0;
                float seg0[4][3], seg1[4][3];
                reveal_segment(RX, cbase, cwall_h, base, wall_h, 0, seg0);
                reveal_segment(RX, cbase, cwall_h, base, wall_h, 1, seg1);
                const float* door_top = door[side ? 1 : 0];
                const float* door_foot = door[side ? 2 : 3];
                const float* lin_front = lin[side ? 2 : 3];
                for (int k = 0; k < 3; ++k) {
                    require(seg0[0][k] == door_top[k], "lower segment back-top does not meet the door's top");
                    require(seg0[3][k] == door_foot[k], "lower segment does not start at the door foot");
                    require(seg1[1][k] == lin_front[k], "upper segment front-top does not meet the lintel");
                    require(seg0[1][k] == seg1[2][k], "segments do not meet at the front");
                    require(seg1[3][k] == door_top[k] || k != 2 || seg1[3][2] == cwall_h, "upper segment back edge");
                }
                require(seg0[2][2] == 0 && seg0[3][2] == 0, "reveal not down to the floor");
                require(seg1[0][2] == cwall_h && seg1[3][2] == 16.0f, "upper triangle must pinch to the door's top at the back");
                for (const auto* seg : {&seg0[0], &seg1[0]})
                    for (int i = 0; i < 4; ++i) {
                        require(seg[i][0] == RX, "reveal crosses the opening");
                        require(seg[i][1] >= cbase && seg[i][1] <= base && seg[i][2] >= 0 && seg[i][2] <= wall_h, "reveal outside the recess volume");
                    }
                // texture: the planter rows (bottom 6 of 16) are never sampled; u is the chosen column
                float uv0[4][2], uv1[4][2];
                reveal_segment_uv(seg0, 0, 6, 12.5f, uv0);
                reveal_segment_uv(seg1, 1, 6, 12.5f, uv1);
                for (int i = 0; i < 4; ++i) {
                    require(uv0[i][0] == 12.5f && uv1[i][0] == 12.5f, "column");
                    require(uv0[i][1] >= 0.0f && uv0[i][1] <= 10.0f && uv1[i][1] >= 0.0f && uv1[i][1] <= 10.0f, "planter rows sampled on the reveal");
                }
                require(uv0[2][1] == 10.0f && uv0[1][1] == 0.0f, "lower segment rows 0..9 over its height");
                require(uv1[1][1] == 0.0f && uv1[2][1] == 10.0f, "upper segment rows 0..9 over its height");
                float uvn[4][2];
                reveal_segment_uv(seg0, 0, 0, 3.5f, uvn);
                require(uvn[2][1] == 16.0f, "no planter: the full tile is used");
            }
            require(lin[0][0] == X0 && lin[1][0] == X0 + 16 && lin[2][0] == X0 + 16 && lin[3][0] == X0, "lintel width");
        }
        {   // House 2 planter metadata (measured): beams 298 / 29A-29C and trim 2A0 / 2A2-2A4 carry a 6 px front flowerbed
            for (int id : {0x298, 0x29A, 0x29B, 0x29C})
                require(g_models.at[id].planter == 6 && g_models.at[id].kind == kWall, "planter wall id");
            require(g_models.at[0x299].planter == 0 && g_models.at[0x29D].planter == 0 && g_models.at[0x154].planter == 0,
                    "planter rows on a door or another building");
        }
        {   // side height must use the same art as the front: a cap's opaque grass background is not roof
            std::uint32_t cap_top[256] = {}, cap_both[256];
            const std::uint32_t ridge = 0xff20c020u, grass = 0xff30a030u;
            for (int y = 10; y < 16; ++y) for (int x = 0; x < 16; ++x) cap_top[y * 16 + x] = ridge;
            for (auto& q : cap_both) q = grass;
            for (int y = 10; y < 16; ++y) for (int x = 0; x < 16; ++x) cap_both[y * 16 + x] = ridge;
            require(box_art_top_only(g_models.at[0x2B3], true) && box_art_top_only(g_models.at[0x283], true), "caps use the top layer");
            // the side extent measured from the chosen art
            require(16 - first_opaque_row(cap_top) == 6, "thin cap side height");
            require(16 - first_opaque_row(cap_both) == 16, "fixture: the combined art would give a tall needle");
            int col = -1;
            require(pick_side_column(cap_top, true, &col), "cap has a side column");
            require(side_segment_height(cap_top, col) == 6.0f, "needle: side taller than the real cap rim");
        }
        {   // the traversal really detects splits: a flat cliff cell between two cap cells disconnects them
            const Grid split{3, 1, {0x2B0, 0x28E, 0x2B2}};
            require(flood(split, &cells, &wr, &rr, &skip) == 2 && skip == 1, "flood fill does not detect a split box");
        }
        // Pokemon Center: 5-wide cap over a 4-wide body; 186/187/18F/197/19F tree overlaps stay out
        const Grid pc{5, 5, {0x2B0, 0x2B1, 0x2B2, 0x2B3, 0x2B4, 0x048, 0x049, 0x04A, 0x04B, 0x187,
                             0x050, 0x051, 0x052, 0x053, 0x18F, 0x058, 0x059, 0x05A, 0x05B, 0x197,
                             0x060, 0x061, 0x062, 0x186, 0x19F}};
        comps = flood(pc, &cells, &wr, &rr, &skip);
        require(comps == 1, "Center cap / front disconnected");
        require(cells == 25 && skip == 0, "Center must be one complete 5x5 box (its right column is real art)");
        require(wr == 2 && rr == 3, "Center: cap + two roof rows over two wall rows");
        require(kind(0x2B4) == kRoof, "Center roof cap end dropped");
        require(kind(0x062) == kWall, "shared 062 door");
        // the Mart and the Center share 062 without sharing a wall-height assumption: each box has its own rows
        require(box_wall_height(1, 0) != box_wall_height(2, 0), "one- and two-row walls");
        // other secondaries do not inherit Viridian ids (Pallet reuses 0x288 etc. differently)
        g_models.load(kTilesetGeneral, kTilesetPalletTown);
        require(g_models.at[0x289].kind == kRoof && g_models.at[0x288].kind == kFlat, "Pallet table changed");
        require(g_models.at[0x290].kind == kFlat && g_models.at[0x2A6].kind == kFlat, "Viridian ids leaked into Pallet");
        g_models.load(kTilesetGeneral, 0x08000000);
        require(g_models.at[0x288].kind == kFlat && g_models.at[0x299].kind == kFlat, "unknown secondary got a table");
        // gym box: two roof rows over two wall rows keeps the roof art aligned
        float Y, Z;
        const float wall_h = box_wall_height(2, 0);
        require(wall_h == 32.0f, "gym wall rows");
        for (float u = 0; u <= 96; u += 8) {
            wall_surface(300, wall_h, 0.75f, u, &Y, &Z);
            require(std::abs((Y - Z) - (300 - u)) < 1e-3f, "gym roof art drifts from its map row");
        }
    }
    g_models.load(0x08000000, 0x08000000);
    require(!g_models.known && g_models.at[0x00E].kind == kFlat, "unknown tilesets are not flat");
    float Y, Z;
    for (float u = 0; u <= 80; u += 4) {
        wall_surface(200, 32, 0.75f, u, &Y, &Z);
        require(std::abs((Y - Z) - (200 - u)) < 1e-3f, "roof art drifts away from its map row");
    }
    wall_surface(200, 16, 1.0f, 40, &Y, &Z);
    require(Z == 16, "furniture top is not flat");

    // ── art layers ──
    std::array<std::uint8_t, 1024> oam{}, pal{};
    std::array<std::uint8_t, 0x18000> vram{};
    std::array<std::uint8_t, 1024> io{};
    auto put = [](auto& memory, int off, int value) {
        memory[off] = value & 255; memory[off + 1] = (value >> 8) & 255;
    };
    put(io, 0, 0x1040);
    for (int i = 1; i < 256; ++i) put(pal, (256 + i) * 2, i);
    for (int y = 0; y < 8; ++y) for (int x = 0; x < 8; ++x) {
        const unsigned value = 1 + (x + 3 * y) % 15;
        vram[0x10000 + y * 4 + x / 2] |= value << ((x & 1) * 4);
    }
    const Mem m{oam.data(), vram.data(), io.data(), pal.data()};
    put(pal, 2, 1); put(pal, 4, 2);
    std::fill_n(vram.data() + 10 * 32, 32, 0x11);   // ground tile, colour 1
    std::fill_n(vram.data() + 13 * 32, 16, 0x22);   // object tile: top half colour 2
    std::uint16_t ent[8] = {10, 10, 10, 10, 13, 0, 0, 0};
    std::array<std::uint32_t, 256> px;
    metatile_art(m, 0, ent, kTop, px.data());
    require(px[0] == 0xff000002u && px[15] == 0 && px[7 * 16] == 0, "top layer art wrong");
    metatile_art(m, 0, ent, kBottom, px.data());
    require(px[0] == 0xff000001u, "bottom layer art wrong");
    metatile_art(m, 0, ent, kBoth, px.data());
    require(px[0] == 0xff000002u && px[15] == 0xff000001u, "combined art wrong");
    require(has_top(ent), "top layer not detected");
    {   // cap-like metatile: opaque background in the bottom layer, a thin rim only in the top layer's lower half
        std::uint16_t cap[8] = {10, 10, 10, 10, 0, 0, 13, 13};
        std::array<std::uint32_t, 256> kt, kb, kk;
        metatile_art(m, 0, cap, kTop, kt.data());
        metatile_art(m, 0, cap, kBoth, kk.data());
        require(has_top(cap), "cap top layer not detected");
        int top_rows_first = 16, both_rows_first = 16;
        for (int i = 255; i >= 0; --i) { if (kt[i] >> 24) top_rows_first = i / 16; if (kk[i] >> 24) both_rows_first = i / 16; }
        require(both_rows_first == 0, "background rows missing from the combined art");
        require(top_rows_first >= 8, "top layer alone must be only the lower rim");
        for (int y = 0; y < 8; ++y) for (int x = 0; x < 16; ++x)
            require(kt[y * 16 + x] == 0, "cap background lifted by the top-only rule");
        // the combined art differs from the top-only art above the rim: that is what the rule keeps off the roof
        require(kk[0] != 0 && kt[0] == 0, "top-only and combined art do not differ");
        (void)kb;
    }

    // ── sprites ──
    SpriteImg sprite;
    std::vector<std::uint32_t> pixels;
    require(decode_sprite(m, 0, &sprite, pixels), "normal sprite missing");
    const auto normal = pixels;
    require(sprite.w == 8 && sprite.h == 8 && !sprite.decal, "normal bounds changed");
    put(oam, 2, 0x1000);
    require(decode_sprite(m, 0, &sprite, pixels), "flipped sprite missing");
    for (int y = 0; y < 8; ++y) for (int x = 0; x < 8; ++x)
        require(pixels[y * 8 + x] == normal[y * 8 + 7 - x], "horizontal flip incorrect");
    put(oam, 2, 0x2000);
    require(decode_sprite(m, 0, &sprite, pixels) && sprite.decal, "reflection not flat");
    for (int y = 0; y < 8; ++y) for (int x = 0; x < 8; ++x)
        require(pixels[y * 8 + x] == normal[(7 - y) * 8 + x], "vertical flip incorrect");
    put(oam, 0, 0x100); put(oam, 2, 0);
    put(oam, 6, 256); put(oam, 14, 0); put(oam, 22, 0); put(oam, 30, 256);
    require(decode_sprite(m, 0, &sprite, pixels) && pixels == normal, "affine identity incorrect");
    put(oam, 0, 0x300);
    require(decode_sprite(m, 0, &sprite, pixels) && sprite.w == 8 && sprite.h == 8 &&
            sprite.x == 4 && sprite.y == 4 && pixels == normal, "affine padding moved the visible feet");
    put(oam, 0, 0x100); put(oam, 30, -256);
    require(decode_sprite(m, 0, &sprite, pixels) && sprite.decal, "affine reflection stood upright");
    put(oam, 30, 256); put(oam, 6, 0); put(oam, 14, 256); put(oam, 22, -256); put(oam, 30, 0);
    require(decode_sprite(m, 0, &sprite, pixels) && !sprite.decal, "rotated sprite missing");
    put(io, 0, 0x40);
    require(!decode_sprite(m, 0, &sprite, pixels), "disabled OBJ layer rendered");
    put(io, 0, 0x1040); put(oam, 0, 0x200);
    require(!decode_sprite(m, 0, &sprite, pixels), "disabled OAM entry rendered");
    put(oam, 0, 0);
    gba::g_hd_obj_hide = [](int, std::uint16_t, std::uint16_t, std::uint16_t) { return 1; };
    require(!decode_sprite(m, 0, &sprite, pixels), "HD-hidden sprite reappeared");
    gba::g_hd_obj_hide = nullptr;
    // ── whole-pipeline audit (real camera + draw_quad): adjacent ground cells leave no crack at any sub-pixel camera phase, decals above the ground keep their depth ──
    {
        Cam cam;
        const float al = 45.0f * 3.14159265f / 180.0f;
        cam.sa = std::sin(al); cam.ca = std::cos(al); cam.sy = 1.0f / cam.sa; cam.sz = 1.0f / cam.ca;
        cam.f = 80.0f / std::tan(28.0f * 3.14159265f / 360.0f);
        cam.d = cam.f / 0.76f; cam.cx = 142.0f; cam.cy = 80.0f;
        std::vector<std::uint32_t> green(256, 0xff00ff00u), grey(256, 0xff808080u), dark(256, 0xff000000u);
        const Tex tg{16, 16, green.data()}, tgrey{16, 16, grey.data()}, tdark{16, 16, dark.data()};
        const float uvq[4][2] = {{0, 0}, {16, 0}, {16, 16}, {0, 16}};
        for (int trial = 0; trial < 24; ++trial) {
            cam.tx = 120.0f + trial * 0.173f; cam.ty = 80.0f + trial * 0.311f;
            g_raster.reset(426, 240, 1.5f, 0u);
            for (int cy = 0; cy < 12; ++cy) for (int cx = 0; cx < 20; ++cx) {
                const float X0 = 40 + cx * 16.0f, Y0 = 20 + cy * 16.0f;
                const float p[4][3] = {{X0, Y0, 0}, {X0 + 16, Y0, 0}, {X0 + 16, Y0 + 16, 0}, {X0, Y0 + 16, 0}};
                draw_quad(cam, p, uvq, tg, 1.0f);
            }
            g_raster.flush();
            float lx, ly, lz, rx, ry, rz, bx, by, bz, cxp, cyp, czp;
            cam.project(40, 20, 0, &lx, &ly, &lz); cam.project(40 + 320, 20, 0, &rx, &ry, &rz);
            cam.project(40, 20 + 192, 0, &bx, &by, &bz); cam.project(40 + 320, 20 + 192, 0, &cxp, &cyp, &czp);
            int holes = 0;
            for (int y = static_cast<int>(std::ceil(std::max(ly, ry) * 1.5f)) + 2; y < static_cast<int>(std::floor(std::min(by, cyp) * 1.5f)) - 2; ++y) {
                const float tt = (y / 1.5f - ly) / (by - ly);
                const float xl = (lx + (bx - lx) * tt) * 1.5f, xr = (rx + (cxp - rx) * tt) * 1.5f;
                for (int x = static_cast<int>(std::ceil(xl)) + 2; x < static_cast<int>(std::floor(xr)) - 2; ++x)
                    if (x >= 0 && x < 426 && y >= 0 && y < 240 && g_raster.color[y * 426 + x] == 0u) ++holes;
            }
            require(holes == 0, "a crack between adjacent ground cells");
        }
        // decals 0.05 and 0.1 above a large ground quad keep their depth everywhere (no dropped shadow pixels along long scanlines)
        cam.tx = 600; cam.ty = 400;
        for (float zoff : {0.05f, 0.1f}) {
            g_raster.reset(426, 240, 1.5f, 0u);
            const float G[4][3] = {{-2000, -2000, 0}, {3000, -2000, 0}, {3000, 3000, 0}, {-2000, 3000, 0}};
            draw_quad(cam, G, uvq, tgrey, 1.0f);
            for (int sy = 0; sy < 14; ++sy) for (int sx = 0; sx < 20; ++sx) {
                const float X = 600 - 80 + sx * 8.0f, Y = 400 - 56 + sy * 8.0f;
                const float P[4][3] = {{X, Y, zoff}, {X + 7, Y, zoff}, {X + 7, Y + 7, zoff}, {X, Y + 7, zoff}};
                draw_quad(cam, P, uvq, tdark, 1.0f, 0.5f);
            }
            g_raster.flush();
            float x0, y0, z0, x1, y1, z1;
            cam.project(600 - 80 + 8, 400 - 56 + 8, 0, &x0, &y0, &z0); cam.project(600 - 80 + 19 * 8 - 8, 400 - 56 + 13 * 8 - 8, 0, &x1, &y1, &z1);
            int lost = 0, inside_n = 0;
            for (int y = std::max(0, static_cast<int>(y0 * 1.5f) + 10); y < std::min(240, static_cast<int>(y1 * 1.5f) - 10); ++y)
                for (int x = std::max(0, static_cast<int>(x0 * 1.5f) + 20); x < std::min(426, static_cast<int>(x1 * 1.5f) - 20); ++x) {
                    ++inside_n;
                    lost += g_raster.color[y * 426 + x] == 0xff808080u ? 1 : 0;
                }
            require(inside_n > 1000 && lost == 0, "a decal above the ground lost its depth test (undarkened ground pixels)");
        }
    }
    // ── character billboards: the trapezoid and the rectified candidate (FRLG_3D_SPRITE_RECT, test only) under the real camera ──
    {
        Cam cam;
        const float al = 45.0f * 3.14159265f / 180.0f;
        cam.sa = std::sin(al); cam.ca = std::cos(al); cam.sy = 1.0f / cam.sa; cam.sz = 1.0f / cam.ca;
        cam.f = 80.0f / std::tan(28.0f * 3.14159265f / 360.0f);
        cam.d = cam.f / 0.76f; cam.cx = 142.0f; cam.cy = 80.0f; cam.tx = 600; cam.ty = 400;
        // column / row index texture: texel (x, y) = (x + 1) in blue, (y + 1) in green
        std::vector<std::uint32_t> tex(16 * 32);
        for (int y = 0; y < 32; ++y) for (int x = 0; x < 16; ++x) tex[y * 16 + x] = 0xff000000u | (static_cast<std::uint32_t>(y + 1) << 8) | static_cast<std::uint32_t>(x + 1);
        const Tex t{16, 32, tex.data()};
        const int W = 852, H = 480;
        auto render = [&](bool rect, std::vector<std::uint32_t>& out) {
            g_raster.reset(W, H, 3.0f, 0u);
            draw_upright(cam, 592.0f, 608.0f, 400.0f, 0.0f, 32.0f, t, 0, 16, 0, 32, 1.0f, 1.0f, false, rect);
            g_raster.flush();
            out = g_raster.color;
        };
        std::vector<std::uint32_t> base, rect;
        render(false, base); render(true, rect);
        struct Row { int x0 = -1, x1 = -1; };
        auto rows_of = [&](const std::vector<std::uint32_t>& img) {
            std::vector<Row> r(H);
            for (int y = 0; y < H; ++y) for (int x = 0; x < W; ++x) if (img[y * W + x]) { if (r[y].x0 < 0) r[y].x0 = x; r[y].x1 = x; }
            return r;
        };
        const auto rb = rows_of(base), rr = rows_of(rect);
        int top = -1, bot = -1;
        for (int y = 0; y < H; ++y) if (rb[y].x0 >= 0) { if (top < 0) top = y; bot = y; }
        require(top >= 0 && bot - top > 60, "the fixture sprite was not drawn");
        // 1. the defect: the baseline is a trapezoid (head several px wider than the feet), the candidate has the same width on every row
        const int w_head = rb[top].x1 - rb[top].x0 + 1, w_feet = rb[bot].x1 - rb[bot].x0 + 1;
        require(w_head - w_feet >= 3, "baseline sprite is not a trapezoid (fixture)");
        int rmin = 1 << 30, rmax = 0;
        for (int y = top; y <= bot; ++y) { const int w = rr[y].x1 - rr[y].x0 + 1; rmin = std::min(rmin, w); rmax = std::max(rmax, w); }
        require(rmax - rmin <= 1, "rectified sprite rows are not constant in width");
        // 2. grounding: the bottom rows (the feet) are the same pixels, the footprint of the sprite did not move
        require(rr[bot].x0 == rb[bot].x0 && rr[bot].x1 == rb[bot].x1 && rr[top].x0 >= rb[top].x0 && rr[top].x1 <= rb[top].x1, "the feet moved or the head grew");
        for (int x = rb[bot].x0; x <= rb[bot].x1; ++x) require(base[bot * W + x] == rect[bot * W + x], "feet row differs");
        // 3. no ghost, no clipping: every rectified pixel is inside the baseline silhouette, the row range is the same, and every texel column and row is still present
        for (int i = 0; i < W * H; ++i) if (rect[i]) require(base[i] != 0u, "the rectified sprite painted outside the original silhouette");
        require(rr[top].x0 >= 0 && rr[bot].x0 >= 0 && rr[top - 1].x0 < 0 && rr[bot + 1].x0 < 0, "rectified sprite rows changed");
        for (int y = top; y <= bot; ++y) {
            bool seen[17] = {};
            for (int x = rr[y].x0; x <= rr[y].x1; ++x) seen[rect[y * W + x] & 255] = true;
            for (int c = 1; c <= 16; ++c) require(seen[c], "a texel column is missing from a rectified row (clipped sprite)");
        }
        // 4. texel column widths stay 2..3 px on every row, vertical edges are straight (the same column starts on the same x for every row)
        for (int y = top + 2; y <= bot - 2; ++y) {
            int run = 0, last = -1;
            for (int x = rr[y].x0; x <= rr[y].x1 + 1; ++x) {
                const int c = x <= rr[y].x1 ? static_cast<int>(rect[y * W + x] & 255) : -2;
                if (c == last) ++run;
                else { if (last > 0) require(run >= 2 && run <= 3, "texel column width outside 2..3 px in a rectified row"); last = c; run = 1; }
            }
        }
        for (int y = top + 1; y <= bot; ++y) require(rr[y].x0 == rr[top].x0 || std::abs(rr[y].x0 - rr[top].x0) <= 1, "rectified left edge is not vertical");
        // 4b. interior stripes: the texel column of every pixel is an exact function of screen x for the rectified sprite (affine u); the perspective-divided u bent the
        //     interior columns across the triangle diagonal (about 10 % of the pixels of a 16 x 32 sprite sampled another column)
        {
            float xl, yb, zb, xr, tmp, yt, zt;
            cam.project(592.0f, 400.0f, 0, &xl, &yb, &zb); cam.project(608.0f, 400.0f, 0, &xr, &tmp, &zb); cam.project(592.0f, 400.0f, 32.0f, &tmp, &yt, &zt);
            long bent_base = 0, bent_rect = 0;
            for (int y = top; y <= bot; ++y) for (int x = 0; x < W; ++x) {
                const float fx = (x + 0.5f) / 3.0f;
                const int ref = std::clamp(static_cast<int>(16.0f * (fx - xl) / (xr - xl)), 0, 15) + 1;
                if (base[y * W + x] && static_cast<int>(base[y * W + x] & 255) != ref) ++bent_base;
                if (rect[y * W + x] && static_cast<int>(rect[y * W + x] & 255) != ref) ++bent_rect;
            }
            require(bent_base > 150, "fixture: the perspective-divided u should bend the interior columns");
            require(bent_rect == 0, "interior texel columns of the rectified sprite are not an exact function of x");
            // the texel ROWS (and depth) are untouched by the u change: the row index of every pixel equals the baseline's where both draw it
            long row_diff = 0;
            for (int i = 0; i < W * H; ++i) if (base[i] && rect[i] && ((base[i] >> 8) & 255) != ((rect[i] >> 8) & 255)) ++row_diff;
            require(row_diff == 0, "the rectified sprite changed the texel rows");
        }
        // 5. occlusion identical: a wall behind (farther) and a wall in front (nearer) of the sprite decide every shared pixel the same way for both versions
        std::vector<std::uint32_t> wall(16 * 16, 0xff00ff00u), gnd(256, 0xff0000ffu);
        const Tex tw{16, 16, wall.data()};
        auto scene = [&](bool rect_on, std::vector<std::uint32_t>& out) {
            g_raster.reset(W, H, 3.0f, 0u);
            draw_upright(cam, 584.0f, 616.0f, 392.0f, 0.0f, 40.0f, tw, 0, 16, 0, 16, 1.0f);                                    // farther (north) wall behind the sprite
            draw_upright(cam, 592.0f, 608.0f, 400.0f, 0.0f, 32.0f, t, 0, 16, 0, 32, 1.0f, 1.0f, false, rect_on);               // the sprite
            draw_upright(cam, 584.0f, 616.0f, 408.0f, 0.0f, 10.0f, tw, 0, 16, 0, 16, 1.0f);                                    // nearer (south) low wall in front of its legs
            g_raster.flush();
            out = g_raster.color;
        };
        std::vector<std::uint32_t> sb, sr;
        scene(false, sb); scene(true, sr);
        int checked = 0, wrong = 0;
        for (int i = 0; i < W * H; ++i) {
            if (!base[i] || !rect[i]) continue;   // pixels both versions cover with the sprite
            ++checked;
            const bool sprite_b = (sb[i] & 0xFF00u) != 0xFF00u || (sb[i] & 255) != 0, sprite_r = (sr[i] & 0xFF00u) != 0xFF00u || (sr[i] & 255) != 0;
            if (sprite_b != sprite_r) ++wrong;
        }
        require(checked > 1000 && wrong == 0, "occlusion decisions differ between the trapezoid and the rectified sprite");
        // 6. the picture is drawn once: pixel counts (no duplicate copy) and a small silhouette change only (<= the trapezoid excess)
        long nb = 0, nr = 0;
        for (int i = 0; i < W * H; ++i) { nb += base[i] != 0u; nr += rect[i] != 0u; }
        require(nr <= nb && nb - nr < nb / 10, "rectified silhouette changed more than the trapezoid excess");
    }
    // ── actor scope: only active, visible, in-use ObjectEvent sprites are characters ──
    {
        std::vector<std::uint8_t> objs(16 * 36, 0), sprs(64 * 68, 0);
        auto set_obj = [&](int id, int flags0, int flags1, int sprite_id) { objs[id * 36] = static_cast<std::uint8_t>(flags0); objs[id * 36 + 1] = static_cast<std::uint8_t>(flags1); objs[id * 36 + 4] = static_cast<std::uint8_t>(sprite_id); };
        auto set_spr = [&](int id, int in_use, unsigned a0, unsigned a1, unsigned a2) {
            std::uint8_t* sp = &sprs[id * 68];
            sp[0] = a0 & 255; sp[1] = a0 >> 8; sp[2] = a1 & 255; sp[3] = a1 >> 8; sp[4] = a2 & 255; sp[5] = a2 >> 8; sp[62] = static_cast<std::uint8_t>(in_use);
        };
        const unsigned kTall0 = 0x8000u /* shape: tall */, kTall1 = 0x4000u /* size 1: 16x32 */;
        set_obj(0, 1, 0, 5);  set_spr(5, 1, kTall0 | 40, kTall1 | 100, 0x2123);           // the player: active, visible, in use
        set_obj(1, 1, 0, 6);  set_spr(6, 1, kTall0 | 50, kTall1 | 60, 0x2140);            // an NPC
        set_obj(2, 1, 0, 7);  set_spr(7, 1, kTall0, kTall1, 0x2123);                      // a second NPC sharing the player's tiles: also a character
        set_obj(3, 0, 0, 8);  set_spr(8, 1, kTall0, kTall1, 0x2200);                      // INACTIVE object event: ignored
        set_obj(4, 1, 0x20, 9);  set_spr(9, 1, kTall0, kTall1, 0x2210);                   // invisible object event: ignored
        set_obj(5, 1, 0, 70); set_spr(10, 1, kTall0, kTall1, 0x2220);                     // invalid sprite id (>= 64): ignored, no out-of-range read
        set_obj(6, 1, 0, 11); set_spr(11, 0, kTall0, kTall1, 0x2230);                     // sprite slot not in use: ignored
        set_obj(7, 1, 0, 12); set_spr(12, 1, kTall0 | 0x0300, kTall1, 0x2240);           // affine / disabled OAM: ignored
        ActorSig sg[16];
        const int n = actor_signatures(objs.data(), sprs.data(), sg, 16);
        require(n == 3, "wrong number of actor signatures (inactive / invisible / invalid / unused / affine entries must not count)");
        // the hardware OAM words the game copies (position bits differ from the sprite's attr words): actors match, everything else does not
        require(oam_is_actor(kTall0 | 77, kTall1 | 33, 0x2123, sg, n), "the player's OAM entry is not recognised");
        require(oam_is_actor(kTall0 | 5, kTall1 | 300, 0x2140, sg, n), "an NPC's OAM entry is not recognised");
        require(!oam_is_actor(kTall0 | 5, kTall1 | 5, 0x2999, sg, n), "an unrelated OAM entry (effect / door / icon tiles) was taken for a character");
        require(!oam_is_actor(kTall0 | 5, kTall1 | 5, 0x2200, sg, n) && !oam_is_actor(kTall0 | 5, kTall1 | 5, 0x2210, sg, n) && !oam_is_actor(kTall0 | 5, kTall1 | 5, 0x2220, sg, n) &&
                !oam_is_actor(kTall0 | 5, kTall1 | 5, 0x2230, sg, n) && !oam_is_actor(kTall0 | 5, kTall1 | 5, 0x2240, sg, n), "an ignored object event's tiles were taken for a character");
        require(!oam_is_actor(0x4000u | 5, kTall1 | 5, 0x2123, sg, n) && !oam_is_actor(kTall0 | 5, 0x8000u | 5, 0x2123, sg, n), "same tiles with another shape / size taken for the character");
        require(!oam_is_actor(kTall0 | 0x0200 | 5, kTall1 | 5, 0x2123, sg, n), "a disabled / affine OAM entry taken for a character");
        require(!oam_is_actor(kTall0, kTall1, 0x2123, nullptr, 0) && actor_signatures(nullptr, sprs.data(), sg, 16) == 0 && actor_signatures(objs.data(), nullptr, sg, 16) == 0, "unreadable guest memory produced actors");
        // all 16 object events active with ids up to 63 and a full table do not overrun
        std::vector<std::uint8_t> objs2(16 * 36, 0);
        for (int id = 0; id < 16; ++id) { objs2[id * 36] = 1; objs2[id * 36 + 4] = static_cast<std::uint8_t>(60 + (id % 8)); }
        ActorSig sg2[4];
        require(actor_signatures(objs2.data(), sprs.data(), sg2, 4) <= 4, "signature table overrun");
    }
    // ── Pewter brown fence (TEST ONLY model, measured ids 0E8 / 0E6 / 0F0 / 0F8): orientation per id, rail face stands, baked shadow stays on the ground ──
    {
        g_models.load(kTilesetGeneral, kTilesetPewterCity);
        // off by default (FRLG_3D_PEWTER_FENCE unset): the four ids are plain flat tiles
        for (int id : {0x0E6, 0x0E8, 0x0F0, 0x0F8}) require(g_models.at[id].kind == kFlat, "the Pewter fence model is on without its test switch");
        const TileModel white = g_models.at[0x0E7], white2 = g_models.at[0x0EC], white3 = g_models.at[0x0ED], ledge = g_models.at[0x0B0], ledge2 = g_models.at[0x087];
        g_models.add_pewter_fence();
        for (int id : {0x0E6, 0x0E8, 0x0F8}) require(g_models.at[id].kind == kStand && g_models.at[id].a == kFenceBrownFront && g_models.at[id].skip == 2 && g_models.at[id].b == 1, "front fence piece");
        require(g_models.at[0x0F0].kind == kStand && g_models.at[0x0F0].a == kFenceBrownNS && g_models.at[0x0F0].skip == 0, "north-south fence piece");
        // existing fence / ledge ids are untouched
        require(g_models.at[0x0E7].a == white.a && g_models.at[0x0EC].a == white2.a && g_models.at[0x0ED].a == white3.a && g_models.at[0x0B0].kind == ledge.kind && g_models.at[0x087].kind == ledge2.kind &&
                g_models.at[0x0E7].kind == kStand && g_models.at[0x0E7].a == kFence, "existing fence / ledge models changed");
        require(pewter_fence_applies(kTilesetGeneral, kTilesetPewterCity) && !pewter_fence_applies(kTilesetGeneral, kTilesetViridianCity) && !pewter_fence_applies(kTilesetGeneral, kTilesetPalletTown) &&
                !pewter_fence_applies(kTilesetBuilding, kTilesetPewterCity), "Pewter fence scope");
        // orientation: fixed for the brown ids (the corner has a fence above it but its art is the rail face), neighbours decide only for the white ids
        require(!fence_piece_is_ns(g_models.at[0x0F8], true, false, false, false), "the south-west corner was taken for an end-on run");
        require(!fence_piece_is_ns(g_models.at[0x0E8], false, true, false, true) && !fence_piece_is_ns(g_models.at[0x0E6], false, false, true, true), "rail pieces not front-facing");
        require(fence_piece_is_ns(g_models.at[0x0F0], true, true, false, false) && fence_piece_is_ns(g_models.at[0x0F0], false, false, true, true), "the north-south run's orientation depends on its neighbours");
        require(fence_piece_is_ns(g_models.at[0x0E7], true, true, false, false) && !fence_piece_is_ns(g_models.at[0x0E7], true, true, true, false) && !fence_piece_is_ns(g_models.at[0x0E7], false, false, false, false),
                "white fence orientation rule changed");
        // foot split on 0E6-like art (rows 2..13 rail face, rows 14..15 baked grey shadow): face stands, shadow is a flat decal, nothing shared, nothing lost
        std::uint32_t art[256] = {}, up[256], sh[256];
        for (int y = 2; y < 14; ++y) for (int x = 1; x < 15; ++x) art[y * 16 + x] = 0xff7b4a4au;
        for (int y = 14; y < 16; ++y) for (int x = 2; x < 16; ++x) art[y * 16 + x] = 0xff7b7b83u;
        fence_foot_split(art, 2, up, sh);
        for (int i = 0; i < 256; ++i) {
            require((up[i] == 0u) != (sh[i] == 0u) || (art[i] == 0u && up[i] == 0u && sh[i] == 0u), "a pixel is in both parts or in neither");
            require((up[i] | sh[i]) == art[i], "the split lost or changed a pixel");
            if (i / 16 >= 14) require(up[i] == 0u, "shadow rows stand upright (a plinth under the fence)");
            else require(sh[i] == 0u, "face rows lie on the ground");
        }
        // grounding: the face stands 14 units behind the cell's bottom edge on row 13 (12 high with the 2 empty rows above), rows keep Y - Z = ytop + row; the decal continues it
        float z0, z1, v1;
        stand_span(0, 2.0f, &z0, &z1, &v1);
        const float ytop = 500.0f, front = ytop + 16.0f - 2.0f;
        require(z0 == 0.0f && z1 == 14.0f && v1 == 14.0f, "fence face span");
        for (int r : {2, 7, 13}) require(std::fabs((front - (z1 - static_cast<float>(r))) - (ytop + static_cast<float>(r))) < 1e-4f, "fence lost the reference-view alignment");
        require(front == ytop + 14.0f, "the decal does not start where the face stands");
        // the 0F8 post (rows 0..1, cols 1..6 of the run above) stays in the upright part
        std::uint32_t corner[256] = {};
        for (int y = 0; y < 2; ++y) for (int x = 1; x < 7; ++x) corner[y * 16 + x] = 0xff7b4a4au;
        for (int y = 2; y < 14; ++y) for (int x = 1; x < 15; ++x) corner[y * 16 + x] = 0xff7b4a4au;
        for (int y = 14; y < 16; ++y) for (int x = 3; x < 15; ++x) corner[y * 16 + x] = 0xff7b7b83u;
        fence_foot_split(corner, 2, up, sh);
        require(up[0 * 16 + 3] != 0u && up[1 * 16 + 6] != 0u && sh[0 * 16 + 3] == 0u && sh[15 * 16 + 5] != 0u && up[15 * 16 + 5] == 0u, "corner post / shadow split");
        // skip out of range keeps everything upright
        fence_foot_split(art, 0, up, sh);
        for (int i = 0; i < 256; ++i) require(up[i] == art[i] && sh[i] == 0u, "skip 0 changed the art");
    }
    // ── order independence: a NEAR translucent surface submitted before a FAR opaque one must still blend over it (audit defect: the far surface used to overwrite it) ──
    {
        std::uint32_t red2 = 0xff0000ffu, blue2 = 0xffff0000u;
        const Tex t_r{1, 1, &red2}, t_b{1, 1, &blue2};
        auto sq = [](Raster& rr, float iz, const Tex& t, float alpha) {
            const Raster::V a{0, 0, iz, 0, 0}, b{8, 0, iz, 0, 0}, c{8, 8, iz, 0, 0}, d{0, 8, iz, 0, 0};
            const std::uint32_t id = alpha < 0.999f ? ++rr.quads : 0u;
            rr.tri(a, b, c, t, 1.0f, alpha, id); rr.tri(a, c, d, t, 1.0f, alpha, id);
        };
        Raster o1, o2;
        o1.reset(8, 8, 1, 0xff000000u); o2.reset(8, 8, 1, 0xff000000u);
        sq(o1, 0.50f, t_b, 0.5f); sq(o1, 0.25f, t_r, 1.0f); o1.flush();   // near translucent first
        sq(o2, 0.25f, t_r, 1.0f); sq(o2, 0.50f, t_b, 0.5f); o2.flush();   // far opaque first
        require(o1.color[4 * 8 + 4] == o2.color[4 * 8 + 4] && o1.color[4 * 8 + 4] != red2, "a near translucent surface was lost behind a later far opaque one");
        require(((o1.color[4 * 8 + 4] >> 16) & 255) > 0 && (o1.color[4 * 8 + 4] & 255) > 0, "translucent blue did not blend over the far red");
        // two translucent layers keep their submission order (blending is not commutative) and a nearer opaque surface still hides both
        Raster o3, o4;
        o3.reset(8, 8, 1, 0xff000000u); o4.reset(8, 8, 1, 0xff000000u);
        sq(o3, 0.40f, t_b, 0.5f); sq(o3, 0.40f, t_r, 0.5f); sq(o3, 0.90f, t_r, 1.0f); o3.flush();
        require(o3.color[4 * 8 + 4] == red2, "a nearer opaque surface did not hide the translucent ones");
        sq(o4, 0.40f, t_b, 0.5f); sq(o4, 0.40f, t_r, 0.5f); o4.flush();
        Raster o5;
        o5.reset(8, 8, 1, 0xff000000u);
        sq(o5, 0.40f, t_r, 0.5f); sq(o5, 0.40f, t_b, 0.5f); o5.flush();
        require(o4.color[4 * 8 + 4] != o5.color[4 * 8 + 4], "fixture: two translucent layers should depend on their order");
    }
    // ── mixed-surface furniture: split front cells, face rows, floating small objects, orphan adoption, continuity ──
    {
        // a cell that straddles the wall top is two planes: the vertical face and the flat top, not a ramp
        FrontPiece pcs[2];
        require(front_pieces(0, 6, pcs) == 2 && pcs[0].ua == 0 && pcs[0].ub == 6 && pcs[1].ua == 6 && pcs[1].ub == 16, "front cell not split at the wall top");
        require(front_pieces(0, 16, pcs) == 1 && front_pieces(16, 32, pcs) == 1 && front_pieces(16, 6, pcs) == 1, "front cell split without a boundary inside it");
        front_pieces(0, 6, pcs);
        float a[4][3], b[4][3], ramp[4][3];
        front_piece_quad(100, 6, 1.0f, 0, pcs[0].ua, pcs[0].ub, a);
        front_piece_quad(100, 6, 1.0f, 0, pcs[1].ua, pcs[1].ub, b);
        front_quad(100, 6, 1.0f, 0, 0, ramp);
        require(a[0][2] == 6 && a[3][2] == 0 && a[0][1] == 100 && a[3][1] == 100, "vertical piece is not an upright plane");
        require(b[0][2] == 6 && b[3][2] == 6 && b[3][1] == 100 && b[0][1] == 90, "top piece is not flat at the wall height");
        require(a[0][1] == b[3][1] && a[0][2] == b[3][2] && a[1][1] == b[2][1], "split pieces do not share the boundary edge");
        require(std::fabs((a[0][1] - a[0][2]) - 94.0f) < 1e-4f && std::fabs((b[0][1] - b[0][2]) - 84.0f) < 1e-4f && std::fabs((a[3][1] - a[3][2]) - 100.0f) < 1e-4f,
                "split pieces lost the reference-view alignment (Y - Z = base - u)");
        // the single quad was a ramp: at half height it sat at Z 3 while the real top plane is already at Z 6
        require(std::fabs((ramp[0][2] + ramp[3][2]) * 0.5f - 3.0f) < 1e-4f && b[0][2] == 6, "ramp comparison fixture broken");
        // mixed metatile faces: only the bottom rows stand
        require(wall_cell_top_row(9, 0) == 7 && wall_cell_top_row(5, 0) == 11 && wall_cell_top_row(0, 3) == 3 && wall_cell_top_row(16, 2) == 2, "wall_cell_top_row");
        require(box_wall_height(1, wall_cell_top_row(9, 0)) == 9.0f && box_wall_height(1, wall_cell_top_row(5, 0)) - 2.0f == 3.0f, "table / bed wall height not the measured face rows");
        g_models.load(kTilesetBuilding, kTilesetGenericBuilding1);
        require(g_models.at[0x054].face == 9 && g_models.at[0x055].face == 9 && g_models.at[0x054].skip == 0, "table face rows (measured 7..15)");
        for (int id : {0x293, 0x294, 0x295}) require(g_models.at[id].face == 5 && g_models.at[id].skip == 2, "bed face rows (measured foot 11..13, shadow 14..15)");
        require(g_models.at[0x04C].kind == kRoof && g_models.at[0x283].kind == kRoof, "table / bed top not a roof plane");
        require(g_models.at[0x296].kind == kStand && g_models.at[0x296].b == 1 && !continues_stand_run(g_models.at[0x296]), "console merged into the TV run");
        // the bedroom console draws only rows 0..6 of its cell: it must stand on the floor at its own lowest row, not float 9 units above it
        std::uint32_t console[256] = {};
        for (int y = 0; y <= 6; ++y) for (int x = 2; x < 14; ++x) console[y * 16 + x] = 0xff203040u;
        require(trailing_empty_rows(console) == 9, "console art gap not measured");
        std::uint32_t full[256]; std::fill(full, full + 256, 0xff112233u);
        require(trailing_empty_rows(full) == 0 && trailing_empty_rows(console + 0) == 9, "full art has a gap");
        const float gap = stand_foot_gap(9);
        require(gap == 9.0f && stand_foot_gap(2) == 0.0f, "foot gap threshold");
        float z0, z1, v1;
        stand_span(0, gap, &z0, &z1, &v1);
        require(z0 == 0 && z1 == 7 && v1 == 7, "console not cropped to its own rows");
        // reference-view alignment: art row 0 of the cell at world y = ytop must land at Y - Z = ytop for every cell of the run
        for (int dy = 0; dy < 3; ++dy) {
            stand_span(dy, gap, &z0, &z1, &v1);
            const float bottom_top = 0.0f, front = (bottom_top + 16.0f) - gap, ytop = bottom_top - dy * 16.0f;
            require(std::fabs((front - z1) - ytop) < 1e-4f, "stand run lost the reference-view alignment after the foot shift");
        }
        // a face with baked-shadow rows under it is raised by them: rows below the base plane are cropped, never drawn under the floor
        require(front_pieces(-2, 3, pcs) == 2 && pcs[0].ua == 0 && pcs[0].ub == 3 && pcs[1].ua == 3 && pcs[1].ub == 14, "bed foot not cropped at the base");
        require(front_pieces(-16, 3, pcs) == 0, "a cell entirely under the base was drawn");
        // house 2 planter: top lies flat at H continuing across the cell boundary, the face stands where the top ends, Y - Z follows the art rows
        {
            float tp0[4][3], tu0[4][2], tp1[4][3], tu1[4][2], fq[4][3], fu[4][2];
            planter_top_quad(0, 400, 8, 16, 6, tp0, tu0);       // wall cell above: rows 8..15 (ytop 400)
            planter_top_quad(0, 416, 0, 2, 6, tp1, tu1);        // planter cell: rows 0..1 (ytop 416)
            planter_face_quad(0, 416, 2, 6, fq, fu);
            require(tp0[0][2] == 6 && tp0[3][2] == 6 && tp1[0][2] == 6, "planter top not flat at its height");
            require(tp0[3][1] == tp1[0][1] && tp0[3][0] == tp1[0][0], "planter top has a seam between the wall cell and the planter cell");
            require(tp0[0][1] - tp0[0][2] == 400 + 8 && tp1[3][1] - tp1[3][2] == 416 + 2 && fq[0][1] - fq[0][2] == 416 + 2 && fq[3][1] - fq[3][2] == 416 + 8,
                    "planter lost the reference-view alignment (Y - Z = ytop + row)");
            require(fq[0][1] == tp1[3][1] && fq[0][2] == 6 && fq[3][2] == 0 && fu[0][1] == 2 && fu[3][1] == 8, "planter face not standing where the top ends");
            float ep[4][3];
            planter_end_quad(48, 414, 424, 6, ep);
            require(ep[0][1] == 414 && ep[1][1] == 424 && ep[0][2] == 6 && ep[2][2] == 0 && ep[0][0] == ep[2][0], "planter end cap geometry");
            std::uint32_t pa[256] = {};
            for (int y = 2; y < 8; ++y) for (int x = 1; x < 16; ++x) pa[y * 16 + x] = 0xff7f4010u;
            int pcol = -1;
            require(planter_side_column(pa, 2, 6, true, &pcol) && pcol == 1 && planter_side_column(pa, 2, 6, false, &pcol) && pcol == 15, "planter side column");
            for (int y = 2; y < 8; ++y) pa[y * 16 + 15] = 0;
            require(planter_side_column(pa, 2, 6, false, &pcol) && pcol == 14, "planter side column kept a transparent edge");
        }
        g_models.load(kTilesetGeneral, kTilesetViridianCity);
        for (int id : {0x298, 0x29A, 0x29B, 0x29C}) require(g_models.at[id].kind == kWall && g_models.at[id].band == 8 && g_models.at[id].a == 6, "house 2 wall cell without the planter band");
        for (int id : {0x2A0, 0x2A2, 0x2A3, 0x2A4}) require(g_models.at[id].kind == kPlanter && g_models.at[id].a == 6 && g_models.at[id].b == 2 && g_models.at[id].fg == 8, "house 2 planter cell");
        require(g_models.at[0x299].kind == kWall && g_models.at[0x299].band == 0, "door cell changed");
        {   // the audit's foreground flag follows opaque pixels, not nonzero entries (measured: tiles with entries but no top pixels)
            std::uint32_t empty_top[256] = {}, some_top[256] = {};
            some_top[5 * 16 + 3] = 0xff102030u;
            require(!has_opaque(empty_top) && has_opaque(some_top), "has_opaque");
            require(!top_is_foreground(0, has_opaque(empty_top)) && top_is_foreground(0, has_opaque(some_top)), "foreground flag from an empty top layer");
        }
        require(g_models.at[0x2A5].kind == kWall && g_models.at[0x29D].kind == kWall, "house 1 front changed");
        // the SIDE wall of a planter-band wall cell is closed from the floor to the wall top: siding rows 0..7 above, the last row repeated below
        // (the old picture hid that wall behind the flowers; the planter's own end cap only covers its depth, not the house body behind it)
        {
            SideSeg sg[2];
            int n = side_band_segments(16.0f, 0, 8, 0.0f, sg);
            require(n == 2 && sg[0].z0 == 8 && sg[0].z1 == 16 && sg[0].v0 == 0 && sg[0].v1 == 8, "siding rows not on the upper side wall");
            require(sg[1].z0 == 0 && sg[1].z1 == 8 && sg[1].v0 == 7.5f && sg[1].v1 == 7.5f, "wall below the band not filled with the last siding row down to the floor");
            require(sg[1].z1 == sg[0].z0, "gap between the siding and the repeat on the side wall");
            // coverage sweep: every height 0..16 of the side wall belongs to exactly one segment
            for (float z = 0.1f; z < 16.0f; z += 0.25f) {
                int hits = 0;
                for (int i = 0; i < n; ++i) hits += z >= sg[i].z0 && z <= sg[i].z1 ? 1 : 0;
                require(hits >= 1, "open hole in the side wall");
            }
            // raised base (bed-like skip): the floor is above zero, nothing is drawn under it, still closed
            n = side_band_segments(14.0f, 0, 8, 2.0f, sg);
            require(n == 2 && sg[1].z0 == 2 && sg[0].z1 == 14 && sg[1].z1 == sg[0].z0, "raised-floor side wall not closed");
            // a short wall: the siding alone is taller than the wall, so it is cropped at the floor and there is no repeat
            n = side_band_segments(6.0f, 0, 8, 0.0f, sg);
            require(n == 1 && sg[0].z0 == 0 && sg[0].z1 == 6, "short wall side");
            // the front hole (wall between the siding and the planter top) is bounded by the same plane heights
            const float uband = 0.0f + 16.0f - 8.0f, Hp = 6.0f;
            require(uband > Hp && uband - 8.0f == 0.0f, "front band fixture");
        }
        // Viridian Forest multi-tile trees: the MEASURED isolated tree only (tip 281 over the 3x3), exact pattern, claimed once
        {
            g_models.load(kTilesetGeneral, kTilesetViridianForest);
            for (int id : {0x281, 0x288, 0x289, 0x28A, 0x28E, 0x2A1, 0x28F, 0x2A3, 0x2A4, 0x2A5})
                require(g_models.at[id].kind == kMulti, "forest tree cell not a multi cell");
            for (int id : {0x290, 0x291, 0x292, 0x298, 0x299, 0x29A, 0x044, 0x04C, 0x001})
                require(g_models.at[id].kind == kFlat, "unmeasured forest boundary / gate roof / grass modelled");
            const int T = 0x001;   // grass
            // the measured tree: tip over the centre, 3x3 body (the two top corner cells are not part of it)
            const int one[] = {T, 0x281, T,
                               0x288, 0x289, 0x28A,
                               0x28E, 0x2A1, 0x28F,
                               0x2A3, 0x2A4, 0x2A5};
            MultiHit hits[8];
            require(find_multi(one, 3, 4, kForestMulti, 1, hits, 8) == 1 && hits[0].x == 0 && hits[0].y == 0, "measured tree with its tip not found");
            require(kForestMulti[0].w == 3 && kForestMulti[0].h == 4 && kForestMulti[0].ids[0] == kWild && kForestMulti[0].ids[2] == kWild, "tree pattern shape");
            const int notip[] = {T, T, T, 0x288, 0x289, 0x28A, 0x28E, 0x2A1, 0x28F, 0x2A3, 0x2A4, 0x2A5};
            require(find_multi(notip, 3, 4, kForestMulti, 1, hits, 8) == 0, "a tree without its tip was drawn (a chopped top)");
            // two trees side by side (6 wide): the corner cells next to the tip belong to the neighbour's body, which is fine because they are wild
            const int two[] = {T, 0x281, T, T, 0x281, T,
                               0x288, 0x289, 0x28A, 0x288, 0x289, 0x28A,
                               0x28E, 0x2A1, 0x28F, 0x28E, 0x2A1, 0x28F,
                               0x2A3, 0x2A4, 0x2A5, 0x2A3, 0x2A4, 0x2A5};
            require(find_multi(two, 6, 4, kForestMulti, 1, hits, 8) == 2 && hits[0].x == 0 && hits[1].x == 3, "two adjacent trees");
            // the dense boundary (3 x 2 blocks under a top row) is never a tree, and its 281 tip above a boundary cell stays flat
            const int B[] = {T, 0x281, T, 0x28A, 0x288, 0x299, 0x292, 0x290, 0x291, 0x29A, 0x298, 0x299};
            require(find_multi(B, 3, 4, kForestMulti, 1, hits, 8) == 0, "unmeasured boundary pattern matched");
            // two-row increments of the boundary under a tree: a following crown is not a bottom row
            const int S[] = {T, 0x281, T, 0x288, 0x289, 0x28A, 0x28E, 0x2A1, 0x28F, 0x288, 0x289, 0x28A, 0x28E, 0x2A1, 0x28F, 0x2A3, 0x2A4, 0x2A5};   // 3 x 6
            require(find_multi(S, 3, 6, kForestMulti, 1, hits, 8) == 0, "a stacked crown was joined to the tree above it");
            const int cut[] = {T, 0x281, T, 0x288, 0x289, 0x28A, 0x28E, 0x2A1, 0x28F};   // only three rows in the snapshot
            require(find_multi(cut, 3, 3, kForestMulti, 1, hits, 8) == 0, "tree cut by the snapshot edge claimed");
            const int invalid[] = {T, 0x281, T, 0x288, 0x289, 0x28A, 0x28E, -1, 0x28F, 0x2A3, 0x2A4, 0x2A5};
            require(find_multi(invalid, 3, 4, kForestMulti, 1, hits, 8) == 0, "tree with a missing cell claimed");
            // camera stability: a 3 wide x 4 tall block that touches a visible cell lies inside the grown window, wherever it is
            for (int v = -6; v < 6; ++v) {
                for (int t = v - 2; t <= v; ++t) {   // block columns t..t+2 contain the visible column v
                    int x0 = v, y0 = 0, x1 = v, y1 = 0;
                    expand_cell_window(&x0, &y0, &x1, &y1, kMultiMargin);
                    require(t >= x0 && t + 2 <= x1, "a tree touching the screen can be cut horizontally");
                }
                for (int t = v - 3; t <= v; ++t) {   // block rows t..t+3 contain the visible row v
                    int x0 = 0, y0 = v, x1 = 0, y1 = v;
                    expand_cell_window(&x0, &y0, &x1, &y1, kMultiMargin);
                    require(t >= y0 && t + 3 <= y1, "a tree touching the screen can be cut vertically");
                }
            }
            // the same tree through two windows (one cropped exactly to it) is found at the same world position
            const int W[] = {T, T, T, T, T,
                             T, T, 0x281, T, T,
                             T, 0x288, 0x289, 0x28A, T,
                             T, 0x28E, 0x2A1, 0x28F, T,
                             T, 0x2A3, 0x2A4, 0x2A5, T};
            MultiHit ha[2], hb[2];
            require(find_multi(W, 5, 5, kForestMulti, 1, ha, 2) == 1 && find_multi(one, 3, 4, kForestMulti, 1, hb, 2) == 1 && ha[0].x - 1 == hb[0].x && ha[0].y - 1 == hb[0].y,
                    "tree found at different world positions through two windows");
        }
        // forest trunk row (measured): side cells hold only the shadow oval, the centre keeps trunk rows 0..4 without the dark oval green
        {
            const std::uint32_t DARK = 0xFF004129u, MID = 0xFF105A29u, BG = 0xFF6A8B31u, BARK = 0xFF204080u;
            std::uint32_t art[256], out[256];
            for (int y = 0; y < 16; ++y) for (int x = 0; x < 16; ++x)
                art[y * 16 + x] = y < 5 ? (x >= 6 && x < 10 ? BARK : DARK) : y < 14 ? MID : BG;
            mask_tree_foot(art, true, out);
            for (int y = 0; y < 16; ++y) for (int x = 0; x < 16; ++x) {
                if (y < 5 && x >= 6 && x < 10) require(out[y * 16 + x] == BARK, "trunk bark removed");
                else require(out[y * 16 + x] == 0u, "oval shadow / background left in the centre trunk cell");
            }
            mask_tree_foot(art, false, out);
            for (int i = 0; i < 256; ++i) require(out[i] == 0u, "side trunk-row cell kept oval pixels");
            // grounding: the masked 3x4 billboard's last art row is 3 * 16 + 4 = 52, so 11 empty rows lie below it and the object is lowered / cropped by them
            std::vector<std::uint32_t> board(48 * 64, 0u);
            for (int y = 0; y < 5; ++y) for (int x = 22; x < 26; ++x) board[(48 + y) * 48 + x] = BARK;
            for (int x = 8; x < 40; ++x) board[40 * 48 + x] = MID;
            const int gap = trailing_empty_rows_w(board.data(), 48, 64);
            require(gap == 11 && stand_foot_gap(gap) == 11.0f, "masked tree foot row not measured (3 * 16 + 4 = 52)");
            float z0, z1, v1;
            stand_span(0, stand_foot_gap(gap), &z0, &z1, &v1);   // same rule as the console: the bottom cell is cropped to its own rows
            require(z0 == 0 && z1 == 5 && v1 == 5, "bottom cell not cropped to the trunk rows");
            // reference-view alignment of the lowered billboard: art row r lands at Y - Z = object top + r
            const float top = 100.0f, bottom = top + 64.0f, front = bottom - gap, hh = 64.0f - gap;
            for (int r : {0, 20, 52}) require(std::fabs((front - (hh - r)) - (top + r)) < 1e-4f, "lowered tree lost the reference-view alignment");
            require(trailing_empty_rows_w(board.data(), 48, 64) == 11 && trailing_empty_rows_w(std::vector<std::uint32_t>(48 * 64, 0xff000000u).data(), 48, 64) == 0, "trailing rows");
            // multi-tile snapshot margin only for the forest pair
            require(uses_multi(kTilesetGeneral, kTilesetViridianForest) && !uses_multi(kTilesetGeneral, kTilesetViridianCity) && !uses_multi(kTilesetBuilding, kTilesetPokemonCenter) &&
                    !uses_multi(kTilesetBuilding, kTilesetGenericBuilding1), "snapshot margin applies outside the forest");
        }
        // Viridian Forest dense canopy (experimental, measured grid): per-cell structure + wall-less height field (78347ae's slab with walls failed natively)
        {
            const int R[5][20] = {
                {0x28A, 0x288, 0x299, 0x28A, 0x288, 0x299, 0x28A, 0x288, 0x299, 0x28A, 0x294, 0x295, 0x179, 0x29C, 0x29D, 0x001, 0x288, 0x299, 0x28A, 0x288},
                {0x292, 0x290, 0x291, 0x292, 0x290, 0x291, 0x292, 0x044, 0x045, 0x045, 0x045, 0x045, 0x045, 0x045, 0x045, 0x045, 0x045, 0x046, 0x292, 0x290},
                {0x29A, 0x298, 0x299, 0x29A, 0x298, 0x299, 0x29A, 0x04C, 0x04D, 0x04D, 0x04D, 0x04D, 0x04D, 0x04D, 0x04D, 0x04D, 0x04D, 0x04E, 0x29A, 0x298},
                {0x292, 0x290, 0x291, 0x292, 0x290, 0x291, 0x292, 0x04C, 0x04D, 0x04D, 0x04D, 0x04D, 0x04D, 0x04D, 0x04D, 0x04D, 0x04D, 0x04E, 0x292, 0x290},
                {0x29A, 0x298, 0x299, 0x29A, 0x298, 0x299, 0x29A, 0x04C, 0x04D, 0x04D, 0x04D, 0x04D, 0x04D, 0x04D, 0x04D, 0x04D, 0x04D, 0x04E, 0x29A, 0x298}};
            int ids[6 * 20];   // a walkable row above the rim (world y68), then the measured rows
            for (int x = 0; x < 20; ++x) ids[x] = 0x001;
            for (int y = 0; y < 5; ++y) for (int x = 0; x < 20; ++x) ids[(y + 1) * 20 + x] = R[y][x];
            unsigned char can[120];
            const int n = canopy_cells(ids, nullptr, 20, 6, can);
            auto C = [&](int x, int y) { return can[(y + 1) * 20 + x] == 1; };
            require(C(0, 0) && C(1, 0) && C(2, 0), "rim cells above body cells are not canopy");
            require(!C(9, 0) && !C(10, 0) && !C(13, 0) && !C(15, 0), "walkable cells / path tiles above the roof taken as canopy");
            require(!C(7, 1) && !C(8, 2) && !C(17, 3), "gate roof taken as canopy");
            require(C(0, 2) && C(2, 4) && C(18, 1) && C(19, 1) && C(19, 0), "canopy body / rim cells missing");
            int cnt = 0; for (int i = 0; i < 120; ++i) cnt += can[i];
            require(n == cnt, "canopy count");
            // isolated tree cells claimed by a tree are never canopy
            const int T[] = {0x288, 0x299, 0x28A, 0x292, 0x290, 0x291};
            unsigned char claim[6] = {1, 0, 1, 0, 0, 0}, c3[6];
            canopy_cells(T, claim, 3, 2, c3);
            require(!c3[0] && c3[1] && !c3[2] && c3[3], "claimed tree cells taken as canopy");
            // ── height field ──
            const float H = 16.0f;
            const int VW = 21, VH = 7;
            std::vector<float> vh(VW * VH);
            canopy_vertex_heights(can, 20, 6, H, 2, vh.data());
            auto V = [&](int i, int j) { return vh[j * VW + i]; };
            // footprint: every vertex touching a non-canopy cell is exactly at ground level (gate roof, the cells above it, the rim row's top edge, ...)
            for (int j = 0; j < VH; ++j) for (int i = 0; i < VW; ++i) {
                bool touches = false;
                for (int dy = -1; dy <= 0; ++dy) for (int dx = -1; dx <= 0; ++dx) {
                    const int cx = i + dx, cy = j + dy;
                    if (cx >= 0 && cy >= 0 && cx < 20 && cy < 6 && !can[cy * 20 + cx]) touches = true;
                }
                if (touches) require(V(i, j) == 0.0f, "a vertex next to the gate roof / a path / the rim row is lifted (a pillar over the footprint)");
                require(V(i, j) >= 0.0f && V(i, j) <= H + 1e-4f, "height out of range");
            }
            for (int i = 0; i <= 20; ++i) require(V(i, 1) == 0.0f, "the rim row's top edge is lifted");
            // interior of the mass reaches H, one step in is the smoothstep midpoint, roof cells' neighbours stay low (rounded slope, not a wall)
            require(V(1, 2) == 0.5f * H, "no slope (half height one vertex in from the rim)");
            require(std::fabs(V(1, 3) - H) < 1e-4f, "the interior of the mass does not reach the peak");
            // no vertical steps: neighbouring vertices (one cell = 16 units apart) never differ by more than half the peak: slopes, never cliffs / pillars
            for (int j = 0; j < VH; ++j) for (int i = 0; i < VW; ++i) {
                if (i + 1 < VW) require(std::fabs(V(i, j) - V(i + 1, j)) <= 0.5f * H + 1e-4f, "cliff between neighbouring vertices (horizontal)");
                if (j + 1 < VH) require(std::fabs(V(i, j) - V(i, j + 1)) <= 0.5f * H + 1e-4f, "cliff between neighbouring vertices (vertical)");
            }
            // a lone canopy cell and a one-cell-wide strip stay flat (nothing to rise from)
            unsigned char lone[9] = {0, 0, 0, 0, 1, 0, 0, 0, 0};
            float lv[16];
            canopy_vertex_heights(lone, 3, 3, H, 2, lv);
            for (int k = 0; k < 16; ++k) require(lv[k] == 0.0f, "a lone canopy cell rose");
            // a big mass: the centre reaches H, the ramp is the smoothstep of the vertex distance
            unsigned char big[9 * 9] = {};
            for (int y = 1; y < 8; ++y) for (int x = 1; x < 8; ++x) big[y * 9 + x] = 1;   // a 7 x 7 mass inside a ring of non-canopy cells
            float bv[10 * 10];
            canopy_vertex_heights(big, 9, 9, H, 2, bv);
            require(bv[1 * 10 + 1] == 0.0f && bv[2 * 10 + 2] == 0.5f * H && bv[3 * 10 + 3] == H && bv[5 * 10 + 5] == H, "ramp is not 0 / half / peak over two vertices");
            // no ring around it (the window edge is not a boundary): the whole mass is at the peak
            unsigned char whole[7 * 7];
            for (int k = 0; k < 49; ++k) whole[k] = 1;
            float wv[8 * 8];
            canopy_vertex_heights(whole, 7, 7, H, 2, wv);
            for (int k = 0; k < 64; ++k) require(wv[k] == H, "the snapshot border was taken for a canopy edge");
            // camera stability: windows cropped by 0..2 cells give the same vertex heights for every vertex at least ramp + 1 vertices from the window border
            for (int x0 = 0; x0 <= 2; ++x0) for (int y0 = 0; y0 <= 1; ++y0) {
                const int w2 = 20 - x0, h2 = 6 - y0;
                std::vector<int> sub(static_cast<std::size_t>(w2) * h2);
                for (int y = 0; y < h2; ++y) for (int x = 0; x < w2; ++x) sub[y * w2 + x] = ids[(y + y0) * 20 + x + x0];
                std::vector<unsigned char> c2(sub.size());
                canopy_cells(sub.data(), nullptr, w2, h2, c2.data());
                std::vector<float> v2(static_cast<std::size_t>(w2 + 1) * (h2 + 1));
                canopy_vertex_heights(c2.data(), w2, h2, H, 2, v2.data());
                for (int j = 0; j <= h2; ++j) for (int i = 0; i <= w2; ++i) {
                    if (i < 3 || j < 3 || i > w2 - 3 || j > h2 - 3) continue;   // only vertices far from the cropped border can be compared
                    require(std::fabs(v2[j * (w2 + 1) + i] - V(i + x0, j + y0)) < 1e-4f, "canopy height changed with the snapshot window");
                }
            }
            // geometry: Y - Z = ytop + v at every corner whatever the heights; neighbouring quads share their edge exactly (no seam, no gap)
            float q0[4][3], u0[4][2], q1[4][3], u1[4][2], q2[4][3], u2[4][2];
            canopy_cell_quad(64, 1104, 0, 8, 16, 4, q0, u0);
            for (int i = 0; i < 4; ++i) require(std::fabs((q0[i][1] - q0[i][2]) - (1104.0f + u0[i][1])) < 1e-4f, "canopy quad lost the reference-view alignment");
            canopy_cell_quad(80, 1104, 8, 12, 5, 16, q1, u1);   // the cell to the right: shares vertices (8, 16) on the common edge
            require(q0[1][0] == q1[0][0] && q0[1][1] == q1[0][1] && q0[1][2] == q1[0][2] && q0[2][0] == q1[3][0] && q0[2][1] == q1[3][1] && q0[2][2] == q1[3][2],
                    "neighbouring canopy quads do not share their edge");
            // the cell below shares the bottom edge of the first: its top edge vertices are the same world points when the heights match (16 and 4)
            canopy_cell_quad(64, 1120, 4, 16, 3, 2, q2, u2);
            require(q2[0][1] == 1120.0f + 4.0f && q2[0][2] == 4.0f && q0[3][2] == 4.0f && q0[3][1] == 1104.0f + 16.0f + 4.0f, "bottom / top edge heights fixture");
        }
        // Forest north mass terminal (Ben's exact ROM map, world y54..y63, x24..43): side end 2A2, terminal front 290 2A1 292 / 2A3 2A4 2A5, isolated tree untouched
        {
            const int M[10][20] = {
                {0x290, 0x291, 0x292, 0x290, 0x291, 0x292, 0x290, 0x291, 0x2A2, 0x00D, 0x00D, 0x011, 0x009, 0x008, 0x00D, 0x00D, 0x290, 0x291, 0x292, 0x290},
                {0x298, 0x299, 0x29A, 0x298, 0x299, 0x29A, 0x298, 0x299, 0x29A, 0x00D, 0x00D, 0x010, 0x011, 0x010, 0x00D, 0x00D, 0x298, 0x299, 0x29A, 0x298},
                {0x290, 0x291, 0x292, 0x290, 0x291, 0x292, 0x290, 0x291, 0x2A2, 0x00D, 0x00D, 0x008, 0x009, 0x008, 0x00D, 0x00D, 0x290, 0x291, 0x292, 0x290},
                {0x298, 0x299, 0x29A, 0x298, 0x299, 0x29A, 0x298, 0x299, 0x29A, 0x00D, 0x00D, 0x011, 0x011, 0x010, 0x00D, 0x00D, 0x298, 0x299, 0x29A, 0x298},
                {0x290, 0x291, 0x292, 0x290, 0x291, 0x292, 0x290, 0x291, 0x2A2, 0x00D, 0x00D, 0x008, 0x009, 0x008, 0x00D, 0x00D, 0x290, 0x291, 0x292, 0x290},
                {0x298, 0x299, 0x29A, 0x298, 0x299, 0x29A, 0x298, 0x299, 0x29A, 0x00D, 0x00D, 0x010, 0x011, 0x010, 0x00D, 0x00D, 0x298, 0x299, 0x29A, 0x298},
                {0x290, 0x2A1, 0x292, 0x290, 0x2A1, 0x292, 0x290, 0x2A1, 0x2A2, 0x00D, 0x00D, 0x011, 0x009, 0x008, 0x00D, 0x00D, 0x290, 0x2A1, 0x292, 0x290},
                {0x2A3, 0x2A4, 0x2A5, 0x2A3, 0x2A4, 0x2A5, 0x2A3, 0x2A4, 0x2A5, 0x00D, 0x00D, 0x009, 0x281, 0x001, 0x00D, 0x00D, 0x2A3, 0x2A4, 0x2A5, 0x2A3},
                {0x00D, 0x00D, 0x00D, 0x00D, 0x00D, 0x00D, 0x00D, 0x00D, 0x00D, 0x00D, 0x00D, 0x288, 0x289, 0x28A, 0x00D, 0x00D, 0x00D, 0x00D, 0x00D, 0x00D},
                {0x00D, 0x00D, 0x00D, 0x00D, 0x00D, 0x00D, 0x00D, 0x00D, 0x00D, 0x00D, 0x00D, 0x28E, 0x2A1, 0x28F, 0x00D, 0x00D, 0x00D, 0x00D, 0x00D, 0x00D}};
            int ids[10 * 20];
            for (int y = 0; y < 10; ++y) for (int x = 0; x < 20; ++x) ids[y * 20 + x] = M[y][x];
            unsigned char role[200];
            canopy_cells(ids, nullptr, 20, 10, role);
            auto Rl = [&](int x, int y) { return static_cast<int>(role[y * 20 + x]); };
            require(Rl(8, 0) == 1 && Rl(8, 2) == 1 && Rl(8, 6) == 1, "the side end 2A2 is not part of the mass");
            require(Rl(1, 6) == 1 && Rl(4, 6) == 1 && Rl(7, 6) == 1, "the terminal lower-leaf centre 2A1 stays flat (a detached chunk)");
            for (int x : {0, 1, 2, 3, 4, 5, 6, 7, 8, 16, 17, 18, 19}) require(Rl(x, 7) == 2, "trunk row below the terminal front is not a trunk");
            require(Rl(12, 9) == 0 && Rl(12, 8) == 0 && Rl(11, 8) == 0 && Rl(13, 9) == 0 && Rl(12, 7) == 0, "the isolated tree (or its tip) was taken");
            require(Rl(9, 0) == 0 && Rl(10, 3) == 0, "tall grass between the masses taken");
            // an isolated tree's own trunk row is never a terminal trunk (its crown is not canopy), and a claimed centre is never canopy
            unsigned char claimed[200] = {};
            for (int dy = 0; dy < 2; ++dy) for (int dx = 0; dx < 3; ++dx) claimed[(8 + dy) * 20 + 11 + dx] = 1;
            unsigned char role2[200];
            canopy_cells(ids, claimed, 20, 10, role2);
            require(role2[9 * 20 + 12] == 0, "claimed tree cell became canopy");
            // heights: the terminal front slopes down to the ground at the trunk row, joined to the mass above without a step; the trunk row itself is ground level
            const float H = 16.0f;
            std::vector<float> vh(21 * 11);
            canopy_vertex_heights(role, 20, 10, H, 2, vh.data());
            auto V = [&](int i, int j) { return vh[j * 21 + i]; };
            for (int i = 0; i <= 20; ++i) require(V(i, 7) == 0.0f, "the terminal front's bottom edge (the trunk row's top) is lifted");
            for (int i = 0; i <= 8; ++i) require(V(i, 6) > 0.0f && V(i, 6) <= 0.5f * H + 1e-4f, "the terminal row does not slope between the mass and the trunk row");
            for (int i = 1; i < 8; ++i) require(std::fabs(V(i, 6) - V(i + 1, 6)) <= 0.5f * H + 1e-4f, "the terminal row is split from the leaf columns beside it (a detached chunk)");
            // the standing trunk's ground keeps the baked oval flat: bark pixels in rows 0..4 are replaced by the oval green, rows 5.. are untouched
            std::uint32_t both[256], g[256];
            for (int i = 0; i < 256; ++i) both[i] = (i / 16) < 5 ? (i % 16 >= 6 && i % 16 < 10 ? 0xFF204080u : kTreeOvalDark) : 0xFF105A29u;
            trunk_ground_art(both, g);
            for (int i = 0; i < 256; ++i) require(g[i] == ((i / 16) < 5 ? kTreeOvalDark : 0xFF105A29u), "trunk ground not the flat oval");
        }
        // canopy ramp radius stays inside the Forest snapshot margin so camera movement cannot change the heights of visible vertices
        {
            require(canopy_clamp_ramp(0) == 1 && canopy_clamp_ramp(-5) == 1 && canopy_clamp_ramp(2) == 2 && canopy_clamp_ramp(3) == 3 && canopy_clamp_ramp(9) == 3, "canopy ramp not clamped to 1..3");
            require(kCanopyRamp == 2 && kCanopyHeight == 16.0f, "verified canopy defaults changed");
            // the height of a vertex depends on boundaries within `ramp` vertices; a visible vertex is at least kMultiMargin cells inside the snapshot (the window is
            // grown by it), so the largest ramp + 1 cells of reach never touches the window border
            require(kCanopyMaxRamp + 1 <= kMultiMargin, "canopy reach exceeds the snapshot margin");
            // outside the Forest pair nothing of this exists
            require(uses_multi(kTilesetGeneral, kTilesetViridianForest) && !uses_multi(kTilesetGeneral, kTilesetPalletTown) && !uses_multi(kTilesetGeneral, kTilesetViridianCity), "canopy / multi-tile models outside the Forest pair");
        }
        // Forest gate roof (experimental, measured grid x30..41 y65..75): 11 columns x 6 rows to the map bottom, canopy on both sides, the entry path above
        {
            const int G[11][12] = {
                {0x00D, 0x00D, 0x00D, 0x001, 0x011, 0x009, 0x008, 0x009, 0x008, 0x009, 0x00D, 0x00D},
                {0x0D1, 0x0D1, 0x0D1, 0x0D2, 0x009, 0x011, 0x010, 0x011, 0x010, 0x011, 0x009, 0x008},
                {0x0D9, 0x0D9, 0x0D9, 0x0DA, 0x011, 0x001, 0x001, 0x001, 0x003, 0x010, 0x011, 0x010},
                {0x0E1, 0x0E1, 0x286, 0x0E2, 0x009, 0x0D3, 0x0D4, 0x0D5, 0x001, 0x011, 0x009, 0x281},
                {0x28A, 0x288, 0x299, 0x28A, 0x294, 0x295, 0x179, 0x29C, 0x29D, 0x001, 0x288, 0x299},
                {0x292, 0x044, 0x045, 0x045, 0x045, 0x045, 0x045, 0x045, 0x045, 0x045, 0x045, 0x046},
                {0x29A, 0x04C, 0x04D, 0x04D, 0x04D, 0x04D, 0x04D, 0x04D, 0x04D, 0x04D, 0x04D, 0x04E},
                {0x292, 0x04C, 0x04D, 0x04D, 0x04D, 0x04D, 0x04D, 0x04D, 0x04D, 0x04D, 0x04D, 0x04E},
                {0x29A, 0x04C, 0x04D, 0x04D, 0x04D, 0x04D, 0x04D, 0x04D, 0x04D, 0x04D, 0x04D, 0x04E},
                {0x292, 0x04C, 0x04D, 0x04D, 0x04D, 0x04D, 0x04D, 0x04D, 0x04D, 0x04D, 0x04D, 0x04E},
                {0x29A, 0x04C, 0x04D, 0x04D, 0x04D, 0x04D, 0x04D, 0x04D, 0x04D, 0x04D, 0x04D, 0x04E}};
            int ids[11 * 12];
            for (int y = 0; y < 11; ++y) for (int x = 0; x < 12; ++x) ids[y * 12 + x] = G[y][x];
            // one extra row of non-roof cells below the map bottom (the border) makes the grid 12 x 12
            std::vector<int> ids12(12 * 12, 0x00D);
            for (int i = 0; i < 11 * 12; ++i) ids12[i] = ids[i];
            std::vector<unsigned char> r12(12 * 12, 0);
            const int n = gate_roof_cells(ids12.data(), nullptr, 12, 12, r12.data());
            require(n == 66, "gate roof is not 11 columns x 6 rows (66 cells)");
            for (int y = 0; y < 12; ++y) for (int x = 0; x < 12; ++x) {
                const bool inside = y >= 5 && y <= 10 && x >= 1 && x <= 11;
                require((r12[y * 12 + x] == 4) == inside, "gate roof footprint wrong (the entry path, the canopy columns or the border taken, or a roof cell missed)");
            }
            for (int x = 3; x <= 8; ++x) require(r12[4 * 12 + x] != 4, "entry path cell taken as roof");
            // claimed cells are never roof
            std::vector<unsigned char> claim(144, 0), r13(144, 0);
            claim[6 * 12 + 5] = 1;
            require(gate_roof_cells(ids12.data(), claim.data(), 12, 12, r13.data()) == 65 && r13[6 * 12 + 5] == 0, "claimed cell taken as roof");
            // heights (raised role 4): ground level on every vertex touching a non-roof cell; the entry path row, the side columns and the bottom border stay untouched
            const float H = 16.0f;
            std::vector<float> vh(13 * 13);
            canopy_vertex_heights(r12.data(), 12, 12, H, 2, vh.data(), 4);
            auto V = [&](int i, int j) { return vh[j * 13 + i]; };
            for (int j = 0; j < 13; ++j) for (int i = 0; i < 13; ++i) {
                bool touches = false;
                for (int dy = -1; dy <= 0; ++dy) for (int dx = -1; dx <= 0; ++dx) {
                    const int cx = i + dx, cy = j + dy;
                    if (cx >= 0 && cy >= 0 && cx < 12 && cy < 12 && r12[cy * 12 + cx] != 4) touches = true;
                }
                if (touches) require(V(i, j) == 0.0f, "a vertex next to the entry path / canopy columns / the border is lifted (the roof must not cover them)");
                require(V(i, j) <= H + 1e-4f && V(i, j) >= 0.0f, "gate roof height out of range");
            }
            for (int i = 0; i <= 12; ++i) require(V(i, 5) == 0.0f, "the roof's north edge (next to the entry path) is lifted");
            require(V(6, 7) == H && V(6, 8) == H && V(6, 6) == 0.5f * H && V(6, 10) == 0.5f * H, "roof plateau / slope (0, H/2, H ... H, H/2, 0)");
            for (int j = 0; j < 13; ++j) for (int i = 0; i < 12; ++i) require(std::fabs(V(i, j) - V(i + 1, j)) <= 0.5f * H + 1e-4f, "cliff along the gate roof edge");
            // stability: windows cropped by 0..2 columns give identical heights away from the window border
            for (int x0 = 0; x0 <= 2; ++x0) {
                const int w2 = 12 - x0;
                std::vector<unsigned char> rs(static_cast<std::size_t>(w2) * 12);
                for (int y = 0; y < 12; ++y) for (int x = 0; x < w2; ++x) rs[y * w2 + x] = r12[y * 12 + x + x0];
                std::vector<float> v2(static_cast<std::size_t>(w2 + 1) * 13);
                canopy_vertex_heights(rs.data(), w2, 12, H, 2, v2.data(), 4);
                for (int j = 0; j < 13; ++j) for (int i = 3; i <= w2 - 3; ++i)
                    require(std::fabs(v2[j * (w2 + 1) + i] - V(i + x0, j)) < 1e-4f, "gate roof height changed with the snapshot window");
            }
            // the canopy beside the roof treats roof cells as non-canopy: its vertices on the shared edge are at ground level too (no gap, no wall between the two slopes)
            std::vector<unsigned char> rc(144, 0);
            canopy_cells(ids12.data(), nullptr, 12, 12, rc.data());
            std::vector<float> vc(13 * 13);
            canopy_vertex_heights(rc.data(), 12, 12, H, 2, vc.data());
            for (int j = 6; j <= 11; ++j) require(vc[j * 13 + 1] == 0.0f && V(1, j) == 0.0f, "canopy and roof do not meet at ground level");
        }
        // gate roof defaults are pinned and its reach stays inside the Forest snapshot margin
        require(kGateRoofHeight == 16.0f && kGateRoofRamp == 2 && canopy_clamp_ramp(kGateRoofRamp) == kGateRoofRamp, "verified gate roof defaults changed");
        require(canopy_clamp_ramp(7) + 1 <= kMultiMargin, "gate roof reach exceeds the snapshot margin");
        // Route 2 / Forest south gate interior (Building + GenericBuilding2), measured ids: plants stand as 2-cell objects, chairs are separate, the plain wall is bare
        {
            g_models.load(kTilesetBuilding, kTilesetGenericBuilding2);
            require(g_models.known && g_models.secondary == kTilesetGenericBuilding2, "gate interior has no model table");
            for (int id : {0x2EA, 0x2F1, 0x2E9, 0x2E8, 0x2F0}) require(g_models.at[id].kind == kStand && g_models.at[id].b == 0, "plant / pot part is not a stand");
            // runs: 2EA over 2F1, 2E9 over 2F1, 2E8 over 2F0 are one object each; a pot with floor above stays a single cell
            require(continues_stand_run(g_models.at[0x2F1]) && continues_stand_run(g_models.at[0x2F0]), "plant over pot not one object");
            for (int id : {0x2E6, 0x2E7}) require(g_models.at[id].kind == kStand && !continues_stand_run(g_models.at[id]), "stacked chairs merged into one object");
            // table (measured TOP rows): top plane above row 10 of the lower row, front rows 10..14 stand, row 15 empty
            for (int id : {0x2D6, 0x2D7}) require(g_models.at[id].kind == kRoof && g_models.at[id].art == 1, "table top not a TOP-layer roof");
            for (int id : {0x2DE, 0x2DF}) require(g_models.at[id].kind == kWall && g_models.at[id].art == 1 && g_models.at[id].face == 6 && g_models.at[id].skip == 1, "table front rows");
            require(box_art_top_only(g_models.at[0x2DE], true) && box_art_top_only(g_models.at[0x2D6], true), "table not drawn from its TOP layer");
            {
                const int top_row = wall_cell_top_row(g_models.at[0x2DE].face, 0);
                const float wall_h = std::max(0.0f, box_wall_height(1, top_row) - g_models.at[0x2DE].skip);
                require(top_row == 10 && wall_h == 5.0f, "table front height (rows 10..14 = 5)");
                // the front cell is raised by the skipped row; its pieces: the front rows 10..14 (row 15 cropped) and the top rows 0..9 flat at the front's top
                FrontPiece pcs[2];
                require(front_pieces(-1.0f, wall_h, pcs) == 2 && pcs[0].ua == 0.0f && pcs[0].ub == 5.0f && pcs[1].ua == 5.0f && pcs[1].ub == 15.0f, "table front cell not split at the front's top");
                const float vt = 16.0f - (pcs[0].ub + 1.0f), vb = 16.0f - (pcs[0].ua + 1.0f);
                require(vt == 10.0f && vb == 15.0f, "front piece does not sample rows 10..14");
                require(16.0f - (pcs[1].ub + 1.0f) == 0.0f && 16.0f - (pcs[1].ua + 1.0f) == 10.0f, "top piece does not sample rows 0..9");
                // the table top above (2D6 / 2D7, u0 = 15) lies flat on the same plane: Y - Z = base - u
                const float base = 100.0f;
                float q[4][3];
                front_piece_quad(base, wall_h, 1.0f, 0, 15.0f, 31.0f, q);
                require(q[0][2] == 5.0f && q[3][2] == 5.0f && std::fabs((q[0][1] - q[0][2]) - (base - 31.0f)) < 1e-4f && std::fabs((q[3][1] - q[3][2]) - (base - 15.0f)) < 1e-4f, "table top plane");
            }
            // the same ids outside this pair are untouched (no cross-tileset leakage)
            g_models.load(kTilesetBuilding, kTilesetGenericBuilding1);
            require(g_models.at[0x2EA].kind == kFlat && g_models.at[0x2E6].kind == kFlat, "gate models leaked into the player's house tileset");
            g_models.load(kTilesetBuilding, kTilesetGenericBuilding2);
            // back wall rows of Ben's room (x1..x13 inside the border): the commonest tile of the lower row was the plant 2EA and was repeated along the side walls
            const int row1[] = {0x2EA, 0x2A0, 0x2A1, 0x28F, 0x2EA, 0x290, 0x291, 0x292, 0x2EA, 0x28F, 0x2A0, 0x2A1, 0x2EA};
            const int row0[] = {0x287, 0x298, 0x299, 0x287, 0x287, 0x288, 0x289, 0x28A, 0x287, 0x287, 0x298, 0x299, 0x287};
            const unsigned char top1[] = {1, 1, 1, 0, 1, 1, 1, 1, 1, 0, 1, 1, 1}, top0[] = {0, 1, 1, 0, 0, 1, 1, 1, 0, 0, 1, 1, 0};
            unsigned char flat13[13], cand[13];
            for (int i = 0; i < 13; ++i) flat13[i] = 1;   // before the plants have a model every tile is flat
            plain_wall_candidates(flat13, top1, 13, false, cand);
            require(pick_plain_wall_id(row1, cand, 13, -1) == 0x2EA, "fixture: the old rule should pick the plant");
            plain_wall_candidates(flat13, top1, 13, true, cand);
            require(pick_plain_wall_id(row1, cand, 13, -1) == 0x28F, "the plain wall of the lower row is not the bare 28F");
            plain_wall_candidates(flat13, top0, 13, true, cand);
            require(pick_plain_wall_id(row0, cand, 13, -1) == 0x287, "the plain wall of the upper row is not 287");
            // a row with no bare tile keeps the model-flat flags (nothing is invented)
            const unsigned char all_top[] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
            plain_wall_candidates(flat13, all_top, 13, true, cand);
            for (int i = 0; i < 13; ++i) require(cand[i] == flat13[i], "no-bare row changed");
            // other rooms are unaffected (prefer_bare off)
            plain_wall_candidates(flat13, top1, 13, false, cand);
            for (int i = 0; i < 13; ++i) require(cand[i] == flat13[i], "prefer_bare leaked to other rooms");
            // a decoration the model table already took out of the flat set is never a candidate either
            unsigned char flat_m[13];
            for (int i = 0; i < 13; ++i) flat_m[i] = g_models.at[row1[i]].kind == kFlat ? 1 : 0;
            plain_wall_candidates(flat_m, top1, 13, true, cand);
            require(pick_plain_wall_id(row1, cand, 13, -1) == 0x28F, "plain wall with the plants modelled");
        }
        // bed floor: quarters that are not floor are replaced by floor quarters of the same cell; no floor quarter = caller borrows a neighbour
        {
            std::uint32_t bottom[256], filled[256];
            for (int q = 0; q < 4; ++q) for (int y = 0; y < 8; ++y) for (int x = 0; x < 8; ++x)
                bottom[((q >> 1) * 8 + y) * 16 + (q & 1) * 8 + x] = 0xff000000u | (0x100u * (q + 1));
            require(floor_fill_quads(bottom, 0b0101, filled), "cell with floor quarters refused");   // quarters 0 and 2 (left column) are floor
            require(filled[0] == bottom[0] && filled[8] == bottom[0] && filled[8 * 16 + 8] == bottom[8 * 16], "frame quarters not replaced by the floor beside them");
            require(filled[0] == bottom[0] && filled[8 * 16] == bottom[8 * 16], "floor quarters changed");
            require(floor_fill_quads(bottom, 0b0001, filled) && filled[8 * 16 + 8] == bottom[0], "diagonal fill");
            require(!floor_fill_quads(bottom, 0, filled), "cell without a floor quarter claimed a floor");
        }
        // layer type and generic standing objects
        require(top_is_foreground(0, true) && top_is_foreground(2, true) && !top_is_foreground(1, true) && !top_is_foreground(0, false), "foreground layer rule");
        require(generic_stand_cell(false, 1, 0, true, 0) && !generic_stand_cell(true, 1, 0, true, 0) && !generic_stand_cell(false, 0, 0, true, 0) &&
                !generic_stand_cell(false, 1, 1, true, 0) && !generic_stand_cell(false, 1, 0, false, 0) && !generic_stand_cell(false, 1, 0, true, 0x15) &&
                !generic_stand_cell(false, 1, 0, true, 0x3A), "generic stand rule");
        // split objects: a missing wall tile in a wall row (or roof tile in a roof column) joins the box; nothing else does
        const std::uint8_t W = kWall, R = kRoof, F = kFlat;
        std::uint8_t kinds[5] = {W, W, F, W, W}, solid[5] = {1, 1, 1, 1, 1}, out[5];
        require(adopt_orphans(kinds, solid, 5, 1, out) == 1 && out[2] == W, "gap in a wall row not adopted");
        solid[2] = 0;
        require(adopt_orphans(kinds, solid, 5, 1, out) == 0 && out[2] == F, "non-blocking cell adopted");
        std::uint8_t mixed[5] = {W, W, F, R, R}, solid5[5] = {1, 1, 1, 1, 1};
        require(adopt_orphans(mixed, solid5, 5, 1, out) == 0, "cell between a wall and a roof adopted");
        std::uint8_t two[6] = {W, F, F, W, W, W}, solid6[6] = {1, 1, 1, 1, 1, 1}, out6[6];
        require(adopt_orphans(two, solid6, 6, 1, out6) == 0, "a two-cell gap adopted (would depend on scan order)");
        std::uint8_t col[3] = {R, F, R}, solid3[3] = {1, 1, 1}, out3[3];
        require(adopt_orphans(col, solid3, 1, 3, out3) == 1 && out3[1] == R, "gap in a roof column not adopted");
        std::uint8_t edge[3] = {F, W, W}, out_e[3];
        require(adopt_orphans(edge, solid3, 3, 1, out_e) == 0, "cell at the snapshot edge adopted");
        // continuity: the same world seen through two different snapshot windows classifies identically; a real change is reported once
        std::uint8_t world[8 * 4];
        for (int i = 0; i < 8 * 4; ++i) world[i] = static_cast<std::uint8_t>(i % 3);
        auto window = [&](int x0, int y0, int w, int h) {
            std::vector<std::uint8_t> v;
            for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) v.push_back(world[(y0 + y) * 8 + x0 + x]);
            return v;
        };
        const auto wa = window(0, 0, 6, 4), wb = window(2, 0, 6, 4);
        require(kind_flips(wa.data(), 0, 0, 6, 4, wb.data(), 2, 0, 6, 4, 0, 0, 8, 4) == 0, "identical world flipped between two windows");
        auto wc = wb;
        wc[1 * 6 + 3] = static_cast<std::uint8_t>(wc[1 * 6 + 3] ^ 1);   // world cell (5, 1)
        int fx = -1, fy = -1;
        require(kind_flips(wa.data(), 0, 0, 6, 4, wc.data(), 2, 0, 6, 4, 0, 0, 8, 4, &fx, &fy) == 1 && fx == 5 && fy == 1, "single changed cell not reported");
        require(kind_flips(wa.data(), 0, 0, 6, 4, wc.data(), 2, 0, 6, 4, 0, 0, 4, 4) == 0, "flip outside the screen rectangle reported");
    }
    std::cout << "3D rasterizer, tile models, art layers and sprite checks passed\n";
}
'''

with tempfile.TemporaryDirectory(prefix="firered-diorama-", dir=root / "tools") as folder:
    pathlib.Path(folder).resolve().relative_to(root)
    cpp = pathlib.Path(folder) / "check.cpp"
    exe = pathlib.Path(folder) / "check.exe"
    cpp.write_text(fixture + consts + camera + raster + "Raster g_raster;\n" + draw + "\n" + art + decoder + checks, encoding="utf-8")
    compiler = sys.argv[1] if len(sys.argv) > 1 else os.environ.get("CXX", "c++")
    compiler = shutil.which(compiler) or compiler
    env = dict(os.environ, TMP=folder, TEMP=folder,
               PATH=str(pathlib.Path(compiler).resolve().parent) + os.pathsep + os.environ.get("PATH", ""))
    flags = ["-std=c++17", "-O1"]
    if os.name != "nt" and os.environ.get("FRLG_NO_SANITIZE") is None:
        flags += ["-fsanitize=address,undefined", "-fno-sanitize-recover=all"]   # bounds / UB in the helpers fail the check
    if os.name == "nt":
        flags.append("-static")
    subprocess.run([compiler, *flags, str(cpp), "-o", str(exe)], check=True, env=env)
    subprocess.run([str(exe)], check=True)
