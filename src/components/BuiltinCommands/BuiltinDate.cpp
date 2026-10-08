#include "BuiltinDate.h"
#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <string>
#include <vector>
#include "src/components/libheaders/CrtInvalidParameterAsError.h"

namespace Haisos {

namespace {

// Whitespace, as GNU's parser separates items on it.
bool IsSpace(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

std::string Lowercased(std::string_view text) {
    std::string lower(text);
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return lower;
}

bool AllDigits(std::string_view text) {
    if (text.empty()) {
        return false;
    }
    for (char c : text) {
        if (c < '0' || c > '9') {
            return false;
        }
    }
    return true;
}

// The digits of |text| as a number, or false when it does not fit an int64.
bool DigitsTo(std::string_view digits, int64_t& out) {
    if (digits.empty() || digits.size() > 18) {
        return false;
    }
    errno = 0;
    char* end = nullptr;
    const long long value = std::strtoll(std::string(digits).c_str(), &end, 10);
    if (errno == ERANGE || end == nullptr || *end != '\0') {
        return false;
    }
    out = value;
    return true;
}

// floor division, what a month out of range is folded into the year with.
int64_t FloorDiv(int64_t a, int64_t b) {
    const int64_t quotient = a / b;
    return (a % b != 0 && ((a < 0) != (b < 0))) ? quotient - 1 : quotient;
}

// The length of |month| of |year|, leap years counted.
int64_t DaysInMonth(int64_t year, int64_t month) {
    static const int64_t kDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month == 2 && (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0))) {
        return 29;
    }
    return kDays[month - 1];
}

// The fraction of a second of |digits|: up to 9 digits kept (more truncated),
// as nanoseconds.
uint32_t FractionToNanoseconds(std::string_view digits) {
    uint64_t nanoseconds = 0;
    size_t i = 0;
    for (; i < digits.size() && i < 9; ++i) {
        nanoseconds = nanoseconds * 10 + (digits[i] - '0');
    }
    for (; i < 9; ++i) {
        nanoseconds *= 10;
    }
    return static_cast<uint32_t>(nanoseconds);
}

// 1-2 digits of |text| starting at |at|, into |out|; |end| set past them.
bool ParseOneOrTwoDigits(std::string_view text, size_t at, int64_t& out, size_t& end) {
    if (at >= text.size() || text[at] < '0' || text[at] > '9') {
        return false;
    }
    size_t last = at + 1;
    if (last < text.size() && text[last] >= '0' && text[last] <= '9') {
        ++last;
    }
    DigitsTo(text.substr(at, last - at), out);
    end = last;
    return true;
}

// A time of day, HH:MM[:SS[.frac]]: 1-2 digit hour and minute, then an
// optional 1-2 digit second with a fraction. Ranges checked (second 0-60,
// the leap second, as GNU's). |consumed| is set to the bytes of |text| the
// time took, so a caller can take what follows it (an attached zone).
bool ParseTimeOfDay(std::string_view text, int& hour, int& minute, int& second,
                    uint32_t& nanoseconds, size_t& consumed) {
    int64_t h = 0, m = 0, s = 0;
    uint32_t ns = 0;
    size_t i = 0;
    if (!ParseOneOrTwoDigits(text, 0, h, i) || i >= text.size() || text[i] != ':' ||
        !ParseOneOrTwoDigits(text, i + 1, m, i)) {
        return false;
    }
    if (i < text.size() && text[i] == ':') {
        if (!ParseOneOrTwoDigits(text, i + 1, s, i)) {
            return false;
        }
        if (i < text.size() && text[i] == '.') {
            const size_t start = i + 1;
            size_t end = start;
            while (end < text.size() && text[end] >= '0' && text[end] <= '9') {
                ++end;
            }
            if (end == start) {
                return false;
            }
            ns = FractionToNanoseconds(text.substr(start, end - start));
            i = end;
        }
    }
    if (h > 23 || m > 59 || s > 60) {
        return false;
    }
    consumed = i;
    hour = static_cast<int>(h);
    minute = static_cast<int>(m);
    second = static_cast<int>(s);
    nanoseconds = ns;
    return true;
}

// One relative item's unit, an optional final 's' allowed, with what a count
// of it adds up to: 1 years, 2 months, 3 days, 4 seconds (added after the
// conversion, as GNU's hours and minutes are).
struct RelativeUnit {
    int kind;
    int64_t multiplier;
};

std::optional<RelativeUnit> UnitOf(const std::string& word) {
    std::string name = word;
    if (!name.empty() && name.back() == 's') {
        name.pop_back();
    }
    if (name == "year") {
        return RelativeUnit{1, 1};
    }
    if (name == "month") {
        return RelativeUnit{2, 1};
    }
    if (name == "fortnight") {
        return RelativeUnit{3, 14};
    }
    if (name == "week") {
        return RelativeUnit{3, 7};
    }
    if (name == "day") {
        return RelativeUnit{3, 1};
    }
    if (name == "hour") {
        return RelativeUnit{4, 3600};
    }
    if (name == "minute" || name == "min") {
        return RelativeUnit{4, 60};
    }
    if (name == "second" || name == "sec") {
        return RelativeUnit{4, 1};
    }
    return std::nullopt;
}

// What the items of a date string came to.
struct DateItems {
    bool hasDate = false;
    bool hasTime = false;
    bool hasZone = false;
    int64_t zoneOffsetSeconds = 0;   // east of UTC positive, as +0100
    int64_t year = 0, month = 0, day = 0;
    int hour = 0, minute = 0, second = 0;
    uint32_t nanoseconds = 0;
    int64_t relYears = 0, relMonths = 0, relDays = 0, relSeconds = 0;
};

void ApplyRelative(DateItems& items, int64_t count, const RelativeUnit& unit) {
    switch (unit.kind) {
        case 1: items.relYears += count; break;
        case 2: items.relMonths += count; break;
        case 3: items.relDays += count * unit.multiplier; break;
        default: items.relSeconds += count * unit.multiplier; break;
    }
}

// A numeric zone, +hh, +hhmm or +hh:mm (- too), |digits| without the sign:
// minutes east of UTC into |offsetSeconds|. Defined below ParseDate, which
// takes a zone attached to a `T`-joined time.
bool ParseZoneDigits(char sign, std::string_view digits, int64_t& offsetSeconds);

// One date item, YYYY-M-D, with a `T`-joined time of day allowed
// (2024-01-02T03:04:05). Sets |withTime| when the time is there.
bool ParseDate(std::string_view text, DateItems& items, bool& withTime) {
    withTime = false;
    size_t i = 0;
    while (i < text.size() && text[i] >= '0' && text[i] <= '9') {
        ++i;
    }
    if (i == 0 || i + 1 >= text.size() || text[i] != '-') {
        return false;
    }
    int64_t year = 0;
    if (!DigitsTo(text.substr(0, i), year)) {
        return false;
    }
    const size_t monthStart = i + 1;
    size_t j = monthStart;
    while (j < text.size() && text[j] >= '0' && text[j] <= '9') {
        ++j;
    }
    int64_t month = 0;
    if (j - monthStart == 0 || j - monthStart > 2 ||
        !DigitsTo(text.substr(monthStart, j - monthStart), month) ||
        j + 1 >= text.size() || text[j] != '-') {
        return false;
    }
    const size_t dayStart = j + 1;
    size_t k = dayStart;
    while (k < text.size() && text[k] >= '0' && text[k] <= '9') {
        ++k;
    }
    int64_t day = 0;
    if (k - dayStart == 0 || k - dayStart > 2 ||
        !DigitsTo(text.substr(dayStart, k - dayStart), day)) {
        return false;
    }
    if (month < 1 || month > 12 || day < 1 || day > DaysInMonth(year, month)) {
        return false;
    }
    items.year = year;
    items.month = month;
    items.day = day;
    items.hasDate = true;
    if (k < text.size() && (text[k] == 'T' || text[k] == 't')) {
        int hour = 0, minute = 0, second = 0;
        uint32_t nanoseconds = 0;
        size_t consumed = 0;
        const std::string_view rest = text.substr(k + 1);
        if (!ParseTimeOfDay(rest, hour, minute, second, nanoseconds, consumed)) {
            return false;
        }
        // A zone may be attached directly, as ISO 8601 writes it:
        // 2024-01-02T03:04:05Z, 2024-01-02T03:04:05+01:00.
        const std::string_view zone = rest.substr(consumed);
        if (!zone.empty()) {
            if (items.hasZone) {
                return false;
            }
            if (zone.size() == 1 && (zone[0] == 'Z' || zone[0] == 'z')) {
                items.zoneOffsetSeconds = 0;
            } else if ((zone[0] == '+' || zone[0] == '-') &&
                       ParseZoneDigits(zone[0], zone.substr(1), items.zoneOffsetSeconds)) {
                // the offset, sign included, set by ParseZoneDigits
            } else {
                return false;
            }
            items.hasZone = true;
        }
        items.hour = hour;
        items.minute = minute;
        items.second = second;
        items.nanoseconds = nanoseconds;
        items.hasTime = true;
        withTime = true;
    } else if (k != text.size()) {
        return false;
    }
    return true;
}

// A numeric zone, +hh, +hhmm or +hh:mm (- too): |digits| without the sign,
// which is |sign|. Minutes east of UTC.
bool ParseZoneDigits(char sign, std::string_view digits, int64_t& offsetSeconds) {
    int64_t hours = 0, minutes = 0;
    const size_t colon = digits.find(':');
    if (colon != std::string_view::npos) {
        int64_t h = 0, m = 0;
        if (colon == 0 || colon > 2 || digits.size() != colon + 3 ||
            !DigitsTo(digits.substr(0, colon), h) || !DigitsTo(digits.substr(colon + 1), m)) {
            return false;
        }
        hours = h;
        minutes = m;
    } else if (digits.size() <= 2) {
        if (!DigitsTo(digits, hours)) {
            return false;
        }
    } else if (digits.size() <= 4) {
        if (!DigitsTo(digits.substr(0, digits.size() - 2), hours) ||
            !DigitsTo(digits.substr(digits.size() - 2), minutes)) {
            return false;
        }
    } else {
        return false;
    }
    if (minutes > 59) {
        return false;
    }
    offsetSeconds = (sign == '-' ? -1 : 1) * (hours * 3600 + minutes * 60);
    return true;
}

// The items of a date string (its tokens, '@' excluded by the caller): dates,
// times, zones, relative items, in any order. False on anything the grammar
// above does not say.
bool ParseDateItems(const std::vector<std::string>& tokens, DateItems& items) {
    size_t i = 0;
    // A signed number is a zone only DIRECTLY after a time of day (GNU's
    // rule): after any other item it is a relative item's count, so
    // "12:00 tomorrow -1 hour" is noon tomorrow minus one hour.
    bool previousWasTime = false;
    while (i < tokens.size()) {
        const std::string& raw = tokens[i];
        const std::string word = Lowercased(raw);

        // A date (with a T-joined time), a time of day, or a relative item's
        // count.
        if (raw[0] >= '0' && raw[0] <= '9') {
            if (raw.find('-') != std::string::npos) {
                if (items.hasDate) {
                    return false;  // a date may appear once only
                }
                const bool hadTime = items.hasTime;
                bool withTime = false;
                if (!ParseDate(raw, items, withTime) || (withTime && hadTime)) {
                    return false;
                }
                // A zone attached to the time (2024-01-02T03:04Z) closes it:
                // a signed number after it is a relative item again, not a
                // second zone. The time alone leaves it open for one.
                previousWasTime = withTime && !items.hasZone;
                ++i;
                continue;
            }
            if (raw.find(':') != std::string::npos) {
                if (items.hasTime) {
                    return false;
                }
                int hour = 0, minute = 0, second = 0;
                uint32_t nanoseconds = 0;
                size_t consumed = 0;
                if (!ParseTimeOfDay(raw, hour, minute, second, nanoseconds, consumed) ||
                    consumed != raw.size()) {
                    return false;
                }
                items.hour = hour;
                items.minute = minute;
                items.second = second;
                items.nanoseconds = nanoseconds;
                items.hasTime = true;
                previousWasTime = true;
                ++i;
                continue;
            }
            int64_t count = 0;
            if (!DigitsTo(raw, count)) {
                return false;
            }
            ++i;
            if (i >= tokens.size()) {
                return false;
            }
            const auto unit = UnitOf(Lowercased(tokens[i]));
            if (!unit) {
                return false;
            }
            ++i;
            if (i < tokens.size() && Lowercased(tokens[i]) == "ago") {
                count = -count;
                ++i;
            }
            ApplyRelative(items, count, *unit);
            previousWasTime = false;
            continue;
        }

        // A zone word, or a signed number: a zone directly after a time of
        // day (GNU's rule), otherwise a relative item's count with a
        // following unit.
        if (raw[0] == '+' || raw[0] == '-') {
            const char sign = raw[0];
            std::string number;
            if (raw.size() == 1) {
                // Spaces are allowed between the sign and the digits.
                ++i;
                if (i >= tokens.size() || !AllDigits(tokens[i])) {
                    return false;
                }
                number = tokens[i];
            } else {
                number = raw.substr(1);
            }
            if (previousWasTime) {
                if (items.hasZone) {
                    return false;
                }
                int64_t offset = 0;
                if (!ParseZoneDigits(sign, number, offset)) {
                    return false;
                }
                items.zoneOffsetSeconds = offset;
                items.hasZone = true;
                previousWasTime = false;
                ++i;
                continue;
            }
            int64_t count = 0;
            if (!DigitsTo(number, count)) {
                return false;
            }
            ++i;
            if (i >= tokens.size()) {
                return false;
            }
            const auto unit = UnitOf(Lowercased(tokens[i]));
            if (!unit) {
                return false;
            }
            ++i;
            if (i < tokens.size() && Lowercased(tokens[i]) == "ago") {
                count = -count;
                ++i;
            }
            ApplyRelative(items, sign == '-' ? -count : count, *unit);
            previousWasTime = false;
            continue;
        }

        // The zone words, all UTC.
        if (word == "z" || word == "utc" || word == "ut" || word == "gmt") {
            if (items.hasZone) {
                return false;
            }
            items.zoneOffsetSeconds = 0;
            items.hasZone = true;
            previousWasTime = false;
            ++i;
            continue;
        }
        if (word == "now" || word == "today") {
            previousWasTime = false;
            ++i;
            continue;
        }
        if (word == "yesterday") {
            items.relDays -= 1;
            previousWasTime = false;
            ++i;
            continue;
        }
        if (word == "tomorrow") {
            items.relDays += 1;
            previousWasTime = false;
            ++i;
            continue;
        }

        // A relative item: last/this/next, or a bare unit, or "ago".
        int64_t count = 1;
        if (word == "last") {
            count = -1;
            ++i;
        } else if (word == "this") {
            count = 0;
            ++i;
        } else if (word == "next") {
            count = 1;
            ++i;
        } else if (word == "ago") {
            return false;  // nothing before it to negate
        }
        if (i >= tokens.size()) {
            return false;
        }
        const auto unit = UnitOf(Lowercased(tokens[i]));
        if (!unit) {
            return false;
        }
        ++i;
        if (i < tokens.size() && Lowercased(tokens[i]) == "ago") {
            count = -count;
            ++i;
        }
        ApplyRelative(items, count, *unit);
        previousWasTime = false;
    }
    return true;
}

// `@` seconds, |token| the whole item: @[-]N[.frac], an exact point in time
// (UTC, whatever the zone would have been).
bool ParseEpochSeconds(const std::string& token, FileDateTime& out) {
    std::string_view rest = token;
    rest.remove_prefix(1);
    char sign = '+';
    if (!rest.empty() && (rest[0] == '+' || rest[0] == '-')) {
        sign = rest[0];
        rest.remove_prefix(1);
    }
    const size_t dot = rest.find('.');
    std::string_view whole = rest, fraction;
    if (dot != std::string_view::npos) {
        whole = rest.substr(0, dot);
        fraction = rest.substr(dot + 1);
        if (!AllDigits(fraction)) {
            return false;
        }
    }
    int64_t seconds = 0;
    if (!AllDigits(whole) || !DigitsTo(whole, seconds)) {
        return false;
    }
    int64_t nanoseconds = static_cast<int64_t>(FractionToNanoseconds(fraction));
    if (sign == '-') {
        seconds = -seconds;
        if (nanoseconds != 0) {
            seconds -= 1;
            nanoseconds = 1000000000 - nanoseconds;
        }
    }
    out = FileDateTime{seconds, static_cast<uint32_t>(nanoseconds)};
    return true;
}

// |text| split on whitespace.
std::vector<std::string> SplitItems(std::string_view text) {
    std::vector<std::string> tokens;
    size_t i = 0;
    while (i < text.size()) {
        while (i < text.size() && IsSpace(text[i])) {
            ++i;
        }
        const size_t start = i;
        while (i < text.size() && !IsSpace(text[i])) {
            ++i;
        }
        if (i > start) {
            tokens.emplace_back(text.substr(start, i - start));
        }
    }
    return tokens;
}

} // namespace

