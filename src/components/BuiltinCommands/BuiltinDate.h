#pragma once
#include <cstdint>
#include <ctime>
#include <optional>
#include <string_view>
#include "interfaces/IFileSystemService.h"

namespace Haisos {

// The date and time parsers and the formatter the time-taking builtins share
// (contract 4, shared with the `date` and `ls` builtins). All work on bytes,
// as GNU's do in the C locale.

// strftime as GNU date has it (gnulib nstrftime in the C locale): |t| in the
// host's local zone, or in UTC when |utc|. What a conversion cannot print it
// writes out as it stands, and so does a conversion it does not know
// (`%5J` is `  %5J`), as GNU's does. Never fails.
std::string FormatDateTime(std::string_view format, FileDateTime t, bool utc);

// GNU date -d / touch -d, this subset: items separated by whitespace, in any
// order -- a date (YYYY-MM-DD, a `T`-joined time allowed), a time of day
// (HH:MM[:SS[.frac]]), a zone (Z/UTC/UT/GMT or +hh[mm]/+hh:mm), relative
// items ([N] unit [ago], the words last/this/next for N and
// now/today/yesterday/tomorrow), each of date, time and zone at most once,
// and `@` seconds (1700000000, -1.5) alone. A signed number after a time of
// day is always a zone, never a relative number, as GNU's rule has it. Empty
// |text| (or only spaces) is midnight today, as GNU's `date -d ''` prints it.
// |now| is what relative items and "now" start from (touch -r passes the
// reference file's time instead). Everything zone-relative is taken in the
// host's local zone, or in UTC when |utc| (date -u; a zone given in |text|
// wins either way). False if |text| is not understood.
bool ParseDateString(std::string_view text, FileDateTime now, bool utc, FileDateTime& out);

// touch -t [[CC]YY]MMDDhhmm[.ss], in the host's local zone, or in UTC when
// |utc| (date's set-time operand): 8 digits MMDDhhmm (the year from |now|),
// 10 YYMMDDhhmm (YY 69-99 is 19YY, 00-68 20YY), 12 CCYYMMDDhhmm; the day is
// checked against the month. False on anything else.
bool ParseTouchStamp(std::string_view text, FileDateTime now, bool utc, FileDateTime& out);

// The zone-relative forms, in the host's local zone.
inline bool ParseDateString(std::string_view text, FileDateTime now, FileDateTime& out) {
    return ParseDateString(text, now, false, out);
}
inline bool ParseTouchStamp(std::string_view text, FileDateTime now, FileDateTime& out) {
    return ParseTouchStamp(text, now, false, out);
}

// The calendar date and time of |seconds| in the host's time zone (what
// Ls.cpp's LocalTime was; a copy for the date and stat builtins).
std::tm LocalTimeOf(int64_t seconds);

// LocalTimeOf's counterpart: the date and time of |seconds| in UTC (gmtime,
// which Windows too has, unlike timegm).
std::tm UtcTimeOf(int64_t seconds);

// std::mktime on a copy of |local| with tm_isdst = -1 (fields may be out of
// range: mktime normalizes them, so Jan 31 + 1 month is Mar 2 or 3).
// nullopt when mktime fails.
std::optional<int64_t> SecondsFromLocalTime(const std::tm& local);

// Seconds since the epoch of a UTC civil time. Month 1-12 and day may be out
// of range (normalized arithmetically: Feb 30 is Mar 1 or 2). Howard
// Hinnant's days_from_civil -- no timegm, which is POSIX-only.
int64_t SecondsFromUtc(int64_t year, int64_t month, int64_t day,
                       int hour, int minute, int second);

} // namespace Haisos