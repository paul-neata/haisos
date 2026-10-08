#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include "BuiltinCommand.h"
#include "BuiltinText.h"
#include "src/components/Filesystem/FilesystemUtils.h"

namespace Haisos {
namespace {

enum UniqOption {
    kUniqCount = 1,
    kUniqRepeated,
    kUniqAllRepeatedShort,  // -D
    kUniqAllRepeated,       // --all-repeated[=METHOD]
    kUniqSkipFields,
    kUniqGroup,
    kUniqIgnoreCase,
    kUniqSkipChars,
    kUniqUnique,
    kUniqZeroTerminated,
    kUniqCheckChars,
    // -0 .. -9, the obsolete -N: one id for all ten, never shown in --help.
    kUniqObsoleteDigit,
};

// What separates the lines a method prints (a run of equal lines), or nothing.
enum UniqSeparator { kUniqSepNone, kUniqSepPrepend, kUniqSepSeparate, kUniqSepAppend, kUniqSepBoth };

// A line's compared part: its key.
std::string UniqKey(std::string_view line, size_t skipFields, size_t skipChars,
                   size_t checkChars) {
    size_t start = 0;
    const size_t size = line.size();
    // Each skipped field: blanks, then non-blanks (space, tab and newline).
    for (size_t field = 0; field < skipFields && start < size; ++field) {
        while (start < size && (line[start] == ' ' || line[start] == '\t' || line[start] == '\n')) {
            ++start;
        }
        while (start < size && line[start] != ' ' && line[start] != '\t' && line[start] != '\n') {
            ++start;
        }
    }
    start = std::min(size, start + skipChars);
    size_t length = size - start;
    if (length > checkChars) {
        length = checkChars;
    }
    return std::string(line.substr(start, length));
}

bool UniqKeysEqual(const std::string& a, const std::string& b, bool ignoreCase) {
    if (a.size() != b.size()) {
        return false;
    }
    if (!ignoreCase) {
        return a == b;
    }
    auto lower = [](char c) {
        return static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
    };
    for (size_t i = 0; i < a.size(); ++i) {
        if (lower(a[i]) != lower(b[i])) {
            return false;
        }
    }
    return true;
}

// A non-negative decimal, saturating at SIZE_MAX; false when it is not one.
bool UniqParseCount(const std::string& text, size_t& value) {
    if (text.empty()) {
        return false;
    }
    size_t result = 0;
    for (const char c : text) {
        if (c < '0' || c > '9') {
            return false;
        }
        const size_t digit = static_cast<size_t>(c - '0');
        if (result > (SIZE_MAX - digit) / 10) {
            result = SIZE_MAX;  // saturated; the rest of the digits change nothing
        } else {
            result = result * 10 + digit;
        }
    }
    value = result;
    return true;
}

class UniqCommand : public IBuiltinCommand {
public:
    std::string Name() const override { return "uniq"; }
    std::string Version() const override { return "1.0.0"; }