bool ParseDateString(std::string_view text, FileDateTime now, bool utc, FileDateTime& out) {
    const std::vector<std::string> tokens = SplitItems(text);

    // `@` seconds stand alone: any other item with them is not understood.
    for (const auto& token : tokens) {
        if (!token.empty() && token[0] == '@') {
            return tokens.size() == 1 && ParseEpochSeconds(token, out);
        }
    }

    // An empty string is midnight today, as GNU's `date -d ''` prints it.
    if (tokens.empty()) {
        std::tm broken = utc ? UtcTimeOf(now.seconds) : LocalTimeOf(now.seconds);
        broken.tm_hour = 0;
        broken.tm_min = 0;
        broken.tm_sec = 0;
        const auto seconds = utc
            ? std::optional<int64_t>(SecondsFromUtc(static_cast<int64_t>(broken.tm_year) + 1900,
                                                   static_cast<int64_t>(broken.tm_mon) + 1,
                                                   broken.tm_mday, 0, 0, 0))
            : SecondsFromLocalTime(broken);
        if (!seconds) {
            return false;
        }
        out = FileDateTime{*seconds, 0};
        return true;
    }

    DateItems items;
    if (!ParseDateItems(tokens, items)) {
        return false;
    }

    // The calendar fields start at |now|'s date and time (in the zone being
    // worked in); a date replaces the date and resets the time, a time of day
    // the time (and the nanoseconds).
    std::tm local = utc ? UtcTimeOf(now.seconds) : LocalTimeOf(now.seconds);
    uint32_t nanoseconds = now.nanoseconds;
    if (items.hasDate) {
        local.tm_year = static_cast<int>(items.year - 1900);
        local.tm_mon = static_cast<int>(items.month - 1);
        local.tm_mday = static_cast<int>(items.day);
        if (!items.hasTime) {
            local.tm_hour = 0;
            local.tm_min = 0;
            local.tm_sec = 0;
            nanoseconds = 0;
        }
    }
    if (items.hasTime) {
        local.tm_hour = items.hour;
        local.tm_min = items.minute;
        local.tm_sec = items.second;
        nanoseconds = items.nanoseconds;
    }
    local.tm_year += static_cast<int>(items.relYears);
    local.tm_mon += static_cast<int>(items.relMonths);
    local.tm_mday += static_cast<int>(items.relDays);

    int64_t seconds = 0;
    if (items.hasZone || utc) {
        // A zone given in |text| wins over -u's: both are UTC civil times,
        // only the offset differs.
        const int64_t offset = items.hasZone ? items.zoneOffsetSeconds : 0;
        seconds = SecondsFromUtc(static_cast<int64_t>(local.tm_year) + 1900,
                                 static_cast<int64_t>(local.tm_mon) + 1, local.tm_mday,
                                 local.tm_hour, local.tm_min, local.tm_sec) - offset;
    } else {
        const auto localSeconds = SecondsFromLocalTime(local);
        if (!localSeconds) {
            return false;
        }
        seconds = *localSeconds;
    }
    out = FileDateTime{seconds + items.relSeconds, nanoseconds};
    return true;
}

