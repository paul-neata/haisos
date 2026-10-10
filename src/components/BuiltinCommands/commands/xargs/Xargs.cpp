// xargs: builds and runs command lines from items read from standard input,
// after GNU findutils 4.9.0's xargs -- its messages byte for byte in the C
// locale, including the -I size ladder and the mutual-exclusion warnings.
// Programs run only through RunProgramAndWait (never the OS directly), and
// files -- the -a input -- only through context.IO().
#include "BuiltinCommand.h"
#include "BuiltinPrompt.h"
#include "BuiltinRunProgram.h"
#include "BuiltinText.h"
#include "src/components/Filesystem/FilesystemUtils.h"
#include "src/components/libheaders/DescriptorLineReader.h"
#include "interfaces/IEnvironment.h"
#include "interfaces/IFileIO.h"
#include <algorithm>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace Haisos {

namespace {

constexpr int kOptionNull = 1;            // -0, --null
constexpr int kOptionArgFile = 2;         // -a, --arg-file=FILE
constexpr int kOptionDelimiter = 3;       // -d, --delimiter=CHARACTER
constexpr int kOptionEofRequired = 4;     // -E END
constexpr int kOptionEofOptional = 5;     // -e, --eof[=END]
constexpr int kOptionReplaceRequired = 6; // -I R
constexpr int kOptionReplaceOptional = 7; // -i, --replace[=R]
constexpr int kOptionMaxLines = 8;        // -L MAX-LINES
constexpr int kOptionMaxLinesShort = 9;   // -l, --max-lines[=MAX-LINES]
constexpr int kOptionMaxArgs = 10;        // -n, --max-args=MAX-ARGS
constexpr int kOptionOpenTty = 11;        // -o, --open-tty
constexpr int kOptionMaxProcs = 12;        // -P, --max-procs=MAX-PROCS
constexpr int kOptionInteractive = 13;    // -p, --interactive
constexpr int kOptionProcessSlotVar = 14; // --process-slot-var=VAR
constexpr int kOptionNoRunIfEmpty = 15;   // -r, --no-run-if-empty
constexpr int kOptionMaxChars = 16;       // -s, --max-chars=MAX-CHARS
constexpr int kOptionShowLimits = 17;     // --show-limits
constexpr int kOptionVerbose = 18;        // -t, --verbose
constexpr int kOptionExit = 19;           // -x, --exit

// Linux's own ARG_MAX, the upper limit GNU computes xargs's from.
constexpr long kArgMax = 2097152;
// What GNU subtracts from ARG_MAX before the environment's own bytes.
constexpr long kArgMaxReserve = 2048;
// The command buffer used when -s does not say one.
constexpr long kDefaultBufferSize = 131072;

// The ways items group onto command lines; at most one of -L, -n and -I is
// active, the last one given, each warning about the one it replaces.
enum class ItemMode { Words, Lines, Args, Replace };

// Everything the options say, kept as GNU's own variables are.
struct Settings {
    ItemMode mode = ItemMode::Words;
    bool linesActive = false;   // -L/-l was given (for the warnings only)
    bool argsActive = false;    // -n was given
    bool replaceActive = false; // -I/-i was given
    long maxLines = 1;
    long maxArgs = 0;       // 0: no limit
    std::string replace;    // -I's R
    bool nullMode = false;  // -0
    char delimiter = '\0';
    bool delimiterMode = false; // -d
    std::string eofString;      // -E/-e; empty: none
    std::string argFile;        // -a; empty: standard input
    bool openTty = false;       // -o
    bool interactive = false;   // -p
    std::string slotVar;        // --process-slot-var
    bool noRunIfEmpty = false;  // -r
    std::optional<long> maxCharsGiven; // -s
    std::string maxCharsText;          // -s's value as written, for its warning
    bool showLimits = false;           // --show-limits
    bool verbose = false;              // -t
    bool exitIfFull = false;           // -x, and -I implies it
    bool exitExplicit = false;         // -x was given: survives a mode change
};

// Reads a file descriptor a chunk at a time, handing out items by delimiter
// and physical lines; a stop asked while it blocks in Read ends the input.
class ByteSource {
public:
    ByteSource(std::shared_ptr<IFileDescriptor> input, BuiltinContext& context)
        : m_input(std::move(input)), m_context(context) {}

    // The next item ending at |delimiter|: every delimited item, even an
    // empty one. False at end of input, with |item| holding a final
    // undelimited tail, counted only when it is not empty.
    bool NextDelimited(char delimiter, std::string& item, bool& delimited) {
        item.clear();
        delimited = false;
        while (true) {
            if (m_pos >= m_buffer.size() && !Refill()) {
                return !item.empty();
            }
            const char c = m_buffer[m_pos++];
            if (c == delimiter) {
                delimited = true;
                return true;
            }
            item += c;
        }
    }

