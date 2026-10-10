#include "PatchParse.h"
#include <cctype>
#include <ctime>
#include "BuiltinDate.h"
#include "src/components/Filesystem/FilesystemUtils.h"

namespace Haisos {
namespace {

std::vector<PatchReader::InputLine> SplitInputLines(const std::string& text) {
    std::vector<PatchReader::InputLine> lines;
    size_t start = 0;
    while (start <= text.size()) {
        const size_t found = text.find('\n', start);
        if (found == std::string::npos) {
            if (start < text.size()) {
                lines.push_back(PatchReader::InputLine{text.substr(start), false});
            }
            break;
        }
        lines.push_back(PatchReader::InputLine{text.substr(start, found - start), true});
        start = found + 1;
    }
    return lines;
}

bool StartsWith(const std::string& text, const char* prefix) {
    for (size_t i = 0; prefix[i] != '\0'; ++i) {
        if (i >= text.size() || text[i] != prefix[i]) {
            return false;
        }
    }
    return true;
}

// A decimal number: digits only, empty or anything else rejected. A value
// with more digits than an int64 holds is refused -- unless |tooLarge| is
// given, when it saturates to INT64_MAX and |tooLarge| is set (the hunk
// header reader reports it).
bool ParseNumber(const std::string& text, size_t begin, size_t end, int64_t& out,
                 bool* tooLarge = nullptr) {
    if (begin >= end) {
        return false;
    }
    int64_t value = 0;
    for (size_t i = begin; i < end; ++i) {
        if (text[i] < '0' || text[i] > '9') {
            return false;
        }
        if (value > (INT64_MAX - (text[i] - '0')) / 10) {
            if (!tooLarge) {
                return false;
            }
            *tooLarge = true;
            out = INT64_MAX;
            return true;
        }
        value = value * 10 + (text[i] - '0');
    }
    out = value;
    return true;
}

// One name out of a "..." string: \" \\ \t \n and three-digit octal escapes
// read, the quoted span |begin .. end| (quotes excluded) unquoted.
std::string UnquoteCName(const std::string& text, size_t begin, size_t end) {
    std::string out;
    for (size_t i = begin; i < end; ++i) {
        if (text[i] != '\\' || i + 1 >= end) {
            out += text[i];
            continue;
        }
        ++i;
        switch (text[i]) {
            case '"': out += '"'; break;
            case '\\': out += '\\'; break;
            case 't': out += '\t'; break;
            case 'n': out += '\n'; break;
            default:
                if (text[i] >= '0' && text[i] <= '7' && i + 2 < end
                    && text[i + 1] >= '0' && text[i + 1] <= '7'
                    && text[i + 2] >= '0' && text[i + 2] <= '7') {
                    out += static_cast<char>((text[i] - '0') * 64
                        + (text[i + 1] - '0') * 8 + (text[i + 2] - '0'));
                    i += 2;
                } else {
                    out += text[i];
                }
                break;
        }
    }
    return out;
}

// Where a "..."-quoted name starting at |at| ends: the index just past its
// closing quote, or nullopt when it never closes.
std::optional<size_t> EndOfQuotedName(const std::string& text, size_t at) {
    for (size_t i = at + 1; i < text.size(); ++i) {
        if (text[i] == '\\') {
            ++i;
            continue;
        }
        if (text[i] == '"') {
            return i + 1;
        }
    }
    return std::nullopt;
}

// -p NUM: remove NUM leading components of |name|, a run of '/' counting as
// one separator and a leading '/' as one component; too few components
// leaves no name. A negative strip (no -p) keeps only the last component.
std::optional<std::string> StripFileName(const std::string& name, int64_t strip) {
    if (strip < 0) {
        const size_t slash = name.find_last_of('/');
        return slash == std::string::npos ? name : name.substr(slash + 1);
    }
    std::vector<std::string> components;
    size_t i = 0;
    if (!name.empty() && name[0] == '/') {
        components.push_back("");  // a leading '/' is one component
        i = 1;
        while (i < name.size() && name[i] == '/') {
            ++i;
        }
    }
    while (i < name.size()) {
        const size_t slash = name.find('/', i);
        if (slash == std::string::npos) {
            components.push_back(name.substr(i));
            break;
        }
        components.push_back(name.substr(i, slash - i));
        i = slash;
        while (i < name.size() && name[i] == '/') {
            ++i;
        }
    }
    if (static_cast<int64_t>(components.size()) <= strip) {
        return std::nullopt;
    }
    std::string out;
    for (size_t j = static_cast<size_t>(strip); j < components.size(); ++j) {
        if (j > static_cast<size_t>(strip)) {
            out += '/';
        }
        out += components[j];
    }
    return out;
}

// The seconds the Www Mmm DD HH:MM:SS YYYY form names, in the local zone
// (as GNU date's -d of the same words does).
bool ParseWordedStamp(const std::string& text, int64_t& seconds) {
    // "Www Mmm DD HH:MM:SS YYYY", the day one or two digits, space-padded.
    static const char* months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    size_t i = 0;
    const auto skipSpaces = [&text, &i]() {
        while (i < text.size() && (text[i] == ' ' || text[i] == '\t')) {
            ++i;
        }
    };
    const auto word = [&text, &i]() {
        const size_t start = i;
        while (i < text.size() && text[i] != ' ' && text[i] != '\t') {
            ++i;
        }
        return text.substr(start, i - start);
    };
    skipSpaces();
    const std::string weekday = word();
    if (weekday.size() != 3) {
        return false;
    }
    skipSpaces();
    const std::string month = word();
    int monthNumber = 0;
    for (int m = 0; m < 12; ++m) {
        if (month == months[m]) {
            monthNumber = m + 1;
        }
    }
    if (monthNumber == 0) {
        return false;
    }
    skipSpaces();
    int64_t day = 0;
    if (!ParseNumber(text, i, text.find_first_not_of("0123456789", i) == std::string::npos
            ? text.size() : text.find_first_not_of("0123456789", i), day)) {
        return false;
    }
    i = text.find_first_not_of("0123456789", i);
    if (i == std::string::npos) {
        return false;
    }
    skipSpaces();
    const std::string time = word();
    if (time.size() != 8 || time[2] != ':' || time[5] != ':') {
        return false;
    }
    int64_t hour = 0, minute = 0, second = 0;
    if (!ParseNumber(time, 0, 2, hour) || !ParseNumber(time, 3, 5, minute)
        || !ParseNumber(time, 6, 8, second)) {
        return false;
    }
    skipSpaces();
    int64_t year = 0;
    const size_t yearEnd = i;
    if (!ParseNumber(text, yearEnd, text.find_first_not_of("0123456789", yearEnd)
            == std::string::npos ? text.size() : text.find_first_not_of("0123456789", yearEnd), year)) {
        return false;
    }
    std::tm when = {};
    when.tm_year = static_cast<int>(year - 1900);
    when.tm_mon = monthNumber - 1;
    when.tm_mday = static_cast<int>(day);
    when.tm_hour = static_cast<int>(hour);
    when.tm_min = static_cast<int>(minute);
    when.tm_sec = static_cast<int>(second);
    when.tm_isdst = -1;
    const std::optional<int64_t> local = SecondsFromLocalTime(when);
    if (!local) {
        return false;
    }
    seconds = *local;
    return true;
}

// The seconds a header time stamp names, through ParseDateString (the local
// zone when it names none) or the Www Mmm DD ... form.
bool ParseStampSeconds(const std::string& text, int64_t& seconds) {
    if (text.empty()) {
        return false;
    }
    FileDateTime when;
    if (ParseDateString(text, CurrentFileDateTime(), false, when)) {
        seconds = when.seconds;
        return true;
    }
    return ParseWordedStamp(text, seconds);
}

// Whether a header time stamp names the epoch: GNU patch takes a stamp more
// than 25 hours before and less than 26 hours after 1970-01-01 00:00:00 UTC
// as "this side's file did not exist" (diff -N writes the epoch for one that
// does not).
bool IsEpochStamp(const std::string& timeText) {
    int64_t seconds = 0;
    if (!ParseStampSeconds(timeText, seconds)) {
        return false;
    }
    return seconds > -25 * 60 * 60 && seconds < 26 * 60 * 60;
}

// A header line's own line ending is never part of a name it holds, so even
// --binary, which keeps the patch's CRs, loses it here: the names of a patch
// written with CRLF line endings still come out without their '\r'.
std::string HeaderText(const std::string& text) {
    if (!text.empty() && text.back() == '\r') {
        return text.substr(0, text.size() - 1);
    }
    return text;
}

// One name side of a "---"/"+++"/"***"/"---" line: the name (nullopt for
// /dev/null or none), the time text after it, and whether the name said the
// file is not there.
struct HeaderName {
    std::optional<std::string> name;
    std::string timeText;
    bool absent = false;
};

HeaderName ParseHeaderName(const std::string& raw) {
    HeaderName out;
    size_t i = 0;
    while (i < raw.size() && (raw[i] == ' ' || raw[i] == '\t')) {
        ++i;
    }
    if (i >= raw.size()) {
        return out;
    }
    if (raw[i] == '"') {
        const std::optional<size_t> end = EndOfQuotedName(raw, i);
        if (end) {
            out.name = UnquoteCName(raw, i + 1, *end - 1);
            i = *end;
        } else {
            out.name = raw.substr(i + 1);
            i = raw.size();
        }
    } else {
        // The name runs to the first whitespace, except that it may hold
        // spaces when a tab follows it (the time stamp separator).
        const size_t tab = raw.find('\t', i);
        if (tab != std::string::npos) {
            out.name = raw.substr(i, tab - i);
            i = tab + 1;
        } else {
            const size_t space = raw.find(' ', i);
            if (space != std::string::npos) {
                out.name = raw.substr(i, space - i);
                i = space + 1;
            } else {
                out.name = raw.substr(i);
                i = raw.size();
            }
        }
    }
    if (out.name && *out.name == "/dev/null") {
        out.name = std::nullopt;
        out.absent = true;
    }
    while (i < raw.size() && (raw[i] == ' ' || raw[i] == '\t')) {
        ++i;
    }
    out.timeText = raw.substr(i);
    return out;
}

// One name out of a "diff --git A B" line: "..."-quoted or a plain word.
std::optional<std::string> ParseGitName(const std::string& text, size_t& at) {
    while (at < text.size() && (text[at] == ' ' || text[at] == '\t')) {
        ++at;
    }
    if (at >= text.size()) {
        return std::nullopt;
    }
    if (text[at] == '"') {
        const std::optional<size_t> end = EndOfQuotedName(text, at);
        if (!end) {
            return std::nullopt;
        }
        const std::string name = UnquoteCName(text, at + 1, *end - 1);
        at = *end;
        return name;
    }
    const size_t start = at;
    while (at < text.size() && text[at] != ' ' && text[at] != '\t') {
        ++at;
    }
    return text.substr(start, at - start);
}

// Whether |text| names an option-like git header line of a "diff --git"
// block, and which.
enum class GitHeader { None, Index, NewFile, DeletedFile, Mode, Rename, Copy };

GitHeader ClassifyGitHeader(const std::string& text) {
    if (StartsWith(text, "index ")) {
        return GitHeader::Index;
    }
    if (StartsWith(text, "new file mode")) {
        return GitHeader::NewFile;
    }
    if (StartsWith(text, "deleted file mode")) {
        return GitHeader::DeletedFile;
    }
    if (StartsWith(text, "old mode") || StartsWith(text, "new mode")
        || StartsWith(text, "similarity index") || StartsWith(text, "dissimilarity index")) {
        return GitHeader::Mode;
    }
    if (StartsWith(text, "rename from ") || StartsWith(text, "rename to ")) {
        return GitHeader::Rename;
    }
    if (StartsWith(text, "copy from ") || StartsWith(text, "copy to ")) {
        return GitHeader::Copy;
    }
    return GitHeader::None;
}

// A context hunk's opening line: a run of 8 or more '*' (fewer is not one),
// whatever follows the run being the hunk's function name.
bool IsStarHunkLine(const std::string& text) {
    size_t stars = 0;
    while (stars < text.size() && text[stars] == '*') {
        ++stars;
    }
    return stars >= 8;
}

// The function name that follows a star hunk line's run of stars.
std::string StarHunkFunction(const std::string& text) {
    size_t at = 0;
    while (at < text.size() && text[at] == '*') {
        ++at;
    }
    std::string function = text.substr(at);
    while (!function.empty() && (function.front() == ' ' || function.front() == '\t')) {
        function.erase(function.begin());
    }
    return function;
}

// The range line of a context hunk's part, |text| what follows the mark
// ("4,6 ****" / "4,6 ----", the suffix left out in the old style): the
// part's first line and how many of its lines follow, |oldStyle| when the
// suffix is missing. The new style's tail is a run of four or more of
// |suffixChar| ('*' for the old part, '-' for the new one; a reject file's
// five dashes read back too). A bare 0 (or 0,0) is the empty range after
// line 0: start 1, no lines.
bool ParseContextRangeLine(const std::string& text, char suffixChar, int64_t& start,
                           int64_t& count, bool& oldStyle) {
    size_t at = 0;
    while (at < text.size() && (text[at] == ' ' || text[at] == '\t')) {
        ++at;
    }
    const auto digitsEnd = [&text](size_t from) {
        size_t j = from;
        while (j < text.size() && text[j] >= '0' && text[j] <= '9') {
            ++j;
        }
        return j;
    };
    int64_t first = 0;
    if (!ParseNumber(text, at, digitsEnd(at), first)) {
        return false;
    }
    at = digitsEnd(at);
    int64_t last = first;
    if (at < text.size() && text[at] == ',') {
        ++at;
        if (!ParseNumber(text, at, digitsEnd(at), last)) {
            return false;
        }
        at = digitsEnd(at);
    }
    size_t rest = at;
    while (rest < text.size() && (text[rest] == ' ' || text[rest] == '\t')) {
        ++rest;
    }
    size_t suffix = rest;
    while (suffix < text.size() && text[suffix] == suffixChar) {
        ++suffix;
    }
    while (suffix < text.size() && (text[suffix] == ' ' || text[suffix] == '\t')) {
        ++suffix;
    }
    if (suffix != text.size()) {
        return false;  // something besides the run of stars/dashes
    }
    oldStyle = rest == suffix;
    if (!oldStyle && suffix - rest < 4) {
        return false;
    }
    if (last < first) {
        return false;
    }
    if (first == 0 && last == 0) {
        start = 1;  // the empty range after line 0
        count = 0;
    } else {
        start = first;
        count = last - first + 1;
    }
    return true;
}

// A normal diff's command: "A[,B]{a|c|d}C[,D]".
struct NormalCommand {
    char kind = 'c';
    int64_t first = 0, last = 0, third = 0, fourth = 0;
};

bool ParseNormalCommand(const std::string& text, NormalCommand& command) {
    size_t at = 0;
    const auto digitsEnd = [&text](size_t from) {
        size_t j = from;
        while (j < text.size() && text[j] >= '0' && text[j] <= '9') {
            ++j;
        }
        return j;
    };
    const auto readNumber = [&](int64_t& out) {
        const size_t end = digitsEnd(at);
        if (!ParseNumber(text, at, end, out)) {
            return false;
        }
        at = end;
        return true;
    };
    if (!readNumber(command.first)) {
        return false;
    }
    command.last = command.first;
    if (at < text.size() && text[at] == ',') {
        ++at;
        if (!readNumber(command.last)) {
            return false;
        }
    }
    if (at >= text.size() || command.last < command.first) {
        return false;
    }
    command.kind = text[at];
    if (command.kind != 'a' && command.kind != 'c' && command.kind != 'd') {
        return false;
    }
    ++at;
    if (!readNumber(command.third)) {
        return false;
    }
    command.fourth = command.third;
    if (at < text.size() && text[at] == ',') {
        ++at;
        if (!readNumber(command.fourth)) {
            return false;
        }
    }
    if (command.fourth < command.third || at != text.size()) {
        return false;
    }
    return true;
}

} // namespace

// ---- PatchReader ----

PatchReader::PatchReader(std::string text, bool binary, std::optional<PatchFormat> format,
                         bool normalAllowed)
    : m_lines(SplitInputLines(text))
    , m_binary(binary)
    , m_format(format)
    , m_normalAllowed(normalAllowed)
{
}

std::string PatchReader::LineText(size_t index, bool& stripCr) const {
    const InputLine& line = m_lines[index];
    if (!m_binary && !line.text.empty() && line.text.back() == '\r') {
        stripCr = true;
        return line.text.substr(0, line.text.size() - 1);
    }
    return line.text;
}

bool PatchReader::ParseHunkHeader(const std::string& text, int64_t& oldStart, int64_t& oldCount,
                                  int64_t& newStart, int64_t& newCount, std::string& function,
                                  std::string* tooLarge, bool* overflow) const {
    if (!StartsWith(text, "@@ -")) {
        return false;
    }
    const auto digitsEnd = [&text](size_t at) {
        size_t j = at;
        while (j < text.size() && text[j] >= '0' && text[j] <= '9') {
            ++j;
        }
        return j;
    };
    const auto readNumber = [&](size_t at, int64_t& out) {
        bool large = false;
        if (!ParseNumber(text, at, digitsEnd(at), out, tooLarge ? &large : nullptr)) {
            return false;
        }
        if (large && tooLarge) {
            // The first number that saturates is the one reported.
            if (tooLarge->empty()) {
                tooLarge->assign(text, at, digitsEnd(at) - at);
            }
        }
        return true;
    };
    size_t i = 4;
    int64_t rawOldStart = 0, rawNewStart = 0;
    oldCount = 1;
    newCount = 1;
    if (!readNumber(i, rawOldStart)) {
        return false;
    }
    i = digitsEnd(i);
    if (i < text.size() && text[i] == ',') {
        ++i;
        if (!readNumber(i, oldCount)) {
            return false;
        }
        i = digitsEnd(i);
    }
    if (i + 1 >= text.size() || text[i] != ' ' || text[i + 1] != '+') {
        return false;
    }
    i += 2;
    if (!readNumber(i, rawNewStart)) {
        return false;
    }
    i = digitsEnd(i);
    if (i < text.size() && text[i] == ',') {
        ++i;
        if (!readNumber(i, newCount)) {
            return false;
        }
        i = digitsEnd(i);
    }
    if (i + 2 >= text.size() || text[i] != ' ' || text[i + 1] != '@' || text[i + 2] != '@') {
        return false;
    }
    i += 3;
    function = text.substr(i);
    while (!function.empty() && (function.front() == ' ' || function.front() == '\t')) {
        function.erase(function.begin());
    }
    // An empty range "-A,0" means "after line A": the hunk starts at A + 1 --
    // a start the increment would overflow is malformed, not a hunk.
    if (overflow && oldCount == 0 && rawOldStart == INT64_MAX) {
        *overflow = true;
    }
    if (overflow && newCount == 0 && rawNewStart == INT64_MAX) {
        *overflow = true;
    }
    oldStart = oldCount == 0 ? rawOldStart + 1 : rawOldStart;
    newStart = newCount == 0 ? rawNewStart + 1 : rawNewStart;
    return true;
}

size_t PatchReader::ReadHunk(size_t index, FilePatch& patch) {
    PatchHunk hunk;
    hunk.headerLine = static_cast<int64_t>(index) + 1;
    const std::string headerText = LineText(index, patch.stripTrailingCr);
    std::string tooLarge;
    bool overflow = false;
    if (!ParseHunkHeader(headerText, hunk.oldStart, hunk.oldCount, hunk.newStart, hunk.newCount,
            hunk.function, &tooLarge, &overflow)) {
        return index + 1;  // the caller checked: not reached
    }
    if (!tooLarge.empty()) {
        patch.malformed = "line number " + tooLarge + " is too large at line "
            + std::to_string(index + 1) + ": " + headerText + "\n";
        return m_lines.size();
    }
    if (overflow) {
        patch.malformed = "malformed patch at line " + std::to_string(index + 1) + ": "
            + headerText + "\n";
        return m_lines.size();
    }
    // The "-0,0" range: the side's file did not exist, surely when its time
    // stamp was the epoch or it was /dev/null (already Surely), else maybe.
    if (hunk.oldCount == 0 && hunk.oldStart == 1
        && patch.oldAbsence == SideAbsence::Present) {
        patch.oldAbsence = SideAbsence::Maybe;
    }
    if (hunk.newCount == 0 && hunk.newStart == 1
        && patch.newAbsence == SideAbsence::Present) {
        patch.newAbsence = SideAbsence::Maybe;
    }

    const size_t last = m_lines.empty() ? 0 : m_lines.size() - 1;
    size_t k = index + 1;
    int64_t oldRead = 0, newRead = 0;
    bool hasChange = false;
    const auto markNoNewline = [&hunk]() {
        if (!hunk.lines.empty()) {
            hunk.lines.back().noNewline = true;
            if (!hunk.lines.back().text.empty() && hunk.lines.back().text.back() == '\n') {
                hunk.lines.back().text.pop_back();
            }
        }
    };
    while (oldRead < hunk.oldCount || newRead < hunk.newCount) {
        if (k > last) {
            // The hunk is cut short by the end of the input. Trailing blank
            // lines lost in transit are made good when as many old as new
            // lines are missing and something changed; anything else is a
            // malformed hunk at the input's last line.
            const int64_t missingOld = hunk.oldCount - oldRead;
            const int64_t missingNew = hunk.newCount - newRead;
            if (hasChange && missingOld == missingNew) {
                for (int64_t m = 0; m < missingOld; ++m) {
                    hunk.lines.push_back(PatchLine{PatchLineKind::Context, "\n", false});
                }
                break;
            }
            patch.malformed = "malformed patch at line " + std::to_string(last + 1) + ":  \n";
            return m_lines.size();
        }
        const size_t lineNumber = k + 1;
        const bool delimited = m_lines[k].delimited;
        const std::string text = LineText(k, patch.stripTrailingCr);
        ++k;
        if (!text.empty() && text[0] == '\\') {
            markNoNewline();  // "\ No newline at end of file"
            continue;
        }
        PatchLineKind kind = PatchLineKind::Context;
        std::string content;
        if (text.empty()) {
            kind = PatchLineKind::Context;  // an empty line: an empty context line
            content = delimited ? "\n" : "";
        } else {
            switch (text[0]) {
                case ' ': kind = PatchLineKind::Context; break;
                case '-': kind = PatchLineKind::Delete; hasChange = true; break;
                case '+': kind = PatchLineKind::Insert; hasChange = true; break;
                default:
                    patch.malformed = "malformed patch at line "
                        + std::to_string(lineNumber) + ": " + text + "\n";
                    return m_lines.size();
            }
            content = text.substr(1) + (delimited ? "\n" : "");
        }
        hunk.lines.push_back(PatchLine{kind, content, false});
        if (kind == PatchLineKind::Context) {
            ++oldRead;
            ++newRead;
        } else if (kind == PatchLineKind::Delete) {
            ++oldRead;
        } else {
            ++newRead;
        }
    }
    // A "\ No newline" marker may follow the last line the counts name.
    while (k <= last) {
        const std::string text = LineText(k, patch.stripTrailingCr);
        if (text.empty() || text[0] != '\\') {
            break;
        }
        markNoNewline();
        ++k;
    }
    // The context runs before the first and after the last change.
    size_t first = 0;
    while (first < hunk.lines.size() && hunk.lines[first].kind == PatchLineKind::Context) {
        ++first;
    }
    size_t after = hunk.lines.size();
    while (after > first && hunk.lines[after - 1].kind == PatchLineKind::Context) {
        --after;
    }
    const bool hasChanges = first < after;
    hunk.leadingContext = hasChanges ? static_cast<int64_t>(first) : 0;
    hunk.trailingContext = hasChanges ? static_cast<int64_t>(hunk.lines.size() - after) : 0;
    patch.hunks.push_back(std::move(hunk));
    return k;
}

// One line of a context hunk's part: its mark (' ': context, '-': old file
// only, '+': new file only, '!': changed) and its text, '\n' included unless
// the "\ No newline" marker followed it.
struct ContextPartLine {
    char mark = ' ';
    std::string text;
    size_t inputLine = 0;  // the input line it came from, 1-based
    bool noNewline = false;
};

// Whether |text| holds a context part's two-character mark (a blank second
// byte included), |mark| the first byte; an empty line is empty context.
bool IsContextPartMark(const std::string& text, char& mark) {
    if (text.empty()) {
        mark = ' ';
        return true;
    }
    if (text.size() < 2 || (text[1] != ' ' && text[1] != '\t')) {
        return false;
    }
    mark = text[0];
    return true;
}

void MarkContextPartNoNewline(std::vector<ContextPartLine>& part) {
    if (!part.empty()) {
        part.back().noNewline = true;
        if (!part.back().text.empty() && part.back().text.back() == '\n') {
            part.back().text.pop_back();
        }
    }
}

size_t PatchReader::ReadContextHunk(size_t index, FilePatch& patch) {
    const size_t n = m_lines.size();
    const bool firstHunk = patch.hunks.empty();
    // A malformed first hunk is reported before anything else of this file
    // patch; an out-of-sync patch alone is not -- the hunk's parts were each
    // well formed, it is the two of them that do not meet.
    const auto malformed = [&](const std::string& message) {
        patch.malformed = message;
        if (firstHunk) {
            patch.malformedBeforeFile = message;
        }
        return n;
    };

    std::string function = StarHunkFunction(LineText(index, patch.stripTrailingCr));
    if (index + 1 >= n) {
        return malformed("malformed patch at line " + std::to_string(index + 2) + ":  \n");
    }
    const std::string oldRangeText = LineText(index + 1, patch.stripTrailingCr);
    int64_t oldStart = 0, oldCount = 0;
    bool oldStyle = false;
    if (!StartsWith(oldRangeText, "***")
        || !ParseContextRangeLine(oldRangeText.substr(3), '*', oldStart, oldCount, oldStyle)) {
        return malformed("malformed patch at line " + std::to_string(index + 2) + ": "
            + oldRangeText + "\n");
    }
    patch.format = oldStyle ? PatchFormat::OldContext : PatchFormat::Context;

    std::vector<ContextPartLine> oldPart, newPart;
    int64_t newStart = 0, newCount = 0;
    size_t separatorIndex = n;  // the "--- C[,D] ----" line
    // The old part, up to the "---" line that heads the new one.
    size_t k = index + 2;
    while (k < n) {
        const bool delimited = m_lines[k].delimited;
        const std::string text = LineText(k, patch.stripTrailingCr);
        const size_t lineNumber = k + 1;
        ++k;
        if (StartsWith(text, "---")) {
            bool style = false;
            if (ParseContextRangeLine(text.substr(3), '-', newStart, newCount, style)) {
                separatorIndex = k - 1;
                break;
            }
        }
        if (!text.empty() && text[0] == '\\') {
            MarkContextPartNoNewline(oldPart);  // "\ No newline at end of file"
            continue;
        }
        char mark = ' ';
        if (!IsContextPartMark(text, mark)
            || (mark != ' ' && mark != '-' && mark != '!')) {
            return malformed("malformed patch at line " + std::to_string(lineNumber)
                + ": " + text + "\n");
        }
        oldPart.push_back(ContextPartLine{mark,
            text.empty() ? std::string(delimited ? "\n" : "") : text.substr(2)
                + (delimited ? "\n" : ""), lineNumber, false});
    }
    if (separatorIndex == n) {
        return malformed("unexpected end of file in patch at line " + std::to_string(n)
            + "\n");
    }
    if (!oldPart.empty()) {
        if (static_cast<int64_t>(oldPart.size()) < oldCount) {
            return malformed("Premature '---' at line " + std::to_string(separatorIndex + 1)
                + "; check line numbers at line " + std::to_string(index + 2) + "\n");
        }
        if (static_cast<int64_t>(oldPart.size()) > oldCount) {
            return malformed("Overdue '---' at line " + std::to_string(separatorIndex + 1)
                + "; check line numbers at line " + std::to_string(index + 2) + "\n");
        }
    }
    // The new part, read until a line that is not one of its marks ("  ",
    // "+ ", "! ") -- a part cut short by the input's end is made good from
    // the old part's remaining lines.
    bool shortAtEnd = false;
    size_t end = n;  // the first line after the hunk
    while (k < n) {
        const bool delimited = m_lines[k].delimited;
        const std::string text = LineText(k, patch.stripTrailingCr);
        char mark = ' ';
        if (!text.empty() && text[0] == '\\') {
            MarkContextPartNoNewline(newPart);
            ++k;
            continue;
        }
        if (!IsContextPartMark(text, mark)
            || (mark != ' ' && mark != '+' && mark != '!')) {
            end = k;  // the terminator is the next thing's, not the hunk's
            break;
        }
        newPart.push_back(ContextPartLine{mark,
            text.empty() ? std::string(delimited ? "\n" : "") : text.substr(2)
                + (delimited ? "\n" : ""), k + 1, false});
        ++k;
    }
    const auto newPartMalformed = [&](const std::string& message) {
        patch.malformed = message;
        if (firstHunk) {
            patch.malformedBeforeFile = message;
        }
        return n;
    };
    if (!newPart.empty() && static_cast<int64_t>(newPart.size()) < newCount) {
        if (end == n) {
            // the input ended the hunk: the old part's remaining lines
            // complete the new part, as GNU completes it
            shortAtEnd = true;
        } else {
            const std::string terminator = LineText(end, patch.stripTrailingCr);
            if (IsStarHunkLine(terminator)) {
                return newPartMalformed("unexpected end of hunk at line "
                    + std::to_string(end + 1) + "\n");
            }
            return newPartMalformed("malformed patch at line " + std::to_string(end + 1)
                + ": " + terminator + "\n");
        }
    }
    if (oldPart.empty()) {
        // A part listing no lines is the other part's context lines.
        for (const ContextPartLine& line : newPart) {
            if (line.mark == ' ') {
                oldPart.push_back(line);
            }
        }
    }
    if (newPart.empty() && !shortAtEnd) {
        for (const ContextPartLine& line : oldPart) {
            if (line.mark == ' ') {
                newPart.push_back(line);
            }
        }
    } else if (shortAtEnd) {
        for (size_t x = newPart.size(); x < oldPart.size(); ++x) {
            newPart.push_back(oldPart[x]);
            newPart.back().mark = ' ';  // what mirrors an old line is context
        }
    }

    // Walking both parts together gives the unified order.
    PatchHunk hunk;
    hunk.headerLine = static_cast<int64_t>(index) + 1;
    hunk.function = std::move(function);
    hunk.oldStart = oldStart;
    hunk.oldCount = oldCount;
    hunk.newStart = newStart;
    hunk.newCount = newCount;
    size_t i2 = 0, j2 = 0;
    while (i2 < oldPart.size() || j2 < newPart.size()) {
        if (i2 < oldPart.size() && oldPart[i2].mark == '-') {
            hunk.lines.push_back(PatchLine{PatchLineKind::Delete, oldPart[i2].text,
                oldPart[i2].noNewline});
            ++i2;
            continue;
        }
        if (j2 < newPart.size() && newPart[j2].mark == '+') {
            hunk.lines.push_back(PatchLine{PatchLineKind::Insert, newPart[j2].text,
                newPart[j2].noNewline});
            ++j2;
            continue;
        }
        if (i2 < oldPart.size() && j2 < newPart.size()
            && oldPart[i2].mark == '!' && newPart[j2].mark == '!') {
            size_t iEnd = i2;
            while (iEnd < oldPart.size() && oldPart[iEnd].mark == '!') {
                ++iEnd;
            }
            size_t jEnd = j2;
            while (jEnd < newPart.size() && newPart[jEnd].mark == '!') {
                ++jEnd;
            }
            for (size_t x = i2; x < iEnd; ++x) {
                hunk.lines.push_back(PatchLine{PatchLineKind::Delete, oldPart[x].text,
                    oldPart[x].noNewline});
            }
            for (size_t x = j2; x < jEnd; ++x) {
                hunk.lines.push_back(PatchLine{PatchLineKind::Insert, newPart[x].text,
                    newPart[x].noNewline});
            }
            i2 = iEnd;
            j2 = jEnd;
            continue;
        }
        if (i2 < oldPart.size() && j2 < newPart.size()
            && oldPart[i2].mark == ' ' && newPart[j2].mark == ' ') {
            hunk.lines.push_back(PatchLine{PatchLineKind::Context, oldPart[i2].text,
                oldPart[i2].noNewline});
            ++i2;
            ++j2;
            continue;
        }
        // A "!" line meets a context line, or one part holds lines the other
        // has none left to meet: out of sync. The two lines named are the
        // ones that failed to meet.
        const size_t oldLine = i2 < oldPart.size() ? oldPart[i2].inputLine
            : (oldPart.empty() ? index + 2 : oldPart.back().inputLine);
        const size_t newLine = j2 < newPart.size() ? newPart[j2].inputLine
            : (newPart.empty() ? separatorIndex + 1 : newPart.back().inputLine);
        patch.malformed = "Out-of-sync patch, lines " + std::to_string(oldLine) + ","
            + std::to_string(newLine) + " -- mangled text or line numbers, maybe?\n";
        return n;
    }
    if (hunk.oldCount == 0 && hunk.oldStart == 1
        && patch.oldAbsence == SideAbsence::Present) {
        patch.oldAbsence = SideAbsence::Maybe;  // "*** 0 ****", like "-0,0"
    }
    if (hunk.newCount == 0 && hunk.newStart == 1
        && patch.newAbsence == SideAbsence::Present) {
        patch.newAbsence = SideAbsence::Maybe;  // "--- 0 ----"
    }
    // The context runs before the first and after the last change.
    size_t first = 0;
    while (first < hunk.lines.size() && hunk.lines[first].kind == PatchLineKind::Context) {
        ++first;
    }
    size_t after = hunk.lines.size();
    while (after > first && hunk.lines[after - 1].kind == PatchLineKind::Context) {
        --after;
    }
    const bool hasChanges = first < after;
    hunk.leadingContext = hasChanges ? static_cast<int64_t>(first) : 0;
    hunk.trailingContext = hasChanges ? static_cast<int64_t>(hunk.lines.size() - after) : 0;
    patch.hunks.push_back(std::move(hunk));
    return end;
}

size_t PatchReader::ReadNormalHunk(size_t index, FilePatch& patch) {
    const size_t n = m_lines.size();
    patch.format = PatchFormat::Normal;
    NormalCommand command;
    ParseNormalCommand(LineText(index, patch.stripTrailingCr), command);  // checked
    // The sides' ranges: "a" names no old lines, "d" no new ones, an empty
    // range being the line after the one named.
    const int64_t oldList = command.kind == 'a' ? 0 : command.last - command.first + 1;
    const int64_t newList = command.kind == 'd' ? 0 : command.fourth - command.third + 1;
    const auto malformed = [&](const std::string& message) {
        patch.malformed = message;
        return n;
    };
    // One side's "TEXT" lines, each "< " / "> " followed by its text.
    std::vector<PatchLine> oldLines, newLines;
    size_t k = index + 1;
    const auto readSide = [&](int64_t want, char mark, std::vector<PatchLine>& out) -> bool {
        while (static_cast<int64_t>(out.size()) < want) {
            if (k >= n) {
                malformed("unexpected end of file in patch at line " + std::to_string(n)
                    + "\n");
                return false;
            }
            const bool delimited = m_lines[k].delimited;
            const std::string text = LineText(k, patch.stripTrailingCr);
            ++k;
            if (!text.empty() && text[0] == '\\') {
                if (!out.empty()) {  // "\ No newline at end of file"
                    out.back().noNewline = true;
                    if (!out.back().text.empty() && out.back().text.back() == '\n') {
                        out.back().text.pop_back();
                    }
                }
                continue;
            }
            if (text.size() < 2 || text[0] != mark
                || (text[1] != ' ' && text[1] != '\t')) {
                malformed("'" + std::string(1, mark)
                    + "' followed by space or tab expected at line "
                    + std::to_string(k) + " of patch\n");
                return false;
            }
            out.push_back(PatchLine{
                mark == '<' ? PatchLineKind::Delete : PatchLineKind::Insert,
                text.substr(2) + (delimited ? "\n" : ""), false});
        }
        return true;
    };
    if (!readSide(oldList, '<', oldLines)) {
        return n;
    }
    if (command.kind == 'c') {
        if (k >= n) {
            return malformed("unexpected end of file in patch at line " + std::to_string(n)
                + "\n");
        }
        const std::string separator = LineText(k, patch.stripTrailingCr);
        ++k;
        if (separator != "---") {
            return malformed("'---' expected at line " + std::to_string(k)
                + " of patch\n");
        }
    }
    if (!readSide(newList, '>', newLines)) {
        return n;
    }

    PatchHunk hunk;
    hunk.headerLine = static_cast<int64_t>(index) + 1;
    if (command.kind == 'a') {
        hunk.oldStart = command.first + 1;  // the empty range after A
        hunk.newStart = command.third;
    } else if (command.kind == 'd') {
        hunk.oldStart = command.first;
        hunk.newStart = command.third + 1;
    } else {
        hunk.oldStart = command.first;
        hunk.newStart = command.third;
    }
    hunk.oldCount = oldList;
    hunk.newCount = newList;
    hunk.lines = std::move(oldLines);
    for (PatchLine& line : newLines) {
        hunk.lines.push_back(std::move(line));
    }
    patch.hunks.push_back(std::move(hunk));
    return k;
}

std::optional<FilePatch> PatchReader::Next(int64_t strip, bool& garbage) {
    garbage = false;
    const size_t n = m_lines.size();
    // What the options let this run recognise: -u, -c or -n one format
    // alone, none of them any of them -- but a normal diff only when it
    // names a file (an ORIGFILE operand or -n), for it holds no name itself.
    const bool unifiedAllowed = !m_format || *m_format == PatchFormat::Unified;
    const bool contextAllowed = !m_format || *m_format == PatchFormat::Context
        || *m_format == PatchFormat::OldContext;
    const bool normalAllowed = (!m_format && m_normalAllowed)
        || (m_format && *m_format == PatchFormat::Normal);
    std::optional<FilePatch> patch;
    int64_t lastHeader = -1;  // index of this patch's last header line
    size_t lastLine = m_pos;   // the last line consumed as part of the patch
    std::optional<std::string> pendingIndex;
    size_t i = m_pos;
    while (i < n) {
        bool cr = false;
        const std::string text = LineText(i, cr);

        // "Index: NAME": the first line of an RCS-style patch, remembered
        // for the patch its headers begin.
        if (StartsWith(text, "Index: ")) {
            if (patch && !patch->hunks.empty()) {
                break;  // a new patch begins here
            }
            std::string name = HeaderText(text).substr(7);
            while (!name.empty() && (name.front() == ' ' || name.front() == '\t')) {
                name.erase(name.begin());
            }
            while (!name.empty() && (name.back() == ' ' || name.back() == '\t')) {
                name.pop_back();
            }
            // The Index name is a candidate only when the headers gave none,
            // and it is taken as it stands, never reduced to its last
            // component.
            pendingIndex = strip < 0 ? std::optional<std::string>(name)
                : StripFileName(name, strip);
            ++i;
            continue;
        }

        // "diff --git A B": the first line of a git patch, its hunks being
        // unified ones.
        if (unifiedAllowed && StartsWith(text, "diff --git")) {
            if (patch) {
                break;  // the previous patch is over, git hunks or not
            }
            patch = FilePatch{};
            patch->gitDiff = true;
            if (cr) {
                patch->stripTrailingCr = true;
            }
            const int gitStrip = strip < 0 ? 1 : strip;  // -p1 by default
            const std::string gitText = HeaderText(text);
            size_t at = 10;  // strlen("diff --git")
            const std::optional<std::string> nameA = ParseGitName(gitText, at);
            const std::optional<std::string> nameB = ParseGitName(gitText, at);
            patch->oldName = nameA ? StripFileName(*nameA, gitStrip) : std::nullopt;
            patch->newName = nameB ? StripFileName(*nameB, gitStrip) : std::nullopt;
            lastHeader = static_cast<int64_t>(i);
            lastLine = i;
            ++i;
            continue;
        }

        // The git block's own header lines, until the "---" of the hunks.
        if (patch && patch->gitDiff && patch->hunks.empty()) {
            const GitHeader gitHeader = ClassifyGitHeader(text);
            if (gitHeader != GitHeader::None) {
                if (gitHeader == GitHeader::NewFile) {
                    patch->oldAbsence = SideAbsence::Surely;
                } else if (gitHeader == GitHeader::DeletedFile) {
                    patch->newAbsence = SideAbsence::Surely;
                } else if (gitHeader == GitHeader::Rename) {
                    patch->gitRename = true;
                } else if (gitHeader == GitHeader::Copy) {
                    patch->gitCopy = true;
                }
                lastHeader = static_cast<int64_t>(i);
                lastLine = i;
                ++i;
                continue;
            }
        }

        // A "*** NAME [TIME]" line followed by a "--- NAME [TIME]" one: a
        // context diff's file headers, read as the unified ones are.
        if (contextAllowed && StartsWith(text, "***") && !IsStarHunkLine(text)
            && i + 1 < n) {
            bool crNext = false;
            const std::string next = LineText(i + 1, crNext);
            if (StartsWith(next, "---")) {
                const HeaderName oldSide = ParseHeaderName(HeaderText(text).substr(3));
                const HeaderName newSide = ParseHeaderName(HeaderText(next).substr(3));
                if ((oldSide.name || oldSide.absent) && (newSide.name || newSide.absent)) {
                    if (patch && !patch->hunks.empty()) {
                        break;  // a new patch begins here
                    }
                    if (!patch) {
                        patch = FilePatch{};
                    }
                    if (cr || crNext) {
                        patch->stripTrailingCr = true;
                    }
                    patch->oldName = oldSide.name ? StripFileName(*oldSide.name, strip)
                        : std::nullopt;
                    patch->newName = newSide.name ? StripFileName(*newSide.name, strip)
                        : std::nullopt;
                    patch->oldTimeText = oldSide.timeText;
                    patch->newTimeText = newSide.timeText;
                    if (oldSide.absent) {
                        patch->oldAbsence = SideAbsence::Surely;
                    }
                    if (newSide.absent) {
                        patch->newAbsence = SideAbsence::Surely;
                    }
                    if (IsEpochStamp(oldSide.timeText)) {
                        patch->oldAbsence = SideAbsence::Surely;
                    }
                    if (IsEpochStamp(newSide.timeText)) {
                        patch->newAbsence = SideAbsence::Surely;
                    }
                    lastHeader = static_cast<int64_t>(i) + 1;
                    lastLine = i + 1;
                    i += 2;
                    continue;
                }
            }
        }

        // A "--- NAME [TIME]" line followed by a "+++ ..." one.
        if (unifiedAllowed && StartsWith(text, "---") && i + 1 < n) {
            bool crNext = false;
            const std::string next = LineText(i + 1, crNext);
            if (StartsWith(next, "+++")) {
                if (patch && !patch->hunks.empty()) {
                    break;  // a new patch begins here
                }
                if (!patch) {
                    patch = FilePatch{};
                }
                if (cr || crNext) {
                    patch->stripTrailingCr = true;
                }
                const int effStrip = (strip < 0 && patch->gitDiff) ? 1 : strip;
                const HeaderName oldSide = ParseHeaderName(HeaderText(text).substr(3));
                const HeaderName newSide = ParseHeaderName(HeaderText(next).substr(3));
                patch->oldName = oldSide.name ? StripFileName(*oldSide.name, effStrip)
                    : std::nullopt;
                patch->newName = newSide.name ? StripFileName(*newSide.name, effStrip)
                    : std::nullopt;
                patch->oldTimeText = oldSide.timeText;
                patch->newTimeText = newSide.timeText;
                if (oldSide.absent) {
                    patch->oldAbsence = SideAbsence::Surely;
                }
                if (newSide.absent) {
                    patch->newAbsence = SideAbsence::Surely;
                }
                // The epoch time stamp diff -N writes for a file that is not
                // there says the same.
                if (IsEpochStamp(oldSide.timeText)) {
                    patch->oldAbsence = SideAbsence::Surely;
                }
                if (IsEpochStamp(newSide.timeText)) {
                    patch->newAbsence = SideAbsence::Surely;
                }
                lastHeader = static_cast<int64_t>(i) + 1;
                lastLine = i + 1;
                i += 2;
                continue;
            }
        }

        // A unified hunk. With no headers before it, the patch begins here.
        int64_t dummyStart = 0, dummyCount = 0;
        std::string dummyFunction;
        if (unifiedAllowed && ParseHunkHeader(text, dummyStart, dummyCount, dummyStart,
                dummyCount, dummyFunction, nullptr, nullptr)) {
            if (!patch) {
                patch = FilePatch{};
                lastHeader = static_cast<int64_t>(i) - 1;
            }
            if (cr) {
                patch->stripTrailingCr = true;
            }
            const size_t after = ReadHunk(i, *patch);
            lastLine = after - 1;
            i = after;
            continue;
        }

        // A context hunk: its "***************" line.
        if (contextAllowed && IsStarHunkLine(text)) {
            if (!patch) {
                patch = FilePatch{};
                lastHeader = static_cast<int64_t>(i) - 1;
            }
            if (cr) {
                patch->stripTrailingCr = true;
            }
            const size_t after = ReadContextHunk(i, *patch);
            lastLine = after - 1;
            i = after;
            continue;
        }

        // A normal hunk: "A[,B]{a|c|d}C[,D]". An "a" line with nothing after
        // it at the input's end is not one (it names no file to read from).
        if (normalAllowed) {
            NormalCommand command;
            if (ParseNormalCommand(text, command)
                && !(command.kind == 'a' && i + 1 >= n)) {
                if (!patch) {
                    patch = FilePatch{};
                    lastHeader = static_cast<int64_t>(i) - 1;
                }
                if (cr) {
                    patch->stripTrailingCr = true;
                }
                const size_t after = ReadNormalHunk(i, *patch);
                lastLine = after - 1;
                i = after;
                continue;
            }
        }

        if (patch && !patch->hunks.empty()) {
            break;  // the patch is over; this line is the next one's business
        }
        ++i;  // leading text, or a line between the headers and the first hunk
    }

    if (!patch) {
        // No patch in the rest of the input: garbage when none was ever read.
        garbage = (m_pos == 0 && !m_lines.empty());
        m_pos = n;
        return std::nullopt;
    }
    FilePatch result = std::move(*patch);
    if (pendingIndex) {
        result.indexName = pendingIndex;
    }
    // The input lines from the end of the previous patch through the last
    // header line, printed each before a '|' when no file can be found.
    result.leadingText.clear();
    if (lastHeader >= 0 && static_cast<size_t>(lastHeader) >= m_lastEnd) {
        for (size_t j = m_lastEnd; j <= static_cast<size_t>(lastHeader); ++j) {
            result.leadingText += m_lines[j].text;
            if (m_lines[j].delimited) {
                result.leadingText += '\n';
            }
        }
    }
    result.reportLine = result.hunks.empty() ? lastHeader + 1 : result.hunks[0].headerLine;
    m_pos = i;
    m_lastEnd = lastLine + 1;
    return result;
}

void SwapFilePatch(FilePatch& patch) {
    std::swap(patch.oldName, patch.newName);
    std::swap(patch.oldTimeText, patch.newTimeText);
    std::swap(patch.oldAbsence, patch.newAbsence);
    for (PatchHunk& hunk : patch.hunks) {
        std::swap(hunk.oldStart, hunk.newStart);
        std::swap(hunk.oldCount, hunk.newCount);
        // Delete <-> Insert; within each run of changed lines the deletions
        // come first, as in any unified diff.
        std::vector<PatchLine>& lines = hunk.lines;
        size_t j = 0;
        while (j < lines.size()) {
            if (lines[j].kind == PatchLineKind::Context) {
                ++j;
                continue;
            }
            size_t runEnd = j;
            while (runEnd < lines.size() && lines[runEnd].kind != PatchLineKind::Context) {
                ++runEnd;
            }
            std::vector<PatchLine> reordered;
            reordered.reserve(runEnd - j);
            // The Insert lines become the deletions and come first, the
            // Delete lines become the insertions: deletions first in the
            // swapped run, as in any unified diff.
            for (size_t k = j; k < runEnd; ++k) {
                if (lines[k].kind == PatchLineKind::Insert) {
                    reordered.push_back(std::move(lines[k]));
                }
            }
            for (size_t k = j; k < runEnd; ++k) {
                if (lines[k].kind == PatchLineKind::Delete) {
                    reordered.push_back(std::move(lines[k]));
                }
            }
            for (size_t k = 0; k < reordered.size(); ++k) {
                lines[j + k] = std::move(reordered[k]);
                lines[j + k].kind = lines[j + k].kind == PatchLineKind::Delete
                    ? PatchLineKind::Insert : PatchLineKind::Delete;
            }
            j = runEnd;
        }
    }
}

} // namespace Haisos
