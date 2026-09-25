#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include "BuiltinCommand.h"
#include "src/components/Filesystem/FilesystemUtils.h"

namespace Haisos {

namespace {

// The owner and group every entry is shown with: HaisosOS has no users yet,
// but the columns are kept, so the listing reads exactly like the real one.
constexpr const char* kOwner = "haisos";
constexpr const char* kGroup = "haisos";
constexpr size_t kDefaultWidth = 80;
constexpr size_t kColumnGap = 2;
// Half a Gregorian year, which is what GNU ls counts as "recent".
constexpr int64_t kSixMonths = 31556952 / 2;

enum LsOption {
    kAll = 1,
    kAlmostAll,
    kIgnoreBackups,
    kCtime,
    kColumns,
    kDirectory,
    kAllUnsorted,
    kFileType,
    kFormat,
    kFullTime,
    kLongNoOwner,
    kGroupDirectoriesFirst,
    kNoGroup,
    kHuman,
    kKibibytes,
    kLong,
    kCommas,
    kLiteral,
    kLongNoGroup,
    kSlash,
    kReverse,
    kRecursive,
    kSize,
    kSortBySize,
    kSort,
    kTime,
    kTimeStyle,
    kSortByTime,
    kAtime,
    kUnsorted,
    kWidth,
    kAcross,
    kSortByExtension,
    kOnePerLine,
};

enum class LsFormat { Columns, Across, Commas, OnePerLine, Long };
enum class LsSort { Name, None, Size, Time, Extension };
enum class LsTime { Modification, Access, Change };

struct LsSettings {
    bool all = false;
    bool almostAll = false;
    bool ignoreBackups = false;
    bool directory = false;
    bool recursive = false;
    bool reverse = false;
    bool human = false;
    bool showSize = false;
    bool groupDirectoriesFirst = false;
    bool slash = false;
    bool literal = false;
    bool showOwner = true;
    bool showGroup = true;
    LsFormat format = LsFormat::Columns;
    LsSort sort = LsSort::Name;
    bool sortGiven = false;
    LsTime time = LsTime::Modification;
    std::string timeStyle = "locale";
    size_t width = kDefaultWidth;
};

// One thing to show: what it is called in the listing, where it is, and its
// status.
struct LsEntry {
    std::string name;
    std::string absolutePath;
    FileStatus status;
};

// --- Formatting pieces ---

// GNU ls's shell-escape quoting, as it prints to a terminal: a name holding
// anything a shell would read specially is shown in quotes.
bool NeedsQuoting(const std::string& name) {
    if (name.empty()) {
        return true;
    }
    if (name[0] == '~' || name[0] == '#') {
        return true;
    }
    for (unsigned char c : name) {
        if (c < 0x20 || c == 0x7f) {
            return true;
        }
        switch (c) {
            case ' ': case '!': case '"': case '$': case '&': case '\'': case '(': case ')':
            case '*': case ';': case '<': case '=': case '>': case '?': case '[': case '\\':
            case ']': case '^': case '`': case '{': case '|': case '}':
                return true;
            default:
                break;
        }
    }
    return false;
}

std::string Quote(const std::string& name) {
    std::string visible;
    for (unsigned char c : name) {
        visible += (c < 0x20 || c == 0x7f) ? '?' : static_cast<char>(c);
    }
    if (visible.find('\'') == std::string::npos) {
        return "'" + visible + "'";
    }
    if (visible.find_first_of("\"$`\\!") == std::string::npos) {
        return "\"" + visible + "\"";
    }
    std::string quoted = "'";
    for (char c : visible) {
        quoted += (c == '\'') ? std::string("'\\''") : std::string(1, c);
    }
    return quoted + "'";
}

// GNU's -h: powers of 1024, rounded up, one decimal below 10 ("1.5K", "12K").
std::string HumanSize(uint64_t bytes) {
    if (bytes < 1024) {
        return std::to_string(bytes);
    }
    static const char kUnits[] = "KMGTPEZY";
    double value = static_cast<double>(bytes);
    size_t unit = 0;
    value /= 1024;
    while (true) {
        double shown = value < 10 ? std::ceil(value * 10) / 10 : std::ceil(value);
        if (shown < 1024 || unit + 1 >= sizeof(kUnits) - 1) {
            char buffer[32];
            if (shown < 10) {
                std::snprintf(buffer, sizeof(buffer), "%.1f%c", shown, kUnits[unit]);
            } else {
                std::snprintf(buffer, sizeof(buffer), "%.0f%c", shown, kUnits[unit]);
            }
            return buffer;
        }
        value /= 1024;
        ++unit;
    }
}

// Allocated size, as -s and the total line show it: 1K blocks (rounded up),
// or with -h, human-readable bytes.
std::string AllocatedSize(uint64_t blocks512, bool human) {
    return human ? HumanSize(blocks512 * 512) : std::to_string((blocks512 + 1) / 2);
}

std::tm LocalTime(int64_t seconds) {
    const std::time_t asTimeT = static_cast<std::time_t>(seconds);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &asTimeT);
#else
    localtime_r(&asTimeT, &local);
#endif
    return local;
}

// strftime, with %N (nanoseconds, as GNU date and ls have it) and %e (the day,
// space-padded -- not every C library knows it) expanded first.
std::string FormatDateTime(const std::string& format, const FileDateTime& time) {
    const std::tm local = LocalTime(time.seconds);
    std::string expanded;
    for (size_t i = 0; i < format.size(); ++i) {
        if (format[i] == '%' && i + 1 < format.size()) {
            char buffer[16];
            if (format[i + 1] == 'N') {
                std::snprintf(buffer, sizeof(buffer), "%09u", static_cast<unsigned>(time.nanoseconds));
                expanded += buffer;
                ++i;
                continue;
            }
            if (format[i + 1] == 'e') {
                std::snprintf(buffer, sizeof(buffer), "%2d", local.tm_mday);
                expanded += buffer;
                ++i;
                continue;
            }
            if (format[i + 1] == '%') {
                expanded += "%%";
                ++i;
                continue;
            }
        }
        expanded += format[i];
    }
    char buffer[256];
    const size_t written = std::strftime(buffer, sizeof(buffer), expanded.c_str(), &local);
    return std::string(buffer, written);
}

// The time column for --time-style STYLE (already validated).
std::string FormatTimeColumn(const std::string& style, const FileDateTime& time, int64_t now) {
    const bool recent = time.seconds <= now && now - time.seconds < kSixMonths;
    if (style == "full-iso") {
        return FormatDateTime("%Y-%m-%d %H:%M:%S.%N %z", time);
    }
    if (style == "long-iso") {
        return FormatDateTime("%Y-%m-%d %H:%M", time);
    }
    if (style == "iso") {
        return FormatDateTime(recent ? "%m-%d %H:%M" : "%Y-%m-%d ", time);
    }
    if (!style.empty() && style[0] == '+') {
        // "+FORMAT", or "+OLD<newline>RECENT".
        const std::string formats = style.substr(1);
        const size_t newline = formats.find('\n');
        if (newline == std::string::npos) {
            return FormatDateTime(formats, time);
        }
        return FormatDateTime(recent ? formats.substr(newline + 1) : formats.substr(0, newline), time);
    }
    return FormatDateTime(recent ? "%b %e %H:%M" : "%b %e  %Y", time);
}

bool IsValidTimeStyle(const std::string& style) {
    return style == "full-iso" || style == "long-iso" || style == "iso" || style == "locale" ||
        (!style.empty() && style[0] == '+');
}

std::string Extension(const std::string& name) {
    const size_t dot = name.find_last_of('.');
    return (dot == std::string::npos || dot == 0) ? std::string() : name.substr(dot + 1);
}

bool ParseWidth(const std::string& text, size_t& width) {
    if (text.empty() || text.find_first_not_of("0123456789") != std::string::npos || text.size() > 9) {
        return false;
    }
    width = static_cast<size_t>(std::stoul(text));
    return true;
}

class LsCommand : public IBuiltinCommand {
public:
    std::string Name() const override { return "ls"; }
    std::string Version() const override { return "1.1.0"; }

