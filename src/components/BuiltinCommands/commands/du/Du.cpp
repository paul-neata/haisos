#include <algorithm>
#include <cstdint>
#include <set>
#include <string>
#include <vector>
#include "BuiltinCommand.h"
#include "BuiltinDate.h"
#include "BuiltinFnmatch.h"
#include "BuiltinSize.h"
#include "BuiltinText.h"
#include "src/components/Filesystem/FilesystemUtils.h"

namespace Haisos {

namespace {

enum DuOption {
    kDuNull = 1,
    kDuAll,
    kDuApparentSize,
    kDuBlockSize,
    kDuBytes,
    kDuTotal,
    kDuDereferenceArgs,
    kDuMaxDepth,
    kDuFiles0From,
    kDuHuman,
    kDuInodes,
    kDuKibibytes,
    kDuDereference,
    kDuCountLinks,
    kDuMebibytes,
    kDuNoDereference,
    kDuSeparateDirs,
    kDuSi,
    kDuSummarize,
    kDuThreshold,
    kDuTime,
    kDuTimeStyle,
    kDuExcludeFrom,
    kDuExclude,
    kDuOneFileSystem,
};

enum class DuSizeStyle { Blocks, Human, Si };
enum class DuTimeField { Modification, Access, Change };

struct DuSettings {
    bool all = false;              // -a: files get a line too
    bool apparent = false;         // --apparent-size / -b
    bool inodes = false;           // --inodes
    bool separateDirs = false;     // -S
    bool countLinks = false;       // -l: count a file seen twice twice
    bool total = false;            // -c
    bool nullSeparator = false;    // -0
    bool summarize = false;        // -s
    bool maxDepthGiven = false;    // -d
    long maxDepth = -1;            // unlimited when negative
    DuSizeStyle style = DuSizeStyle::Blocks;
    uint64_t blockSize = 1024;
    // What a -B value with no leading digit shows after each size ("K",
    // "kB", "KiB"); empty when sizes print as bare numbers.
    std::string blockSuffix;
    bool thresholdGiven = false;
    int64_t threshold = 0;         // positive: at least SIZE; negative: at most |SIZE|
    bool showTime = false;
    DuTimeField timeField = DuTimeField::Modification;
    std::string timeStyle = "long-iso";  // or a +FORMAT, its '+' already off
    std::vector<std::string> excludePatterns;
};

// Joins a shown path and a name: "a/" keeps its slash ("a/b"), "." gains one
// ("./a"), "/" never doubles it ("/a").
std::string JoinPath(const std::string& parent, const std::string& name) {
    if (!parent.empty() && parent.back() == '/') {
        return parent + name;
    }
    return parent + "/" + name;
}

// Whether any exclude pattern matches |path|: the whole path, or any part of
// it after a '/' (gnulib's unanchored matching).
bool IsExcluded(const DuSettings& settings, const std::string& path) {
    for (const auto& pattern : settings.excludePatterns) {
        size_t start = 0;
        while (start <= path.size()) {
            if (FnMatch(pattern, std::string_view(path).substr(start), 0)) {
                return true;
            }
            const size_t slash = path.find('/', start);
            if (slash == std::string::npos) {
                break;
            }
            start = slash + 1;
        }
    }
    return false;
}

// The power a -B suffix letter stands for (k/K 1 through q/Q 10), 0 for the
// fixed letters b, c and w and for anything not a suffix at all.
int SuffixPower(char letter) {
    switch (letter) {
        case 'k': case 'K': return 1;
        case 'm': case 'M': return 2;
        case 'g': case 'G': return 3;
        case 't': case 'T': return 4;
        case 'p': case 'P': return 5;
        case 'e': case 'E': return 6;
        case 'z': case 'Z': return 7;
        case 'y': case 'Y': return 8;
        case 'r': case 'R': return 9;
        case 'q': case 'Q': return 10;
        default: return 0;
    }
}

// What a -B value with no leading digit shows after each size: GNU's own
// spelling of the unit ("K" for 1024s, "kB" for the 1000-based k, "KiB",
// "M", "MB", and b, c, w as they stand).
std::string BlockSuffixText(std::string_view value) {
    size_t pos = 0;
    while (pos < value.size() && (value[pos] == ' ' || value[pos] == '\t')) {
        ++pos;
    }
    if (pos >= value.size()) {
        return std::string();
    }
    const char letter = value[pos];
    const int power = SuffixPower(letter);
    if (power == 0) {
        return std::string(1, letter);
    }
    static const char kPowerLetters[] = "?KMGTPEZYRQ";  // by power, 1-based
    std::string text(1, kPowerLetters[power]);
    if (pos + 2 < value.size() && value[pos + 1] == 'i' && value[pos + 2] == 'B') {
        text += "iB";
    } else if (pos + 1 < value.size() && (value[pos + 1] == 'B' || value[pos + 1] == 'D')) {
        // A 1000-based unit: GNU writes its k in lower case ("kB").
        text += "B";
        text[0] = power == 1 ? 'k' : text[0];
    }
    return text;
}

// One entry's size as the lines show it: in 1K (or -B) blocks rounded up, in
// human-readable form with -h/--si, with --inodes the plain entry count, and
// with a -B suffix text after the number when one was given without digits.
std::string SizeText(const DuSettings& settings, uint64_t amount) {
    if (settings.inodes) {
        return std::to_string(amount);
    }
    switch (settings.style) {
        case DuSizeStyle::Human:
            return FormatHumanSize(amount, /*si=*/false);
        case DuSizeStyle::Si:
            return FormatHumanSize(amount, /*si=*/true);
        default: {
            const uint64_t blocks = amount / settings.blockSize
                + (amount % settings.blockSize != 0 ? 1 : 0);
            return std::to_string(blocks) + settings.blockSuffix;
        }
    }
}

// --time's TIME column, in the host's local zone.
std::string TimeText(const DuSettings& settings, const FileDateTime& time) {
    if (settings.timeStyle == "full-iso") {
        return FormatDateTime("%Y-%m-%d %H:%M:%S.%N %z", time, false);
    }
    if (settings.timeStyle == "long-iso") {
        return FormatDateTime("%Y-%m-%d %H:%M", time, false);
    }
    if (settings.timeStyle == "iso") {
        return FormatDateTime("%Y-%m-%d", time, false);
    }
    return FormatDateTime(settings.timeStyle, time, false);
}

const FileDateTime& TimeOf(const FileStatus& status, const DuSettings& settings) {
    switch (settings.timeField) {
        case DuTimeField::Access: return status.accessTime;
        case DuTimeField::Change: return status.changeTime;
        default: return status.modificationTime;
    }
}

// What one walked entry reports back to its parent.
struct DuWalk {
    uint64_t size = 0;          // what the parent counts for this entry
    uint64_t lineSize = 0;     // what this entry's own line shows
    FileDateTime latest{};     // the latest time in the subtree
    bool isDir = false;
    bool stopped = false;      // the command was asked to stop
};

class DuCommand : public IBuiltinCommand {
public:
    std::string Name() const override { return "du"; }
    std::string Version() const override { return "1.0.0"; }

