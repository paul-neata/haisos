#include <gtest/gtest.h>
#include <cstdint>
#include <ctime>
#include <string>
#include "BuiltinDate.h"

using namespace Haisos;

namespace {

// A local calendar date and time as a std::tm, for SecondsFromLocalTime.
std::tm LocalOf(int year, int month, int day, int hour, int minute, int second) {
    std::tm local{};
    local.tm_year = year - 1900;
    local.tm_mon = month - 1;
    local.tm_mday = day;
    local.tm_hour = hour;
    local.tm_min = minute;
    local.tm_sec = second;
    return local;
}

} // namespace

// --- BuiltinDate: ParseDateString, zone- and @-qualified forms ---

TEST(BuiltinCommandsDateTest, ParsesUtcAndOffsets) {
    const FileDateTime now{1700000000, 0};

    FileDateTime out;
    ASSERT_TRUE(ParseDateString("2024-01-02T03:04:05Z", now, out));
    EXPECT_EQ(out, (FileDateTime{1704164645, 0}));

    ASSERT_TRUE(ParseDateString("2024-01-02 03:04:05.123456789 +0100", now, out));
    EXPECT_EQ(out, (FileDateTime{1704161045, 123456789}));

    ASSERT_TRUE(ParseDateString("@1700000000.5", now, out));
    EXPECT_EQ(out, (FileDateTime{1700000000, 500000000}));
}

TEST(BuiltinCommandsDateTest, ZoneAfterTimeQuirk) {
    // A signed number directly after a time of day is a zone, never a
    // relative number -- GNU's rule: 03:04 at UTC+2, plus one hour.
    const FileDateTime now{1700000000, 0};
    FileDateTime out;
    ASSERT_TRUE(ParseDateString("2024-01-02 03:04 +2 hours", now, out));
    EXPECT_EQ(out, (FileDateTime{1704161040, 0}));
}

TEST(BuiltinCommandsDateTest, ZoneOnlyDirectlyAfterTime) {
    const FileDateTime now{1700000000, 0};
    FileDateTime out;

    // "tomorrow" breaks the time/number adjacency, so -1 is a relative item:
    // 2024-01-03 02:04 UTC.
    ASSERT_TRUE(ParseDateString("2024-01-02 03:04 Z tomorrow -1 hour", now, out));
    EXPECT_EQ(out, (FileDateTime{1704247440, 0}));

    // Directly after the time, it still is a zone.
    ASSERT_TRUE(ParseDateString("2024-01-02 03:04 +2 hours", now, out));
    EXPECT_EQ(out, (FileDateTime{1704161040, 0}));
}

TEST(BuiltinCommandsDateTest, LocalDatesAndRelativeItems) {
    // The host's time zone is unknown, so the expected values are computed
    // with SecondsFromLocalTime, as the parser does.
    const FileDateTime now{1700000000, 123};
    const std::tm nowLocal = LocalTimeOf(now.seconds);
    const auto nowSeconds = SecondsFromLocalTime(nowLocal);
    ASSERT_TRUE(nowSeconds.has_value());

    FileDateTime out;
    ASSERT_TRUE(ParseDateString("2024-01-02 3 days ago", now, out));
    EXPECT_EQ(out, (FileDateTime{*SecondsFromLocalTime(LocalOf(2023, 12, 30, 0, 0, 0)), 0}));

    // mktime normalizes Jan 31 + 1 month: Feb 31 is Mar 2 (2024 is a leap
    // year, so February has 29 days).
    ASSERT_TRUE(ParseDateString("2024-01-31 +1 month", now, out));
    EXPECT_EQ(out, (FileDateTime{*SecondsFromLocalTime(LocalOf(2024, 3, 2, 0, 0, 0)), 0}));

    ASSERT_TRUE(ParseDateString("1 hour ago", now, out));
    EXPECT_EQ(out, (FileDateTime{*nowSeconds - 3600, now.nanoseconds}));

    std::tm local = nowLocal;
    local.tm_mday -= 1;
    ASSERT_TRUE(ParseDateString("yesterday", now, out));
    EXPECT_EQ(out, (FileDateTime{*SecondsFromLocalTime(local), now.nanoseconds}));

    local = nowLocal;
    local.tm_mday += 1;
    ASSERT_TRUE(ParseDateString("tomorrow", now, out));
    EXPECT_EQ(out, (FileDateTime{*SecondsFromLocalTime(local), now.nanoseconds}));

    local = nowLocal;
    local.tm_mday += 7;
    ASSERT_TRUE(ParseDateString("next week", now, out));
    EXPECT_EQ(out, (FileDateTime{*SecondsFromLocalTime(local), now.nanoseconds}));

    local = nowLocal;
    local.tm_mday += 28;
    ASSERT_TRUE(ParseDateString("2 fortnights", now, out));
    EXPECT_EQ(out, (FileDateTime{*SecondsFromLocalTime(local), now.nanoseconds}));

    ASSERT_TRUE(ParseDateString("+ 5 minutes", now, out));
    EXPECT_EQ(out, (FileDateTime{*nowSeconds + 300, now.nanoseconds}));
}

TEST(BuiltinCommandsDateTest, RejectsWhatItDoesNotKnow) {
    const FileDateTime now{1700000000, 0};
    FileDateTime out;
    EXPECT_FALSE(ParseDateString("garbage", now, out));
    EXPECT_FALSE(ParseDateString("2024-13-01", now, out));
    EXPECT_FALSE(ParseDateString("2024-02-30", now, out));
    EXPECT_FALSE(ParseDateString("25:00", now, out));
    EXPECT_FALSE(ParseDateString("@1 2024-01-01", now, out));
    EXPECT_FALSE(ParseDateString("Jan 2 2024", now, out));
}

TEST(BuiltinCommandsDateTest, TouchStamps) {
    const FileDateTime now{1700000000, 0};
    const int nowYear = LocalTimeOf(now.seconds).tm_year + 1900;

    FileDateTime out;
    ASSERT_TRUE(ParseTouchStamp("202401020304.05", now, out));
    EXPECT_EQ(out, (FileDateTime{*SecondsFromLocalTime(LocalOf(2024, 1, 2, 3, 4, 5)), 0}));

    ASSERT_TRUE(ParseTouchStamp("2401020304", now, out));
    EXPECT_EQ(out, (FileDateTime{*SecondsFromLocalTime(LocalOf(2024, 1, 2, 3, 4, 0)), 0}));

    ASSERT_TRUE(ParseTouchStamp("6901020304", now, out));
    EXPECT_EQ(out, (FileDateTime{*SecondsFromLocalTime(LocalOf(1969, 1, 2, 3, 4, 0)), 0}));

    ASSERT_TRUE(ParseTouchStamp("01020304", now, out));
    EXPECT_EQ(out, (FileDateTime{*SecondsFromLocalTime(LocalOf(nowYear, 1, 2, 3, 4, 0)), 0}));

    EXPECT_FALSE(ParseTouchStamp("2024", now, out));
    EXPECT_FALSE(ParseTouchStamp("202402300000", now, out));
}

TEST(BuiltinCommandsDateTest, SecondsFromUtcNormalizes) {
    EXPECT_EQ(SecondsFromUtc(2024, 2, 30, 0, 0, 0), SecondsFromUtc(2024, 3, 1, 0, 0, 0));
    EXPECT_EQ(SecondsFromUtc(1970, 1, 1, 0, 0, 0), 0);
    EXPECT_EQ(SecondsFromUtc(2024, 1, 2, 3, 4, 5), 1704164645);
}