    const std::vector<BuiltinOption>& Options() const override {
        using A = BuiltinArgument;
        static const std::vector<BuiltinOption> options = {
            {'a', "all", kAll, A::None, "", "include entries starting with ."},
            {'A', "almost-all", kAlmostAll, A::None, "", "like -a, without . and .."},
            {0, "author", kBuiltinNotTreated},
            {'b', "escape", kBuiltinNotTreated},
            {0, "block-size", kBuiltinNotTreated, A::Required, "SIZE"},
            {'B', "ignore-backups", kIgnoreBackups, A::None, "", "skip entries ending with ~"},
            {'c', "", kCtime, A::None, "", "use status change time"},
            {'C', "", kColumns, A::None, "", "list by columns"},
            {0, "color", kBuiltinNotTreated, A::Optional, "WHEN"},
            {'d', "directory", kDirectory, A::None, "", "list directories themselves"},
            {'D', "dired", kBuiltinNotTreated},
            {'f', "", kAllUnsorted, A::None, "", "same as -a -U"},
            {'F', "classify", kBuiltinNotTreated, A::Optional, "WHEN"},
            {0, "file-type", kFileType, A::None, "", "append / to directories"},
            {0, "format", kFormat, A::Required, "WORD", "across commas horizontal long single-column verbose vertical"},
            {0, "full-time", kFullTime, A::None, "", "like -l --time-style=full-iso"},
            {'g', "", kLongNoOwner, A::None, "", "like -l, without owner"},
            {0, "group-directories-first", kGroupDirectoriesFirst, A::None, "", "directories before files"},
            {'G', "no-group", kNoGroup, A::None, "", "no group in -l"},
            {'h', "human-readable", kHuman, A::None, "", "sizes like 1K 234M 2G"},
            {0, "si", kBuiltinNotTreated},
            {'H', "dereference-command-line", kBuiltinNotTreated},
            {0, "dereference-command-line-symlink-to-dir", kBuiltinNotTreated},
            {0, "hide", kBuiltinNotTreated, A::Required, "PATTERN"},
            {0, "hyperlink", kBuiltinNotTreated, A::Optional, "WHEN"},
            {0, "indicator-style", kBuiltinNotTreated, A::Required, "WORD"},
            {'i', "inode", kBuiltinNotTreated},
            {'I', "ignore", kBuiltinNotTreated, A::Required, "PATTERN"},
            {'k', "kibibytes", kKibibytes, A::None, "", "1024-byte blocks (default)"},
            {'l', "", kLong, A::None, "", "long listing format"},
            {'L', "dereference", kBuiltinNotTreated},
            {'m', "", kCommas, A::None, "", "comma-separated list"},
            {'n', "numeric-uid-gid", kBuiltinNotTreated},
            {'N', "literal", kLiteral, A::None, "", "names unquoted"},
            {'o', "", kLongNoGroup, A::None, "", "like -l, without group"},
            {'p', "", kSlash, A::None, "", "append / to directories"},
            {'q', "hide-control-chars", kBuiltinNotTreated},
            {0, "show-control-chars", kBuiltinNotTreated},
            {'Q', "quote-name", kBuiltinNotTreated},
            {0, "quoting-style", kBuiltinNotTreated, A::Required, "WORD"},
            {'r', "reverse", kReverse, A::None, "", "reverse the sort order"},
            {'R', "recursive", kRecursive, A::None, "", "list subdirectories recursively"},
            {'s', "size", kSize, A::None, "", "allocated size, in blocks"},
            {'S', "", kSortBySize, A::None, "", "sort by size, largest first"},
            {0, "sort", kSort, A::Required, "WORD", "none name size time extension"},
            {0, "time", kTime, A::Required, "WORD", "atime access use ctime status mtime modification"},
            {0, "time-style", kTimeStyle, A::Required, "STYLE", "full-iso long-iso iso locale +FORMAT"},
            {'t', "", kSortByTime, A::None, "", "sort by time, newest first"},
            {'T', "tabsize", kBuiltinNotTreated, A::Required, "COLS"},
            {'u', "", kAtime, A::None, "", "use access time"},
            {'U', "", kUnsorted, A::None, "", "do not sort"},
            {'v', "", kBuiltinNotTreated},
            {'w', "width", kWidth, A::Required, "COLS", "line width (0: no limit)"},
            {'x', "", kAcross, A::None, "", "list by lines instead of columns"},
            {'X', "", kSortByExtension, A::None, "", "sort by extension"},
            {'Z', "context", kBuiltinNotTreated},
            {0, "zero", kBuiltinNotTreated},
            {'1', "", kOnePerLine, A::None, "", "one file per line"},
        };
        return options;
    }

