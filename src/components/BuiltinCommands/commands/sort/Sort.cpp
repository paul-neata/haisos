#include <algorithm>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <vector>
#include "BuiltinCommand.h"
#include "BuiltinText.h"
#include "commands/sort/SortKeys.h"
#include "src/components/Filesystem/FilesystemUtils.h"

namespace Haisos {
namespace {

enum SortOption {
    kSortIgnoreLeadingBlanks = 1,
    kSortDictionaryOrder,
    kSortIgnoreCase,
    kSortGeneralNumeric,
    kSortIgnoreNonprinting,
    kSortMonth,
    kSortHumanNumeric,
    kSortNumericSort,
    kSortRandom,
    kSortReverse,
    kSortSortWord,
    kSortVersion,
    kSortCheck,
    kSortCheckQuiet,
    kSortKey,
    kSortMerge,
    kSortFieldSeparator,
    kSortOutput,
    kSortStable,
    kSortUnique,
    kSortZeroTerminated,
    kSortFiles0From,
    // -S, -T, --parallel and --batch-size: accepted, never acted on.
    kSortAcceptedIgnored,
};

// The whole input as lines, one owned text per input (the lines are views
// into them, so the strings must outlive the sort).
class SortCommand : public IBuiltinCommand {
public:
    std::string Name() const override { return "sort"; }
    std::string Version() const override { return "1.1.0"; }

    const std::vector<BuiltinOption>& Options() const override {
        static const std::vector<BuiltinOption> options = {
            {'b', "ignore-leading-blanks", kSortIgnoreLeadingBlanks, BuiltinArgument::None, "", "ignore leading blanks"},
            {'d', "dictionary-order", kSortDictionaryOrder, BuiltinArgument::None, "", "consider only blanks and alphanumeric characters"},
            {'f', "ignore-case", kSortIgnoreCase, BuiltinArgument::None, "", "fold lower case to upper case characters"},
            {'g', "general-numeric-sort", kSortGeneralNumeric, BuiltinArgument::None, "", "compare according to general numerical value"},
            {'i', "ignore-nonprinting", kSortIgnoreNonprinting, BuiltinArgument::None, "", "consider only printable characters"},
            {'M', "month-sort", kSortMonth, BuiltinArgument::None, "", "compare (unknown) < 'JAN' < ... < 'DEC'"},
            {'h', "human-numeric-sort", kSortHumanNumeric, BuiltinArgument::None, "", "compare human readable numbers (e.g., 2K 1G)"},
            {'n', "numeric-sort", kSortNumericSort, BuiltinArgument::None, "", "compare according to string numerical value"},
            {'R', "random-sort", kSortRandom, BuiltinArgument::None, "", "shuffle, but group identical keys together"},
            {0, "random-source", kBuiltinNotTreated, BuiltinArgument::Required, "FILE", ""},
            {'r', "reverse", kSortReverse, BuiltinArgument::None, "", "reverse the result of comparisons"},
            {0, "sort", kSortSortWord, BuiltinArgument::Required, "WORD", "sort according to WORD: general-numeric, human-numeric, month, numeric, random, version"},
            {'V', "version-sort", kSortVersion, BuiltinArgument::None, "", "natural sort of (version) numbers within text"},
            {0, "batch-size", kSortAcceptedIgnored, BuiltinArgument::Required, "NMERGE", "accepted and not acted on"},
            {'c', "check", kSortCheck, BuiltinArgument::Optional, "WHEN", "check for sorted input; do not sort"},
            {'C', "", kSortCheckQuiet, BuiltinArgument::None, "", "like -c, but do not report first bad line"},
            {0, "compress-program", kBuiltinNotTreated, BuiltinArgument::Required, "PROG", ""},
            {0, "debug", kBuiltinNotTreated, BuiltinArgument::None, "", ""},
            {0, "files0-from", kSortFiles0From, BuiltinArgument::Required, "F", "read NUL-separated input names from F (- for stdin)"},
            {'k', "key", kSortKey, BuiltinArgument::Required, "KEYDEF", "sort via a key: F[.C][OPTS][,F[.C][OPTS]]"},
            {'m', "merge", kSortMerge, BuiltinArgument::None, "", "merge already sorted files; do not sort"},
            {'o', "output", kSortOutput, BuiltinArgument::Required, "FILE", "write result to FILE instead of standard output"},
            {'s', "stable", kSortStable, BuiltinArgument::None, "", "stabilize sort by disabling last-resort comparison"},
            {'S', "buffer-size", kSortAcceptedIgnored, BuiltinArgument::Required, "SIZE", "accepted and not acted on"},
            {'t', "field-separator", kSortFieldSeparator, BuiltinArgument::Required, "SEP", "use SEP instead of non-blank to blank transition"},
            {'T', "temporary-directory", kSortAcceptedIgnored, BuiltinArgument::Required, "DIR", "accepted and not acted on"},
            {0, "parallel", kSortAcceptedIgnored, BuiltinArgument::Required, "N", "accepted and not acted on"},
            {'u', "unique", kSortUnique, BuiltinArgument::None, "", "output only the first of an equal run"},
            {'z', "zero-terminated", kSortZeroTerminated, BuiltinArgument::None, "", "line delimiter is NUL, not newline"},
        };
        return options;
    }

