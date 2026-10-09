#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <vector>
#include "BuiltinCommand.h"
#include "BuiltinSize.h"
#include "BuiltinText.h"
#include "src/components/Filesystem/FilesystemUtils.h"

namespace Haisos {
namespace {

enum HeadOption {
    kHeadBytes = 1,
    kHeadLines,
    kHeadQuiet,
    kHeadVerbose,
    kHeadZero,
    kHeadDigit,  // -0 .. -9, the obsolete -NUM: one id for all ten
};

enum class HeadHeader { Multiple, Never, Always };

// The count option in effect: the first n lines/bytes of each file, or with
// fromEnd all but the last n.
struct HeadCount {
    bool bytes = false;
    bool fromEnd = false;
    uintmax_t n = 10;
};

// The size value a -n/-c option takes: a leading '-' means "all but the last
// NUM", the rest through ParseSizeWithSuffix. Reports and returns false on a
// bad value (GNU exits 1 after reporting).
bool ParseHeadCount(BuiltinContext& context, const std::string& text, bool bytes, HeadCount& out) {
    std::string value = text;
    out.fromEnd = !value.empty() && value[0] == '-';
    if (out.fromEnd) {
        value.erase(0, 1);
    }
    out.bytes = bytes;
    const SizeParse result = ParseSizeWithSuffix(value, "bkKmMGTPEZYRQ0", out.n);
    if (result == SizeParse::Ok) {
        return true;
    }
    const std::string kind = bytes ? "bytes" : "lines";
    if (result == SizeParse::Overflow) {
        context.Error("invalid number of " + kind + ": " + GnuQuote(value)
            + ": Value too large for defined data type");
    } else {
        context.Error("invalid number of " + kind + ": " + GnuQuote(value));
    }
    return false;
}

// The obsolete spelling "-NUM[b|c|k|m|l|q|v|z]...": consumed before the
// regular option parsing, as GNU head does (its old_obs so that "head -5"
// works; the count applies to lines). False on a usage error (reported).
bool ParseHeadObsolete(BuiltinContext& context, const std::string& arg,
                       HeadCount& count, HeadHeader& header, bool& zero) {
    uintmax_t n = 0;
    bool overflow = false;
    size_t i = 1;
    for (; i < arg.size() && arg[i] >= '0' && arg[i] <= '9'; ++i) {
        const uintmax_t digit = static_cast<uintmax_t>(arg[i] - '0');
        if (n > (UINTMAX_MAX - digit) / 10) {
            overflow = true;
        } else {
            n = n * 10 + digit;
        }
    }
    const std::string digits = arg.substr(1, i - 1);
    bool bytes = false;
    uintmax_t multiplier = 1;
    for (; i < arg.size(); ++i) {
        switch (arg[i]) {
            case 'c': bytes = true; multiplier = 1; break;
            case 'b': bytes = true; multiplier = 512; break;
            case 'k': bytes = true; multiplier = 1024; break;
            case 'm': bytes = true; multiplier = 1024 * 1024; break;
            case 'l': break;
            case 'q': header = HeadHeader::Never; break;
            case 'v': header = HeadHeader::Always; break;
            case 'z': zero = true; break;
            default:
                context.Error(std::string("invalid trailing option -- ") + arg[i]);
                context.TryHelp();
                return false;
        }
    }
    if (bytes && n > UINTMAX_MAX / multiplier) {
        overflow = true;
    } else if (bytes) {
        n *= multiplier;
    }
    if (overflow) {
        context.Error("invalid number of " + std::string(bytes ? "bytes" : "lines") + ": "
            + GnuQuote(digits) + ": Value too large for defined data type");
        return false;
    }
    count.bytes = bytes;
    count.fromEnd = false;
    count.n = n;
    return true;
}

// A read error on an input GNU opened: there is nothing to recover, so the
// command reports it and goes on to the next FILE. Whether an operand could
// be opened at all was decided by OpenInputOperand; this is what a read past
// that failed with.
int HeadReadFailed(BuiltinContext& context, const std::string& shownName) {
    context.Error("error reading " + ShellEscapeQuoted(shownName, /*always=*/true)
        + ": Input/output error");
    return 0;
}

// One input's first n bytes, or all but its last n. Returns 1 on success, 0
// on a reported read error, -1 when the process was asked to stop.
int HeadBytes(BuiltinContext& context, IFileDescriptor& input, const std::string& shownName,
              const HeadCount& count) {
    char buffer[64 * 1024];
    if (!count.fromEnd) {
        uintmax_t remaining = count.n;
        while (remaining > 0) {
            if (context.StopRequested()) {
                return -1;
            }
            const size_t want = remaining < sizeof(buffer) ? static_cast<size_t>(remaining) : sizeof(buffer);
            const ssize_t n = input.Read(buffer, want);
            if (n == 0) {
                return 1;
            }
            if (n == kIOInterrupted) {
                return -1;
            }
            if (n < 0) {
                return HeadReadFailed(context, shownName);
            }
            context.Out(std::string(buffer, static_cast<size_t>(n)));
            remaining -= static_cast<uintmax_t>(n);
        }
        return 1;
    }
    // All but the last n bytes: hold the last n back in a ring and print what
    // overflows it -- never more than n bytes (and one read) in memory. At
    // end of input the ring holds the last bytes, and is dropped.
    std::string held;
    while (true) {
        if (context.StopRequested()) {
            return -1;
        }
        const ssize_t n = input.Read(buffer, sizeof(buffer));
        if (n == 0) {
            return 1;
        }
        if (n == kIOInterrupted) {
            return -1;
        }
        if (n < 0) {
            return HeadReadFailed(context, shownName);
        }
        held.append(buffer, static_cast<size_t>(n));
        if (held.size() > count.n) {
            const uintmax_t print = held.size() - count.n;
            context.Out(held.substr(0, static_cast<size_t>(print)));
            held.erase(0, static_cast<size_t>(print));
        }
    }
}

// One input's first n lines, or all but its last n. Same returns as
// HeadBytes.
int HeadLines(BuiltinContext& context, IFileDescriptor& input, const std::string& shownName,
              const HeadCount& count, char delimiter) {
    BuiltinLineReader reader(context, input, delimiter);
    std::string line;
    bool delimited = false;
    if (!count.fromEnd) {
        uintmax_t remaining = count.n;
        while (remaining > 0) {
            const LineReadResult result = reader.Next(line, delimited);
            switch (result) {
                case LineReadResult::Line: break;
                case LineReadResult::End: return 1;
                case LineReadResult::Stopped: return -1;
                case LineReadResult::Error: return HeadReadFailed(context, shownName);
            }
            std::string entry = line;
            if (delimited) {
                entry += delimiter;
            }
            context.Out(entry);
            --remaining;
        }
        return 1;
    }
    // All but the last n lines: a ring of n lines, each printed when a newer
    // one pushes it out; the ring itself is dropped at end of input.
    std::deque<std::string> ring;
    while (true) {
        const LineReadResult result = reader.Next(line, delimited);
        switch (result) {
            case LineReadResult::Line: break;
            case LineReadResult::End: return 1;
            case LineReadResult::Stopped: return -1;
            case LineReadResult::Error: return HeadReadFailed(context, shownName);
        }
        std::string entry = line;
        if (delimited) {
            entry += delimiter;
        }
        ring.push_back(std::move(entry));
        if (ring.size() > count.n) {
            context.Out(ring.front());
            ring.pop_front();
        }
    }
}

class HeadCommand : public IBuiltinCommand {
public:
    std::string Name() const override { return "head"; }
    std::string Version() const override { return "1.0.0"; }