    const std::vector<BuiltinOption>& Options() const override {
        static const std::vector<BuiltinOption> options = {
            {'c', "count", kUniqCount, BuiltinArgument::None, "", "prefix lines by the number of occurrences"},
            {'d', "repeated", kUniqRepeated, BuiltinArgument::None, "", "only print duplicate lines, one for each group"},
            {'D', "", kUniqAllRepeatedShort, BuiltinArgument::None, "", "print all duplicate lines"},
            {0, "all-repeated", kUniqAllRepeated, BuiltinArgument::Optional, "METHOD", "like -D, but allow separating groups with an empty line: none, prepend, separate"},
            {'f', "skip-fields", kUniqSkipFields, BuiltinArgument::Required, "N", "avoid comparing the first N fields"},
            {0, "group", kUniqGroup, BuiltinArgument::Optional, "METHOD", "show all items, separating groups with an empty line: separate, prepend, append, both"},
            {'i', "ignore-case", kUniqIgnoreCase, BuiltinArgument::None, "", "ignore differences in case when comparing"},
            {'s', "skip-chars", kUniqSkipChars, BuiltinArgument::Required, "N", "avoid comparing the first N characters"},
            {'u', "unique", kUniqUnique, BuiltinArgument::None, "", "only print unique lines"},
            {'z', "zero-terminated", kUniqZeroTerminated, BuiltinArgument::None, "", "end lines with NUL, not newline"},
            {'w', "check-chars", kUniqCheckChars, BuiltinArgument::Required, "N", "compare no more than N characters in lines"},
            {'0', "", kUniqObsoleteDigit, BuiltinArgument::None, "", "", /*hidden=*/true},
            {'1', "", kUniqObsoleteDigit, BuiltinArgument::None, "", "", /*hidden=*/true},
            {'2', "", kUniqObsoleteDigit, BuiltinArgument::None, "", "", /*hidden=*/true},
            {'3', "", kUniqObsoleteDigit, BuiltinArgument::None, "", "", /*hidden=*/true},
            {'4', "", kUniqObsoleteDigit, BuiltinArgument::None, "", "", /*hidden=*/true},
            {'5', "", kUniqObsoleteDigit, BuiltinArgument::None, "", "", /*hidden=*/true},
            {'6', "", kUniqObsoleteDigit, BuiltinArgument::None, "", "", /*hidden=*/true},
            {'7', "", kUniqObsoleteDigit, BuiltinArgument::None, "", "", /*hidden=*/true},
            {'8', "", kUniqObsoleteDigit, BuiltinArgument::None, "", "", /*hidden=*/true},
            {'9', "", kUniqObsoleteDigit, BuiltinArgument::None, "", "", /*hidden=*/true},
        };
        return options;
    }

    BuiltinHelp Help() const override {
        return BuiltinHelp{
            "report or omit repeated lines",
            {"uniq [OPTION]... [INPUT [OUTPUT]]"},
            "-N is the obsolete spelling of -f N; +N (an operand) the obsolete -s N.\n"
            "Lines are compared byte by byte, as GNU uniq does with LC_ALL=C;\n"
            "-i folds ASCII case while comparing.",
        };
    }

