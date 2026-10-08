#include <algorithm>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include "BuiltinCommand.h"
#include "BuiltinFnmatch.h"
#include "BuiltinText.h"
#include "commands/grep/GrepContext.h"
#include "commands/grep/GrepFile.h"
#include "commands/grep/GrepMatcher.h"
#include "commands/grep/GrepSettings.h"

namespace Haisos {
namespace {

enum GrepOptionId {
    kExtendedMatcher = 1,  // -E
    kFixedMatcher,          // -F, --fixed-regexp
    kBasicMatcher,          // -G
    kPerlMatcher,           // -P
    kPatterns,              // -e
    kPatternFile,           // -f
    kIgnoreCase,            // -i, -y
    kNoIgnoreCase,          // --no-ignore-case
    kWordRegexp,            // -w
    kLineRegexp,            // -x
    kNullData,              // -z
    kNoMessages,            // -s
    kInvert,                // -v
    kMaxCount,              // -m
    kByteOffset,            // -b
    kLineNumber,            // -n
    kLineBuffered,
    kWithFilename,          // -H
    kNoFilename,            // -h
    kLabel,                 // --label
    kOnlyMatching,          // -o
    kQuiet,                 // -q, --silent
    kBinaryFiles,           // --binary-files
    kText,                  // -a
    kBinaryWithoutMatch,    // -I
    kFilesWithoutMatch,     // -L
    kFilesWithMatches,      // -l
    kCount,                 // -c
    kInitialTab,            // -T
    kBinaryFlag,            // -U: does nothing, as on Linux
    kObsoleteU,             // -u: warning, then nothing
    kRecursive,             // -r, -R
    kDirectories,           // -d
    kDevices,               // -D
    kInclude,               // --include
    kExclude,               // --exclude
    kExcludeFrom,           // --exclude-from
    kExcludeDir,            // --exclude-dir
    kNull,                  // -Z
    kAfterContext,          // -A
    kBeforeContext,         // -B
    kContext,               // -C
    kGroupSeparator,        // --group-separator
    kNoGroupSeparator,      // --no-group-separator
    kColor,                 // --color
    kColour,                // --colour
    kDigitBase = 100,       // -0 through -9: the NUM of -NUM's context
};

// PATTERNS holds one pattern per line: each newline-separated piece is a
// pattern of its own; an empty PATTERNS is one empty pattern, which matches
// every line.
void AddPatterns(std::vector<std::string>& patterns, const std::string& text) {
    size_t start = 0;
    while (true) {
        const size_t newline = text.find('\n', start);
        if (newline == std::string::npos) {
            patterns.push_back(text.substr(start));
            return;
        }
        patterns.push_back(text.substr(start, newline - start));
        start = newline + 1;
    }
}

const char* OpenFailureText(InputOpenFailure failure) {
    switch (failure) {
        case InputOpenFailure::Missing: return "No such file or directory";
        case InputOpenFailure::Directory: return "Is a directory";
        case InputOpenFailure::Denied: return "Permission denied";
        case InputOpenFailure::BadDescriptor: return "Bad file descriptor";
        case InputOpenFailure::None: break;
    }
    return "";
}

// A value GREP_COLORS accepts for a capability: digits and ';' only, possibly
// empty. (GREP_COLOR takes the same.)
bool IsSgrValue(const std::string& value) {
    for (const char c : value) {
        if ((c < '0' || c > '9') && c != ';') {
            return false;
        }
    }
    return true;
}

// ASCII-lowercases a copy of |value|.
std::string LowerAscii(const std::string& value) {
    std::string out = value;
    for (char& c : out) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return out;
}

// A non-negative decimal number, overflowing to the largest it can hold;
// anything else is rejected (the caller prints GNU's own message).
bool ParseContextNumber(const std::string& text, size_t& out) {
    if (text.empty()) {
        return false;
    }
    size_t value = 0;
    for (const char c : text) {
        if (c < '0' || c > '9') {
            return false;
        }
        const size_t digit = static_cast<size_t>(c - '0');
        if (value > (std::numeric_limits<size_t>::max() - digit) / 10) {
            value = std::numeric_limits<size_t>::max();
        } else {
            value = value * 10 + digit;
        }
    }
    out = value;
    return true;
}

// grep, egrep and fgrep are one command: Ubuntu's egrep/fgrep are scripts
// running `grep -E` / `grep -F`, so every diagnostic, of whichever of the
// three, names grep and points at grep's help.
class GrepCommand : public IBuiltinCommand {
public:
    GrepCommand(std::string name, GrepSyntax defaultSyntax)
        : m_name(std::move(name)), m_defaultSyntax(defaultSyntax) {}

    std::string Name() const override { return m_name; }
    std::string Version() const override { return "1.1.0"; }

