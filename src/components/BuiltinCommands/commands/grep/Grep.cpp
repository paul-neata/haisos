#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include "BuiltinCommand.h"
#include "BuiltinText.h"
#include "commands/grep/GrepFile.h"
#include "commands/grep/GrepMatcher.h"
#include "commands/grep/GrepSettings.h"
#include "src/components/Filesystem/FilesystemUtils.h"

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

// grep, egrep and fgrep are one command: Ubuntu's egrep/fgrep are scripts
// running `grep -E` / `grep -F`, so every diagnostic, of whichever of the
// three, names grep and points at grep's help.
class GrepCommand : public IBuiltinCommand {
public:
    GrepCommand(std::string name, GrepSyntax defaultSyntax)
        : m_name(std::move(name)), m_defaultSyntax(defaultSyntax) {}

    std::string Name() const override { return m_name; }
    std::string Version() const override { return "1.0.0"; }

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
            // The rest of GNU grep 3.11's options, acted on by
            // search--grep-recursive: recursion, directories, context, colour.
            {'r', "recursive", kBuiltinNotTreated, BuiltinArgument::None, "", ""},
            {'R', "dereference-recursive", kBuiltinNotTreated, BuiltinArgument::None, "", ""},
            {'d', "directories", kBuiltinNotTreated, BuiltinArgument::Required, "ACTION", ""},
            {'D', "devices", kBuiltinNotTreated, BuiltinArgument::Required, "ACTION", ""},
            {0, "include", kBuiltinNotTreated, BuiltinArgument::Required, "GLOB", ""},
            {0, "exclude", kBuiltinNotTreated, BuiltinArgument::Required, "GLOB", ""},
            {0, "exclude-from", kBuiltinNotTreated, BuiltinArgument::Required, "FILE", ""},
            {0, "exclude-dir", kBuiltinNotTreated, BuiltinArgument::Required, "GLOB", ""},
            {'Z', "null", kBuiltinNotTreated, BuiltinArgument::None, "", ""},
            {'A', "after-context", kBuiltinNotTreated, BuiltinArgument::Required, "NUM", ""},
            {'B', "before-context", kBuiltinNotTreated, BuiltinArgument::Required, "NUM", ""},
            {'C', "context", kBuiltinNotTreated, BuiltinArgument::Required, "NUM", ""},
            {0, "group-separator", kBuiltinNotTreated, BuiltinArgument::Required, "SEP", ""},
            {0, "no-group-separator", kBuiltinNotTreated, BuiltinArgument::None, "", ""},
            {0, "color", kBuiltinNotTreated, BuiltinArgument::Optional, "WHEN", ""},
            {0, "colour", kBuiltinNotTreated, BuiltinArgument::Optional, "WHEN", ""},
            {'0', "", kBuiltinNotTreated, BuiltinArgument::None, "", "", true},
            {'1', "", kBuiltinNotTreated, BuiltinArgument::None, "", "", true},
            {'2', "", kBuiltinNotTreated, BuiltinArgument::None, "", "", true},
            {'3', "", kBuiltinNotTreated, BuiltinArgument::None, "", "", true},
            {'4', "", kBuiltinNotTreated, BuiltinArgument::None, "", "", true},
            {'5', "", kBuiltinNotTreated, BuiltinArgument::None, "", "", true},
            {'6', "", kBuiltinNotTreated, BuiltinArgument::None, "", "", true},
            {'7', "", kBuiltinNotTreated, BuiltinArgument::None, "", "", true},
            {'8', "", kBuiltinNotTreated, BuiltinArgument::None, "", "", true},
            {'9', "", kBuiltinNotTreated, BuiltinArgument::None, "", "", true},
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
            "the standard input is read. Everything is in the C locale: bytes,\n"
            "ASCII case folding with -i, and only a NUL byte makes a file binary\n"
            "(decided per 96 KiB read, exactly for a file that size or smaller,\n"
            "as GNU). With -T the standard input's numbers pad to 19 columns (a\n"
            "descriptor has no size). -U does nothing, as on Linux. Exit status\n"
            "is 0 if a line is selected, 1 if none is, 2 on trouble.";
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
        for (const auto& option : parsed.options) {
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
                default: break;
            }
        }

        // -w wraps the pattern for Perl; the other matchers check the
        // match's neighbours -- decided here, once the matcher is known.
        settings.matcher.wholeLine = lineRequested;
        if (wordRequested) {
            settings.matcher.wholeWord = settings.matcher.syntax == GrepSyntax::Perl
                ? GrepWholeWord::PerlBoundaries
                : GrepWholeWord::NonWordNeighbours;
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

        std::vector<std::string> files = std::move(parsed.operands);
        if (files.empty()) {
            files.push_back("-");
        }
        // Filenames are shown with -H, or (no -h) when there is more than one
        // file operand.
        if (!settings.withFilename) {
            settings.withFilename = files.size() > 1;
        }

        bool anySelected = false;
        bool anyError = false;
        for (const auto& file : files) {
            if (context.StopRequested()) {
                return 1;
            }
            InputOpenFailure failure = InputOpenFailure::None;
            auto input = OpenInputOperand(context, file, failure);
            const std::string shownName = file == "-" ? settings.label : file;
            if (!input) {
                if (!settings.noMessages) {
                    context.ErrorText("grep: " + shownName + ": " + OpenFailureText(failure) + "\n");
                }
                anyError = true;
                continue;
            }
            std::optional<uint64_t> sizeForTab;
            if (file != "-") {
                FileStatus status;
                if (context.IO().Stat(file, status) == 0 && status.type == DirectoryEntryType::File) {
                    sizeForTab = status.size;
                }
            }
            const GrepFileResult result = GrepOneInput(
                context, settings, *matcher, *input, shownName, sizeForTab);
            anyError = anyError || result.error;
            anySelected = anySelected || result.selected > 0;
            if (result.stopped) {
                // -q found its match: 0 at once, before any later file --
                // even one that already failed (GNU's -q, too).
                if (settings.quiet && result.selected > 0) {
                    return 0;
                }
                return 1;
            }
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