// fr_hd_draw.h — drawing kit shared by the HD UI renderers: per-frame
// context, theme, fonts, vector primitives, ROM sprite textures and pointer
// targets. All geometry is in *view units* (the game's logical pixels,
// 160 tall, widescreen width included); the kit scales to the drawable.

#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "fr_guest.h"
#include "fr_hd_ui.h"
#include "imgui.h"

namespace frlg::hd {

// ── theme ────────────────────────────────────────────────────────────────
namespace col {
constexpr ImU32 rgba(int r, int g, int b, int a = 255) {
    return IM_COL32(r, g, b, a);
}
constexpr ImU32 kPanel      = rgba(15, 19, 30, 236);
constexpr ImU32 kPanelHi    = rgba(28, 35, 52, 244);
constexpr ImU32 kPanelLo    = rgba(10, 13, 21, 246);
constexpr ImU32 kStroke     = rgba(255, 255, 255, 30);
constexpr ImU32 kStrokeHi   = rgba(255, 255, 255, 64);
constexpr ImU32 kText       = rgba(245, 247, 252);
constexpr ImU32 kTextDim    = rgba(170, 180, 200);
constexpr ImU32 kTextFaint  = rgba(112, 122, 144);
constexpr ImU32 kAccent     = rgba(255, 88, 54);
constexpr ImU32 kAccent2    = rgba(255, 146, 60);
constexpr ImU32 kAccentSoft = rgba(255, 88, 54, 64);
constexpr ImU32 kGold       = rgba(255, 201, 87);
constexpr ImU32 kGood       = rgba(74, 222, 128);
constexpr ImU32 kWarn       = rgba(250, 204, 21);
constexpr ImU32 kBad        = rgba(248, 96, 96);
constexpr ImU32 kBlue       = rgba(96, 165, 250);
constexpr ImU32 kPink       = rgba(244, 114, 182);
constexpr ImU32 kShadow     = rgba(0, 0, 0, 110);
constexpr ImU32 kWhite      = rgba(255, 255, 255);
constexpr ImU32 kBlack      = rgba(0, 0, 0);
ImU32 alpha(ImU32 c, float a);           // multiply alpha
ImU32 mix(ImU32 a, ImU32 b, float t);    // lerp a -> b
ImU32 type_color(int type);              // Pokémon type (TYPE_NORMAL ...)
ImU32 text_color(int gba_fg);            // TEXT_COLOR_* on a dark panel
}  // namespace col

// ── fonts ────────────────────────────────────────────────────────────────
enum class F { Medium, Bold };

// ── frame context ────────────────────────────────────────────────────────
struct Ctx {
    ImDrawList* dl = nullptr;
    float ox = 0, oy = 0;     // drawable position of view (0, 0)
    float s = 1;              // drawable px per view unit
    float vw = 240, vh = 160; // view size
    float margin = 0;         // native x 0 in view units
    double t = 0;             // seconds
    float dt = 0;
    Mem m;
    const Scene* sc = nullptr;