    // The next physical line without its '\n'. False at end of input with
    // nothing read; a last line not ended by '\n' is a line too.
    bool NextLine(std::string& line) {
        line.clear();
        while (true) {
            if (m_pos >= m_buffer.size() && !Refill()) {
                return !line.empty();
            }
            const char c = m_buffer[m_pos++];
            if (c == '\n') {
                return true;
            }
            line += c;
        }
    }

private:
    // One more chunk into the buffer, dropping what was read. False at end of
    // input, on a failed read (a stop asked while it blocked included), and
    // when the process was asked to stop between chunks.
    bool Refill() {
        m_buffer.clear();
        m_pos = 0;
        if (m_atEnd || !m_input) {
            return false;
        }
        char chunk[65536];
        const ssize_t n = m_input->Read(chunk, sizeof(chunk));
        if (n <= 0) {
            // End of file, any failure, and a stop while blocked all end the
            // input; a stop between chunks is checked below.
            m_atEnd = true;
            m_input.reset();
            return false;
        }
        m_buffer.assign(chunk, static_cast<size_t>(n));
        if (m_context.StopRequested()) {
            m_atEnd = true;
            m_input.reset();
            return false;
        }
        return true;
    }

    std::shared_ptr<IFileDescriptor> m_input;
    BuiltinContext& m_context;
    std::string m_buffer;
    size_t m_pos = 0;
    bool m_atEnd = false;
};

// How one line's parse ended.
enum class ParseStatus { Ok, UnmatchedSingle, UnmatchedDouble, Nul };

// Parses one line's items with GNU's own grammar: blanks separate (or, in -I
// mode, are kept), quotes protect with no escapes inside, a backslash takes
// the next byte literally, a NUL cannot pass and ends the input, and the EOF
// string is the caller's to check on each item. State is kept across the one
// line and no further -- a quote open at the line's end is the error GNU
// prints, and the items the line had completed still run.
class QuoteParser {
public:
    explicit QuoteParser(bool blanksSeparate) : m_blanksSeparate(blanksSeparate) {}

    ParseStatus ParseLine(std::string line, std::vector<std::string>& items) {
        // -I's item is the whole line, its leading blanks gone.
        if (!m_blanksSeparate) {
            size_t start = 0;
            while (start < line.size() && (line[start] == ' ' || line[start] == '\t')) {
                ++start;
            }
            line.erase(0, start);
        }
        std::string item;
        bool hasItem = false;
        auto complete = [&]() {
            items.push_back(item);
            item.clear();
            hasItem = false;
        };
        for (size_t i = 0; i < line.size(); ++i) {
            const char c = line[i];
            if (c == '\0') {
                // A NUL cannot pass: the word being built becomes an item --
                // an empty one when nothing was built -- and the input ends.
                complete();
                return ParseStatus::Nul;
            }
            if (m_quote != 0) {
                if (c == m_quote) {
                    m_quote = 0;
                } else {
                    item += c;
                }
                continue;
            }
            if (m_blanksSeparate && (c == ' ' || c == '\t')) {
                if (hasItem) {
                    complete();
                }
                continue;
            }
            if (c == '\'' || c == '"') {
                m_quote = c;
                hasItem = true;
                continue;
            }
            if (c == '\\') {
                // A backslash takes the next byte literally, spaces and
                // quotes among them; one at the line's very end stands alone.
                if (i + 1 < line.size()) {
                    item += line[++i];
                }
                hasItem = true;
                continue;
            }
            item += c;
            hasItem = true;
        }
        if (m_quote != 0) {
            return m_quote == '\'' ? ParseStatus::UnmatchedSingle : ParseStatus::UnmatchedDouble;
        }
        if (hasItem) {
            complete();
        }
        return ParseStatus::Ok;
    }

private:
    bool m_blanksSeparate;
    char m_quote = 0;
};

// xargs's own quoting for the lines -t prints and -p asks about: GNU's
// shell-escape shape -- words of safe characters alone left bare, everything
// else in '...' with embedded quotes as '\'' , strings of quotes alone in
// "...", and control and high bytes in $'...' runs of octal escapes.
bool ShellSafeChar(char c) {
    const unsigned char b = static_cast<unsigned char>(c);
    if ((b >= 'a' && b <= 'z') || (b >= 'A' && b <= 'Z') || (b >= '0' && b <= '9')) {
        return true;
    }
    return b == '%' || b == '+' || b == ',' || b == '-' || b == '.' || b == '/' || b == ':'
        || b == '@' || b == ']' || b == '_';
}

bool AllShellSafe(const std::string& text) {
    return !text.empty() && std::all_of(text.begin(), text.end(),
        [](char c) { return ShellSafeChar(c); });
}

bool NeedsShellEscape(unsigned char b) {
    return b < 0x20 || b >= 0x7f;
}

// One control or high byte as $'...' writes it: the named escapes GNU's own
// print uses, everything else three octal digits.
std::string ShellEscapeByte(unsigned char b) {
    switch (b) {
        case '\a': return "\\a";
        case '\b': return "\\b";
        case '\f': return "\\f";
        case '\n': return "\\n";
        case '\r': return "\\r";
        case '\t': return "\\t";
        case '\v': return "\\v";
        case '\\': return "\\\\";
        default: {
            char buf[5];
            std::snprintf(buf, sizeof(buf), "\\%03o", b);
            return buf;
        }
    }
}

std::string ShellQuoteArg(const std::string& arg) {
    if (arg.empty()) {
        return "''";
    }
    if (AllShellSafe(arg)) {
        return arg;
    }
    // A string of quotes alone is shorter in double quotes.
    if (std::all_of(arg.begin(), arg.end(), [](char c) { return c == '\''; })) {
        return "\"" + arg + "\"";
    }
    if (std::none_of(arg.begin(), arg.end(), [](char c) {
            return NeedsShellEscape(static_cast<unsigned char>(c));
        })) {
        std::string quoted = "'";
        for (char c : arg) {
            if (c == '\'') {
                quoted += "'\\''";
            } else {
                quoted += c;
            }
        }
        return quoted + "'";
    }
    // Runs of plain bytes in '...', runs of control and high bytes in $'...'.
    std::string quoted;
    std::string plain;
    std::string escaped;
    auto flushPlain = [&]() {
        if (!plain.empty()) {
            quoted += "'" + plain + "'";
            plain.clear();
        }
    };
    auto flushEscaped = [&]() {
        if (!escaped.empty()) {
            quoted += "$'" + escaped + "'";
            escaped.clear();
        }
    };
    for (char c : arg) {
        if (NeedsShellEscape(static_cast<unsigned char>(c))) {
            flushPlain();
            escaped += ShellEscapeByte(static_cast<unsigned char>(c));
        } else {
            flushEscaped();
            if (c == '\'') {
                plain += "'\\''";
            } else {
                plain += c;
            }
        }
    }
    flushPlain();
    flushEscaped();
    return quoted;
}

class XargsCommand : public IBuiltinCommand {
public:
    std::string Name() const override { return "xargs"; }
    std::string Version() const override { return "1.0.0"; }

