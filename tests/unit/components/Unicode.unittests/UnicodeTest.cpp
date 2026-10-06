#include <gtest/gtest.h>
#include <cstring>
#include "Unicode.h"
#include "UnicodeTables.h"

using Haisos::Unicode::DecodeStatus;
using Haisos::Unicode::DecodedChar;
using Haisos::Unicode::DecodeUtf8;
using Haisos::Unicode::DisplayWidth;
using Haisos::Unicode::IsNoBreakSpace;
using Haisos::Unicode::IsPrintable;
using Haisos::Unicode::IsSpace;

namespace {

DecodedChar Decode(const char* data) {
    return DecodeUtf8(data, std::strlen(data));
}

DecodedChar Decode(const char* data, size_t size) {
    return DecodeUtf8(data, size);
}

void ExpectInvalid(const char* data, size_t size) {
    DecodedChar decoded = Decode(data, size);
    EXPECT_EQ(decoded.status, DecodeStatus::Invalid);
    EXPECT_EQ(decoded.length, 1u);
}

void ExpectIncomplete(const char* data, size_t size) {
    DecodedChar decoded = Decode(data, size);
    EXPECT_EQ(decoded.status, DecodeStatus::Incomplete);
    EXPECT_EQ(decoded.length, 0u);
}

} // namespace

TEST(UnicodeTest, DecodesAsciiAndEachSequenceLength) {
    DecodedChar decoded = Decode("A");
    EXPECT_EQ(decoded.status, DecodeStatus::Ok);
    EXPECT_EQ(decoded.codePoint, 0x0041u);
    EXPECT_EQ(decoded.length, 1u);

    decoded = Decode("\xC3\xA9");
    EXPECT_EQ(decoded.status, DecodeStatus::Ok);
    EXPECT_EQ(decoded.codePoint, 0x00E9u);
    EXPECT_EQ(decoded.length, 2u);

    decoded = Decode("\xE4\xB8\xAD");
    EXPECT_EQ(decoded.status, DecodeStatus::Ok);
    EXPECT_EQ(decoded.codePoint, 0x4E2Du);
    EXPECT_EQ(decoded.length, 3u);

    decoded = Decode("\xF0\x9F\x98\x80");
    EXPECT_EQ(decoded.status, DecodeStatus::Ok);
    EXPECT_EQ(decoded.codePoint, 0x1F600u);
    EXPECT_EQ(decoded.length, 4u);

    decoded = Decode("\xF4\x8F\xBF\xBF");
    EXPECT_EQ(decoded.status, DecodeStatus::Ok);
    EXPECT_EQ(decoded.codePoint, 0x10FFFFu);
    EXPECT_EQ(decoded.length, 4u);

    // A NUL byte is a whole, valid character.
    decoded = Decode("\0", 1);
    EXPECT_EQ(decoded.status, DecodeStatus::Ok);
    EXPECT_EQ(decoded.codePoint, 0x0000u);
    EXPECT_EQ(decoded.length, 1u);

    // Decoding stops after one character.
    decoded = Decode("ab");
    EXPECT_EQ(decoded.status, DecodeStatus::Ok);
    EXPECT_EQ(decoded.codePoint, 0x0061u);
    EXPECT_EQ(decoded.length, 1u);
}

TEST(UnicodeTest, RejectsInvalidBytesOneAtATime) {
    ExpectInvalid("\x80", 1);                 // stray continuation byte
    ExpectInvalid("\xBF", 1);                 // stray continuation byte
    ExpectInvalid("\xC0\x80", 2);             // C0 starts nothing
    ExpectInvalid("\xC1\xBF", 2);             // C1 starts nothing
    ExpectInvalid("\xE0\x80\x80", 3);         // overlong
    ExpectInvalid("\xED\xA0\x80", 3);         // surrogate
    ExpectInvalid("\xF4\x90\x80\x80", 4);     // above U+10FFFF
    ExpectInvalid("\xF5\x80\x80\x80", 4);     // F5 starts nothing
    ExpectInvalid("\xFF", 1);                 // FF starts nothing
    ExpectInvalid("\xE4\x41", 2);             // cut short by 'A'
}

TEST(UnicodeTest, ReportsATruncatedSequenceAsIncomplete) {
    ExpectIncomplete("\xE2\x82", 2);
    ExpectIncomplete("\xF0", 1);
    ExpectIncomplete("\xF0\x9F\x98", 3);

    // 0x80 is no valid second byte of E0: already not a valid prefix.
    ExpectInvalid("\xE0\x80", 2);
}

