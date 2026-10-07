#include <algorithm>
#include <cstdio>
#include <cstring>
#include <optional>
#include <string>
#include <vector>
#include "BuiltinCommand.h"
#include "src/components/Filesystem/FilesystemUtils.h"
#include "src/components/Unicode/Unicode.h"

namespace Haisos {

namespace {

enum WcOption {
    kWcBytes = 1,
    kWcChars,
    kWcLines,
    kWcMaxLineLength,
    kWcWords,
    kWcFiles0From,
    kWcTotal,
};

enum class TotalMode { Auto, Always, Only, Never };

// Which counts the run prints.
struct WcSelection {
    bool lines = false;
    bool words = false;
    bool chars = false;
    bool bytes = false;
    bool maxLineLength = false;

    int Count() const {
        return (lines ? 1 : 0) + (words ? 1 : 0) + (chars ? 1 : 0)
            + (bytes ? 1 : 0) + (maxLineLength ? 1 : 0);
    }
    bool Any() const { return Count() != 0; }
};

// The counts of one input (and of the grand total). linePos and inWord are
// the counting state: the display width of the current line so far, and
// whether it is inside a word.
struct WcCounts {
    uint64_t lines = 0;
    uint64_t words = 0;
    uint64_t chars = 0;
    uint64_t bytes = 0;
    uint64_t maxLineLength = 0;
    uint64_t linePos = 0;
    bool inWord = false;
    // The start of a multi-byte character a chunk ended in the middle of.
    std::string carry;
};

// GNU wc's multibyte counting loop (Haisos is UTF-8 throughout): every byte
// counts as a byte; every complete character counts as one character, then
// moves the word/line state as wc's does -- a separator ends a word, \n
// counts a line, tab/CR/FF/VT/space move or reset the line position, and any
// other printable character is part of a word and adds its display width.
void WcFeed(WcCounts& counts, const char* data, size_t size, bool posixlyCorrect) {
    counts.bytes += size;
    // The carry is at most 3 bytes, so joining it back on never costs.
    std::string bytes = std::move(counts.carry);
    bytes.append(data, size);
    counts.carry.clear();
    size_t i = 0;
    while (i < bytes.size()) {
        const Unicode::DecodedChar decoded = Unicode::DecodeUtf8(bytes.data() + i, bytes.size() - i);
        if (decoded.status == Unicode::DecodeStatus::Incomplete) {
            // A character the next chunk completes; at the very end of the
            // input these bytes were already counted as bytes, nothing more.
            counts.carry = bytes.substr(i);
            return;
        }
        if (decoded.status == Unicode::DecodeStatus::Invalid) {
            ++i;
            continue;
        }
        i += decoded.length;
        ++counts.chars;
        const char32_t c = decoded.codePoint;
        bool separator = false;
        switch (c) {
            case U'\n':
                ++counts.lines;
                [[fallthrough]];
            case U'\r':
            case U'\f':
                counts.maxLineLength = std::max(counts.maxLineLength, counts.linePos);
                counts.linePos = 0;
                separator = true;
                break;
            case U'\t':
                counts.linePos += 8 - counts.linePos % 8;
                separator = true;
                break;
            case U' ':
                ++counts.linePos;
                separator = true;
                break;
            case U'\v':
                separator = true;
                break;
            default:
                if (Unicode::IsPrintable(c)) {
                    const int width = Unicode::DisplayWidth(c);
                    if (width > 0) {
                        counts.linePos += static_cast<uint64_t>(width);
                    }
                    if (Unicode::IsSpace(c) || (!posixlyCorrect && Unicode::IsNoBreakSpace(c))) {
                        separator = true;
                    } else {
                        counts.inWord = true;
                    }
                }
                break;
        }
        if (separator && counts.inWord) {
            ++counts.words;
            counts.inWord = false;
        }
    }
}

void WcFinish(WcCounts& counts) {
    counts.maxLineLength = std::max(counts.maxLineLength, counts.linePos);
    if (counts.inWord) {
        ++counts.words;
        counts.inWord = false;
    }
}

void Accumulate(WcCounts& totals, const WcCounts& counts) {
    totals.lines += counts.lines;
    totals.words += counts.words;
    totals.chars += counts.chars;
    totals.bytes += counts.bytes;
    totals.maxLineLength = std::max(totals.maxLineLength, counts.maxLineLength);
}

// The counts of one input: 0 counted to its end, 1 an error was reported
// (the counts so far are still printed), -1 stopped -- quiet, as a
// signal-stopped program is.
int WcRead(BuiltinContext& context, IFileDescriptor& input, const std::string& errorName,
           bool posixlyCorrect, WcCounts& counts) {
    char buffer[16 * 1024];
    while (true) {
        if (context.StopRequested()) {
            return -1;
        }
        const ssize_t n = input.Read(buffer, sizeof(buffer));
        if (n == 0) {
            WcFinish(counts);
            return 0;
        }
        if (n == kIOInterrupted) {
            return -1;
        }
        if (n < 0) {
            context.Error(errorName + ": Input/output error");
            WcFinish(counts);
            return 1;
        }
        WcFeed(counts, buffer, static_cast<size_t>(n), posixlyCorrect);
    }
}

// How one input went: its counts, whether its counts line is printed (a missing
// file has none, a directory has one of zeros), and the outcome as WcRead's.
struct WcInputResult {
    int outcome = 0;
    bool printLine = true;
    WcCounts counts;
};

WcInputResult WcStdIn(BuiltinContext& context, bool posixlyCorrect) {
    const auto input = context.IO().GetDescriptor(IFileIO::kStdIn);
    if (!input) {
        context.Error("'standard input': Bad file descriptor");
        return WcInputResult{1, false, {}};
    }
    WcInputResult result;
    result.outcome = WcRead(context, *input, "'standard input'", posixlyCorrect, result.counts);
    result.printLine = result.outcome >= 0;
    return result;
}

WcInputResult WcFile(BuiltinContext& context, const std::string& name, bool posixlyCorrect) {
    IFileIO& io = context.IO();
    const std::string shown = ShellEscapeQuoted(name);
    FileStatus status;
    if (io.Stat(name, status) != 0) {
        context.Error(shown + ": No such file or directory");
        return WcInputResult{1, false, {}};
    }
    // GNU opens a directory fine; its read then fails with EISDIR, the counts
    // of zeros going out. Same here.
    if (status.type == DirectoryEntryType::Dir) {
        context.Error(shown + ": Is a directory");
        return WcInputResult{1, true, {}};
    }
    const auto handle = io.OpenFile(name, kFileOpenReadOnly);
    if (!handle) {
        context.Error(shown + ": Permission denied");
        return WcInputResult{1, false, {}};
    }
    WcInputResult result;
    result.outcome = WcRead(context, *handle, shown, posixlyCorrect, result.counts);
    result.printLine = result.outcome >= 0;
    return result;
}

// One counts line as GNU's write_counts prints it: the selected counts,
// right-aligned in |width| columns, joined by one space, in the fixed order
// lines, words, chars, bytes, maximum line width; then, if there is a name, a
// space and the name -- as given, unless it holds a newline.
void WriteCounts(BuiltinContext& context, const WcCounts& counts, const WcSelection& selection,
                 int width, const std::optional<std::string>& name) {
    std::string line;
    bool first = true;
    const auto add = [&line, &first, width](uint64_t value) {
        char number[32];
        std::snprintf(number, sizeof(number), "%*llu", width, static_cast<unsigned long long>(value));
        if (!first) {
            line += ' ';
        }
        first = false;
        line += number;
    };
    if (selection.lines) add(counts.lines);
    if (selection.words) add(counts.words);
    if (selection.chars) add(counts.chars);
    if (selection.bytes) add(counts.bytes);
    if (selection.maxLineLength) add(counts.maxLineLength);
    if (name) {
        line += ' ';
        line += name->find('\n') == std::string::npos ? *name : ShellEscapeQuoted(*name);
    }
    line += '\n';
    context.Out(line);
}

// GNU's get_input_fstatus + compute_number_width: the counts' width from the
// input sizes. Stdin is never a regular file here (no file descriptor status
// in Haisos -- GNU would size a redirected file), so with it, a directory or
// a device the columns are at least 7 wide. A count wider than the width is
// printed whole.
int ComputeWidth(IFileIO& io, bool totalOnly, bool namesFromStdin,
                 const std::vector<std::string>& names, int countColumns) {
    if (totalOnly || namesFromStdin) {
        return 1;
    }
    if ((names.empty() ? 1 : names.size()) == 1 && countColumns == 1) {
        return 1;
    }
    int minimum = names.empty() ? 7 : 1;  // no name: stdin
    uint64_t total = 0;
    for (const auto& name : names) {
        if (name.empty()) {
            continue;
        }
        if (name == "-") {
            minimum = std::max(minimum, 7);
            continue;
        }
        FileStatus status;
        if (io.Stat(name, status) != 0) {
            continue;
        }
        if (status.type == DirectoryEntryType::File) {
            total += status.size;
        } else {
            minimum = std::max(minimum, 7);
        }
    }
    int width = 1;
    for (uint64_t value = total; value >= 10; value /= 10) {
        ++width;
    }
    return std::max(width, minimum);
}

// GNU's argmatch: an exact value, or an unambiguous prefix of one. Anything
// else is the whole diagnostic and there is nothing more to do.
enum class TotalMatch { Matched, Invalid, Ambiguous };
TotalMatch MatchTotalValue(const std::string& value, TotalMode& mode) {
    static const std::pair<const char*, TotalMode> kModes[] = {
        {"auto", TotalMode::Auto},
        {"always", TotalMode::Always},
        {"only", TotalMode::Only},
        {"never", TotalMode::Never},
    };
    for (const auto& [name, candidate] : kModes) {
        if (value == name) {
            mode = candidate;
            return TotalMatch::Matched;
        }
    }
    bool matched = false;
    int matches = 0;
    for (const auto& [name, candidate] : kModes) {
        if (value.size() <= std::strlen(name) && std::memcmp(name, value.data(), value.size()) == 0) {
            mode = candidate;
            matched = true;
            ++matches;
        }
    }
    if (matches > 1) {
        return TotalMatch::Ambiguous;
    }
    if (matched) {
        return TotalMatch::Matched;
    }
    return TotalMatch::Invalid;
}

class WcCommand : public IBuiltinCommand {
public:
    std::string Name() const override { return "wc"; }
    std::string Version() const override { return "1.0.1"; }

