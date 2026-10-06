#include "Unicode.h"

#include "UnicodeTables.h"

namespace Haisos::Unicode {

namespace {

inline unsigned char ByteAt(const char* data, size_t i) {
    return static_cast<unsigned char>(data[i]);
}

} // namespace

DecodedChar DecodeUtf8(const char* data, size_t size) {
    // The caller guarantees size >= 1.
    const unsigned char lead = ByteAt(data, 0);
    if (lead <= 0x7F)
        return {DecodeStatus::Ok, lead, 1};

    // The sequence's total length and the accepted span of its second byte
    // (the first byte alone fixes both, for strict RFC 3629 UTF-8: the
    // narrowed spans of E0/ED/F0/F4 are what refuse overlong forms,
    // surrogates and code points above U+10FFFF).
    size_t length;
    unsigned char secondMin, secondMax;
    char32_t codePoint;
    if (lead >= 0xC2 && lead <= 0xDF) {
        length = 2; secondMin = 0x80; secondMax = 0xBF;
        codePoint = lead & 0x1F;
    } else if (lead == 0xE0) {
        length = 3; secondMin = 0xA0; secondMax = 0xBF;
        codePoint = lead & 0x0F;
    } else if ((lead >= 0xE1 && lead <= 0xEC) || lead == 0xEE || lead == 0xEF) {
        length = 3; secondMin = 0x80; secondMax = 0xBF;
        codePoint = lead & 0x0F;
    } else if (lead == 0xED) {
        length = 3; secondMin = 0x80; secondMax = 0x9F;
        codePoint = lead & 0x0F;
    } else if (lead == 0xF0) {
        length = 4; secondMin = 0x90; secondMax = 0xBF;
        codePoint = lead & 0x07;
    } else if (lead >= 0xF1 && lead <= 0xF3) {
        length = 4; secondMin = 0x80; secondMax = 0xBF;
        codePoint = lead & 0x07;
    } else if (lead == 0xF4) {
        length = 4; secondMin = 0x80; secondMax = 0x8F;
        codePoint = lead & 0x07;
    } else {
        // 80-BF (a stray continuation byte), C0, C1, F5-FF.
        return {DecodeStatus::Invalid, 0, 1};
    }

    for (size_t i = 1; i < length; ++i) {
        if (i >= size)
            // Every byte so far was a valid prefix; the data just ends.
            return {DecodeStatus::Incomplete, 0, 0};
        const unsigned char b = ByteAt(data, i);
        const unsigned char min = (i == 1) ? secondMin : 0x80;
        const unsigned char max = (i == 1) ? secondMax : 0xBF;
        if (b < min || b > max)
            // Cut short by a byte that cannot continue it: the caller skips
            // the lead byte alone and decodes again from the next.
            return {DecodeStatus::Invalid, 0, 1};
        codePoint = (codePoint << 6) | (b & 0x3F);
    }
    return {DecodeStatus::Ok, codePoint, length};
}

bool IsPrintable(char32_t c) {
    if (c > kMaxCodePoint)
        return false;
    if (c <= 0x1F)                          // C0 controls
        return false;
    if (c >= 0x7F && c <= 0x9F)             // DEL and C1 controls
        return false;
    if (c == 0x2028 || c == 0x2029)         // line and paragraph separators
        return false;
    if (c >= 0xFDD0 && c <= 0xFDEF)         // noncharacters
        return false;
    if (c >= 0xD800 && c <= 0xDFFF)         // surrogates (never encoded)
        return false;
    if ((c & 0xFFFE) == 0xFFFE)             // U+xxFFFE/U+xxFFFF of every plane
        return false;
    // Unassigned code points count as printable (glibc says not).
    return true;
}

bool IsSpace(char32_t c) {
    if (c >= 0x09 && c <= 0x0D)
        return true;
    if (c == 0x20)
        return true;
    if (c == 0x1680)
        return true;
    if (c >= 0x2000 && c <= 0x2006)
        return true;
    if (c >= 0x2008 && c <= 0x200A)
        return true;
    if (c == 0x2028 || c == 0x2029)
        return true;
    return c == 0x205F || c == 0x3000;
}

bool IsNoBreakSpace(char32_t c) {
    return c == 0x00A0 || c == 0x2007 || c == 0x202F || c == 0x2060;
}

int DisplayWidth(char32_t c) {
    if (!IsPrintable(c))
        return -1;
    if (Tables::InRanges(Tables::kZeroWidth, Tables::kZeroWidthCount, c))
        return 0;
    if (Tables::InRanges(Tables::kWide, Tables::kWideCount, c))
        return 2;
    return 1;
}

} // namespace Haisos::Unicode
