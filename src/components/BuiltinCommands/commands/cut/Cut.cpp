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

enum CutOption {
    kCutBytes = 1,
    kCutCharacters,
    kCutDelimiter,
    kCutFields,
    kCutIgnored,  // -n, which GNU ignores too
    kCutComplement,
    kCutOnlyDelimited,
    kCutOutputDelimiter,
    kCutZeroTerminated,
};

// One selected range, 1-based and inclusive; SIZE_MAX as an end means "to the
// end of the line" (N-). "-M" starts at 1.
using CutRange = std::pair<size_t, size_t>;

bool IsCutSeparator(char c) {
    return c == ',' || c == ' ' || c == '\t';
}

// The digits from |pos| on, as a number saturating at SIZE_MAX (|overflow|
// then set). Returns the number of digits there are (0 when |pos| is not on
// one) -- the digits still count past a saturation.
size_t ReadCutDigits(const std::string& item, size_t pos, size_t& value, bool& overflow) {
    size_t digits = 0;
    value = 0;
    overflow = false;
    while (pos + digits < item.size() && item[pos + digits] >= '0' && item[pos + digits] <= '9') {
        const size_t d = static_cast<size_t>(item[pos + digits] - '0');
        if (overflow || value > (SIZE_MAX - d) / 10) {
            overflow = true;  // saturated; the digits still count
        } else {
            value = value * 10 + d;
        }
        ++digits;
    }
    return digits;
}

// A LIST: items separated by ',' or blanks -- an empty one is an error, so is
// one between two separators -- each item N, N-, N-M or -M. The ranges are
// sorted and overlapping ones merged (adjacent ones kept apart, which is what
// decides where --output-delimiter goes). The messages are GNU's, |fields|
// picking the words: "fields are numbered from 1" against "byte/character
// positions are numbered from 1", "invalid field value" against "invalid
// byte/character position", and so on.
std::optional<std::vector<CutRange>> ParseCutList(const std::string& list, bool fields,
                                                  std::string& error) {
    std::vector<CutRange> ranges;
    size_t start = 0;
    while (start <= list.size()) {
        size_t end = start;
        while (end < list.size() && !IsCutSeparator(list[end])) {
            ++end;
        }
        const std::string item = list.substr(start, end - start);

        size_t pos = 0;
        size_t first = 0;
        bool firstOverflow = false;
        const size_t firstDigits = ReadCutDigits(item, pos, first, firstOverflow);
        pos += firstDigits;
        if (firstDigits > 0 && first == 0) {
            error = fields ? "fields are numbered from 1" : "byte/character positions are numbered from 1";
            return std::nullopt;
        }
        // GNU takes a number up to SIZE_MAX - 1; SIZE_MAX itself is too large.
        if (firstDigits > 0 && (firstOverflow || first == SIZE_MAX)) {
            error = (fields ? "field number " : "byte/character offset ")
                + GnuQuote(item.substr(0, firstDigits)) + " is too large";
            return std::nullopt;
        }

        // A '-' before the first number is an open start ("-M"), one after it
        // opens the end ("N-", "N-M"); a second one anywhere is an error.
        bool dash = false;
        if (pos < item.size() && item[pos] == '-') {
            ++pos;
            dash = true;
            if (pos < item.size() && item[pos] == '-') {
                error = fields ? "invalid field range" : "invalid byte or character range";
                return std::nullopt;
            }
        }
        size_t second = 0;
        bool secondOverflow = false;
        const size_t secondDigits = ReadCutDigits(item, pos, second, secondOverflow);
        pos += secondDigits;
        if (secondDigits > 0 && (secondOverflow || second == SIZE_MAX)) {
            error = (fields ? "field number " : "byte/character offset ")
                + GnuQuote(item.substr(pos - secondDigits, secondDigits)) + " is too large";
            return std::nullopt;
        }

        // Whatever is left is either a second '-' or a byte no number is made
        // of; what is named in the message is the rest of the item from it.
        if (pos < item.size()) {
            if (item[pos] == '-') {
                error = fields ? "invalid field range" : "invalid byte or character range";
            } else {
                error = (fields ? "invalid field value " : "invalid byte/character position ")
                    + GnuQuote(item.substr(pos));
            }
            return std::nullopt;
        }

        if (firstDigits == 0 && secondDigits == 0 && !dash) {
            // An empty item, the whole list empty included.
            error = fields ? "fields are numbered from 1" : "byte/character positions are numbered from 1";
            return std::nullopt;
        }
        if (firstDigits == 0 && secondDigits == 0) {
            error = "invalid range with no endpoint: -";
            return std::nullopt;
        }

        const size_t rangeStart = firstDigits > 0 ? first : 1;
        if (dash && secondDigits > 0 && second < rangeStart) {
            error = "invalid decreasing range";
            return std::nullopt;
        }
        size_t rangeEnd = first;
        if (dash) {
            rangeEnd = secondDigits > 0 ? second : SIZE_MAX;  // N-: to the end
        }
        ranges.push_back({rangeStart, rangeEnd});
        if (end == list.size()) {
            break;
        }
        start = end + 1;
    }

    std::stable_sort(ranges.begin(), ranges.end());
    std::vector<CutRange> merged;
    for (const auto& range : ranges) {
        if (!merged.empty() && range.first <= merged.back().second) {
            merged.back().second = std::max(merged.back().second, range.second);
        } else {
            merged.push_back(range);
        }
    }
    return merged;
}