    BuiltinHelp Help() const override {
        return BuiltinHelp{
            "list directory contents",
            {"ls [OPTION]... [FILE]..."},
            "Owner and group show as haisos; permissions as rwxrwxrwx.\n"
            "Exit status: 0 if OK, 2 if a FILE could not be accessed.\n"};
    }

    int Run(BuiltinContext& context) override {
        int exitStatus = 0;
        const auto parsed = BeginBuiltin(context, *this, /*usageErrorStatus=*/2, exitStatus);
        if (!parsed) {
            return exitStatus;
        }
        LsSettings settings;
        if (!ApplyOptions(context, *parsed, settings)) {
            return 2;
        }

        std::vector<std::string> operands = parsed->operands;
        if (operands.empty()) {
            operands.push_back(".");
        }

        // As the real ls does: operands that are files (or, with -d,
        // directories too) are listed first, together; then each directory's
        // contents, headed by its name when there is more than one thing to show.
        IFileIO& io = context.IO();
        int status = 0;
        std::vector<LsEntry> files;
        std::vector<LsEntry> directories;
        for (const auto& operand : operands) {
            LsEntry entry{operand, io.ResolvePath(operand), FileStatus{}};
            if (io.Stat(entry.absolutePath, entry.status) != 0) {
                context.Error("cannot access '" + operand + "': No such file or directory");
                status = 2;
                continue;
            }
            if (entry.status.type == DirectoryEntryType::Dir && !settings.directory) {
                directories.push_back(std::move(entry));
            } else {
                files.push_back(std::move(entry));
            }
        }
        Sort(files, settings);
        Sort(directories, settings);

        bool printedSomething = false;
        if (!files.empty()) {
            ListEntries(context, files, settings, /*withTotal=*/false);
            printedSomething = true;
        }
        const bool showHeaders = settings.recursive || operands.size() > 1;
        for (const auto& directory : directories) {
            if (context.StopRequested()) {
                return status;
            }
            ListDirectory(context, directory.name, directory.absolutePath, settings, showHeaders, printedSomething);
        }
        return status;
    }

private:
    // Turns the parsed options into settings; reports a bad argument to one
    // (as ls would, exit status 2) and returns false.
    static bool ApplyOptions(BuiltinContext& context, const ParsedBuiltinArgs& parsed, LsSettings& settings) {
        auto invalid = [&context](const std::string& value, const std::string& option) {
            context.Error("invalid argument '" + value + "' for '--" + option + "'");
            context.TryHelp();
            return false;
        };
        for (const auto& option : parsed.options) {
            const std::string& value = option.argument;
            switch (option.id) {
                // As with the real ls, the last of -a and -A given wins, and
                // likewise for the layouts.
                case kAll: settings.all = true; settings.almostAll = false; break;
                case kAlmostAll: settings.almostAll = true; settings.all = false; break;
                case kIgnoreBackups: settings.ignoreBackups = true; break;
                case kCtime: settings.time = LsTime::Change; break;
                case kColumns: settings.format = LsFormat::Columns; break;
                case kDirectory: settings.directory = true; break;
                case kAllUnsorted: settings.all = true; settings.almostAll = false; settings.sort = LsSort::None; settings.sortGiven = true; break;
                case kFileType: settings.slash = true; break;
                case kFormat:
                    if (value == "across" || value == "horizontal") settings.format = LsFormat::Across;
                    else if (value == "commas") settings.format = LsFormat::Commas;
                    else if (value == "long" || value == "verbose") settings.format = LsFormat::Long;
                    else if (value == "single-column") settings.format = LsFormat::OnePerLine;
                    else if (value == "vertical") settings.format = LsFormat::Columns;
                    else return invalid(value, "format");
                    break;
                case kFullTime: settings.format = LsFormat::Long; settings.timeStyle = "full-iso"; break;
                case kLongNoOwner: settings.format = LsFormat::Long; settings.showOwner = false; break;
                case kGroupDirectoriesFirst: settings.groupDirectoriesFirst = true; break;
                case kNoGroup: settings.showGroup = false; break;
                case kHuman: settings.human = true; break;
                case kKibibytes: break; // already the unit
                case kLong: settings.format = LsFormat::Long; break;
                case kCommas: settings.format = LsFormat::Commas; break;
                case kLiteral: settings.literal = true; break;
                case kLongNoGroup: settings.format = LsFormat::Long; settings.showGroup = false; break;
                case kSlash: settings.slash = true; break;
                case kReverse: settings.reverse = true; break;
                case kRecursive: settings.recursive = true; break;
                case kSize: settings.showSize = true; break;
                case kSortBySize: settings.sort = LsSort::Size; settings.sortGiven = true; break;
                case kSort:
                    settings.sortGiven = true;
                    if (value == "none") settings.sort = LsSort::None;
                    else if (value == "name") settings.sort = LsSort::Name;
                    else if (value == "size") settings.sort = LsSort::Size;
                    else if (value == "time") settings.sort = LsSort::Time;
                    else if (value == "extension") settings.sort = LsSort::Extension;
                    else if (value == "version" || value == "width") context.NotTreated("--sort=" + value);
                    else return invalid(value, "sort");
                    break;
                case kTime:
                    if (value == "atime" || value == "access" || value == "use") settings.time = LsTime::Access;
                    else if (value == "ctime" || value == "status") settings.time = LsTime::Change;
                    else if (value == "mtime" || value == "modification") settings.time = LsTime::Modification;
                    else if (value == "birth" || value == "creation") context.NotTreated("--time=" + value);
                    else return invalid(value, "time");
                    break;
                case kTimeStyle: {
                    // "posix-STYLE" means STYLE outside the POSIX locale; the
                    // month names here are always the C locale's, so it is STYLE.
                    std::string style = value.compare(0, 6, "posix-") == 0 ? value.substr(6) : value;
                    if (!IsValidTimeStyle(style)) {
                        return invalid(value, "time-style");
                    }
                    settings.timeStyle = style;
                    break;
                }
                case kSortByTime: settings.sort = LsSort::Time; settings.sortGiven = true; break;
                case kAtime: settings.time = LsTime::Access; break;
                case kUnsorted: settings.sort = LsSort::None; settings.sortGiven = true; break;
                case kWidth:
                    if (!ParseWidth(value, settings.width)) {
                        context.Error("invalid line width: '" + value + "'");
                        return false;
                    }
                    break;
                case kAcross: settings.format = LsFormat::Across; break;
                case kSortByExtension: settings.sort = LsSort::Extension; settings.sortGiven = true; break;
                case kOnePerLine: settings.format = LsFormat::OnePerLine; break;
                default: break;
            }
        }
        // As GNU ls: -u or -c without a long listing and without a sort order
        // of its own sorts by that time.
        if (settings.time != LsTime::Modification && settings.format != LsFormat::Long && !settings.sortGiven) {
            settings.sort = LsSort::Time;
        }
        return true;
    }

