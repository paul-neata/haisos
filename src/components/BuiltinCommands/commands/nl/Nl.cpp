#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>
#include "BuiltinCommand.h"
#include "BuiltinText.h"
#include "src/components/Regex/Regex.h"

namespace Haisos {

namespace {

enum NlOption {
    kNlBodyStyle = 1,
    kNlDelimiter,
    kNlFooterStyle,
    kNlHeaderStyle,
    kNlIncrement,
    kNlJoinBlanks,
    kNlNumberFormat,
    kNlNoRenumber,
    kNlNumberSeparator,
    kNlStartNumber,
    kNlWidth,
};

// A numbering style, per section: all lines, non-empty lines, no lines, or
// the lines matching a BRE.
enum class NlStyleKind { All, NonEmpty, None, Match };

struct NlStyle {
    NlStyleKind kind = NlStyleKind::None;
    std::shared_ptr<const Regex> match;
};

enum NlFormat { kNlLeft, kNlRight, kNlRightZero };

// The three sections a logical page falls into.
enum NlSection { kNlBody = 0, kNlHeader = 1, kNlFooter = 2 };

// One FILE's numbering, carrying the line number, the empty-line run and the
// current section across lines and FILE operands, as GNU's does.
class NlRun {
public:
    NlRun(BuiltinContext& context, intmax_t start, intmax_t increment, int joinBlanks,
          NlFormat format, int width, std::string separator, bool renumber,
          const NlStyle (&styles)[3], const std::string (&delimiters)[3])
        : m_context(context)
        , m_start(start)
        , m_increment(increment)
        , m_joinBlanks(joinBlanks)
        , m_format(format)
        , m_width(width)
        , m_separator(std::move(separator))
        , m_renumber(renumber)
        , m_styles(styles)
        , m_delimiters(delimiters) {
        m_lineNumber = start;
    }

    // One line, without its newline. Returns false when nl must stop at
    // once: a line-number overflow.
    bool Line(const std::string& line) {
        // A logical page delimiter: the line itself comes out bare, the
        // section changes under it and the numbering starts over.
        for (int section = 0; section < 3; ++section) {
            if (!m_delimiters[section].empty() && line == m_delimiters[section]) {
                m_section = section;
                m_blankRun = 0;
                if (m_renumber) {
                    m_lineNumber = m_start;
                    m_overflowed = false;
                }
                m_context.Out("\n");
                return true;
            }
        }
        const NlStyle& style = m_styles[m_section];
        bool number = false;
        if (line.empty()) {
            ++m_blankRun;
            // The 'a' style numbers a group of -l empty lines at its end:
            // every -l-th of a run.
            number = style.kind == NlStyleKind::All && m_blankRun % m_joinBlanks == 0;
            if (style.kind == NlStyleKind::Match) {
                RegexMatch unused;
                number = style.match->Search(line, 0, unused);
            }
        } else {
            m_blankRun = 0;
            switch (style.kind) {
                case NlStyleKind::All:
                case NlStyleKind::NonEmpty:
                    number = true;
                    break;
                case NlStyleKind::Match: {
                    RegexMatch unused;
                    number = style.match->Search(line, 0, unused);
                    break;
                }
                case NlStyleKind::None:
                    break;
            }
        }
        if (!number) {
            // No number: the number's width and the separator's, in spaces.
            m_context.Out(std::string(static_cast<size_t>(m_width) + m_separator.size(), ' ')
                + line + "\n");
            return true;
        }
        if (m_overflowed) {
            m_context.Error("line number overflow");
            return false;
        }
        const char* pattern = m_format == kNlLeft ? "%-*jd"
            : (m_format == kNlRight ? "%*jd" : "%0*jd");
        // Sized for the width asked, however wide (-w may go up to INT_MAX).
        const int length = std::snprintf(nullptr, 0, pattern, m_width, m_lineNumber);
        std::string formatted(static_cast<size_t>(length > 0 ? length : 0) + 1, '\0');
        std::snprintf(&formatted[0], formatted.size(), pattern, m_width, m_lineNumber);
        formatted.pop_back();
        m_context.Out(formatted + m_separator + line + "\n");
        // Overflow either way: a negative increment can run past INTMAX_MIN.
        if (m_increment > 0 ? m_lineNumber > INTMAX_MAX - m_increment
                            : m_lineNumber < INTMAX_MIN - m_increment) {
            // The next line to number is past the end: reported there, not
            // here -- a section that renumbers first starts over cleanly.
            m_overflowed = true;
        } else {
            m_lineNumber += m_increment;
        }
        return true;
    }

private:
    BuiltinContext& m_context;
    intmax_t m_start;
    intmax_t m_increment;
    int m_joinBlanks;
    int m_width;
    NlFormat m_format;
    std::string m_separator;
    bool m_renumber;
    const NlStyle (&m_styles)[3];
    const std::string (&m_delimiters)[3];

