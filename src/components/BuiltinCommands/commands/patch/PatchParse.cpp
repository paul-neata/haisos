#include "PatchParse.h"
#include <cctype>
#include <ctime>
#include "BuiltinDate.h"
#include "src/components/Filesystem/FilesystemUtils.h"

namespace Haisos {
namespace {

// One line of the input: its text without the '\n', and whether the input
// ended it with one (the last line may not).
struct InputLine {
    std::string text;
    bool delimited = true;
};

std::vector<InputLine> SplitInputLines(const std::string& text) {
    std::vector<InputLine> lines;
    size_t start = 0;
    while (start <= text.size()) {
        const size_t found = text.find('\n', start);
        if (found == std::string::npos) {
            if (start < text.size()) {
                lines.push_back(InputLine{text.substr(start), false});
            }
            break;
        }
        lines.push_back(InputLine{text.substr(start, found - start), true});
        start = found + 1;
    }
    return lines;
}

size_t strlen(const char* s) {
    size_t n = 0;
    while (s[n] != '\0') {
        ++n;
    }
    return n;
}

bool StartsWith(const std::string& text, const char* prefix) {
    return text.compare(0, strlen(prefix), prefix) == 0;
}

// A decimal number: digits only, empty or anything else rejected.
bool ParseNumber(const std::string& text, size_t begin, size_t end, int64_t& out) {
    if (begin >= end) {
        return false;
    }
    int64_t value = 0;
    for (size_t i = begin; i < end; ++i) {
        if (text[i] < '0' || text[i] > '9') {
            return false;
        }
        if (value > (INT64_MAX - (text[i] - '0')) / 10) {
            return false;
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
std::optional<std::string> StripFileName(const std::string& name, int strip) {
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
    if (static_cast<int>(components.size()) <= strip) {
        return std::nullopt;
    }
    std::string out;
    for (size_t j = strip; j < components.size(); ++j) {
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

} // namespace
// ---- PatchReader ----

PatchReader::PatchReader(std::string text, bool binary)
    : m_lines(SplitInputLines(text))
    , m_binary(binary)
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

// One name side of a "---"/"+++" line: the name (nullopt for /dev/null or
// none), the time text after it, and whether the name said the file is not
// there.
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

bool PatchReader::ParseHunkHeader(const std::string& text, int64_t& oldStart, int64_t& oldCount,
                                  int64_t& newStart, int64_t& newCount, std::string& function) const {
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
    size_t i = 4;
    int64_t rawOldStart = 0, rawNewStart = 0;
    oldCount = 1;
    newCount = 1;
    if (!ParseNumber(text, i, digitsEnd(i), rawOldStart)) {
        return false;
    }
    i = digitsEnd(i);
    if (i < text.size() && text[i] == ',') {
        ++i;
        if (!ParseNumber(text, i, digitsEnd(i), oldCount)) {
            return false;
        }
        i = digitsEnd(i);
    }
    if (i + 1 >= text.size() || text[i] != ' ' || text[i + 1] != '+') {
        return false;
    }
    i += 2;
    if (!ParseNumber(text, i, digitsEnd(i), rawNewStart)) {
        return false;
    }
    i = digitsEnd(i);
    if (i < text.size() && text[i] == ',') {
        ++i;
        if (!ParseNumber(text, i, digitsEnd(i), newCount)) {
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
    // An empty range "-A,0" means "after line A": the hunk starts at A + 1.
    oldStart = oldCount == 0 ? rawOldStart + 1 : rawOldStart;
    newStart = newCount == 0 ? rawNewStart + 1 : rawNewStart;
    return true;
}

size_t PatchReader::ReadHunk(size_t index, FilePatch& patch) {
    PatchHunk hunk;
    hunk.headerLine = static_cast<int64_t>(index) + 1;
    if (!ParseHunkHeader(LineText(index, patch.stripTrailingCr), hunk.oldStart, hunk.oldCount,
            hunk.newStart, hunk.newCount, hunk.function)) {
        return index + 1;  // the caller checked: not reached
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

std::optional<FilePatch> PatchReader::Next(int strip, bool& garbage) {
    garbage = false;
    const size_t n = m_lines.size();
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
            std::string name = text.substr(7);
            while (!name.empty() && (name.front() == ' ' || name.front() == '\t')) {
                name.erase(name.begin());
            }
            while (!name.empty() && (name.back() == ' ' || name.back() == '\t')) {
                name.pop_back();
            }
            pendingIndex = StripFileName(name, strip);
            ++i;
            continue;
        }

        // "diff --git A B": the first line of a git patch.
        if (StartsWith(text, "diff --git")) {
            if (patch) {
                break;  // the previous patch is over, git hunks or not
            }
            patch = FilePatch{};
            patch->gitDiff = true;
            if (cr) {
                patch->stripTrailingCr = true;
            }
            const int gitStrip = strip < 0 ? 1 : strip;  // -p1 by default
            size_t at = 10;  // strlen("diff --git")
            const std::optional<std::string> nameA = ParseGitName(text, at);
            const std::optional<std::string> nameB = ParseGitName(text, at);
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

        // A "--- NAME [TIME]" line followed by a "+++ ..." one.
        if (StartsWith(text, "---") && i + 1 < n) {
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
                const HeaderName oldSide = ParseHeaderName(text.substr(3));
                const HeaderName newSide = ParseHeaderName(next.substr(3));
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

        // A hunk. With no headers before it, the patch begins here.
        int64_t dummyStart = 0, dummyCount = 0;
        std::string dummyFunction;
        if (ParseHunkHeader(text, dummyStart, dummyCount, dummyStart, dummyCount,
                dummyFunction)) {
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
            for (size_t k = j; k < runEnd; ++k) {
                if (lines[k].kind == PatchLineKind::Delete) {
                    reordered.push_back(std::move(lines[k]));
                }
            }
            for (size_t k = j; k < runEnd; ++k) {
                if (lines[k].kind == PatchLineKind::Insert) {
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
