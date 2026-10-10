#include "SedExecutor.h"

#include <memory>
#include <utility>

#include "BuiltinText.h"
#include "interfaces/IFileIO.h"

namespace Haisos::Sed {
namespace {

// Where one stream's bytes go: standard output, standard error, or a file
// (an -i temp file among them). The streams a script writes to each keep
// their own pending-delimiter state, which the writers below take by
// reference (the main one lives here, a w/W file's in its OutputFile).
class OutStream {
public:
    OutStream() = default;
    explicit OutStream(BuiltinContext& context) : m_context(&context) {}
    OutStream(BuiltinContext& context, bool toStderr)
        : m_context(&context), m_stderr(toStderr)
    {
    }
    explicit OutStream(std::shared_ptr<IFileDescriptor> file) : m_file(std::move(file)) {}

    void Write(const std::string& data) const
    {
        if (data.empty()) {
            return;
        }
        if (m_file) {
            WriteFully(*m_file, data);
        } else if (m_stderr) {
            m_context->ErrorText(data);
        } else {
            m_context->Out(data);
        }
    }

private:
    BuiltinContext* m_context = nullptr;
    bool m_stderr = false;
    std::shared_ptr<IFileDescriptor> m_file;
};

// The delimiter a previous undelimited record is still owed, paid before
// anything else is written to the stream.
void PayPending(const OutStream& stream, bool& missing, char d)
{
    if (missing) {
        stream.Write(std::string(1, d));
        missing = false;
    }
}

// A record (the pattern space): paid what it is owed, ended with the
// delimiter only when |delimited|, and remembering whether it was.
void WriteRecord(const OutStream& stream, bool& missing, const std::string& text, bool delimited, char d)
{
    std::string out;
    if (missing) {
        out += d;
    }
    out += text;
    if (delimited) {
        out += d;
    }
    stream.Write(out);
    missing = !delimited;
}

// A line that is always complete: what '=', i, c, l and F write.
void WriteLine(const OutStream& stream, bool& missing, const std::string& text, char d)
{
    std::string out;
    if (missing) {
        out += d;
    }
    out += text;
    out += d;
    stream.Write(out);
    missing = false;
}

// Through the first record delimiter only, inclusive (P and W); with none,
// the whole pattern space as WriteRecord takes it.
void WriteHeadRecord(const OutStream& stream, bool& missing, const std::string& text, bool delimited, char d)
{
    const size_t pos = text.find(d);
    if (pos == std::string::npos) {
        WriteRecord(stream, missing, text, delimited, d);
        return;
    }
    std::string out;
    if (missing) {
        out += d;
    }
    out.append(text, 0, pos + 1);
    stream.Write(out);
    missing = false;
}

// The stream of a w/W target.
OutStream OutputTarget(BuiltinContext& context, const OutputFile& out)
{
    if (out.isStdout) {
        return OutStream(context);
    }
    if (out.isStderr) {
        return OutStream(context, true);
    }
    return OutStream(out.file);
}

// The pattern space as l prints it: the escapes GNU's, wrapped every |wrap|
// characters (0 never), ended with a '$'.
std::string FormatList(const std::string& text, int wrap)
{
    std::string out;
    int column = 0;
    auto put = [&](const std::string& unit) {
        if (wrap > 0 && column >= wrap - 1) {
            out += '\\';
            out += '\n';
            column = 0;
        }
        out += unit;
        column += static_cast<int>(unit.size());
    };
    for (const unsigned char c : text) {
        switch (c) {
            case '\a': put("\\a"); break;
            case '\b': put("\\b"); break;
            case '\f': put("\\f"); break;
            case '\n': put("\\n"); break;
            case '\r': put("\\r"); break;
            case '\t': put("\\t"); break;
            case '\v': put("\\v"); break;
            case '\\': put("\\\\"); break;
            default: {
                if (c < 0x20 || c >= 0x7F) {
                    const char oct[5] = {'\\',
                                         static_cast<char>('0' + ((c >> 6) & 7)),
                                         static_cast<char>('0' + ((c >> 3) & 7)),
                                         static_cast<char>('0' + (c & 7))};
                    put(std::string(oct, 4));
                } else {
                    put(std::string(1, static_cast<char>(c)));
                }
            }
        }
    }
    out += '$';
    return out;
}

// The inputs, one read at a time, with the one-line lookahead the '$' address
// needs. A pattern-space read reports what GNU reports; the lookahead is
// quieter (as GNU's is): a file that cannot be opened is reported and costs
// status 2, a read error ends the input silently, and the status it would
// cost is settled when the pattern space reaches the same end.
class SedInput {
public:
    SedInput(BuiltinContext& context, std::vector<std::string> inputs, bool separate, char delimiter)
        : m_context(context), m_inputs(std::move(inputs)), m_separate(separate), m_delimiter(delimiter)
    {
    }

