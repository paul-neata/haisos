#include <cstdint>
#include <cstdio>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include "BuiltinCommand.h"
#include "BuiltinSize.h"
#include "BuiltinText.h"
#include "src/components/Filesystem/FilesystemUtils.h"

namespace Haisos {

namespace {

enum CmpOption {
    kCmpPrintBytes = 1,   // -b / -c
    kCmpIgnoreInitial,    // -i
    kCmpVerbose,          // -l
    kCmpBytesLimit,       // -n
    kCmpQuiet,            // -s / --silent
};

// A byte as cmp prints it with -b/-c: a printable byte as it is, one below
// 32 as ^ + (c + 64), 127 ^?, 128 and up M- followed by its low seven bits'
// form (diffutils' sprintc).
std::string SprintC(int c) {
    std::string text;
    if (c >= 128) {
        text += "M-";
        c -= 128;
    }
    if (c < 32) {
        text += '^';
        c += 64;
    } else if (c == 127) {
        text += '^';
        c = '?';
    }
    text += static_cast<char>(c);
    return text;
}

// A byte as %3o prints it, three columns wide.
std::string Octal3(int c) {
    char buffer[8];
    std::snprintf(buffer, sizeof(buffer), "%3o", static_cast<unsigned>(c));
    return buffer;
}

// A number right-aligned to |width| columns, as %*s prints it.
std::string RightAligned(uint64_t number, size_t width) {
    std::string text = std::to_string(number);
    if (text.size() < width) {
        text.insert(0, width - text.size(), ' ');
    }
    return text;
}

// The decimal digits of |value| (one for 0).
size_t DigitCount(uint64_t value) {
    size_t digits = 1;
    while (value >= 10) {
        value /= 10;
        ++digits;
    }
    return digits;
}

// What one compared input keeps track of, beyond its bytes: how many were
// read past the skip, how many lines they held, and what the last was.
struct CmpSide {
    uint64_t bytesRead = 0;
    uint64_t newlines = 0;
    int lastByte = -1;
};

// One input being compared, its bytes handed out one at a time behind a
// chunked read (cmp reads both in chunks, as diffutils does).
class CmpByteSource {
public:
    explicit CmpByteSource(IFileDescriptor& file)
        : m_file(file) {}

    // The next byte (0-255), -1 at end of file, -2 on a read error.
    int Next() {
        if (m_position >= m_length) {
            const ssize_t n = m_file.Read(m_buffer, sizeof(m_buffer));
            if (n == 0) {
                return -1;
            }
            if (n < 0) {
                return -2;
            }
            m_length = static_cast<size_t>(n);
            m_position = 0;
        }
        return static_cast<unsigned char>(m_buffer[m_position++]);
    }

    // Reads and discards |count| bytes (a skip: descriptors cannot seek).
    // Returns 0 when done, -1 on a read error, -2 on a stop.
    int Skip(BuiltinContext& context, uint64_t count) {
        while (count > 0) {
            if (context.StopRequested()) {
                return -2;
            }
            const uint64_t want = count < sizeof(m_buffer) ? count : sizeof(m_buffer);
            const ssize_t n = m_file.Read(m_buffer, static_cast<size_t>(want));
            if (n == 0) {
                return 0;  // end of file before the whole skip: nothing to read
            }
            if (n == kIOInterrupted) {
                return -2;
            }
            if (n < 0) {
                return -1;
            }
            count -= static_cast<uint64_t>(n);
        }
        return 0;
    }

private:
    IFileDescriptor& m_file;
    char m_buffer[16 * 1024];
    size_t m_length = 0;
    size_t m_position = 0;
};

class CmpCommand : public IBuiltinCommand {
public:
    std::string Name() const override { return "cmp"; }
    std::string Version() const override { return "1.0.0"; }