    const std::vector<BuiltinOption>& Options() const override {
        static const std::vector<BuiltinOption> options = {
            {'0', "null", kOptionNull, BuiltinArgument::None, "",
                "items are separated by a null, not whitespace; disables quote and backslash processing and logical EOF processing"},
            {'a', "arg-file", kOptionArgFile, BuiltinArgument::Required, "FILE",
                "read arguments from FILE, not standard input"},
            {'d', "delimiter", kOptionDelimiter, BuiltinArgument::Required, "CHARACTER",
                "items in input stream are separated by CHARACTER, not by whitespace; disables quote and backslash processing and logical EOF processing"},
            {'E', "", kOptionEofRequired, BuiltinArgument::Required, "END",
                "set logical EOF string; if END occurs as a line of input, the rest of the input is ignored"},
            {'e', "eof", kOptionEofOptional, BuiltinArgument::OptionalAttached, "[END]",
                "same as -E END if END is specified; otherwise, there is no end-of-file string"},
            {'I', "", kOptionReplaceRequired, BuiltinArgument::Required, "R",
                "same as --replace=R"},
            {'i', "replace", kOptionReplaceOptional, BuiltinArgument::OptionalAttached, "[R]",
                "replace R in INITIAL-ARGS with names read from standard input, split at newlines; if R is unspecified, assume {}"},
            {'L', "", kOptionMaxLines, BuiltinArgument::Required, "MAX-LINES",
                "use at most MAX-LINES non-blank input lines per command line"},
            {'l', "max-lines", kOptionMaxLinesShort, BuiltinArgument::OptionalAttached,
                "[MAX-LINES]",
                "similar to -L but defaults to one non-blank input line if MAX-LINES is not specified"},
            {'n', "max-args", kOptionMaxArgs, BuiltinArgument::Required, "MAX-ARGS",
                "use at most MAX-ARGS arguments per command line"},
            {'o', "open-tty", kOptionOpenTty, BuiltinArgument::None, "",
                "reopen stdin as /dev/tty in the child process before executing the command; useful to run an interactive application"},
            {'P', "max-procs", kOptionMaxProcs, BuiltinArgument::Required, "MAX-PROCS",
                "run at most MAX-PROCS processes at a time"},
            {'p', "interactive", kOptionInteractive, BuiltinArgument::None, "",
                "prompt before running commands"},
            {0, "process-slot-var", kOptionProcessSlotVar, BuiltinArgument::Required, "VAR",
                "set environment variable VAR in child processes"},
            {'r', "no-run-if-empty", kOptionNoRunIfEmpty, BuiltinArgument::None, "",
                "if there are no arguments, then do not run COMMAND; if this option is not given, COMMAND will be run at least once"},
            {'s', "max-chars", kOptionMaxChars, BuiltinArgument::Required, "MAX-CHARS",
                "limit length of command line to MAX-CHARS"},
            {0, "show-limits", kOptionShowLimits, BuiltinArgument::None, "",
                "show limits on command-line length"},
            {'t', "verbose", kOptionVerbose, BuiltinArgument::None, "",
                "print commands before executing them"},
            {'x', "exit", kOptionExit, BuiltinArgument::None, "",
                "exit if the size (see -s) is exceeded"},
        };
        return options;
    }

    BuiltinHelp Help() const override {
        return BuiltinHelp{
            "build and execute command lines from standard input",
            {"xargs [OPTION]... COMMAND [INITIAL-ARGS]..."},
            "-P runs one command at a time: Haisos has no parallel children,\n"
            "but the number is still checked; --process-slot-var is always 0.\n"
            "The limits come from a fixed ARG_MAX of 2097152, as Linux's own,\n"
            "less 2048 and the environment's bytes. Haisos has no /dev/tty:\n"
            "-p and -o take standard input as the terminal when it is one,\n"
            "and otherwise fail their first command and exit 1. A signal is\n"
            "an exit code 128+n; a child stopped by one makes xargs exit 125.\n"
            "Each child reads an empty input, never xargs's own."};
    }

