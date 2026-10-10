#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include "BuiltinCommand.h"
#include "BuiltinCopy.h"
#include "BuiltinDate.h"
#include "BuiltinPrintf.h"

namespace Haisos {

namespace {

constexpr int kOptionDereference = 1;  // -L, --dereference
constexpr int kOptionFileSystem = 2;    // -f, --file-system
constexpr int kOptionFormat = 3;        // -c, --format=FORMAT
constexpr int kOptionPrintf = 4;        // --printf=FORMAT
constexpr int kOptionTerse = 5;         // -t, --terse

// What every human-readable time directive prints: GNU's own format for
// %x %y %z in the C locale, ls --full-time's.
constexpr const char* kTimeFormat = "%Y-%m-%d %H:%M:%S.%N %z";

// GNU stat 9.4's format strings (stat.c), byte for byte; the values in them
// are Haisos's, the documented exceptions of --help. A character device's
// default layout differs in its third line, as GNU's does.
constexpr const char* kDefaultFormat =
    "  File: %n\n"
    "  Size: %-10s\tBlocks: %-10b IO Block: %-6o %F\n"
    "Device: %Hd,%Ld\tInode: %-10i  Links: %h\n"
    "Access: (%04a/%10.10A)  Uid: (%5u/%8U)   Gid: (%5g/%8G)\n"
    "Access: %x\n"
    "Modify: %y\n"
    "Change: %z\n"
    " Birth: %w\n";
constexpr const char* kDefaultDeviceFormat =
    "  File: %n\n"
    "  Size: %-10s\tBlocks: %-10b IO Block: %-6o %F\n"
    "Device: %Hd,%Ld\tInode: %-10i  Links: %-5h Device type: %Hr,%Lr\n"
    "Access: (%04a/%10.10A)  Uid: (%5u/%8U)   Gid: (%5g/%8G)\n"
    "Access: %x\n"
    "Modify: %y\n"
    "Change: %z\n"
    " Birth: %w\n";
constexpr const char* kTerseFormat = "%n %s %b %f %u %g %D %i %h %t %T %X %Y %Z %W %o\n";
constexpr const char* kFsDefaultFormat =
    "  File: \"%n\"\n"
    "    ID: %-8i Namelen: %-7l Type: %T\n"
    "Block size: %-10s Fundamental block size: %S\n"
    "Blocks: Total: %-10b Free: %-10f Available: %a\n"
    "Inodes: Total: %-10c Free: %d\n";
constexpr const char* kFsTerseFormat = "%n %i %l %t %s %S %b %f %a %c %d\n";

// The optimal I/O transfer size Haisos reports everywhere: %o and, with -f,
// the block sizes. There is nothing better to ask, Haisos having no device
// geometry of its own.
constexpr uint64_t kIoBlockSize = 4096;

// A hex digit's value, or -1.
int HexDigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// Whether a character is one of stat's directive flags, stat.c's
// printf_flags "'-+ #0I" (the grouping quote and I accepted, and without
// effect in the C locale, as BuiltinPrintf takes them).
bool IsStatFlag(char c) {
    return c == '\'' || c == '-' || c == '+' || c == ' ' || c == '#' || c == '0' || c == 'I';
}

// A run of digits as a width or precision, clamped to INT_MAX as
// BuiltinPrintf's own scan does.
int ReadDigitRun(std::string_view text, size_t& pos) {
    long long value = 0;
    while (pos < text.size() && text[pos] >= '0' && text[pos] <= '9') {
        if (value <= (std::numeric_limits<int>::max() - 9) / 10) {
            value = value * 10 + (text[pos] - '0');
        } else {
            value = std::numeric_limits<int>::max();
        }
        ++pos;
    }
    return static_cast<int>(value);
}

// glibc's gnu_dev_makedev: how %r and %R encode a device's major and minor
// numbers into one value.
uint64_t MakeDeviceNumber(uint32_t major, uint32_t minor) {
    return static_cast<uint64_t>((minor & 0xff) | ((major & 0xfff) << 8)
        | (static_cast<uint64_t>(minor & ~0xffu) << 12)
        | (static_cast<uint64_t>(major & ~0xfffu) << 32));
}

// What %F prints, GNU's file_type_string.
const char* FileTypeName(const FileStatus& status) {
    switch (status.type) {
        case DirectoryEntryType::Dir: return "directory";
        case DirectoryEntryType::CharDevice: return "character special file";
        default: return status.size == 0 ? "regular empty file" : "regular file";
    }
}

// What %A prints: the permissions every file has (as ls -l shows them) with
// the file type's leading character.
const char* PermissionString(const FileStatus& status) {
    switch (status.type) {
        case DirectoryEntryType::Dir: return "drwxrwxrwx";
        case DirectoryEntryType::CharDevice: return "crwxrwxrwx";
        default: return "-rwxrwxrwx";
    }
}

// What %f prints: the raw mode in hex -- the POSIX S_IFMT bits of the file
// type plus 0777, the permissions every file has.
uint64_t RawMode(const FileStatus& status) {
    switch (status.type) {
        case DirectoryEntryType::Dir: return 0x41ff;
        case DirectoryEntryType::CharDevice: return 0x21ff;
        default: return 0x81ff;
    }
}

// stat.c's out_epoch_sec: the whole seconds, and when a precision was given
// that many fractional digits (nine when '.' has no digits, the
// nanoseconds truncated or zero-extended). The flags and width go as GNU
// 9.4 applies them to the composed value -- '+' and ' ' prefixing it, '0'
// padding after the sign, '-' justifying left -- with one rule of its own:
// a width below the text's own length still pads, once it leaves room for
// the seconds, with trailing spaces whatever the flags say, and never past
// a length that follows the fraction digits only up to nine of them and
// then shrinks back again (min(DIGITS, 18 - DIGITS) of them).
std::string FormatEpochSeconds(const std::string& flags, const std::optional<int>& width,
                               bool precisionSpecified, bool precisionDigitsGiven,
                               int precision, FileDateTime t) {
    std::string text = std::to_string(t.seconds);
    if (text[0] != '-' && (flags.find('+') != std::string::npos
                           || flags.find(' ') != std::string::npos)) {
        text.insert(0, 1, flags.find('+') != std::string::npos ? '+' : ' ');
    }
    const size_t secondsLen = text.size();
    int digits = 0;
    if (precisionSpecified) {
        digits = precisionDigitsGiven ? precision : 9;
        if (digits > 4096) {
            digits = 4096;  // more zeros than any caller could mean
        }
        if (digits > 0) {
            std::string fraction = std::to_string(t.nanoseconds);
            fraction.insert(0, 9 > fraction.size() ? 9 - fraction.size() : 0, '0');
            if (fraction.size() > static_cast<size_t>(digits)) {
                fraction.resize(digits);
            } else if (fraction.size() < static_cast<size_t>(digits)) {
                fraction.resize(digits, '0');
            }
            text += '.';
            text += fraction;
        }
    }
    if (!width || *width <= 0) {
        return text;
    }
    const int w = *width;
    if (w >= static_cast<int>(text.size())) {
        const size_t pad = static_cast<size_t>(w) - text.size();
        if (flags.find('-') != std::string::npos) {
            text.append(pad, ' ');
        } else if (flags.find('0') != std::string::npos) {
            // The zeros go after the sign, as glibc pads a number.
            text.insert(text[0] == '-' || text[0] == '+' || text[0] == ' ' ? 1 : 0, pad, '0');
        } else {
            text.insert(0, pad, ' ');
        }
    } else if (digits > 0 && w >= static_cast<int>(secondsLen) + 2) {
        const int effective = static_cast<int>(secondsLen) + 1 + std::min(digits, 18 - digits);
        if (effective > w) {
            text.append(static_cast<size_t>(effective - w), ' ');
        }
    }
    return text;
}

// stat.c's print_esc_char: what a backslash means in a --printf format. The
// octal escape takes one to three digits and the hex escape one or two; any
// other character is printed after a warning on standard error, and a
// backslash ending the format is a plain backslash. Unlike printf's \c,
// nothing here stops the output.
void AppendStatEscape(BuiltinContext& context, std::string_view format, size_t& pos,
                      std::string& out) {
    if (pos >= format.size()) {
        out += '\\';
        return;
    }
    const char c = format[pos++];
    switch (c) {
        case 'a': out += '\a'; return;
        case 'b': out += '\b'; return;
        case 'e': out += '\033'; return;
        case 'f': out += '\f'; return;
        case 'n': out += '\n'; return;
        case 'r': out += '\r'; return;
        case 't': out += '\t'; return;
        case 'v': out += '\v'; return;
        case '"': out += '"'; return;
        case '\\': out += '\\'; return;
        case 'x': {
            int value = 0;
            size_t digits = 0;
            while (pos < format.size() && digits < 2 && HexDigit(format[pos]) >= 0) {
                value = value * 16 + HexDigit(format[pos]);
                ++pos;
                ++digits;
            }
            if (digits == 0) {
                context.Error("warning: unrecognized escape '\\x'");
                out += 'x';
                return;
            }
            out += static_cast<char>(value);
            return;
        }
        default:
            if (c >= '0' && c <= '7') {
                int value = c - '0';
                size_t digits = 1;
                while (pos < format.size() && digits < 3
                       && format[pos] >= '0' && format[pos] <= '7') {
                    value = value * 8 + (format[pos] - '0');
                    ++pos;
                    ++digits;
                }
                out += static_cast<char>(value);
                return;
            }
            context.Error(std::string("warning: unrecognized escape '\\") + c + "'");
            out += c;
            return;
    }
}

// The PrintfSpec the value of one directive goes through: the flags, width
// and precision as scanned, with the conversion the directive's kind chooses
// (o for %a, u for the decimal numbers, x for the hex ones, s for strings).
PrintfSpec MakePrintfSpec(const std::string& flags, const std::optional<int>& width,
                           bool precisionSpecified, int precision, char conversion) {
    PrintfSpec spec;
    spec.flags = flags;
    spec.width = width;
    spec.precision = precisionSpecified ? std::optional<int>(precision) : std::nullopt;
    spec.conversion = conversion;
    return spec;
}

// Prints one directive into |out|. Everything Haisos cannot know is a fixed
// value, the documented exceptions of --help: device and inode numbers 0,
// permissions 0777, uid and gid 0 with the names haisos, birth time unknown.
// A directive that means nothing in the mode at hand prints '?', as GNU's
// does for one it does not know.
void PrintStatDirective(const std::string& flags, const std::optional<int>& width,
                        bool precisionSpecified, bool precisionDigitsGiven, int precision,
                        char prefix, char directive, const std::string& name,
                        const FileStatus& status, bool fileSystem, std::string& out) {
    const auto printString = [&](std::string_view value) {
        out += FormatPrintfString(
            MakePrintfSpec(flags, width, precisionSpecified, precision, 's'), value);
    };
    const auto printNumber = [&](uint64_t value, char conversion) {
        out += FormatPrintfUnsigned(
            MakePrintfSpec(flags, width, precisionSpecified, precision, conversion), value);
    };
    const auto printEpoch = [&](FileDateTime t) {
        out += FormatEpochSeconds(flags, width, precisionSpecified, precisionDigitsGiven,
                                  precision, t);
    };

    if (fileSystem) {
        switch (directive) {
            case 'n': printString(name); return;
            case 'i': printNumber(0, 'x'); return;   // the file system ID, in hex
            case 'l': printNumber(255, 'u'); return; // the maximum length of file names
            case 't': printNumber(0, 'x'); return;   // the file system type, in hex
            case 'T': printString("haisos"); return;
            case 's':
            case 'S': printNumber(kIoBlockSize, 'u'); return;
            case 'b':
            case 'f':
            case 'a':
            case 'c':
            case 'd': printNumber(0, 'u'); return;
            default: out += '?'; return;
        }
    }

    switch (directive) {
        case 'n': printString(name); return;
        case 'N': printString(ShellEscapeQuoted(name, true)); return;
        case 's': printNumber(status.size, 'u'); return;
        case 'b': printNumber(status.blocks, 'u'); return;
        case 'B': printNumber(512, 'u'); return;    // what %b counts its blocks in
        case 'o': printNumber(kIoBlockSize, 'u'); return;
        case 'F': printString(FileTypeName(status)); return;
        case 'a': printNumber(0777, 'o'); return;
        case 'A': printString(PermissionString(status)); return;
        case 'f': printNumber(RawMode(status), 'x'); return;
        case 'h': printNumber(status.linkCount, 'u'); return;
        case 'i': printNumber(0, 'u'); return;      // no inode numbers
        case 'd': printNumber(0, 'u'); return;     // no device numbers, %Hd and %Ld too
        case 'D': printNumber(0, 'x'); return;
        case 'r':
            // A device's type, as st_rdev: %Hr and %Lr its major and minor
            // halves, %r and %R the two makedev-encoded.
            if (prefix == 'H') {
                printNumber(status.deviceMajor, 'u');
            } else if (prefix == 'L') {
                printNumber(status.deviceMinor, 'u');
            } else {
                printNumber(MakeDeviceNumber(status.deviceMajor, status.deviceMinor), 'u');
            }
            return;
        case 'R': printNumber(MakeDeviceNumber(status.deviceMajor, status.deviceMinor), 'x'); return;
        case 't': printNumber(status.deviceMajor, 'x'); return;
        case 'T': printNumber(status.deviceMinor, 'x'); return;
        case 'u':
        case 'g': printNumber(0, 'u'); return;
        case 'U':
        case 'G': printString("haisos"); return;
        case 'm': printString("/"); return;        // no mount information
        case 'C': printString("?"); return;        // no security contexts, no message
        case 'x': printString(FormatDateTime(kTimeFormat, status.accessTime, false)); return;
        case 'y': printString(FormatDateTime(kTimeFormat, status.modificationTime, false)); return;
        case 'z': printString(FormatDateTime(kTimeFormat, status.changeTime, false)); return;
        case 'X': printEpoch(status.accessTime); return;
        case 'Y': printEpoch(status.modificationTime); return;
        case 'Z': printEpoch(status.changeTime); return;
        case 'w': printString("-"); return;        // the birth time is unknown
        case 'W': printEpoch(FileDateTime{}); return;
        default: out += '?'; return;
    }
}

// Prints |format| for one file (stat.c's print_it), appending to the output
// as the directives resolve; |escapes| is --printf's backslash handling.
// Returns false when an invalid directive was met -- already reported, with
// everything before it printed -- and the whole command then ends with
// status 1, as GNU's does at once.
bool PrintStatFormat(BuiltinContext& context, std::string_view format,
                     const std::string& name, const FileStatus& status,
                     bool fileSystem, bool escapes) {
    std::string out;
    size_t pos = 0;
    while (pos < format.size()) {
        const char c = format[pos];
        if (c == '\\' && escapes) {
            ++pos;
            AppendStatEscape(context, format, pos, out);
            continue;
        }
        if (c != '%') {
            out += c;
            ++pos;
            continue;
        }
        ++pos;
        // The flags, width and precision, as stat.c's print_it scans them
        // (not a printf specification: %h and %Ld are directives here, and
        // the H and L that select a device number's half come after).
        const size_t specStart = pos;
        std::string flags;
        std::optional<int> width;
        bool precisionSpecified = false;
        bool precisionDigitsGiven = false;
        int precision = 0;
        while (pos < format.size() && IsStatFlag(format[pos])) {
            flags += format[pos];
            ++pos;
        }
        if (pos < format.size() && format[pos] >= '0' && format[pos] <= '9') {
            width = ReadDigitRun(format, pos);
        }
        if (pos < format.size() && format[pos] == '.') {
            precisionSpecified = true;
            ++pos;
            if (pos < format.size() && format[pos] >= '0' && format[pos] <= '9') {
                precisionDigitsGiven = true;
                precision = ReadDigitRun(format, pos);
            }
        }
        const std::string spec(format.substr(specStart, pos - specStart));
        if (pos >= format.size()) {
            // The format ended inside a directive: a bare '%' prints '%',
            // anything scanned in front of it is an invalid directive.
            if (spec.empty()) {
                out += '%';
                break;
            }
            context.Out(out);
            context.Error("'%" + spec + "': invalid directive");
            return false;
        }
        char directive = format[pos];
        if (directive == '%') {
            if (!spec.empty()) {
                context.Out(out);
                context.Error("'%" + spec + "%': invalid directive");
                return false;
            }
            out += '%';
            ++pos;
            continue;
        }
        // An H or L before d or r selects the device number's major or minor
        // half; before anything else it is a directive of its own.
        char prefix = 0;
        if ((directive == 'H' || directive == 'L') && pos + 1 < format.size()
            && (format[pos + 1] == 'd' || format[pos + 1] == 'r')) {
            prefix = directive;
            directive = format[pos + 1];
            pos += 2;
        } else {
            ++pos;
        }
        PrintStatDirective(flags, width, precisionSpecified, precisionDigitsGiven, precision,
                           prefix, directive, name, status, fileSystem, out);
    }
    context.Out(out);
    return true;
}

class StatCommand : public IBuiltinCommand {
public:
    std::string Name() const override { return "stat"; }
    std::string Version() const override { return "1.0.0"; }