// The other side of the selected set, over 1..the end of everything.
std::vector<CutRange> ComplementCutRanges(const std::vector<CutRange>& ranges) {
    std::vector<CutRange> result;
    size_t next = 1;
    for (const auto& range : ranges) {
        if (range.first > next) {
            result.push_back({next, range.first - 1});
        }
        if (range.second == SIZE_MAX) {
            return result;
        }
        next = range.second + 1;
    }
    result.push_back({next, SIZE_MAX});
    return result;
}

class CutCommand : public IBuiltinCommand {
public:
    std::string Name() const override { return "cut"; }
    std::string Version() const override { return "1.0.0"; }

    const std::vector<BuiltinOption>& Options() const override {
        static const std::vector<BuiltinOption> options = {
            {'b', "bytes", kCutBytes, BuiltinArgument::Required, "LIST", "select only these bytes"},
            {'c', "characters", kCutCharacters, BuiltinArgument::Required, "LIST", "select only these characters (bytes, as GNU cut does)"},
            {'d', "delimiter", kCutDelimiter, BuiltinArgument::Required, "DELIM", "use DELIM instead of TAB for field delimiter"},
            {'f', "fields", kCutFields, BuiltinArgument::Required, "LIST", "select only these fields; also print any line with no delimiter, unless -s is given"},
            {'n', "", kCutIgnored, BuiltinArgument::None, "", "(ignored)"},
            {0, "complement", kCutComplement, BuiltinArgument::None, "", "complement the set of selected bytes, characters or fields"},
            {'s', "only-delimited", kCutOnlyDelimited, BuiltinArgument::None, "", "do not print lines not containing delimiters"},
            {0, "output-delimiter", kCutOutputDelimiter, BuiltinArgument::Required, "STRING", "use STRING as the output delimiter; the default is the input delimiter"},
            {'z', "zero-terminated", kCutZeroTerminated, BuiltinArgument::None, "", "line delimiter is NUL, not newline"},
        };
        return options;
    }

    BuiltinHelp Help() const override {
        return BuiltinHelp{
            "remove sections from each line of files",
            {"cut OPTION... [FILE]..."},
            "Each LIST is N, N-, N-M or -M, its ranges separated by commas or blanks.\n"
            "-c counts bytes, as GNU cut does; there are no multibyte characters.\n"
            "With -f, a line with no delimiter is printed whole unless -s drops it.",
        };
    }