    int Run(BuiltinContext& context) override {
        // One instance serves every process running the command -- several
        // at once, and xargs running xargs -- so each run gets its own state.
        XargsCommand run;
        return run.RunOnce(context);
    }

private:
    int RunOnce(BuiltinContext& context) {
        int exitStatus = 0;
        auto parsed = BeginBuiltin(context, *this, /*usageErrorStatus=*/1, exitStatus,
            /*stopAtFirstOperand=*/true);
        if (!parsed) {
            return exitStatus;
        }
        m_context = &context;
        Settings settings;
        if (!ProcessOptions(*parsed, settings)) {
            return 1;
        }
        m_settings = &settings;

        // The limits, by GNU's arithmetic: the environment's own bytes out
        // of ARG_MAX, then the buffer -s asked for or the default.
        long envBytes = 0;
        const auto environment = context.Process().GetEnvironment();
        for (const auto& name : environment->GetVariableNames()) {
            const auto value = environment->GetVariable(name);
            envBytes += static_cast<long>(name.size()) + 1
                + static_cast<long>(value ? value->size() : 0) + 1;
        }
        const long upperLimit = kArgMax - kArgMaxReserve - envBytes;
        long maxChars = std::min(kDefaultBufferSize, upperLimit);
        if (settings.maxCharsGiven) {
            maxChars = *settings.maxCharsGiven;
            if (maxChars > upperLimit) {
                context.Error("value " + settings.maxCharsText + " for -s option should be <= "
                    + std::to_string(upperLimit));
                maxChars = upperLimit;
            }
        }
        if (settings.showLimits) {
            context.ErrorText("Your environment variables take up " + std::to_string(envBytes)
                + " bytes\n");
            context.ErrorText("POSIX upper limit on argument length (this system): "
                + std::to_string(upperLimit) + "\n");
            context.ErrorText("POSIX smallest allowable upper limit on argument length "
                "(all systems): 4096\n");
            context.ErrorText("Maximum length of command we could actually use: "
                + std::to_string(upperLimit - envBytes) + "\n");
            context.ErrorText("Size of command buffer we are actually using: "
                + std::to_string(maxChars) + "\n");
            context.ErrorText("Maximum parallelism (--max-procs must be no greater): "
                "2147483647\n");
        }

        // The command and its initial arguments; with none, GNU's own echo.
        if (!parsed->operands.empty()) {
            m_command = parsed->operands.front();
            m_initialArgs.assign(parsed->operands.begin() + 1, parsed->operands.end());
        } else {
            m_command = "echo";
        }
        m_maxChars = maxChars;
        m_baseSize = static_cast<long>(m_command.size()) + 1;
        for (const auto& arg : m_initialArgs) {
            m_baseSize += static_cast<long>(arg.size()) + 1;
        }
        m_lineSize = m_baseSize;

        // GNU's startup check, the word modes only: the command and its
        // initial arguments alone must fit, even before anything is read.
        if (settings.mode != ItemMode::Replace && m_baseSize > maxChars) {
            context.Error("cannot fit single argument within argument list size limit");
            return 1;
        }

        // The input: -a's file, or standard input. A directory opens as GNU's
        // own open does and reads nothing, so it is an input at its end.
        std::shared_ptr<IFileDescriptor> input;
        if (!settings.argFile.empty()) {
            IFileIO& io = context.IO();
            FileStatus status;
            const bool isThere = io.Stat(settings.argFile, status) == 0;
            if (isThere && status.type == DirectoryEntryType::Dir) {
                // A directory reads nothing, as GNU's own open of one does on
                // Linux; the run goes on with an empty input.
                input = nullptr;
            } else if (isThere) {
                input = io.OpenFile(settings.argFile, kFileOpenReadOnly);
            }
            if (!input && !isThere) {
                context.Error("Cannot open input file " + GnuQuote(settings.argFile)
                    + ": No such file or directory");
                return 1;
            }
        } else {
            input = context.IO().GetDescriptor(IFileIO::kStdIn);
        }

        ByteSource source(std::move(input), context);
        const int result = settings.mode == ItemMode::Replace
            ? RunReplace(source)
            : RunWords(source);
        if (result != 0) {
            return result;
        }
        return m_childFailed ? 123 : 0;
    }