    const std::vector<BuiltinOption>& Options() const override {
        static const std::vector<BuiltinOption> options = {
            {'c', "bytes", kHeadBytes, BuiltinArgument::Required, "[-]NUM",
                "print the first NUM bytes of each file; with the leading '-', print all but the last NUM bytes of each file"},
            {'n', "lines", kHeadLines, BuiltinArgument::Required, "[-]NUM",
                "print the first NUM lines instead of the first 10; with the leading '-', print all but the last NUM lines of each file"},
            {'q', "quiet", kHeadQuiet, BuiltinArgument::None, "",
                "never print headers giving file names"},
            {0, "silent", kHeadQuiet, BuiltinArgument::None, "",
                "never print headers giving file names"},
            {'v', "verbose", kHeadVerbose, BuiltinArgument::None, "",
                "always print headers giving file names"},
            {'z', "zero-terminated", kHeadZero, BuiltinArgument::None, "",
                "line delimiter is NUL, not newline"},
            {0, "-presume-input-pipe", kBuiltinNotTreated},
            {'0', "", kHeadDigit, BuiltinArgument::None, "", "", /*hidden=*/true},
            {'1', "", kHeadDigit, BuiltinArgument::None, "", "", /*hidden=*/true},
            {'2', "", kHeadDigit, BuiltinArgument::None, "", "", /*hidden=*/true},
            {'3', "", kHeadDigit, BuiltinArgument::None, "", "", /*hidden=*/true},
            {'4', "", kHeadDigit, BuiltinArgument::None, "", "", /*hidden=*/true},
            {'5', "", kHeadDigit, BuiltinArgument::None, "", "", /*hidden=*/true},
            {'6', "", kHeadDigit, BuiltinArgument::None, "", "", /*hidden=*/true},
            {'7', "", kHeadDigit, BuiltinArgument::None, "", "", /*hidden=*/true},
            {'8', "", kHeadDigit, BuiltinArgument::None, "", "", /*hidden=*/true},
            {'9', "", kHeadDigit, BuiltinArgument::None, "", "", /*hidden=*/true},
        };
        return options;
    }