    intmax_t m_lineNumber = 0;
    int m_section = kNlBody;
    intmax_t m_blankRun = 0;
    bool m_overflowed = false;
};

class NlCommand : public IBuiltinCommand {
public:
    std::string Name() const override { return "nl"; }
    std::string Version() const override { return "1.0.0"; }

    const std::vector<BuiltinOption>& Options() const override {
        static const std::vector<BuiltinOption> options = {
            {'b', "body-numbering", kNlBodyStyle, BuiltinArgument::Required, "STYLE",
                "use STYLE for numbering body lines"},
            {'d', "section-delimiter", kNlDelimiter, BuiltinArgument::Required, "CC",
                "use CC for logical page delimiters"},
            {'f', "footer-numbering", kNlFooterStyle, BuiltinArgument::Required, "STYLE",
                "use STYLE for numbering footer lines"},
            {'h', "header-numbering", kNlHeaderStyle, BuiltinArgument::Required, "STYLE",
                "use STYLE for numbering header lines"},
            {'i', "line-increment", kNlIncrement, BuiltinArgument::Required, "NUMBER",
                "line number increment at each line"},
            {'l', "join-blank-lines", kNlJoinBlanks, BuiltinArgument::Required, "NUMBER",
                "group of NUMBER empty lines counted as one"},
            {'n', "number-format", kNlNumberFormat, BuiltinArgument::Required, "FORMAT",
                "insert line numbers according to FORMAT"},
            {'p', "no-renumber", kNlNoRenumber, BuiltinArgument::None, "",
                "do not reset line numbers for each section"},
            {'s', "number-separator", kNlNumberSeparator, BuiltinArgument::Required, "STRING",
                "add STRING after (possible) line number"},
            {'v', "starting-line-number", kNlStartNumber, BuiltinArgument::Required, "NUMBER",
                "first line number for each section"},
            {'w', "number-width", kNlWidth, BuiltinArgument::Required, "NUMBER",
                "use NUMBER columns for line numbers"},
        };
        return options;
    }

    BuiltinHelp Help() const override {
        return BuiltinHelp{
            "write each FILE to standard output, with line numbers added",
            {"nl [OPTION]... [FILE]..."},
            "STYLE is 'a' (all lines), 't' (non-empty lines), 'n' (no lines) or 'pBRE'\n"
            "(the lines containing a match for the BRE). FORMAT is 'ln', 'rn' or 'rz',\n"
            "left justified, right justified, or right justified with leading zeros.\n"
            "CC, the section delimiter characters, build the three logical page\n"
            "delimiters: a line of CC three times starts a header, twice a body, once\n"
            "a footer; a one-character CC gains a ':', and an empty -d turns the\n"
            "sections off. Numbering, the current section and the empty-line run all\n"
            "carry across FILE operands, and a last line without a newline gets one.",
        };
    }

