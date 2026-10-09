#include "commands/rg/RgSearch.h"
#include <algorithm>
#include <cstring>
#include <memory>
#include <optional>
#include <vector>
#include "BuiltinText.h"
#include "commands/grep/GrepContext.h"
#include "interfaces/IFileIO.h"
#include "interfaces/IProcess.h"

namespace Haisos {
namespace {

// ripgrep's read buffer.
constexpr size_t kRgReadChunk = 65536;

// rg's colours, byte for byte: each piece wrapped in its own SGR run, reset
// with ESC [ 0 m before it, as ripgrep's printer does.
std::string ColorPath(const RgSettings& settings, const std::string& text) {
    return settings.color == 2 ? "\x1b[0m\x1b[35m" + text + "\x1b[0m" : text;
}

std::string ColorLineNumber(const RgSettings& settings, const std::string& text) {
    return settings.color == 2 ? "\x1b[0m\x1b[32m" + text + "\x1b[0m" : text;
}

std::string ColorNumber(const RgSettings& settings, const std::string& text) {
    return settings.color == 2 ? "\x1b[0m" + text + "\x1b[0m" : text;
}

std::string ColorMatch(const RgSettings& settings, std::string_view text) {
    return settings.color == 2
        ? "\x1b[0m\x1b[1m\x1b[31m" + std::string(text) + "\x1b[0m"
        : std::string(text);
}

// A matching line with every match highlighted, as ripgrep prints it. A
// context line is never highlighted (with -v the selected lines hold no
// matches, so a selected one always does).
std::string Highlighted(const RgSettings& settings, const GrepMatcher& matcher,
                        std::string_view text) {
    if (settings.color != 2) {
        return std::string(text);
    }
    std::string out;
    size_t pos = 0;
    size_t lastEnd = 0;
    while (pos <= text.size()) {
        size_t begin = 0;
        size_t end = 0;
        if (!matcher.Find(text, pos, begin, end)) {
            break;
        }
        if (end > begin) {
            out.append(text.substr(lastEnd, begin - lastEnd));
            out += ColorMatch(settings, text.substr(begin, end - begin));
            lastEnd = end;
            pos = end;
        } else {
            pos = begin + 1;
        }
    }
    out.append(text.substr(lastEnd));
    return out;
}

// Everything rg prints while searching: the prefixes, the headings, the
// group separators, the counts and paths, the binary-file message. One
// instance spans the run; each input begins with its own name.
class RgPrinter {
public:
    RgPrinter(BuiltinContext& context, const RgSettings& settings, const GrepMatcher& matcher,
              bool showNames, bool heading, bool lineNumbers)
        : m_context(context)
        , m_settings(settings)
        , m_matcher(matcher)
        , m_showNames(showNames)
        , m_heading(heading)
        , m_lineNumbers(lineNumbers) {}

    const std::string& Name() const { return m_name; }

    void BeginFile(const std::string& name) {
        m_name = name;
        m_headingDone = false;
    }

    // One output line. |selected|: a matching line (':' after each field);
    // a context line's separators are '-'. |matchText|: the line is one
    // match of -o, starting |matchBegin| bytes into its line, for
    // --column and -b.
    void Line(std::string_view text, uint64_t lineNumber, uint64_t byteOffset, bool selected,
              bool matchText, size_t matchBegin) {
        if (m_heading && !m_headingDone) {
            if (m_anythingPrinted) {
                Out("\n");
            }
            Out(ColorPath(m_settings, m_name) + "\n");
            m_headingDone = true;
        }
        const char sep = selected ? ':' : '-';
        std::string out;
        if (!m_heading && m_showNames) {
            out += ColorPath(m_settings, m_name);
            out += m_settings.nullSeparator ? '\0' : sep;
        }
        if (m_lineNumbers) {
            out += ColorLineNumber(m_settings, std::to_string(lineNumber)) + sep;
        }
        if (m_settings.columns && selected && !m_settings.invert) {
            // The column of the line's first match (of the one match -o
            // prints); -v selects lines without matches, so there is none.
            const size_t begin = matchText ? matchBegin : FirstMatchBegin(text);
            out += ColorNumber(m_settings, std::to_string(begin + 1)) + sep;
        }
        if (m_settings.byteOffsets) {
            // With -o, the offset of the match, not of its line.
            out += ColorNumber(m_settings, std::to_string(byteOffset)) + sep;
        }
        // -M: a line at least as long as the limit prints in its place.
        if (m_settings.maxColumns && text.size() >= *m_settings.maxColumns) {
            out += selected ? "[Omitted long matching line]" : "[Omitted long context line]";
        } else if (matchText) {
            out += ColorMatch(m_settings, text);
        } else {
            out += Highlighted(m_settings, m_matcher, text);
        }
        out += '\n';
        Out(out);
    }

