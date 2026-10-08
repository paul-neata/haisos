#include <gtest/gtest.h>
#include <cstdint>
#include <cstdlib>
#include <regex>
#include <string>
#include <vector>
#include "BuiltinCommandsFixture.h"
#include "BuiltinDate.h"

using namespace Haisos;

namespace {

const std::string kNone;
const std::string kTryDate = "Try 'date --help' for more information.\n";

} // namespace

// --- FormatDateTime, the formatter behind date and ls --time-style ---

TEST(FormatDateTimeTest, MatchesGnu) {
    // The formats ls --time-style=full-iso and date -Iseconds build on.
    EXPECT_EQ(FormatDateTime("%Y-%m-%d %H:%M:%S.%N %:z", FileDateTime{1700000000, 5}, true),
              "2023-11-14 22:13:20.000000005 +00:00");
    // The ISO week of a Sunday the ISO year does not agree with.
    EXPECT_EQ(FormatDateTime("%G-W%V-%u", FileDateTime{1609632000, 0}, true), "2020-W53-7");
    EXPECT_EQ(FormatDateTime("%U %W", FileDateTime{1609632000, 0}, true), "01 00");
    // A year that does not fill %Y's four digits, and its century.
    EXPECT_EQ(FormatDateTime("%Y|%C|%y", FileDateTime{-59042995200, 0}, true), "0099|00|99");
    // Padding, and the '#' swap of the am/pm name.
    EXPECT_EQ(FormatDateTime("%10a|%010a|%12N|%#p", FileDateTime{1700000000, 123456789}, true),
              "       Tue|0000000Tue|123456789000|pm");
    // Zone-relative, in the host's local zone.
    EXPECT_EQ(FormatDateTime("%s", FileDateTime{1700000000, 0}, false), "1700000000");
    EXPECT_TRUE(std::regex_match(FormatDateTime("%z", FileDateTime{1700000000, 0}, false),
                                 std::regex("[+-][0-9]{4}")));
}

// --- date ---

TEST_F(BuiltinCommandsTest, DateDefaultFormat) {
    // The default format, GNU's in the C locale.
    auto run = RunCaptured("date", {"-u", "-d", "@1700000000"});
    EXPECT_EQ(run.out, "Tue Nov 14 22:13:20 UTC 2023\n");
    EXPECT_EQ(run.err, kNone);
    EXPECT_EQ(run.status, 0);

    // Before the epoch, with the fraction folded in.
    run = RunCaptured("date", {"-u", "-d", "@-1"});
    EXPECT_EQ(run.out, "Wed Dec 31 23:59:59 UTC 1969\n");
    EXPECT_EQ(run.err, kNone);
    EXPECT_EQ(run.status, 0);

    // An empty -d string is midnight today, as GNU's is.
    run = RunCaptured("date", {"-u", "-d", ""});
    const std::tm now = UtcTimeOf(CurrentFileDateTime().seconds);
    const int64_t midnight = SecondsFromUtc(now.tm_year + 1900, now.tm_mon + 1, now.tm_mday, 0, 0, 0);
    EXPECT_EQ(run.out, FormatDateTime("%a %b %e %H:%M:%S %Z %Y", FileDateTime{midnight, 0}, true) + "\n");
    EXPECT_EQ(run.err, kNone);
    EXPECT_EQ(run.status, 0);
}

TEST_F(BuiltinCommandsTest, DateFormatConversions) {
    // Every conversion FormatDateTime has, byte for byte as GNU prints it.
    const auto run = RunCaptured("date", {"-u", "-d", "@1700000000.123456789",
        "+%q|%P|%p|%c|%x|%X|%r|%D|%T|%R|%h|%C|%g|%G|%V|%U|%W|%j|%u|%w|%k|%l|%I|"
        "%s|%N|%3N|%-N|%:z|%::z|%:::z|%z|%Z|%^a|%#Z|%#a|%^B|%10Y|%-d|%_m|%05e|"
        "%-H|%_5S|%Ey|%Od"});
    EXPECT_EQ(run.out,
        "4|pm|PM|Tue Nov 14 22:13:20 2023|11/14/23|22:13:20|10:13:20 PM|11/14/23|"
        "22:13:20|22:13|Nov|20|23|2023|46|46|46|318|2|2|22|10|10|1700000000|"
        "123456789|123|123456789|+00:00|+00:00:00|+00|+0000|UTC|TUE|utc|TUE|"
        "NOVEMBER|0000002023|14|11|00014|22|   20|23|14\n");
    EXPECT_EQ(run.err, kNone);
    EXPECT_EQ(run.status, 0);
}