    const std::vector<BuiltinOption>& Options() const override {
        using A = BuiltinArgument;
        static const std::vector<BuiltinOption> options = {
            {'L', "dereference", kOptionDereference, A::None, "", "follow links (no links here: no effect)"},
            {'f', "file-system", kOptionFileSystem, A::None, "", "display file system status instead of file status"},
            {0, "cached", kBuiltinNotTreated, A::Required, "MODE"},
            {'c', "format", kOptionFormat, A::Required, "FORMAT", "use FORMAT instead of the default; a newline after each file"},
            {0, "printf", kOptionPrintf, A::Required, "FORMAT", "like --format, but interpret backslash escapes and add no newline"},
            {'t', "terse", kOptionTerse, A::None, "", "print the information in terse form"},
        };
        return options;
    }

    BuiltinHelp Help() const override {
        return BuiltinHelp{
            "display file or file system status",
            {"stat [OPTION]... FILE..."},
            "The format directives are GNU stat 9.4's, for files and, with -f, for\n"
            "file systems; --terse is GNU's own terse FORMAT. Device and inode numbers\n"
            "are 0, permissions 0777/rwxrwxrwx, uid and gid 0/haisos, the I/O block\n"
            "4096, the birth time '-' (unknown), %m '/', and %C '?' without a message\n"
            "(no security contexts). With -f the values are fixed: ID 0, Namelen 255,\n"
            "Type haisos, block sizes 4096, block and inode counts 0 -- Haisos has no\n"
            "filesystem statistics. -L changes nothing (no symbolic links),\n"
            "QUOTING_STYLE is not read (%N is always shell-escape quoted), and '-' is\n"
            "a file name, not the standard input.",
        };
    }