    int Run(BuiltinContext& context) override {
        int exitStatus = 0;
        const auto parsed = BeginBuiltin(context, *this, /*usageErrorStatus=*/1, exitStatus);
        if (!parsed) {
            return exitStatus;
        }

        NlStyle styles[3];
        styles[kNlBody].kind = NlStyleKind::NonEmpty; // -b t
        styles[kNlHeader].kind = NlStyleKind::None;    // -h n
        styles[kNlFooter].kind = NlStyleKind::None;    // -f n
        std::string delimiterCc = "\\:";
        intmax_t start = 1;
        intmax_t increment = 1;
        int joinBlanks = 1;
        NlFormat format = kNlRight;
        bool renumber = true;
        std::string separator = "\t";
        int width = 6;

        // Options are taken in the order given, and each value is checked as
        // it comes, as GNU's does.
        for (const auto& option : parsed->options) {
            switch (option.id) {
                case kNlBodyStyle:
                case kNlHeaderStyle:
                case kNlFooterStyle: {
                    NlStyle& style = styles[option.id == kNlBodyStyle
                        ? kNlBody
                        : (option.id == kNlHeaderStyle ? kNlHeader : kNlFooter)];
                    const char* what = option.id == kNlBodyStyle
                        ? "body"
                        : (option.id == kNlHeaderStyle ? "header" : "footer");
                    if (!ParseStyle(context, option, what, style)) {
                        return 1;
                    }
                    break;
                }
                case kNlDelimiter:
                    delimiterCc = option.argument;
                    break;
                case kNlIncrement:
                    if (!ParseNumber(context, option, "line number increment", increment)) {
                        return 1;
                    }
                    break;
                case kNlJoinBlanks: {
                    intmax_t value = 0;
                    if (!ParseNumber(context, option, "line number of blank lines", value)
                        || !CheckRange(context, option, "line number of blank lines", value, 1,
                                       INT32_MAX)) {
                        return 1;
                    }
                    joinBlanks = static_cast<int>(value);
                    break;
                }
                case kNlNumberFormat:
                    if (option.argument == "ln") {
                        format = kNlLeft;
                    } else if (option.argument == "rn") {
                        format = kNlRight;
                    } else if (option.argument == "rz") {
                        format = kNlRightZero;
                    } else {
                        context.Error("invalid line numbering format: " + GnuQuote(option.argument));
                        context.TryHelp();
                        return 1;
                    }
                    break;
                case kNlNoRenumber:
                    renumber = false;
                    break;
                case kNlNumberSeparator:
                    separator = option.argument;
                    break;
                case kNlStartNumber:
                    if (!ParseNumber(context, option, "starting line number", start)) {
                        return 1;
                    }
                    break;
                case kNlWidth: {
                    intmax_t value = 0;
                    if (!ParseNumber(context, option, "line number field width", value)
                        || !CheckRange(context, option, "line number field width", value, 1,
                                       INT32_MAX)) {
                        return 1;
                    }
                    width = static_cast<int>(value);
                    break;
                }
                default:
                    break;
            }
        }

        // The delimiters: a line of CC thrice, twice, once starts a header,
        // a body, a footer. A one-character CC gains a ':'; an empty CC
        // turns the sections off.
        std::string delimiters[3];
        if (!delimiterCc.empty()) {
            if (delimiterCc.size() == 1) {
                delimiterCc += ':';
            }
            delimiters[kNlFooter] = delimiterCc;
            delimiters[kNlBody] = delimiterCc + delimiterCc;
            delimiters[kNlHeader] = delimiters[kNlBody] + delimiterCc;
        }

        NlRun run(context, start, increment, joinBlanks, format, width, separator, renumber,
                  styles, delimiters);

        int status = 0;
        std::vector<std::string> operands = parsed->operands;
        if (operands.empty()) {
            operands.push_back("-");
        }
        for (const auto& operand : operands) {
            if (context.StopRequested()) {
                return status;
            }
            InputOpenFailure failure = InputOpenFailure::None;
            std::shared_ptr<IFileDescriptor> input;
            if (operand == "-") {
                input = context.IO().GetDescriptor(IFileIO::kStdIn);
                if (!input) {
                    failure = InputOpenFailure::BadDescriptor;
                }
            } else {
                input = OpenInputOperand(context, operand, failure);
            }
            if (failure != InputOpenFailure::None) {
                ReportOpenFailure(context, operand, failure);
                status = 1;
                continue;
            }
            BuiltinLineReader reader(context, *input, '\n');
            std::string line;
            bool delimited = true;
            while (true) {
                const LineReadResult result = reader.Next(line, delimited);
                if (result == LineReadResult::End) {
                    break; // a last line without a newline came as a Line, and got one
                }
                if (result == LineReadResult::Stopped) {
                    return status;
                }
                if (result == LineReadResult::Error) {
                    ReportReadError(context, operand);
                    status = 1;
                    break;
                }
                if (!run.Line(line)) {
                    return 1; // a line-number overflow ends nl at once
                }
            }
        }
        return status;
    }

private:
    static void ReportOpenFailure(BuiltinContext& context, const std::string& name,
                                  InputOpenFailure failure) {
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
            case InputOpenFailure::BadDescriptor:
                context.Error(ShellEscapeQuoted(name) + ": Bad file descriptor");
                break;
            case InputOpenFailure::None:
                break;
        }
    }

