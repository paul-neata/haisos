#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>
#include "BuiltinDate.h"
#include "commands/diff/DiffOutput.h"

namespace Haisos {

namespace {

// The whitespace of C's isspace, as the engine's key modes use it.
bool IsSpaceByte(unsigned char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' || c == '\r';
}

// Line |i| of |text| without its '\n'.
std::string_view LineTextOf(const DiffText& text, size_t i) {
    size_t end = text.lineStarts[i + 1];
    if (end > text.lineStarts[i] && text.bytes[end - 1] == '\n') {
        --end;
    }
    return std::string_view(text.bytes).substr(text.lineStarts[i], end - text.lineStarts[i]);
}

// A line blank for -B: empty, or only whitespace when the whitespace mode is
// TrailingSpace or stronger (-Z, -b, -w).
bool LineIsBlank(std::string_view line, const DiffOutputOptions& options) {
    if (line.empty()) {
        return true;
    }
    switch (options.whiteSpace) {
        case DiffWhiteSpace::TrailingSpace:
        case DiffWhiteSpace::SpaceChange:
        case DiffWhiteSpace::AllSpace:
            for (char c : line) {
                if (!IsSpaceByte(static_cast<unsigned char>(c))) {
                    return false;
                }
            }
            return true;
        case DiffWhiteSpace::None:
        case DiffWhiteSpace::TabExpansion:
            break;
    }
    return false;
}

// A change ignorable under -B: every line it deletes or inserts is blank.
bool ChangeIsIgnorable(const DiffChange& change, const DiffText& a, const DiffText& b,
                       const DiffOutputOptions& options) {
    if (!options.ignoreBlankLines) {
        return false;
    }
    for (int64_t i = 0; i < change.deleted; ++i) {
        if (!LineIsBlank(LineTextOf(a, static_cast<size_t>(change.line0 + i)), options)) {
            return false;
        }
    }
    for (int64_t i = 0; i < change.inserted; ++i) {
        if (!LineIsBlank(LineTextOf(b, static_cast<size_t>(change.line1 + i)), options)) {
            return false;
        }
    }
    return true;
}

// The mark of one normal-format output line: the mark and its separator,
// -T's tab and --suppress-blank-empty applied (no separator on an empty
// line, so it prints as a bare '<').
std::string NormalMark(const DiffOutputOptions& options, char mark, std::string_view text) {
    if (text.empty() && options.suppressBlankEmpty) {
        return std::string(1, mark);
    }
    std::string out(1, mark);
    out += options.initialTab ? '\t' : ' ';
    return out;
}

// The mark of one unified output line: the mark glued to the text, a tab
// between with -T (for a context line the tab replaces the space), and a
// bare mark on an empty line under --suppress-blank-empty (no mark at all
// for an empty context line).
std::string UnifiedMark(const DiffOutputOptions& options, char mark, std::string_view text) {
    if (text.empty() && options.suppressBlankEmpty) {
        return mark == ' ' ? std::string() : std::string(1, mark);
    }
    if (options.initialTab) {
        if (mark == ' ') {
            return std::string(1, '\t');
        }
        std::string out(1, mark);
        out += '\t';
        return out;
    }
    return std::string(1, mark);
}

// The mark of one context-format output line: two characters, the mark and a
// space (a tab with -T), the space dropped on an empty line under
// --suppress-blank-empty (an empty context line prints nothing at all).
std::string ContextMark(const DiffOutputOptions& options, char mark, std::string_view text) {
    if (text.empty() && options.suppressBlankEmpty) {
        return mark == ' ' ? std::string() : std::string(1, mark);
    }
    std::string out(1, mark);
    out += options.initialTab ? '\t' : ' ';
    return out;
}

// One marked output line: |prefix| (the mark, its separator already chosen),
// then the text -- expanded under -t, where a '\r' that more text follows
// makes normal and context output write the prefix again after it -- then
// '\n', and the missing-newline marker when the line is a file's incomplete
// last. Under -t the columns are counted from the start of the text: a tab
// runs to the next multiple of the tab size, '\b' is written and steps back
// one column (dropped at column 0), '\r' is written and resets the column.
void AppendMarkedLine(std::string& out, const DiffOutputOptions& options, const std::string& prefix,
                      bool rewriteAfterCr, std::string_view text, bool incomplete) {
    out += prefix;
    if (options.expandTabs) {
        const size_t tabSize = options.tabSize ? options.tabSize : 8;
        size_t column = 0;
        for (size_t i = 0; i < text.size(); ++i) {
            const char c = text[i];
            if (c == '\t') {
                const size_t to = (column / tabSize + 1) * tabSize;
                out.append(to - column, ' ');
                column = to;
            } else if (c == '\b') {
                if (column > 0) {
                    out += '\b';
                    --column;
                }
            } else if (c == '\r') {
                out += '\r';
                column = 0;
                if (rewriteAfterCr && i + 1 < text.size()) {
                    out += prefix;
                }
            } else {
                out += c;
                ++column;
            }
        }
    } else {
        out.append(text.data(), text.size());
    }
    out += '\n';
    if (incomplete) {
        out += "\\ No newline at end of file\n";
    }
}

// A normal-format range of one side of a change: |anchor| is the 0-based
// line before it, |count| its lines, both 1-based on output. A range of one
// line is the number, of none the line before it.
std::string NormalRangeText(int64_t anchor, int64_t count) {
    if (count <= 0) {
        return std::to_string(anchor);
    }
    if (count == 1) {
        return std::to_string(anchor + 1);
    }
    return std::to_string(anchor + 1) + "," + std::to_string(anchor + count);
}

// The normal format, one hunk per change.
std::string FormatNormal(const std::vector<DiffChange>& script, const DiffText& a, const DiffText& b,
                        const DiffOutputOptions& options) {
    std::string out;
    const int64_t n0 = static_cast<int64_t>(a.LineCount());
    const int64_t n1 = static_cast<int64_t>(b.LineCount());
    for (const DiffChange& change : script) {
        if (ChangeIsIgnorable(change, a, b, options)) {
            continue;
        }
        const char letter = change.deleted == 0 ? 'a' : change.inserted == 0 ? 'd' : 'c';
        out += NormalRangeText(change.line0, change.deleted);
        out += letter;
        out += NormalRangeText(change.line1, change.inserted);
        out += '\n';
        for (int64_t i = 0; i < change.deleted; ++i) {
            const int64_t line = change.line0 + i;
            const std::string_view text = LineTextOf(a, static_cast<size_t>(line));
            AppendMarkedLine(out, options, NormalMark(options, '<', text), true, text,
                             line + 1 == n0 && a.missingNewline);
        }
        if (letter == 'c') {
            out += "---\n";
        }
        for (int64_t i = 0; i < change.inserted; ++i) {
            const int64_t line = change.line1 + i;
            const std::string_view text = LineTextOf(b, static_cast<size_t>(line));
            AppendMarkedLine(out, options, NormalMark(options, '>', text), true, text,
                             line + 1 == n1 && b.missingNewline);
        }
    }
    return out;
}

// The ed script: the changes last first, each an ed command on file 0's
// line numbers, the lines to insert raw between the command and a '.'.
// A line that is exactly '.' cannot be inserted as it stands: it goes in as
// '..', the insertion is closed ('.'), 's/.//' turns the line into '.', and
// the next line resumes the insertion with a new 'a'. The closing '.' is
// printed only when an insertion is open at the end.
std::string FormatEd(const std::vector<DiffChange>& script, const DiffText& a, const DiffText& b,
                     const DiffOutputOptions& options) {
    std::string out;
    for (size_t k = script.size(); k-- > 0;) {
        const DiffChange& change = script[k];
        if (ChangeIsIgnorable(change, a, b, options)) {
            continue;
        }
        const char letter = change.deleted == 0 ? 'a' : change.inserted == 0 ? 'd' : 'c';
        out += NormalRangeText(change.line0, change.deleted);
        out += letter;
        out += '\n';
        if (letter == 'd') {
            continue;
        }
        bool inserting = true;
        for (int64_t i = 0; i < change.inserted; ++i) {
            const std::string_view text = LineTextOf(b, static_cast<size_t>(change.line1 + i));
            if (text == ".") {
                out += "..\n.\ns/.//\n";
                inserting = false;
            } else {
                if (!inserting) {
                    out += "a\n";
                    inserting = true;
                }
                AppendMarkedLine(out, options, std::string(), false, text, false);
            }
        }
        if (inserting) {
            out += ".\n";
        }
    }
    return out;
}

// One hunk of the unified/context formats: a run of the script's indexes
// [first, last] whose changes are to be printed as one hunk, and the two
// files' ranges it covers.
struct DiffHunk {
    size_t first = 0, last = 0;
    int64_t start0 = 0, end0 = 0, start1 = 0, end1 = 0;
};

// The script's changes grouped as unified/context hunks: a change joins the
// hunk being built when the unchanged lines between it and the hunk's last
// change are fewer than 2 * context + 1 -- fewer than context, under -B,
// when the joining change is ignorable. A hunk with no change that
// survives -B is left out. Each hunk shows up to context unchanged lines
// before its first and after its last change, clamped to the file.
std::vector<DiffHunk> DiffHunksOf(const std::vector<DiffChange>& script, const DiffText& a,
                                  const DiffText& b, const DiffOutputOptions& options) {
    const int64_t context = options.context > 0 ? options.context : 0;
    const int64_t n0 = static_cast<int64_t>(a.LineCount());
    const int64_t n1 = static_cast<int64_t>(b.LineCount());
    std::vector<DiffHunk> hunks;
    size_t first = 0;
    for (size_t i = 1; i <= script.size(); ++i) {
        const bool closing = i == script.size();
        if (!closing) {
            const int64_t gap = script[i].line0 - (script[i - 1].line0 + script[i - 1].deleted);
            const int64_t threshold =
                ChangeIsIgnorable(script[i], a, b, options) ? context : 2 * context + 1;
            if (gap < threshold) {
                continue;
            }
        }
        DiffHunk hunk;
        hunk.first = first;
        hunk.last = i - 1;
        hunk.start0 = script[hunk.first].line0;
        hunk.start1 = script[hunk.first].line1;
        bool real = false;
        for (size_t k = hunk.first; k <= hunk.last; ++k) {
            const DiffChange& change = script[k];
            if (!ChangeIsIgnorable(change, a, b, options)) {
                real = true;
            }
            hunk.start0 = std::min(hunk.start0, change.line0);
            hunk.start1 = std::min(hunk.start1, change.line1);
            hunk.end0 = std::max(hunk.end0, change.line0 + change.deleted);
            hunk.end1 = std::max(hunk.end1, change.line1 + change.inserted);
        }
        if (real) {
            hunk.start0 = std::max(int64_t{0}, hunk.start0 - context);
            hunk.start1 = std::max(int64_t{0}, hunk.start1 - context);
            hunk.end0 = std::min(n0, hunk.end0 + context);
            hunk.end1 = std::min(n1, hunk.end1 + context);
            hunks.push_back(hunk);
        }
        first = i;
    }
    return hunks;
}

} // namespace

bool DiffHasRealChanges(const std::vector<DiffChange>& changes, const DiffText& a,
                        const DiffText& b, const DiffOutputOptions& options) {
    for (const DiffChange& change : changes) {
        if (!ChangeIsIgnorable(change, a, b, options)) {
            return true;
        }
    }
    return false;
}
namespace {

// A file name in a header: in double quotes when it is empty or holds a
// space, '"', '\', a byte below 0x20 or a byte 0x80 or above, with the
// escapes \a \b \t \n \v \f \r \" \\ and every other such byte as 3-digit
// octal; 0x7f and everything else as is. Labels never go through here.
std::string QuoteHeaderName(const std::string& name) {
    bool quote = name.empty();
    for (char c : name) {
        const unsigned char byte = static_cast<unsigned char>(c);
        if (c == ' ' || c == '"' || c == '\\' || byte < 0x20 || byte >= 0x80) {
            quote = true;
            break;
        }
    }
    if (!quote) {
        return name;
    }
    std::string out = "\"";
    for (char c : name) {
        const unsigned char byte = static_cast<unsigned char>(c);
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\a': out += "\\a"; break;
            case '\b': out += "\\b"; break;
            case '\t': out += "\\t"; break;
            case '\n': out += "\\n"; break;
            case '\v': out += "\\v"; break;
            case '\f': out += "\\f"; break;
            case '\r': out += "\\r"; break;
            default:
                if (byte < 0x20 || byte >= 0x80) {
                    char buffer[8];
                    std::snprintf(buffer, sizeof(buffer), "\\%03o", byte);
                    out += buffer;
                } else {
                    out += c;
                }
        }
    }
    out += "\"";
    return out;
}

// One file's half of a unified/context header: its label as given, or its
// quoted name and modification time.
std::string HeaderFileText(const DiffHeaderFile& file, const char* timeFormat) {
    if (file.label) {
        return *file.label;
    }
    return QuoteHeaderName(file.name) + "\t" + FormatDateTime(timeFormat, file.modificationTime, false);
}

// The unified formats' header times.
constexpr const char* kUnifiedTimeFormat = "%Y-%m-%d %H:%M:%S.%N %z";
constexpr const char* kContextTimeFormat = "%a %b %e %T %Y";

// A unified-format range: one line is its number, none is "n,0" (n the
// line before it, 0 at the start), else "first,count".
std::string UnifiedRangeText(int64_t start, int64_t count) {
    if (count == 0) {
        return std::to_string(start) + ",0";
    }
    if (count == 1) {
        return std::to_string(start + 1);
    }
    return std::to_string(start + 1) + "," + std::to_string(count);
}

// The unified format: the two header lines, then each hunk as "@@ -R0 +R1 @@"
// with context lines marked ' ', deletions '-' and insertions '+' glued to
// the text.
std::string FormatUnified(const std::vector<DiffChange>& script, const DiffText& a, const DiffText& b,
                         const DiffHeaderFile& h0, const DiffHeaderFile& h1,
                         const DiffOutputOptions& options) {
    const std::vector<DiffHunk> hunks = DiffHunksOf(script, a, b, options);
    if (hunks.empty()) {
        return std::string();
    }
    std::string out = "--- " + HeaderFileText(h0, kUnifiedTimeFormat) + "\n";
    out += "+++ " + HeaderFileText(h1, kUnifiedTimeFormat) + "\n";
    const int64_t n0 = static_cast<int64_t>(a.LineCount());
    const int64_t n1 = static_cast<int64_t>(b.LineCount());
    for (const DiffHunk& hunk : hunks) {
        out += "@@ -" + UnifiedRangeText(hunk.start0, hunk.end0 - hunk.start0);
        out += " +" + UnifiedRangeText(hunk.start1, hunk.end1 - hunk.start1) + " @@\n";
        int64_t p0 = hunk.start0, p1 = hunk.start1;
        const auto contextLine = [&](int64_t i, int64_t j) {
            const std::string_view text = LineTextOf(a, static_cast<size_t>(i));
            const bool incomplete = (i + 1 == n0 && a.missingNewline) || (j + 1 == n1 && b.missingNewline);
            AppendMarkedLine(out, options, UnifiedMark(options, ' ', text), false, text, incomplete);
        };
        for (size_t k = hunk.first; k <= hunk.last; ++k) {
            const DiffChange& change = script[k];
            while (p0 < change.line0) {
                contextLine(p0, p1);
                ++p0;
                ++p1;
            }
            for (int64_t i = 0; i < change.deleted; ++i) {
                const int64_t line = change.line0 + i;
                const std::string_view text = LineTextOf(a, static_cast<size_t>(line));
                AppendMarkedLine(out, options, UnifiedMark(options, '-', text), false, text,
                                 line + 1 == n0 && a.missingNewline);
            }
            for (int64_t i = 0; i < change.inserted; ++i) {
                const int64_t line = change.line1 + i;
                const std::string_view text = LineTextOf(b, static_cast<size_t>(line));
                AppendMarkedLine(out, options, UnifiedMark(options, '+', text), false, text,
                                 line + 1 == n1 && b.missingNewline);
            }
            p0 = change.line0 + change.deleted;
            p1 = change.line1 + change.inserted;
        }
        while (p0 < hunk.end0) {
            contextLine(p0, p1);
            ++p0;
            ++p1;
        }
    }
    return out;
}

// A context-format range: "a,b", or just "b" when it has one line or none
// (b then the line before it).
std::string ContextRangeText(int64_t start, int64_t count) {
    if (count >= 2) {
        return std::to_string(start + 1) + "," + std::to_string(start + count);
    }
    return std::to_string(start + count);
}

// The context format: the two header lines, then each hunk as
// "***************", the file-0 half ("*** R0 ****", its lines when the
// hunk deletes something), the file-1 half ("--- R1 ----", its lines when
// it inserts something).
std::string FormatContext(const std::vector<DiffChange>& script, const DiffText& a, const DiffText& b,
                         const DiffHeaderFile& h0, const DiffHeaderFile& h1,
                         const DiffOutputOptions& options) {
    const std::vector<DiffHunk> hunks = DiffHunksOf(script, a, b, options);
    if (hunks.empty()) {
        return std::string();
    }
    std::string out = "*** " + HeaderFileText(h0, kContextTimeFormat) + "\n";
    out += "--- " + HeaderFileText(h1, kContextTimeFormat) + "\n";
    const int64_t n0 = static_cast<int64_t>(a.LineCount());
    const int64_t n1 = static_cast<int64_t>(b.LineCount());
    for (const DiffHunk& hunk : hunks) {
        out += "***************\n";
        out += "*** " + ContextRangeText(hunk.start0, hunk.end0 - hunk.start0) + " ****\n";
        bool deletes = false, inserts = false;
        for (size_t k = hunk.first; k <= hunk.last; ++k) {
            deletes = deletes || script[k].deleted > 0;
            inserts = inserts || script[k].inserted > 0;
        }
        if (deletes) {
            int64_t p0 = hunk.start0;
            for (size_t k = hunk.first; k <= hunk.last; ++k) {
                const DiffChange& change = script[k];
                while (p0 < change.line0) {
                    const std::string_view text = LineTextOf(a, static_cast<size_t>(p0));
                    AppendMarkedLine(out, options, ContextMark(options, ' ', text), true, text,
                                     p0 + 1 == n0 && a.missingNewline);
                    ++p0;
                }
                for (int64_t i = 0; i < change.deleted; ++i) {
                    const int64_t line = change.line0 + i;
                    const std::string_view text = LineTextOf(a, static_cast<size_t>(line));
                    AppendMarkedLine(out, options,
                                     ContextMark(options, change.inserted == 0 ? '-' : '!', text),
                                     true, text, line + 1 == n0 && a.missingNewline);
                }
                p0 = change.line0 + change.deleted;
            }
            while (p0 < hunk.end0) {
                const std::string_view text = LineTextOf(a, static_cast<size_t>(p0));
                AppendMarkedLine(out, options, ContextMark(options, ' ', text), true, text,
                                 p0 + 1 == n0 && a.missingNewline);
                ++p0;
            }
        }
        out += "--- " + ContextRangeText(hunk.start1, hunk.end1 - hunk.start1) + " ----\n";
        if (inserts) {
            int64_t p1 = hunk.start1;
            for (size_t k = hunk.first; k <= hunk.last; ++k) {
                const DiffChange& change = script[k];
                while (p1 < change.line1) {
                    const std::string_view text = LineTextOf(b, static_cast<size_t>(p1));
                    AppendMarkedLine(out, options, ContextMark(options, ' ', text), true, text,
                                     p1 + 1 == n1 && b.missingNewline);
                    ++p1;
                }
                for (int64_t i = 0; i < change.inserted; ++i) {
                    const int64_t line = change.line1 + i;
                    const std::string_view text = LineTextOf(b, static_cast<size_t>(line));
                    AppendMarkedLine(out, options,
                                     ContextMark(options, change.deleted == 0 ? '+' : '!', text),
                                     true, text, line + 1 == n1 && b.missingNewline);
                }
                p1 = change.line1 + change.inserted;
            }
            while (p1 < hunk.end1) {
                const std::string_view text = LineTextOf(b, static_cast<size_t>(p1));
                AppendMarkedLine(out, options, ContextMark(options, ' ', text), true, text,
                                 p1 + 1 == n1 && b.missingNewline);
                ++p1;
            }
        }
    }
    return out;
}

} // namespace

std::string FormatDiff(const std::vector<DiffChange>& script, const DiffText& a, const DiffText& b,
                       const DiffHeaderFile& h0, const DiffHeaderFile& h1,
                       const DiffOutputOptions& options) {
    switch (options.style) {
        case DiffStyle::Normal: return FormatNormal(script, a, b, options);
        case DiffStyle::Unified: return FormatUnified(script, a, b, h0, h1, options);
        case DiffStyle::Context: return FormatContext(script, a, b, h0, h1, options);
        case DiffStyle::Ed: return FormatEd(script, a, b, options);
    }
    return std::string();
}

} // namespace Haisos