    int Run(BuiltinContext& context) override {
        int exitStatus = 0;
        const auto parsed = BeginBuiltin(context, *this, /*usageErrorStatus=*/1, exitStatus);
        if (!parsed) {
            return exitStatus;
        }

        bool fieldMode = false;
        bool modeGiven = false;
        std::string list;
        char delimiter = '\t';
        bool delimiterGiven = false;
        bool suppress = false;
        bool complement = false;
        std::optional<std::string> outputDelimiter;
        char lineDelimiter = '\n';

        for (const auto& option : parsed->options) {
            switch (option.id) {
                case kCutBytes:
                case kCutCharacters:
                case kCutFields:
                    if (modeGiven) {
                        context.Error("only one list may be specified");
                        context.TryHelp();
                        return 1;
                    }
                    modeGiven = true;
                    fieldMode = option.id == kCutFields;
                    list = option.argument;
                    break;
                case kCutDelimiter:
                    if (option.argument.size() > 1) {
                        context.Error("the delimiter must be a single character");
                        context.TryHelp();
                        return 1;
                    }
                    delimiterGiven = true;
                    // -d '' is the NUL byte, as GNU's is.
                    delimiter = option.argument.empty() ? '\0' : option.argument[0];
                    break;
                case kCutIgnored: break;  // -n, which GNU ignores too
                case kCutComplement: complement = true; break;
                case kCutOnlyDelimited: suppress = true; break;
                case kCutOutputDelimiter: outputDelimiter = option.argument; break;
                case kCutZeroTerminated: lineDelimiter = '\0'; break;
                default: break;
            }
        }

        if (!modeGiven) {
            context.Error("you must specify a list of bytes, characters, or fields");
            context.TryHelp();
            return 1;
        }
        if (delimiterGiven && !fieldMode) {
            context.Error("an input delimiter may be specified only when operating on fields");
            context.TryHelp();
            return 1;
        }
        if (suppress && !fieldMode) {
            context.Error("suppressing non-delimited lines makes sense\n\tonly when operating on fields");
            context.TryHelp();
            return 1;
        }

        std::string listError;
        const auto ranges = ParseCutList(list, fieldMode, listError);
        if (!ranges) {
            context.Error(listError);
            context.TryHelp();
            return 1;
        }
        const std::vector<CutRange> selected = complement ? ComplementCutRanges(*ranges) : *ranges;
        // What joins the printed pieces: --output-delimiter ('' is the NUL
        // byte, as GNU's), else the input delimiter; in byte mode nothing,
        // unless --output-delimiter says one.
        const std::string outDelimiter = outputDelimiter
            ? (outputDelimiter->empty() ? std::string(1, '\0') : *outputDelimiter)
            : (fieldMode ? std::string(1, delimiter) : std::string());
        const bool haveOutDelimiter = !outDelimiter.empty();

        int status = 0;
        std::vector<std::string> names = parsed->operands;
        if (names.empty()) {
            names.push_back("-");  // the standard input
        }
        for (const auto& name : names) {
            InputOpenFailure failure = InputOpenFailure::None;
            auto input = OpenInputOperand(context, name, failure);
            if (!input) {
                switch (failure) {
                    case InputOpenFailure::Missing:
                        context.Error(ShellEscapeQuoted(name) + ": No such file or directory");
                        break;
                    case InputOpenFailure::Directory:
                        context.Error(ShellEscapeQuoted(name) + ": Is a directory");
                        break;
                    case InputOpenFailure::Denied:
                        context.Error(ShellEscapeQuoted(name) + ": Permission denied");
                        break;
                    default:
                        context.Error(ShellEscapeQuoted(name) + ": Bad file descriptor");
                        break;
                }
                status = 1;
                continue;
            }
            const int outcome = ProcessInput(context, *input, selected, fieldMode,
                delimiter, suppress, haveOutDelimiter, outDelimiter, lineDelimiter);
            if (outcome < 0) {
                return 1;  // stopped: quiet, the process reports its own code
            }
            if (outcome > 0) {
                context.Error(ShellEscapeQuoted(name) + ": Input/output error");
                status = 1;
            }
        }
        return status;
    }

private:
    // One input, streamed line by line. 0 read to its end, -1 stopped (the
    // caller returns quietly), 1 a read error (the caller reports it).
    static int ProcessInput(BuiltinContext& context, IFileDescriptor& input,
                            const std::vector<CutRange>& selected, bool fieldMode, char delimiter,
                            bool suppress, bool haveOutDelimiter, const std::string& outDelimiter,
                            char lineDelimiter) {
        BuiltinLineReader reader(context, input, lineDelimiter);
        std::string line;
        bool delimited = true;
        while (true) {
            const LineReadResult result = reader.Next(line, delimited);
            if (result == LineReadResult::End) {
                return 0;
            }
            if (result == LineReadResult::Stopped) {
                return -1;
            }
            if (result == LineReadResult::Error) {
                return 1;
            }
            std::string out;
            if (fieldMode) {
                if (!CutFields(line, selected, delimiter, suppress, outDelimiter, out)) {
                    continue;  // dropped by -s: not even the delimiter
                }
            } else {
                CutBytes(line, selected, haveOutDelimiter, outDelimiter, out);
            }
            out += lineDelimiter;
            context.Out(out);
        }
    }

    // The selected bytes, in order: --output-delimiter is written before the
    // first byte of each range that prints something after the first that did.
    static void CutBytes(const std::string& line, const std::vector<CutRange>& selected,
                         bool haveOutDelimiter, const std::string& outDelimiter, std::string& out) {
        bool anyPrinted = false;
        for (const auto& [start, end] : selected) {
            const size_t last = std::min(end, line.size());
            bool rangePrinted = false;
            for (size_t i = start; i <= last; ++i) {
                if (haveOutDelimiter && anyPrinted && !rangePrinted) {
                    out += outDelimiter;
                }
                out += line[i - 1];
                rangePrinted = true;
                anyPrinted = true;
            }
        }
    }

    // The selected fields, joined by the output delimiter (a delimiter written
    // before every field after the first one printed, an empty field one
    // included; a field past the line's end is absent, and writes nothing). A
    // line with no delimiter is printed whole, or dropped with -s -- which
    // returns false, for even the line's delimiter is then not written.
    static bool CutFields(const std::string& line, const std::vector<CutRange>& selected,
                          char delimiter, bool suppress, const std::string& outDelimiter,
                          std::string& out) {
        if (line.find(delimiter) == std::string::npos) {
            if (!suppress) {
                out = line;
                return true;
            }
            return false;
        }
        std::vector<std::pair<size_t, size_t>> spans;  // each field's [start, end)
        size_t start = 0;
        for (size_t i = 0; i <= line.size(); ++i) {
            if (i == line.size() || line[i] == delimiter) {
                spans.push_back({start, i});
                start = i + 1;
            }
        }
        bool printed = false;
        for (const auto& [first, last] : selected) {
            const size_t end = std::min(last, spans.size());
            for (size_t field = first; field <= end; ++field) {
                if (printed) {
                    out += outDelimiter;
                }
                out.append(line, spans[field - 1].first, spans[field - 1].second - spans[field - 1].first);
                printed = true;
            }
        }
        return true;
    }
};

} // namespace

std::shared_ptr<IBuiltinCommand> CreateCutCommand() {
    return std::make_shared<CutCommand>();
}

} // namespace Haisos