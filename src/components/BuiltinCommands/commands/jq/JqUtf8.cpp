#include "commands/jq/JqUtf8.h"

namespace Haisos::Jq {
namespace {

// The U+FFFD replacement character, as UTF-8.
void AppendReplacement(std::string& out) {
    out += "\xEF\xBF\xBD";
}

bool IsContinuation(char c) {
    const unsigned char b = static_cast<unsigned char>(c);
    return b >= 0x80 && b <= 0xBF;
}

} // namespace

void AppendUtf8(std::string& out, unsigned int codePoint) {
    if (codePoint < 0x80) {
        out += static_cast<char>(codePoint);
    } else if (codePoint < 0x800) {
        out += static_cast<char>(0xC0 | (codePoint >> 6));
        out += static_cast<char>(0x80 | (codePoint & 0x3F));
    } else if (codePoint < 0x10000) {
        out += static_cast<char>(0xE0 | (codePoint >> 12));
        out += static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (codePoint & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (codePoint >> 18));
        out += static_cast<char>(0x80 | ((codePoint >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (codePoint & 0x3F));
    }
}

std::string RepairUtf8(std::string_view bytes) {
    std::string out;
    out.reserve(bytes.size());
    size_t i = 0;
    while (i < bytes.size()) {
        const unsigned char lead = static_cast<unsigned char>(bytes[i]);
        if (lead < 0x80) {
            out += static_cast<char>(lead);
            ++i;
            continue;
        }
        // A lead byte C2-DF needs one continuation, E0-EF two, F0-F4 three;
        // everything else starts no sequence and is one U+FFFD alone.
        size_t need = 0;
        unsigned int codePoint = 0;
        unsigned int minValue = 0;
        if (lead >= 0xC2 && lead <= 0xDF) {
            need = 1;
            codePoint = lead & 0x1F;
            minValue = 0x80;
        } else if (lead >= 0xE0 && lead <= 0xEF) {
            need = 2;
            codePoint = lead & 0x0F;
            minValue = 0x800;
        } else if (lead >= 0xF0 && lead <= 0xF4) {
            need = 3;
            codePoint = lead & 0x07;
            minValue = 0x10000;
        } else {
            AppendReplacement(out);
            ++i;
            continue;
        }
        size_t taken = 0;
        while (taken < need && i + 1 + taken < bytes.size() &&
               IsContinuation(bytes[i + 1 + taken])) {
            codePoint = (codePoint << 6) |
                        (static_cast<unsigned char>(bytes[i + 1 + taken]) & 0x3F);
            ++taken;
        }
        if (taken < need) {
            // Cut short: the lead and what followed it are one U+FFFD, and
            // the byte that stopped the sequence is read afresh.
            AppendReplacement(out);
            i += 1 + taken;
            continue;
        }
        if (codePoint < minValue || (codePoint >= 0xD800 && codePoint <= 0xDFFF) ||
            codePoint > 0x10FFFF) {
            AppendReplacement(out);
        } else {
            AppendUtf8(out, codePoint);
        }
        i += 1 + taken;
    }
    return out;
}

size_t Utf8Length(const std::string& utf8) {
    size_t count = 0;
    for (size_t i = 0; i < utf8.size(); ++i) {
        const unsigned char lead = static_cast<unsigned char>(utf8[i]);
        size_t need = 0;
        if (lead < 0x80) {
            need = 0;
        } else if (lead >= 0xC2 && lead <= 0xDF) {
            need = 1;
        } else if (lead >= 0xE0 && lead <= 0xEF) {
            need = 2;
        } else if (lead >= 0xF0 && lead <= 0xF4) {
            need = 3;
        } else {
            need = 0;  // starts no sequence: one code point on its own
        }
        while (need > 0 && i + 1 < utf8.size() &&
               IsContinuation(utf8[i + 1])) {
            --need;
            ++i;
        }
        ++count;
    }
    return count;
}

size_t Utf8ByteOffset(const std::string& utf8, size_t codePoints) {
    size_t seen = 0;
    size_t i = 0;
    while (i < utf8.size() && seen < codePoints) {
        const unsigned char lead = static_cast<unsigned char>(utf8[i]);
        size_t step = 1;
        if (lead >= 0xC2 && lead <= 0xDF) {
            step = 2;
        } else if (lead >= 0xE0 && lead <= 0xEF) {
            step = 3;
        } else if (lead >= 0xF0 && lead <= 0xF4) {
            step = 4;
        }
        while (step > 1 && i + step - 1 >= utf8.size()) --step;  // cut short
        i += step;
        ++seen;
    }
    return i;
}

} // namespace Haisos::Jq