    int Run(BuiltinContext& context) override {
        int exitStatus = 0;
        const auto parsed = BeginBuiltin(context, *this, /*usageErrorStatus=*/1, exitStatus);
        if (!parsed) {
            return exitStatus;
        }

        bool count = false;
        bool printUnique = true;         // a group of one prints (-d, -D clear it)
        bool printFirstRepeated = true;  // a larger group's first line (-u clears it)
        bool printLaterRepeated = false; // a larger group's other lines (-D sets it)
        bool groupMode = false;
        int groupSeparator = kUniqSepSeparate;
        int allRepeatedSeparator = kUniqSepNone;
        bool ignoreCase = false;
        char delimiter = '\n';
        size_t skipFields = 0;
        size_t skipChars = 0;
        size_t checkChars = SIZE_MAX;
        bool skipFieldsSetByF = false;

        for (const auto& option : parsed->options) {
            switch (option.id) {
                case kUniqCount: count = true; break;
                case kUniqRepeated: printUnique = false; break;
                case kUniqAllRepeatedShort:
                    printUnique = false;
                    printLaterRepeated = true;
                    allRepeatedSeparator = kUniqSepNone;
                    break;
                case kUniqAllRepeated: {
                    printUnique = false;
                    printLaterRepeated = true;
                    if (!option.hasArgument) {
                        allRepeatedSeparator = kUniqSepNone;
                        break;
                    }
                    static const std::vector<ArgChoice> kMethods = {
                        {"none", kUniqSepNone},
                        {"prepend", kUniqSepPrepend},
                        {"separate", kUniqSepSeparate},
                    };
                    const auto matched = ArgMatch(context, "--all-repeated", option.argument, kMethods);
                    if (!matched) {
                        return 1;
                    }
                    allRepeatedSeparator = *matched;
                    break;
                }
                case kUniqSkipFields:
                    if (!UniqParseCount(option.argument, skipFields)) {
                        context.Error(option.argument + ": invalid number of fields to skip");
                        return 1;
                    }
                    skipFieldsSetByF = true;
                    break;
                case kUniqGroup: {
                    groupMode = true;
                    if (!option.hasArgument) {
                        groupSeparator = kUniqSepSeparate;
                        break;
                    }
                    static const std::vector<ArgChoice> kMethods = {
                        {"separate", kUniqSepSeparate},
                        {"prepend", kUniqSepPrepend},
                        {"append", kUniqSepAppend},
                        {"both", kUniqSepBoth},
                    };
                    const auto matched = ArgMatch(context, "--group", option.argument, kMethods);
                    if (!matched) {
                        return 1;
                    }
                    groupSeparator = *matched;
                    break;
                }
                case kUniqIgnoreCase: ignoreCase = true; break;
                case kUniqSkipChars:
                    if (!UniqParseCount(option.argument, skipChars)) {
                        context.Error(option.argument + ": invalid number of bytes to skip");
                        return 1;
                    }
                    break;
                case kUniqUnique: printFirstRepeated = false; break;
                case kUniqZeroTerminated: delimiter = '\0'; break;
                case kUniqCheckChars:
                    if (!UniqParseCount(option.argument, checkChars)) {
                        context.Error(option.argument + ": invalid number of bytes to compare");
                        return 1;
                    }
                    break;
                case kUniqObsoleteDigit: {
                    // The obsolete -N: -f's count, digits glued together. An -f
                    // since the last digit starts the number over.
                    if (skipFieldsSetByF) {
                        skipFields = 0;
                    }
                    const size_t digit = static_cast<size_t>(option.spelling[1] - '0');
                    skipFields = skipFields > (SIZE_MAX - digit) / 10
                        ? SIZE_MAX
                        : skipFields * 10 + digit;
                    skipFieldsSetByF = false;
                    break;
                }
                default: break;
            }
        }

        // GNU checks these two once every option has been read.
        if (groupMode && (count || !printUnique || !printFirstRepeated)) {
            context.Error("--group is mutually exclusive with -c/-d/-D/-u");
            context.TryHelp();
            return 1;
        }
        if (count && printLaterRepeated) {
            context.Error("printing all duplicated lines and repeat counts is meaningless");
            context.TryHelp();
            return 1;
        }

        // The operands: a +N one is the obsolete -s N; the others INPUT, then
        // OUTPUT; a third is one too many.
        std::string inputName = "-";
        bool haveInput = false;
        std::optional<std::string> outputName;
        for (const auto& operand : parsed->operands) {
            size_t obsoleteChars = 0;
            if (operand.size() > 1 && operand[0] == '+') {
                obsoleteChars = 1;
                while (obsoleteChars < operand.size()
                    && operand[obsoleteChars] >= '0' && operand[obsoleteChars] <= '9') {
                    ++obsoleteChars;
                }
            }
            if (obsoleteChars == operand.size()) {
                if (!UniqParseCount(operand.substr(1), skipChars)) {
                    context.Error(operand.substr(1) + ": invalid number of bytes to skip");
                    return 1;
                }
                continue;
            }
            if (!haveInput) {
                inputName = operand;
                haveInput = true;
            } else if (!outputName) {
                outputName = operand;
            } else {
                context.Error("extra operand " + GnuQuote(operand));
                context.TryHelp();
                return 1;
            }
        }

        InputOpenFailure failure = InputOpenFailure::None;
        auto input = OpenInputOperand(context, inputName, failure);
        if (!input) {
            switch (failure) {
                case InputOpenFailure::Missing:
                    context.Error(ShellEscapeQuoted(inputName) + ": No such file or directory");
                    break;
                case InputOpenFailure::Directory:
                    context.Error("error reading " + ShellEscapeQuoted(inputName, /*always=*/true) + ": Is a directory");
                    break;
                case InputOpenFailure::Denied:
                    context.Error(ShellEscapeQuoted(inputName) + ": Permission denied");
                    break;
                default:
                    context.Error(ShellEscapeQuoted(inputName) + ": Bad file descriptor");
                    break;
            }
            return 1;
        }

        // The OUTPUT is opened before anything is read: naming the input itself
        // empties it first, as GNU's does.
        std::shared_ptr<IFileDescriptor> outputFile;
        if (outputName && *outputName != "-") {
            IFileIO& io = context.IO();
            FileStatus status;
            const bool exists = io.Stat(*outputName, status) == 0;
            if (exists && status.type == DirectoryEntryType::Dir) {
                context.Error(ShellEscapeQuoted(*outputName) + ": Is a directory");
                return 1;
            }
            outputFile = io.OpenFile(*outputName, kFileOpenWriteCreateTruncate, kFileCreateMode);
            if (!outputFile) {
                context.Error(ShellEscapeQuoted(*outputName) + ": "
                    + (exists ? "Permission denied" : "No such file or directory"));
                return 1;
            }
        }

        UniqRun run{context, std::move(outputFile)};
        run.count = count;
        run.printUnique = printUnique;
        run.printFirstRepeated = printFirstRepeated;
        run.printLaterRepeated = printLaterRepeated;
        run.groupMode = groupMode;
        run.groupSeparator = groupSeparator;
        run.allRepeatedSeparator = allRepeatedSeparator;
        run.ignoreCase = ignoreCase;
        run.delimiter = delimiter;
        run.skipFields = skipFields;
        run.skipChars = skipChars;
        run.checkChars = checkChars;
        return run.Run(*input, inputName);
    }

private:
    // One run of uniq: the printing rules, the output, and the group being
    // decided. Only the current group's first line is kept, so the input is
    // streamed: each line is written as soon as its fate is known.
    struct UniqRun {
        BuiltinContext& context;
        std::shared_ptr<IFileDescriptor> outputFile;
        std::string buffer;  // outputFile's, flushed every 4096 bytes