    const std::vector<BuiltinOption>& Options() const override {
        using A = BuiltinArgument;
        static const std::vector<BuiltinOption> options = {
            {'b', "print-bytes", kCmpPrintBytes, A::None, "", "print differing bytes"},
            {'c', "print-chars", kCmpPrintBytes, A::None, "", "same as -b, but print unprintable bytes as characters"},
            {'i', "ignore-initial", kCmpIgnoreInitial, A::Required, "SKIP", "ignore first SKIP bytes of input"},
            {'l', "verbose", kCmpVerbose, A::None, "", "output byte numbers and differing byte values"},
            {'n', "bytes", kCmpBytesLimit, A::Required, "LIMIT", "compare at most LIMIT bytes"},
            {'s', "quiet", kCmpQuiet, A::None, "", "suppress all normal output"},
            {0, "silent", kCmpQuiet, A::None, "", "same as -s"},
            {'v', "", kBuiltinOptionVersion, A::None, "", "output version information"},
        };
        return options;
    }

    BuiltinHelp Help() const override {
        return BuiltinHelp{
            "compare two files byte by byte",
            {"cmp [OPTION]... FILE1 [FILE2 [SKIP1 [SKIP2]]]"},
            "SKIP1 and SKIP2 are the number of bytes to skip in each file (a suffix\n"
            "K 1024, kB 1000, M 1024^2 ... scales them); with no FILE2, or when it is -,\n"
            "standard input is read. Exit status: 0 if the files are the same, 1 if they\n"
            "differ, 2 on trouble."};
    }

