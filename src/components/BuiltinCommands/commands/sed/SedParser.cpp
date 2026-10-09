#include "SedParser.h"

#include <algorithm>
#include <cctype>

#include "src/components/Regex/Regex.h"
#include "src/components/Filesystem/FilesystemUtils.h"

namespace Haisos::Sed {
namespace {

bool IsBlank(char c)
{
    return c == ' ' || c == '\t';
}

bool IsDigit(char c)
{
    return c >= '0' && c <= '9';
}

// The byte value of a \dNNN \oNNN \xNN escape, or -1; |taken| is how many
// digits were there for the caller to consume.
int EscapeValue(const std::string& raw, size_t i, int maxDigits, int base, int& taken)
{
    int value = 0;
    taken = 0;
    for (; taken < maxDigits && i + taken < raw.size(); ++taken) {
        const char c = raw[i + taken];
        int d = -1;
        if (base == 8) {
            d = c >= '0' && c <= '7' ? c - '0' : -1;
        } else if (base == 10) {
            d = IsDigit(c) ? c - '0' : -1;
        } else {
            d = c >= '0' && c <= '9' ? c - '0'
                : c >= 'a' && c <= 'f' ? c - 'a' + 10
                : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                      : -1;
        }
        if (d < 0) {
            break;
        }
        value = value * base + d;
    }
    return taken > 0 ? value & 0xFF : -1;
}

// sed translates its own escapes before handing a pattern to the regex
// engine: \n \t and friends become the byte, everything else passes through
// as written (the engine has GNU's escapes; it does not read \n as a
// newline). Inside [ ] only \n and \t are translated, as GNU does. A bracket
// is tracked so its contents are not taken for anything else.
std::string TranslateRegex(const std::string& raw)
{
    std::string out;
    out.reserve(raw.size());
    size_t i = 0;
    bool inBracket = false;
    while (i < raw.size()) {
        const char c = raw[i];
        if (c == '\\') {
            if (i + 1 >= raw.size()) {  // a trailing backslash: left to the engine, as GNU's is
                out += c;
                ++i;
                continue;
            }
            const char d = raw[i + 1];
            if (inBracket && d != 'n' && d != 't') {
                out += c;
                out += d;
                i += 2;
                continue;
            }
            switch (d) {
                case 'n': out += '\n'; i += 2; continue;
                case 't': out += '\t'; i += 2; continue;
                case 'f': out += '\f'; i += 2; continue;
                case 'v': out += '\v'; i += 2; continue;
                case 'a': out += '\a'; i += 2; continue;
                case 'r': out += '\r'; i += 2; continue;
                case 'd': {
                    int taken = 0;
                    const int v = EscapeValue(raw, i + 2, 3, 10, taken);
                    if (v >= 0 && taken > 0) {  // GNU takes one to three digits
                        out += static_cast<char>(v);
                        i += 2 + taken;
                        continue;
                    }
                    break;
                }
                case 'o': {
                    int taken = 0;
                    const int v = EscapeValue(raw, i + 2, 3, 8, taken);
                    if (v >= 0) {
                        out += static_cast<char>(v);
                        i += 2 + taken;
                        continue;
                    }
                    break;
                }
                case 'x': {
                    int taken = 0;
                    const int v = EscapeValue(raw, i + 2, 2, 16, taken);
                    if (v >= 0) {
                        out += static_cast<char>(v);
                        i += 2 + taken;
                        continue;
                    }
                    break;
                }
                case 'c': {
                    if (i + 2 < raw.size()) {
                        out += static_cast<char>(raw[i + 2] & 0x1F);
                        i += 3;
                        continue;
                    }
                    break;
                }
                default:
                    break;
            }
            out += c;
            out += d;
            i += 2;
            continue;
        }
        if (!inBracket && c == '[') {
            inBracket = true;
            out += c;
            ++i;
            if (i < raw.size() && raw[i] == '^') {
                out += raw[i];
                ++i;
            }
            if (i < raw.size() && raw[i] == ']') {  // a first ']' is a literal
                out += raw[i];
                ++i;
            }
            continue;
        }
        if (inBracket && c == ']') {
            inBracket = false;
        }
        out += c;
        ++i;
    }
    return out;
}

// The second pass over a/i/c text and the y strings: the reader kept every
// escaped pair as written, and here \n \t and friends become the byte,
// \dNNN \oNNN \xNN and \cX their byte, \\ a backslash and \ before anything
// else the char alone, as GNU takes it.
std::string ApplyTextEscapes(const std::string& raw)
{
    std::string out;
    out.reserve(raw.size());
    size_t i = 0;
    while (i < raw.size()) {
        const char c = raw[i];
        if (c != '\\' || i + 1 >= raw.size()) {
            out += c;
            ++i;
            continue;
        }
        const char d = raw[++i];
        ++i;
        switch (d) {
            case 'n': out += '\n'; continue;
            case 't': out += '\t'; continue;
            case 'f': out += '\f'; continue;
            case 'v': out += '\v'; continue;
            case 'a': out += '\a'; continue;
            case 'r': out += '\r'; continue;
            case 'd': {
                int taken = 0;
                const int v = EscapeValue(raw, i, 3, 10, taken);
                if (v >= 0 && taken > 0) {
                    out += static_cast<char>(v);
                    i += taken;
                } else {
                    out += 'd';
                }
                continue;
            }
            case 'o': {
                int taken = 0;
                const int v = EscapeValue(raw, i, 3, 8, taken);
                if (v >= 0) {
                    out += static_cast<char>(v);
                    i += taken;
                } else {
                    out += 'o';
                }
                continue;
            }
            case 'x': {
                int taken = 0;
                const int v = EscapeValue(raw, i, 2, 16, taken);
                if (v >= 0) {
                    out += static_cast<char>(v);
                    i += taken;
                } else {
                    out += 'x';
                }
                continue;
            }
            case 'c': {
                if (i < raw.size()) {
                    out += static_cast<char>(raw[i] & 0x1F);
                    ++i;
                } else {
                    out += 'c';
                }
                continue;
            }
            case '\\': out += '\\'; continue;
            default: out += d; continue;
        }
    }
    return out;
}

// Parses the pieces of the script as one stream. Each piece is read as GNU
// reads it -- a newline is added when its text lacks one -- and an error is
// located within its own piece: "expression #N, char M" for a command-line
// piece, "file F line L" for a script file.
class Parser {
public:
    Parser(BuiltinContext& context, const std::vector<ScriptPiece>& pieces, bool extended,
           bool sandbox, Script& script)
        : m_context(context), m_extended(extended), m_sandbox(sandbox), m_script(script)
    {
        int expression = 0;
        for (const auto& piece : pieces) {
            std::string data = piece.text;
            if (data.empty() || data.back() != '\n') {
                data += '\n';
            }
            if (!piece.fromFile) {
                ++expression;
            }
            m_pieces.push_back(Piece{std::move(data), piece.fromFile, piece.fileName,
                                     piece.text.size(), expression});
        }
    }