    const std::vector<BuiltinOption>& Options() const override {
        static const std::vector<BuiltinOption> options = {
            {'E', "extended-regexp", kExtendedMatcher, BuiltinArgument::None, "",
                "PATTERNS are extended regular expressions"},
            {'F', "fixed-strings", kFixedMatcher, BuiltinArgument::None, "",
                "PATTERNS are fixed strings"},
            {0, "fixed-regexp", kFixedMatcher, BuiltinArgument::None, "", "", true},
            {'G', "basic-regexp", kBasicMatcher, BuiltinArgument::None, "",
                "PATTERNS are basic regular expressions"},
            {'P', "perl-regexp", kPerlMatcher, BuiltinArgument::None, "",
                "PATTERNS are Perl regular expressions"},
            {'e', "regexp", kPatterns, BuiltinArgument::Required, "PATTERNS",
                "use PATTERNS for matching"},
            {'f', "file", kPatternFile, BuiltinArgument::Required, "FILE",
                "take PATTERNS from FILE"},
            {'i', "ignore-case", kIgnoreCase, BuiltinArgument::None, "",
                "ignore case distinctions in patterns and data"},
            {'y', "", kIgnoreCase, BuiltinArgument::None, "", "", true},
            {0, "no-ignore-case", kNoIgnoreCase, BuiltinArgument::None, "",
                "do not ignore case distinctions in patterns and data"},
            {'w', "word-regexp", kWordRegexp, BuiltinArgument::None, "",
                "match whole words only"},
            {'x', "line-regexp", kLineRegexp, BuiltinArgument::None, "",
                "match whole lines only"},
            {'z', "null-data", kNullData, BuiltinArgument::None, "",
                "a data line ends in a NUL byte, not a newline"},
            {'s', "no-messages", kNoMessages, BuiltinArgument::None, "",
                "suppress error messages"},
            {'v', "invert-match", kInvert, BuiltinArgument::None, "",
                "select non-matching lines"},
            {'m', "max-count", kMaxCount, BuiltinArgument::Required, "NUM",
                "stop after NUM selected lines"},
            {'b', "byte-offset", kByteOffset, BuiltinArgument::None, "",
                "print the byte offset with output lines"},
            {'n', "line-number", kLineNumber, BuiltinArgument::None, "",
                "print line numbers with output lines"},
            {0, "line-buffered", kLineBuffered, BuiltinArgument::None, "",
                "flush output on every line"},
            {'H', "with-filename", kWithFilename, BuiltinArgument::None, "",
                "print file names with output lines"},
            {'h', "no-filename", kNoFilename, BuiltinArgument::None, "",
                "suppress file names on output"},
            {0, "label", kLabel, BuiltinArgument::Required, "LABEL",
                "use LABEL as the standard input file name"},
            {'o', "only-matching", kOnlyMatching, BuiltinArgument::None, "",
                "print only matching parts of lines"},
            {'q', "quiet", kQuiet, BuiltinArgument::None, "",
                "suppress all normal output"},
            {0, "silent", kQuiet, BuiltinArgument::None, "", "", true},
            {0, "binary-files", kBinaryFiles, BuiltinArgument::Required, "TYPE",
                "read binary files as TYPE: binary, text, without-match"},
            {'a', "text", kText, BuiltinArgument::None, "",
                "same as --binary-files=text"},
            {'I', "", kBinaryWithoutMatch, BuiltinArgument::None, "",
                "same as --binary-files=without-match"},
            {'L', "files-without-match", kFilesWithoutMatch, BuiltinArgument::None, "",
                "print only names of files with no matches"},
            {'l', "files-with-matches", kFilesWithMatches, BuiltinArgument::None, "",
                "print only names of files with matches"},
            {'c', "count", kCount, BuiltinArgument::None, "",
                "print only a count of selected lines"},
            {'T', "initial-tab", kInitialTab, BuiltinArgument::None, "",
                "make tabs line up"},
            {'U', "binary", kBinaryFlag, BuiltinArgument::None, "",
                "does nothing, as on Linux"},
            {'u', "unix-byte-offsets", kObsoleteU, BuiltinArgument::None, "", "", true},
            {'V', "version", kBuiltinOptionVersion, BuiltinArgument::None, "",
                "display version information"},
            {'r', "recursive", kRecursive, BuiltinArgument::None, "",
                "search directories recursively"},
            {'R', "dereference-recursive", kRecursive, BuiltinArgument::None, "",
                "likewise, but Haisos has no symbolic links of its own, so -R is -r"},
            {'d', "directories", kDirectories, BuiltinArgument::Required, "ACTION",
                "read, recurse or skip a directory (an unambiguous prefix)"},
            {'D', "devices", kDevices, BuiltinArgument::Required, "ACTION",
                "read or skip a device"},
            {0, "include", kInclude, BuiltinArgument::Required, "GLOB",
                "search only files whose base name matches GLOB"},
            {0, "exclude", kExclude, BuiltinArgument::Required, "GLOB",
                "skip files whose base name matches GLOB"},
            {0, "exclude-from", kExcludeFrom, BuiltinArgument::Required, "FILE",
                "skip files whose base name matches a GLOB from FILE"},
            {0, "exclude-dir", kExcludeDir, BuiltinArgument::Required, "GLOB",
                "skip directories whose base name matches GLOB"},
            {'Z', "null", kNull, BuiltinArgument::None, "",
                "print a NUL byte after a file name"},
            {'A', "after-context", kAfterContext, BuiltinArgument::Required, "NUM",
                "print NUM lines of trailing context"},
            {'B', "before-context", kBeforeContext, BuiltinArgument::Required, "NUM",
                "print NUM lines of leading context"},
            {'C', "context", kContext, BuiltinArgument::Required, "NUM",
                "print NUM lines of output context"},
            {0, "group-separator", kGroupSeparator, BuiltinArgument::Required, "SEP",
                "use SEP as a group separator (default --)"},
            {0, "no-group-separator", kNoGroupSeparator, BuiltinArgument::None, "",
                "print no group separators"},
            {0, "color", kColor, BuiltinArgument::Optional, "WHEN",
                "highlight matches with WHEN: always, never or auto"},
            {0, "colour", kColour, BuiltinArgument::Optional, "WHEN",
                "same as --color"},
            {'0', "", kDigitBase, BuiltinArgument::None, "", "", true},
            {'1', "", kDigitBase + 1, BuiltinArgument::None, "", "", true},
            {'2', "", kDigitBase + 2, BuiltinArgument::None, "", "", true},
            {'3', "", kDigitBase + 3, BuiltinArgument::None, "", "", true},
            {'4', "", kDigitBase + 4, BuiltinArgument::None, "", "", true},
            {'5', "", kDigitBase + 5, BuiltinArgument::None, "", "", true},
            {'6', "", kDigitBase + 6, BuiltinArgument::None, "", "", true},
            {'7', "", kDigitBase + 7, BuiltinArgument::None, "", "", true},
            {'8', "", kDigitBase + 8, BuiltinArgument::None, "", "", true},
            {'9', "", kDigitBase + 9, BuiltinArgument::None, "", "", true},
            {'X', "", kBuiltinNotTreated, BuiltinArgument::Required, "TYPE", "", true},
        };
        return options;
    }