TEST_F(BuiltinCommandsTest, DateUnknownConversionsAreWrittenOut) {
    // What the formatter does not know it writes out as it stands, as GNU's
    // does; a trailing '%' is a '%' on its own.
    const auto run = RunCaptured("date", {"-u", "-d", "@0", "+%J|%-J|%5J|%:x|%t|%n|%%|%"});
    EXPECT_EQ(run.out, "%J|%-J|  %5J|%:x|\t|\n|%|%\n");
    EXPECT_EQ(run.err, kNone);
    EXPECT_EQ(run.status, 0);
}

TEST_F(BuiltinCommandsTest, DateIsoAndRfcFormats) {
    const auto with = [&](const std::string& option) {
        return RunCaptured("date", {"-u", "-d", "@1700000000.5", option});
    };
    EXPECT_EQ(with("-I").out, "2023-11-14\n");
    EXPECT_EQ(with("-I").err, kNone);
    EXPECT_EQ(with("-I").status, 0);
    EXPECT_EQ(with("-Ihours").out, "2023-11-14T22+00:00\n");
    EXPECT_EQ(with("-Iminutes").out, "2023-11-14T22:13+00:00\n");
    EXPECT_EQ(with("-Iseconds").out, "2023-11-14T22:13:20+00:00\n");
    EXPECT_EQ(with("-Ins").out, "2023-11-14T22:13:20,500000000+00:00\n");
    EXPECT_EQ(with("--rfc-3339=date").out, "2023-11-14\n");
    EXPECT_EQ(with("--rfc-3339=seconds").out, "2023-11-14 22:13:20+00:00\n");
    EXPECT_EQ(with("--rfc-3339=ns").out, "2023-11-14 22:13:20.500000000+00:00\n");
    EXPECT_EQ(with("-R").out, "Tue, 14 Nov 2023 22:13:20 +0000\n");
}

TEST_F(BuiltinCommandsTest, DateDateStrings) {
    // -d takes what touch -d takes.
    auto run = RunCaptured("date", {"-u", "-d", "2020-01-01 12:30"});
    EXPECT_EQ(run.out, "Wed Jan  1 12:30:00 UTC 2020\n");
    EXPECT_EQ(run.err, kNone);
    EXPECT_EQ(run.status, 0);

    run = RunCaptured("date", {"-u", "-d", "2020-01-01 +1 day", "+%F"});
    EXPECT_EQ(run.out, "2020-01-02\n");

    // '@' seconds are UTC, whatever the zone would have been.
    run = RunCaptured("date", {"-u", "-d", "@86400", "+%F"});
    EXPECT_EQ(run.out, "1970-01-02\n");

    // A zone attached to a `T`-joined time does not stop relative items
    // behind it from counting (the #59 fix).
    run = RunCaptured("date", {"-u", "-d", "2024-01-02T03:04Z +1 hour", "+%F %T"});
    EXPECT_EQ(run.out, "2024-01-02 04:04:00\n");

    // An invalid string is GNU's wording, the string quoted.
    run = RunCaptured("date", {"-u", "-d", "garbage"});
    EXPECT_EQ(run.out, kNone);
    EXPECT_EQ(run.err, "date: invalid date 'garbage'\n");
    EXPECT_EQ(run.status, 1);
}

TEST_F(BuiltinCommandsTest, DateFromAFile) {
    // -f: one date per line, parsed and printed in turn; a line that does not
    // parse is reported and the file goes on, with a failing exit status.
    auto run = RunCaptured("date", {"-u", "-f", "-"}, "\n@0\nfoo\n@86400\n");
    const Lines lines = SplitLines(run.out);
    ASSERT_EQ(lines.size(), 3u);
    // The empty first line is midnight today, in UTC.
    const int year = UtcTimeOf(CurrentFileDateTime().seconds).tm_year + 1900;
    const std::string tail = "00:00:00 UTC " + std::to_string(year);
    ASSERT_GE(lines[0].size(), tail.size());
    EXPECT_EQ(lines[0].compare(lines[0].size() - tail.size(), tail.size(), tail), 0) << lines[0];
    EXPECT_EQ(lines[1], "Thu Jan  1 00:00:00 UTC 1970");
    EXPECT_EQ(lines[2], "Fri Jan  2 00:00:00 UTC 1970");
    EXPECT_EQ(run.err, "date: invalid date 'foo'\n");
    EXPECT_EQ(run.status, 1);

    // A file that is not there is GNU's wording.
    run = RunCaptured("date", {"-u", "-f", "/nonexist"});
    EXPECT_EQ(run.out, kNone);
    EXPECT_EQ(run.err, "date: /nonexist: No such file or directory\n");
    EXPECT_EQ(run.status, 1);
}