    ImVec2 P(float x, float y) const { return ImVec2(ox + x * s, oy + y * s); }
    float nx(int native_x) const { return margin + static_cast<float>(native_x); }
};

// Text (top-left anchored at the line box; size = font height in units).
enum Align { kLeft = 0, kCenter = 1, kRight = 2 };
void text(Ctx& c, F f, float size, float x, float y, ImU32 color, std::string_view s,
          int align = kLeft, float max_w = 0, bool shadow = true);
float text_w(F f, float size, std::string_view s);
// Draw text squeezed horizontally (smaller size) so it never exceeds max_w.
float fit_size(F f, float size, std::string_view s, float max_w, float min_scale = 0.72f);

// Shapes.
void rect(Ctx& c, float x, float y, float w, float h, ImU32 color, float r = 0,
          ImDrawFlags flags = 0);
void rect_line(Ctx& c, float x, float y, float w, float h, ImU32 color, float r = 0,
               float th = 0.5f);
void grad_h(Ctx& c, float x, float y, float w, float h, ImU32 l, ImU32 r_, float r = 0);
void grad_v(Ctx& c, float x, float y, float w, float h, ImU32 top, ImU32 bottom, float r = 0);
void shadow(Ctx& c, float x, float y, float w, float h, float r, float spread = 3.0f,
            float strength = 1.0f);
void panel(Ctx& c, float x, float y, float w, float h, float r = 5.0f,
           ImU32 fill = col::kPanel, bool drop = true);
void pill_select(Ctx& c, float x, float y, float w, float h, float r, float glow = 1.0f);
void circle(Ctx& c, float x, float y, float r, ImU32 color, int seg = 0);
void ring(Ctx& c, float x, float y, float r, ImU32 color, float th);
void line(Ctx& c, float x0, float y0, float x1, float y1, ImU32 color, float th = 0.6f);
void tri(Ctx& c, float x0, float y0, float x1, float y1, float x2, float y2, ImU32 color);
void chevron_down(Ctx& c, float x, float y, float size, ImU32 color);
void bar(Ctx& c, float x, float y, float w, float h, float frac, ImU32 fill, ImU32 track);
void keycap(Ctx& c, float x, float y, float h, std::string_view label, float* out_w = nullptr);
// Keycap for a GBA key (KEYINPUT bit) showing its bound PC key.
float keycap_for(Ctx& c, float x, float y, float h, int gba_bit, bool measure_only = false);
std::string key_label(int gba_bit);
void push_clip(Ctx& c, float x, float y, float w, float h);
void pop_clip(Ctx& c);

// Icons (vector), cx/cy centre, size = box edge.
enum class Icon {
    Pokedex, Pokeball, Bag, Card, Save, Gear, Exit, Flag, Link, Fight, Run, Party,
    Info, Back, Check,
};
void icon(Ctx& c, Icon i, float cx, float cy, float size, ImU32 color);

// ── ROM sprite textures (nearest-filtered pixel art) ────────────────────
struct Tex {
    void* id = nullptr;
    int w = 0, h = 0;
    explicit operator bool() const { return id != nullptr; }
};
Tex tex_mon_icon(const Mem& m, int species, int frame);
Tex tex_mon_front(const Mem& m, int species, bool shiny, std::uint32_t personality);
Tex tex_item_icon(const Mem& m, int item);
Tex tex_trainer_front(const Mem& m, int pic);
void image(Ctx& c, const Tex& t, float x, float y, float w, float h, ImU32 tint = col::kWhite);
void textures_reset();

// Smoothly animated list scroll (in rows): items glide instead of jumping.
// `key` identifies the list (a new key snaps).
struct ScrollAnim { float pos = -1; std::uint32_t key = 0; };
float scroll_anim(ScrollAnim& a, std::uint32_t key, int target, float dt);

// ── pointer targets for the next input frame ────────────────────────────
void target(float x, float y, float w, float h, HdHitKind kind, int index);

// ── shared game data helpers ────────────────────────────────────────────
std::string species_name(const Mem& m, int species);
std::string move_name(const Mem& m, int move);
std::string item_name(const Mem& m, int item);
std::string type_name(int type);
std::string rom_string(const Mem& m, std::uint32_t addr, std::size_t max = 400, bool one_line = false);
int move_type(const Mem& m, int move);
int move_power(const Mem& m, int move);
int move_accuracy(const Mem& m, int move);
int move_pp(const Mem& m, int move);
int species_type(const Mem& m, int species, int which);
int type_effectiveness(const Mem& m, int atk_type, int def1, int def2);  // x10: 0,5,10,20,40
bool modern_text();

// Party/box Pokémon decoded from guest memory.
struct MonView {
    bool valid = false, egg = false, bad_egg = false;
    int species = 0, item = 0, level = 0, hp = 0, max_hp = 0;
    int status = 0;
    int moves[4] = {0, 0, 0, 0};
    int pp[4] = {0, 0, 0, 0};
    int pp_bonuses = 0;
    int stats[5] = {0, 0, 0, 0, 0};   // atk def spe spa spd
    std::uint32_t personality = 0, ot_id = 0, exp = 0;
    int friendship = 0, ability_num = 0, met_level = 0, met_location = 0, ball = 0;
    int ivs[6] = {0, 0, 0, 0, 0, 0}, evs[6] = {0, 0, 0, 0, 0, 0};
    std::string nickname, ot_name;
    int gender = 0;   // 0 male, 1 female, 2 genderless
    bool shiny = false;
};
MonView read_mon(const Mem& m, std::uint32_t addr, bool party);
int exp_for_level(const Mem& m, int species, int level);
std::string status_label(int status);
ImU32 status_color(int status);

// Fade of the guest picture (palette fade / brightness): multiply = 0..1
// towards black, white = 0..1 towards white.
struct Fade { float black = 0, white = 0; };
Fade guest_fade(const Mem& m);

}  // namespace frlg::hd