    BuiltinHelp Help() const override {
        BuiltinHelp help;
        help.summary = "print lines that match patterns";
        help.usage = {m_name + " [OPTION]... PATTERNS [FILE]..."};
        help.notes =
            "PATTERNS holds one pattern per line. With no FILE, or when FILE is -,\n"
            "the standard input is read; with -r/-R and no FILE, . is searched\n"
            "recursively, its names printed without the leading ./. A recursive\n"
            "walk lists each directory's entries in byte order (GNU's readdir\n"
            "order is unspecified), depth first; -R is -r, as HaisosOS has no\n"
            "symbolic links of its own (a physical filesystem follows those on\n"
            "the disk, as it always does). Consecutive digit options form one\n"
            "number (-15 is 15, -5n is 5 and -n); GNU starts a new number with\n"
            "each argument word, which the parse here cannot tell apart, so -1\n"
            "-5 is 15 where GNU takes it as 5. Colour follows GNU's --color and\n"
            "the GREP_COLORS capabilities. Everything is in the C locale:\n"
            "bytes, ASCII case folding with -i, and only a NUL byte makes a\n"
            "file binary (decided per 96 KiB read, exactly for a file that size\n"
            "or smaller, as GNU). With -T the standard input's numbers pad to 19\n"
            "columns (a descriptor has no size). -U does nothing, as on Linux.\n"
            "Exit status is 0 if a line is selected, 1 if none is, 2 on\n"
            "trouble.";
        if (m_name != "grep") {
            help.basedOn = "grep";
            help.notes += "\nThis builtin prints no obsolescence warning, as Debian/Ubuntu's\n"
                + m_name + " scripts do (upstream 3.8+ prints one).";
        }
        return help;
    }