    static const FileDateTime& TimeOf(const LsEntry& entry, const LsSettings& settings) {
        switch (settings.time) {
            case LsTime::Access: return entry.status.accessTime;
            case LsTime::Change: return entry.status.changeTime;
            default: return entry.status.modificationTime;
        }
    }

    static void Sort(std::vector<LsEntry>& entries, const LsSettings& settings) {
        if (settings.sort != LsSort::None) {
            std::stable_sort(entries.begin(), entries.end(), [&settings](const LsEntry& a, const LsEntry& b) {
                switch (settings.sort) {
                    case LsSort::Size:
                        if (a.status.size != b.status.size) return a.status.size > b.status.size;
                        break;
                    case LsSort::Time: {
                        const FileDateTime& ta = TimeOf(a, settings);
                        const FileDateTime& tb = TimeOf(b, settings);
                        if (ta != tb) return ta > tb;
                        break;
                    }
                    case LsSort::Extension: {
                        const std::string ea = Extension(a.name);
                        const std::string eb = Extension(b.name);
                        if (ea != eb) return ea < eb;
                        break;
                    }
                    default:
                        break;
                }
                return a.name < b.name;
            });
            if (settings.reverse) {
                std::reverse(entries.begin(), entries.end());
            }
        }
        if (settings.groupDirectoriesFirst) {
            std::stable_partition(entries.begin(), entries.end(),
                [](const LsEntry& entry) { return entry.status.type == DirectoryEntryType::Dir; });
        }
    }