    enum class ReadResult { Line, End, Error, Stopped };

    // The next pattern space; |newFile| tells the caller the line came from a
    // newly opened file (with -s, a fresh stream).
    ReadResult Read(std::string& line, bool& delimited, bool& newFile)
    {
        newFile = false;
        if (m_silentEnd) {
            return ReadResult::End;
        }
        if (m_peeked) {
            TakePeek(line, delimited);
            return ReadResult::Line;
        }
        while (true) {
            if (!m_input) {
                ReadResult opened = OpenNext(false, newFile);
                if (opened != ReadResult::Line) {
                    return opened;
                }
            }
            const LineReadResult r = m_reader->Next(line, delimited);
            if (r == LineReadResult::Line) {
                ++m_lineNumber;
                m_lastKnown = 0;
                return ReadResult::Line;
            }
            if (r == LineReadResult::Stopped) {
                return ReadResult::Stopped;
            }
            if (r == LineReadResult::Error) {
                m_context.Error("read error on " + m_name + ": Input/output error");
                m_fatal = true;
                return ReadResult::Error;
            }
            m_input.reset();  // End: this file is finished
            m_reader.reset();
            m_peeked = false;  // it holds none: the lookahead never crosses a file it did not open
        }
    }

    // Whether the pattern space just read (or, mid-cycle, the one in hand) is
    // the last line: the lookahead GNU takes, one line ahead, per evaluation,
    // cached per line. With -s it never crosses into the next file.
    bool IsLastLine()
    {
        if (m_lastKnown != 0) {
            return m_lastKnown == 1;
        }
        if (m_peeked) {
            m_lastKnown = 2;
            return false;
        }
        if (m_silentEnd) {
            m_lastKnown = 1;
            return true;
        }
        bool newFile = false;
        while (true) {
            if (!m_input) {
                if (m_separate || m_next >= m_inputs.size()) {
                    m_lastKnown = 1;  // with -s (or nothing left): this is the last line
                    return true;
                }
                const ReadResult r = OpenNext(true, newFile);
                if (r == ReadResult::End) {
                    m_lastKnown = 1;
                    return true;
                }
                if (r == ReadResult::Error) {  // a read error in the lookahead: silent, as GNU's
                    m_silentEnd = true;
                    m_lastKnown = 1;
                    return true;
                }
            }
            std::string line;
            bool delimited = false;
            const LineReadResult r = m_reader->Next(line, delimited);
            if (r == LineReadResult::Line) {
                m_peeked = true;
                m_peekedLine = line;
                m_peekedDelimited = delimited;
                m_lastKnown = 2;
                return false;
            }
            if (r == LineReadResult::Error) {
                m_silentEnd = true;
                m_lastKnown = 1;
                return true;
            }
            if (r == LineReadResult::Stopped) {  // a stop will end the run anyway
                m_lastKnown = 2;
                return false;
            }
            m_input.reset();
            m_reader.reset();
        }
    }

    uint64_t LineNumber() const { return m_lineNumber; }
    // The name of the input the lines come from, as given: F's output.
    const std::string& Name() const { return m_name; }