    int Run(BuiltinContext& context) override {
        ParsedBuiltinArgs parsed = ParseBuiltinArgs(context.Args(), Options());
        if (!parsed.error.empty()) {
            UsageError(context, parsed.error);
            return 2;
        }
        // As getopt_long-based commands do, --help and --version win over
        // everything else given, whichever comes first.
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

        GrepSettings settings;
        settings.matcher.syntax = m_defaultSyntax;
        // egrep/fgrep are grep with -E/-F given first; a second, different
        // matcher is a conflict, the same one twice is fine.
        std::optional<GrepSyntax> givenSyntax = m_defaultSyntax == GrepSyntax::Basic
            ? std::optional<GrepSyntax>()
            : std::optional<GrepSyntax>(m_defaultSyntax);
        const auto setSyntax = [&](GrepSyntax syntax) -> bool {
            if (givenSyntax && *givenSyntax != syntax) {
                context.ErrorText("grep: conflicting matchers specified\n");
                return false;
            }
            givenSyntax = syntax;
            settings.matcher.syntax = syntax;
            return true;
        };
        bool patternsGiven = false;  // -e or -f
        bool wordRequested = false;
        bool lineRequested = false;
        // -A/-B win over -C/-NUM whatever the order; the digits of clustered
        // options form one number, which counts as -C.
        std::optional<size_t> beforeGiven;
        std::optional<size_t> afterGiven;
        std::optional<size_t> contextGiven;
        size_t digitNumber = 0;
        bool digitsOpen = false;
        const auto closeDigits = [&]() {
            if (digitsOpen) {
                contextGiven = digitNumber;
                digitsOpen = false;
                digitNumber = 0;
            }
        };
        const auto invalidContext = [&](const std::string& value) {
            context.ErrorText("grep: " + value + ": invalid context length argument\n");
            return 2;
        };
        for (const auto& option : parsed.options) {
            // Consecutive digit options form one number: the -NUM context.
            // Any other option ends the run, GNU's getopt order.
            if (option.id >= kDigitBase && option.id <= kDigitBase + 9) {
                digitNumber = digitNumber <= std::numeric_limits<size_t>::max() / 10
                    ? digitNumber * 10 + static_cast<size_t>(option.id - kDigitBase)
                    : digitNumber;
                digitsOpen = true;
                continue;
            }
            closeDigits();
            // -A NUM, -B NUM, -C NUM: a non-negative decimal, overflow
            // saturating.
            const auto contextArgument = [&](const std::string& value) -> std::optional<size_t> {
                size_t number = 0;
                if (!ParseContextNumber(value, number)) {
                    return std::nullopt;
                }
                return number;
            };
            switch (option.id) {
                case kExtendedMatcher:
                    if (!setSyntax(GrepSyntax::Extended)) return 2;
                    break;
                case kFixedMatcher:
                    if (!setSyntax(GrepSyntax::Fixed)) return 2;
                    break;
                case kBasicMatcher:
                    if (!setSyntax(GrepSyntax::Basic)) return 2;
                    break;
                case kPerlMatcher:
                    if (!setSyntax(GrepSyntax::Perl)) return 2;
                    break;
                case kPatterns:
                    AddPatterns(settings.patterns, option.argument);
                    patternsGiven = true;
                    break;
                case kPatternFile: {
                    InputOpenFailure failure = InputOpenFailure::None;
                    auto input = OpenInputOperand(context, option.argument, failure);
                    if (!input) {
                        // Not one of -s's messages: the pattern file missing
                        // is trouble, not a file skipped.
                        context.ErrorText("grep: " + option.argument + ": "
                            + OpenFailureText(failure) + "\n");
                        return 2;
                    }
                    BuiltinLineReader reader(context, *input, '\n');
                    std::string line;
                    bool delimited = false;
                    while (true) {
                        const LineReadResult read = reader.Next(line, delimited);
                        if (read == LineReadResult::End) {
                            break;
                        }
                        if (read == LineReadResult::Stopped) {
                            return 1;  // quiet; the process reports 143
                        }
                        if (read == LineReadResult::Error) {
                            context.ErrorText("grep: " + option.argument
                                + ": Input/output error\n");
                            return 2;
                        }
                        settings.patterns.push_back(line);
                    }
                    patternsGiven = true;
                    break;
                }
                case kIgnoreCase: settings.matcher.ignoreCase = true; break;
                case kNoIgnoreCase: settings.matcher.ignoreCase = false; break;
                case kWordRegexp: wordRequested = true; break;
                case kLineRegexp: lineRequested = true; break;
                case kNullData: settings.nullData = true; break;
                case kNoMessages: settings.noMessages = true; break;
                case kInvert: settings.invert = true; break;
                case kMaxCount: {
                    // A decimal integer, optionally negative; negative and
                    // overflow mean no limit, anything else is invalid.
                    const std::string& text = option.argument;
                    size_t i = 0;
                    bool negative = false;
                    if (i < text.size() && (text[i] == '-' || text[i] == '+')) {
                        negative = text[i] == '-';
                        ++i;
                    }
                    bool valid = i < text.size();
                    bool overflow = false;
                    uint64_t magnitude = 0;
                    for (; valid && i < text.size(); ++i) {
                        const char c = text[i];
                        if (c < '0' || c > '9') {
                            valid = false;
                            break;
                        }
                        if (!overflow) {
                            if (magnitude > (std::numeric_limits<uint64_t>::max() - (c - '0')) / 10) {
                                overflow = true;
                            } else {
                                magnitude = magnitude * 10 + static_cast<uint64_t>(c - '0');
                            }
                        }
                    }
                    if (!valid) {
                        context.ErrorText("grep: invalid max count\n");
                        return 2;
                    }
                    if (!overflow && !negative) {
                        settings.maxCount = magnitude;
                    }
                    break;
                }
                case kByteOffset: settings.byteOffsets = true; break;
                case kLineNumber: settings.lineNumbers = true; break;
                case kLineBuffered: settings.lineBuffered = true; break;
                case kWithFilename: settings.withFilename = true; break;
                case kNoFilename: settings.withFilename = false; break;
                case kLabel: settings.label = option.argument; break;
                case kOnlyMatching: settings.onlyMatching = true; break;
                case kQuiet: settings.quiet = true; break;
                case kBinaryFiles:
                    if (option.argument == "binary") {
                        settings.binaryFiles = GrepBinaryFiles::Binary;
                    } else if (option.argument == "text") {
                        settings.binaryFiles = GrepBinaryFiles::Text;
                    } else if (option.argument == "without-match") {
                        settings.binaryFiles = GrepBinaryFiles::WithoutMatch;
                    } else {
                        context.ErrorText("grep: unknown binary-files type\n");
                        return 2;
                    }
                    break;
                case kText: settings.binaryFiles = GrepBinaryFiles::Text; break;
                case kBinaryWithoutMatch: settings.binaryFiles = GrepBinaryFiles::WithoutMatch; break;
                case kFilesWithoutMatch: settings.listFiles = GrepListFiles::NonMatching; break;
                case kFilesWithMatches: settings.listFiles = GrepListFiles::Matching; break;
                case kCount: settings.count = true; break;
                case kInitialTab: settings.initialTab = true; break;
                case kBinaryFlag: break;  // -U: no CR handling on Linux either
                case kObsoleteU:
                    context.ErrorText("grep: warning: --unix-byte-offsets (-u) is obsolete\n");
                    break;
                case kRecursive:
                    settings.directories = GrepDirectories::Recurse;
                    break;
                case kDirectories: {
                    // argmatch is case-sensitive: `-d READ` is invalid.
                    const std::string& value = option.argument;
                    static const struct { const char* name; GrepDirectories action; } kActions[] = {
                        {"read", GrepDirectories::Read},
                        {"recurse", GrepDirectories::Recurse},
                        {"skip", GrepDirectories::Skip},
                    };
                    int found = -1;
                    int matches = 0;
                    for (int i = 0; i < 3; ++i) {
                        const std::string name = kActions[i].name;
                        if (name == value
                            || (name.size() > value.size()
                                && name.compare(0, value.size(), value) == 0)) {
                            found = i;
                            ++matches;
                        }
                    }
                    if (matches == 1) {
                        settings.directories = kActions[found].action;
                        break;
                    }
                    // GNU's argmatch failure, plus grep's Usage: lines, and
                    // its exit status 1 (verified).
                    context.ErrorText("grep: " + std::string(matches == 0 ? "invalid" : "ambiguous")
                        + " argument " + GnuQuote(option.argument) + " for '--directories'\n"
                        "Valid arguments are:\n"
                        "  - 'read'\n"
                        "  - 'recurse'\n"
                        "  - 'skip'\n"
                        "Usage: grep [OPTION]... PATTERNS [FILE]...\n"
                        "Try 'grep --help' for more information.\n");
                    return 1;
                }
                case kDevices:
                    if (option.argument == "read") {
                        settings.devices = GrepDevices::Read;
                    } else if (option.argument == "skip") {
                        settings.devices = GrepDevices::Skip;
                    } else {
                        context.ErrorText("grep: unknown devices method\n");
                        return 2;
                    }
                    break;
                case kInclude:
                    settings.filters.push_back({option.argument, /*include=*/true});
                    break;
                case kExclude:
                    settings.filters.push_back({option.argument, /*include=*/false});
                    break;
                case kExcludeFrom: {
                    InputOpenFailure failure = InputOpenFailure::None;
                    auto input = OpenInputOperand(context, option.argument, failure);
                    if (!input) {
                        context.ErrorText("grep: " + option.argument + ": "
                            + OpenFailureText(failure) + "\n");
                        return 2;
                    }
                    BuiltinLineReader reader(context, *input, '\n');
                    std::string line;
                    bool delimited = false;
                    while (true) {
                        const LineReadResult read = reader.Next(line, delimited);
                        if (read == LineReadResult::End) {
                            break;
                        }
                        if (read == LineReadResult::Stopped) {
                            return 1;  // quiet; the process reports 143
                        }
                        if (read == LineReadResult::Error) {
                            context.ErrorText("grep: " + option.argument
                                + ": Input/output error\n");
                            return 2;
                        }
                        settings.filters.push_back({line, /*include=*/false});
                    }
                    break;
                }
                case kExcludeDir: {
                    // Its trailing '/'s are not part of the name it matches.
                    size_t end = option.argument.size();
                    while (end > 0 && option.argument[end - 1] == '/') {
                        --end;
                    }
                    settings.excludeDirs.push_back(option.argument.substr(0, end));
                    break;
                }
                case kNull:
                    settings.nullAfterName = true;
                    break;
                case kAfterContext: {
                    const auto number = contextArgument(option.argument);
                    if (!number) {
                        return invalidContext(option.argument);
                    }
                    afterGiven = *number;
                    break;
                }
                case kBeforeContext: {
                    const auto number = contextArgument(option.argument);
                    if (!number) {
                        return invalidContext(option.argument);
                    }
                    beforeGiven = *number;
                    break;
                }
                case kContext: {
                    const auto number = contextArgument(option.argument);
                    if (!number) {
                        return invalidContext(option.argument);
                    }
                    contextGiven = *number;
                    break;
                }
                case kGroupSeparator:
                    settings.groupSeparator = option.argument;
                    break;
                case kNoGroupSeparator:
                    settings.groupSeparator = std::nullopt;
                    break;
                case kColor:
                case kColour: {
                    // No value is `auto`; GNU prints its whole help and
                    // exits 0 for a WHEN it does not know.
                    const std::string value = LowerAscii(option.hasArgument ? option.argument : "auto");
                    if (value == "always" || value == "yes" || value == "force") {
                        settings.colorWhen = GrepColorWhen::Always;
                    } else if (value == "never" || value == "no" || value == "none") {
                        settings.colorWhen = GrepColorWhen::Never;
                    } else if (value == "auto" || value == "tty" || value == "if-tty") {
                        settings.colorWhen = GrepColorWhen::Auto;
                    } else {
                        context.Out(BuiltinHelpText(*this));
                        return 0;
                    }
                    break;
                }
                default:
                    break;
            }
        }
        closeDigits();
        settings.contextEnabled = beforeGiven.has_value() || afterGiven.has_value()
            || contextGiven.has_value();
        const size_t contextNumber = contextGiven.value_or(0);
        settings.before = beforeGiven.value_or(contextNumber);
        settings.after = afterGiven.value_or(contextNumber);

        // -w wraps the pattern for no matcher here: every syntax gets the
        // same word-neighbour check around each match in GrepMatcher.
        settings.matcher.wholeLine = lineRequested;
        if (wordRequested) {
            settings.matcher.wholeWord = GrepWholeWord::NonWordNeighbours;
        }

        if (!patternsGiven) {
            if (parsed.operands.empty()) {
                context.ErrorText(
                    "Usage: grep [OPTION]... PATTERNS [FILE]...\n"
                    "Try 'grep --help' for more information.\n");
                return 2;
            }
            AddPatterns(settings.patterns, parsed.operands.front());
            parsed.operands.erase(parsed.operands.begin());
        }

        if (settings.matcher.syntax == GrepSyntax::Perl && settings.patterns.size() > 1) {
            context.ErrorText("grep: the -P option only supports a single pattern\n");
            return 2;
        }

        std::string matcherError;
        const auto matcher = GrepMatcher::Create(settings.patterns, settings.matcher, matcherError);
        if (!matcher) {
            context.ErrorText("grep: " + matcherError + "\n");
            return 2;
        }

        // Colour: `auto` colours only on a terminal with a TERM worth
        // colouring on (GNU's should_colorize), and the capabilities come
        // from GREP_COLORS, with the deprecated GREP_COLOR before it.
        const bool recurse = settings.directories == GrepDirectories::Recurse;
        switch (settings.colorWhen) {
            case GrepColorWhen::Always:
                settings.color = true;
                break;
            case GrepColorWhen::Auto: {
                const auto environment = context.Process().GetEnvironment();
                const auto term = environment->GetVariable("TERM");
                settings.color = context.OutIsTerminal()
                    && term.has_value() && *term != "dumb";
                break;
            }
            case GrepColorWhen::Never:
                settings.color = false;
                break;
        }
        if (settings.color) {
            ParseGrepColors(context, settings.colors);
        }

        std::vector<std::string> files = std::move(parsed.operands);
        // With -r and no operand, `.` is searched as an implicit operand,
        // its names printed without the leading ./.
        bool implicitDot = false;
        if (files.empty()) {
            if (recurse) {
                files.push_back(".");
                implicitDot = true;
            } else {
                files.push_back("-");
            }
        }
        // Filenames are shown with -H, or (no -h) when there is more than one
        // operand, or the single operand is a directory searched recursively
        // (the implicit . included).
        if (!settings.withFilename) {
            bool singleDirectory = false;
            if (recurse && files.size() == 1) {
                if (implicitDot || files[0] == ".") {
                    singleDirectory = true;
                } else {
                    FileStatus status;
                    singleDirectory = context.IO().Stat(files[0], status) == 0
                        && status.type == DirectoryEntryType::Dir;
                }
            }
            settings.withFilename = files.size() > 1 || singleDirectory;
        }

        // The run's before/after context: one for the whole run, so a group
        // separator also goes between groups of different files. It has no
        // part in -c -l -L -q, whose outputs are not lines of context.
        std::optional<GrepContext> grepContext;
        const bool useContext = settings.contextEnabled && !settings.count
            && settings.listFiles == GrepListFiles::None && !settings.quiet;
        if (useContext) {
            grepContext.emplace(settings.before, settings.after, /*enabled=*/true,
                                /*separatorAcrossFiles=*/true, nullptr, nullptr);
        }

        bool anySelected = false;
        bool anyError = false;
        bool stopped = false;      // -q found its match, or the process was asked to stop
        bool quietMatched = false;
        GrepContext* const grepContextPtr = grepContext ? &*grepContext : nullptr;

        const auto takeResult = [&](const GrepFileResult& result) {
            anyError = anyError || result.error;
            anySelected = anySelected || result.selected > 0;
            if (result.stopped) {
                stopped = true;
                quietMatched = quietMatched || result.selected > 0;
            }
        };

        // One file operand or one entry of the walk, by its path.
        const auto grepPath = [&](const std::string& file) {
            InputOpenFailure failure = InputOpenFailure::None;
            auto input = OpenInputOperand(context, file, failure);
            if (!input) {
                if (!settings.noMessages) {
                    context.ErrorText("grep: " + file + ": " + OpenFailureText(failure) + "\n");
                }
                anyError = true;
                return;
            }
            std::optional<uint64_t> sizeForTab;
            FileStatus status;
            if (context.IO().Stat(file, status) == 0 && status.type == DirectoryEntryType::File) {
                sizeForTab = status.size;
            }
            takeResult(GrepOneInput(context, settings, *matcher, *input, file, sizeForTab, grepContextPtr));
        };

        // The include/exclude filters, gnulib's excluded_file_name: for a
        // name met while walking, the pattern is matched against the entry's
        // base name; the list is walked from the last given to the first,
        // the first match deciding; when none matches, the file is skipped
        // if the first given was an --include. An operand is matched
        // unanchored: the whole operand, or the part after any '/' that is
        // not followed by another '/'.
        const auto patternMatchesName = [&](const std::string& pattern, const std::string& name) {
            return FnMatch(pattern, name, 0);
        };
        const auto patternMatchesOperand = [&](const std::string& pattern, const std::string& operand) {
            if (FnMatch(pattern, operand, 0)) {
                return true;
            }
            for (size_t i = 0; i < operand.size(); ++i) {
                if (operand[i] == '/' && (i + 1 >= operand.size() || operand[i + 1] != '/')
                    && FnMatch(pattern, operand.substr(i + 1), 0)) {
                    return true;
                }
            }
            return false;
        };
        const auto nameIncluded = [&](const std::string& name) {
            for (auto it = settings.filters.rbegin(); it != settings.filters.rend(); ++it) {
                if (patternMatchesName(it->pattern, name)) {
                    return it->include;
                }
            }
            return settings.filters.empty() || !settings.filters.front().include;
        };
        const auto operandIncluded = [&](const std::string& operand) {
            for (auto it = settings.filters.rbegin(); it != settings.filters.rend(); ++it) {
                if (patternMatchesOperand(it->pattern, operand)) {
                    return it->include;
                }
            }
            return settings.filters.empty() || !settings.filters.front().include;
        };
        const auto dirExcluded = [&](const std::string& name) {
            for (const auto& pattern : settings.excludeDirs) {
                if (patternMatchesName(pattern, name)) {
                    return true;
                }
            }
            return false;
        };
        const auto operandDirExcluded = [&](const std::string& operand) {
            for (const auto& pattern : settings.excludeDirs) {
                if (patternMatchesOperand(pattern, operand)) {
                    return true;
                }
            }
            return false;
        };

        // The operand as given, with any run of trailing '/' cut away and
        // one added back: `src/` and `src//` both name their children
        // `src/sub`; the implicit `.` names them without a prefix at all.
        const auto childPrefix = [&](const std::string& operand) {
            if (implicitDot) {
                return std::string();
            }
            size_t end = operand.size();
            while (end > 0 && operand[end - 1] == '/') {
                --end;
            }
            return operand.substr(0, end) + "/";
        };

        // The recursive walk, depth first: the entries of a directory minus
        // . and .., in byte order, each child named by its parent's path +
        // '/' + its name. Directories inside are walked unless an
        // --exclude-dir pattern matches their base name; a device is skipped
        // unless -D read; a file is searched when the filters allow it.
        const auto walk = [&](const auto& self, const std::string& dirPath, const std::string& prefix) -> void {
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
                    if (dirExcluded(entry.name)) {
                        continue;
                    }
                    self(self, child, child + "/");
                } else {
                    if (entry.type == DirectoryEntryType::CharDevice
                        && settings.devices != GrepDevices::Read) {
                        continue;
                    }
                    if (!nameIncluded(entry.name)) {
                        continue;
                    }
                    grepPath(child);
                }
            }
        };