    // The separator between non-contiguous context groups ("--"), never
    // coloured, whatever else is.
    void Separator() {
        if (!m_settings.contextSeparator) {
            return;
        }
        Out(m_settings.separator + "\n");
    }

    // [path:]N, the count of a whole file (-c, --count-matches).
    void Count(const std::string& name, uint64_t count) {
        std::string out;
        if (m_showNames) {
            out += ColorPath(m_settings, name);
            out += m_settings.nullSeparator ? '\0' : ':';
        }
        out += std::to_string(count);
        out += '\n';
        Out(out);
    }

    // A path on a line of its own (-l, --files-without-match, --files).
    void Path(const std::string& name) {
        Out(ColorPath(m_settings, name) + std::string(1, m_settings.nullSeparator ? '\0' : '\n'));
    }

    // What a binary input says when its first match ends its search. Always
    // in the path: prefix form, headings included; the blank line a heading
    // would start with still comes first.
    void BinaryMessage(uint64_t nulOffset) {
        if (m_heading && !m_headingDone) {
            if (m_anythingPrinted) {
                Out("\n");
            }
            m_headingDone = true;
        }
        std::string out;
        if (m_showNames) {
            out += m_name;
            out += m_settings.nullSeparator ? '\0' : ':';
            out += ' ';
        }
        out += "binary file matches (found \"\\0\" byte around offset "
            + std::to_string(nulOffset) + ")\n";
        Out(out);
    }

private:
    size_t FirstMatchBegin(std::string_view text) {
        size_t begin = 0;
        size_t end = 0;
        if (m_matcher.Find(text, 0, begin, end)) {
            return begin;
        }
        return 0;
    }

    void Out(const std::string& text) {
        m_context.Out(text);
        if (m_settings.lineBuffered) {
            m_context.Flush();
        }
        m_anythingPrinted = true;
    }