bool ParseTouchStamp(std::string_view text, FileDateTime now, bool utc, FileDateTime& out) {
    std::string_view main = text, fraction;
    const size_t dot = text.find('.');
    if (dot != std::string_view::npos) {
        main = text.substr(0, dot);
        fraction = text.substr(dot + 1);
        if (fraction.size() != 2 || !AllDigits(fraction) || !AllDigits(main)) {
            return false;
        }
    } else if (!AllDigits(main)) {
        return false;
    }
    if (main.size() != 8 && main.size() != 10 && main.size() != 12) {
        return false;
    }

    // [[CC]YY]MMDDhhmm, most of the year first.
    size_t at = 0;
    int64_t year = 0;
    if (main.size() == 12) {
        if (!DigitsTo(main.substr(0, 4), year)) {
            return false;
        }
        at = 4;
    } else if (main.size() == 10) {
        int64_t yy = 0;
        if (!DigitsTo(main.substr(0, 2), yy)) {
            return false;
        }
        year = yy >= 69 ? 1900 + yy : 2000 + yy;
        at = 2;
    } else {
        const std::tm nowBroken = utc ? UtcTimeOf(now.seconds) : LocalTimeOf(now.seconds);
        year = static_cast<int64_t>(nowBroken.tm_year) + 1900;
    }
    int64_t month = 0, day = 0, hour = 0, minute = 0, second = 0;
    if (!DigitsTo(main.substr(at, 2), month) || !DigitsTo(main.substr(at + 2, 2), day) ||
        !DigitsTo(main.substr(at + 4, 2), hour) || !DigitsTo(main.substr(at + 6, 2), minute)) {
        return false;
    }
    if (!fraction.empty() && !DigitsTo(fraction, second)) {
        return false;
    }
    if (month < 1 || month > 12 || day < 1 || day > DaysInMonth(year, month) ||
        hour > 23 || minute > 59 || second > 60) {
        return false;
    }

    std::tm local{};
    local.tm_year = static_cast<int>(year - 1900);
    local.tm_mon = static_cast<int>(month - 1);
    local.tm_mday = static_cast<int>(day);
    local.tm_hour = static_cast<int>(hour);
    local.tm_min = static_cast<int>(minute);
    local.tm_sec = static_cast<int>(second);
    const auto seconds = utc
        ? std::optional<int64_t>(SecondsFromUtc(year, month, day, hour, minute, second))
        : SecondsFromLocalTime(local);
    if (!seconds) {
        return false;
    }
    out = FileDateTime{*seconds, 0};
    return true;
}

