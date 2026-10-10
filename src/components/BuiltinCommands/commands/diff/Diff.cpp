#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include "BuiltinCommand.h"
#include "BuiltinText.h"
#include "commands/diff/Diff.h"
#include "commands/diff/DiffDirectories.h"
#include "src/components/Filesystem/FilesystemUtils.h"

namespace Haisos {

namespace {

enum DiffOptionId {
    kDiffText = 1,               // -a
    kDiffIgnoreSpaceChange,      // -b
    kDiffIgnoreBlankLines,       // -B
    kDiffContext,                // -c / -C / --context
    kDiffMinimal,                // -d
    kDiffEd,                     // -e
    kDiffIgnoreTabExpansion,     // -E
    kDiffIgnoreCase,             // -i
    kDiffLabel,                  // -L
    kDiffNormal,                 // --normal
    kDiffBrief,                  // -q
    kDiffReportIdentical,        // -s
    kDiffExpandTabs,             // -t
    kDiffInitialTab,             // -T
    kDiffUnified,                // -u / -U / --unified
    kDiffIgnoreAllSpace,         // -w
    kDiffIgnoreTrailingSpace,    // -Z
    kDiffStripTrailingCr,        // --strip-trailing-cr
    kDiffTabSize,                // --tabsize
    kDiffSuppressBlankEmpty,     // --suppress-blank-empty
    kDiffHorizonLines,           // --horizon-lines
    kDiffNoEffect,               // --binary, -h, --inhibit-hunk-merge
    kDiffRecursive,              // -r
    kDiffNoDereference,          // --no-dereference (no links here)
    kDiffNewFile,                // -N
    kDiffUnidirectionalNewFile,  // --unidirectional-new-file
    kDiffExclude,                // -x
    kDiffExcludeFrom,            // -X
    kDiffStartingFile,           // -S
    kDiffFromFile,               // --from-file
    kDiffToFile,                 // --to-file
};

// A non-negative decimal number, as diff's NUM options take it; false when
// |text| is not one (empty, a sign, other bytes) or does not fit.
bool ParseCount(const std::string& text, int64_t& out) {
    if (text.empty()) {
        return false;
    }
    int64_t value = 0;
    for (char c : text) {
        if (c < '0' || c > '9') {
            return false;
        }
        if (value > (INT64_MAX - (c - '0')) / 10) {
            return false;
        }
        value = value * 10 + (c - '0');
    }
    out = value;
    return true;
}

// The command-line words that are options or an option's separate argument,
// in the order given -- what the recursive form echoes in its
// "diff OPTIONS A B" lines. Operands and a "--" are left out; the words are
// classified with the same rules the parser uses (it has already accepted
// every word by the time this runs).
std::vector<std::string> DiffOptionWords(const std::vector<std::string>& args,
                                         const std::vector<BuiltinOption>& options) {
    std::vector<std::string> words;
    for (size_t i = 0; i < args.size(); ++i) {
        const std::string& arg = args[i];
        if (arg == "--") {
            break;
        }
        if (arg.size() > 2 && arg.compare(0, 2, "--") == 0) {
            const size_t eq = arg.find('=');
            const std::string name = arg.substr(2, eq == std::string::npos ? std::string::npos : eq - 2);
            const BuiltinOption* option = DiffFindLongOption(name, options);
            if (!option) {
                continue;
            }
            words.push_back(arg);
            if (eq == std::string::npos && option->argument == BuiltinArgument::Required
                && i + 1 < args.size()) {
                words.push_back(args[++i]);  // a separate argument word
            }
            continue;
        }
        if (arg.size() > 1 && arg[0] == '-') {
            // One cluster word: a Required option's argument follows it in
            // the cluster ("-xo") or as the next word ("-x o").
            bool takesNextWord = false;
            for (size_t j = 1; j < arg.size(); ++j) {
                const BuiltinOption* option = nullptr;
                for (const BuiltinOption& candidate : options) {
                    if (candidate.shortName == arg[j]) {
                        option = &candidate;
                        break;
                    }
                }
                if (!option) {
                    break;
                }
                if (option->argument == BuiltinArgument::Required) {
                    takesNextWord = j + 1 == arg.size();
                    break;
                }
                if (option->argument == BuiltinArgument::OptionalAttached && j + 1 < arg.size()) {
                    break;  // the rest of the cluster is the argument
                }
            }
            words.push_back(arg);
            if (takesNextWord && i + 1 < args.size()) {
                words.push_back(args[++i]);
            }
            continue;
        }
    }
    return words;
}

class DiffCommand : public IBuiltinCommand {
public:
    std::string Name() const override { return "diff"; }
    std::string Version() const override { return "1.1.1"; }