    // The options, in the order given: GNU's own validations and warnings,
    // each message under the option's short name as getopt reports it.
    bool ProcessOptions(const ParsedBuiltinArgs& parsed, Settings& settings) {
        for (const auto& option : parsed.options) {
            switch (option.id) {
                case kOptionNull:
                    settings.nullMode = true;
                    break;
                case kOptionArgFile:
                    settings.argFile = option.argument;
                    break;
                case kOptionDelimiter:
                    if (!ParseDelimiter(option.argument, settings)) {
                        return false;
                    }
                    break;
                case kOptionEofRequired:
                    settings.eofString = option.argument;
                    break;
                case kOptionEofOptional:
                    settings.eofString = option.hasArgument ? option.argument : std::string();
                    break;
                case kOptionReplaceRequired:
                    SetReplace(settings, option.argument);
                    break;
                case kOptionReplaceOptional:
                    SetReplace(settings, option.hasArgument ? option.argument : "{}");
                    break;
                case kOptionMaxLines:
                case kOptionMaxLinesShort: {
                    // -l defaults to one line when its optional argument is
                    // not written; both report under their own short name.
                    long value = 1;
                    if (option.id == kOptionMaxLines || option.hasArgument) {
                        if (!ParseCount(option, option.argument, value, 1, -1)) {
                            return false;
                        }
                    }
                    SetLines(settings, value, option.id == kOptionMaxLinesShort);
                    break;
                }
                case kOptionMaxArgs: {
                    long value = 1;
                    if (!ParseCount(option, option.argument, value, 1, -1)) {
                        return false;
                    }
                    SetArgs(settings, value);
                    break;
                }
                case kOptionOpenTty:
                    settings.openTty = true;
                    break;
                case kOptionMaxProcs: {
                    // Checked as GNU checks it, then not acted on: commands
                    // run one at a time (see --help's notes).
                    long value = 0;
                    if (!ParseCount(option, option.argument, value, 0, 2147483647)) {
                        return false;
                    }
                    break;
                }
                case kOptionInteractive:
                    settings.interactive = true;
                    break;
                case kOptionProcessSlotVar:
                    settings.slotVar = option.argument;
                    break;
                case kOptionNoRunIfEmpty:
                    settings.noRunIfEmpty = true;
                    break;
                case kOptionMaxChars: {
                    // A value below 1 is warned of and taken as 1; the
                    // upper-limit warning waits until the environment's own
                    // bytes are known, so its text is kept here.
                    long value = 1;
                    if (!ParseCount(option, option.argument, value, 1, -1)) {
                        return false;
                    }
                    settings.maxCharsGiven = value;
                    settings.maxCharsText = option.argument;
                    break;
                }
                case kOptionShowLimits:
                    settings.showLimits = true;
                    break;
                case kOptionVerbose:
                    settings.verbose = true;
                    break;
                case kOptionExit:
                    settings.exitIfFull = true;
                    settings.exitExplicit = true;
                    break;
                default:
                    break;
            }
        }
        // -E is silent when -0 or -d takes its place.
        if (!settings.eofString.empty() && (settings.nullMode || settings.delimiterMode)) {
            m_context->Error("warning: the -E option has no effect if -0 or -d is used.");
        }
        return true;
    }

    // -d's argument: one character, or one escape -- with anything after a
    // valid escape ignored, as GNU's own parse does.
    bool ParseDelimiter(const std::string& spec, Settings& settings) {
        settings.delimiterMode = true;
        auto invalid = [&]() {
            m_context->Error("Invalid input delimiter specification " + spec
                + ": the delimiter must be either a single character or an escape sequence "
                "starting with \\.");
            return false;
        };
        if (spec.empty()) {
            return invalid();
        }
        if (spec[0] != '\\') {
            if (spec.size() != 1) {
                return invalid();
            }
            settings.delimiter = spec[0];
            return true;
        }
        // The escapes: \a \b \f \n \r \t \v \\, \xHH, and octal \NNN (\0 among
        // them), each reported by the whole rest of the specification.
        const std::string rest = spec.substr(1);
        if (rest.empty()) {
            return invalid();
        }
        const auto hexValue = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return -1;
        };
        const auto octValue = [](char c) -> int {
            return (c >= '0' && c <= '7') ? c - '0' : -1;
        };
        int value = -1;
        switch (rest[0]) {
            case 'a': value = '\a'; break;
            case 'b': value = '\b'; break;
            case 'f': value = '\f'; break;
            case 'n': value = '\n'; break;
            case 'r': value = '\r'; break;
            case 't': value = '\t'; break;
            case 'v': value = '\v'; break;
            case '\\': value = '\\'; break;
            case 'x': {
                if (rest.size() < 3) {
                    break;
                }
                const int hi = hexValue(rest[1]);
                const int lo = hexValue(rest[2]);
                if (hi >= 0 && lo >= 0) {
                    value = hi * 16 + lo;
                }
                break;
            }
            default: {
                int parsed = 0;
                int digits = 0;
                for (char c : rest) {
                    const int digit = octValue(c);
                    if (digit < 0) {
                        break;
                    }
                    parsed = parsed * 8 + digit;
                    if (++digits == 3) {
                        break;
                    }
                }
                if (digits > 0) {
                    value = parsed;
                }
                break;
            }
        }
        if (value < 0 || value > 255) {
            m_context->Error("Invalid escape sequence \\" + rest
                + " in input delimiter specification.");
            return false;
        }
        settings.delimiter = static_cast<char>(value);
        return true;
    }

    // The option's canonical short name, the one GNU's messages use however
    // the option was spelled.
    static char ShortNameOf(int id) {
        switch (id) {
            case kOptionMaxLines: return 'L';
            case kOptionMaxLinesShort: return 'l';
            case kOptionMaxArgs: return 'n';
            case kOptionMaxProcs: return 'P';
            case kOptionMaxChars: return 's';
            default: return '?';
        }
    }

    // One number option's value, with GNU's own two diagnostics. False when
    // the run is over: an unparsable number, or a counting value out of
    // range. True for -s below 1, its warning printed and the value taken as
    // 1, as GNU carries on to its next complaint.
    bool ParseCount(const ParsedBuiltinOption& option, const std::string& text, long& value,
                   long minimum, long maximum) {
        const char name = ShortNameOf(option.id);
        const bool isMaxChars = option.id == kOptionMaxChars;
        auto invalid = [&]() {
            m_context->Error("invalid number \"" + text + "\" for -" + std::string(1, name)
                + " option");
            m_context->TryHelp();
            return false;
        };
        if (text.empty() || text.find_first_not_of("+-0123456789") != std::string::npos) {
            return invalid();
        }
        char* end = nullptr;
        const long parsed = std::strtol(text.c_str(), &end, 10);
        if (!end || *end != '\0') {
            return invalid();
        }
        if (parsed < minimum) {
            m_context->Error("value " + text + " for -" + std::string(1, name)
                + " option should be >= " + std::to_string(minimum));
            if (!isMaxChars) {
                m_context->TryHelp();
                return false;
            }
            value = minimum;
            return true;
        }
        if (maximum >= 0 && parsed > maximum) {
            m_context->Error("value " + text + " for -" + std::string(1, name)
                + " option should be <= " + std::to_string(maximum));
            m_context->TryHelp();
            return false;
        }
        value = parsed;
        return true;
    }