    BuiltinContext& m_context;
    const RgSettings& m_settings;
    const GrepMatcher& m_matcher;
    bool m_showNames;
    bool m_heading;
    bool m_lineNumbers;
    std::string m_name;
    bool m_headingDone = false;
    bool m_anythingPrinted = false;
};

// What one searched input came to.
struct FileOutcome {
    uint64_t selected = 0;  // matching lines
    uint64_t matches = 0;   // matches (--count-matches)
    bool binaryMatched = false;
    bool stopped = false;   // the input's search ended early (a match, a stop)
    bool error = false;
};

// Searches one input: ripgrep's chunked reader (a carry, line numbers, byte
// offsets), its binary rules and its -m rules. |binaryAsOperand|: a NUL
// makes the input binary (an operand, stdin, or --binary) instead of ending
// it silently (a file met while walking). |initialData|: bytes already read
// (the one read that decided standard input would be searched).
FileOutcome SearchFile(BuiltinContext& context, const RgSettings& settings, const GrepMatcher& matcher,
                       IFileDescriptor& input, RgPrinter& printer, GrepContext* grepContext,
                       const std::string& initialData, bool binaryAsOperand) {
    FileOutcome result;
    const bool counting = settings.mode == RgMode::Count || settings.mode == RgMode::CountMatches;
    const bool listing = settings.mode == RgMode::ListMatching || settings.mode == RgMode::ListNonMatching;

    uint64_t lineNo = 0;
    uint64_t offset = 0;  // the byte offset of the next line's first byte
    bool binary = false;
    std::optional<uint64_t> firstNul;  // the first NUL of a binary input
    bool binaryMatched = false;
    bool limitReached = false;  // -m used up, trailing context still owed

    // One line (without its terminator). Returns whether to stop reading.
    const auto processLine = [&](std::string_view line) -> bool {
        ++lineNo;
        if (limitReached) {
            // -m with context: only as far as the trailing context reaches;
            // a matching line inside it prints as a match (rg, not grep).
            if (grepContext) {
                const bool selected = matcher.Matches(line) != settings.invert;
                grepContext->Line(line, lineNo, offset, selected, /*extendsAfter=*/false);
                return !grepContext->AfterPending();
            }
            return true;
        }
        const bool selected = matcher.Matches(line) != settings.invert;
        if (selected) {
            ++result.selected;
            if (settings.mode == RgMode::CountMatches) {
                // Every match of the line, not the line.
                size_t pos = 0;
                while (pos <= line.size()) {
                    size_t begin = 0;
                    size_t end = 0;
                    if (!matcher.Find(line, pos, begin, end)) {
                        break;
                    }
                    if (end > begin) {
                        ++result.matches;
                    }
                    pos = end > begin ? end : begin + 1;
                }
            }
            if (settings.mode == RgMode::Quiet) {
                result.stopped = true;
                return true;
            }
            if (binary) {
                // The first match ends a binary input, the counts and lists
                // excepted: they keep going to the end of the file.
                binaryMatched = true;
                if (!counting && !listing) {
                    return true;
                }
            }
            if (listing) {
                return true;
            }
            if (counting) {
                // The counted line counts towards -m too.
            } else if (grepContext) {
                grepContext->Line(line, lineNo, offset, true);
            } else if (settings.onlyMatching) {
                size_t pos = 0;
                while (pos <= line.size()) {
                    size_t begin = 0;
                    size_t end = 0;
                    if (!matcher.Find(line, pos, begin, end)) {
                        break;
                    }
                    if (end > begin) {
                        printer.Line(line.substr(begin, end - begin), lineNo, offset + begin,
                                     /*selected=*/true, /*matchText=*/true, begin);
                    }
                    pos = end > begin ? end : begin + 1;
                }
            } else {
                printer.Line(line, lineNo, offset, true, /*matchText=*/false, 0);
            }
            if (settings.maxCount && result.selected >= *settings.maxCount) {
                if (grepContext && grepContext->AfterPending()) {
                    limitReached = true;
                    return false;
                }
                return true;
            }
            return false;
        }
        // A binary input prints nothing further, its message excepted; it
        // joins no context either.
        if (grepContext && !binary) {
            grepContext->Line(line, lineNo, offset, false);
        }
        return false;
    };

    std::string data = initialData;  // the carry: the partial line so far
    bool eof = false;
    // -m 0: the input is not read at all.
    const bool readAtAll = !settings.maxCount || *settings.maxCount > 0;

    if (readAtAll) {
        std::vector<char> buffer(kRgReadChunk);
        while (true) {
            if (context.StopRequested()) {
                result.stopped = true;
                break;
            }
            const ssize_t n = input.Read(buffer.data(), buffer.size());
            if (n == kIOInterrupted) {
                result.stopped = true;
                break;
            }
            if (n < 0) {
                if (!settings.noMessages) {
                    context.ErrorText("rg: " + printer.Name() + ": IO error for operation on "
                        + printer.Name() + ": Input/output error (os error 5)\n");
                }
                result.error = true;
                break;
            }
            if (n == 0) {
                eof = true;
            } else {
                data.append(buffer.data(), static_cast<size_t>(n));
                if (!binary && !settings.text
                    && std::memchr(buffer.data(), '\0', static_cast<size_t>(n)) != nullptr) {
                    firstNul = offset + data.find('\0');
                    if (!binaryAsOperand) {
                        // A binary file met while walking ends silently: lines
                        // already printed stay, nothing else comes.
                        data.clear();
                        break;
                    }
                    // An operand (or stdin, or --binary): from here a NUL
                    // ends a line too, nothing is printed, and the first
                    // match ends the input with ripgrep's message.
                    binary = true;
                }
            }
            // Split what is held into complete lines -- at the end of input
            // too, so a carry already read (the standard input's deciding
            // read) is split before its last line is taken as unterminated.
            // Once binary, a NUL ends a line too (for matching, counting
            // and line numbers).
            size_t pos = 0;
            bool stopInput = false;
            while (pos < data.size()) {
                if (context.StopRequested()) {
                    result.stopped = true;
                    break;
                }
                size_t end = data.find('\n', pos);
                if (binary) {
                    const size_t nul = data.find('\0', pos);
                    if (nul != std::string::npos && (end == std::string::npos || nul < end)) {
                        end = nul;
                    }
                }
                if (end == std::string::npos) {
                    break;
                }
                stopInput = processLine(std::string_view(data).substr(pos, end - pos));
                offset += end - pos + 1;
                pos = end + 1;
                if (stopInput || result.stopped) {
                    break;
                }
            }
            if (!result.stopped) {
                data.erase(0, pos);
            }
            if (eof || stopInput || result.stopped) {
                break;
            }
        }
    }

    if (eof && !data.empty() && !result.stopped) {
        // A last line without its terminator is printed with one added.
        processLine(data);
    }

    // A binary operand (or --binary, or stdin) that matched says so, in the
    // modes that print lines; the counts and lists need no message.
    if (binaryMatched && settings.mode == RgMode::Lines) {
        printer.BinaryMessage(*firstNul);
    }
    result.binaryMatched = binaryMatched;
    return result;
}

// The os error text of a path that cannot be opened, rg's wording.
const char* OpenFailureText(InputOpenFailure failure) {
    switch (failure) {
        case InputOpenFailure::Missing: return "No such file or directory (os error 2)";
        case InputOpenFailure::Directory: return "Is a directory (os error 21)";
        case InputOpenFailure::Denied: return "Permission denied (os error 13)";
        case InputOpenFailure::BadDescriptor: return "No such file or directory (os error 2)";
        case InputOpenFailure::None: break;
    }
    return "";
}

} // namespace

RgResult RgSearch(BuiltinContext& context, const RgSettings& settings, const GrepMatcher& matcher,
                  const std::vector<std::string>& paths) {
    RgResult result;
    const bool outIsTerminal = context.OutIsTerminal();
    const std::shared_ptr<IEnvironment> environment = context.Process().GetEnvironment();

    // No paths given: standard input when it is not a terminal and its
    // first read returns data (a descriptor has no type in Haisos, so an
    // input that is empty at once -- /dev/null's case -- means no stdin:
    // documented), else the implicit path, `.`.
    bool stdinSearched = false;
    std::string stdinData;
    std::shared_ptr<IFileDescriptor> stdinDescriptor;
    if (paths.empty() && settings.mode != RgMode::Files) {
        stdinDescriptor = context.IO().GetDescriptor(IFileIO::kStdIn);
        if (stdinDescriptor && !stdinDescriptor->IsTerminal()) {
            std::vector<char> buffer(kRgReadChunk);
            const ssize_t n = stdinDescriptor->Read(buffer.data(), buffer.size());
            if (n > 0) {
                stdinSearched = true;
                stdinData.assign(buffer.data(), static_cast<size_t>(n));
            } else if (n == kIOInterrupted) {
                result.stopped = true;
                return result;
            }
        }
    }
    std::vector<std::string> effective = paths;
    bool implicitDot = false;
    if (paths.empty() && !stdinSearched) {
        effective = {"."};
        implicitDot = true;
    }

    // Names are shown unless exactly one operand is not a directory, or
    // standard input is searched (-H and -I override).
    bool showNames = true;
    if (settings.withFilename) {
        showNames = *settings.withFilename;
    } else if (stdinSearched) {
        showNames = false;
    } else if (effective.size() == 1) {
        FileStatus status;
        showNames = context.IO().Stat(effective[0], status) == 0
            && status.type == DirectoryEntryType::Dir;
    }
    const bool heading = settings.heading.value_or(outIsTerminal && showNames);
    const bool lineNumbers = settings.lineNumbers.value_or(outIsTerminal);
    // --color=auto: a terminal, TERM set and not dumb, NO_COLOR unset or empty.
    bool useColor = settings.color == 2;
    if (settings.color == 1 && outIsTerminal && environment) {
        const auto term = environment->GetVariable("TERM");
        if (term && *term != "dumb") {
            const auto noColor = environment->GetVariable("NO_COLOR");
            if (!noColor || noColor->empty()) {
                useColor = true;
            }
        }
    }
    RgSettings search = settings;
    search.color = useColor ? 2 : 0;
    RgPrinter printer(context, search, matcher, showNames, heading, lineNumbers);

    // The run's before/after context, one for the whole run. Headings have
    // no separators between files (the blank line and the path separate
    // them); -o prints no context lines, so it joins none.
    std::optional<GrepContext> grepContext;
    if (settings.contextEnabled && settings.mode == RgMode::Lines && !settings.onlyMatching) {
        grepContext.emplace(settings.before, settings.after, /*enabled=*/true,
                            /*separatorAcrossFiles=*/!heading, nullptr, nullptr);
    }
    GrepContext* const grepContextPtr = grepContext ? &*grepContext : nullptr;

    // Each input joins the run's context under its own name and numbering.
    const auto beginFile = [&](const std::string& name) {
        printer.BeginFile(name);
        if (grepContextPtr) {
            grepContextPtr->SetPrinters(
                [&](std::string_view text, uint64_t lineNumber, uint64_t byteOffset, bool selected) {
                    printer.Line(text, lineNumber, byteOffset, selected, /*matchText=*/false, 0);
                },
                [&]() { printer.Separator(); });
            grepContextPtr->BeginFile();
        }
    };

    uint64_t filesSearched = 0;
    bool stopped = false;
    bool quietMatched = false;

    // The counts and lists of one searched input, after its search.
    const auto finishFile = [&](const FileOutcome& outcome) {
        if (outcome.error) {
            result.error = true;
        }
        switch (settings.mode) {
            case RgMode::Count:
            case RgMode::CountMatches:
                if (outcome.selected > 0 || settings.includeZero) {
                    printer.Count(printer.Name(),
                        settings.mode == RgMode::CountMatches ? outcome.matches : outcome.selected);
                    result.matched = result.matched || outcome.selected > 0;
                }
                break;
            case RgMode::ListMatching:
                if (outcome.selected > 0) {
                    printer.Path(printer.Name());
                    result.matched = true;
                }
                break;
            case RgMode::ListNonMatching:
                if (outcome.selected == 0) {
                    printer.Path(printer.Name());
                    result.matched = true;
                }
                break;
            case RgMode::Lines:
            case RgMode::Quiet:
            case RgMode::Files:
                result.matched = result.matched || outcome.selected > 0;
                break;
        }
    };

    // One path to search: a file, a device, whatever is not a directory.
    // |operand|: named on the command line (a NUL makes it binary, and its
    // open failure, when it is the only one, is rg's longer message).
    const auto searchFile = [&](const std::string& file, bool operand) {
        InputOpenFailure failure = InputOpenFailure::None;
        auto input = OpenInputOperand(context, file, failure);
        if (!input) {
            if (!settings.noMessages) {
                const std::string text = OpenFailureText(failure);
                if (operand && effective.size() == 1) {
                    context.ErrorText("rg: " + file + ": IO error for operation on " + file
                        + ": " + text + "\n");
                } else {
                    context.ErrorText("rg: " + file + ": " + text + "\n");
                }
            }
            result.error = true;
            return;
        }
        ++filesSearched;
        beginFile(file);
        FileOutcome outcome = SearchFile(context, search, matcher, *input, printer, grepContextPtr,
                                         /*initialData=*/std::string(), operand || settings.binary);
        if (outcome.stopped) {
            stopped = true;
            quietMatched = quietMatched || outcome.selected > 0;
        }
        finishFile(outcome);
    };

    // The operand as given: its children are it + '/' + their names, with
    // no '/' added when it already ends with one; the implicit `.` has no
    // prefix at all, so its entries print without `./`.
    const auto childPrefix = [&](const std::string& operand) {
        if (implicitDot && operand == ".") {
            return std::string();
        }
        if (!operand.empty() && operand.back() == '/') {
            return operand;
        }
        return operand + "/";
    };

    // The recursive walk, depth first: the entries of a directory minus
    // . and .., in byte order, files and directories together (rg's
    // --sort path order, without its parallelism). A device met while
    // walking is skipped.
    const auto walk = [&](const auto& self, const std::string& dirPath,
                          const std::string& prefix) -> void {
        if (context.StopRequested() || stopped) {
            return;
        }
        std::vector<DirectoryEntry> children;
        for (const auto& entry : context.IO().ReadDirectory(dirPath)) {
            if (entry.name != "." && entry.name != "..") {
                children.push_back(entry);
            }
        }
        std::sort(children.begin(), children.end(),
            [](const DirectoryEntry& a, const DirectoryEntry& b) { return a.name < b.name; });
        for (const auto& entry : children) {
            if (context.StopRequested() || stopped) {
                return;
            }
            const std::string child = prefix + entry.name;
            if (entry.type == DirectoryEntryType::Dir) {
                self(self, child, child + "/");
            } else if (entry.type == DirectoryEntryType::CharDevice) {
                continue;
            } else if (settings.mode == RgMode::Files) {
                printer.Path(child);
                result.matched = true;
            } else {
                searchFile(child, /*operand=*/false);
            }
        }
    };

    if (stdinSearched) {
        ++filesSearched;
        beginFile("<stdin>");
        FileOutcome outcome = SearchFile(context, search, matcher, *stdinDescriptor, printer,
                                         grepContextPtr, stdinData, /*binaryAsOperand=*/true);
        if (outcome.stopped) {
            stopped = true;
            quietMatched = quietMatched || outcome.selected > 0;
        }
        finishFile(outcome);
    }

    for (const auto& path : effective) {
        if (context.StopRequested() || stopped) {
            break;
        }
        FileStatus status;
        const bool haveStat = context.IO().Stat(path, status) == 0;
        if (haveStat && status.type == DirectoryEntryType::Dir) {
            walk(walk, path, childPrefix(path));
            continue;
        }
        if (!haveStat) {
            // A path that is not there: rg's message, its longer form when
            // it is the only operand.
            if (!settings.noMessages) {
                if (effective.size() == 1) {
                    context.ErrorText("rg: " + path + ": IO error for operation on " + path
                        + ": No such file or directory (os error 2)\n");
                } else {
                    context.ErrorText("rg: " + path
                        + ": No such file or directory (os error 2)\n");
                }
            }
            result.error = true;
            continue;
        }
        if (settings.mode == RgMode::Files) {
            // --files lists every operand that would be searched, too.
            printer.Path(path);
            result.matched = true;
            continue;
        }
        searchFile(path, /*operand=*/true);
    }

    if (stopped) {
        result.stopped = true;
        result.quietMatched = quietMatched;
        return result;
    }
    // The implicit path, and nothing searched at all (every file filtered):
    // rg's message. --files just comes back empty-handed.
    if (implicitDot && filesSearched == 0 && settings.mode != RgMode::Files) {
        context.ErrorText("rg: No files were searched, which means ripgrep probably applied "
            "a filter you didn't expect.\nRunning with --debug will show why files are "
            "being skipped.\n");
        result.noFilesSearched = true;
    }
    return result;
}

} // namespace Haisos