    int Run(BuiltinContext& context) override {
        // diffutils prefixes its Try line, so the shared BeginBuiltin does not
        // fit; cmp words its own usage errors, all exit 2.
        const auto parsed = ParseBuiltinArgs(context.Args(), Options());
        if (!parsed.error.empty()) {
            context.Error(parsed.error);
            context.Error("Try 'cmp --help' for more information.");
            return 2;
        }
        for (const auto& option : parsed.options) {
            if (option.id == kBuiltinOptionHelp) {
                context.Out(BuiltinHelpText(*this));
                return 0;
            }
            if (option.id == kBuiltinOptionVersion) {
                context.Out(BuiltinVersionText(*this));
                return 0;
            }
        }
        context.ReportNotTreated(parsed);

        bool printBytes = false;
        bool verbose = false;
        bool quiet = false;
        std::string ignoreInitial;
        std::string limitText;
        for (const auto& option : parsed.options) {
            switch (option.id) {
                case kCmpPrintBytes: printBytes = true; break;
                case kCmpVerbose: verbose = true; break;
                case kCmpQuiet: quiet = true; break;
                case kCmpIgnoreInitial: ignoreInitial = option.argument; break;
                case kCmpBytesLimit: limitText = option.argument; break;
                default: break;
            }
        }
        if (verbose && quiet) {
            return UsageError(context, "options -l and -s are incompatible");
        }
        if (parsed.operands.empty()) {
            return UsageError(context, "missing operand after 'cmp'");
        }
        if (parsed.operands.size() > 4) {
            return UsageError(context, "extra operand '" + parsed.operands[4] + "'");
        }

        // -i SKIP skips that much of both files, -i SKIP1:SKIP2 each its own;
        // the SKIP1/SKIP2 operands override whatever -i gave.
        uint64_t skip1 = 0, skip2 = 0;
        if (!ignoreInitial.empty()) {
            const size_t colon = ignoreInitial.find(':');
            if (colon == std::string::npos) {
                const auto skip = ParseSizeValue(context, ignoreInitial, "--ignore-initial");
                if (!skip) {
                    return 2;
                }
                skip1 = skip2 = *skip;
            } else {
                const auto first = ParseSizeValue(context, ignoreInitial.substr(0, colon), "--ignore-initial");
                if (!first) {
                    return 2;
                }
                const auto second = ParseSizeValue(context, ignoreInitial.substr(colon + 1), "--ignore-initial");
                if (!second) {
                    return 2;
                }
                skip1 = *first;
                skip2 = *second;
            }
        }
        if (parsed.operands.size() >= 3) {
            const auto skip = ParseSizeValue(context, parsed.operands[2], "--ignore-initial");
            if (!skip) {
                return 2;
            }
            skip1 = *skip;
        }
        if (parsed.operands.size() >= 4) {
            const auto skip = ParseSizeValue(context, parsed.operands[3], "--ignore-initial");
            if (!skip) {
                return 2;
            }
            skip2 = *skip;
        }

        // -n LIMIT: compare at most LIMIT bytes (none, when LIMIT is 0).
        uint64_t limit = ~uint64_t{0};
        bool limitGiven = false;
        if (!limitText.empty()) {
            const auto parsedLimit = ParseSizeValue(context, limitText, "--bytes");
            if (!parsedLimit) {
                return 2;
            }
            limit = *parsedLimit;
            limitGiven = true;
        }

        const std::string& name1 = parsed.operands[0];
        const std::string name2 = parsed.operands.size() >= 2 ? parsed.operands[1] : "-";
        // Both the same file (both standard input, or one resolved path):
        // they compare equal at once.
        const bool sameFile = name1 == name2
            || (name1 != "-" && name2 != "-"
                && context.IO().ResolvePath(name1) == context.IO().ResolvePath(name2));
        if (sameFile) {
            return 0;
        }

        const auto file1 = OpenOperand(context, name1, quiet);
        if (!file1) {
            return 2;
        }
        const auto file2 = OpenOperand(context, name2, quiet);
        if (!file2) {
            return 2;
        }

        // The -l byte-number column: as wide as the number of digits of the
        // smaller file's size past the skip, or of the limit when smaller.
        // (The same resolved path twice never reaches here.)
        const uint64_t size1 = OperandSize(context, name1);
        const uint64_t size2 = OperandSize(context, name2);
        uint64_t widthBase = ~uint64_t{0};
        if (size1 != kUnknownSize) {
            widthBase = size1 > skip1 ? size1 - skip1 : 0;
        }
        if (size2 != kUnknownSize) {
            const uint64_t afterSkip = size2 > skip2 ? size2 - skip2 : 0;
            widthBase = widthBase < afterSkip ? widthBase : afterSkip;
        }
        if (limitGiven && limit < widthBase) {
            widthBase = limit;
        }
        if (widthBase == ~uint64_t{0}) {
            widthBase = 1;  // neither size known (standard input): one digit
        }
        const size_t numberWidth = DigitCount(widthBase);

        auto source1 = std::make_unique<CmpByteSource>(*file1);
        auto source2 = std::make_unique<CmpByteSource>(*file2);
        if (source1->Skip(context, skip1) != 0 || source2->Skip(context, skip2) != 0) {
            // A stop, or a read error while skipping.
            return 1;
        }

        CmpSide side1, side2;
        uint64_t compared = 0;  // byte pairs compared past the skip
        uint64_t lineNumber = 1;
        uint64_t differences = 0;
        while (limit > 0) {
            if (context.StopRequested()) {
                return 1;
            }
            const int c1 = source1->Next();
            const int c2 = source2->Next();
            if (c1 == -2 || c2 == -2) {
                const std::string& failed = c1 == -2 ? name1 : name2;
                if (!quiet) {
                    context.Error(failed + ": Input/output error");
                }
                return 2;
            }
            if (c1 == -1 && c2 == -1) {
                break;  // both ended: equal so far
            }
            if (c1 == -1 || c2 == -1) {
                // One input ended first; the messages below name it. -l
                // says only how far it got; the others say which line.
                const bool firstEnded = c1 == -1;
                const CmpSide& ended = firstEnded ? side1 : side2;
                const std::string& name = firstEnded ? name1 : name2;
                if (!quiet) {
                    if (verbose) {
                        context.Error("EOF on " + name + " after byte " + std::to_string(ended.bytesRead));
                    } else if (ended.bytesRead == 0) {
                        context.Error("EOF on " + name + " which is empty");
                    } else if (ended.lastByte == '\n') {
                        context.Error("EOF on " + name + " after byte " + std::to_string(ended.bytesRead)
                            + ", line " + std::to_string(ended.newlines));
                    } else {
                        context.Error("EOF on " + name + " after byte " + std::to_string(ended.bytesRead)
                            + ", in line " + std::to_string(ended.newlines + 1));
                    }
                }
                return 1;
            }

            // Two bytes: count them (the line they are in included).
            ++compared;
            --limit;
            Count(side1, c1);
            Count(side2, c2);
            if (c1 == '\n' && c2 == '\n') {
                ++lineNumber;
            }
            if (c1 == c2) {
                continue;
            }
            ++differences;
            if (!verbose) {
                // The first difference ends it, unless -s asked for silence.
                if (!quiet) {
                    if (printBytes) {
                        context.Out(name1 + " " + name2 + " differ: byte " + std::to_string(compared)
                            + ", line " + std::to_string(lineNumber) + " is " + Octal3(c1) + " " + SprintC(c1)
                            + " " + Octal3(c2) + " " + SprintC(c2) + "\n");
                    } else {
                        context.Out(name1 + " " + name2 + " differ: char " + std::to_string(compared)
                            + ", line " + std::to_string(lineNumber) + "\n");
                    }
                }
                return 1;
            }
            // -l: every difference on a line of its own, the byte number as
            // wide as the smaller file.
            const std::string number = RightAligned(compared, numberWidth);
            if (printBytes) {
                std::string padded = SprintC(c1);
                padded.resize(4, ' ');
                context.Out(number + " " + Octal3(c1) + " " + padded + " " + Octal3(c2) + " "
                    + SprintC(c2) + "\n");
            } else {
                context.Out(number + " " + Octal3(c1) + " " + Octal3(c2) + "\n");
            }
        }
        return differences > 0 ? 1 : 0;
    }

private:
    static constexpr uint64_t kUnknownSize = ~uint64_t{0};