    BuiltinHelp Help() const override {
        return BuiltinHelp{
            "sort lines of text files",
            {"sort [OPTION]... [FILE]...", "sort [OPTION]... --files0-from=F"},
            "Lines are compared byte by byte, as GNU sort does with LC_ALL=C.\n"
            "-S, -T, --parallel and --batch-size are accepted and not acted on: Haisos sorts in memory, with no temporary files.\n"
            "-R orders by a hash of each key with a random salt, not GNU's MD5 (so its order\n"
            "changes from run to run as GNU's does); --random-source is not treated."};
    }

    int Run(BuiltinContext& context) override {
        int exitStatus = 0;
        const auto parsed = BeginBuiltin(context, *this, /*usageErrorStatus=*/2, exitStatus);
        if (!parsed) {
            return exitStatus;
        }

        SortSettings settings;
        SortKey global;   // the global ordering options, for the inheritance
        std::optional<std::string> output;
        std::optional<std::string> files0From;
        bool haveTab = false;
        int tab = -1;
        bool checkDiagnose = false;  // -c, --check, --check=diagnose-first
        bool checkQuiet = false;      // -C, --check=quiet, --check=silent
        bool merge = false;

        for (const auto& option : parsed->options) {
            switch (option.id) {
                case kSortIgnoreLeadingBlanks:
                    global.skipStartBlanks = global.skipEndBlanks = true;
                    break;
                // GNU keeps only -d of -d and -i together, whichever order
                // they came in.
                case kSortDictionaryOrder:
                    global.dictionary = true;
                    global.ignoreNonprinting = false;
                    break;
                case kSortIgnoreCase: global.foldCase = true; break;
                case kSortGeneralNumeric: global.generalNumeric = true; break;
                case kSortIgnoreNonprinting:
                    if (!global.dictionary) {
                        global.ignoreNonprinting = true;
                    }
                    break;
                case kSortMonth: global.month = true; break;
                case kSortHumanNumeric: global.humanNumeric = true; break;
                case kSortNumericSort: global.numeric = true; break;
                case kSortRandom: global.random = true; break;
                case kSortVersion: global.version = true; break;
                case kSortSortWord: {
                    // The words are matched as -c's WHEN is: an exact name or
                    // an unambiguous prefix of one -- so the short letters
                    // work too ("g" of "general-numeric", and no other).
                    static const std::vector<ArgChoice> kSortWords = {
                        {"general-numeric", kSortGeneralNumeric},
                        {"human-numeric", kSortHumanNumeric},
                        {"month", kSortMonth},
                        {"numeric", kSortNumericSort},
                        {"random", kSortRandom},
                        {"version", kSortVersion},
                    };
                    const auto matched = ArgMatch(context, "--sort", option.argument, kSortWords);
                    if (!matched) {
                        return 1;
                    }
                    switch (*matched) {
                        case kSortGeneralNumeric: global.generalNumeric = true; break;
                        case kSortHumanNumeric: global.humanNumeric = true; break;
                        case kSortMonth: global.month = true; break;
                        case kSortNumericSort: global.numeric = true; break;
                        case kSortRandom: global.random = true; break;
                        case kSortVersion: global.version = true; break;
                        default: break;
                    }
                    break;
                }
                case kSortCheck: {
                    if (!option.hasArgument) {
                        checkDiagnose = true;
                        break;
                    }
                    static const std::vector<ArgChoice> kCheckWords = {
                        {"quiet", 0},
                        {"silent", 0},
                        {"diagnose-first", 1},
                    };
                    const auto matched = ArgMatch(context, "--check", option.argument, kCheckWords);
                    if (!matched) {
                        return 1;
                    }
                    if (*matched == 0) {
                        checkQuiet = true;
                    } else {
                        checkDiagnose = true;
                    }
                    break;
                }
                case kSortCheckQuiet: checkQuiet = true; break;
                case kSortMerge: merge = true; break;
                case kSortReverse: global.reverse = true; break;
                case kSortKey: {
                    SortKey key;
                    std::string error;
                    if (!ParseSortKey(option.argument, key, error)) {
                        context.Error(error);
                        return 2;
                    }
                    settings.keys.push_back(key);
                    break;
                }
                case kSortFieldSeparator: {
                    const std::string& separator = option.argument;
                    if (separator.empty()) {
                        context.Error("empty tab");
                        return 2;
                    }
                    // GNU takes the two characters \0 as the NUL byte.
                    const bool nul = separator == "\\0";
                    if (separator.size() > 1 && !nul) {
                        context.Error("multi-character tab " + GnuQuote(separator));
                        return 2;
                    }
                    const int byte = nul ? 0 : static_cast<unsigned char>(separator[0]);
                    if (haveTab && tab != byte) {
                        context.Error("incompatible tabs");
                        return 2;
                    }
                    haveTab = true;
                    tab = byte;
                    break;
                }
                case kSortOutput: {
                    if (output && *output != option.argument) {
                        context.Error("multiple output files specified");
                        return 2;
                    }
                    output = option.argument;
                    break;
                }
                case kSortStable: settings.stable = true; break;
                case kSortUnique: settings.unique = true; break;
                case kSortZeroTerminated: settings.delimiter = '\0'; break;
                case kSortFiles0From: files0From = option.argument; break;
                default: break;  // not treated (already reported), or accepted and ignored
            }
        }

        // The inheritance: a key with no modifier at all takes every global
        // ordering option; a key with any takes none of them. With no -k, any
        // global ordering option makes the whole line one key carrying them.
        const bool anyGlobal = SortKeyHasModifier(global);
        for (auto& key : settings.keys) {
            if (SortKeyHasModifier(key)) {
                continue;
            }
            key.skipStartBlanks = global.skipStartBlanks;
            key.skipEndBlanks = global.skipEndBlanks;
            key.dictionary = global.dictionary;
            key.foldCase = global.foldCase;
            key.ignoreNonprinting = global.ignoreNonprinting;
            key.numeric = global.numeric;
            key.generalNumeric = global.generalNumeric;
            key.humanNumeric = global.humanNumeric;
            key.month = global.month;
            key.random = global.random;
            key.version = global.version;
            key.reverse = global.reverse;
        }
        if (settings.keys.empty() && anyGlobal) {
            settings.keys.push_back(global);  // a whole-line key, startField 0, endField SIZE_MAX
        }
        settings.tab = tab;
        settings.reverse = global.reverse;
        // The salt of this run's -R: a fresh one every time, so the order
        // changes from run to run as GNU's does.
        {
            std::random_device random;
            settings.randomSalt[0] = (static_cast<uint64_t>(random()) << 32) | random();
            settings.randomSalt[1] = (static_cast<uint64_t>(random()) << 32) | random();
        }

        const std::string incompatible = IncompatibleOptions(settings);
        if (!incompatible.empty()) {
            context.Error(incompatible);
            return 2;
        }

        // Check mode (-c/-C/--check) and merge (-m) both change what the
        // inputs are for, before anything is read.
        if (checkDiagnose && checkQuiet) {
            context.Error("options '-cC' are incompatible");
            return 2;
        }
        const bool check = checkDiagnose || checkQuiet;
        if (check && output) {
            context.Error("options '-co' are incompatible");
            return 2;
        }

        // The inputs: the operands, or the NUL-separated names of
        // --files0-from=F, or -- with neither -- the standard input, unnamed.
        std::vector<std::string> names;
        bool namesFromStdinList = false;
        if (files0From) {
            if (!parsed->operands.empty()) {
                context.Error("extra operand " + ShellEscapeQuoted(parsed->operands.front(), /*always=*/true));
                context.ErrorText("file operands cannot be combined with --files0-from\n");
                context.TryHelp();
                return 2;
            }
            std::string list;
            if (*files0From == "-") {
                namesFromStdinList = true;
                const auto input = context.IO().GetDescriptor(IFileIO::kStdIn);
                if (!input) {
                    context.Error("stat failed: -: Bad file descriptor");
                    return 2;
                }
                const int outcome = ReadWholeStream(context, *input, list);
                if (outcome < 0) {
                    return 2;  // stopped: quiet
                }
                if (outcome > 0) {
                    context.Error("cannot read file names from " + GnuQuote("-"));
                    return 2;
                }
            } else {
                const std::string shown = ShellEscapeQuoted(*files0From);
                FileStatus status;
                if (context.IO().Stat(*files0From, status) != 0) {
                    context.Error("open failed: " + shown + ": No such file or directory");
                    return 2;
                }
                if (status.type == DirectoryEntryType::Dir) {
                    context.Error("cannot read file names from " + GnuQuote(*files0From));
                    return 2;
                }
                const auto handle = context.IO().OpenFile(*files0From, kFileOpenReadOnly);
                if (!handle) {
                    context.Error("open failed: " + shown + ": Permission denied");
                    return 2;
                }
                const int outcome = ReadWholeStream(context, *handle, list);
                if (outcome < 0) {
                    return 2;  // stopped: quiet
                }
                if (outcome > 0) {
                    context.Error("cannot read file names from " + GnuQuote(*files0From));
                    return 2;
                }
            }
            // Every NUL ends a name (an empty one if two meet); bytes after the
            // last NUL form one more name, if there are any.
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
            const std::string listShown = namesFromStdinList
                ? std::string("-") : ShellEscapeQuoted(*files0From);
            for (size_t i = 0; i < names.size(); ++i) {
                if (names[i].empty()) {
                    context.Error(listShown + ":" + std::to_string(i + 1) + ": invalid zero-length file name");
                    return 2;
                }
                if (namesFromStdinList && names[i] == "-") {
                    context.Error("when reading file names from stdin, no file name of '-' allowed");
                    return 2;
                }
            }
            if (names.empty()) {
                context.Error("no input from " + GnuQuote(*files0From));
                return 2;
            }
        } else {
            names = parsed->operands;
            if (names.empty()) {
                names.push_back("-");
            }
        }

        if (check && names.size() > 1) {
            context.Error("extra operand " + GnuQuote(names[1]) + " not allowed with "
                + (checkQuiet ? "-C" : "-c"));
            return 2;
        }
        if (check) {
            return CheckSorted(context, names.front(), checkDiagnose, settings);
        }

        // Read every input fully into memory, in order: no size cap, the
        // output opened only afterwards, which is what makes `sort -o f f`
        // safe. A read failure aborts before anything is printed, as GNU's
        // does. The lines are kept per input, so -m can merge them by their
        // source; a plain sort flattens them afterwards.
        std::vector<std::shared_ptr<std::string>> inputs;
        std::vector<std::vector<std::string_view>> inputLines;
        for (const auto& name : names) {
            InputOpenFailure failure = InputOpenFailure::None;
            auto input = OpenInputOperand(context, name, failure);
            const std::string shown = ShellEscapeQuoted(name);
            if (!input) {
                switch (failure) {
                    case InputOpenFailure::Missing:
                        context.Error("cannot read: " + shown + ": No such file or directory");
                        break;
                    case InputOpenFailure::Directory:
                        context.Error("read failed: " + shown + ": Is a directory");
                        break;
                    case InputOpenFailure::Denied:
                        context.Error("cannot read: " + shown + ": Permission denied");
                        break;
                    default:
                        context.Error("stat failed: " + shown + ": Bad file descriptor");
                        break;
                }
                return 2;
            }
            auto text = std::make_shared<std::string>();
            std::vector<std::pair<size_t, size_t>> spans;  // each line's [offset, size)
            BuiltinLineReader reader(context, *input, settings.delimiter);
            while (true) {
                std::string line;
                bool delimited = true;
                const LineReadResult result = reader.Next(line, delimited);
                if (result == LineReadResult::End) {
                    break;
                }
                if (result == LineReadResult::Stopped) {
                    return 2;  // quiet: the process reports its own stop code
                }
                if (result == LineReadResult::Error) {
                    context.Error("read failed: " + shown + ": Input/output error");
                    return 2;
                }
                spans.emplace_back(text->size(), line.size());
                // The delimiter is appended too, so a last line the input
                // ended without one still gets one on the way out.
                *text += line;
                *text += settings.delimiter;
            }
            // The views are taken only now: appending above may reallocate.
            std::vector<std::string_view> group;
            for (const auto& [offset, size] : spans) {
                group.push_back(std::string_view(*text).substr(offset, size));
            }
            inputLines.push_back(std::move(group));
            inputs.push_back(std::move(text));
        }

        std::vector<std::string_view> lines;
        if (merge) {
            // A k-way merge of the inputs as they stand, never re-sorted:
            // repeatedly the smallest head line wins, an equal head of an
            // earlier input first. With -u a line comparing equal to the
            // last one output is dropped (keys only: -u disables the last
            // resort).
            std::vector<size_t> next(inputLines.size(), 0);
            std::optional<std::string_view> lastOutput;
            while (true) {
                size_t best = SIZE_MAX;
                for (size_t i = 0; i < inputLines.size(); ++i) {
                    if (next[i] >= inputLines[i].size()) {
                        continue;
                    }
                    if (best == SIZE_MAX
                        || CompareLines(inputLines[i][next[i]], inputLines[best][next[best]], settings) < 0) {
                        best = i;
                    }
                }
                if (best == SIZE_MAX) {
                    break;
                }
                const std::string_view line = inputLines[best][next[best]];
                ++next[best];
                if (settings.unique && lastOutput
                    && CompareLines(*lastOutput, line, settings) == 0) {
                    continue;
                }
                lines.push_back(line);
                lastOutput = line;
            }
        } else {
            for (const auto& group : inputLines) {
                lines.insert(lines.end(), group.begin(), group.end());
            }
            // The stability is what makes -s and -u's "first of a run" right:
            // the first line of a run of equal lines is the first in input
            // order.
            std::stable_sort(lines.begin(), lines.end(),
                [&settings](const std::string_view& a, const std::string_view& b) {
                    return CompareLines(a, b, settings) < 0;
                });

            if (settings.unique && !lines.empty()) {
                // Only the first of each run of lines comparing equal by the keys.
                size_t kept = 0;
                for (size_t i = 1; i < lines.size(); ++i) {
                    if (CompareLines(lines[kept], lines[i], settings) != 0) {
                        ++kept;
                        lines[kept] = lines[i];
                    }
                }
                lines.resize(kept + 1);
            }
        }

        std::string result;
        for (const auto& line : lines) {
            result += line;
            result += settings.delimiter;
        }
        if (output) {
            const std::string shown = ShellEscapeQuoted(*output);
            IFileIO& io = context.IO();
            FileStatus status;
            const bool exists = io.Stat(*output, status) == 0;
            if (exists && status.type == DirectoryEntryType::Dir) {
                context.Error("open failed: " + shown + ": Is a directory");
                return 2;
            }
            auto handle = io.OpenFile(*output, kFileOpenWriteCreateTruncate, kFileCreateMode);
            if (!handle) {
                context.Error("open failed: " + shown + ": " + (exists ? "Permission denied" : "No such file or directory"));
                return 2;
            }
            if (WriteFully(*handle, result) < 0) {
                context.Error("write failed: " + shown + ": Input/output error");
                return 2;
            }
        } else {
            context.Out(result);
        }
        return 0;
    }

private:
    // Check mode (-c/-C/--check): the one input read line by line, nothing
    // sorted; 0 sorted, 1 disordered (the first bad line reported unless the
    // mode is quiet), 2 a failure to open or read. |name| as given is what
    // the disorder line prints.
    static int CheckSorted(BuiltinContext& context, const std::string& name,
                           bool diagnose, const SortSettings& settings) {
        InputOpenFailure failure = InputOpenFailure::None;
        auto input = OpenInputOperand(context, name, failure);
        const std::string shown = ShellEscapeQuoted(name);
        if (!input) {
            // GNU opens the input its own way in check mode, so its wordings
            // differ from a sorting run's ("open failed", not "cannot read").
            switch (failure) {
                case InputOpenFailure::Missing:
                    context.Error("open failed: " + shown + ": No such file or directory");
                    break;
                case InputOpenFailure::Directory:
                    context.Error("read failed: " + shown + ": Is a directory");
                    break;
                case InputOpenFailure::Denied:
                    context.Error("open failed: " + shown + ": Permission denied");
                    break;
                default:
                    context.Error("open failed: " + shown + ": Bad file descriptor");
                    break;
            }
            return 2;
        }
        std::string previous;
        bool havePrevious = false;
        size_t lineNumber = 0;
        BuiltinLineReader reader(context, *input, settings.delimiter);
        while (true) {
            std::string line;
            bool delimited = true;
            const LineReadResult result = reader.Next(line, delimited);
            if (result == LineReadResult::End) {
                break;
            }
            if (result == LineReadResult::Stopped) {
                return 2;  // quiet: the process reports its own stop code
            }
            if (result == LineReadResult::Error) {
                context.Error("read failed: " + shown + ": Input/output error");
                return 2;
            }
            ++lineNumber;
            if (!havePrevious) {
                previous = std::move(line);
                havePrevious = true;
                continue;
            }
            const int diff = CompareLines(previous, line, settings);
            if (diff > 0 || (settings.unique && diff >= 0)) {
                if (diagnose) {
                    // The whole message is GNU's, not this builtin's own
                    // "<name>: " shape, and it ends with the delimiter.
                    context.ErrorText("sort: " + name + ":" + std::to_string(lineNumber)
                        + ": disorder: " + line + settings.delimiter);
                }
                return 1;
            }
            previous = std::move(line);
        }
        return 0;
    }

    // A whole stream into |out|. Returns 0 read to its end, -1 stopped (the
    // caller returns quietly), 1 a read error (the caller reports it).
    static int ReadWholeStream(BuiltinContext& context, IFileDescriptor& input, std::string& out) {
        char buffer[64 * 1024];
        while (true) {
            if (context.StopRequested()) {
                return -1;
            }
            const ssize_t n = input.Read(buffer, sizeof(buffer));
            if (n == 0) {
                return 0;
            }
            if (n == kIOInterrupted) {
                return -1;
            }
            if (n < 0) {
                return 1;
            }
            out.append(buffer, static_cast<size_t>(n));
        }
    }
};

} // namespace

std::shared_ptr<IBuiltinCommand> CreateSortCommand() {
    return std::make_shared<SortCommand>();
}

} // namespace Haisos