    // The status of an input that could not be opened, once it happens.
    int ErrorStatus() const { return m_errorStatus; }
    // A read error (a directory, as Haisos takes it): everything stops.
    bool Fatal() const { return m_fatal; }

private:
    // Opens the next input, skipping the ones that cannot be read, reporting
    // each as GNU does (cost 2). A directory is different: reached by the
    // pattern-space read it stops everything (GNU's read error, exit 4);
    // met by the lookahead it is passed over in silence, and so is anything
    // else the pattern space would have reported. Returns Line when one is
    // open (the caller reads it), End when none are left.
    ReadResult OpenNext(bool peeking, bool& newFile)
    {
        while (m_next < m_inputs.size()) {
            const std::string name = m_inputs[m_next++];
            InputOpenFailure failure = InputOpenFailure::None;
            auto input = OpenInputOperand(m_context, name, failure);
            if (!input) {
                if (failure == InputOpenFailure::Directory) {
                    if (peeking) {
                        continue;  // the lookahead passes a directory in silence
                    }
                    m_context.Error("read error on " + name + ": Is a directory");
                    m_fatal = true;
                    return ReadResult::Error;
                }
                std::string reason = "No such file or directory";
                if (failure == InputOpenFailure::Denied) {
                    reason = "Permission denied";
                } else if (failure == InputOpenFailure::BadDescriptor) {
                    reason = "Bad file descriptor";
                }
                m_context.Error("can't read " + name + ": " + reason);
                m_errorStatus = 2;
                continue;
            }
            m_input = std::move(input);
            m_name = name;
            m_reader = std::make_unique<BuiltinLineReader>(m_context, *m_input, m_delimiter);
            if (m_separate) {  // with -s each file's lines are numbered anew
                m_lineNumber = 0;
            }
            m_lastKnown = 0;
            newFile = true;
            return ReadResult::Line;
        }
        return ReadResult::End;
    }

    void TakePeek(std::string& line, bool& delimited)
    {
        line = m_peekedLine;
        delimited = m_peekedDelimited;
        m_peeked = false;
        ++m_lineNumber;
        m_lastKnown = 0;
    }

