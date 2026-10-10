// fr_hd_draw.cpp — drawing kit for the HD UI. See fr_hd_draw.h.
#include "fr_hd_draw.h"

#include <SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <tuple>

#include "fr_hd_text.h"
#include "host_window.h"
#include "runtime.h"

namespace frlg::hd {

constexpr float kPi = 3.14159265358979f;

extern const std::uint8_t kPoppinsMedium[];
extern const std::size_t kPoppinsMediumSize;
extern const std::uint8_t kPoppinsBold[];
extern const std::size_t kPoppinsBoldSize;
extern const std::uint8_t kSymbols[];
extern const std::size_t kSymbolsSize;

// ── colors ───────────────────────────────────────────────────────────────
namespace col {
ImU32 alpha(ImU32 c, float a) {
    const int al = static_cast<int>(((c >> IM_COL32_A_SHIFT) & 0xFF) * std::clamp(a, 0.0f, 1.0f) + 0.5f);
    return (c & ~IM_COL32_A_MASK) | (static_cast<ImU32>(al) << IM_COL32_A_SHIFT);
}
ImU32 mix(ImU32 a, ImU32 b, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    auto ch = [&](int shift) {
        const float x = ((a >> shift) & 0xFF), y = ((b >> shift) & 0xFF);
        return static_cast<ImU32>(x + (y - x) * t + 0.5f) << shift;
    };
    return ch(IM_COL32_R_SHIFT) | ch(IM_COL32_G_SHIFT) | ch(IM_COL32_B_SHIFT) | ch(IM_COL32_A_SHIFT);
}
ImU32 type_color(int type) {
    static const ImU32 k[] = {
        rgba(168, 167, 122), rgba(194, 46, 40), rgba(169, 143, 243), rgba(163, 62, 161),
        rgba(226, 191, 101), rgba(182, 161, 54), rgba(166, 185, 26), rgba(115, 87, 151),
        rgba(183, 183, 206), rgba(104, 160, 144), rgba(238, 129, 48), rgba(99, 144, 240),
        rgba(122, 199, 76), rgba(247, 208, 44), rgba(249, 85, 135), rgba(150, 217, 214),
        rgba(111, 53, 252), rgba(112, 87, 70),
    };
    return (type >= 0 && type < 18) ? k[type] : rgba(104, 160, 144);
}
ImU32 text_color(int fg) {
    switch (fg) {
        case 3: return kTextDim;
        case 4: case 5: return rgba(255, 178, 176);
        case 6: case 7: return rgba(150, 232, 170);
        case 8: case 9: return rgba(166, 204, 255);
        default: return kText;
    }
}
}  // namespace col

// ── fonts ────────────────────────────────────────────────────────────────
namespace {
constexpr float kSizes[] = {7.0f, 9.0f, 11.0f, 14.0f, 20.0f};
constexpr int kNumSizes = 5;
ImFont* g_fonts[2][kNumSizes] = {};
float g_raster = 0;    // drawable px per view unit the atlas was built for
float g_view_w = 284;  // last view width seen by the overlay

const ImWchar kRanges[] = {0x0020, 0x007E, 0x00A0, 0x00FF, 0x0152, 0x0153, 0x2010, 0x2027,
                           0x20BD, 0x20BD, 0};
const ImWchar kRangesBig[] = {0x0020, 0x007E, 0x00E9, 0x00E9, 0x20BD, 0x20BD, 0};
const ImWchar kSymbolRanges[] = {0x00D7, 0x00D7, 0x2190, 0x2193, 0x2460, 0x2468, 0x25B2, 0x25C0,
                                 0x25CE, 0x25CE, 0x2605, 0x2605, 0x2640, 0x2642, 0x2715, 0x2715, 0};

ImFont* pick(F f, float size) {
    const int w = f == F::Bold ? 1 : 0;
    for (int i = 0; i < kNumSizes; ++i)
        if (g_fonts[w][i] && kSizes[i] >= size * 0.94f) return g_fonts[w][i];
    for (int i = kNumSizes - 1; i >= 0; --i) if (g_fonts[w][i]) return g_fonts[w][i];
    return ImGui::GetFont();
}
}  // namespace

void set_view_width_hint(float vw) { g_view_w = vw; }

bool fonts_prepare(int dw, int dh) {
    if (dw <= 0 || dh <= 0) return false;
    float s = std::min(dh / 160.0f, dw / std::max(240.0f, g_view_w));
    s = std::clamp(s, 2.0f, 8.0f);
    if (g_raster > 0 && std::fabs(s - g_raster) / g_raster < 0.06f) return false;
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();
    io.Fonts->Flags |= ImFontAtlasFlags_NoPowerOfTwoHeight;
    io.Fonts->TexDesiredWidth = 4096;
    io.Fonts->AddFontDefault();  // index 0 stays the runtime menu's font
    for (int w = 0; w < 2; ++w) {
        const std::uint8_t* data = w ? kPoppinsBold : kPoppinsMedium;
        const std::size_t size = w ? kPoppinsBoldSize : kPoppinsMediumSize;
        for (int i = 0; i < kNumSizes; ++i) {
            if (i == kNumSizes - 1 && !w) { g_fonts[w][i] = nullptr; continue; }
            ImFontConfig cfg;
            cfg.FontDataOwnedByAtlas = false;
            cfg.OversampleH = 1;
            cfg.OversampleV = 1;
            cfg.PixelSnapH = false;
            const float px = std::round(kSizes[i] * s);
            g_fonts[w][i] = io.Fonts->AddFontFromMemoryTTF(const_cast<std::uint8_t*>(data),
                                                           static_cast<int>(size), px, &cfg,
                                                           i == kNumSizes - 1 ? kRangesBig : kRanges);
            ImFontConfig sym;
            sym.FontDataOwnedByAtlas = false;
            sym.MergeMode = true;
            sym.OversampleH = 1;
            sym.OversampleV = 1;
            sym.GlyphOffset = ImVec2(0, px * 0.04f);
            io.Fonts->AddFontFromMemoryTTF(const_cast<std::uint8_t*>(kSymbols),
                                           static_cast<int>(kSymbolsSize), px * 0.9f, &sym,
                                           kSymbolRanges);
        }
    }
    io.Fonts->Build();
    g_raster = s;
    std::fprintf(stderr, "[frlg-hd] fonts built for %.2fx (%dx%d atlas)\n", s,
                 io.Fonts->TexWidth, io.Fonts->TexHeight);
    return true;
}

bool fonts_ready() { return g_raster > 0; }

float text_w(F f, float size, std::string_view s) {
    if (s.empty()) return 0;
    ImFont* font = pick(f, size);
    const float px = size;  // measured in view units: scale font size 1:1
    return font->CalcTextSizeA(px, FLT_MAX, 0.0f, s.data(), s.data() + s.size()).x;
}

float fit_size(F f, float size, std::string_view s, float max_w, float min_scale) {
    if (max_w <= 0) return size;
    const float w = text_w(f, size, s);
    if (w <= max_w) return size;
    return std::max(size * min_scale, size * max_w / w);
}

void text(Ctx& c, F f, float size, float x, float y, ImU32 color, std::string_view s, int align,
          float max_w, bool drop) {
    if (s.empty() || ((color >> IM_COL32_A_SHIFT) & 0xFF) == 0) return;
    if (max_w > 0) size = fit_size(f, size, s, max_w);
    ImFont* font = pick(f, size);
    const float w = text_w(f, size, s);
    if (align == kCenter) x -= w * 0.5f;
    else if (align == kRight) x -= w;
    const float px = size * c.s;
    const ImVec2 p = c.P(x, y);
    if (drop) {
        const float a = ((color >> IM_COL32_A_SHIFT) & 0xFF) / 255.0f;
        c.dl->AddText(font, px, ImVec2(p.x, p.y + std::max(1.0f, size * 0.07f * c.s)),
                      col::alpha(col::kBlack, 0.42f * a), s.data(), s.data() + s.size());
    }
    c.dl->AddText(font, px, p, color, s.data(), s.data() + s.size());
}

// ── shapes ───────────────────────────────────────────────────────────────
void rect(Ctx& c, float x, float y, float w, float h, ImU32 color, float r, ImDrawFlags flags) {
    c.dl->AddRectFilled(c.P(x, y), c.P(x + w, y + h), color, r * c.s, flags);
}
void rect_line(Ctx& c, float x, float y, float w, float h, ImU32 color, float r, float th) {
    c.dl->AddRect(c.P(x, y), c.P(x + w, y + h), color, r * c.s, 0, std::max(1.0f, th * c.s));
}
void grad_h(Ctx& c, float x, float y, float w, float h, ImU32 l, ImU32 rc, float r) {
    if (r <= 0) {
        c.dl->AddRectFilledMultiColor(c.P(x, y), c.P(x + w, y + h), l, rc, rc, l);
        return;
    }
    // Rounded: gradient body clipped by drawing vertex colours over a path.
    const int start = c.dl->VtxBuffer.Size;
    c.dl->AddRectFilled(c.P(x, y), c.P(x + w, y + h), col::kWhite, r * c.s);
    const float x0 = c.P(x, 0).x, x1 = c.P(x + w, 0).x;
    for (int i = start; i < c.dl->VtxBuffer.Size; ++i) {
        ImDrawVert& v = c.dl->VtxBuffer[i];
        v.col = col::mix(l, rc, (v.pos.x - x0) / std::max(1.0f, x1 - x0));
    }
}
void grad_v(Ctx& c, float x, float y, float w, float h, ImU32 top, ImU32 bottom, float r) {
    if (r <= 0) {
        c.dl->AddRectFilledMultiColor(c.P(x, y), c.P(x + w, y + h), top, top, bottom, bottom);
        return;
    }
    const int start = c.dl->VtxBuffer.Size;
    c.dl->AddRectFilled(c.P(x, y), c.P(x + w, y + h), col::kWhite, r * c.s);
    const float y0 = c.P(0, y).y, y1 = c.P(0, y + h).y;
    for (int i = start; i < c.dl->VtxBuffer.Size; ++i) {
        ImDrawVert& v = c.dl->VtxBuffer[i];
        v.col = col::mix(top, bottom, (v.pos.y - y0) / std::max(1.0f, y1 - y0));
    }
}
void shadow(Ctx& c, float x, float y, float w, float h, float r, float spread, float strength) {
    const int steps = 3;
    for (int i = steps; i >= 1; --i) {
        const float k = static_cast<float>(i) / steps;
        const float e = spread * k;
        const float a = 0.09f * strength * (1.0f - k * 0.6f);
        c.dl->AddRectFilled(c.P(x - e, y - e * 0.6f + spread * 0.35f),
                            c.P(x + w + e, y + h + e + spread * 0.35f),
                            col::alpha(col::kBlack, a * 2.2f), (r + e) * c.s);
    }
}
void panel(Ctx& c, float x, float y, float w, float h, float r, ImU32 fill, bool drop) {
    if (drop) shadow(c, x, y, w, h, r, 3.5f, 1.0f);
    grad_v(c, x, y, w, h, col::mix(fill, col::kPanelHi, 0.35f), fill, r);
    // inner top highlight + hairline border
    c.dl->AddRect(c.P(x, y), c.P(x + w, y + h), col::kStroke, r * c.s, 0, std::max(1.0f, 0.45f * c.s));
    c.dl->AddLine(c.P(x + r, y + 0.45f), c.P(x + w - r, y + 0.45f), col::alpha(col::kWhite, 0.10f),
                  std::max(1.0f, 0.4f * c.s));
}
void pill_select(Ctx& c, float x, float y, float w, float h, float r, float glow) {
    if (glow > 0) {
        for (int i = 3; i >= 1; --i) {
            const float e = i * 0.9f;
            c.dl->AddRectFilled(c.P(x - e, y - e), c.P(x + w + e, y + h + e),
                                col::alpha(col::kAccent, 0.07f * glow), (r + e) * c.s);
        }
    }
    grad_h(c, x, y, w, h, col::kAccent, col::kAccent2, r);
    c.dl->AddLine(c.P(x + r * 0.8f, y + 0.5f), c.P(x + w - r * 0.8f, y + 0.5f),
                  col::alpha(col::kWhite, 0.35f), std::max(1.0f, 0.4f * c.s));
}
void circle(Ctx& c, float x, float y, float r, ImU32 color, int seg) {
    c.dl->AddCircleFilled(c.P(x, y), r * c.s, color, seg);
}
void ring(Ctx& c, float x, float y, float r, ImU32 color, float th) {
    c.dl->AddCircle(c.P(x, y), r * c.s, color, 0, std::max(1.0f, th * c.s));
}
void line(Ctx& c, float x0, float y0, float x1, float y1, ImU32 color, float th) {
    c.dl->AddLine(c.P(x0, y0), c.P(x1, y1), color, std::max(1.0f, th * c.s));
}
void tri(Ctx& c, float x0, float y0, float x1, float y1, float x2, float y2, ImU32 color) {
    c.dl->AddTriangleFilled(c.P(x0, y0), c.P(x1, y1), c.P(x2, y2), color);
}
void chevron_down(Ctx& c, float x, float y, float size, ImU32 color) {
    const float h = size * 0.55f;
    tri(c, x - size * 0.5f, y - h * 0.5f, x + size * 0.5f, y - h * 0.5f, x, y + h * 0.5f, color);
}
void bar(Ctx& c, float x, float y, float w, float h, float frac, ImU32 fill, ImU32 track) {
    frac = std::clamp(frac, 0.0f, 1.0f);
    rect(c, x, y, w, h, track, h * 0.5f);
    if (frac > 0) {
        const float fw = std::max(h, w * frac);
        grad_v(c, x, y, fw, h, col::mix(fill, col::kWhite, 0.25f), fill, h * 0.5f);
    }
}
void keycap(Ctx& c, float x, float y, float h, std::string_view label, float* out_w) {
    const float size = h * 0.62f;
    const float tw = text_w(F::Bold, size, label);
    const float w = std::max(h, tw + h * 0.55f);
    rect(c, x, y + h * 0.08f, w, h, col::rgba(0, 0, 0, 120), h * 0.24f);
    grad_v(c, x, y, w, h * 0.94f, col::rgba(250, 251, 255), col::rgba(208, 214, 228), h * 0.24f);
    text(c, F::Bold, size, x + w * 0.5f, y + (h * 0.94f - size) * 0.5f, col::rgba(24, 28, 40),
         label, kCenter, 0, false);
    if (out_w) *out_w = w;
}
void push_clip(Ctx& c, float x, float y, float w, float h) {
    c.dl->PushClipRect(c.P(x, y), c.P(x + w, y + h), true);
}
void pop_clip(Ctx& c) { c.dl->PopClipRect(); }

// ── icons ────────────────────────────────────────────────────────────────
void icon(Ctx& c, Icon i, float cx, float cy, float size, ImU32 color) {
    const float h = size * 0.5f, th = std::max(0.55f, size * 0.09f);
    const ImU32 dim = col::alpha(color, 0.55f);
    switch (i) {
        case Icon::Pokeball: {
            ring(c, cx, cy, h * 0.86f, color, th);
            line(c, cx - h * 0.86f, cy, cx - h * 0.28f, cy, color, th);
            line(c, cx + h * 0.28f, cy, cx + h * 0.86f, cy, color, th);
            ring(c, cx, cy, h * 0.26f, color, th);
            break;
        }
        case Icon::Pokedex: {
            rect_line(c, cx - h * 0.62f, cy - h * 0.82f, h * 1.24f, h * 1.64f, color, h * 0.18f, th);
            circle(c, cx - h * 0.2f, cy - h * 0.38f, h * 0.2f, color);
            line(c, cx - h * 0.35f, cy + h * 0.2f, cx + h * 0.35f, cy + h * 0.2f, dim, th);
            line(c, cx - h * 0.35f, cy + h * 0.48f, cx + h * 0.15f, cy + h * 0.48f, dim, th);
            break;
        }
        case Icon::Bag: {
            rect_line(c, cx - h * 0.72f, cy - h * 0.36f, h * 1.44f, h * 1.16f, color, h * 0.28f, th);
            c.dl->PathArcTo(c.P(cx, cy - h * 0.36f), h * 0.36f * c.s, kPi, kPi * 2.0f, 10);
            c.dl->PathStroke(color, 0, std::max(1.0f, th * c.s));
            line(c, cx - h * 0.72f, cy + h * 0.1f, cx + h * 0.72f, cy + h * 0.1f, dim, th);
            break;
        }
        case Icon::Card: {
            rect_line(c, cx - h * 0.86f, cy - h * 0.58f, h * 1.72f, h * 1.16f, color, h * 0.16f, th);
            circle(c, cx - h * 0.38f, cy - h * 0.12f, h * 0.2f, color);
            c.dl->PathArcTo(c.P(cx - h * 0.38f, cy + h * 0.42f), h * 0.3f * c.s, kPi, kPi * 2, 8);
            c.dl->PathFillConvex(color);
            line(c, cx + h * 0.05f, cy - h * 0.15f, cx + h * 0.6f, cy - h * 0.15f, dim, th);
            line(c, cx + h * 0.05f, cy + h * 0.18f, cx + h * 0.48f, cy + h * 0.18f, dim, th);
            break;
        }
        case Icon::Save: {
            rect_line(c, cx - h * 0.72f, cy - h * 0.72f, h * 1.44f, h * 1.44f, color, h * 0.14f, th);
            rect(c, cx - h * 0.38f, cy - h * 0.72f, h * 0.76f, h * 0.48f, dim, h * 0.06f);
            rect_line(c, cx - h * 0.42f, cy + h * 0.06f, h * 0.84f, h * 0.5f, color, h * 0.06f, th);
            break;
        }
        case Icon::Gear: {
            const int teeth = 8;
            for (int k = 0; k < teeth; ++k) {
                const float a = k * 2.0f * kPi / teeth;
                line(c, cx + std::cos(a) * h * 0.5f, cy + std::sin(a) * h * 0.5f,
                     cx + std::cos(a) * h * 0.86f, cy + std::sin(a) * h * 0.86f, color, th * 1.5f);
            }
            ring(c, cx, cy, h * 0.55f, color, th);
            ring(c, cx, cy, h * 0.2f, color, th);
            break;
        }
        case Icon::Exit: {
            line(c, cx - h * 0.55f, cy - h * 0.55f, cx + h * 0.55f, cy + h * 0.55f, color, th * 1.2f);
            line(c, cx + h * 0.55f, cy - h * 0.55f, cx - h * 0.55f, cy + h * 0.55f, color, th * 1.2f);
            break;
        }
        case Icon::Back: {
            line(c, cx + h * 0.55f, cy, cx - h * 0.5f, cy, color, th * 1.2f);
            line(c, cx - h * 0.5f, cy, cx - h * 0.1f, cy - h * 0.42f, color, th * 1.2f);
            line(c, cx - h * 0.5f, cy, cx - h * 0.1f, cy + h * 0.42f, color, th * 1.2f);
            break;
        }
        case Icon::Flag: {
            line(c, cx - h * 0.5f, cy - h * 0.8f, cx - h * 0.5f, cy + h * 0.8f, color, th);
            tri(c, cx - h * 0.5f, cy - h * 0.8f, cx + h * 0.7f, cy - h * 0.45f, cx - h * 0.5f, cy - h * 0.1f, color);
            break;
        }
        case Icon::Link: {
            ring(c, cx - h * 0.3f, cy, h * 0.4f, color, th);
            ring(c, cx + h * 0.3f, cy, h * 0.4f, color, th);
            break;
        }
        case Icon::Fight: {
            line(c, cx - h * 0.7f, cy + h * 0.7f, cx + h * 0.6f, cy - h * 0.6f, color, th * 1.3f);
            line(c, cx + h * 0.7f, cy + h * 0.7f, cx - h * 0.6f, cy - h * 0.6f, color, th * 1.3f);
            line(c, cx - h * 0.75f, cy + h * 0.35f, cx - h * 0.35f, cy + h * 0.75f, color, th * 1.3f);
            line(c, cx + h * 0.75f, cy + h * 0.35f, cx + h * 0.35f, cy + h * 0.75f, color, th * 1.3f);
            break;
        }
        case Icon::Run: {
            line(c, cx - h * 0.6f, cy, cx + h * 0.55f, cy, color, th * 1.2f);
            line(c, cx + h * 0.55f, cy, cx + h * 0.15f, cy - h * 0.42f, color, th * 1.2f);
            line(c, cx + h * 0.55f, cy, cx + h * 0.15f, cy + h * 0.42f, color, th * 1.2f);
            line(c, cx - h * 0.8f, cy - h * 0.5f, cx - h * 0.35f, cy - h * 0.5f, dim, th);
            line(c, cx - h * 0.8f, cy + h * 0.5f, cx - h * 0.35f, cy + h * 0.5f, dim, th);
            break;
        }
        case Icon::Party: {
            circle(c, cx - h * 0.35f, cy - h * 0.3f, h * 0.24f, color);
            circle(c, cx + h * 0.35f, cy - h * 0.3f, h * 0.24f, color);
            c.dl->PathArcTo(c.P(cx - h * 0.35f, cy + h * 0.55f), h * 0.4f * c.s, kPi, kPi * 2, 8);
            c.dl->PathFillConvex(color);
            c.dl->PathArcTo(c.P(cx + h * 0.35f, cy + h * 0.55f), h * 0.4f * c.s, kPi, kPi * 2, 8);
            c.dl->PathFillConvex(color);
            break;
        }
        case Icon::Info: {
            ring(c, cx, cy, h * 0.8f, color, th);
            circle(c, cx, cy - h * 0.38f, th * 0.9f, color);
            line(c, cx, cy - h * 0.1f, cx, cy + h * 0.45f, color, th * 1.1f);
            break;
        }
        case Icon::Check: {
            line(c, cx - h * 0.6f, cy, cx - h * 0.15f, cy + h * 0.45f, color, th * 1.3f);
            line(c, cx - h * 0.15f, cy + h * 0.45f, cx + h * 0.65f, cy - h * 0.45f, color, th * 1.3f);
            break;
        }
    }
}

// ── keys ─────────────────────────────────────────────────────────────────
std::string key_label(int bit) {
    auto* w = gbarecomp::active_host_window();
    const char* n = w ? w->key_binding_name(bit, 0) : "";
    std::string u;
    for (const char* p = n ? n : ""; *p; ++p) u += static_cast<char>(std::toupper(static_cast<unsigned char>(*p)));
    if (u.empty()) {
        static const char* const kDefault[] = {"E", "Q", "F", "TAB", "D", "A", "W", "S", "C", "Z"};
        return (bit >= 0 && bit < 10) ? kDefault[bit] : "?";
    }
    if (u == "RETURN") return "ENTER";
    if (u == "ESCAPE") return "ESC";
    if (u == "BACKSPACE") return "BKSP";
    if (u == "LEFT SHIFT" || u == "RIGHT SHIFT") return "SHIFT";
    if (u == "PAGEUP") return "PG UP";
    if (u == "PAGEDOWN") return "PG DN";
    if (u == "UP") return "\xe2\x86\x91";
    if (u == "DOWN") return "\xe2\x86\x93";
    if (u == "LEFT") return "\xe2\x86\x90";
    if (u == "RIGHT") return "\xe2\x86\x92";
    return u;
}

float keycap_for(Ctx& c, float x, float y, float h, int bit, bool measure_only) {
    const std::string l = key_label(bit);
    const float size = h * 0.62f;
    const float w = std::max(h, text_w(F::Bold, size, l) + h * 0.55f);
    if (!measure_only) keycap(c, x, y, h, l);
    return w;
}

// ── ROM sprites → textures ──────────────────────────────────────────────
namespace {
std::vector<std::uint8_t> lz77(const Mem& m, std::uint32_t addr) {
    std::vector<std::uint8_t> out;
    const std::uint32_t head = m.u32(addr);
    if ((head & 0xFF) != 0x10) return out;
    const std::uint32_t size = head >> 8;
    if (size == 0 || size > 0x10000) return out;
    out.reserve(size);
    std::uint32_t p = addr + 4;
    while (out.size() < size) {
        const std::uint8_t flags = m.u8(p++);
        for (int b = 0; b < 8 && out.size() < size; ++b) {
            if (flags & (0x80 >> b)) {
                const std::uint8_t b1 = m.u8(p++), b2 = m.u8(p++);
                const int len = (b1 >> 4) + 3;
                const int disp = (((b1 & 0xF) << 8) | b2) + 1;
                for (int k = 0; k < len && out.size() < size; ++k) {
                    if (disp > static_cast<int>(out.size())) out.push_back(0);
                    else out.push_back(out[out.size() - disp]);
                }
            } else {
                out.push_back(m.u8(p++));
            }
        }
        if (p - addr > 0x20000) break;
    }
    return out;
}

void palette_rgba(const std::uint8_t* pal16, std::uint32_t out[16]) {
    for (int i = 0; i < 16; ++i) {
        const std::uint16_t c = static_cast<std::uint16_t>(pal16[i * 2] | (pal16[i * 2 + 1] << 8));
        const int r = (c & 31) * 255 / 31, g = ((c >> 5) & 31) * 255 / 31, b = ((c >> 10) & 31) * 255 / 31;
        out[i] = i == 0 ? 0u : (0xFF000000u | (static_cast<std::uint32_t>(b) << 16) |
                                (static_cast<std::uint32_t>(g) << 8) | static_cast<std::uint32_t>(r));
    }
}

// tiles: 4bpp, 1D layout, tw x th tiles.
std::vector<std::uint32_t> decode_4bpp(const std::uint8_t* tiles, std::size_t avail, int tw, int th,
                                       const std::uint32_t pal[16]) {
    std::vector<std::uint32_t> px(static_cast<std::size_t>(tw) * 8 * th * 8, 0);
    const int W = tw * 8;
    for (int t = 0; t < tw * th; ++t) {
        if (static_cast<std::size_t>(t + 1) * 32 > avail) break;
        const int bx = (t % tw) * 8, by = (t / tw) * 8;
        for (int y = 0; y < 8; ++y)
            for (int x = 0; x < 8; ++x) {
                const std::uint8_t b = tiles[t * 32 + y * 4 + x / 2];
                const int idx = (x & 1) ? (b >> 4) : (b & 15);
                px[(by + y) * W + bx + x] = pal[idx];
            }
    }
    return px;
}

std::map<std::tuple<int, int, int>, Tex> g_tex;

Tex upload(const std::vector<std::uint32_t>& px, int w, int h) {
    Tex t;
    auto* hw = gbarecomp::active_host_window();
    auto* r = hw ? static_cast<SDL_Renderer*>(hw->sdl_renderer()) : nullptr;
    if (!r || px.empty()) return t;
    SDL_Texture* tex = SDL_CreateTexture(r, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STATIC, w, h);
    if (!tex) return t;
    SDL_UpdateTexture(tex, nullptr, px.data(), w * 4);
    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
#if SDL_VERSION_ATLEAST(2, 0, 12)
    SDL_SetTextureScaleMode(tex, SDL_ScaleModeNearest);
#endif
    t.id = tex;
    t.w = w;
    t.h = h;
    return t;
}

// Crop transparent borders (front pics have generous padding).
void bounds(const std::vector<std::uint32_t>& px, int w, int h, int* x0, int* y0, int* x1, int* y1) {
    *x0 = w; *y0 = h; *x1 = 0; *y1 = 0;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            if (px[y * w + x] >> 24) {
                *x0 = std::min(*x0, x); *y0 = std::min(*y0, y);
                *x1 = std::max(*x1, x + 1); *y1 = std::max(*y1, y + 1);
            }
}

int unown_form(int species, std::uint32_t p) {
    if (species != 201) return species;
    const int letter = static_cast<int>((((p & 0x3000000) >> 18) | ((p & 0x30000) >> 12) |
                                         ((p & 0x300) >> 6) | (p & 0x3)) % 28);
    return letter ? 413 + letter - 1 : species;
}
}  // namespace

Tex tex_mon_icon(const Mem& m, int species, int frame) {
    if (species <= 0 || species >= 440) species = 0;
    const auto key = std::make_tuple(0, species, frame & 1);
    auto it = g_tex.find(key);
    if (it != g_tex.end()) return it->second;
    Tex t;
    const std::uint32_t gfx = m.u32(0x083D37A0 + species * 4);
    const int pal_idx = m.u8(0x083D3E80 + std::min(species, 439));
    const std::uint32_t pal_ptr = m.u32(0x083D4038 + (pal_idx % 6) * 8);
    if (m.valid(gfx, 0x400) && m.valid(pal_ptr, 32)) {
        std::uint32_t pal[16];
        palette_rgba(m.ptr(pal_ptr, 32), pal);
        auto px = decode_4bpp(m.ptr(gfx + (frame & 1) * 0x200, 0x200), 0x200, 4, 4, pal);
        t = upload(px, 32, 32);
    }
    g_tex[key] = t;
    return t;
}

Tex tex_mon_front(const Mem& m, int species, bool shiny, std::uint32_t personality) {
    species = unown_form(species, personality);
    if (species <= 0 || species >= 440) species = 0;
    const auto key = std::make_tuple(1, species, shiny ? 1 : 0);
    auto it = g_tex.find(key);
    if (it != g_tex.end()) return it->second;
    Tex t;
    const std::uint32_t sheet = m.u32(0x082350AC + species * 8);
    const std::uint32_t pal_c = m.u32((shiny ? 0x082380CC : 0x0823730C) + species * 8);
    auto tiles = lz77(m, sheet);
    auto pal_raw = lz77(m, pal_c);
    if (tiles.size() >= 0x800 && pal_raw.size() >= 32) {
        std::uint32_t pal[16];
        palette_rgba(pal_raw.data(), pal);
        auto px = decode_4bpp(tiles.data(), tiles.size(), 8, 8, pal);
        t = upload(px, 64, 64);
    }
    g_tex[key] = t;
    return t;
}

Tex tex_trainer_front(const Mem& m, int pic) {
    if (pic < 0 || pic > 147) pic = 0;
    const auto key = std::make_tuple(3, pic, 0);
    auto it = g_tex.find(key);
    if (it != g_tex.end()) return it->second;
    Tex t;
    auto tiles = lz77(m, m.u32(0x0823957C + pic * 8));
    auto pal_raw = lz77(m, m.u32(0x08239A1C + pic * 8));
    if (tiles.size() >= 0x800 && pal_raw.size() >= 32) {
        std::uint32_t pal[16];
        palette_rgba(pal_raw.data(), pal);
        auto px = decode_4bpp(tiles.data(), tiles.size(), 8, 8, pal);
        t = upload(px, 64, 64);
    }
    g_tex[key] = t;
    return t;
}

Tex tex_item_icon(const Mem& m, int item) {
    if (item < 0 || item > 375) item = 0;
    const auto key = std::make_tuple(2, item, 0);
    auto it = g_tex.find(key);
    if (it != g_tex.end()) return it->second;
    Tex t;
    const std::uint32_t gfx = m.u32(0x083D4294 + item * 8);
    const std::uint32_t palp = m.u32(0x083D4294 + item * 8 + 4);
    auto tiles = lz77(m, gfx);
    auto pal_raw = lz77(m, palp);
    if (tiles.size() >= 9 * 32 && pal_raw.size() >= 32) {
        std::uint32_t pal[16];
        palette_rgba(pal_raw.data(), pal);
        auto px = decode_4bpp(tiles.data(), tiles.size(), 3, 3, pal);
        t = upload(px, 24, 24);
    }
    g_tex[key] = t;
    return t;
}

// A window's own pixel buffer (fallback for windows holding graphics).
Tex tex_window_pixels(const Mem& m, int window, std::uint32_t gen) {
    struct Entry { Tex t; std::uint32_t gen = 0xFFFFFFFFu; int w = 0, h = 0; };
    static Entry cache[32];
    if (window < 0 || window >= 32) return {};
    const WindowInfo w = window_info(m, window);
    if (!w.live()) return {};
    Entry& e = cache[window];
    if (e.t && e.gen == gen && e.w == w.width && e.h == w.height) return e.t;
    const std::size_t bytes = static_cast<std::size_t>(w.width) * w.height * 32;
    const std::uint8_t* tiles = m.ptr(w.tiles, bytes);
    const std::uint8_t* pal = m.ptr(0x05000000u + (w.palette & 15) * 32, 32);
    if (!tiles || !pal) return {};
    std::uint32_t rgba[16];
    palette_rgba(pal, rgba);
    auto px = decode_4bpp(tiles, bytes, w.width, w.height, rgba);
    if (e.t.id) SDL_DestroyTexture(static_cast<SDL_Texture*>(e.t.id));
    e.t = upload(px, w.width * 8, w.height * 8);
    e.gen = gen;
    e.w = w.width;
    e.h = w.height;
    return e.t;
}

void image(Ctx& c, const Tex& t, float x, float y, float w, float h, ImU32 tint) {
    if (!t) return;
    c.dl->AddImage(static_cast<ImTextureID>(reinterpret_cast<std::intptr_t>(t.id)), c.P(x, y),
                   c.P(x + w, y + h), ImVec2(0, 0), ImVec2(1, 1), tint);
}

void textures_reset() {
    for (auto& kv : g_tex)
        if (kv.second.id) SDL_DestroyTexture(static_cast<SDL_Texture*>(kv.second.id));
    g_tex.clear();
}

// ── game data ────────────────────────────────────────────────────────────
float scroll_anim(ScrollAnim& a, std::uint32_t key, int target, float dt) {
    const float t = static_cast<float>(target);
    if (a.key != key || a.pos < 0 || std::fabs(t - a.pos) > 6.0f) { a.key = key; a.pos = t; return t; }
    a.pos += (t - a.pos) * (1.0f - std::exp(-dt * 20.0f));
    if (std::fabs(t - a.pos) < 0.01f) a.pos = t;
    return a.pos;
}

bool g_modern_text = true;
bool modern_text() { return g_modern_text; }
void set_modern_text(bool on) { g_modern_text = on; }

std::string rom_string(const Mem& m, std::uint32_t addr, std::size_t max, bool one_line) {
    const std::uint8_t* p = m.ptr(addr, 1);
    if (!p) return {};
    std::size_t n = 0;
    while (n < max && m.ptr(addr + n, 1)) ++n;
    return decode_gba(p, n, (g_modern_text ? kDecodeModern : 0) | (one_line ? kDecodeOneLine : 0));
}
std::string species_name(const Mem& m, int species) {
    return rom_string(m, 0x08245EE0 + species * 11, 11);
}
std::string move_name(const Mem& m, int move) { return rom_string(m, 0x08247094 + move * 13, 13); }
std::string item_name(const Mem& m, int item) { return rom_string(m, 0x083DB028 + item * 44, 14); }
std::string type_name(int type) {
    static const char* const k[] = {"Normal", "Fighting", "Flying", "Poison", "Ground", "Rock",
                                    "Bug", "Ghost", "Steel", "???", "Fire", "Water", "Grass",
                                    "Electric", "Psychic", "Ice", "Dragon", "Dark"};
    return (type >= 0 && type < 18) ? k[type] : "???";
}
int move_type(const Mem& m, int move) { return m.u8(0x08250C04 + move * 12 + 2); }
int move_power(const Mem& m, int move) { return m.u8(0x08250C04 + move * 12 + 1); }
int move_accuracy(const Mem& m, int move) { return m.u8(0x08250C04 + move * 12 + 3); }
int move_pp(const Mem& m, int move) { return m.u8(0x08250C04 + move * 12 + 4); }
int species_type(const Mem& m, int species, int which) {
    return m.u8(0x08254784 + species * 28 + 6 + (which & 1));
}
int type_effectiveness(const Mem& m, int atk, int d1, int d2) {
    int mult = 10;
    for (std::uint32_t p = 0x0824F050; p < 0x0824F050 + 0x150; p += 3) {
        const int a = m.u8(p), d = m.u8(p + 1), x = m.u8(p + 2);
        if (a == 0xFF) break;
        if (a == 0xFE) continue;  // foresight separator
        if (a != atk) continue;
        if (d == d1) mult = mult * x / 10;
        if (d == d2 && d2 != d1) mult = mult * x / 10;
    }
    return mult;
}

int exp_for_level(const Mem& m, int species, int level) {
    const int growth = m.u8(0x08254784 + species * 28 + 19);
    level = std::clamp(level, 0, 100);
    return static_cast<int>(m.u32(0x08253AE4 + (growth * 101 + level) * 4));
}

MonView read_mon(const Mem& m, std::uint32_t a, bool party) {
    MonView v;
    if (!m.valid(a, party ? 100 : 80)) return v;
    v.personality = m.u32(a);
    v.ot_id = m.u32(a + 4);
    const std::uint8_t flags = m.u8(a + 0x13);
    if (!(flags & 0x02)) return v;  // hasSpecies
    v.bad_egg = (flags & 0x01) != 0;
    std::uint32_t plain[12];
    const std::uint32_t key = v.personality ^ v.ot_id;
    std::uint16_t sum = 0;
    for (int i = 0; i < 12; ++i) {
        plain[i] = m.u32(a + 32 + i * 4) ^ key;
        sum = static_cast<std::uint16_t>(sum + (plain[i] & 0xFFFF) + (plain[i] >> 16));
    }
    if (sum != m.u16(a + 28)) { v.bad_egg = true; }
    static const std::uint8_t kOrder[24][4] = {
        {0,1,2,3},{0,1,3,2},{0,2,1,3},{0,3,1,2},{0,2,3,1},{0,3,2,1},{1,0,2,3},{1,0,3,2},
        {2,0,1,3},{3,0,1,2},{2,0,3,1},{3,0,2,1},{1,2,0,3},{1,3,0,2},{2,1,0,3},{3,1,0,2},
        {2,3,0,1},{3,2,0,1},{1,2,3,0},{1,3,2,0},{2,1,3,0},{3,1,2,0},{2,3,1,0},{3,2,1,0}};
    const std::uint8_t* o = kOrder[v.personality % 24];
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(plain);
    const std::uint8_t* gr = bytes + o[0] * 12;
    const std::uint8_t* at = bytes + o[1] * 12;
    const std::uint8_t* ev = bytes + o[2] * 12;
    const std::uint8_t* mi = bytes + o[3] * 12;
    auto rd16 = [](const std::uint8_t* p) { return static_cast<int>(p[0] | (p[1] << 8)); };
    auto rd32 = [](const std::uint8_t* p) {
        return static_cast<std::uint32_t>(p[0] | (p[1] << 8) | (p[2] << 16) | (std::uint32_t(p[3]) << 24));
    };
    v.species = rd16(gr);
    v.item = rd16(gr + 2);
    v.exp = rd32(gr + 4);
    v.pp_bonuses = gr[8];
    v.friendship = gr[9];
    for (int i = 0; i < 4; ++i) { v.moves[i] = rd16(at + i * 2); v.pp[i] = at[8 + i]; }
    for (int i = 0; i < 6; ++i) v.evs[i] = ev[i];
    v.met_location = mi[1];
    const int origins = rd16(mi + 2);
    v.met_level = origins & 0x7F;
    v.ball = (origins >> 11) & 0xF;
    const std::uint32_t ivs = rd32(mi + 4);
    for (int i = 0; i < 6; ++i) v.ivs[i] = (ivs >> (i * 5)) & 31;
    v.egg = ((ivs >> 30) & 1) != 0 || (flags & 0x04) != 0;
    v.ability_num = (ivs >> 31) & 1;
    if (v.bad_egg) v.egg = true;
    v.nickname = decode_gba(m.ptr(a + 8, 10), 10, 0);
    v.ot_name = decode_gba(m.ptr(a + 20, 7), 7, 0);
    if (g_modern_text) { v.nickname = modern_case(v.nickname); v.ot_name = modern_case(v.ot_name); }
    const int ratio = m.u8(0x08254784 + std::clamp(v.species, 0, 411) * 28 + 16);
    v.gender = ratio == 255 ? 2 : ratio == 254 ? 1 : ratio == 0 ? 0 : ((v.personality & 0xFF) < static_cast<std::uint32_t>(ratio) ? 1 : 0);
    v.shiny = (((v.ot_id >> 16) ^ (v.ot_id & 0xFFFF) ^ (v.personality >> 16) ^ (v.personality & 0xFFFF))) < 8;
    if (party) {
        v.status = static_cast<int>(m.u32(a + 80));
        v.level = m.u8(a + 84);
        v.hp = m.u16(a + 86);
        v.max_hp = m.u16(a + 88);
        for (int i = 0; i < 5; ++i) v.stats[i] = m.u16(a + 90 + i * 2);
    } else {
        int lv = 1;
        while (lv < 100 && v.exp >= static_cast<std::uint32_t>(exp_for_level(m, v.species, lv + 1))) ++lv;
        v.level = lv;
    }
    v.valid = v.species > 0 && v.species < 440;
    return v;
}

std::string status_label(int s) {
    if (s & 7) return "SLP";
    if (s & 0x88) return "PSN";
    if (s & 0x10) return "BRN";
    if (s & 0x20) return "FRZ";
    if (s & 0x40) return "PAR";
    return "";
}
ImU32 status_color(int s) {
    if (s & 7) return col::rgba(140, 150, 170);
    if (s & 0x88) return col::rgba(176, 90, 200);
    if (s & 0x10) return col::rgba(240, 120, 50);
    if (s & 0x20) return col::rgba(110, 200, 230);
    if (s & 0x40) return col::rgba(230, 196, 40);
    return 0;
}

Fade guest_fade(const Mem& m) {
    Fade f;
    // gPaletteFade: +0 selected palettes, +4 {delayCounter:6, y:5, targetY:5},
    // +6 {blendColor:15, active:1}. y (0..16) is the live blend amount.
    const std::uint32_t selected = m.u32(g::PaletteFade);
    const std::uint16_t w4 = m.u16(g::PaletteFade + 4), w6 = m.u16(g::PaletteFade + 6);
    const int y = std::min(16, (w4 >> 6) & 31);
    const std::uint16_t blend = w6 & 0x7FFF;
    int bg_pals = 0;
    for (int i = 0; i < 16; ++i) bg_pals += (selected >> i) & 1;
    if (y > 0 && bg_pals >= 12) {
        const int lum = (blend & 31) + ((blend >> 5) & 31) + ((blend >> 10) & 31);
        if (lum >= 60) f.white = y / 16.0f;
        else f.black = y / 16.0f;
    }
    // Hardware brightness (BLDCNT effect 2/3 on all BGs).
    const std::uint16_t bld = m.io16(0x50);
    const int by = std::min(16, m.io16(0x54) & 0x1F);
    const int effect = (bld >> 6) & 3;
    // (only a full-screen effect: with windows on, BLDY usually darkens
    //  just the area around a highlighted box)
    if ((bld & 0x0F) == 0x0F && !(m.io16(0) & 0xE000)) {
        if (effect == 3) f.black = std::max(f.black, by / 16.0f);
        if (effect == 2) f.white = std::max(f.white, by / 16.0f);
    }
    if (m.io16(0) & 0x80) f.black = 1.0f;  // forced blank
    f.black = std::clamp(f.black, 0.0f, 1.0f);
    f.white = std::clamp(f.white, 0.0f, 1.0f);
    return f;
}

}  // namespace frlg::hd