    const std::vector<BuiltinOption>& Options() const override {
        using A = BuiltinArgument;
        static const std::vector<BuiltinOption> options = {
            {'0', "null", kDuNull, A::None, "", "end each output line with NUL, not newline"},
            {'a', "all", kDuAll, A::None, "", "write counts for all files, not just directories"},
            {0, "apparent-size", kDuApparentSize, A::None, "", "print apparent sizes, rather than disk usage"},
            {'B', "block-size", kDuBlockSize, A::Required, "SIZE", "scale sizes by SIZE before printing them"},
            {'b', "bytes", kDuBytes, A::None, "", "equivalent to '--apparent-size --block-size=1'"},
            {'c', "total", kDuTotal, A::None, "", "produce a grand total"},
            {'D', "dereference-args", kDuDereferenceArgs, A::None, "", "dereference only command line symlinks (none exist)"},
            {'d', "max-depth", kDuMaxDepth, A::Required, "N", "print a total only if N or fewer levels below"},
            {0, "files0-from", kDuFiles0From, A::Required, "F", "read NUL-separated names from F (- for stdin)"},
            {'H', "", kDuDereferenceArgs, A::None, "", "same as --dereference-args (-D)"},
            {'h', "human-readable", kDuHuman, A::None, "", "print sizes like 1K 234M 2G"},
            {0, "inodes", kDuInodes, A::None, "", "list inode usage numbers instead of block usage"},
            {'k', "", kDuKibibytes, A::None, "", "like --block-size=1K"},
            {'L', "dereference", kDuDereference, A::None, "", "dereference all symbolic links (none exist)"},
            {'l', "count-links", kDuCountLinks, A::None, "", "count sizes many times if hard linked"},
            {'m', "", kDuMebibytes, A::None, "", "like --block-size=1M"},
            {'P', "no-dereference", kDuNoDereference, A::None, "", "don't follow any symbolic links (the default)"},
            {'S', "separate-dirs", kDuSeparateDirs, A::None, "", "for directories do not include size of subdirectories"},
            {0, "si", kDuSi, A::None, "", "like -h, but use powers of 1000 not 1024"},
            {'s', "summarize", kDuSummarize, A::None, "", "display only a total for each argument"},
            {'t', "threshold", kDuThreshold, A::Required, "SIZE", "exclude entries smaller than SIZE (bigger if negative)"},
            {0, "time", kDuTime, A::Optional, "WORD", "show time of last modification of any file in the directory"},
            {0, "time-style", kDuTimeStyle, A::Required, "STYLE", "show times as full-iso, long-iso, iso, or +FORMAT"},
            {'X', "exclude-from", kDuExcludeFrom, A::Required, "FILE", "exclude files that match any pattern in FILE"},
            {0, "exclude", kDuExclude, A::Required, "PATTERN", "exclude files that match PATTERN"},
            {'x', "one-file-system", kDuOneFileSystem, A::None, "", "skip directories on different file systems (no devices)"},
        };
        return options;
    }