    // The three mode setters, with GNU's mutual-exclusion warnings: the mode
    // being replaced is named by its long name, the newcomer by its short.
    void SetReplace(Settings& settings, const std::string& replace) {
        if (settings.linesActive) {
            m_context->Error("warning: options --max-lines and --replace/-I/-i are mutually "
                "exclusive, ignoring previous --max-lines value");
            settings.linesActive = false;
        }
        if (settings.argsActive) {
            m_context->Error("warning: options --max-args and --replace/-I/-i are mutually "
                "exclusive, ignoring previous --max-args value");
            settings.argsActive = false;
        }
        settings.replace = replace;
        settings.replaceActive = true;
        settings.mode = ItemMode::Replace;
        // -I implies -x, and keeps implying it even when -n or -L replaces
        // the mode later.
        settings.exitIfFull = true;
    }

    void SetLines(Settings& settings, long value, bool shortSpelling) {
        const std::string spelling = shortSpelling ? "--max-lines/-l" : "-L";
        if (settings.argsActive) {
            m_context->Error("warning: options --max-args and " + spelling + " are mutually "
                "exclusive, ignoring previous --max-args value");
            settings.argsActive = false;
        }
        if (settings.replaceActive) {
            m_context->Error("warning: options --replace and " + spelling + " are mutually "
                "exclusive, ignoring previous --replace value");
            settings.replaceActive = false;
        }
        settings.maxLines = value;
        settings.linesActive = true;
        settings.mode = ItemMode::Lines;
        // An -I that -L replaces took its implied -x with it; one given
        // outright stays.
        settings.exitIfFull = settings.exitExplicit;
    }

    void SetArgs(Settings& settings, long value) {
        if (settings.replaceActive) {
            // -I implies one item per command, so an -n 1 after it says
            // nothing new and GNU drops it without a word; any other count
            // replaces the mode, with the warning.
            if (value == 1) {
                return;
            }
            m_context->Error("warning: options --replace and --max-args/-n are mutually "
                "exclusive, ignoring previous --replace value");
            settings.replaceActive = false;
        } else if (settings.linesActive) {
            m_context->Error("warning: options --max-lines and --max-args/-n are mutually "
                "exclusive, ignoring previous --max-lines value");
            settings.linesActive = false;
        }
        settings.maxArgs = value;
        settings.argsActive = true;
        settings.mode = ItemMode::Args;
        // An -I that -n replaces took its implied -x with it; one given
        // outright stays.
        settings.exitIfFull = settings.exitExplicit;
    }

    // The -I mode: one item per non-blank line (or per -0/-d item), R
    // replaced in every initial argument, one command per item, the empty
    // input running nothing.
    int RunReplace(ByteSource& source) {
        if (m_settings->nullMode || m_settings->delimiterMode) {
            const char delimiter = m_settings->nullMode ? '\0' : m_settings->delimiter;
            std::string item;
            bool delimited = false;
            while (source.NextDelimited(delimiter, item, delimited)) {
                if (m_context->StopRequested()) {
                    return 0;
                }
                const int result = AddReplaceItem(item);
                if (result != 0) {
                    return result;
                }
            }
            return 0;
        }
        QuoteParser parser(/*blanksSeparate=*/false);
        while (true) {
            if (m_context->StopRequested()) {
                return 0;
            }
            std::string line;
            if (!source.NextLine(line)) {
                return 0;
            }
            if (line.find_first_not_of(" \t") == std::string::npos) {
                continue;
            }
            std::vector<std::string> items;
            const ParseStatus status = parser.ParseLine(line, items);
            if (status == ParseStatus::Nul) {
                m_context->Error("WARNING: a NUL character occurred in the input.  It cannot "
                    "be passed through in the argument list.  Did you mean to use the "
                    "--null option?");
                return 0;
            }
            if (!ReportParseStatus(status)) {
                return 1;
            }
            // The whole line is one item, blanks inside included.
            const std::string item = items.empty() ? std::string() : items.front();
            if (!m_settings->eofString.empty() && item == m_settings->eofString) {
                return 0;
            }
            const int result = AddReplaceItem(item);
            if (result != 0) {
                return result;
            }
        }
    }