std::tm LocalTimeOf(int64_t seconds) {
    const std::time_t asTimeT = static_cast<std::time_t>(seconds);
    std::tm local{};
#ifdef _WIN32
    // A time before 1970, which a file on the disk can have, is an invalid
    // parameter to localtime_s -- by default the end of the program (see
    // CrtInvalidParameterAsError). In scope, the call just fails, and |local|
    // stays zeroed.
    CrtInvalidParameterAsError crtErrors;
    localtime_s(&local, &asTimeT);
#else
    localtime_r(&asTimeT, &local);
#endif
    return local;
}

std::tm UtcTimeOf(int64_t seconds) {
    const std::time_t asTimeT = static_cast<std::time_t>(seconds);
    std::tm utc{};
#ifdef _WIN32
    CrtInvalidParameterAsError crtErrors;
    gmtime_s(&utc, &asTimeT);
#else
    gmtime_r(&asTimeT, &utc);
#endif
    return utc;
}

std::optional<int64_t> SecondsFromLocalTime(const std::tm& local) {
    std::tm copy = local;
    copy.tm_isdst = -1;
    const std::time_t result = std::mktime(&copy);
    if (result == static_cast<std::time_t>(-1)) {
        return std::nullopt;
    }
    return static_cast<int64_t>(result);
}