        for (const auto& file : files) {
            if (context.StopRequested()) {
                return 1;
            }
            if (file == "-") {
                // Standard input, while recursing too.
                InputOpenFailure failure = InputOpenFailure::None;
                auto input = OpenInputOperand(context, file, failure);
                if (!input) {
                    if (!settings.noMessages) {
                        context.ErrorText("grep: " + settings.label + ": "
                            + OpenFailureText(failure) + "\n");
                    }
                    anyError = true;
                    continue;
                }
                takeResult(GrepOneInput(context, settings, *matcher, *input, settings.label,
                                        /*sizeForTab=*/std::nullopt, grepContextPtr));
                if (stopped) {
                    break;
                }
                continue;
            }
            FileStatus status;
            const bool haveStat = context.IO().Stat(file, status) == 0;
            if (haveStat && status.type == DirectoryEntryType::Dir) {
                if (settings.directories == GrepDirectories::Skip) {
                    continue;  // silently, as GNU's -d skip
                }
                if (recurse) {
                    // An operand directory is skipped by --exclude-dir too,
                    // except the implicit `.`.
                    if (!implicitDot && operandDirExcluded(file)) {
                        continue;
                    }
                    const std::string prefix = childPrefix(file);
                    walk(walk, file, prefix);
                    continue;
                }
                // -d read: opened, and reported as a directory (below).
            } else {
                if (haveStat && status.type == DirectoryEntryType::CharDevice
                    && settings.devices == GrepDevices::Skip) {
                    continue;
                }
                if (!operandIncluded(file)) {
                    continue;
                }
            }
            grepPath(file);
            if (stopped) {
                break;
            }
        }
        if (stopped) {
            // -q found its match: 0 at once, before any later file -- even
            // one that already failed (GNU's -q, too). A stop asked for by
            // TriggerStop exits 1 (the process reports 143).
            if (settings.quiet && quietMatched) {
                return 0;
            }
            return 1;
        }
        if (settings.quiet && anySelected) {
            return 0;
        }
        if (anyError) {
            return 2;
        }
        return anySelected ? 0 : 1;
    }

private:
    // grep's usage errors carry the Usage: line, and every diagnostic of
    // grep, egrep and fgrep names grep (Ubuntu's egrep/fgrep scripts exec
    // grep, whose argv[0] it is).
    static void UsageError(BuiltinContext& context, const std::string& message) {
        context.ErrorText("grep: " + message + "\n"
            "Usage: grep [OPTION]... PATTERNS [FILE]...\n"
            "Try 'grep --help' for more information.\n");
    }