    BuiltinHelp Help() const override {
        return BuiltinHelp{
            "output the first part of files",
            {"head [OPTION]... [FILE]..."},
            "NUM may have a multiplier suffix: b 512, kB 1000, K 1024, MB 1000*1000, M 1024*1024,\n"
            "and so on for T, P, E, Z, Y, R, Q; the same with a trailing B (1000-based) or iB\n"
            "(1024-based).\n"
            "With more than one FILE, precede each with a header giving the file name.",
        };
    }

    int Run(BuiltinContext& context) override {
        HeadCount count;
        HeadHeader header = HeadHeader::Multiple;
        bool zero = false;
        std::vector<std::string> args = context.Args();
        // The obsolete form "-NUM..." is consumed before the regular parsing;
        // anything left that starts with a digit is one given where GNU would
        // not accept it.
        if (!args.empty() && args[0].size() >= 2 && args[0][0] == '-'
            && args[0][1] >= '0' && args[0][1] <= '9') {
            if (!ParseHeadObsolete(context, args[0], count, header, zero)) {
                return 1;
            }
            args.erase(args.begin());
        }
        int exitStatus = 0;
        const auto parsed = BeginBuiltin(context, args, *this, 1, exitStatus);
        if (!parsed) {
            return exitStatus;
        }

        for (const auto& option : parsed->options) {
            switch (option.id) {
                case kHeadBytes:
                    if (!ParseHeadCount(context, option.argument, true, count)) {
                        return 1;
                    }
                    break;
                case kHeadLines:
                    if (!ParseHeadCount(context, option.argument, false, count)) {
                        return 1;
                    }
                    break;
                case kHeadQuiet:
                    header = HeadHeader::Never;
                    break;
                case kHeadVerbose:
                    header = HeadHeader::Always;
                    break;
                case kHeadZero:
                    zero = true;
                    break;
                case kHeadDigit:
                    // Outside the obsolete form, where GNU's getopt knows
                    // the digits too: the trailing-option error.
                    context.Error("invalid trailing option -- "
                        + std::string(1, option.spelling.size() > 1 ? option.spelling[1] : '0'));
                    context.TryHelp();
                    return 1;
                default:
                    break;
            }
        }

        std::vector<std::string> operands = parsed->operands;
        if (operands.empty()) {
            operands.push_back("-");
        }
        const bool headers = header == HeadHeader::Always
            || (header == HeadHeader::Multiple && operands.size() > 1);
        bool printedHeader = false;
        int status = 0;
        for (const auto& file : operands) {
            if (context.StopRequested()) {
                return 1;
            }
            const std::string shown = file == "-" ? "standard input" : file;
            InputOpenFailure failure = InputOpenFailure::None;
            auto input = OpenInputOperand(context, file, failure);
            if (failure == InputOpenFailure::Directory) {
                // GNU opens the directory and fails on read: the header
                // shows, then the read error, and the next FILE goes on.
                if (headers) {
                    PrintHeader(context, shown, printedHeader);
                }
                context.Error("error reading " + ShellEscapeQuoted(file, /*always=*/true)
                    + ": Is a directory");
                status = 1;
                continue;
            }
            if (!input) {
                context.Error("cannot open " + ShellEscapeQuoted(shown, /*always=*/true)
                    + " for reading: "
                    + (failure == InputOpenFailure::Denied ? "Permission denied"
                        : failure == InputOpenFailure::BadDescriptor ? "Bad file descriptor"
                            : "No such file or directory"));
                status = 1;
                continue;
            }
            if (headers) {
                PrintHeader(context, shown, printedHeader);
            }
            const int result = count.bytes
                ? HeadBytes(context, *input, shown, count)
                : HeadLines(context, *input, shown, count, zero ? '\0' : '\n');
            if (result < 0) {
                // Stopped, as a signal would stop the real command: quietly.
                return 1;
            }
            if (result == 0) {
                status = 1;
            }
        }
        return status;
    }

private:
    static void PrintHeader(BuiltinContext& context, const std::string& shownName, bool& printedHeader) {
        std::string text = printedHeader ? "\n" : "";
        text += "==> " + shownName + " <==\n";
        context.Out(text);
        printedHeader = true;
    }
};

} // namespace

std::shared_ptr<IBuiltinCommand> CreateHeadCommand() {
    return std::make_shared<HeadCommand>();
}

} // namespace Haisos