    // A usage error: the message, then diffutils' prefixed Try line.
    static int UsageError(BuiltinContext& context, const std::string& message) {
        context.Error(message);
        context.Error("Try 'cmp --help' for more information.");
        return 2;
    }

    // One of the two operands, opened for reading; "-" is standard input.
    // Returns null after reporting (unless -s), as diffutils does: the plain
    // name, no quoting.
    static std::shared_ptr<IFileDescriptor> OpenOperand(
        BuiltinContext& context, const std::string& name, bool quiet) {
        InputOpenFailure failure = InputOpenFailure::None;
        auto file = OpenInputOperand(context, name, failure);
        if (!file) {
            const char* why;
            switch (failure) {
                case InputOpenFailure::Missing: why = "No such file or directory"; break;
                case InputOpenFailure::Directory: why = "Is a directory"; break;
                case InputOpenFailure::Denied: why = "Permission denied"; break;
                default: why = "Bad file descriptor"; break;
            }
            if (!quiet) {
                context.Error(name + ": " + why);
            }
        }
        return file;
    }

    static void Count(CmpSide& side, int c) {
        ++side.bytesRead;
        if (c == '\n') {
            ++side.newlines;
        }
        side.lastByte = c;
    }

    // A SKIP or LIMIT value; a bad one is a usage error (exit 2), reported
    // here, and nullopt for the caller to exit on.
    static std::optional<uint64_t> ParseSizeValue(BuiltinContext& context, const std::string& value,
                                                  const char* what) {
        uintmax_t parsed = 0;
        if (ParseSizeWithSuffix(value, "kKmMGTPEZYRQ0", parsed) == SizeParse::Ok) {
            return static_cast<uint64_t>(parsed);
        }
        UsageError(context, "invalid " + std::string(what) + " value '" + value + "'");
        return std::nullopt;
    }

    // The size an operand's file has, for -l's byte-number width; unknown for
    // standard input.
    static uint64_t OperandSize(BuiltinContext& context, const std::string& name) {
        if (name == "-") {
            return kUnknownSize;
        }
        FileStatus status;
        if (context.IO().Stat(name, status) != 0) {
            return kUnknownSize;
        }
        return status.size;
    }
};

} // namespace

std::shared_ptr<IBuiltinCommand> CreateCmpCommand() {
    return std::make_shared<CmpCommand>();
}

} // namespace Haisos