int64_t SecondsFromUtc(int64_t year, int64_t month, int64_t day,
                       int hour, int minute, int second) {
    // A month out of range folds into the year first, so the day is all that
    // days_from_civil has to take.
    if (month != 1) {
        const int64_t years = FloorDiv(month - 1, 12);
        year += years;
        month = month - 1 - years * 12 + 1;
    }
    // Howard Hinnant's days_from_civil, on the first of the month: |day| may
    // be out of range, the rest of it is whole days.
    const int64_t y = year - (month <= 2 ? 1 : 0);
    const int64_t era = FloorDiv(y, 400);
    const int64_t yearOfEra = y - era * 400;                                  // 0..399
    const int64_t dayOfYear = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5; // Mar: 0
    const int64_t dayOfEra = yearOfEra * 365 + yearOfEra / 4 - yearOfEra / 100 + dayOfYear;
    const int64_t days = era * 146097 + dayOfEra - 719468 + day - 1;
    return days * 86400 + static_cast<int64_t>(hour) * 3600 +
           static_cast<int64_t>(minute) * 60 + second;
}

namespace {

// --- strftime, as GNU date has it (gnulib nstrftime in the C locale) ---

const char* const kShortWeekdays[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
const char* const kLongWeekdays[] = {"Sunday", "Monday", "Tuesday", "Wednesday",
                                     "Thursday", "Friday", "Saturday"};
const char* const kShortMonths[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
const char* const kLongMonths[] = {"January", "February", "March", "April", "May",
                                   "June", "July", "August", "September", "October",
                                   "November", "December"};

// The flags a conversion may carry, from the characters between its '%' and
// its conversion character. '_' and '0' set the padding together: the last
// one given wins.
struct ConversionFlags {
    char pad = 0;         // '_' spaces, '0' zeros, 0 the conversion's own
    bool noPad = false;   // '-': nothing padded at all
    bool upper = false;   // '^': upper case
    bool swap = false;    // '#': am/pm and the zone name lower case, the other names upper
};

// What the conversions read: the broken-down time in the zone being worked
// in, the point itself (%s), its nanoseconds (%N) and its zone offset (%z).
struct FormatTime {
    std::tm broken;
    int64_t seconds = 0;
    uint32_t nanoseconds = 0;
    int64_t zoneOffset = 0;
    bool utc = false;
};

std::string UpperAscii(std::string text) {
    for (char& c : text) {
        if (c >= 'a' && c <= 'z') {
            c = static_cast<char>(c - 'a' + 'A');
        }
    }
    return text;
}

std::string LowerAscii(std::string text) {
    for (char& c : text) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return text;
}

// A number, padded on the left to at least max(|defaultWidth|, |width|)
// characters, with |flags.pad| where it says one and |pad| otherwise; '-'
// leaves it as it is. '_' pads with spaces, '0' with zeros.
std::string PadNumber(std::string digits, const ConversionFlags& flags,
                      size_t width, size_t defaultWidth, char pad) {
    if (flags.noPad) {
        return digits;
    }
    const char padding = flags.pad == '_' ? ' ' : flags.pad == '0' ? '0' : pad;
    const size_t target = std::max(defaultWidth, width);
    if (digits.size() < target) {
        digits.insert(0, target - digits.size(), padding);
    }
    return digits;
}

std::string FormatNumber(int64_t value, const ConversionFlags& flags,
                         size_t width, size_t defaultWidth, char pad) {
    return PadNumber(std::to_string(value), flags, width, defaultWidth, pad);
}

// A signed number (%s, %z): padding zeros go between the sign and the digits
// ('-000000100'), padding spaces before the sign ('      -100').
std::string PadSignedNumber(const std::string& sign, std::string digits,
                            const ConversionFlags& flags, size_t width,
                            size_t defaultWidth, char pad) {
    if (flags.noPad) {
        return sign + digits;
    }
    const char padding = flags.pad == '_' ? ' ' : flags.pad == '0' ? '0' : pad;
    const size_t target = std::max(defaultWidth, width);
    const size_t have = sign.size() + digits.size();
    if (padding == '0') {
        if (have < target) {
            digits.insert(0, target - have, '0');
        }
        return sign + digits;
    }
    return have < target ? std::string(target - have, padding) + sign + digits
                         : sign + digits;
}

// A name, a fixed text or a composite's whole result: padded on the left to
// |width| (spaces, or zeros with '0'), never when '-'.
std::string PadText(std::string text, const ConversionFlags& flags, size_t width) {
    if (flags.noPad || width == 0 || text.size() >= width) {
        return text;
    }
    text.insert(0, width - text.size(), flags.pad == '0' ? '0' : ' ');
    return text;
}

// '^' upper-cases a name; '#' swaps it, per conversion: the am/pm names and
// the zone name come out lower case, the day and month names upper case.
std::string SwapCase(std::string text, const ConversionFlags& flags, bool swapLowers) {
    if (flags.upper) {
        return UpperAscii(std::move(text));
    }
    if (flags.swap) {
        return swapLowers ? LowerAscii(std::move(text)) : UpperAscii(std::move(text));
    }
    return text;
}

std::string TwoDigitsOf(int64_t value) {
    char digits[2] = {static_cast<char>('0' + value / 10),
                      static_cast<char>('0' + value % 10)};
    return std::string(digits, 2);
}

// The ISO week and its year, %G %g %V: a week belongs to the year of its
// Thursday, and is that Thursday's number from the year's first week -- its
// day of the year divided by 7, plus one.
struct IsoWeek {
    int year;
    int week;
};

IsoWeek IsoWeekOf(const FormatTime& t) {
    const std::tm& tm = t.broken;
    const int isoWday = tm.tm_wday == 0 ? 7 : tm.tm_wday;  // Monday 1 .. Sunday 7
    const int64_t thursday = SecondsFromUtc(static_cast<int64_t>(tm.tm_year) + 1900,
                                            static_cast<int64_t>(tm.tm_mon) + 1,
                                            tm.tm_mday, 0, 0, 0) + (4 - isoWday) * 86400;
    const std::tm iso = UtcTimeOf(thursday);
    return IsoWeek{iso.tm_year + 1900, iso.tm_yday / 7 + 1};
}

// The zone name %Z prints: "UTC" when working in UTC, the C library's
// abbreviation of the local time otherwise (on Windows the long name -- a
// documented exception, as it was in ls's own code).
std::string ZoneNameOf(const FormatTime& t) {
    if (t.utc) {
        return "UTC";
    }
    std::tm local = t.broken;
    char buffer[64];
#ifdef _WIN32
    CrtInvalidParameterAsError crtErrors;
#endif
    const size_t written = std::strftime(buffer, sizeof(buffer), "%Z", &local);
    return std::string(buffer, written);
}

// %N: the nanoseconds as GNU prints them -- 9 digits by default, |width|
// under 9 takes only its first digits, over 9 pads behind the 9 (zeros,
// spaces with '_'), never padded at all with '-'.
std::string FormatNanoseconds(uint32_t nanoseconds, const ConversionFlags& flags, size_t width) {
    const std::string digits = PadNumber(std::to_string(nanoseconds), {}, 0, 9, '0');
    if (flags.noPad || width == 0) {
        return digits;
    }
    if (width < 9) {
        return digits.substr(0, width);
    }
    return digits + std::string(width - 9, flags.pad == '_' ? ' ' : '0');
}

// One format's text, |format| applied to |t|. The composites (%c and friends)
// come back here with their own expansions, whose own padding stands: the
// flags and the width of the whole conversion never reach the parts (GNU's
// %-c keeps the " 1" of an unpadded %e, and the width pads the result).
void AppendFormatted(std::string& out, std::string_view format, const FormatTime& t) {
    size_t i = 0;
    while (i < format.size()) {
        if (format[i] != '%') {
            out += format[i++];
            continue;
        }
        const size_t start = i++;

        // Flags, a width, an E/O locale modifier (parsed and ignored: no
        // locale but the C one is ever loaded), and up to three ':'s, which
        // only %z uses.
        ConversionFlags flags;
        while (i < format.size()) {
            if (format[i] == '_') {
                flags.pad = '_';
            } else if (format[i] == '-') {
                flags.noPad = true;
            } else if (format[i] == '0') {
                flags.pad = '0';
            } else if (format[i] == '^') {
                flags.upper = true;
            } else if (format[i] == '#') {
                flags.swap = true;
            } else {
                break;
            }
            ++i;
        }
        size_t width = 0;
        while (i < format.size() && format[i] >= '0' && format[i] <= '9') {
            width = width * 10 + static_cast<size_t>(format[i] - '0');
            ++i;
        }
        const bool barePercent = i == start + 1;  // nothing between the two '%'s
        if (i < format.size() && (format[i] == 'E' || format[i] == 'O')) {
            ++i;
        }
        size_t colons = 0;
        while (i < format.size() && colons < 3 && format[i] == ':') {
            ++colons;
            ++i;
        }

        // What is left is a conversion character -- or the format ran out, or
        // another '%' stands where the conversion should be. Both are written
        // out as they stand, as GNU's does: a bad conversion is copied from
        // its '%' through its last understood character, padded to |width|.
        // A '%' taken for the conversion character is left out of the copy and
        // read again -- it may start the real conversion ('%3%N' is ' %3' and
        // the nanoseconds).
        const auto writeAsItStands = [&](size_t end, bool excludeLast) {
            out += PadText(std::string(format.substr(
                               start, end - start - (excludeLast ? 1 : 0))),
                           flags, width);
        };
        if (i >= format.size()) {
            writeAsItStands(format.size(), false);  // a trailing '%' or '%5'
            continue;
        }
        if (format[i] == '%') {
            if (barePercent) {
                out += '%';
                ++i;
            } else {
                writeAsItStands(i, true);
            }
            continue;
        }

        // ':'s make sense to %z alone; in front of anything else the
        // conversion is not one, and written out as it stands.
        if (colons != 0 && format[i] != 'z') {
            writeAsItStands(i + 1, false);
            ++i;
            continue;
        }

        const char c = format[i];
        const std::tm& tm = t.broken;
        const int year = tm.tm_year + 1900;
        switch (c) {
            // The day of the week.
            case 'a':
            case 'A': {
                const int wday = tm.tm_wday < 0 || tm.tm_wday > 6 ? 0 : tm.tm_wday;
                out += PadText(SwapCase(c == 'a' ? kShortWeekdays[wday]
                                                 : kLongWeekdays[wday],
                                        flags, false), flags, width);
                ++i;
                break;
            }
            // The month.
            case 'b':
            case 'h':
            case 'B': {
                const int mon = tm.tm_mon < 0 || tm.tm_mon > 11 ? 0 : tm.tm_mon;
                out += PadText(SwapCase(c == 'B' ? kLongMonths[mon] : kShortMonths[mon],
                                        flags, false), flags, width);
                ++i;
                break;
            }
            // AM/PM, and its lower-case form %P.
            case 'p':
            case 'P': {
                std::string text = tm.tm_hour < 12 ? "AM" : "PM";
                if (c == 'P') {
                    text = LowerAscii(text);
                    if (flags.upper || flags.swap) {
                        text = UpperAscii(text);
                    }
                    out += PadText(std::move(text), flags, width);
                } else {
                    out += PadText(SwapCase(std::move(text), flags, true), flags, width);
                }
                ++i;
                break;
            }
            case 'n':
                out += PadText("\n", flags, width);
                ++i;
                break;
            case 't':
                out += PadText("\t", flags, width);
                ++i;
                break;
            // The zone name.
            case 'Z': {
                std::string text = ZoneNameOf(t);
                if (flags.upper) {
                    text = UpperAscii(text);
                } else if (flags.swap) {
                    text = LowerAscii(text);
                }
                out += PadText(std::move(text), flags, width);
                ++i;
                break;
            }
            // The composites, each a format of their own.
            case 'c':
            case 'D':
            case 'x':
            case 'F':
            case 'r':
            case 'R':
            case 'T':
            case 'X': {
                const char* expansion = "%H:%M:%S";  // T and X
                if (c == 'c') {
                    expansion = "%a %b %e %H:%M:%S %Y";
                } else if (c == 'D' || c == 'x') {
                    expansion = "%m/%d/%y";
                } else if (c == 'F') {
                    expansion = "%Y-%m-%d";
                } else if (c == 'r') {
                    expansion = "%I:%M:%S %p";
                } else if (c == 'R') {
                    expansion = "%H:%M";
                }
                std::string text;
                AppendFormatted(text, expansion, t);
                // '^' upper-cases the whole result; '#' changes nothing here.
                out += PadText(flags.upper ? UpperAscii(std::move(text)) : std::move(text),
                               flags, width);
                ++i;
                break;
            }
            // The numbers. The default padding is the digit '0' for most,
            // a space for %e %k %l.
            case 'd': out += FormatNumber(tm.tm_mday, flags, width, 2, '0'); ++i; break;
            case 'e': out += FormatNumber(tm.tm_mday, flags, width, 2, ' '); ++i; break;
            case 'H': out += FormatNumber(tm.tm_hour, flags, width, 2, '0'); ++i; break;
            case 'k': out += FormatNumber(tm.tm_hour, flags, width, 2, ' '); ++i; break;
            case 'I':
            case 'l': {
                const int hour12 = tm.tm_hour % 12 == 0 ? 12 : tm.tm_hour % 12;
                out += FormatNumber(hour12, flags, width, 2, c == 'I' ? '0' : ' ');
                ++i;
                break;
            }
            case 'm': out += FormatNumber(tm.tm_mon + 1, flags, width, 2, '0'); ++i; break;
            case 'M': out += FormatNumber(tm.tm_min, flags, width, 2, '0'); ++i; break;
            case 'S': out += FormatNumber(tm.tm_sec, flags, width, 2, '0'); ++i; break;
            case 'j': out += FormatNumber(tm.tm_yday + 1, flags, width, 3, '0'); ++i; break;
            case 'y': out += FormatNumber(((year % 100) + 100) % 100, flags, width, 2, '0'); ++i; break;
            case 'C': out += FormatNumber(year / 100, flags, width, 2, '0'); ++i; break;
            case 'Y': out += FormatNumber(year, flags, width, 4, '0'); ++i; break;
            case 'u': out += FormatNumber(tm.tm_wday == 0 ? 7 : tm.tm_wday, flags, width, 1, '0'); ++i; break;
            case 'w': out += FormatNumber(tm.tm_wday, flags, width, 1, '0'); ++i; break;
            case 'q': out += FormatNumber(tm.tm_mon / 3 + 1, flags, width, 1, '0'); ++i; break;
            // The week numbers.
            case 'U': out += FormatNumber((tm.tm_yday + 7 - tm.tm_wday) / 7, flags, width, 2, '0'); ++i; break;
            case 'W': out += FormatNumber((tm.tm_yday + 7 - (tm.tm_wday + 6) % 7) / 7, flags, width, 2, '0'); ++i; break;
            case 'G':
            case 'g':
            case 'V': {
                const IsoWeek iso = IsoWeekOf(t);
                if (c == 'G') {
                    out += FormatNumber(iso.year, flags, width, 4, '0');
                } else if (c == 'g') {
                    out += FormatNumber(((iso.year % 100) + 100) % 100, flags, width, 2, '0');
                } else {
                    out += FormatNumber(iso.week, flags, width, 2, '0');
                }
                ++i;
                break;
            }
            // The seconds since the epoch, a signed number.
            case 's': {
                std::string digits = std::to_string(t.seconds);
                std::string sign;
                if (!digits.empty() && digits[0] == '-') {
                    sign = "-";
                    digits.erase(0, 1);
                }
                out += PadSignedNumber(sign, digits, flags, width, 1, '0');
                ++i;
                break;
            }
            case 'N':
                out += FormatNanoseconds(t.nanoseconds, flags, width);
                ++i;
                break;
            // The zone offset, +hhmm or (with ':'s) +hh:mm[:ss]. %:::z keeps
            // only the parts the offset needs.
            case 'z': {
                const std::string sign(1, t.zoneOffset < 0 ? '-' : '+');
                const int64_t magnitude = t.zoneOffset < 0 ? -t.zoneOffset : t.zoneOffset;
                const int64_t minutes = magnitude / 60;
                const int64_t minutesOfHour = minutes % 60;
                const int64_t secondsOfMinute = magnitude % 60;
                if (colons == 0) {
                    out += PadSignedNumber(sign,
                                            std::to_string(minutes / 60 * 100 + minutesOfHour),
                                            flags, width, 5, '0');
                    ++i;
                    break;
                }
                std::string inner = PadNumber(std::to_string(minutes / 60), flags, 0, 2, '0');
                if (colons == 3 && minutesOfHour == 0 && secondsOfMinute == 0) {
                    // %:::z of a whole-hour (or UTC) offset: the hours alone.
                } else {
                    inner += ":" + TwoDigitsOf(minutesOfHour);
                    if (colons == 2 || (colons == 3 && secondsOfMinute != 0)) {
                        inner += ":" + TwoDigitsOf(secondsOfMinute);
                    }
                }
                out += PadSignedNumber(sign, inner, flags, width, inner.size() + 1, '0');
                ++i;
                break;
            }
            // A conversion the C locale does not have: written out as it
            // stands, as everything above already decided for worse cases.
            default:
                writeAsItStands(i + 1, false);
                ++i;
                break;
        }
    }
}

} // namespace

std::string FormatDateTime(std::string_view format, FileDateTime t, bool utc) {
    FormatTime parts;
    parts.broken = utc ? UtcTimeOf(t.seconds) : LocalTimeOf(t.seconds);
    parts.seconds = t.seconds;
    parts.nanoseconds = t.nanoseconds;
    parts.utc = utc;
    // The offset %z prints: whatever the civil time says, minus the point
    // itself -- right even where a std::tm does not carry its zone.
    parts.zoneOffset = SecondsFromUtc(static_cast<int64_t>(parts.broken.tm_year) + 1900,
                                      static_cast<int64_t>(parts.broken.tm_mon) + 1,
                                      parts.broken.tm_mday, parts.broken.tm_hour,
                                      parts.broken.tm_min, parts.broken.tm_sec) - t.seconds;
    std::string out;
    AppendFormatted(out, format, parts);
    return out;
}

} // namespace Haisos