    bool Parse()
    {
        if (!m_pieces.empty() && m_pieces[0].data.size() >= 2 && m_pieces[0].data[0] == '#' &&
            m_pieces[0].data[1] == 'n') {
            m_script.quietFromScript = true;
        }
        while (true) {
            SkipSeparators();
            if (End()) {
                break;
            }
            if (!ParseOne()) {
                return false;
            }
        }
        if (!m_open.empty()) {
            return FailOpen(m_open.front());
        }
        StoreEndLocation();
        for (size_t i = 0; i < m_script.commands.size(); ++i) {
            if (m_script.commands[i].name == ':') {
                m_script.labels.push_back({m_script.commands[i].text, i});
            }
        }
        // The jumps, resolved now that every label is known: an unknown one
        // is GNU's compile-time error, exit 4, with no position to name.
        for (auto& command : m_script.commands) {
            if ((command.name == 'b' || command.name == 't' || command.name == 'T') &&
                !command.text.empty() && command.jump == 0) {
                bool found = false;
                for (const auto& label : m_script.labels) {
                    if (label.first == command.text) {
                        command.jump = label.second;
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    m_context.Error("can't find label for jump to `" + command.text + "'");
                    m_script.parseErrorStatus = 4;
                    return false;
                }
            }
        }
        return true;
    }

private:
    struct Piece {
        std::string data;      // the text, with a newline added when it lacks one
        bool fromFile = false;
        std::string fileName;
        size_t textLen = 0;    // the text without the added newline
        int expression = 0;    // among the command-line pieces, 1-based
    };

    // A '{' waiting for its '}': the command's index and where it sits.
    struct OpenBlock {
        size_t command;
        size_t piece;
        size_t index;
    };

    int Peek() const
    {
        if (m_piece >= m_pieces.size()) {
            return -1;
        }
        return static_cast<unsigned char>(m_pieces[m_piece].data[m_pos]);
    }

    // The character after the current one, or -1 at the stream's end (each
    // piece ends with a newline, so a pair never straddles two pieces).
    int PeekNext() const
    {
        if (m_piece >= m_pieces.size() || m_pos + 1 >= m_pieces[m_piece].data.size()) {
            return -1;
        }
        return static_cast<unsigned char>(m_pieces[m_piece].data[m_pos + 1]);
    }

    void Advance()
    {
        m_takenPiece = m_piece;
        m_takenIndex = m_pos;
        ++m_pos;
        if (m_pos >= m_pieces[m_piece].data.size()) {
            ++m_piece;
            m_pos = 0;
        }
    }

    bool End() const { return m_piece >= m_pieces.size(); }

    // Reports a parse error at |index| of piece |piece| (a 0-based index into
    // its data) and returns false. GNU's "char M" is the index plus one,
    // clamped to the text's own length when the error is revealed by the added
    // newline; "line L" counts the newlines before it.
    bool Fail(size_t piece, size_t index, const std::string& message)
    {
        const Piece& p = m_pieces[std::min(piece, m_pieces.size() - 1)];
        if (p.fromFile) {
            const size_t upto = std::min(index, p.data.size());
            int line = 1;
            for (size_t i = 0; i < upto; ++i) {
                if (p.data[i] == '\n') {
                    ++line;
                }
            }
            m_context.Error("file " + p.fileName + " line " + std::to_string(line) + ": " + message);
        } else {
            const int pos = index < p.textLen ? static_cast<int>(index) + 1 : static_cast<int>(p.textLen);
            m_context.Error("-e expression #" + std::to_string(p.expression) + ", char " + std::to_string(pos) +
                            ": " + message);
        }
        return false;
    }

    // An error at the current position. The stream never ends in the middle of
    // something, but a piece-crossing scan can leave it at the very end.
    bool FailHere(const std::string& message)
    {
        if (End()) {
            return Fail(m_takenPiece, m_takenIndex, message);
        }
        return Fail(m_piece, m_pos, message);
    }

    // An error at the last character taken -- GNU's position for a mistake in
    // what was just parsed (a bad regex, a group reference).
    bool FailTaken(const std::string& message) { return Fail(m_takenPiece, m_takenIndex, message); }

    // The unmatched-'{' error: GNU reports it at the '{' itself -- character 0
    // of a command-line piece, the line of the '{' in a script file.
    bool FailOpen(const OpenBlock& block)
    {
        const Piece& p = m_pieces[block.piece];
        if (p.fromFile) {
            int line = 1;
            for (size_t i = 0; i < block.index; ++i) {
                if (p.data[i] == '\n') {
                    ++line;
                }
            }
            m_context.Error("file " + p.fileName + " line " + std::to_string(line) + ": unmatched `{'");
        } else {
            m_context.Error("-e expression #" + std::to_string(p.expression) + ", char 0: unmatched `{'");
        }
        return false;
    }

    // Where a runtime script error is reported: GNU reports the position at
    // the end of the parse -- the last piece read.
    void StoreEndLocation()
    {
        if (m_pieces.empty()) {
            return;
        }
        const Piece& last = m_pieces.back();
        m_script.errorLocationIsFile = last.fromFile;
        m_script.errorFileName = last.fileName;
        m_script.errorExpression = last.expression;
        int line = 1;
        for (const char c : last.data) {
            if (c == '\n') {
                ++line;
            }
        }
        m_script.errorLine = line;
    }

    void SkipBlanks()
    {
        while (!End() && IsBlank(static_cast<char>(Peek()))) {
            Advance();
        }
    }

    void SkipSeparators()
    {
        while (!End()) {
            const char c = static_cast<char>(Peek());
            if (c == ' ' || c == '\t' || c == '\n' || c == ';') {
                Advance();
            } else {
                break;
            }
        }
    }

    uint64_t ParseNumber()
    {
        uint64_t value = 0;
        while (!End() && IsDigit(static_cast<char>(Peek()))) {
            if (value < 1000000000) {
                value = value * 10 + (static_cast<char>(Peek()) - '0');
            }
            Advance();
        }
        return value;
    }

    std::shared_ptr<const Regex> CompileRegex(const std::string& pattern, bool ignoreCase, bool multiline)
    {
        RegexOptions options;
        options.syntax = m_extended ? RegexSyntax::Extended : RegexSyntax::Basic;
        options.ignoreCase = ignoreCase;
        options.multiline = multiline;
        std::string error;
        auto regex = Regex::Compile(pattern, options, error);
        if (!regex) {
            FailTaken(error);  // at the last character of the regex, as GNU counts
        }
        return regex;
    }

    // A /RE/ or \cREc address (the current character is '/' or '\').
    bool ParseRegexAddress(Address& address)
    {
        char delimiter = '/';
        if (static_cast<char>(Peek()) == '\\') {
            Advance();
            delimiter = static_cast<char>(Peek());
            Advance();
        } else {
            Advance();
        }
        std::string raw;
        // The same scanning as s's halves: an escape is one unit and '['
        // brackets hold their delimiter, so /a\/ and /[/]/ parse as GNU's.
        if (!ScanDelimited(raw, delimiter, true)) {
            return FailHere("unterminated address regex");
        }
        bool ignoreCase = false;
        bool multiline = false;
        while (!End()) {
            const char c = static_cast<char>(Peek());
            if (c == 'I' || c == 'i') {
                ignoreCase = true;
            } else if (c == 'M' || c == 'm') {
                multiline = true;
            } else {
                break;
            }
            Advance();
        }
        address.kind = AddressKind::Regex;
        address.ignoreCase = ignoreCase;
        address.multiline = multiline;
        if (!raw.empty()) {
            address.regex = CompileRegex(TranslateRegex(raw), ignoreCase, multiline);
            if (!address.regex) {
                return false;
            }
        }
        return true;
    }

    // One address, if there is one; |have| tells the caller. |second| is
    // after a comma, where +N and ~N are the relative forms.
    bool ParseAddress(Address& address, bool& have, bool second)
    {
        have = false;
        const int c = Peek();
        if (c == -1) {
            return true;
        }
        if (IsDigit(static_cast<char>(c))) {
            have = true;
            const uint64_t n = ParseNumber();
            if (!second && !End() && static_cast<char>(Peek()) == '~') {
                Advance();
                const uint64_t step = ParseNumber();
                address.kind = AddressKind::Step;
                address.line = n;
                address.step = step;
            } else {
                address.kind = n == 0 ? AddressKind::Zero : AddressKind::Line;
                address.line = n;
            }
            return true;
        }
        switch (c) {
            case '$':
                have = true;
                address.kind = AddressKind::Last;
                Advance();
                return true;
            case '/':
            case '\\':
                have = true;
                return ParseRegexAddress(address);
            case '+':
            case '~': {
                have = true;
                Advance();
                const uint64_t n = ParseNumber();
                if (!second) {
                    return Fail(m_piece, m_takenIndex, "invalid usage of +N or ~N as first address");
                }
                address.kind = c == '+' ? AddressKind::RelativeLines : AddressKind::Multiple;
                address.line = n;
                return true;
            }
            default:
                return true;
        }
    }

    // The text between the delimiters of s (and of an address regex):
    // '\\' escapes the delimiter and, before a newline, continues the line
    // keeping a newline in the text, as GNU does; '[' brackets are counted
    // when |regex| -- the delimiter is a literal inside them, as GNU takes
    // it -- with a first ']' literal and [:class:], [.x.] and [=x=] each one
    // unit, their own ']' not closing the bracket.
    bool ScanDelimited(std::string& raw, char delimiter, bool regex)
    {
        bool inBracket = false;
        while (!End()) {
            const char c = static_cast<char>(Peek());
            if (c == '\n') {
                return false;
            }
            if (c == '\\') {
                const bool has = m_pos + 1 < m_pieces[m_piece].data.size();
                if (has && m_pieces[m_piece].data[m_pos + 1] == '\n') {
                    raw += '\n';  // a continued line, the newline kept, as GNU's
                    Advance();
                    Advance();
                    continue;
                }
                if (has && m_pieces[m_piece].data[m_pos + 1] == delimiter) {
                    raw += delimiter;
                    Advance();
                    Advance();
                    continue;
                }
                // An escape is one unit: "\\/" is a literal backslash
                // (kept as "\\\\" for the regex or replacement parser) and
                // then the delimiter, not an escaped one.
                if (has) {
                    raw += c;
                    raw += m_pieces[m_piece].data[m_pos + 1];
                    Advance();
                    Advance();
                    continue;
                }
                raw += c;
                Advance();
                continue;
            }
            if (regex && !inBracket && c == '[') {
                inBracket = true;
                raw += c;
                Advance();
                if (!End() && static_cast<char>(Peek()) == '^') {
                    raw += '^';
                    Advance();
                }
                if (!End() && static_cast<char>(Peek()) == ']') {  // a first ']' is a literal
                    raw += ']';
                    Advance();
                }
                continue;
            }
            if (regex && inBracket && c == '[' && m_pos + 1 < m_pieces[m_piece].data.size()) {
                const char n = m_pieces[m_piece].data[m_pos + 1];
                if (n == ':' || n == '.' || n == '=') {
                    // A bracket item, one unit: its ']' does not close the
                    // bracket it sits in.
                    raw += c;
                    raw += n;
                    Advance();
                    Advance();
                    while (!End()) {
                        const char c2 = static_cast<char>(Peek());
                        if (c2 == '\n') {
                            return false;
                        }
                        raw += c2;
                        Advance();
                        if (c2 == n && !End() && static_cast<char>(Peek()) == ']') {
                            raw += ']';
                            Advance();
                            break;
                        }
                    }
                    continue;
                }
            }
            if (regex && inBracket && c == ']') {
                inBracket = false;
            }
            if (c == delimiter && !inBracket) {
                Advance();
                return true;
            }
            raw += c;
            Advance();
        }
        return false;
    }

    bool BuildReplacement(const std::string& raw, const std::shared_ptr<const Regex>& regex,
                          const std::shared_ptr<Replacement>& replacement)
    {
        auto addText = [&](std::string text) {
            if (!replacement->pieces.empty() && replacement->pieces.back().kind == ReplacementPiece::Kind::Text) {
                replacement->pieces.back().text += text;
            } else {
                replacement->pieces.push_back(ReplacementPiece{ReplacementPiece::Kind::Text, std::move(text), 0});
            }
        };
        for (size_t i = 0; i < raw.size(); ++i) {
            const char c = raw[i];
            if (c != '\\' && c != '&') {
                addText(std::string(1, c));
                continue;
            }
            if (c == '&') {
                replacement->pieces.push_back(ReplacementPiece{ReplacementPiece::Kind::Group, "", 0});
                continue;
            }
            if (i + 1 >= raw.size()) {
                addText("\\");
                continue;
            }
            const char d = raw[i + 1];
            ++i;
            switch (d) {
                case 'n': addText("\n"); continue;
                case 't': addText("\t"); continue;
                case 'f': addText("\f"); continue;
                case 'v': addText("\v"); continue;
                case 'a': addText("\a"); continue;
                case 'r': addText("\r"); continue;
                case 'd': {
                    int taken = 0;
                    const int v = EscapeValue(raw, i + 1, 3, 10, taken);
                    if (v >= 0 && taken > 0) {  // GNU takes one to three digits
                        addText(std::string(1, static_cast<char>(v)));
                        i += taken;
                        continue;
                    }
                    addText("d");
                    continue;
                }
                case 'o': {
                    int taken = 0;
                    const int v = EscapeValue(raw, i + 1, 3, 8, taken);
                    if (v >= 0) {
                        addText(std::string(1, static_cast<char>(v)));
                        i += taken;
                        continue;
                    }
                    addText("o");
                    continue;
                }
                case 'x': {
                    int taken = 0;
                    const int v = EscapeValue(raw, i + 1, 2, 16, taken);
                    if (v >= 0) {
                        addText(std::string(1, static_cast<char>(v)));
                        i += taken;
                        continue;
                    }
                    addText("x");
                    continue;
                }
                case 'c': {
                    if (i + 1 < raw.size()) {
                        addText(std::string(1, static_cast<char>(raw[i + 1] & 0x1F)));
                        ++i;
                        continue;
                    }
                    addText("c");
                    continue;
                }
                case 'L':
                    replacement->pieces.push_back(ReplacementPiece{ReplacementPiece::Kind::LowerRest, "", 0});
                    continue;
                case 'U':
                    replacement->pieces.push_back(ReplacementPiece{ReplacementPiece::Kind::UpperRest, "", 0});
                    continue;
                case 'l':
                    replacement->pieces.push_back(ReplacementPiece{ReplacementPiece::Kind::LowerNext, "", 0});
                    continue;
                case 'u':
                    replacement->pieces.push_back(ReplacementPiece{ReplacementPiece::Kind::UpperNext, "", 0});
                    continue;
                case 'E':
                    replacement->pieces.push_back(ReplacementPiece{ReplacementPiece::Kind::EndCase, "", 0});
                    continue;
                case '&': addText("&"); continue;
                case '\\': addText("\\"); continue;
                default:
                    if (d >= '1' && d <= '9') {
                        const int group = d - '0';
                        if (regex && static_cast<size_t>(group) > regex->GroupCount()) {
                            return FailTaken("invalid reference \\" + std::string(1, d) +
                                             " on `s' command's RHS");
                        }
                        replacement->pieces.push_back(ReplacementPiece{ReplacementPiece::Kind::Group, "", group});
                        continue;
                    }
                    addText(std::string(1, d));
                    continue;
            }
        }
        return true;
    }

    // Registers a w/W/s///w file, opening it now (GNU reports one it cannot
    // open during the parse, exiting 4 without reading any input). Returns
    // its index, or -1 with the message written.
    int AddOutputFile(const std::string& name)
    {
        OutputFile out;
        out.name = name;
        if (name == "/dev/stdout") {
            out.isStdout = true;
        } else if (name == "/dev/stderr") {
            out.isStderr = true;
        } else {
            out.file = m_context.IO().OpenFile(name, kFileOpenWriteCreateTruncate);
            if (!out.file) {
                std::string reason = "Permission denied";
                FileStatus status;
                if (m_context.IO().Stat(name, status) != 0) {
                    reason = "No such file or directory";
                } else if (status.type == DirectoryEntryType::Dir) {
                    reason = "Is a directory";
                }
                m_context.Error("couldn't open file " + name + ": " + reason);
                m_script.parseErrorStatus = 4;
                return -1;
            }
        }
        m_script.outputFiles.push_back(std::move(out));
        return static_cast<int>(m_script.outputFiles.size()) - 1;
    }

    // The file name of r/R/w/W: leading blanks, then the rest of the line as
    // written -- a ';' or '}' is part of the name, as GNU takes it. The
    // ending newline is consumed. Empty is GNU's error at |errorIndex| (the
    // command character, or the w flag of s).
    bool ParseFileName(size_t errorIndex, std::string& name)
    {
        SkipBlanks();
        const size_t start = m_pos;
        while (!End() && static_cast<char>(Peek()) != '\n') {
            Advance();
        }
        if (m_pos == start) {
            return Fail(m_piece, errorIndex, "missing filename in r/R/w/W commands");
        }
        name = m_pieces[m_piece].data.substr(start, m_pos - start);
        return true;
    }

    // The a/i/c text. One line: an optional '\' introducer, then the rest of
    // the line with every escaped pair kept as written and '\'+newline
    // continuing the text; `a\` on its own line is the classic form, where
    // the text is the following lines, each one ending in '\' continuing it.
    // The assembled text then takes ApplyTextEscapes.
    bool ParseTextCommand(Command& command, size_t commandIndex)
    {
        std::string raw;
        SkipBlanks();
        if (End() || static_cast<char>(Peek()) == '\n') {
            return Fail(m_piece, commandIndex, "expected \\ after `a', `c' or `i'");
        }
        if (static_cast<char>(Peek()) == '\\') {
            Advance();  // the introducer, not part of the text
            if (End() || static_cast<char>(Peek()) == '\n') {
                Advance();  // the newline: the classic form
                bool anyLine = false;
                while (!End()) {
                    anyLine = true;
                    std::string line;
                    while (!End() && static_cast<char>(Peek()) != '\n') {
                        line += static_cast<char>(Peek());
                        Advance();
                    }
                    bool hadNewline = false;
                    if (!End() && static_cast<char>(Peek()) == '\n') {
                        Advance();
                        hadNewline = true;
                    }
                    // GNU continues the text onto the next line only when
                    // the line ends with an odd number of backslashes: the
                    // last one is the marker, the pairs before it literal
                    // backslashes (ApplyTextEscapes takes each pair down to
                    // one, an even count leaving the text ended).
                    size_t trailing = 0;
                    while (trailing < line.size() && line[line.size() - 1 - trailing] == '\\') {
                        ++trailing;
                    }
                    const bool continued = (trailing & 1) == 1;
                    if (continued) {
                        line.pop_back();
                    }
                    raw += line;
                    if (!continued) {
                        break;
                    }
                    raw += '\n';
                    if (!hadNewline) {
                        break;  // the stream ended mid-line
                    }
                }
                command.text = ApplyTextEscapes(raw);
                command.noText = !anyLine;  // GNU: no text line, nothing is added
                return true;
            }
            // `a\\` at the end of its line: GNU ends the text there, empty.
            if (static_cast<char>(Peek()) == '\\' &&
                (PeekNext() == '\n' || PeekNext() == -1)) {
                Advance();
                if (!End() && static_cast<char>(Peek()) == '\n') {
                    Advance();
                }
                command.text = "";
                return true;
            }
        }
        while (!End()) {
            const char c = static_cast<char>(Peek());
            if (c == '\n') {
                Advance();
                break;
            }
            if (c == '\\') {
                if (PeekNext() == '\n') {
                    raw += '\n';  // a continued line, the newline kept
                    Advance();
                    Advance();
                    continue;
                }
                raw += c;  // every other escaped pair, as written
                if (PeekNext() != -1) {
                    raw += static_cast<char>(PeekNext());
                    Advance();
                }
                Advance();
                continue;
            }
            raw += c;
            Advance();
        }
        command.text = ApplyTextEscapes(raw);
        return true;
    }

    // The s command (the current character is 's'): s/regex/replacement/flags.
    bool ParseS(Command& command)
    {
        Advance();  // 's'
        command.name = 's';
        const int d = Peek();
        if (d == -1 || d == '\n' || d == '\\') {
            // No delimiter: GNU consumes the line and reports at its end.
            while (!End() && static_cast<char>(Peek()) != '\n') {
                Advance();
            }
            return FailHere("unterminated `s' command");
        }
        const char delimiter = static_cast<char>(d);
        Advance();
        std::string rawRegex;
        if (!ScanDelimited(rawRegex, delimiter, true)) {
            return FailHere("unterminated `s' command");
        }
        std::string rawReplacement;
        if (!ScanDelimited(rawReplacement, delimiter, false)) {
            return FailHere("unterminated `s' command");
        }
        bool global = false;
        bool print = false;
        bool ignoreCase = false;
        bool multiline = false;
        uint64_t occurrence = 1;
        while (true) {
            SkipBlanks();  // GNU takes blanks among the flags
            const int c = Peek();
            if (c == -1) {
                break;
            }
            const char ch = static_cast<char>(c);
            if (ch == '\n' || ch == ';' || ch == '}' || ch == '#') {
                break;
            }
            if (IsDigit(ch)) {
                const size_t digit = m_pos;
                const uint64_t n = ParseNumber();
                if (n == 0) {
                    return Fail(m_piece, digit, "number option to `s' command may not be zero");
                }
                occurrence = n;
                continue;
            }
            if (ch == 'g') {
                if (global) {
                    return FailHere("multiple `g' options to `s' command");
                }
                global = true;
            } else if (ch == 'p') {
                if (print) {
                    return FailHere("multiple `p' options to `s' command");
                }
                print = true;
            } else if (ch == 'i' || ch == 'I') {
                ignoreCase = true;
            } else if (ch == 'm' || ch == 'M') {
                multiline = true;
            } else if (ch == 'e') {
                m_context.NotTreated("s///e");
            } else if (ch == 'w') {
                // s///w's file, the rest of the line (a ';' is part of the
                // name, as GNU takes it), registered like w's.
                const size_t wPos = m_pos;
                if (m_sandbox) {
                    return Fail(m_piece, wPos, "e/r/w commands disabled in sandbox mode");
                }
                Advance();
                SkipBlanks();
                const size_t start = m_pos;
                while (!End() && static_cast<char>(Peek()) != '\n') {
                    Advance();
                }
                if (m_pos == start) {
                    return Fail(m_piece, m_pos, "missing filename in r/R/w/W commands");
                }
                const int index = AddOutputFile(m_pieces[m_piece].data.substr(start, m_pos - start));
                if (index < 0) {
                    return false;
                }
                command.wFile = index;
                break;  // the name took the rest of the line
            } else {
                return FailHere("unknown option to `s'");
            }
            Advance();
        }
        std::shared_ptr<const Regex> regex;
        if (!rawRegex.empty()) {
            regex = CompileRegex(TranslateRegex(rawRegex), ignoreCase, multiline);
            if (!regex) {
                return false;
            }
        }
        command.regex = std::move(regex);
        command.replacement = std::make_shared<Replacement>();
        if (!BuildReplacement(rawReplacement, command.regex, command.replacement)) {
            return false;
        }
        command.global = global;
        command.print = print;
        command.occurrence = occurrence;
        m_script.commands.push_back(std::move(command));
        return AfterCommand();
    }

    // After a command (or a label): blanks, then a ';', a newline, the '}'
    // of an enclosing block, a comment or the end. Anything else is extra.
    bool AfterCommand()
    {
        SkipBlanks();
        const int c = Peek();
        if (c == -1 || c == '\n' || c == ';' || c == '}') {
            return true;
        }
        if (c == '#') {
            while (!End() && static_cast<char>(Peek()) != '\n') {
                Advance();
            }
            return true;
        }
        return FailHere("extra characters after command");
    }

    // One command, at the current position (already past any separators).
    bool ParseOne()
    {
        Command command;
        bool haveA1 = false;
        bool haveA2 = false;
        if (!ParseAddress(command.a1, haveA1, false)) {
            return false;
        }
        SkipBlanks();
        if (!End() && static_cast<char>(Peek()) == ',') {
            Advance();
            SkipBlanks();
            if (!ParseAddress(command.a2, haveA2, true)) {
                return false;
            }
            if (!haveA2) {
                return FailHere("unexpected ','");
            }
        }
        SkipBlanks();
        if (!End() && static_cast<char>(Peek()) == '!') {
            Advance();
            SkipBlanks();
            if (!End() && static_cast<char>(Peek()) == '!') {
                return FailHere("multiple `!'s");
            }
            command.negate = true;
        }
        SkipBlanks();
        // The line address 0: only the first half of 0,/re/ takes it. GNU
        // reports it at the '!' when there is one, else at the command char,
        // else right after the address itself (in place of "missing command").
        if ((command.a1.kind == AddressKind::Zero && !(haveA2 && command.a2.kind == AddressKind::Regex)) ||
            command.a2.kind == AddressKind::Zero) {
            if (command.negate) {
                return Fail(m_piece, m_takenIndex, "invalid usage of line address 0");
            }
            if (End()) {
                return FailTaken("invalid usage of line address 0");
            }
            return Fail(m_piece, m_pos, "invalid usage of line address 0");
        }
        const int c = Peek();
        if (c == -1) {
            return FailHere("missing command");
        }
        const size_t commandIndex = m_pos;
        if (c == '\n') {
            return Fail(m_piece, commandIndex, "missing command");
        }
        const char ch = static_cast<char>(c);
        if (ch == '}') {
            // GNU checks for an open block first, then for addresses: `1}`
            // is unexpected, `{p;1}` has addresses on a closing '}'.
            if (m_open.empty()) {
                return Fail(m_piece, commandIndex, "unexpected `}'");
            }
            if (haveA1 || haveA2) {
                return Fail(m_piece, commandIndex, "`}' doesn't want any addresses");
            }
            Advance();
            const OpenBlock& block = m_open.back();
            m_script.commands[block.command].jump = m_script.commands.size() + 1;
            m_open.pop_back();
            command.name = '}';
            m_script.commands.push_back(std::move(command));
            return AfterCommand();
        }
        if (ch == '#') {
            if (haveA1 || haveA2) {
                return Fail(m_piece, commandIndex, "comments don't accept any addresses");
            }
            while (!End() && static_cast<char>(Peek()) != '\n') {
                Advance();
            }
            return true;
        }
        if (ch == ':') {
            if (haveA1 || haveA2) {
                return Fail(m_piece, commandIndex, ": doesn't want any addresses");
            }
            // Blanks may follow a label before more script on the same line
            // (":a p" is GNU's), unlike the commands AfterCommand checks.
            while (true) {
                Advance();  // ':' or the blank before the next label
                SkipBlanks();
                const size_t start = m_pos;
                while (!End()) {
                    const char c2 = static_cast<char>(Peek());
                    if (c2 == '\n' || c2 == ';' || IsBlank(c2)) {
                        break;
                    }
                    Advance();
                }
                if (m_pos == start) {
                    return FailHere("\":\" lacks a label");
                }
                Command label;
                label.name = ':';
                label.text = m_pieces[m_piece].data.substr(start, m_pos - start);
                m_script.commands.push_back(std::move(label));
                SkipBlanks();
                const int next = Peek();
                if (next == -1 || next == '\n' || next == ';' || next == '}' || next == '#') {
                    return AfterCommand();
                }
                if (next != ':') {
                    return ParseOne();
                }
            }
        }
        if (ch == '{') {
            Advance();
            command.name = '{';
            const size_t index = m_script.commands.size();
            m_script.commands.push_back(std::move(command));
            m_open.push_back(OpenBlock{index, m_piece, commandIndex});
            return true;
        }
        if (ch == 's') {
            return ParseS(command);
        }
        if (ch == 'q' || ch == 'Q') {
            if (haveA1 && haveA2) {
                return Fail(m_piece, commandIndex, "command only uses one address");
            }
            Advance();
            command.name = ch;
            SkipBlanks();
            if (!End() && IsDigit(static_cast<char>(Peek()))) {
                command.intArg = static_cast<int>(ParseNumber() & 0xFF);
            }
            m_script.commands.push_back(std::move(command));
            return AfterCommand();
        }
        if (ch == 'a' || ch == 'i' || ch == 'c') {
            Advance();
            command.name = ch;
            if (!ParseTextCommand(command, commandIndex)) {
                return false;
            }
            // The text took its line (and any lines it continued to); what
            // follows is a fresh command, without AfterCommand.
            m_script.commands.push_back(std::move(command));
            return true;
        }
        if (ch == 'b' || ch == 't' || ch == 'T') {
            Advance();
            command.name = ch;
            SkipBlanks();
            const size_t start = m_pos;
            while (!End()) {
                const char c2 = static_cast<char>(Peek());
                if (c2 == '\n' || c2 == ';' || c2 == '}' || IsBlank(c2)) {
                    break;
                }
                Advance();
            }
            if (m_pos == start) {
                command.jump = kJumpToEnd;  // the end of the script, GNU's empty label
            } else {
                command.text = m_pieces[m_piece].data.substr(start, m_pos - start);
                command.jump = 0;  // resolved after the parse
            }
            m_script.commands.push_back(std::move(command));
            return AfterCommand();
        }
        if (ch == 'r' || ch == 'R') {
            if (m_sandbox) {
                return Fail(m_piece, commandIndex, "e/r/w commands disabled in sandbox mode");
            }
            Advance();
            command.name = ch;
            if (!ParseFileName(commandIndex, command.text)) {
                return false;
            }
            m_script.commands.push_back(std::move(command));
            return true;  // the name took the rest of the line
        }
        if (ch == 'w' || ch == 'W') {
            if (m_sandbox) {
                return Fail(m_piece, commandIndex, "e/r/w commands disabled in sandbox mode");
            }
            Advance();
            command.name = ch;
            std::string name;
            if (!ParseFileName(commandIndex, name)) {
                return false;
            }
            const int index = AddOutputFile(name);
            if (index < 0) {
                return false;
            }
            command.wFile = index;
            m_script.commands.push_back(std::move(command));
            return true;  // the name took the rest of the line
        }
        if (ch == 'y') {
            Advance();
            command.name = 'y';
            if (End() || static_cast<char>(Peek()) == '\n') {
                return FailTaken("unterminated `y' command");
            }
            const char delimiter = static_cast<char>(Peek());
            Advance();
            std::string source;
            std::string dest;
            if (!ScanDelimited(source, delimiter, false)) {
                return FailTaken("unterminated `y' command");
            }
            if (!ScanDelimited(dest, delimiter, false)) {
                return FailTaken("unterminated `y' command");
            }
            command.text = ApplyTextEscapes(source);
            command.text2 = ApplyTextEscapes(dest);
            if (command.text.size() != command.text2.size()) {
                return FailTaken("strings for `y' command are different lengths");
            }
            m_script.commands.push_back(std::move(command));
            return AfterCommand();
        }
        if (ch == 'l') {
            Advance();
            command.name = 'l';
            SkipBlanks();
            if (!End() && IsDigit(static_cast<char>(Peek()))) {
                command.lLength = static_cast<int>(ParseNumber());
            }
            m_script.commands.push_back(std::move(command));
            return AfterCommand();
        }
        if (ch == 'v') {
            Advance();
            command.name = 'v';
            SkipBlanks();
            const size_t start = m_pos;
            while (!End()) {
                const char c2 = static_cast<char>(Peek());
                if (c2 == '\n' || c2 == ';' || c2 == '}' || IsBlank(c2)) {
                    break;
                }
                Advance();
            }
            if (m_pos > start && !VersionOk(m_pieces[m_piece].data.substr(start, m_pos - start))) {
                return FailTaken("expected newer version of sed");
            }
            m_script.commands.push_back(std::move(command));
            return AfterCommand();
        }
        if (ch == 'e') {
            if (m_sandbox) {
                return Fail(m_piece, commandIndex, "e/r/w commands disabled in sandbox mode");
            }
            Advance();
            command.name = 'e';
            SkipBlanks();
            const size_t start = m_pos;
            while (!End() && static_cast<char>(Peek()) != '\n') {
                Advance();
            }
            if (m_pos > start) {
                command.text = m_pieces[m_piece].data.substr(start, m_pos - start);
            }
            m_context.NotTreated("e");
            m_script.commands.push_back(std::move(command));
            return true;  // the command line took the rest of the line
        }
        if (ch == 'd' || ch == 'p' || ch == 'n' || ch == '=' || ch == 'N' || ch == 'D' || ch == 'P' ||
            ch == 'h' || ch == 'H' || ch == 'g' || ch == 'G' || ch == 'x' || ch == 'z' || ch == 'F') {
            Advance();
            command.name = ch;
            m_script.commands.push_back(std::move(command));
            return AfterCommand();
        }
        return Fail(m_piece, commandIndex, "unknown command: `" + std::string(1, ch) + "'");
    }

    // v's version, against this sed's 4.9: every component numeric, and the
    // version greater than 4.9 (or as long as it: 4.9.x) is refused.
    static bool VersionOk(const std::string& token)
    {
        size_t i = 0;
        const uint64_t reference[] = {4, 9};
        size_t component = 0;
        while (true) {
            if (i >= token.size() || !IsDigit(token[i])) {
                return false;
            }
            uint64_t value = 0;
            while (i < token.size() && IsDigit(token[i])) {
                value = value * 10 + (token[i] - '0');
                ++i;
            }
            if (component >= 2) {
                return false;  // more components than 4.9 has
            }
            if (value > reference[component]) {
                return false;
            }
            if (value < reference[component]) {
                return true;  // an older version is fine
            }
            ++component;
            if (i >= token.size()) {
                return true;
            }
            if (token[i] != '.') {
                return false;
            }
            ++i;
        }
    }

    BuiltinContext& m_context;
    bool m_extended = false;
    bool m_sandbox = false;
    Script& m_script;
    std::vector<Piece> m_pieces;
    std::vector<OpenBlock> m_open;
    size_t m_piece = 0;
    size_t m_pos = 0;
    size_t m_takenPiece = 0;
    size_t m_takenIndex = 0;
};

} // namespace

bool ParseScript(BuiltinContext& context, const std::vector<ScriptPiece>& pieces, bool extendedRegex,
                 bool sandbox, Script& script)
{
    Parser parser(context, pieces, extendedRegex, sandbox, script);
    return parser.Parse();
}

} // namespace Haisos::Sed