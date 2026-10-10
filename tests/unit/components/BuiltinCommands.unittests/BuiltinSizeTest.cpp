#include <gtest/gtest.h>
#include <cstdint>
#include "BuiltinSize.h"

namespace Haisos {

TEST(BuiltinSizeTest, ParseSizeWithSuffixFollowsXstrtoumax) {
    constexpr std::string_view suffixes = "bkKmMGTPEZYRQ0";
    auto parse = [&](std::string_view text, uintmax_t& out) {
        return ParseSizeWithSuffix(text, suffixes, out);
    };
    uintmax_t value = 0;
    EXPECT_EQ(parse("10", value), SizeParse::Ok);
    EXPECT_EQ(value, 10);
    EXPECT_EQ(parse("1k", value), SizeParse::Ok);
    EXPECT_EQ(value, 1024);
    // "B" after the letter makes a power of 1000 of it, "iB" keeps 1024.
    EXPECT_EQ(parse("1kB", value), SizeParse::Ok);
    EXPECT_EQ(value, 1000);
    EXPECT_EQ(parse("1KiB", value), SizeParse::Ok);
    EXPECT_EQ(value, 1024);
    EXPECT_EQ(parse("2M", value), SizeParse::Ok);
    EXPECT_EQ(value, 2097152);
    EXPECT_EQ(parse("1b", value), SizeParse::Ok);
    EXPECT_EQ(value, 512);
    // No digits: the value is 1, so a bare suffix is its own multiplier.
    EXPECT_EQ(parse("K", value), SizeParse::Ok);
    EXPECT_EQ(value, 1024);
    // "w" is not among the valid suffixes here, so it is a leftover.
    EXPECT_EQ(parse("1w", value), SizeParse::InvalidSuffix);
    EXPECT_EQ(parse("x", value), SizeParse::Invalid);
    EXPECT_EQ(parse("-1", value), SizeParse::Invalid);
    EXPECT_EQ(parse("99999999999999999999", value), SizeParse::Overflow);
    EXPECT_EQ(value, ~uintmax_t{0});
}

TEST(BuiltinSizeTest, FormatHumanSizeRoundsUp) {
    EXPECT_EQ(FormatHumanSize(0, false), "0");
    EXPECT_EQ(FormatHumanSize(1023, false), "1023");
    EXPECT_EQ(FormatHumanSize(1024, false), "1.0K");
    EXPECT_EQ(FormatHumanSize(8192, false), "8.0K");
    EXPECT_EQ(FormatHumanSize(20480, false), "20K");
    EXPECT_EQ(FormatHumanSize(1536 * 1024, false), "1.5M");
    EXPECT_EQ(FormatHumanSize(8192, true), "8.2k");
    EXPECT_EQ(FormatHumanSize(20480, true), "21k");
}

} // namespace Haisos