TEST(UnicodeTest, PrintableClasses) {
    EXPECT_TRUE(IsPrintable(0x0020));
    EXPECT_TRUE(IsPrintable(0x0041));
    EXPECT_TRUE(IsPrintable(0x00A0));
    EXPECT_TRUE(IsPrintable(0x00E9));
    EXPECT_TRUE(IsPrintable(0x0301));
    EXPECT_TRUE(IsPrintable(0x200B));
    EXPECT_TRUE(IsPrintable(0xE000));
    EXPECT_TRUE(IsPrintable(0x0378));    // unassigned: printable here
    EXPECT_TRUE(IsPrintable(0x1F600));
    EXPECT_TRUE(IsPrintable(0x10FFFD));

    EXPECT_FALSE(IsPrintable(0x0000));
    EXPECT_FALSE(IsPrintable(0x0001));
    EXPECT_FALSE(IsPrintable(0x001F));
    EXPECT_FALSE(IsPrintable(0x007F));
    EXPECT_FALSE(IsPrintable(0x0085));
    EXPECT_FALSE(IsPrintable(0x009F));
    EXPECT_FALSE(IsPrintable(0x2028));
    EXPECT_FALSE(IsPrintable(0x2029));
    EXPECT_FALSE(IsPrintable(0xFDD0));
    EXPECT_FALSE(IsPrintable(0xFFFE));
    EXPECT_FALSE(IsPrintable(0x1FFFF));
    EXPECT_FALSE(IsPrintable(0x10FFFF));
    EXPECT_FALSE(IsPrintable(0xD800));
    EXPECT_FALSE(IsPrintable(0x110000));
}

TEST(UnicodeTest, SpaceClasses) {
    EXPECT_TRUE(IsSpace(0x0009));
    EXPECT_TRUE(IsSpace(0x000A));
    EXPECT_TRUE(IsSpace(0x000B));
    EXPECT_TRUE(IsSpace(0x000C));
    EXPECT_TRUE(IsSpace(0x000D));
    EXPECT_TRUE(IsSpace(0x0020));
    EXPECT_TRUE(IsSpace(0x1680));
    EXPECT_TRUE(IsSpace(0x2000));
    EXPECT_TRUE(IsSpace(0x2006));
    EXPECT_TRUE(IsSpace(0x2008));
    EXPECT_TRUE(IsSpace(0x200A));
    EXPECT_TRUE(IsSpace(0x205F));
    EXPECT_TRUE(IsSpace(0x3000));

    EXPECT_FALSE(IsSpace(0x00A0));
    EXPECT_FALSE(IsSpace(0x2007));
    EXPECT_FALSE(IsSpace(0x202F));
    EXPECT_FALSE(IsSpace(0x2060));
    EXPECT_FALSE(IsSpace(0x200B));
    EXPECT_FALSE(IsSpace(0x0041));

    EXPECT_TRUE(IsNoBreakSpace(0x00A0));
    EXPECT_TRUE(IsNoBreakSpace(0x2007));
    EXPECT_TRUE(IsNoBreakSpace(0x202F));
    EXPECT_TRUE(IsNoBreakSpace(0x2060));
    EXPECT_FALSE(IsNoBreakSpace(0x0020));
    EXPECT_FALSE(IsNoBreakSpace(0x3000));
    EXPECT_FALSE(IsNoBreakSpace(0x0009));
}

TEST(UnicodeTest, DisplayWidths) {
    EXPECT_EQ(DisplayWidth(0x0001), -1);
    EXPECT_EQ(DisplayWidth(0x007F), -1);
    EXPECT_EQ(DisplayWidth(0x2028), -1);

    EXPECT_EQ(DisplayWidth(0x0301), 0);
    EXPECT_EQ(DisplayWidth(0x200B), 0);
    EXPECT_EQ(DisplayWidth(0x2060), 0);
    EXPECT_EQ(DisplayWidth(0xFEFF), 0);
    EXPECT_EQ(DisplayWidth(0xFE0F), 0);
    EXPECT_EQ(DisplayWidth(0xE0001), 0);
    EXPECT_EQ(DisplayWidth(0x1160), 0);
    EXPECT_EQ(DisplayWidth(0x302A), 0);

    EXPECT_EQ(DisplayWidth(0x0041), 1);
    EXPECT_EQ(DisplayWidth(0x00E9), 1);
    EXPECT_EQ(DisplayWidth(0x00AD), 1);
    EXPECT_EQ(DisplayWidth(0x00A0), 1);
    EXPECT_EQ(DisplayWidth(0x0600), 1);
    EXPECT_EQ(DisplayWidth(0xE000), 1);
    EXPECT_EQ(DisplayWidth(0x2003), 1);

    EXPECT_EQ(DisplayWidth(0x4E2D), 2);
    EXPECT_EQ(DisplayWidth(0xAC00), 2);
    EXPECT_EQ(DisplayWidth(0xFF21), 2);
    EXPECT_EQ(DisplayWidth(0x3000), 2);
    EXPECT_EQ(DisplayWidth(0x1F600), 2);
    EXPECT_EQ(DisplayWidth(0x1FA70), 2);
    EXPECT_EQ(DisplayWidth(0x231A), 2);
    EXPECT_EQ(DisplayWidth(0x20000), 2);
}

TEST(UnicodeTest, EveryTableIsSortedAndDisjoint) {
    using Haisos::Unicode::Tables::CodePointRange;

    auto checkTable = [](const CodePointRange* ranges, size_t count) {
        char32_t previousLast = 0;
        for (size_t i = 0; i < count; ++i) {
            EXPECT_LE(ranges[i].first, ranges[i].last) << i;
            if (i > 0)
                EXPECT_GT(ranges[i].first, previousLast) << i;
            previousLast = ranges[i].last;
        }
    };
    checkTable(Haisos::Unicode::Tables::kZeroWidth,
               Haisos::Unicode::Tables::kZeroWidthCount);
    checkTable(Haisos::Unicode::Tables::kWide,
               Haisos::Unicode::Tables::kWideCount);
}