    // The word modes: -0/-d items, or lines parsed by GNU's grammar, grouped
    // onto command lines by size, by -n's count, or by -L's lines.
    int RunWords(ByteSource& source) {
        if (m_settings->nullMode || m_settings->delimiterMode) {
            // -L counts each item as a line here, as GNU's own reader does;
            // the EOF string has no effect in these modes.
            const char delimiter = m_settings->nullMode ? '\0' : m_settings->delimiter;
            std::string item;
            bool delimited = false;
            while (source.NextDelimited(delimiter, item, delimited)) {
                if (m_context->StopRequested()) {
                    return 0;
                }
                AddItem(item);
                if (m_abort != 0) {
                    return m_abort;
                }
                if (m_settings->mode == ItemMode::Lines
                    && static_cast<long>(m_items.size()) >= m_settings->maxLines) {
                    Flush();
                    if (m_abort != 0) {
                        return m_abort;
                    }
                }
            }
            return FinalFlush();
        }

        QuoteParser parser(/*blanksSeparate=*/true);
        long lineCount = 0;
        while (true) {
            if (m_context->StopRequested()) {
                return 0;
            }
            std::string physical;
            if (!source.NextLine(physical)) {
                return FinalFlush();
            }
            if (physical.empty()) {
                // An empty line separates; in -L mode it is not blank input
                // either, so it counts as nothing.
                continue;
            }
            std::string logical = physical;
            // -L continues a line that ends in a blank -- the lines it pulls
            // in continue it in turn, blank lines among them.
            while (m_settings->mode == ItemMode::Lines
                && (logical.back() == ' ' || logical.back() == '\t')) {
                std::string next;
                if (!source.NextLine(next)) {
                    break;
                }
                logical += next;
            }
            if (logical.find_first_not_of(" \t") == std::string::npos) {
                continue;
            }
            std::vector<std::string> items;
            const ParseStatus status = parser.ParseLine(logical, items);
            if (status == ParseStatus::Nul) {
                m_context->Error("WARNING: a NUL character occurred in the input.  It cannot "
                    "be passed through in the argument list.  Did you mean to use the "
                    "--null option?");
                AddParsedItems(items);
                if (m_abort != 0) {
                    return m_abort;
                }
                return FinalFlush();
            }
            if (!ReportParseStatus(status)) {
                // The items the line had completed still run, then the run is
                // over -- GNU's error comes after them.
                AddParsedItems(items);
                Flush();
                return m_abort != 0 ? m_abort : 1;
            }
            if (!AddParsedItems(items)) {
                // Either the EOF string ended the input, or an item would not
                // fit; the line's items up to either point are on the line.
                return m_abort != 0 ? m_abort : FinalFlush();
            }
            if (m_settings->mode == ItemMode::Lines) {
                if (++lineCount >= m_settings->maxLines) {
                    Flush();
                    if (m_abort != 0) {
                        return m_abort;
                    }
                    lineCount = 0;
                }
            }
        }
    }

    // Adds a parsed line's items until the EOF string ends the input. False
    // when the input ended there.
    bool AddParsedItems(std::vector<std::string>& items) {
        for (std::string& item : items) {
            if (!m_settings->eofString.empty() && item == m_settings->eofString) {
                return false;
            }
            AddItem(item);
            if (m_abort != 0) {
                return false;
            }
        }
        return true;
    }

    // GNU's messages for a line's parse going wrong. A NUL is reported by
    // the caller, which knows the input ends there.
    bool ReportParseStatus(ParseStatus status) {
        switch (status) {
            case ParseStatus::Ok:
                return true;
            case ParseStatus::UnmatchedSingle:
                m_context->Error("unmatched single quote; by default quotes are special to "
                    "xargs unless you use the -0 option");
                return false;
            case ParseStatus::UnmatchedDouble:
                m_context->Error("unmatched double quote; by default quotes are special to "
                    "xargs unless you use the -0 option");
                return false;
            default:
                return true;
        }
    }

    // One item onto the line being built, with GNU's own ways of not
    // fitting. An item that cannot stand on a command line even alone is
    // "argument line too long" -- what is already on the line still runs,
    // except in -L mode, whose lines cannot be split at all. An item that
    // fits alone but not with what is on the line splits the line, unless
    // the batch cannot be split: a -L line never, an -n batch under -x --
    // and then it is "argument list too long", with nothing running.
    void AddItem(const std::string& item) {
        const long itemSize = static_cast<long>(item.size()) + 1;
        const long freshSize = m_baseSize + itemSize;
        if (freshSize > m_maxChars) {
            m_context->Error("argument line too long");
            if (m_settings->mode != ItemMode::Lines) {
                Flush();
            }
            m_abort = 1;
            return;
        }
        if (!m_items.empty() && m_lineSize + itemSize > m_maxChars) {
            if (m_settings->mode == ItemMode::Lines
                || (m_settings->mode == ItemMode::Args && m_settings->exitIfFull)) {
                m_context->Error("argument list too long");
                m_abort = 1;
                return;
            }
            Flush();
        }
        m_items.push_back(item);
        m_lineSize += itemSize;
        m_everHadItems = true;
        if (m_settings->mode == ItemMode::Args
            && static_cast<long>(m_items.size()) >= m_settings->maxArgs) {
            Flush();
        }
    }

    // GNU's -I ladder, in its own order: the item, the command, the longest
    // replaced argument, then the line as it will run.
    int AddReplaceItem(const std::string& item) {
        const long itemSize = static_cast<long>(item.size()) + 1;
        if (itemSize > m_maxChars) {
            m_context->Error("argument line too long");
            return 1;
        }
        if (static_cast<long>(m_command.size()) + 1 > m_maxChars) {
            m_context->Error("cannot fit single argument within argument list size limit");
            return 1;
        }
        std::vector<std::string> replaced;
        long longest = 0;
        long total = static_cast<long>(m_command.size()) + 1;
        for (const auto& arg : m_initialArgs) {
            std::string result;
            size_t at = 0;
            while (true) {
                const size_t found = arg.find(m_settings->replace, at);
                if (found == std::string::npos) {
                    result += arg.substr(at);
                    break;
                }
                result += arg.substr(at, found - at);
                result += item;
                at = found + m_settings->replace.size();
            }
            longest = std::max(longest, static_cast<long>(result.size()));
            total += static_cast<long>(result.size()) + 1;
            replaced.push_back(std::move(result));
        }
        if (longest + 2 > m_maxChars) {
            m_context->Error("command too long");
            return 1;
        }
        if (total > m_maxChars) {
            m_context->Error("argument list too long");
            return 1;
        }
        return RunLine(replaced);
    }