    const std::vector<BuiltinOption>& Options() const override {
        using A = BuiltinArgument;
        static const std::vector<BuiltinOption> options = {
            {'a', "text", kDiffText, A::None, "", "treat all files as text"},
            {'b', "ignore-space-change", kDiffIgnoreSpaceChange, A::None, "",
                "ignore changes in the amount of white space"},
            {'B', "ignore-blank-lines", kDiffIgnoreBlankLines, A::None, "",
                "ignore changes where lines are all blank"},
            {'c', "", kDiffContext, A::None, "", "output 3 lines of copied context"},
            {0, "context", kDiffContext, A::Optional, "NUM",
                "output NUM (default 3) lines of copied context"},
            {'C', "", kDiffContext, A::Required, "NUM",
                "output NUM (default 3) lines of copied context"},
            {'d', "minimal", kDiffMinimal, A::None, "",
                "try hard to find a smaller set of changes"},
            {'e', "ed", kDiffEd, A::None, "", "output an ed script"},
            {'E', "ignore-tab-expansion", kDiffIgnoreTabExpansion, A::None, "",
                "ignore changes due to tab expansion"},
            {'i', "ignore-case", kDiffIgnoreCase, A::None, "",
                "ignore case differences in file contents"},
            {'L', "label", kDiffLabel, A::Required, "LABEL",
                "use LABEL instead of file name and timestamp"},
            {0, "normal", kDiffNormal, A::None, "", "output a normal diff (the default)"},
            {'q', "brief", kDiffBrief, A::None, "", "report only when files differ"},
            {'s', "report-identical-files", kDiffReportIdentical, A::None, "",
                "report when two files are the same"},
            {'t', "expand-tabs", kDiffExpandTabs, A::None, "", "expand tabs to spaces in output"},
            {'T', "initial-tab", kDiffInitialTab, A::None, "",
                "make tabs line up by prepending a tab"},
            {'u', "", kDiffUnified, A::None, "", "output 3 lines of unified context"},
            {'U', "", kDiffUnified, A::Required, "NUM",
                "output NUM (default 3) lines of unified context"},
            {0, "unified", kDiffUnified, A::Optional, "NUM",
                "output NUM (default 3) lines of unified context"},
            {'w', "ignore-all-space", kDiffIgnoreAllSpace, A::None, "", "ignore all white space"},
            {'Z', "ignore-trailing-space", kDiffIgnoreTrailingSpace, A::None, "",
                "ignore white space at line end"},
            {0, "strip-trailing-cr", kDiffStripTrailingCr, A::None, "",
                "strip trailing carriage return on input"},
            {0, "tabsize", kDiffTabSize, A::Required, "NUM",
                "tab stops every NUM (default 8) print columns"},
            {0, "suppress-blank-empty", kDiffSuppressBlankEmpty, A::None, "",
                "suppress space or tab before empty output lines"},
            {0, "horizon-lines", kDiffHorizonLines, A::Required, "NUM",
                "keep NUM lines of the common prefix and suffix"},
            {0, "binary", kDiffNoEffect, A::None, "",
                "read and write data as binary (no effect here)"},
            {'h', "", kDiffNoEffect, A::None, "", "accepted with no effect"},
            {0, "inhibit-hunk-merge", kDiffNoEffect, A::None, "", "accepted with no effect"},
            {'v', "", kBuiltinOptionVersion, A::None, "", "output version information"},
            {'r', "recursive", kDiffRecursive, A::None, "",
                "recursively compare any subdirectories found"},
            {0, "no-dereference", kDiffNoDereference, A::None, "",
                "treat arguments that are symlinks as themselves (no links here)"},
            {'N', "new-file", kDiffNewFile, A::None, "",
                "treat absent files as empty"},
            {'P', "unidirectional-new-file", kDiffUnidirectionalNewFile, A::None, "",
                "treat absent first files as empty"},
            {0, "ignore-file-name-case", kBuiltinNotTreated, A::None},
            {0, "no-ignore-file-name-case", kBuiltinNotTreated, A::None},
            {'x', "exclude", kDiffExclude, A::Required, "PAT",
                "exclude files that match PAT"},
            {'X', "exclude-from", kDiffExcludeFrom, A::Required, "FILE",
                "exclude files that match any pattern in FILE"},
            {'S', "starting-file", kDiffStartingFile, A::Required, "FILE",
                "start with FILE when comparing directories"},
            {0, "from-file", kDiffFromFile, A::Required, "FILE1",
                "compare FILE1 to all operands; FILE1 can be a directory"},
            {0, "to-file", kDiffToFile, A::Required, "FILE2",
                "compare all operands to FILE2; FILE2 can be a directory"},
            // Never treated:
            {'y', "side-by-side", kBuiltinNotTreated, A::None},
            {'W', "width", kBuiltinNotTreated, A::Required, "NUM"},
            {0, "left-column", kBuiltinNotTreated, A::None},
            {0, "suppress-common-lines", kBuiltinNotTreated, A::None},
            {'p', "show-c-function", kBuiltinNotTreated, A::None},
            {'F', "show-function-line", kBuiltinNotTreated, A::Required, "RE"},
            {'I', "ignore-matching-lines", kBuiltinNotTreated, A::Required, "RE"},
            {'f', "forward-ed", kBuiltinNotTreated, A::None},
            {'n', "rcs", kBuiltinNotTreated, A::None},
            {'D', "ifdef", kBuiltinNotTreated, A::Required, "NAME"},
            {0, "line-format", kBuiltinNotTreated, A::Required, "LFMT"},
            {0, "old-line-format", kBuiltinNotTreated, A::Required, "LFMT"},
            {0, "new-line-format", kBuiltinNotTreated, A::Required, "LFMT"},
            {0, "unchanged-line-format", kBuiltinNotTreated, A::Required, "LFMT"},
            {0, "old-group-format", kBuiltinNotTreated, A::Required, "GFMT"},
            {0, "new-group-format", kBuiltinNotTreated, A::Required, "GFMT"},
            {0, "unchanged-group-format", kBuiltinNotTreated, A::Required, "GFMT"},
            {0, "changed-group-format", kBuiltinNotTreated, A::Required, "GFMT"},
            {'H', "speed-large-files", kBuiltinNotTreated, A::None},
            {'l', "paginate", kBuiltinNotTreated, A::None},
            {0, "color", kBuiltinNotTreated, A::Optional, "WHEN"},
            {0, "palette", kBuiltinNotTreated, A::Required, "PALETTE"},
            {0, "sdiff-merge-assist", kBuiltinNotTreated, A::None},
            // GNU's obsolete -NUM context spellings.
            {'0', "", kBuiltinNotTreated, A::None, "", "", true},
            {'1', "", kBuiltinNotTreated, A::None, "", "", true},
            {'2', "", kBuiltinNotTreated, A::None, "", "", true},
            {'3', "", kBuiltinNotTreated, A::None, "", "", true},
            {'4', "", kBuiltinNotTreated, A::None, "", "", true},
            {'5', "", kBuiltinNotTreated, A::None, "", "", true},
            {'6', "", kBuiltinNotTreated, A::None, "", "", true},
            {'7', "", kBuiltinNotTreated, A::None, "", "", true},
            {'8', "", kBuiltinNotTreated, A::None, "", "", true},
            {'9', "", kBuiltinNotTreated, A::None, "", "", true},
        };
        return options;
    }

