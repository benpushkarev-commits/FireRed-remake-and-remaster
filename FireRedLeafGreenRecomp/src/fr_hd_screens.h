// fr_hd_screens.h — shared chrome for the HD full-screen menus.
#pragma once

#include <initializer_list>
#include <string>

#include "fr_hd_draw.h"

namespace frlg::hd {

void screen_bg(Ctx& c, ImU32 accent);
void title(Ctx& c, const std::string& t, const std::string& sub = {});
struct Hint { int bit; const char* label; int bit2 = -1; };
void hints(Ctx& c, float x_right, float y, std::initializer_list<Hint> list);
void type_badge(Ctx& c, int type, float x, float y, float h, float* out_w = nullptr);
void wrap_text(Ctx& c, F f, float size, float x, float y, float max_w, float line_h, ImU32 color,
               const std::string& s, int max_lines = 6);
int wrap_count(F f, float size, float max_w, const std::string& s);   // lines wrap_text would use
std::string window_message(int id);
void message_bar(Ctx& c, float x, float y, float w, float h, const std::string& msg, bool waiting);
void context_menu(Ctx& c, const Scene& s, float x, float y_bottom, float w);
// "−  ×03  +" picker for a quantity window the game prints (toss / sell).
void quantity_stepper(Ctx& c, float x, float y, float w, const std::string& q);
bool is_quantity_text(const std::string& t);
int bag_count(const Mem& m, int item);
std::string money_str(unsigned v);

// fr_hd_screens2.cpp
bool screens2_draw(Ctx& c);
// fr_hd_map.cpp (Town Map / Fly)
bool map_draw(Ctx& c);
// fr_hd_naming.cpp
bool naming_draw(Ctx& c);

}  // namespace frlg::hd