    BuiltinHelp Help() const override {
        return BuiltinHelp{
            "estimate file space usage",
            {"du [OPTION]... [FILE]...", "du [OPTION]... --files0-from=F"},
            "Sizes come from the filesystem's allocated 512-byte blocks; entries are visited in name order.\n"
            "Exit status: 0 if OK, 1 if a file could not be accessed.\n"
            "-D, -H, -L, -P and -x change nothing (no links or devices); a file seen twice is recognised\n"
            "by its resolved path; DU_BLOCK_SIZE, BLOCK_SIZE and BLOCKSIZE are not read."};
    }

    int Run(BuiltinContext& context) override {
        int exitStatus = 0;
        const auto parsed = BeginBuiltin(context, *this, /*usageErrorStatus=*/1, exitStatus);
        if (!parsed) {
            return exitStatus;
        }
        DuSettings settings;
        std::optional<std::string> files0From;
        if (!ApplyOptions(context, *parsed, settings, files0From)) {
            return 1;
        }

        if (settings.summarize && settings.all) {
            context.Error("cannot both summarize and show all entries");
            context.TryHelp();
            return 1;
        }
        if (settings.summarize && settings.maxDepthGiven && settings.maxDepth > 0) {
            context.Error("warning: summarizing conflicts with --max-depth=" + std::to_string(settings.maxDepth));
            context.TryHelp();
            return 1;
        }
        if (settings.summarize) {
            settings.maxDepth = 0;
        }

        // The operands: the command line's, or the NUL-separated names of
        // --files0-from=F; neither, ".".
        std::vector<std::string> operands;
        bool namesFromStdin = false;
        if (files0From) {
            if (!parsed->operands.empty()) {
                context.Error("extra operand " + ShellEscapeQuoted(parsed->operands.front(), /*always=*/true));
                context.ErrorText("file operands cannot be combined with --files0-from\n");
                context.TryHelp();
                return 1;
            }
            std::string list;
            if (*files0From == "-") {
                namesFromStdin = true;
                const auto input = context.IO().GetDescriptor(IFileIO::kStdIn);
                if (!input || !ReadWholeList(context, *input, list)) {
                    return 1;
                }
            } else {
                const std::string shown = ShellEscapeQuoted(*files0From, /*always=*/true);
                FileStatus status;
                if (context.IO().Stat(*files0From, status) != 0) {
                    context.Error("cannot open " + shown + " for reading: No such file or directory");
                    return 1;
                }
                if (status.type == DirectoryEntryType::Dir) {
                    context.Error(shown + ": read error: Is a directory");
                    return 1;
                }
                const auto handle = context.IO().OpenFile(*files0From, kFileOpenReadOnly);
                if (!handle || !ReadWholeList(context, *handle, list)) {
                    context.Error("cannot open " + shown + " for reading: Permission denied");
                    return 1;
                }
            }
            operands = SplitNames(list);
            for (size_t i = 0; i < operands.size(); ++i) {
                if (operands[i].empty()) {
                    context.Error(ShellEscapeQuoted(*files0From) + ":" + std::to_string(i + 1)
                        + ": invalid zero-length file name");
                    return 1;
                }
                if (namesFromStdin && operands[i] == "-") {
                    context.Error("when reading file names from stdin, no file name of '-' allowed");
                    return 1;
                }
            }
        } else {
            operands = parsed->operands;
            if (operands.empty()) {
                operands.push_back(".");
            }
        }

        int status = 0;
        uint64_t grand = 0;
        FileDateTime latest{};
        std::set<std::string> seen;
        for (const auto& operand : operands) {
            if (context.StopRequested()) {
                return 1;
            }
            const DuWalk walk = WalkEntry(context, settings, operand, context.IO().ResolvePath(operand),
                /*depth=*/0, /*operand=*/true, seen, status);
            if (walk.stopped) {
                return 1;
            }
            grand += walk.lineSize;
            if (walk.latest > latest) {
                latest = walk.latest;
            }
        }
        if (settings.total) {
            PrintLine(context, settings, SizeText(settings, grand),
                settings.showTime ? &latest : nullptr, "total");
        }
        return status;
    }

private:
    // Turns the parsed options into settings, reporting a bad value to one as
    // du does (exit status 1); false when the command is done for.
    static bool ApplyOptions(BuiltinContext& context, const ParsedBuiltinArgs& parsed,
                             DuSettings& settings, std::optional<std::string>& files0From) {
        // The size settings are applied in the order given, the last winning
        // ("-h -k" is "-k", "-k -h" is "-h").
        auto setBlockStyle = [&settings](DuSizeStyle style, uint64_t blockSize, std::string suffix = std::string()) {
            settings.style = style;
            settings.blockSize = blockSize;
            settings.blockSuffix = std::move(suffix);
        };
        for (const auto& option : parsed.options) {
            const std::string& value = option.argument;
            switch (option.id) {
                case kDuNull: settings.nullSeparator = true; break;
                case kDuAll: settings.all = true; break;
                case kDuApparentSize: settings.apparent = true; break;
                case kDuBlockSize: {
                    std::string spec = value;
                    if (!spec.empty() && spec[0] == '\'') {
                        spec.erase(0, 1);  // a leading quote is ignored
                    }
                    if (spec == "human-readable") {
                        setBlockStyle(DuSizeStyle::Human, 1024);
                        break;
                    }
                    if (spec == "si") {
                        setBlockStyle(DuSizeStyle::Si, 1000);
                        break;
                    }
                    uintmax_t size = 0;
                    const SizeParse result = ParseSizeWithSuffix(spec, "eEgGkKmMpPtTyYzZ0", size);
                    if (result == SizeParse::InvalidSuffix) {
                        context.Error("invalid suffix in " + option.spelling + " argument " + GnuQuote(value));
                        return false;
                    }
                    if (result != SizeParse::Ok || size == 0) {
                        context.Error("invalid " + option.spelling + " argument " + GnuQuote(value));
                        return false;
                    }
                    // A value with no leading digit ("K", "KB", "KiB") shows
                    // its suffix after each size; one with digits ("1K",
                    // "1000") prints bare numbers.
                    const size_t firstNonBlank = spec.find_first_not_of(" \t");
                    const bool hasDigits = firstNonBlank != std::string::npos
                        && spec[firstNonBlank] >= '0' && spec[firstNonBlank] <= '9';
                    setBlockStyle(DuSizeStyle::Blocks, static_cast<uint64_t>(size),
                        hasDigits ? std::string() : BlockSuffixText(spec));
                    break;
                }
                case kDuBytes:
                    settings.apparent = true;
                    setBlockStyle(DuSizeStyle::Blocks, 1);
                    break;
                case kDuTotal: settings.total = true; break;
                case kDuDereferenceArgs: case kDuDereference: case kDuNoDereference:
                case kDuOneFileSystem:
                    break;  // accepted with no effect: no links or devices
                case kDuMaxDepth: {
                    if (value.empty() || value.find_first_not_of("0123456789") != std::string::npos) {
                        context.Error("invalid maximum depth " + GnuQuote(value));
                        context.TryHelp();
                        return false;
                    }
                    // However deep, every walk is far shallower; a huge N is
                    // taken as the deepest there is.
                    uint64_t depth = 0;
                    for (const char c : value) {
                        if (depth > (uint64_t{1} << 40)) {
                            break;
                        }
                        depth = depth * 10 + static_cast<uint64_t>(c - '0');
                    }
                    settings.maxDepth = static_cast<long>(depth > (uint64_t{1} << 40)
                        ? (uint64_t{1} << 40) : depth);
                    settings.maxDepthGiven = true;
                    break;
                }
                case kDuFiles0From: files0From = value; break;
                case kDuHuman: setBlockStyle(DuSizeStyle::Human, 1024); break;
                case kDuInodes: settings.inodes = true; break;
                case kDuKibibytes: setBlockStyle(DuSizeStyle::Blocks, 1024); break;
                case kDuCountLinks: settings.countLinks = true; break;
                case kDuMebibytes: setBlockStyle(DuSizeStyle::Blocks, 1048576); break;
                case kDuSeparateDirs: settings.separateDirs = true; break;
                case kDuSi: setBlockStyle(DuSizeStyle::Si, 1000); break;
                case kDuSummarize: settings.summarize = true; break;
                case kDuThreshold: {
                    std::string spec = value;
                    bool negative = false;
                    if (!spec.empty() && spec[0] == '-') {
                        negative = true;
                        spec.erase(0, 1);
                    }
                    uintmax_t size = 0;
                    if (ParseSizeWithSuffix(spec, "kKmMGTPEZYRQ0", size) != SizeParse::Ok) {
                        context.Error("invalid " + option.spelling + " argument " + GnuQuote(value));
                        return false;
                    }
                    settings.thresholdGiven = true;
                    settings.threshold = negative ? -static_cast<int64_t>(size) : static_cast<int64_t>(size);
                    break;
                }
                case kDuTime:
                    settings.showTime = true;
                    if (option.hasArgument) {
                        const auto field = ArgMatch(context, "--time", value, {
                            {"atime", static_cast<int>(DuTimeField::Access)},
                            {"access", static_cast<int>(DuTimeField::Access)},
                            {"use", static_cast<int>(DuTimeField::Access)},
                            {"ctime", static_cast<int>(DuTimeField::Change)},
                            {"status", static_cast<int>(DuTimeField::Change)},
                        });
                        if (!field) {
                            return false;
                        }
                        settings.timeField = static_cast<DuTimeField>(*field);
                    }
                    break;
                case kDuTimeStyle: {
                    if (!value.empty() && value[0] == '+') {
                        settings.timeStyle = value.substr(1);
                        break;
                    }
                    const auto style = ArgMatch(context, "--time-style", value, {
                        {"full-iso", 1},
                        {"long-iso", 2},
                        {"iso", 3},
                    });
                    if (!style) {
                        return false;
                    }
                    settings.timeStyle = *style == 1 ? "full-iso" : *style == 2 ? "long-iso" : "iso";
                    break;
                }
                case kDuExcludeFrom:
                    if (!AddExcludeFile(context, value, settings.excludePatterns)) {
                        return false;
                    }
                    break;
                case kDuExclude: settings.excludePatterns.push_back(value); break;
                default: break;
            }
        }
        return true;
    }