        bool count = false;
        bool printUnique = true;
        bool printFirstRepeated = true;
        bool printLaterRepeated = false;
        bool groupMode = false;
        int groupSeparator = kUniqSepSeparate;
        int allRepeatedSeparator = kUniqSepNone;
        bool ignoreCase = false;
        char delimiter = '\n';
        size_t skipFields = 0;
        size_t skipChars = 0;
        size_t checkChars = SIZE_MAX;

        bool haveGroup = false;
        std::string firstLine;
        std::string firstKey;
        uint64_t groupSize = 0;
        bool firstLinePrinted = false;  // -D and --group print it themselves
        bool anyGroupPrinted = false;   // what 'separate' needs to know

        int Run(IFileDescriptor& input, const std::string& inputName) {
            BuiltinLineReader reader(context, input, delimiter);
            std::string line;
            bool delimited = true;
            while (true) {
                const LineReadResult result = reader.Next(line, delimited);
                if (result == LineReadResult::End) {
                    break;
                }
                if (result == LineReadResult::Stopped) {
                    return 1;  // quiet: the process reports its own stop code
                }
                if (result == LineReadResult::Error) {
                    context.Error("error reading " + GnuQuote(inputName) + ": Input/output error");
                    return 1;
                }
                const std::string key = UniqKey(line, skipFields, skipChars, checkChars);
                bool ok = true;
                if (!haveGroup) {
                    ok = StartGroup(line, key);
                } else if (UniqKeysEqual(key, firstKey, ignoreCase)) {
                    ok = AddToGroup(line);
                } else {
                    ok = FinishGroup() && StartGroup(line, key);
                }
                if (!ok) {
                    context.Error("write error: Input/output error");
                    return 1;
                }
            }
            if (!FinishGroup() || !Flush()) {
                context.Error("write error: Input/output error");
                return 1;
            }
            return 0;
        }