    int Run(BuiltinContext& context) override {
        int exitStatus = 0;
        const auto parsed = BeginBuiltin(context, *this, /*usageErrorStatus=*/1, exitStatus);
        if (!parsed) {
            return exitStatus;
        }

        bool fileSystem = false;
        bool terse = false;
        // A --format/--printf given wins over --terse whatever their order,
        // as GNU's fixed precedence does; between the two, the later wins,
        // with its own escapes and trailing newline.
        bool formatGiven = false;
        bool escapes = false;
        std::string userFormat;
        for (const auto& option : parsed->options) {
            switch (option.id) {
                case kOptionFileSystem:
                    fileSystem = true;
                    break;
                case kOptionFormat:
                    formatGiven = true;
                    escapes = false;
                    userFormat = option.argument;
                    break;
                case kOptionPrintf:
                    formatGiven = true;
                    escapes = true;
                    userFormat = option.argument;
                    break;
                case kOptionTerse:
                    terse = true;
                    break;
                default:
                    break;  // -L: no effect; --cached: not treated, already reported
            }
        }

        if (parsed->operands.empty()) {
            context.Error("missing operand");
            context.TryHelp();
            return 1;
        }

        // The format of one file: the user's, or GNU's defaults -- the
        // device variant for a character device, as GNU's own per-file
        // choice.
        const auto formatFor = [&](const FileStatus& fileStatus) -> const char* {
            if (formatGiven) {
                return nullptr;  // userFormat, not one of the constants
            }
            if (terse) {
                return fileSystem ? kFsTerseFormat : kTerseFormat;
            }
            if (fileSystem) {
                return kFsDefaultFormat;
            }
            return fileStatus.type == DirectoryEntryType::CharDevice ? kDefaultDeviceFormat
                                                                     : kDefaultFormat;
        };

        int status = 0;
        for (const std::string& name : parsed->operands) {
            if (context.StopRequested()) {
                break;
            }
            FileStatus fileStatus;
            if (context.IO().Stat(name, fileStatus) != 0) {
                const std::string reason = CopyStatMissingReason(context.IO(), name);
                context.Error(std::string(fileSystem
                        ? "cannot read file system information for "
                        : "cannot statx ")
                    + ShellEscapeQuoted(name, true) + ": " + reason);
                status = 1;
                continue;
            }
            const char* builtIn = formatFor(fileStatus);
            const bool printed = builtIn == nullptr
                ? PrintStatFormat(context, userFormat, name, fileStatus, fileSystem, escapes)
                : PrintStatFormat(context, builtIn, name, fileStatus, fileSystem, /*escapes=*/false);
            if (!printed) {
                return 1;
            }
            // -c adds the newline after each file itself; --printf adds
            // nothing, and the built-in formats end with one of their own.
            if (formatGiven && !escapes) {
                context.Out("\n");
            }
        }
        return status;
    }
};

} // namespace

std::shared_ptr<IBuiltinCommand> CreateStatCommand() {
    return std::make_shared<StatCommand>();
}

} // namespace Haisos