    // Lists the contents of one directory, shown as |shownName|, and then,
    // with -R, of every directory beneath it.
    static void ListDirectory(
        BuiltinContext& context,
        const std::string& shownName,
        const std::string& absolutePath,
        const LsSettings& settings,
        bool showHeader,
        bool& printedSomething)
    {
        if (printedSomething) {
            context.Out("\n");
        }
        if (showHeader) {
            context.Out(shownName + ":\n");
        }
        printedSomething = true;

        IFileIO& io = context.IO();
        std::vector<LsEntry> entries;
        if (settings.all) {
            for (const auto& [name, path] : {std::pair<std::string, std::string>{".", absolutePath},
                                              std::pair<std::string, std::string>{"..", VirtualParentOf(absolutePath)}}) {
                LsEntry entry{name, path, FileStatus{DirectoryEntryType::Dir}};
                io.Stat(path, entry.status);
                entries.push_back(std::move(entry));
            }
        }
        for (const auto& listed : io.ReadDirectory(absolutePath)) {
            const bool hidden = !listed.name.empty() && listed.name[0] == '.';
            if (hidden && !settings.all && !settings.almostAll) {
                continue;
            }
            if (settings.ignoreBackups && !listed.name.empty() && listed.name.back() == '~') {
                continue;
            }
            const std::string childPath = (absolutePath == "/") ? "/" + listed.name : absolutePath + "/" + listed.name;
            LsEntry entry{listed.name, childPath, FileStatus{}};
            if (io.Stat(childPath, entry.status) != 0) {
                // Gone since it was listed: shown as it was listed.
                entry.status.type = listed.type;
            }
            entries.push_back(std::move(entry));
        }
        Sort(entries, settings);
        ListEntries(context, entries, settings, /*withTotal=*/true);

        if (!settings.recursive) {
            return;
        }
        for (const auto& entry : entries) {
            if (context.StopRequested()) {
                return;
            }
            if (entry.status.type != DirectoryEntryType::Dir || entry.name == "." || entry.name == "..") {
                continue;
            }
            const std::string childShown = (!shownName.empty() && shownName.back() == '/')
                ? shownName + entry.name
                : shownName + "/" + entry.name;
            ListDirectory(context, childShown, entry.absolutePath, settings, /*showHeader=*/true, printedSomething);
        }
    }

