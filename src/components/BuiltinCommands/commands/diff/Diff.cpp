#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include "BuiltinCommand.h"
#include "BuiltinText.h"
#include "commands/diff/Diff.h"
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

class DiffCommand : public IBuiltinCommand {
public:
    std::string Name() const override { return "diff"; }
    std::string Version() const override { return "1.0.0"; }

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
            // Treated by diff--diff-recursive, not yet:
            {'r', "recursive", kBuiltinNotTreated, A::None},
            {0, "no-dereference", kBuiltinNotTreated, A::None},
            {'N', "new-file", kBuiltinNotTreated, A::None},
            {'P', "unidirectional-new-file", kBuiltinNotTreated, A::None},
            {0, "ignore-file-name-case", kBuiltinNotTreated, A::None},
            {0, "no-ignore-file-name-case", kBuiltinNotTreated, A::None},
            {'x', "exclude", kBuiltinNotTreated, A::Required, "PAT"},
            {'X', "exclude-from", kBuiltinNotTreated, A::Required, "FILE"},
            {'S', "starting-file", kBuiltinNotTreated, A::Required, "FILE"},
            {0, "from-file", kBuiltinNotTreated, A::Required, "FILE1"},
            {0, "to-file", kBuiltinNotTreated, A::Required, "FILE2"},
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

// One input's bytes read whole, or why they could not be.
enum class DiffReadOutcome { Done, Stopped, Error };

// Reads |file| whole into |out|, 64 KiB at a time, with no size cap. A stop
// asked for (or a read it interrupted) ends quietly; any other failed read
// is the caller's error to report.
DiffReadOutcome ReadWholeInput(BuiltinContext& context, IFileDescriptor& file, std::string& out) {
    char buffer[64 * 1024];
    for (;;) {
        if (context.StopRequested()) {
            return DiffReadOutcome::Stopped;
        }
        const ssize_t n = file.Read(buffer, sizeof(buffer));
        if (n == 0) {
            return DiffReadOutcome::Done;
        }
        if (n == kIOInterrupted) {
            return DiffReadOutcome::Stopped;
        }
        if (n < 0) {
            return DiffReadOutcome::Error;
        }
        out.append(buffer, static_cast<size_t>(n));
    }
}

} // namespace

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

    if (parsed.operands.size() < 2) {
        const std::string last = context.Args().empty() ? std::string("diff") : context.Args().back();
        return usageError("missing operand after '" + last + "'");
    }
    if (parsed.operands.size() > 2) {
        return usageError("extra operand '" + parsed.operands[2] + "'");
    }
    return DiffTwoFiles(context, settings, parsed.operands[0], parsed.operands[1], "");
}

int DiffTwoFiles(BuiltinContext& context, const DiffSettings& settings,
                 const std::string& name0, const std::string& name1, const std::string& header) {
    // The name each message shows for a file: its label when one was given,
    // the operand as given otherwise.
    const std::string shown0 = settings.label0 ? *settings.label0 : name0;
    const std::string shown1 = settings.label1 ? *settings.label1 : name1;
    const auto outHeader = [&]() {
        if (!header.empty()) {
            context.Out(header + "\n");
        }
    };

    // Open both operands; both failures are reported before returning.
    std::shared_ptr<IFileDescriptor> files[2] = {};
    bool trouble = false;
    for (int i = 0; i < 2; ++i) {
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
        if (i == 1 && name0 == "-" && name1 == "-") {
            bytes[1] = bytes[0];
            continue;
        }
        const std::string& name = i == 0 ? name0 : name1;
        const DiffReadOutcome outcome = ReadWholeInput(context, *files[i], bytes[i]);
        if (outcome == DiffReadOutcome::Stopped) {
            return 2;  // a quiet stop
        }
        if (outcome == DiffReadOutcome::Error) {
            context.Error(name + ": Input/output error");
            return 2;
        }
    }

    // The modification times the headers show: the file's own, the current
    // time for standard input.
    FileDateTime times[2];
    for (int i = 0; i < 2; ++i) {
        const std::string& name = i == 0 ? name0 : name1;
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
            outHeader();
            if (settings.reportIdentical) {
                context.Out("Files " + shown0 + " and " + shown1 + " are identical\n");
            }
            return 0;
        }
        outHeader();
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
    // A missing final newline is trouble only when there is a difference to
    // print: an identical pair is silently the same, in -e style too.
    const bool newlineTrouble = !robust && !settings.brief && !same
        && (texts[0].missingNewline || texts[1].missingNewline);
    if (same) {
        outHeader();
        if (settings.reportIdentical) {
            context.Out("Files " + shown0 + " and " + shown1 + " are identical\n");
        }
        return 0;
    }
    if (!same) {
        outHeader();
        if (settings.brief) {
            context.Out("Files " + shown0 + " and " + shown1 + " differ\n");
            return 1;
        }
        context.Out(FormatDiff(script, texts[0], texts[1],
            DiffHeaderFile{name0, settings.label0, times[0]},
            DiffHeaderFile{name1, settings.label1, times[1]}, settings.output));
    }
    if (newlineTrouble) {
        for (int i = 0; i < 2; ++i) {
            if (texts[i].missingNewline) {
                context.Error((i == 0 ? shown0 : shown1) + ": No newline at end of file");
                context.ErrorText("\n");
            }
        }
        return 2;
    }
    return same ? 0 : 1;
}

} // namespace Haisos
