#include "SedExecutor.h"

#include <memory>
#include <utility>

#include "BuiltinText.h"
#include "interfaces/IFileIO.h"

namespace Haisos::Sed {
namespace {

// Everything a run writes to standard output goes through here, so GNU's
// missing-newline rule holds: a line read without its delimiter is written
// without one, and the newline it is owed is written before anything else
// that follows.
class SedOutput {
public:
    explicit SedOutput(BuiltinContext& context)
        : m_context(context)
    {
    }

    // The pattern space, written with its newline unless the line was read
    // without one (|delimited|).
    void WritePatternSpace(const std::string& text, bool delimited)
    {
        std::string out;
        if (m_missing) {
            out += '\n';
        }
        out += text;
        if (delimited) {
            out += '\n';
        }
        m_context.Out(out);
        m_missing = !delimited;
    }

    // A line '=' writes: always complete with its newline.
    void WriteLine(const std::string& text)
    {
        std::string out;
        if (m_missing) {
            out += '\n';
        }
        out += text;
        out += '\n';
        m_context.Out(out);
        m_missing = false;
    }

    // q's write, not the ordinary one: GNU flushes the pending newline even
    // under -n, and terminates the pattern space with a newline even when
    // the line was read without one (printf 'x' | sed q prints "x\n").
    void QuitWrite(const std::string& text, bool quiet)
    {
        std::string out;
        if (m_missing) {
            out += '\n';
        }
        if (!quiet) {
            out += text;
            out += '\n';
        }
        if (!out.empty()) {
            m_context.Out(out);
        }
        m_missing = false;
    }

private:
    BuiltinContext& m_context;
    bool m_missing = false;
};

// The inputs, one read at a time, with the one-line lookahead the '$' address
// needs. A pattern-space read reports what GNU reports; the lookahead is
// quieter (as GNU's is): a file that cannot be opened is reported and costs
// status 2, a read error ends the input silently, and the status it would
// cost is settled when the pattern space reaches the same end.
class SedInput {
public:
    SedInput(BuiltinContext& context, std::vector<std::string> inputs, bool separate)
        : m_context(context), m_inputs(std::move(inputs)), m_separate(separate)
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
            m_reader = std::make_unique<BuiltinLineReader>(m_context, *m_input, '\n');
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

bool MatchAddress(Command& command, SedInput& input, const std::string& patternSpace,
                  std::shared_ptr<const Regex>& lastRegex, bool& missingRegex)
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
            }
        }
    } else if (command.a2.kind != AddressKind::None) {
        if (command.rangeEnded) {
            matched = false;
        } else if (!command.rangeActive) {
            matched = MatchOne(command.a1, input, patternSpace, lastRegex, missingRegex);
            if (matched) {
                command.rangeActive = true;
                command.rangeStart = input.LineNumber();
                if (command.a2.kind == AddressKind::Line && command.a2.line <= input.LineNumber()) {
                    command.rangeEnded = true;
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
                        end = input.LineNumber() % command.a2.line == 0;
                        break;
                    default:
                        break;
                }
            }
            if (end) {
                command.rangeEnded = true;
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

// The ranges' state is per file with -s.
void ResetRanges(Script& script)
{
    for (auto& command : script.commands) {
        command.rangeActive = false;
        command.rangeEnded = false;
        command.rangeStart = 0;
    }
}

} // namespace

int RunScript(BuiltinContext& context, Script& script, const std::vector<std::string>& inputs,
              const ExecSettings& settings)
{
    SedInput input(context, inputs, settings.separate);
    SedOutput output(context);
    const bool quiet = settings.quiet || script.quietFromScript;
    std::shared_ptr<const Regex> lastRegex;
    int quitCode = -1;
    bool missingRegex = false;
    std::string patternSpace;
    bool delimited = false;
    bool cycleEnd = false;   // reached the end without printing (d, or n without a line)
    while (true) {
        bool newFile = false;
        const auto r = input.Read(patternSpace, delimited, newFile);
        if (r == SedInput::ReadResult::End || r == SedInput::ReadResult::Error ||
            r == SedInput::ReadResult::Stopped) {
            break;
        }
        if (newFile && settings.separate) {
            ResetRanges(script);
        }
        cycleEnd = false;
        for (size_t i = 0; i < script.commands.size(); ++i) {
            Command& command = script.commands[i];
            const bool matches = MatchAddress(command, input, patternSpace, lastRegex, missingRegex);
            if (missingRegex) {
                break;
            }
            if (!matches) {
                if (command.name == '{') {
                    i = command.jump - 1;
                }
                continue;
            }
            switch (command.name) {
                case '{':
                case '}':
                case ':':
                    break;
                case 'p':
                    output.WritePatternSpace(patternSpace, delimited);
                    break;
                case 'd':
                    cycleEnd = true;
                    break;
                case 'n': {
                    if (!quiet) {
                        output.WritePatternSpace(patternSpace, delimited);
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
                    break;
                }
                case 'q':
                    output.QuitWrite(patternSpace, quiet);
                    quitCode = command.intArg;
                    cycleEnd = true;
                    break;
                case 'Q':
                    quitCode = command.intArg;
                    cycleEnd = true;
                    break;
                case '=':
                    output.WriteLine(std::to_string(input.LineNumber()));
                    break;
                case 's': {
                    bool replaced = false;
                    Substitute(command, patternSpace, lastRegex, missingRegex, replaced);
                    if (missingRegex) {
                        break;
                    }
                    if (replaced && command.print) {
                        output.WritePatternSpace(patternSpace, delimited);
                    }
                    break;
                }
                default:
                    break;
            }
            if (cycleEnd || missingRegex) {
                break;
            }
        }
        if (cycleEnd || missingRegex) {
            if (quitCode >= 0) {
                break;
            }
            if (missingRegex) {
                break;
            }
            continue;  // d, or n without a line: on to the next cycle
        }
        if (!quiet) {
            output.WritePatternSpace(patternSpace, delimited);
        }
    }
    if (input.Fatal()) {
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