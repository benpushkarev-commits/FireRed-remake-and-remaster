// fr_hd_text.cpp — FireRed character set to UTF-8, and modern casing.
#include "fr_hd_text.h"

#include <cctype>
#include <cstring>

namespace frlg::hd {
namespace {

// pokefirered charmap.txt (Latin set). "" = no glyph / control code.
const char* const kCharset[256] = {
    " ", "\xc3\x80", "\xc3\x81", "\xc3\x82", "\xc3\x87", "\xc3\x88", "\xc3\x89", "\xc3\x8a",  // 00
    "\xc3\x8b", "\xc3\x8c", "", "\xc3\x8e", "\xc3\x8f", "\xc3\x92", "\xc3\x93", "\xc3\x94",  // 08
    "\xc5\x92", "\xc3\x99", "\xc3\x9a", "\xc3\x9b", "\xc3\x91", "\xc3\x9f", "\xc3\xa0", "\xc3\xa1",  // 10
    "", "\xc3\xa7", "\xc3\xa8", "\xc3\xa9", "\xc3\xaa", "\xc3\xab", "\xc3\xac", "",  // 18
    "\xc3\xae", "\xc3\xaf", "\xc3\xb2", "\xc3\xb3", "\xc3\xb4", "\xc5\x93", "\xc3\xb9", "\xc3\xba",  // 20
    "\xc3\xbb", "\xc3\xb1", "\xc2\xba", "\xc2\xaa", "er", "&", "+", "",  // 28
    "", "", "", "", "Lv", "=", ";", " ",  // 30
    "", "", "", "", "", "", "", "",  // 38
    "", "", "", "", "", "", "", "",  // 40
    "", "", "", "", "", "", "", "",  // 48
    "", "\xc2\xbf", "\xc2\xa1", "Pk", "Mn", "Po", "K\xc3\xa9", "Bl",  // 50
    "oc", "k", "\xc3\x8d", "%", "(", ")", "", "",  // 58
    "", "", "", "", "", "", "", "",  // 60
    "\xc3\xa2", "", "", "", "", "", "", "\xc3\xad",  // 68
    "", "", "", "", "", "", "", " ",  // 70
    "", "\xe2\x86\x91", "\xe2\x86\x93", "\xe2\x86\x90", "\xe2\x86\x92", "", "", "",  // 78
    "", "", "", "", "e", "<", ">", "",  // 80
    "", "", "", "", "", "", "", "",  // 88
    "", "", "", "", "", "", "", "",  // 90
    "", "", "", "", "", "", "", "",  // 98
    "re", "0", "1", "2", "3", "4", "5", "6",  // A0
    "7", "8", "9", "!", "?", ".", "-", "\xc2\xb7",  // A8
    "\xe2\x80\xa6", "\xe2\x80\x9c", "\xe2\x80\x9d", "\xe2\x80\x98", "\xe2\x80\x99", "\xe2\x99\x82", "\xe2\x99\x80", "\xe2\x82\xbd",  // B0
    ",", "\xc3\x97", "/", "A", "B", "C", "D", "E",  // B8
    "F", "G", "H", "I", "J", "K", "L", "M",  // C0
    "N", "O", "P", "Q", "R", "S", "T", "U",  // C8
    "V", "W", "X", "Y", "Z", "a", "b", "c",  // D0
    "d", "e", "f", "g", "h", "i", "j", "k",  // D8
    "l", "m", "n", "o", "p", "q", "r", "s",  // E0
    "t", "u", "v", "w", "x", "y", "z", "\xe2\x96\xb6",  // E8
    ":", "\xc3\x84", "\xc3\x96", "\xc3\x9c", "\xc3\xa4", "\xc3\xb6", "\xc3\xbc", "",  // F0
    "", "", "", "", "", "", "", "",  // F8
};

// CHAR_EXTRA_SYMBOL (0xF9 nn) glyphs.
const char* const kExtra[] = {
    "\xe2\x86\x91", "\xe2\x86\x93", "\xe2\x86\x90", "\xe2\x86\x92", "+", "Lv", "PP", "ID", "No",
    "_", "1", "2", "3", "4", "5", "6", "7", "8", "9", "(", ")", "\xe2\x97\x8e",
    "\xe2\x96\xb3", "\xe2\x9c\x95",
};

int ext_ctrl_args(unsigned code) {
    switch (code) {
        case 0x01: case 0x02: case 0x03: case 0x05: case 0x06: case 0x08:
        case 0x0C: case 0x0D: case 0x0E: case 0x11: case 0x12: case 0x13: case 0x14:
            return 1;
        case 0x04: return 3;
        case 0x0B: case 0x10: return 2;
        default: return 0;
    }
}

bool is_upper(unsigned char c) { return c >= 'A' && c <= 'Z'; }
bool is_lower(unsigned char c) { return c >= 'a' && c <= 'z'; }

const char* const kAcronyms[] = {
    "HP", "PP", "PC", "TM", "HM", "ID", "OT", "OK", "EXP", "KO", "TV", "SS", "NPC", "EV",
    "IV", "ATK", "DEF", "SPD", "UFO", "DNA", "CD", "PS", "TMS", "HMS", "II", "III", "IV",
    "PKMN", "GBA", "VS",
};

}  // namespace

const char* gba_glyph_utf8(unsigned code) {
    if (code >= 0x100) {
        const unsigned i = code - 0x100;
        return i < sizeof(kExtra) / sizeof(kExtra[0]) ? kExtra[i] : "";
    }
    return kCharset[code & 0xFF];
}

std::string decode_gba(const std::uint8_t* s, std::size_t max, unsigned flags) {
    std::string out;
    if (!s) return out;
    for (std::size_t i = 0; i < max; ++i) {
        const unsigned c = s[i];
        if (c == 0xFF) break;
        if (c == 0xFE || c == 0xFA || c == 0xFB) {
            if (flags & kDecodeOneLine) out += ' ';
            else out += c == 0xFB ? "\n\n" : "\n";
            continue;
        }
        if (c == 0xFC) {
            if (i + 1 >= max) break;
            const unsigned code = s[i + 1];
            if (code == 0x0C && i + 2 < max) { out += gba_glyph_utf8(s[i + 2]); i += 2; continue; }
            i += 1 + ext_ctrl_args(code);
            continue;
        }
        if (c == 0xFD) { ++i; continue; }          // unexpanded placeholder
        if (c == 0xF7) { ++i; continue; }          // dynamic list text
        if (c == 0xF8) { ++i; continue; }          // keypad icon (callers draw keys)
        if (c == 0xF9) { if (i + 1 < max) out += gba_glyph_utf8(0x100 + s[++i]); continue; }
        if (c == 0x53 && i + 1 < max && s[i + 1] == 0x54) { out += "Pok\xc3\xa9mon"; ++i; continue; }
        out += gba_glyph_utf8(c);
    }
    if (flags & kDecodeModern) out = modern_case(out);
    return out;
}

std::string modern_case(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    std::size_t i = 0;
    const std::size_t n = in.size();
    auto letter_at = [&](std::size_t p, std::size_t* len) -> int {
        const unsigned char c = static_cast<unsigned char>(in[p]);
        if (is_upper(c) || is_lower(c)) { *len = 1; return c; }
        // Latin-1 letters (U+00C0..U+00FF, minus x/division) as 2-byte UTF-8.
        if (c == 0xC3 && p + 1 < n) {
            const unsigned char d = static_cast<unsigned char>(in[p + 1]);
            if (d != 0x97 && d != 0xB7) { *len = 2; return 0x100 | d; }
        }
        if (c == 0xC5 && p + 1 < n) { *len = 2; return 0x200 | static_cast<unsigned char>(in[p + 1]); }
        *len = 0;
        return 0;
    };
    while (i < n) {
        std::size_t len = 0;
        if (!letter_at(i, &len)) { out += in[i++]; continue; }
        // A word: a run of letters.
        const std::size_t start = i;
        int upper = 0, lower = 0, letters = 0;
        while (i < n) {
            const int l = letter_at(i, &len);
            if (!l) break;
            if (l < 0x100) { if (is_upper(static_cast<unsigned char>(l))) ++upper; else ++lower; }
            else if ((l & 0xFF) >= 0x80 && (l & 0xFF) < 0xA0 && l < 0x200) ++upper;  // À..ß
            else if (l < 0x200 && (l & 0xFF) != 0xA9) ++lower;                        // é is neutral
            ++letters;
            i += len;
        }
        std::string word = in.substr(start, i - start);
        // Followed by a digit (TM28, HM05) or a single letter: leave it.
        const bool next_digit = i < n && in[i] >= '0' && in[i] <= '9';
        bool acronym = false;
        for (const char* a : kAcronyms) if (word == a) { acronym = true; break; }
        // Contractions / possessives in caps text: BILL'S -> Bill's, DON'T -> Don't.
        const bool after_apos = start >= 2 &&
            (in[start - 1] == '\'' || (start >= 4 && static_cast<unsigned char>(in[start - 1]) == 0x99 &&
                                        static_cast<unsigned char>(in[start - 2]) == 0x80 &&
                                        static_cast<unsigned char>(in[start - 3]) == 0xE2)) &&
            std::isalpha(static_cast<unsigned char>(in[start - (in[start - 1] == '\'' ? 2 : 4)]));
        if (after_apos && upper >= 1 && lower == 0 && letters <= 2) {
            for (char& ch : word) if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
            out += word;
            continue;
        }
        static const char* const kSmall[] = {"OF", "THE", "AND", "TO", "IN", "ON", "AT", "A", "FOR", "OR"};
        bool small = false;
        for (const char* w : kSmall) if (word == w) small = true;
        if (small && !out.empty() && upper == letters) {
            // a small word inside a caps title: "HALL OF FAME" -> "Hall of Fame"
            bool mid = false;
            for (std::size_t k = out.size(); k-- > 0;) {
                const char ch = out[k];
                if (ch == ' ') continue;
                mid = std::isalpha(static_cast<unsigned char>(ch)) != 0;
                break;
            }
            if (mid && i < n && in[i] == ' ') {
                for (char& ch : word) if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
                out += word;
                continue;
            }
        }
        if (upper >= 2 && lower == 0 && !acronym && !next_digit) {
            std::string w;
            bool first = true;
            for (std::size_t k = 0; k < word.size(); ++k) {
                unsigned char c = static_cast<unsigned char>(word[k]);
                if (first) { w += static_cast<char>(c); first = (c == 0xC3 || c == 0xC5); continue; }
                if (is_upper(c)) c = static_cast<unsigned char>(c - 'A' + 'a');
                else if (c == 0xC3 && k + 1 < word.size()) {
                    w += static_cast<char>(c);
                    unsigned char d = static_cast<unsigned char>(word[++k]);
                    if (d >= 0x80 && d < 0x9F && d != 0x97) d = static_cast<unsigned char>(d + 0x20);
                    w += static_cast<char>(d);
                    continue;
                }
                w += static_cast<char>(c);
            }
            word = w;
        }
        out += word;
    }
    return out;
}

}  // namespace frlg::hd