    // The end of the input: the line being built runs, or the bare command
    // runs once -- unless -r says not to, or -I already ran everything.
    int FinalFlush() {
        if (!m_items.empty()) {
            Flush();
        } else if (!m_everHadItems && !m_settings->noRunIfEmpty) {
            // Nothing was ever read: the command runs once on its own
            // arguments alone, as GNU's does even of an empty input.
            RunLine(m_initialArgs);
        }
        return m_abort;
    }

    // Runs the line being built; with nothing on it, nothing happens.
    void Flush() {
        if (m_items.empty()) {
            return;
        }
        std::vector<std::string> args = m_initialArgs;
        args.insert(args.end(), m_items.begin(), m_items.end());
        RunLine(args);
        m_items.clear();
        m_lineSize = m_baseSize;
    }

    // Runs one command line: the prompt or the trace line, the child's input
    // and environment, and the status GNU makes of each way a child ends.
    // Returns 0 to carry on, or the exit status the run is over with.
    int RunLine(const std::vector<std::string>& args) {
        BuiltinContext& context = *m_context;
        std::string line = ShellQuoteArg(m_command);
        for (const auto& arg : args) {
            line += " ";
            line += ShellQuoteArg(arg);
        }
        // Haisos has no /dev/tty: the terminal is standard input, when it is one.
        const auto stdIn = context.IO().GetDescriptor(IFileIO::kStdIn);
        const bool stdInIsTerminal = stdIn && stdIn->IsTerminal();
        if (m_settings->interactive) {
            // GNU prints the line, then asks on the terminal; without one,
            // the failure GNU prints.
            if (!stdInIsTerminal) {
                context.ErrorText(line);
                context.Error("failed to open /dev/tty for reading: "
                    "No such device or address");
                m_abort = 1;
                return 1;
            }
            if (!m_prompt) {
                m_prompt.emplace(context);
            }
            if (!m_prompt->Ask(line + " ?...")) {
                return 0;
            }
        } else if (m_settings->verbose) {
            context.ErrorText(line + "\n");
        }

        std::shared_ptr<IEnvironment> childEnvironment;
        if (!m_settings->slotVar.empty()) {
            // A clone of the process's own, so the slot number never leaks
            // back; RunProgramAndWait would clone anyway when null.
            childEnvironment = context.Process().GetEnvironment();
            // One child at a time: GNU reuses the freed slot, so always 0.
            childEnvironment->SetVariable(m_settings->slotVar, "0");
        }

        std::shared_ptr<IFileDescriptor> childInput;
        if (m_settings->openTty) {
            childInput = stdInIsTerminal ? stdIn : nullptr;
            if (!childInput) {
                context.Error(GnuQuote("/dev/tty") + ": No such device or address");
                m_abort = 1;
                return 1;
            }
        } else {
            childInput = OpenEmptyInput(context);
            if (!childInput) {
                context.Error("cannot make an empty input for " + GnuQuote(m_command));
                m_abort = 1;
                return 1;
            }
        }

        const auto program = FindProgramInPath(context, m_command, childEnvironment.get());
        if (!program) {
            context.Error(m_command + ": No such file or directory");
            m_abort = 127;
            return 127;
        }
        RunProgramOptions options;
        options.stdIn = childInput;
        options.environment = childEnvironment;
        bool started = true;
        const int code = RunProgramAndWait(context, *program, args, options, &started);
        if (context.StopRequested()) {
            // The stop is the process's own exit, not the child's.
            return 0;
        }
        if (!started) {
            context.Error(m_command + ": Permission denied");
            m_abort = 126;
            return 126;
        }
        if (code == 255) {
            context.Error(m_command + ": exited with status 255; aborting");
            m_abort = 124;
            return 124;
        }
        if (code >= 129 && code <= 254) {
            context.Error(m_command + ": terminated by signal " + std::to_string(code - 128));
            m_abort = 125;
            return 125;
        }
        if (code != 0) {
            // Any other failure, a child's own 126 or 127 among them, is
            // remembered: the run ends in GNU's 123.
            m_childFailed = true;
        }
        return 0;
    }

    BuiltinContext* m_context = nullptr;
    const Settings* m_settings = nullptr;
    std::string m_command;
    std::vector<std::string> m_initialArgs;
    long m_maxChars = 0;
    long m_baseSize = 0;
    long m_lineSize = 0;
    std::vector<std::string> m_items;
    bool m_everHadItems = false;
    int m_abort = 0;
    bool m_childFailed = false;
    std::optional<BuiltinPrompt> m_prompt; // -p's, over standard input
};

} // namespace

std::shared_ptr<IBuiltinCommand> CreateXargsCommand() {
    return std::make_shared<XargsCommand>();
}

} // namespace Haisos