    BuiltinContext& m_context;
    std::vector<std::string> m_inputs;
    bool m_separate = false;
    char m_delimiter = '\n';
    size_t m_next = 0;
    std::shared_ptr<IFileDescriptor> m_input;
    std::unique_ptr<BuiltinLineReader> m_reader;
    std::string m_name;
    bool m_peeked = false;
    std::string m_peekedLine;
    bool m_peekedDelimited = false;
    uint64_t m_lineNumber = 0;  // across the files, or of the current one with -s
    int m_lastKnown = 0;        // 0 unknown, 1 last, 2 not
    int m_errorStatus = 0;
    bool m_fatal = false;
    bool m_silentEnd = false;   // the input ended in the lookahead: say no more
};

bool RegexMatches(const Address& address, const std::string& text, std::shared_ptr<const Regex>& lastRegex,
                 bool& missingRegex)
{
    const Regex* regex = address.regex.get();
    if (!regex) {
        regex = lastRegex.get();
        if (!regex) {
            missingRegex = true;
            return false;
        }
    } else {
        lastRegex = address.regex;
    }
    RegexMatch match;
    return regex->Search(text, 0, match);
}

bool MatchOne(const Address& address, SedInput& input, const std::string& patternSpace,
              std::shared_ptr<const Regex>& lastRegex, bool& missingRegex)
{
    switch (address.kind) {
        case AddressKind::Line:
            return input.LineNumber() == address.line;
        case AddressKind::Last:
            return input.IsLastLine();
        case AddressKind::Step: {
            const uint64_t n = input.LineNumber();
            if (n < address.line) {
                return false;
            }
            if (address.step == 0) {
                return n == address.line;
            }
            return (n - address.line) % address.step == 0;
        }
        case AddressKind::Regex:
            return RegexMatches(address, patternSpace, lastRegex, missingRegex);
        case AddressKind::None:
        default:
            return true;
    }
}

// |rangeEnded| tells the caller a two-address range ended on this line (what
// c waits for before it writes its text).
bool MatchAddress(Command& command, SedInput& input, const std::string& patternSpace,
                  std::shared_ptr<const Regex>& lastRegex, bool& missingRegex, bool& rangeEnded)
{
    bool matched = false;
    if (command.a1.kind == AddressKind::None && command.a2.kind == AddressKind::None) {
        matched = true;
    } else if (command.a1.kind == AddressKind::Zero) {
        // 0,/re/: the range is open from the first line on, and the regex
        // (checked on every line, the first included) ends it.
        if (!command.rangeEnded) {
            matched = true;
            if (RegexMatches(command.a2, patternSpace, lastRegex, missingRegex)) {
                command.rangeEnded = true;
                rangeEnded = true;
            }
        }
    } else if (command.a2.kind != AddressKind::None) {
        if (!command.rangeActive) {
            matched = MatchOne(command.a1, input, patternSpace, lastRegex, missingRegex);
            if (matched) {
                command.rangeStart = input.LineNumber();
                // A second address already reached (a line number at or
                // before this line, +0, ~0) ends the range on its first
                // line, as one line; a ~N with N > 0 ends at the next
                // multiple strictly after it, never on the line itself.
                const bool oneLine =
                    (command.a2.kind == AddressKind::Line && command.a2.line <= input.LineNumber()) ||
                    (command.a2.kind == AddressKind::RelativeLines && command.a2.line == 0) ||
                    (command.a2.kind == AddressKind::Multiple && command.a2.line == 0);
                command.rangeActive = !oneLine;
                if (oneLine) {
                    rangeEnded = true;
                }
            }
        } else {
            matched = true;
            bool end = false;
            if (input.LineNumber() > command.rangeStart) {
                switch (command.a2.kind) {
                    case AddressKind::Line:
                        end = input.LineNumber() >= command.a2.line;
                        break;
                    case AddressKind::Last:
                        end = input.IsLastLine();
                        break;
                    case AddressKind::Regex:
                        end = RegexMatches(command.a2, patternSpace, lastRegex, missingRegex);
                        break;
                    case AddressKind::RelativeLines:
                        end = input.LineNumber() >= command.rangeStart + command.a2.line;
                        break;
                    case AddressKind::Multiple:
                        // ~0 cannot reach here (the range ended at its
                        // start), but keep the division safe.
                        end = command.a2.line == 0 || input.LineNumber() % command.a2.line == 0;
                        break;
                    default:
                        break;
                }
            }
            if (end) {
                // The range is over: a1 is matched again from the next line
                // on, so a later one may open a range of its own. Only
                // 0,/re/ (rangeEnded) never restarts.
                command.rangeActive = false;
                rangeEnded = true;
            }
        }
    } else {
        matched = MatchOne(command.a1, input, patternSpace, lastRegex, missingRegex);
    }
    return command.negate ? !matched : matched;
}

// The right-hand side of s, as pieces: literal text, group references and
// the case commands \L \U \l \u \E.
std::string BuildReplacement(const Replacement& replacement, const std::string& text, const RegexMatch& match)
{
    std::string out;
    int restCase = 0;  // 0 none, 1 upper, 2 lower
    int nextCase = 0;  // for the next character written
    auto addText = [&](const std::string& s) {
        for (char c : s) {
            if (nextCase != 0) {
                if (nextCase == 1 && c >= 'a' && c <= 'z') {
                    c = static_cast<char>(c - 'a' + 'A');
                } else if (nextCase == 2 && c >= 'A' && c <= 'Z') {
                    c = static_cast<char>(c - 'A' + 'a');
                }
                nextCase = 0;
            } else if (restCase == 1 && c >= 'a' && c <= 'z') {
                c = static_cast<char>(c - 'a' + 'A');
            } else if (restCase == 2 && c >= 'A' && c <= 'Z') {
                c = static_cast<char>(c - 'A' + 'a');
            }
            out += c;
        }
    };
    for (const auto& piece : replacement.pieces) {
        switch (piece.kind) {
            case ReplacementPiece::Kind::Text:
                addText(piece.text);
                break;
            case ReplacementPiece::Kind::Group: {
                if (piece.group == 0) {
                    addText(text.substr(match.groups[0].first,
                                        match.groups[0].second - match.groups[0].first));
                } else if (piece.group < static_cast<int>(match.groups.size()) &&
                           match.groups[piece.group].first >= 0) {
                    addText(text.substr(match.groups[piece.group].first,
                                        match.groups[piece.group].second - match.groups[piece.group].first));
                }
                break;
            }
            case ReplacementPiece::Kind::UpperRest:
                restCase = 1;
                break;
            case ReplacementPiece::Kind::LowerRest:
                restCase = 2;
                break;
            case ReplacementPiece::Kind::UpperNext:
                nextCase = 1;
                break;
            case ReplacementPiece::Kind::LowerNext:
                nextCase = 2;
                break;
            case ReplacementPiece::Kind::EndCase:
                restCase = 0;
                break;
        }
    }
    return out;
}

// GNU's do_sub: replace occurrence |command.occurrence|, and every later one
// with g -- of the matches the regex finds one by one, with an empty match
// where the previous one ended passed over (one byte copied, the search moved
// on) and an empty match at the end of the line allowed once. Returns whether
// anything was replaced (the p flag prints only then, as GNU's does).
bool Substitute(Command& command, std::string& patternSpace, std::shared_ptr<const Regex>& lastRegex,
                bool& missingRegex, bool& replaced)
{
    const Regex* regex = command.regex.get();
    if (!regex) {
        regex = lastRegex.get();
        if (!regex) {
            missingRegex = true;
            return false;
        }
    } else {
        lastRegex = command.regex;
    }
    RegexMatch match;
    if (!regex->Search(patternSpace, 0, match)) {
        return false;
    }
    std::string result;
    size_t pos = 0;
    size_t prevEnd = 0;
    bool havePrev = false;
    uint64_t count = 0;
    while (true) {
        const size_t start = match.groups[0].first;
        const size_t end = match.groups[0].second;
        if (start == end && havePrev && start == prevEnd) {
            // an empty match right where the previous one ended: skip it
            if (start >= patternSpace.size()) {
                break;
            }
            result += patternSpace[start];
            pos = start + 1;
        } else {
            ++count;
            if (count == command.occurrence || (command.global && count >= command.occurrence)) {
                result.append(patternSpace, pos, start - pos);
                result += BuildReplacement(*command.replacement, patternSpace, match);
                replaced = true;
                if (!command.global) {
                    pos = end;
                    break;
                }
            } else {
                result.append(patternSpace, pos, end - pos);
            }
            prevEnd = end;
            havePrev = true;
            pos = end;
            if (start == end) {
                if (pos >= patternSpace.size()) {
                    break;
                }
                result += patternSpace[pos];
                ++pos;
            }
        }
        if (!regex->Search(patternSpace, pos, match, pos > 0 ? kRegexNotBol : 0)) {
            break;
        }
    }
    result.append(patternSpace, pos, patternSpace.size() - pos);
    patternSpace = std::move(result);
    return true;
}

// The ranges' state is per file with -s, and per run: the caller resets it.
void ResetRanges(Script& script)
{
    for (auto& command : script.commands) {
        command.rangeActive = false;
        command.rangeEnded = false;
        command.rangeStart = 0;
    }
}

// One item of the append queue (a and r build it as they run; R is resolved
// when the queue is flushed, reading its own next line).
struct QueueItem {
    char kind = 'a';       // 'a' text, 'r' a file, 'R' one line of a file
    std::string text;     // 'a': the text; 'r': the file name
    size_t command = 0;   // 'R': the command's index
};

// The file R reads from, opened the first time the queue is flushed with one
// of its lines queued, and kept open for the rest of the run.
struct RReader {
    bool tried = false;
    std::shared_ptr<IFileDescriptor> file;
    std::unique_ptr<BuiltinLineReader> reader;
};

// r's file, read whole when the queue is flushed. A missing file is silent,
// as GNU's; a directory is GNU's read error, which stops the run (|fatal|).
bool ReadAppendFile(BuiltinContext& context, const std::string& name, std::string& contents, bool& fatal)
{
    if (name == "/dev/stdin") {
        auto input = context.IO().GetDescriptor(IFileIO::kStdIn);
        return input ? ReadWholeDescriptor(*input, contents) : false;
    }
    InputOpenFailure failure = InputOpenFailure::None;
    auto input = OpenInputOperand(context, name, failure);
    if (!input) {
        if (failure == InputOpenFailure::Directory) {
            context.Error("read error on " + name + ": Is a directory");
            fatal = true;
        }
        return false;
    }
    return ReadWholeDescriptor(*input, contents);
}

// The queue, flushed before every read and after q's print. Every item is
// paid what the main stream is owed first, then written whole. Returns false
// when an r file was a directory (GNU's read error: the run ends at once;
// an R file is opened by the command itself, so it never fails here).
bool FlushQueue(BuiltinContext& context, std::vector<QueueItem>& queue,
                std::vector<RReader>& rReaders, const OutStream& main, bool& mainMissing, char d)
{
    for (const auto& item : queue) {
        switch (item.kind) {
            case 'a':
                PayPending(main, mainMissing, d);
                main.Write(item.text + '\n');  // a literal newline, even with -z
                mainMissing = false;
                break;
            case 'r': {
                std::string contents;
                bool fatal = false;
                if (!ReadAppendFile(context, item.text, contents, fatal)) {
                    return !fatal;
                }
                PayPending(main, mainMissing, d);
                main.Write(contents);
                mainMissing = false;
                break;
            }
            case 'R': {
                RReader& state = rReaders[item.command];
                if (!state.reader) {
                    break;  // a file that never could be opened: silent, as GNU's
                }
                std::string line;
                bool delimited = false;
                if (state.reader->Next(line, delimited) == LineReadResult::Line) {
                    PayPending(main, mainMissing, d);
                    std::string out = line;
                    if (delimited) {
                        out += d;
                    }
                    main.Write(out);
                    mainMissing = !delimited;
                }
                break;
            }
        }
    }
    queue.clear();
    return true;
}

} // namespace

int RunScript(BuiltinContext& context, Script& script, const std::vector<std::string>& inputs,
              const ExecSettings& settings)
{
    ResetRanges(script);  // each call is one run (-i runs the files one by one)
    const char d = settings.delimiter;
    SedInput input(context, inputs, settings.separate, d);
    OutStream main(context);
    if (settings.inPlaceOutput) {
        main = OutStream(settings.inPlaceOutput);
    }
    bool mainMissing = false;
    std::string holdSpace;
    bool holdDelimited = true;  // the hold space's own delimiter state
    std::vector<QueueItem> queue;
    std::vector<RReader> rReaders(script.commands.size());
    bool tFlag = false;
    const bool quiet = settings.quiet || script.quietFromScript;
    std::shared_ptr<const Regex> lastRegex;
    int quitCode = -1;
    bool missingRegex = false;
    std::string patternSpace;
    bool delimited = false;
    bool stopAll = false;    // N at the end of the input: print, flush, stop
    bool readAgain = true;   // D restarts a cycle without reading
    bool appendFatal = false;  // r/R's file was a directory: the run ends, exit 4
    while (!stopAll) {
        if (readAgain) {
            if (!FlushQueue(context, queue, rReaders, main, mainMissing, d)) {
                appendFatal = true;
                break;
            }
            bool newFile = false;
            const auto r = input.Read(patternSpace, delimited, newFile);
            if (r == SedInput::ReadResult::End || r == SedInput::ReadResult::Error ||
                r == SedInput::ReadResult::Stopped) {
                break;
            }
            tFlag = false;  // the t flag is cleared by every line read
            if (newFile && settings.separate) {
                ResetRanges(script);
            }
        }
        readAgain = true;
        bool cycleEnd = false;  // the pattern space is discarded, not printed
        bool restart = false;   // D: run the script again without reading
        size_t i = 0;
        while (i < script.commands.size()) {
            Command& command = script.commands[i];
            bool rangeEnded = false;
            const bool matches =
                MatchAddress(command, input, patternSpace, lastRegex, missingRegex, rangeEnded);
            if (missingRegex) {
                break;
            }
            if (!matches) {
                if (command.name == '{') {
                    i = command.jump - 1;
                }
                ++i;
                continue;
            }
            switch (command.name) {
                case '{':
                case '}':
                case ':':
                case 'v':   // checked against this sed when parsed
                case 'e':   // not treated by HaisosOS, reported when parsed
                    break;
                case 'p':
                    WriteRecord(main, mainMissing, patternSpace, delimited, d);
                    break;
                case 'P':
                    WriteHeadRecord(main, mainMissing, patternSpace, delimited, d);
                    break;
                case 'd':
                    cycleEnd = true;
                    break;
                case 'D': {
                    const size_t pos = patternSpace.find(d);
                    if (pos == std::string::npos) {
                        cycleEnd = true;  // no delimiter: as d
                    } else {
                        patternSpace.erase(0, pos + 1);
                        restart = true;  // from the top, without reading
                    }
                    break;
                }
                case 'n': {
                    if (!quiet) {
                        WriteRecord(main, mainMissing, patternSpace, delimited, d);
                    }
                    if (!FlushQueue(context, queue, rReaders, main, mainMissing, d)) {
                        appendFatal = true;
                        stopAll = true;
                        break;
                    }
                    bool nNewFile = false;
                    std::string line;
                    bool nDelimited = false;
                    const auto nr = input.Read(line, nDelimited, nNewFile);
                    if (nr != SedInput::ReadResult::Line) {
                        cycleEnd = true;  // the end: nothing more runs, nothing is printed again
                        break;
                    }
                    if (nNewFile && settings.separate) {
                        ResetRanges(script);
                    }
                    patternSpace = std::move(line);
                    delimited = nDelimited;
                    tFlag = false;
                    break;
                }
                case 'N': {
                    if (!FlushQueue(context, queue, rReaders, main, mainMissing, d)) {
                        appendFatal = true;
                        stopAll = true;
                        break;
                    }
                    bool nNewFile = false;
                    std::string line;
                    bool nDelimited = false;
                    const auto nr = input.Read(line, nDelimited, nNewFile);
                    if (nr != SedInput::ReadResult::Line) {
                        // GNU prints the pattern space (unless -n), flushes
                        // the queue and stops the whole run.
                        if (!quiet) {
                            WriteRecord(main, mainMissing, patternSpace, delimited, d);
                        }
                        FlushQueue(context, queue, rReaders, main, mainMissing, d);
                        stopAll = true;
                        break;
                    }
                    if (nNewFile && settings.separate) {
                        ResetRanges(script);
                    }
                    patternSpace += d;
                    patternSpace += line;
                    delimited = nDelimited;
                    tFlag = false;
                    break;
                }
                case 'q':
                    PayPending(main, mainMissing, d);
                    if (!quiet) {
                        main.Write(patternSpace + std::string(1, d));
                    }
                    if (!FlushQueue(context, queue, rReaders, main, mainMissing, d)) {
                        appendFatal = true;  // GNU's read error wins over q's code
                    }
                    quitCode = command.intArg;
                    cycleEnd = true;
                    break;
                case 'Q':
                    quitCode = command.intArg;
                    cycleEnd = true;
                    break;
                case '=':
                    WriteLine(main, mainMissing, std::to_string(input.LineNumber()), d);
                    break;
                case 'F':
                    WriteLine(main, mainMissing, input.Name(), d);
                    break;
                case 'i':
                    if (!command.noText) {
                        WriteLine(main, mainMissing, command.text, d);
                    }
                    break;
                case 'c':
                    // With a range, only when it ends on this line.
                    if (!command.noText &&
                        (command.a2.kind == AddressKind::None || command.negate || rangeEnded)) {
                        WriteLine(main, mainMissing, command.text, d);
                    }
                    cycleEnd = true;  // the pattern space is discarded either way
                    break;
                case 'a':
                    if (!command.noText) {
                        queue.push_back(QueueItem{'a', command.text, 0});
                    }
                    break;
                case 'r':
                    queue.push_back(QueueItem{'r', command.text, 0});
                    break;
                case 'R': {
                    // GNU opens R's file the first time the command runs, so
                    // its read error (a directory) aborts right there, before
                    // this line is printed; the line itself is read at flush.
                    RReader& state = rReaders[i];
                    if (!state.tried) {
                        state.tried = true;
                        const std::string& name = command.text;
                        if (name == "/dev/stdin") {
                            state.file = context.IO().GetDescriptor(IFileIO::kStdIn);
                        } else {
                            InputOpenFailure failure = InputOpenFailure::None;
                            state.file = OpenInputOperand(context, name, failure);
                            if (!state.file && failure == InputOpenFailure::Directory) {
                                context.Error("read error on " + name + ": Is a directory");
                                appendFatal = true;
                                stopAll = true;
                                break;
                            }
                        }
                        if (state.file) {
                            state.reader = std::make_unique<BuiltinLineReader>(context, *state.file, d);
                        }
                    }
                    queue.push_back(QueueItem{'R', "", i});
                    break;
                }
                case 'h':
                    holdSpace = patternSpace;
                    holdDelimited = delimited;
                    break;
                case 'H':
                    holdSpace += d;
                    holdSpace += patternSpace;
                    holdDelimited = delimited;
                    break;
                case 'g':
                    patternSpace = holdSpace;
                    delimited = holdDelimited;
                    break;
                case 'G':
                    patternSpace += d;
                    patternSpace += holdSpace;
                    delimited = holdDelimited;
                    break;
                case 'x':
                    std::swap(patternSpace, holdSpace);
                    std::swap(delimited, holdDelimited);
                    break;
                case 'z':
                    patternSpace.clear();  // its delimiter state is kept
                    break;
                case 'b':
                    // A continue skips the ++i below, so the target is the
                    // label's ':' itself, a no-op (the end of the script for
                    // an empty label: the loop falls through to the print).
                    i = command.jump == kJumpToEnd ? script.commands.size() : command.jump;
                    continue;
                case 't':
                    if (tFlag) {
                        tFlag = false;
                        i = command.jump == kJumpToEnd ? script.commands.size() : command.jump;
                        continue;
                    }
                    break;
                case 'T':
                    if (tFlag) {
                        tFlag = false;
                    } else {
                        i = command.jump == kJumpToEnd ? script.commands.size() : command.jump;
                        continue;
                    }
                    break;
                case 'y':
                    for (char& c : patternSpace) {
                        const size_t k = command.text.find(c);
                        if (k != std::string::npos) {
                            c = command.text2[k];
                        }
                    }
                    break;
                case 'l': {
                    const int wrap = command.lLength >= 0 ? command.lLength : settings.lineLength;
                    WriteLine(main, mainMissing, FormatList(patternSpace, wrap), d);
                    break;
                }
                case 'w': {
                    OutputFile& out = script.outputFiles[command.wFile];
                    WriteRecord(OutputTarget(context, out), out.missing, patternSpace, delimited, d);
                    break;
                }
                case 'W': {
                    OutputFile& out = script.outputFiles[command.wFile];
                    WriteHeadRecord(OutputTarget(context, out), out.missing, patternSpace, delimited, d);
                    break;
                }
                case 's': {
                    bool replaced = false;
                    Substitute(command, patternSpace, lastRegex, missingRegex, replaced);
                    if (missingRegex) {
                        break;
                    }
                    if (replaced) {
                        tFlag = true;
                    }
                    if (replaced && command.print) {
                        WriteRecord(main, mainMissing, patternSpace, delimited, d);
                    }
                    if (replaced && command.wFile >= 0) {
                        OutputFile& out = script.outputFiles[command.wFile];
                        WriteRecord(OutputTarget(context, out), out.missing, patternSpace, delimited, d);
                    }
                    break;
                }
                default:
                    break;
            }
            if (cycleEnd || restart || stopAll || quitCode >= 0) {
                break;
            }
            ++i;
        }
        if (stopAll || quitCode >= 0 || missingRegex) {
            break;
        }
        if (restart) {
            readAgain = false;
            continue;
        }
        if (!cycleEnd && !quiet) {
            WriteRecord(main, mainMissing, patternSpace, delimited, d);
        }
        if (settings.unbuffered) {
            context.Flush();
        }
    }
    if (input.Fatal() || appendFatal) {
        return 4;
    }
    if (missingRegex) {
        if (script.errorLocationIsFile) {
            context.Error("file " + script.errorFileName + " line " + std::to_string(script.errorLine) +
                         ": no previous regular expression");
        } else {
            context.Error("-e expression #" + std::to_string(script.errorExpression) +
                         ", char 0: no previous regular expression");
        }
        return 1;
    }
    if (input.ErrorStatus() != 0) {
        return input.ErrorStatus();
    }
    if (quitCode >= 0) {
        return quitCode;
    }
    return 0;
}

} // namespace Haisos::Sed