        // Writes |text| out: to the OUTPUT file through the buffer, or through
        // context.Out when there is none. False on a failed write.
        bool Write(const std::string& text) {
            if (!outputFile) {
                context.Out(text);
                return true;
            }
            buffer += text;
            return buffer.size() < kBuiltinOutBufferSize || Flush();
        }

        bool Flush() {
            if (!outputFile || buffer.empty()) {
                return true;
            }
            const ssize_t written = WriteFully(*outputFile, buffer);
            buffer.clear();
            return written >= 0;
        }

        // The printed text is the line itself, never its compared part; every
        // line ends with the delimiter, a last one that had none included.
        bool EmitLine(const std::string& text, bool withCount, uint64_t size) {
            std::string out;
            if (withCount) {
                out = std::to_string(size);
                if (out.size() < 7) {
                    out.insert(0, 7 - out.size(), ' ');
                }
                out += ' ';
            }
            out += text;
            out += delimiter;
            return Write(out);
        }

        bool EmitEmptyLine() {
            return Write(std::string(1, delimiter));
        }

        bool BeforeGroup(int separator) {
            // 'both' writes one separator between groups, not two: the append
            // after a group already made the boundary, so its prepend fires
            // only before the first group, as GNU's does.
            return separator == kUniqSepPrepend
                || (separator == kUniqSepBoth && !anyGroupPrinted)
                || (separator == kUniqSepSeparate && anyGroupPrinted);
        }

        bool StartGroup(const std::string& line, const std::string& key) {
            haveGroup = true;
            firstLine = line;
            firstKey = key;
            groupSize = 1;
            firstLinePrinted = false;
            if (groupMode) {
                // --group prints every line, separators around the groups.
                if (BeforeGroup(groupSeparator) && !EmitEmptyLine()) {
                    return false;
                }
                if (!EmitLine(line, false, 0)) {
                    return false;
                }
                firstLinePrinted = true;
                anyGroupPrinted = true;
            }
            return true;
        }

        bool AddToGroup(const std::string& line) {
            ++groupSize;
            if (groupMode) {
                return EmitLine(line, false, 0);
            }
            if (groupSize == 2 && printLaterRepeated) {
                // The group just proved repeated: its first line first, then
                // this one. Nothing can be printed before this moment, for the
                // group was a single line until now.
                if (BeforeGroup(allRepeatedSeparator) && !EmitEmptyLine()) {
                    return false;
                }
                if (printFirstRepeated && !EmitLine(firstLine, false, 0)) {
                    return false;
                }
                firstLinePrinted = printFirstRepeated;
                anyGroupPrinted = true;
            }
            return printLaterRepeated ? EmitLine(line, false, 0) : true;
        }

        bool FinishGroup() {
            if (!haveGroup) {
                return true;
            }
            bool ok = true;
            if (groupMode) {
                if ((groupSeparator == kUniqSepAppend || groupSeparator == kUniqSepBoth)
                    && !EmitEmptyLine()) {
                    ok = false;
                }
            } else if (groupSize == 1) {
                // A line on its own: -c prints its count of 1.
                if (printUnique) {
                    ok = EmitLine(firstLine, count, 1);
                    anyGroupPrinted = true;
                }
            } else if (!firstLinePrinted && printFirstRepeated) {
                // -c needs the whole group counted, so the first line waits
                // until here (with -D it went out with the second line).
                ok = EmitLine(firstLine, count, groupSize);
                firstLinePrinted = true;
                anyGroupPrinted = true;
            }
            haveGroup = false;
            return ok;
        }
    };
};

} // namespace

std::shared_ptr<IBuiltinCommand> CreateUniqCommand() {
    return std::make_shared<UniqCommand>();
}

} // namespace Haisos