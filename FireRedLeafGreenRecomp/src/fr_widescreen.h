// fr_widescreen.h — FireRed overworld widescreen (16:9).
//
// A port of EmeraldRecomp's overworld widescreen experiment
// (src/emerald_extended_view.cpp, emerald_object_view.cpp,
// emerald_ui_view.cpp) to Pokémon FireRed (USA rev0). FireRed shares the
// Gen 3 field engine; the differences handled here are FireRed's addresses,
// 640 primary metatiles, 32-bit metatile attributes (layer type in bits
// 29-30), arbitrary-size map borders and its vertical 1x2 doors.
//
// It is presentation only: it reads guest RAM/VRAM/ROM and never writes
// guest state. Every frame the expanded tile mapping is checked against the
// tiles the game actually published; any mismatch, or any non-overworld
// scene (battles, menus, cutscenes), falls back to the normal centred view.

#pragma once

#include <cstdint>
#include <vector>

namespace gbarecomp { struct ExtendedViewFrameInfo; }

namespace frlg {

constexpr int kWidescreenWidth = 284;  // round(160 * 16 / 9)

void install_extended_view(std::uint32_t extra_left, std::uint32_t extra_right);

// ── read-only field snapshot (used by the 3D view) ──────────────────────
// World pixel of native screen (0, 0) in the padded map grid (VMap pixels),
// verified against the tiles the game published this frame.
bool field_camera(int* origin_x, int* origin_y);
struct FieldCell {
    bool valid = false, border = false;
    bool door = false, animated = false;
    std::uint16_t id = 0;          // metatile id
    std::uint8_t collision = 0, elevation = 0, layer = 0;
    std::uint16_t behavior = 0;
    std::uint16_t entries[8]{};    // bottom 4, top 4 tile entries
};
// Cells [mx0, mx0 + mw) x [my0, my0 + mh) of the grid, connected maps and
// border blocks included.
bool field_cells(int mx0, int my0, int mw, int mh, std::vector<FieldCell>* out);
// Overlay verified door frames from the native tile ring onto this snapshot.
void field_door_frames(int origin_x, int origin_y, int mx0, int my0, int mw, int mh,
                       std::vector<FieldCell>* cells);
void update_extended_view(const gbarecomp::ExtendedViewFrameInfo* frame);

}  // namespace frlg