    BuiltinHelp Help() const override {
        return BuiltinHelp{
            "compare files line by line",
            {"diff [OPTION]... FILES"},
            "FILES are 'FILE1 FILE2'. If a FILE is '-', read standard input. Exit status is\n"
            "0 if inputs are the same, 1 if different, 2 if trouble.\n"
            "The changes are always a minimal set, as with -d (Myers' algorithm); GNU's\n"
            "default output may place some of them differently, or list more on very\n"
            "large inputs."};
    }

    int Run(BuiltinContext& context) override;
};

} // namespace

// The long option |name| names: an exact match over the whole table first
// (a later exact match must not lose to two earlier prefix matches), else an
// unambiguous prefix of one, the way the shared parser finds it too.
const BuiltinOption* DiffFindLongOption(const std::string& name,
                                        const std::vector<BuiltinOption>& options) {
    const BuiltinOption* prefix = nullptr;
    for (const BuiltinOption& option : options) {
        if (!option.longName.empty() && option.longName == name) {
            return &option;
        }
    }
    for (const BuiltinOption& option : options) {
        if (option.longName.empty()) {
            continue;
        }
        if (option.longName.compare(0, name.size(), name) == 0) {
            if (prefix) {
                return nullptr;  // ambiguous: the parser has reported it
            }
            prefix = &option;
        }
    }
    return prefix;
}

std::shared_ptr<IBuiltinCommand> CreateDiffCommand() {
    return std::make_shared<DiffCommand>();
}

int DiffCommand::Run(BuiltinContext& context) {
    // diffutils prefixes its Try line, so the shared BeginBuiltin does not
    // fit; diff words its own usage errors, all exit 2.
    const auto parsed = ParseBuiltinArgs(context.Args(), Options());
    if (!parsed.error.empty()) {
        context.Error(parsed.error);
        context.Error("Try 'diff --help' for more information.");
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

    DiffSettings settings;
    DiffTreeSettings tree;
    std::optional<std::string> fromFile, toFile;
    int64_t contextLines = 0;   // the -C/-U value, the largest given
    int64_t horizon = 0;
    size_t tabSize = 8;
    DiffWhiteSpace whiteSpace = DiffWhiteSpace::None;
    // The style asked for: 0 none yet, 1 normal, 2 context, 3 unified, 4 ed.
    // A second, different one is a conflict.
    int styleGiven = 0;
    const auto usageError = [&](const std::string& message) {
        context.Error(message);
        context.Error("Try 'diff --help' for more information.");
        return 2;
    };
    const auto rankOf = [](DiffWhiteSpace mode) {
        switch (mode) {
            case DiffWhiteSpace::AllSpace: return 4;
            case DiffWhiteSpace::SpaceChange: return 3;
            case DiffWhiteSpace::TrailingSpace: return 2;
            case DiffWhiteSpace::TabExpansion: return 1;
            case DiffWhiteSpace::None: break;
        }
        return 0;
    };
    for (const auto& option : parsed.options) {
        switch (option.id) {
            case kDiffText: settings.text = true; break;
            case kDiffIgnoreAllSpace:
            case kDiffIgnoreSpaceChange:
            case kDiffIgnoreTrailingSpace:
            case kDiffIgnoreTabExpansion: {
                const DiffWhiteSpace mode = option.id == kDiffIgnoreAllSpace
                    ? DiffWhiteSpace::AllSpace
                    : option.id == kDiffIgnoreSpaceChange ? DiffWhiteSpace::SpaceChange
                    : option.id == kDiffIgnoreTrailingSpace ? DiffWhiteSpace::TrailingSpace
                    : DiffWhiteSpace::TabExpansion;
                if (rankOf(mode) > rankOf(whiteSpace)) {
                    whiteSpace = mode;  // the strongest whitespace option wins
                }
                break;
            }
            case kDiffIgnoreBlankLines: settings.output.ignoreBlankLines = true; break;
            case kDiffContext:
            case kDiffUnified: {
                const int want = option.id == kDiffContext ? 2 : 3;
                if (styleGiven != 0 && styleGiven != want) {
                    return usageError("conflicting output style options");
                }
                styleGiven = want;
                int64_t value = 3;
                if (option.hasArgument && !ParseCount(option.argument, value)) {
                    return usageError("invalid context length '" + option.argument + "'");
                }
                contextLines = std::max(contextLines, value);
                break;
            }
            case kDiffMinimal: break;  // the diff is always minimal
            case kDiffEd:
                if (styleGiven != 0 && styleGiven != 4) {
                    return usageError("conflicting output style options");
                }
                styleGiven = 4;
                break;
            case kDiffIgnoreCase: settings.analysis.lines.ignoreCase = true; break;
            case kDiffLabel:
                if (!settings.label0) {
                    settings.label0 = option.argument;
                } else if (!settings.label1) {
                    settings.label1 = option.argument;
                } else {
                    context.Error("too many file label options");
                    return 2;
                }
                break;
            case kDiffNormal:
                if (styleGiven != 0 && styleGiven != 1) {
                    return usageError("conflicting output style options");
                }
                styleGiven = 1;
                break;
            case kDiffBrief: settings.brief = true; break;
            case kDiffReportIdentical: settings.reportIdentical = true; break;
            case kDiffExpandTabs: settings.output.expandTabs = true; break;
            case kDiffInitialTab: settings.output.initialTab = true; break;
            case kDiffStripTrailingCr: settings.stripTrailingCr = true; break;
            case kDiffSuppressBlankEmpty: settings.output.suppressBlankEmpty = true; break;
            case kDiffTabSize: {
                int64_t value = 0;
                if (!ParseCount(option.argument, value) || value == 0) {
                    return usageError("invalid tabsize '" + option.argument + "'");
                }
                tabSize = static_cast<size_t>(value);
                break;
            }
            case kDiffHorizonLines: {
                int64_t value = 0;
                if (!ParseCount(option.argument, value)) {
                    return usageError("invalid horizon length '" + option.argument + "'");
                }
                horizon = value;
                break;
            }
            case kDiffNoEffect: break;  // --binary, -h, --inhibit-hunk-merge
            case kDiffRecursive: tree.recursive = true; break;
            case kDiffNoDereference: break;  // no links: nothing to not follow
            case kDiffNewFile: tree.newFile = true; break;
            case kDiffUnidirectionalNewFile: tree.unidirectionalNewFile = true; break;
            case kDiffExclude: tree.excludes.push_back(option.argument); break;
            case kDiffExcludeFrom: {
                // Read now, in option order: the patterns are a file's
                // lines, empty ones skipped.
                InputOpenFailure failure = InputOpenFailure::None;
                const auto file = OpenInputOperand(context, option.argument, failure);
                if (!file) {
                    context.Error(option.argument + ": " + OpenFailureText(failure));
                    return 2;
                }
                BuiltinLineReader reader(context, *file, '\n');
                std::string line;
                bool delimited = false;
                for (;;) {
                    const LineReadResult result = reader.Next(line, delimited);
                    if (result == LineReadResult::End) {
                        break;
                    }
                    if (result == LineReadResult::Stopped) {
                        return 2;  // a quiet stop
                    }
                    if (result == LineReadResult::Error) {
                        context.Error(option.argument + ": Input/output error");
                        return 2;
                    }
                    if (!line.empty()) {
                        tree.excludes.push_back(line);
                    }
                }
                break;
            }
            case kDiffStartingFile:
                if (tree.startingFile && *tree.startingFile != option.argument) {
                    return usageError("conflicting -S option value '" + option.argument + "'");
                }
                tree.startingFile = option.argument;
                break;
            case kDiffFromFile:
                if (fromFile && *fromFile != option.argument) {
                    return usageError("conflicting --from-file option value '" + option.argument + "'");
                }
                fromFile = option.argument;
                break;
            case kDiffToFile:
                if (toFile && *toFile != option.argument) {
                    return usageError("conflicting --to-file option value '" + option.argument + "'");
                }
                toFile = option.argument;
                break;
            default: break;
        }
    }

    if (styleGiven == 2 || styleGiven == 3) {
        settings.output.style = styleGiven == 2 ? DiffStyle::Context : DiffStyle::Unified;
        settings.output.context = contextLines;
        settings.analysis.horizonLines = std::max(horizon, contextLines);
    } else {
        settings.output.style = styleGiven == 4 ? DiffStyle::Ed : DiffStyle::Normal;
        settings.analysis.horizonLines = horizon;
    }
    settings.analysis.robustOutput = settings.output.style != DiffStyle::Ed;
    settings.analysis.lines.whiteSpace = whiteSpace;
    settings.analysis.lines.tabSize = tabSize;
    settings.output.whiteSpace = whiteSpace;
    settings.output.tabSize = tabSize;

    if (fromFile && toFile) {
        context.Error("--from-file and --to-file both specified");
        return 2;
    }
    // The options the recursive form echoes in its "diff OPTIONS A B"
    // lines, each word as typed.
    for (const std::string& word : DiffOptionWords(context.Args(), Options())) {
        tree.echoedOptions += " " + ShellEscapeQuoted(word);
    }

    // --from-file/--to-file compare one file with every operand, in order.
    if (fromFile || toFile) {
        int status = 0;
        for (const std::string& operand : parsed.operands) {
            const int compared = fromFile
                ? DiffOperands(context, settings, tree, *fromFile, operand)
                : DiffOperands(context, settings, tree, operand, *toFile);
            status = std::max(status, compared);
        }
        return status;
    }

    if (parsed.operands.size() < 2) {
        const std::string last = context.Args().empty() ? std::string("diff") : context.Args().back();
        return usageError("missing operand after '" + last + "'");
    }
    if (parsed.operands.size() > 2) {
        return usageError("extra operand '" + parsed.operands[2] + "'");
    }
    return DiffOperands(context, settings, tree, parsed.operands[0], parsed.operands[1]);
}

int DiffTwoFiles(BuiltinContext& context, const DiffSettings& settings,
                 const std::string& name0, const std::string& name1, const std::string& header,
                 bool missing0, bool missing1) {
    // The name each message shows for a file: its label when one was given,
    // the operand as given otherwise.
    const std::string shown0 = settings.label0 ? *settings.label0 : name0;
    const std::string shown1 = settings.label1 ? *settings.label1 : name1;
    const auto outHeader = [&]() {
        if (!header.empty()) {
            context.Out(header + "\n");
        }
    };
    const bool missing[2] = {missing0, missing1};

    // Open both operands; both failures are reported before returning. A
    // side flagged missing (diff -N against a file that is not there) is
    // neither opened nor stated: it is read as empty bytes.
    std::shared_ptr<IFileDescriptor> files[2] = {};
    bool trouble = false;
    for (int i = 0; i < 2; ++i) {
        if (missing[i]) {
            continue;
        }
        const std::string& name = i == 0 ? name0 : name1;
        InputOpenFailure failure = InputOpenFailure::None;
        files[i] = OpenInputOperand(context, name, failure);
        if (!files[i]) {
            context.Error(name + ": " + OpenFailureText(failure));
            trouble = true;
        }
    }
    if (trouble) {
        return 2;
    }

    // Read both whole; "-" twice is standard input read once, which then
    // always compares equal to itself.
    std::string bytes[2];
    for (int i = 0; i < 2; ++i) {
        if (missing[i]) {
            continue;
        }
        if (i == 1 && name0 == "-" && name1 == "-") {
            bytes[1] = bytes[0];
            continue;
        }
        const std::string& name = i == 0 ? name0 : name1;
        const WholeReadOutcome outcome = ReadWholeInput(context, *files[i], bytes[i]);
        if (outcome == WholeReadOutcome::Stopped) {
            return 2;  // a quiet stop
        }
        if (outcome == WholeReadOutcome::Error) {
            context.Error(name + ": Input/output error");
            return 2;
        }
    }

    // The modification times the headers show: the file's own, the current
    // time for standard input, the epoch for a side that is not there.
    FileDateTime times[2];
    for (int i = 0; i < 2; ++i) {
        const std::string& name = i == 0 ? name0 : name1;
        if (missing[i]) {
            continue;
        }
        if (name == "-") {
            times[i] = CurrentFileDateTime();
        } else {
            FileStatus status;
            if (context.IO().Stat(name, status) == 0) {
                times[i] = status.modificationTime;
            }
        }
    }

    // Unless -a, a NUL byte in the first 4096 makes a file binary: binary
    // files are compared as bytes.
    const auto isBinary = [](const std::string& bytes) {
        const size_t nul = bytes.find('\0');
        return nul != std::string::npos && nul < 4096;
    };
    if (!settings.text && (isBinary(bytes[0]) || isBinary(bytes[1]))) {
        if (bytes[0] == bytes[1]) {
            if (settings.reportIdentical) {
                context.Out("Files " + shown0 + " and " + shown1 + " are identical\n");
            }
            return 0;
        }
        if (settings.brief) {
            context.Out("Files " + shown0 + " and " + shown1 + " differ\n");
        } else {
            context.Out("Binary files " + shown0 + " and " + shown1 + " differ\n");
        }
        return 1;
    }

    // The text comparison. Ed scripts are not robust: a file missing its
    // final newline is compared with the newline appended, and reported
    // after the output (exit status 2) -- unless -q, which reports nothing
    // but the difference itself.
    const bool robust = settings.output.style != DiffStyle::Ed;
    // The bytes as read, before an ed comparison's appended newline: what -q
    // compares in -e style, where the script's comparison would call them even.
    const bool rawBytesEqual = bytes[0] == bytes[1];
    DiffText texts[2] = {
        MakeDiffText(std::move(bytes[0]), settings.stripTrailingCr, robust),
        MakeDiffText(std::move(bytes[1]), settings.stripTrailingCr, robust),
    };
    bool stopped = false;
    const std::vector<DiffChange> script = ComputeDiff(texts[0], texts[1], settings.analysis,
        [&context]() { return context.StopRequested(); }, stopped);
    if (stopped) {
        return 2;
    }

    const bool same = !DiffHasRealChanges(script, texts[0], texts[1], settings.output);
    // Both names the same file: both "-", or neither "-" and one resolved
    // path (GNU tells by its dev/ino, which Haisos has no counterpart of).
    // The only pair a missing final newline leaves silent in -e style.
    const bool sameFile = (name0 == "-" && name1 == "-")
        || (name0 != "-" && name1 != "-"
            && context.IO().ResolvePath(name0) == context.IO().ResolvePath(name1));
    if (same) {
        // An ed comparison appends the missing final newline, so two
        // distinct files can read the same while their bytes differ: -q
        // compares the bytes as read, and without it a missing final
        // newline is trouble even for an identical pair -- GNU warns and
        // exits 2, with no "are identical" line.
        if (!robust && !sameFile) {
            if (settings.brief) {
                if (!rawBytesEqual) {
                    context.Out("Files " + shown0 + " and " + shown1 + " differ\n");
                    return 1;
                }
                if (settings.reportIdentical) {
                    context.Out("Files " + shown0 + " and " + shown1 + " are identical\n");
                }
                return 0;
            }
            if (texts[0].missingNewline || texts[1].missingNewline) {
                for (int i = 0; i < 2; ++i) {
                    if (texts[i].missingNewline) {
                        context.Error((i == 0 ? shown0 : shown1) + ": No newline at end of file");
                        context.ErrorText("\n");
                    }
                }
                return 2;
            }
        }
        if (settings.reportIdentical) {
            context.Out("Files " + shown0 + " and " + shown1 + " are identical\n");
        }
        return 0;
    }
    if (settings.brief) {
        context.Out("Files " + shown0 + " and " + shown1 + " differ\n");
        return 1;
    }
    // The pair header goes before a text difference's output alone: GNU
    // prints none before a binary, brief or identical report.
    outHeader();
    context.Out(FormatDiff(script, texts[0], texts[1],
        DiffHeaderFile{name0, settings.label0, times[0]},
        DiffHeaderFile{name1, settings.label1, times[1]}, settings.output));
    if (!robust && (texts[0].missingNewline || texts[1].missingNewline)) {
        for (int i = 0; i < 2; ++i) {
            if (texts[i].missingNewline) {
                context.Error((i == 0 ? shown0 : shown1) + ": No newline at end of file");
                context.ErrorText("\n");
            }
        }
        return 2;
    }
    return 1;
}

} // namespace Haisos
