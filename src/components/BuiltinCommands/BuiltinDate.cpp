#include "BuiltinDate.h"
#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdlib>
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
// the leap second, as GNU's).
bool ParseTimeOfDay(std::string_view text, int& hour, int& minute, int& second, uint32_t& nanoseconds) {
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
    if (i != text.size() || h > 23 || m > 59 || s > 60) {
        return false;
    }
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
        if (!ParseTimeOfDay(text.substr(k + 1), hour, minute, second, nanoseconds)) {
            return false;
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
                ++i;
                continue;
            }
            if (raw.find(':') != std::string::npos) {
                if (items.hasTime) {
                    return false;
                }
                int hour = 0, minute = 0, second = 0;
                uint32_t nanoseconds = 0;
                if (!ParseTimeOfDay(raw, hour, minute, second, nanoseconds)) {
                    return false;
                }
                items.hour = hour;
                items.minute = minute;
                items.second = second;
                items.nanoseconds = nanoseconds;
                items.hasTime = true;
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
            continue;
        }

        // A zone word, or a signed number: a zone after a time of day (GNU's
        // rule), otherwise a relative item's count with a following unit.
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
            if (items.hasTime) {
                if (items.hasZone) {
                    return false;
                }
                int64_t offset = 0;
                if (!ParseZoneDigits(sign, number, offset)) {
                    return false;
                }
                items.zoneOffsetSeconds = offset;
                items.hasZone = true;
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
            continue;
        }

        // The zone words, all UTC.
        if (word == "z" || word == "utc" || word == "ut" || word == "gmt") {
            if (items.hasZone) {
                return false;
            }
            items.zoneOffsetSeconds = 0;
            items.hasZone = true;
            ++i;
            continue;
        }
        if (word == "now" || word == "today") {
            ++i;
            continue;
        }
        if (word == "yesterday") {
            items.relDays -= 1;
            ++i;
            continue;
        }
        if (word == "tomorrow") {
            items.relDays += 1;
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

bool ParseDateString(std::string_view text, FileDateTime now, FileDateTime& out) {
    const std::vector<std::string> tokens = SplitItems(text);

    // `@` seconds stand alone: any other item with them is not understood.
    for (const auto& token : tokens) {
        if (!token.empty() && token[0] == '@') {
            return tokens.size() == 1 && ParseEpochSeconds(token, out);
        }
    }

    // An empty string is midnight today, as GNU's `date -d ''` prints it.
    if (tokens.empty()) {
        std::tm local = LocalTimeOf(now.seconds);
        local.tm_hour = 0;
        local.tm_min = 0;
        local.tm_sec = 0;
        const auto seconds = SecondsFromLocalTime(local);
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

    // The calendar fields start at |now|'s local date and time; a date
    // replaces the date and resets the time, a time of day the time (and the
    // nanoseconds).
    std::tm local = LocalTimeOf(now.seconds);
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
    if (items.hasZone) {
        seconds = SecondsFromUtc(static_cast<int64_t>(local.tm_year) + 1900,
                                 static_cast<int64_t>(local.tm_mon) + 1, local.tm_mday,
                                 local.tm_hour, local.tm_min, local.tm_sec) - items.zoneOffsetSeconds;
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

bool ParseTouchStamp(std::string_view text, FileDateTime now, FileDateTime& out) {
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
        year = static_cast<int64_t>(LocalTimeOf(now.seconds).tm_year) + 1900;
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
    const auto seconds = SecondsFromLocalTime(local);
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

} // namespace Haisos