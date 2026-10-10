// fr_hd_text.h — FireRed character set to UTF-8, and modern casing.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace frlg::hd {

enum DecodeFlags : unsigned {
    kDecodeModern = 1,   // title-case ALL-CAPS words (POKéMON -> Pokémon)
    kDecodeOneLine = 2,  // line breaks / prompts become spaces
};

// UTF-8 for one GBA glyph code (0x100 + n = CHAR_EXTRA_SYMBOL n); "" if none.
const char* gba_glyph_utf8(unsigned code);
// Decode a 0xFF-terminated game string (control codes skipped).
std::string decode_gba(const std::uint8_t* s, std::size_t max, unsigned flags = kDecodeModern);
// "POKéMON CENTER" -> "Pokémon Center"; acronyms (HP, PP, TM, PC ...) kept.
std::string modern_case(const std::string& utf8);

}  // namespace frlg::hd