    // GREP_COLORS' capabilities, over the defaults: ':'-separated entries,
    // `mt=V` setting ms and mc, the others one each, `rv` and `ne` the
    // booleans, the values digits and ';' (possibly empty); parsing stops at
    // the first malformed or unknown entry, keeping what was read. Before it
    // the deprecated GREP_COLOR (non-empty, digits and ';') sets ms and mc,
    // and when GREP_COLORS has not replaced it, the warning is written once.
    static void ParseGrepColors(BuiltinContext& context, GrepColors& colors) {
        const auto environment = context.Process().GetEnvironment();
        std::optional<std::string> grepColor;
        if (const auto value = environment->GetVariable("GREP_COLOR")) {
            if (!value->empty() && IsSgrValue(*value)) {
                colors.ms = *value;
                colors.mc = *value;
                grepColor = *value;
            }
        }
        bool matchCapabilitySet = false;
        if (const auto value = environment->GetVariable("GREP_COLORS")) {
            size_t start = 0;
            while (start <= value->size()) {
                const size_t colon = value->find(':', start);
                const std::string entry = value->substr(
                    start, colon == std::string::npos ? std::string::npos : colon - start);
                const size_t eq = entry.find('=');
                const std::string name = entry.substr(0, eq);
                bool ok = true;
                if (eq == std::string::npos) {
                    if (name == "rv") {
                        colors.rv = true;
                    } else if (name == "ne") {
                        colors.ne = true;
                    } else {
                        ok = false;
                    }
                } else {
                    const std::string entryValue = entry.substr(eq + 1);
                    if (!IsSgrValue(entryValue)) {
                        ok = false;
                    } else if (name == "mt") {
                        colors.ms = entryValue;
                        colors.mc = entryValue;
                        matchCapabilitySet = true;
                    } else if (name == "ms") {
                        colors.ms = entryValue;
                        matchCapabilitySet = true;
                    } else if (name == "mc") {
                        colors.mc = entryValue;
                        matchCapabilitySet = true;
                    } else if (name == "sl") {
                        colors.sl = entryValue;
                    } else if (name == "cx") {
                        colors.cx = entryValue;
                    } else if (name == "fn") {
                        colors.fn = entryValue;
                    } else if (name == "ln") {
                        colors.ln = entryValue;
                    } else if (name == "bn") {
                        colors.bn = entryValue;
                    } else if (name == "se") {
                        colors.se = entryValue;
                    } else {
                        ok = false;
                    }
                }
                if (!ok) {
                    break;
                }
                if (colon == std::string::npos) {
                    break;
                }
                start = colon + 1;
            }
        }
        if (grepColor && !matchCapabilitySet) {
            context.ErrorText("grep: warning: GREP_COLOR='" + *grepColor
                + "' is deprecated; use GREP_COLORS='mt=" + *grepColor + "'\n");
        }
    }

    std::string m_name;
    GrepSyntax m_defaultSyntax;
};

} // namespace

std::shared_ptr<IBuiltinCommand> CreateGrepCommand() {
    return std::make_shared<GrepCommand>("grep", GrepSyntax::Basic);
}

std::shared_ptr<IBuiltinCommand> CreateEgrepCommand() {
    return std::make_shared<GrepCommand>("egrep", GrepSyntax::Extended);
}

std::shared_ptr<IBuiltinCommand> CreateFgrepCommand() {
    return std::make_shared<GrepCommand>("fgrep", GrepSyntax::Fixed);
}

} // namespace Haisos