    const std::vector<BuiltinOption>& Options() const override {
        static const std::vector<BuiltinOption> options = {
            {'c', "bytes", kWcBytes, BuiltinArgument::None, "", "print the byte counts"},
            {'m', "chars", kWcChars, BuiltinArgument::None, "", "print the character counts"},
            {'l', "lines", kWcLines, BuiltinArgument::None, "", "print the newline counts"},
            {'L', "max-line-length", kWcMaxLineLength, BuiltinArgument::None, "", "print the maximum display width"},
            {'w', "words", kWcWords, BuiltinArgument::None, "", "print the word counts"},
            {0, "files0-from", kWcFiles0From, BuiltinArgument::Required, "F", "read NUL-separated names from F (- for stdin)"},
            {0, "total", kWcTotal, BuiltinArgument::Required, "WHEN", "auto, always, only, never"},
            {0, "debug", kBuiltinNotTreated, BuiltinArgument::None, "", ""},
        };
        return options;
    }

    BuiltinHelp Help() const override {
        return BuiltinHelp{
            "print newline, word, and byte counts for each file",
            {"wc [OPTION]... [FILE]...", "wc [OPTION]... --files0-from=F"},
            "Standard input has no file size here, so with it the columns are at least 7 wide.\n"
            "-m -w -L use compact Unicode tables: unassigned characters count as printable, rare scripts' widths are approximate."};
    }