    static void ReportReadError(BuiltinContext& context, const std::string& name) {
        context.Error(ShellEscapeQuoted(name) + ": Input/output error");
    }

    // 'a', 't', 'n' or 'p' + a BRE, compiled now, as GNU's does.
    static bool ParseStyle(BuiltinContext& context, const ParsedBuiltinOption& option,
                           const char* what, NlStyle& style) {
        const std::string& value = option.argument;
        if (value == "a") {
            style.kind = NlStyleKind::All;
            style.match.reset();
            return true;
        }
        if (value == "t") {
            style.kind = NlStyleKind::NonEmpty;
            style.match.reset();
            return true;
        }
        if (value == "n") {
            style.kind = NlStyleKind::None;
            style.match.reset();
            return true;
        }
        if (value.size() >= 1 && value[0] == 'p') {
            std::string error;
            auto regex = Regex::Compile(value.substr(1), RegexOptions{}, error);
            if (!regex) {
                context.Error(error);
                return false;
            }
            style.kind = NlStyleKind::Match;
            style.match = std::move(regex);
            return true;
        }
        context.Error(std::string("invalid ") + what + " numbering style: " + GnuQuote(value));
        context.TryHelp();
        return false;
    }

    // GNU's xstrtoimax: a whole intmax, an optional leading '+' or '-', and
    // nothing but digits after it. Reports and returns false for a value that
    // is not a number (no reason) or one past intmax (GNU's EOVERFLOW
    // wording); a negative value may reach one past intmax, to INTMAX_MIN.
    static bool ParseNumber(BuiltinContext& context, const ParsedBuiltinOption& option,
                            const std::string& what, intmax_t& out) {
        const std::string& text = option.argument;
        size_t used = 0;
        bool negative = false;
        if (used < text.size() && (text[used] == '+' || text[used] == '-')) {
            negative = text[used] == '-';
            ++used;
        }
        if (used >= text.size()) {
            context.Error("invalid " + what + ": " + GnuQuote(text));
            return false;
        }
        uint64_t magnitude = 0;
        bool overflow = false;
        for (; used < text.size(); ++used) {
            const char c = text[used];
            if (c < '0' || c > '9') {
                context.Error("invalid " + what + ": " + GnuQuote(text));
                return false;
            }
            if (magnitude > (UINT64_MAX - static_cast<uint64_t>(c - '0')) / 10) {
                overflow = true;
            } else {
                magnitude = magnitude * 10 + static_cast<uint64_t>(c - '0');
            }
        }
        const uint64_t limit = negative
            ? static_cast<uint64_t>(INTMAX_MAX) + 1 // INTMAX_MIN's magnitude
            : static_cast<uint64_t>(INTMAX_MAX);
        if (overflow || magnitude > limit) {
            context.Error("invalid " + what + ": " + GnuQuote(text)
                + ": Value too large for defined data type");
            return false;
        }
        out = negative
            ? (magnitude == static_cast<uint64_t>(INTMAX_MAX) + 1
                ? INTMAX_MIN
                : -static_cast<intmax_t>(magnitude))
            : static_cast<intmax_t>(magnitude);
        return true;
    }

    // The -w and -l range, GNU's two wordings: past intmax was already
    // reported by ParseNumber; past the int range is EOVERFLOW again, below
    // the minimum is ERANGE.
    static bool CheckRange(BuiltinContext& context, const ParsedBuiltinOption& option,
                           const std::string& what, intmax_t value, intmax_t minimum,
                           intmax_t maximum) {
        if (value > maximum) {
            context.Error("invalid " + what + ": " + GnuQuote(option.argument)
                + ": Value too large for defined data type");
            return false;
        }
        if (value < minimum) {
            context.Error("invalid " + what + ": " + GnuQuote(option.argument)
                + ": Numerical result out of range");
            return false;
        }
        return true;
    }
};

} // namespace

std::shared_ptr<IBuiltinCommand> CreateNlCommand() {
    return std::make_shared<NlCommand>();
}

} // namespace Haisos