    // Reads --files0-from's whole stream. Returns false on a stop (quietly).
    static bool ReadWholeList(BuiltinContext& context, IFileDescriptor& input, std::string& out) {
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

    // One pattern per line of an -X FILE, as grep's --exclude-from does (the
    // same gnulib exclude module behind both).
    static bool AddExcludeFile(BuiltinContext& context, const std::string& name,
                               std::vector<std::string>& patterns) {
        InputOpenFailure failure = InputOpenFailure::None;
        const auto input = OpenInputOperand(context, name, failure);
        if (!input) {
            const char* why;
            switch (failure) {
                case InputOpenFailure::Missing: why = "No such file or directory"; break;
                case InputOpenFailure::Directory: why = "Is a directory"; break;
                case InputOpenFailure::Denied: why = "Permission denied"; break;
                default: why = "Bad file descriptor"; break;
            }
            context.Error(name + ": " + why);
            return false;
        }
        BuiltinLineReader reader(context, *input, '\n');
        std::string line;
        bool delimited = false;
        while (true) {
            const LineReadResult read = reader.Next(line, delimited);
            if (read == LineReadResult::End) {
                return true;
            }
            if (read == LineReadResult::Stopped) {
                return false;  // quiet; the process reports 143
            }
            if (read == LineReadResult::Error) {
                context.Error(name + ": Input/output error");
                return false;
            }
            patterns.push_back(line);
        }
    }

    // Whether --threshold hides a line: a positive SIZE hides entries below
    // it, a negative one entries above |SIZE|.
    static bool HiddenByThreshold(const DuSettings& settings, uint64_t size) {
        if (!settings.thresholdGiven) {
            return false;
        }
        return settings.threshold >= 0 ? size < static_cast<uint64_t>(settings.threshold)
            : size > static_cast<uint64_t>(-settings.threshold);
    }

    static void PrintLine(BuiltinContext& context, const DuSettings& settings, const std::string& size,
                         const FileDateTime* time, const std::string& path) {
        std::string line = size + "\t";
        if (time != nullptr) {
            line += TimeText(settings, *time) + "\t";
        }
        line += path;
        line += settings.nullSeparator ? '\0' : '\n';
        context.Out(line);
    }

    static bool DepthShown(const DuSettings& settings, size_t depth) {
        return settings.maxDepth < 0 || static_cast<long>(depth) <= settings.maxDepth;
    }

    // One entry, depth-first and post-order (each subtree completing before
    // its next sibling), printing its line and returning what its parent
    // counts for it. |shownPath| is the entry as its line names it, |absPath|
    // where it is.
    static DuWalk WalkEntry(BuiltinContext& context, const DuSettings& settings,
                            const std::string& shownPath, const std::string& absPath,
                            size_t depth, bool operand, std::set<std::string>& seen, int& status) {
        DuWalk walk;
        if (context.StopRequested()) {
            walk.stopped = true;
            return walk;
        }
        // An excluded entry is neither shown nor counted.
        if (IsExcluded(settings, shownPath)) {
            return walk;
        }
        IFileIO& io = context.IO();
        FileStatus fileStatus;
        if (io.Stat(absPath, fileStatus) != 0) {
            context.Error("cannot access " + ShellEscapeQuoted(shownPath, /*always=*/true)
                + ": No such file or directory");
            status = 1;
            return walk;
        }
        walk.latest = TimeOf(fileStatus, settings);
        if (fileStatus.type != DirectoryEntryType::Dir) {
            walk.isDir = false;
            // A file seen twice is skipped, as GNU skips a second hard link
            // to it (unless -l counts it again).
            if (!settings.countLinks && !seen.insert(absPath).second) {
                return walk;
            }
            walk.size = walk.lineSize = settings.inodes
                ? 1
                : settings.apparent ? fileStatus.size : fileStatus.blocks * 512;
            if (DepthShown(settings, depth) && (settings.all || operand)
                && !HiddenByThreshold(settings, walk.lineSize)) {
                PrintLine(context, settings, SizeText(settings, walk.lineSize),
                    settings.showTime ? &walk.latest : nullptr, shownPath);
            }
            return walk;
        }

        walk.isDir = true;
        std::vector<DirectoryEntry> entries = io.ReadDirectory(absPath);
        if (entries.empty()) {
            context.Error("cannot read directory " + ShellEscapeQuoted(shownPath, /*always=*/true)
                + ": Permission denied");
            status = 1;
            return walk;
        }
        std::sort(entries.begin(), entries.end(),
            [](const DirectoryEntry& a, const DirectoryEntry& b) { return a.name < b.name; });
        // An entry's own size (a directory holds none of its own, or 1 inode),
        // then its children's: all of them, or with -S only the files directly
        // inside, in which case a subdirectory line keeps its own subtree.
        const uint64_t own = settings.inodes ? 1
            : settings.apparent ? 0 : fileStatus.blocks * 512;
        uint64_t fullSum = own;
        uint64_t separateSum = own;
        for (const auto& entry : entries) {
            if (entry.name == "." || entry.name == "..") {
                continue;
            }
            if (context.StopRequested()) {
                walk.stopped = true;
                return walk;
            }
            const DuWalk child = WalkEntry(context, settings, JoinPath(shownPath, entry.name),
                JoinPath(absPath, entry.name), depth + 1, /*operand=*/false, seen, status);
            if (child.stopped) {
                walk.stopped = true;
                return walk;
            }
            fullSum += child.size;
            if (!child.isDir) {
                separateSum += child.size;
            }
            if (child.latest > walk.latest) {
                walk.latest = child.latest;
            }
        }
        walk.lineSize = settings.separateDirs ? separateSum : fullSum;
        walk.size = settings.separateDirs ? 0 : fullSum;
        if (DepthShown(settings, depth) && !HiddenByThreshold(settings, walk.lineSize)) {
            PrintLine(context, settings, SizeText(settings, walk.lineSize),
                settings.showTime ? &walk.latest : nullptr, shownPath);
        }
        return walk;
    }
};

} // namespace

std::shared_ptr<IBuiltinCommand> CreateDuCommand() {
    return std::make_shared<DuCommand>();
}

} // namespace Haisos