    // The name as the listing shows it: quoted if need be, with the -p /
    // --file-type slash after it.
    struct ShownName {
        std::string text;
        bool quoted = false;
    };

    static std::vector<ShownName> ShownNames(const std::vector<LsEntry>& entries, const LsSettings& settings) {
        std::vector<ShownName> names;
        bool anyQuoted = false;
        for (const auto& entry : entries) {
            ShownName name;
            name.quoted = !settings.literal && NeedsQuoting(entry.name);
            name.text = name.quoted ? Quote(entry.name) : entry.name;
            if (settings.slash && entry.status.type == DirectoryEntryType::Dir) {
                name.text += '/';
            }
            anyQuoted = anyQuoted || name.quoted;
            names.push_back(std::move(name));
        }
        // As GNU ls does when some names are quoted, the others are shifted
        // right by one so that the names themselves line up.
        const bool align = anyQuoted && settings.format != LsFormat::Commas && settings.format != LsFormat::OnePerLine;
        if (align) {
            for (auto& name : names) {
                if (!name.quoted) {
                    name.text = " " + name.text;
                }
            }
        }
        return names;
    }

    static void ListEntries(BuiltinContext& context, const std::vector<LsEntry>& entries, const LsSettings& settings, bool withTotal) {
        if (withTotal && (settings.format == LsFormat::Long || settings.showSize)) {
            uint64_t blocks = 0;
            for (const auto& entry : entries) {
                blocks += entry.status.blocks;
            }
            context.Out("total " + AllocatedSize(blocks, settings.human) + "\n");
        }
        if (entries.empty()) {
            return;
        }

        const std::vector<ShownName> names = ShownNames(entries, settings);
        std::vector<std::string> sizes;
        size_t sizeWidth = 0;
        if (settings.showSize) {
            for (const auto& entry : entries) {
                sizes.push_back(AllocatedSize(entry.status.blocks, settings.human));
                sizeWidth = std::max(sizeWidth, sizes.back().size());
            }
        }
        auto sizePrefix = [&](size_t i) {
            return settings.showSize ? std::string(sizeWidth - sizes[i].size(), ' ') + sizes[i] + " " : std::string();
        };

        if (settings.format == LsFormat::Long) {
            ListLong(context, entries, names, settings, sizePrefix);
            return;
        }
        std::vector<std::string> items;
        for (size_t i = 0; i < entries.size(); ++i) {
            items.push_back(sizePrefix(i) + names[i].text);
        }
        switch (settings.format) {
            case LsFormat::OnePerLine:
                for (const auto& item : items) {
                    context.Out(item + "\n");
                }
                break;
            case LsFormat::Commas:
                ListWithCommas(context, items, settings.width);
                break;
            default:
                ListInColumns(context, items, settings.width, settings.format == LsFormat::Across);
                break;
        }
    }