    int Run(BuiltinContext& context) override {
        int exitStatus = 0;
        const auto parsed = BeginBuiltin(context, *this, /*usageErrorStatus=*/1, exitStatus);
        if (!parsed) {
            return exitStatus;
        }

        WcSelection selection;
        std::optional<std::string> files0From;
        TotalMode totalMode = TotalMode::Auto;
        for (const auto& option : parsed->options) {
            switch (option.id) {
                case kWcBytes: selection.bytes = true; break;
                case kWcChars: selection.chars = true; break;
                case kWcLines: selection.lines = true; break;
                case kWcMaxLineLength: selection.maxLineLength = true; break;
                case kWcWords: selection.words = true; break;
                case kWcFiles0From: files0From = option.argument; break;
                case kWcTotal: {
                    TotalMode mode;
                    const TotalMatch match = MatchTotalValue(option.argument, mode);
                    if (match != TotalMatch::Matched) {
                        context.Error(std::string(match == TotalMatch::Ambiguous ? "ambiguous" : "invalid")
                            + " argument '" + option.argument + "' for '--total'");
                        context.ErrorText("Valid arguments are:\n  - 'auto'\n  - 'always'\n  - 'only'\n  - 'never'\n");
                        context.TryHelp();
                        return 1;
                    }
                    totalMode = mode;
                    break;
                }
                default: break;
            }
        }
        if (!selection.Any()) {
            selection.lines = selection.words = selection.bytes = true;
        }

        const auto environment = context.Process().GetEnvironment();
        const bool posixlyCorrect = environment && environment->GetVariable("POSIXLY_CORRECT").has_value();

        // The inputs: the operands, or the NUL-separated names of
        // --files0-from=F, or -- with neither -- the standard input, unnamed.
        std::vector<std::string> names;
        bool namesFromStdinStream = false;
        if (files0From) {
            if (!parsed->operands.empty()) {
                context.Error("extra operand " + ShellEscapeQuoted(parsed->operands.front(), /*always=*/true));
                context.ErrorText("file operands cannot be combined with --files0-from\n");
                context.TryHelp();
                return 1;
            }
            std::string list;
            if (*files0From == "-") {
                namesFromStdinStream = true;
                const auto input = context.IO().GetDescriptor(IFileIO::kStdIn);
                if (!input || !ReadWholeList(context, *input, "-", list)) {
                    return 1;
                }
            } else {
                const std::string shown = ShellEscapeQuoted(*files0From);
                FileStatus status;
                if (context.IO().Stat(*files0From, status) != 0) {
                    context.Error("cannot open " + ShellEscapeQuoted(*files0From, /*always=*/true)
                        + " for reading: No such file or directory");
                    return 1;
                }
                if (status.type == DirectoryEntryType::Dir) {
                    context.Error(shown + ": read error: Is a directory");
                    return 1;
                }
                const auto handle = context.IO().OpenFile(*files0From, kFileOpenReadOnly);
                if (!handle) {
                    context.Error("cannot open " + ShellEscapeQuoted(*files0From, /*always=*/true)
                        + " for reading: Permission denied");
                    return 1;
                }
                if (!ReadWholeList(context, *handle, shown, list)) {
                    return 1;
                }
            }
            names = SplitNames(list);
            if (names.empty()) {
                // GNU: no names at all, nothing is printed -- except the
                // total of zero counts --total=always/--total=only asks for
                // (width 1: there are no files to size from).
                if (totalMode == TotalMode::Always || totalMode == TotalMode::Only) {
                    WriteCounts(context, WcCounts{}, selection, 1,
                        totalMode == TotalMode::Only
                            ? std::nullopt : std::optional<std::string>("total"));
                }
                return 0;
            }
        } else {
            names = parsed->operands;
        }

        const int width = ComputeWidth(context.IO(), totalMode == TotalMode::Only,
            namesFromStdinStream, names, selection.Count());
        const bool printLines = totalMode != TotalMode::Only;
        const bool printTotal = totalMode == TotalMode::Always || totalMode == TotalMode::Only
            || (totalMode == TotalMode::Auto && names.size() > 1);

        WcCounts totals;
        int status = 0;
        if (names.empty()) {
            const WcInputResult result = WcStdIn(context, posixlyCorrect);
            if (result.outcome < 0) {
                return 1;
            }
            if (result.outcome > 0) {
                status = 1;
            }
            Accumulate(totals, result.counts);
            if (printLines && result.printLine) {
                WriteCounts(context, result.counts, selection, width, std::nullopt);
            }
        } else {
        for (size_t i = 0; i < names.size(); ++i) {
            const std::string& name = names[i];
            if (name.empty()) {
                // Every NUL ends a name, even an empty one.
                status = 1;
                context.Error(files0From
                    ? ShellEscapeQuoted(*files0From) + ":" + std::to_string(i + 1) + ": invalid zero-length file name"
                    : "invalid zero-length file name");
                continue;
            }
            if (namesFromStdinStream && name == "-") {
                context.Error("when reading file names from stdin, no file name of '-' allowed");
                status = 1;
                continue;
            }
            if (context.StopRequested()) {
                return 1;
            }
            const WcInputResult result = name == "-"
                ? WcStdIn(context, posixlyCorrect)
                : WcFile(context, name, posixlyCorrect);
            if (result.outcome < 0) {
                return 1;
            }
            if (result.outcome > 0) {
                status = 1;
            }
            Accumulate(totals, result.counts);
            if (printLines && result.printLine) {
                WriteCounts(context, result.counts, selection, width, name);
            }
        }
        }
        if (printTotal) {
            WriteCounts(context, totals, selection, width,
                totalMode == TotalMode::Only ? std::nullopt : std::optional<std::string>("total"));
        }
        return status;
    }

private:
    // Reads the whole of --files0-from's stream. Returns false on a stop
    // (quiet) or after reporting a read failure.
    static bool ReadWholeList(BuiltinContext& context, IFileDescriptor& input,
                              const std::string& what, std::string& out) {
        char buffer[16 * 1024];
        while (true) {
            if (context.StopRequested()) {
                return false;
            }
            const ssize_t n = input.Read(buffer, sizeof(buffer));
            if (n == 0) {
                return true;
            }
            if (n == kIOInterrupted) {
                return false;
            }
            if (n < 0) {
                context.Error(what + ": read error: Input/output error");
                return false;
            }
            out.append(buffer, static_cast<size_t>(n));
        }
    }

    // Every NUL ends a name (an empty one if two meet); bytes after the last
    // NUL form one more name, if there are any.
    static std::vector<std::string> SplitNames(const std::string& list) {
        std::vector<std::string> names;
        size_t start = 0;
        for (size_t i = 0; i < list.size(); ++i) {
            if (list[i] == '\0') {
                names.push_back(list.substr(start, i - start));
                start = i + 1;
            }
        }
        if (start < list.size()) {
            names.push_back(list.substr(start));
        }
        return names;
    }
};

} // namespace

std::shared_ptr<IBuiltinCommand> CreateWcCommand() {
    return std::make_shared<WcCommand>();
}

} // namespace Haisos