TEST_F(BuiltinCommandsTest, DateReference) {
    // -r prints the file's modification time.
    FileStatus status;
    ASSERT_EQ(root->Stat("/notes.txt", status), 0);
    auto run = RunCaptured("date", {"-u", "-r", "/notes.txt", "+%s"});
    EXPECT_EQ(run.out, std::to_string(status.modificationTime.seconds) + "\n");
    EXPECT_EQ(run.err, kNone);
    EXPECT_EQ(run.status, 0);

    run = RunCaptured("date", {"-u", "-r", "/nonexist"});
    EXPECT_EQ(run.out, kNone);
    EXPECT_EQ(run.err, "date: /nonexist: No such file or directory\n");
    EXPECT_EQ(run.status, 1);
}

TEST_F(BuiltinCommandsTest, DateSetIsRefused) {
    // -s: HaisosOS has no clock to set, so the time is printed and setting it
    // refused, as GNU's unprivileged date is.
    auto run = RunCaptured("date", {"-u", "-s", "2020-01-01"});
    EXPECT_EQ(run.out, "Wed Jan  1 00:00:00 UTC 2020\n");
    EXPECT_EQ(run.err, "date: cannot set date: Operation not permitted\n");
    EXPECT_EQ(run.status, 1);

    // The MMDDhhmm[[CC]YY][.ss] operand, its year last.
    run = RunCaptured("date", {"-u", "0102030426"});
    EXPECT_EQ(run.out, "Fri Jan  2 03:04:00 UTC 2026\n");
    EXPECT_EQ(run.err, "date: cannot set date: Operation not permitted\n");
    EXPECT_EQ(run.status, 1);

    // An operand that is neither +FORMAT nor a stamp.
    run = RunCaptured("date", {"-u", "x"});
    EXPECT_EQ(run.out, kNone);
    EXPECT_EQ(run.err, "date: invalid date 'x'\n");
    EXPECT_EQ(run.status, 1);
}

TEST_F(BuiltinCommandsTest, DateUsageErrors) {
    // Two ways of giving the time to print.
    auto run = RunCaptured("date", {"-d", "1", "-r", "/"});
    EXPECT_EQ(run.out, kNone);
    EXPECT_EQ(run.err, "date: the options to specify dates for printing are mutually exclusive\n" + kTryDate);
    EXPECT_EQ(run.status, 1);

    // Printing and setting do not mix either.
    run = RunCaptured("date", {"-d", "1", "-s", "@0"});
    EXPECT_EQ(run.err, "date: the options to print and set the time may not be used together\n" + kTryDate);
    EXPECT_EQ(run.status, 1);

    // Two output formats; no "Try" line after this one.
    run = RunCaptured("date", {"-R", "-I"});
    EXPECT_EQ(run.out, kNone);
    EXPECT_EQ(run.err, "date: multiple output formats specified\n");
    EXPECT_EQ(run.status, 1);

    // More than one operand.
    run = RunCaptured("date", {"+%Y", "+%m"});
    EXPECT_EQ(run.err, "date: extra operand '+%m'\n" + kTryDate);
    EXPECT_EQ(run.status, 1);

    // An operand that is not a format, with a date option in play.
    run = RunCaptured("date", {"-d", "@0", "x"});
    EXPECT_EQ(run.err,
        "date: the argument 'x' lacks a leading '+';\n"
        "when using an option to specify date(s), any non-option\n"
        "argument must be a format string beginning with '+'\n" + kTryDate);
    EXPECT_EQ(run.status, 1);

    // A word -I does not take.
    run = RunCaptured("date", {"-Ix"});
    EXPECT_EQ(run.out, kNone);
    EXPECT_EQ(run.err,
        "date: invalid argument 'x' for '--iso-8601'\n"
        "Valid arguments are:\n"
        "  - 'hours'\n"
        "  - 'minutes'\n"
        "  - 'date'\n"
        "  - 'seconds'\n"
        "  - 'ns'\n" + kTryDate);
    EXPECT_EQ(run.status, 1);

    // --resolution: HaisosOS's clock reaches the nanosecond.
    run = RunCaptured("date", {"--resolution"});
    EXPECT_EQ(run.out, "0.000000001\n");
    EXPECT_EQ(run.err, kNone);
    EXPECT_EQ(run.status, 0);

    // --resolution is one more way of giving the time to print.
    run = RunCaptured("date", {"--resolution", "-d", "@0"});
    EXPECT_EQ(run.err, "date: the options to specify dates for printing are mutually exclusive\n" + kTryDate);
    EXPECT_EQ(run.status, 1);
}

TEST_F(BuiltinCommandsTest, DateNowIsNow) {
    // Without a source, the time printed is the test's own clock's.
    const FileDateTime before = CurrentFileDateTime();
    const auto run = RunCaptured("date", {"+%s"});
    const FileDateTime after = CurrentFileDateTime();
    const int64_t printed = std::strtoll(run.out.c_str(), nullptr, 10);
    EXPECT_GE(printed, before.seconds - 1);
    EXPECT_LE(printed, after.seconds + 5);
    EXPECT_EQ(run.err, kNone);
    EXPECT_EQ(run.status, 0);
}