    // -l: "<type>rwxrwxrwx <links> <owner> <group> <size> <time> <name>",
    // the numbers right-aligned and the names left-aligned in their columns.
    template <typename SizePrefix>
    static void ListLong(BuiltinContext& context, const std::vector<LsEntry>& entries, const std::vector<ShownName>& names,
                         const LsSettings& settings, const SizePrefix& sizePrefix) {
        const int64_t now = CurrentFileDateTime().seconds;
        std::vector<std::string> links;
        std::vector<std::string> sizes;
        size_t linkWidth = 0;
        size_t sizeWidth = 0;
        for (const auto& entry : entries) {
            links.push_back(std::to_string(entry.status.linkCount));
            sizes.push_back(settings.human ? HumanSize(entry.status.size) : std::to_string(entry.status.size));
            linkWidth = std::max(linkWidth, links.back().size());
            sizeWidth = std::max(sizeWidth, sizes.back().size());
        }
        for (size_t i = 0; i < entries.size(); ++i) {
            std::string line = sizePrefix(i);
            line += entries[i].status.type == DirectoryEntryType::Dir ? 'd' : '-';
            line += "rwxrwxrwx ";
            line += std::string(linkWidth - links[i].size(), ' ') + links[i] + " ";
            if (settings.showOwner) {
                line += std::string(kOwner) + " ";
            }
            if (settings.showGroup) {
                line += std::string(kGroup) + " ";
            }
            line += std::string(sizeWidth - sizes[i].size(), ' ') + sizes[i] + " ";
            line += FormatTimeColumn(settings.timeStyle, TimeOf(entries[i], settings), now) + " ";
            line += names[i].text;
            context.Out(line + "\n");
        }
    }

    // -m: "a, b, c", wrapped before an item that would reach the line width,
    // as GNU ls wraps it.
    static void ListWithCommas(BuiltinContext& context, const std::vector<std::string>& items, size_t width) {
        std::string out;
        size_t position = 0;
        for (size_t i = 0; i < items.size(); ++i) {
            if (i > 0) {
                if (width == 0 || position + items[i].size() + 2 < width) {
                    out += ", ";
                    position += 2;
                } else {
                    out += ",\n";
                    position = 0;
                }
            }
            out += items[i];
            position += items[i].size();
        }
        context.Out(out + "\n");
    }

    // The default layout, as ls prints to a terminal: as many columns as fit
    // in the line (a line must stay shorter than the width), each as wide as
    // its longest item plus a gap -- filled top to bottom, or with -x left to
    // right.
    static void ListInColumns(BuiltinContext& context, const std::vector<std::string>& items, size_t width, bool across) {
        const size_t count = items.size();
        size_t columns = 1;
        size_t rows = count;
        std::vector<size_t> widths;
        for (size_t tryColumns = count; tryColumns >= 1; --tryColumns) {
            const size_t tryRows = (count + tryColumns - 1) / tryColumns;
            const size_t usedColumns = across ? tryColumns : (count + tryRows - 1) / tryRows;
            std::vector<size_t> tryWidths(usedColumns, 0);
            for (size_t i = 0; i < count; ++i) {
                const size_t column = across ? i % tryColumns : i / tryRows;
                tryWidths[column] = std::max(tryWidths[column], items[i].size());
            }
            size_t lineLength = kColumnGap * (usedColumns - 1);
            for (size_t w : tryWidths) {
                lineLength += w;
            }
            if (width == 0 || lineLength < width || usedColumns == 1) {
                columns = usedColumns;
                rows = tryRows;
                widths = std::move(tryWidths);
                break;
            }
        }
        for (size_t row = 0; row < rows; ++row) {
            std::string line;
            for (size_t column = 0; column < columns; ++column) {
                const size_t index = across ? row * columns + column : column * rows + row;
                if (index >= count) {
                    break;
                }
                line += items[index];
                const size_t next = across ? index + 1 : index + rows;
                const bool last = column + 1 == columns || next >= count;
                if (!last) {
                    line += std::string(widths[column] - items[index].size() + kColumnGap, ' ');
                }
            }
            context.Out(line + "\n");
        }
    }
};

} // namespace

std::shared_ptr<IBuiltinCommand> CreateLsCommand() {
    return std::make_shared<LsCommand>();
}

} // namespace Haisos
