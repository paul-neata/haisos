#include "commands/hsh/HshLexer.h"

#include "commands/hsh/HshError.h"

namespace Haisos::Hsh {

const char* OperatorText(TokenKind kind) {
    switch (kind) {
        case TokenKind::Semicolon:       return ";";
        case TokenKind::DoubleSemicolon: return ";;";
        case TokenKind::Ampersand:       return "&";
        case TokenKind::AndIf:           return "&&";
        case TokenKind::Pipe:            return "|";
        case TokenKind::OrIf:            return "||";
        case TokenKind::LeftParen:       return "(";
        case TokenKind::RightParen:      return ")";
        case TokenKind::Less:            return "<";
        case TokenKind::Great:           return ">";
        case TokenKind::DoubleGreat:     return ">>";
        case TokenKind::Clobber:         return ">|";
        case TokenKind::LessGreat:       return "<>";
        case TokenKind::LessAnd:         return "<&";
        case TokenKind::GreatAnd:        return ">&";
        case TokenKind::DoubleLess:      return "<<";
        case TokenKind::DoubleLessDash:  return "<<-";
        case TokenKind::TripleLess:      return "<<<";
        case TokenKind::AndGreat:        return "&>";
        default:                         return "";
    }
}

bool IsRedirectionOperator(TokenKind kind) {
    return static_cast<int>(kind) >= static_cast<int>(TokenKind::Less) &&
           static_cast<int>(kind) <= static_cast<int>(TokenKind::AndGreat);
}

const char* ReservedWordText(ReservedWord word) {
    switch (word) {
        case ReservedWord::If:         return "if";
        case ReservedWord::Then:       return "then";
        case ReservedWord::Else:       return "else";
        case ReservedWord::Elif:       return "elif";
        case ReservedWord::Fi:         return "fi";
        case ReservedWord::Do:         return "do";
        case ReservedWord::Done:       return "done";
        case ReservedWord::Case:       return "case";
        case ReservedWord::Esac:       return "esac";
        case ReservedWord::While:      return "while";
        case ReservedWord::Until:      return "until";
        case ReservedWord::For:        return "for";
        case ReservedWord::In:         return "in";
        case ReservedWord::LeftBrace:  return "{";
        case ReservedWord::RightBrace: return "}";
        case ReservedWord::Bang:       return "!";
    }
    return "";
}

std::optional<ReservedWord> AsReservedWord(const Token& token) {
    if (token.kind != TokenKind::Word) {
        return std::nullopt;
    }
    std::optional<std::string> text = LiteralText(token.word);
    if (!text) {
        return std::nullopt;
    }
    static const ReservedWord words[] = {
        ReservedWord::If, ReservedWord::Then, ReservedWord::Else, ReservedWord::Elif,
        ReservedWord::Fi, ReservedWord::Do, ReservedWord::Done, ReservedWord::Case,
        ReservedWord::Esac, ReservedWord::While, ReservedWord::Until, ReservedWord::For,
        ReservedWord::In, ReservedWord::LeftBrace, ReservedWord::RightBrace, ReservedWord::Bang,
    };
    for (ReservedWord word : words) {
        if (*text == ReservedWordText(word)) {
            return word;
        }
    }
    return std::nullopt;
}

std::string DescribeToken(const Token& token) {
    switch (token.kind) {
        case TokenKind::Word:       return "W(" + DescribeWord(token.word) + ")";
        case TokenKind::IoNumber:   return "IO(" + std::to_string(token.ioNumber) + ")";
        case TokenKind::Newline:    return "NL";
        case TokenKind::EndOfInput: return "EOF";
        default:                    return OperatorText(token.kind);
    }
}

namespace {

constexpr int kEof = -1;

bool IsBlank(int c) {
    return c == ' ' || c == '\t';
}

bool IsOperatorChar(int c) {
    return c == ';' || c == '&' || c == '|' || c == '(' || c == ')' || c == '<' || c == '>';
}

bool IsNameStart(int c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
}

bool IsNameChar(int c) {
    return IsNameStart(c) || (c >= '0' && c <= '9');
}

bool IsDigit(int c) {
    return c >= '0' && c <= '9';
}

bool IsSpecialParameterChar(int c) {
    return c == '@' || c == '*' || c == '#' || c == '?' || c == '-' || c == '$' ||
           c == '!' || c == '0';
}

} // namespace

struct Lexer::Impl {
    std::shared_ptr<const std::string> src;
    size_t pos = 0;
    int line = 1;
    bool interactive = false;
    // Set when the character layer skipped a backslash-newline pair that ended
    // exactly at the end of the source; the interactive shell then reads more.
    bool lastContinuationAtEnd = false;
    size_t lastCharEnd = 0;  // one past the last raw character consumed as content
    bool expectHereDocDelimiter = false;
    bool hereDocStripsTabs = false;
    bool incompleteHereDoc = false;
    std::vector<std::shared_ptr<HereDocument>> pendingHereDocs;

    Impl(std::shared_ptr<const std::string> source, size_t start, int startLine, bool isInteractive)
        : src(std::move(source)), pos(start), line(startLine), interactive(isInteractive),
          lastCharEnd(start) {}

    // -- the character layer ------------------------------------------------

    int RawPeek() const {
        return pos < src->size() ? static_cast<unsigned char>((*src)[pos]) : kEof;
    }

    int RawGet() {
        int c = RawPeek();
        if (c != kEof) {
            ++pos;
            if (c == '\n') {
                ++line;
            }
            lastCharEnd = pos;
            lastContinuationAtEnd = false;
        }
        return c;
    }

    // Skips backslash-newline pairs: the line continuation, removed everywhere
    // but in single quotes, comments and quoted-delimiter heredoc bodies (those
    // read raw).
    void SkipContinuations() {
        bool skipped = false;
        while (pos + 1 < src->size() && (*src)[pos] == '\\' && (*src)[pos + 1] == '\n') {
            pos += 2;
            ++line;
            skipped = true;
        }
        if (skipped) {
            lastContinuationAtEnd = (pos == src->size());
        }
    }

    int Peek() {
        SkipContinuations();
        return RawPeek();
    }

    int Get() {
        SkipContinuations();
        return RawGet();
    }

    // -- part building --------------------------------------------------------

    static void AppendChar(std::vector<WordPart>& parts, WordPartKind kind, char c) {
        if (!parts.empty() && parts.back().kind == kind &&
            (kind == WordPartKind::Literal || kind == WordPartKind::Quoted)) {
            parts.back().text += c;
            return;
        }
        WordPart part;
        part.kind = kind;
        part.text = std::string(1, c);
        parts.push_back(std::move(part));
    }

    static void AppendText(std::vector<WordPart>& parts, WordPartKind kind,
                           const std::string& text) {
        if (text.empty()) {
            // An empty quoted group ('' or "") keeps a part: the word is
            // present but empty. Merging an empty text changes nothing.
            if (kind == WordPartKind::Quoted &&
                (parts.empty() || parts.back().kind != WordPartKind::Quoted)) {
                WordPart part;
                part.kind = WordPartKind::Quoted;
                parts.push_back(std::move(part));
            }
            return;
        }
        if (!parts.empty() && parts.back().kind == kind &&
            (kind == WordPartKind::Literal || kind == WordPartKind::Quoted)) {
            parts.back().text += text;
            return;
        }
        WordPart part;
        part.kind = kind;
        part.text = text;
        parts.push_back(std::move(part));
    }

    // -- words ---------------------------------------------------------------

    // Reads a single-quoted group (raw: no continuation removal there) and
    // appends its contents as Quoted text. The opening quote is at pos.
    void ReadSingleQuoted(std::vector<WordPart>& parts) {
        RawGet(); // '\''
        std::string text;
        while (true) {
            int c = RawPeek();
            if (c == kEof) {
                throw ShellError("Syntax error: Unterminated quoted string", line, true);
            }
            RawGet();
            if (c == '\'') {
                break;
            }
            text += static_cast<char>(c);
        }
        AppendText(parts, WordPartKind::Quoted, text);
    }

    // Reads a double-quoted group. The opening quote is at pos.
    WordPart ReadDoubleQuoted() {
        WordPart part;
        part.kind = WordPartKind::DoubleQuoted;
        RawGet(); // '"'
        while (true) {
            int c = Peek();
            if (c == kEof) {
                throw ShellError("Syntax error: Unterminated quoted string", line, true);
            }
            if (c == '"') {
                RawGet();
                break;
            }
            if (c == '\\') {
                RawGet();
                int d = RawPeek();
                if (d == '$' || d == '`' || d == '"' || d == '\\') {
                    RawGet();
                    AppendChar(part.parts, WordPartKind::Quoted, static_cast<char>(d));
                } else {
                    AppendChar(part.parts, WordPartKind::Quoted, '\\');
                    if (d != kEof) {
                        RawGet();
                        AppendChar(part.parts, WordPartKind::Quoted, static_cast<char>(d));
                    }
                }
                continue;
            }
            if (c == '$') {
                ReadDollar(part.parts, WordPartKind::Quoted, true);
                continue;
            }
            if (c == '`') {
                part.parts.push_back(ReadBackquote(true));
                continue;
            }
            RawGet();
            AppendChar(part.parts, WordPartKind::Quoted, static_cast<char>(c));
        }
        return part;
    }

    // Reads a backquoted command substitution; the opening backquote is at pos.
    // Only `\$`, `` \` `` and `\\` are unescaped -- and `\"` too when the
    // backquote opened inside double quotes; every other backslash is kept.
    WordPart ReadBackquote(bool dquote) {
        WordPart part;
        part.kind = WordPartKind::CommandSubstitution;
        part.backquoted = true;
        part.line = line;
        RawGet(); // '`'
        while (true) {
            int c = Peek();
            if (c == kEof) {
                throw ShellError("Syntax error: EOF in backquote substitution", line, true);
            }
            if (c == '`') {
                RawGet();
                break;
            }
            if (c == '\\') {
                RawGet();
                int d = RawPeek();
                if (d == '$' || d == '`' || d == '\\' || (d == '"' && dquote)) {
                    RawGet();
                    part.text += static_cast<char>(d);
                } else {
                    part.text += '\\';
                    if (d != kEof) {
                        RawGet();
                        part.text += static_cast<char>(d);
                    }
                }
                continue;
            }
            RawGet();
            part.text += static_cast<char>(c);
        }
        return part;
    }

    // Reads a `$...` expansion; the '$' is at pos. |plain| is the part kind a
    // lone '$' takes in this context; |dquote| selects the operand flavor.
    void ReadDollar(std::vector<WordPart>& parts, WordPartKind plain, bool dquote) {
        RawGet(); // '$'
        int c = Peek();
        if (c != kEof && IsNameStart(c)) {
            std::string name;
            while (true) {
                c = Peek();
                if (c == kEof || !IsNameChar(c)) {
                    break;
                }
                RawGet();
                name += static_cast<char>(c);
            }
            WordPart part;
            part.kind = WordPartKind::Parameter;
            part.text = name;
            parts.push_back(std::move(part));
            return;
        }
        if (IsDigit(c) || IsSpecialParameterChar(c)) {
            RawGet();
            WordPart part;
            part.kind = WordPartKind::Parameter;
            part.text = std::string(1, static_cast<char>(c));
            parts.push_back(std::move(part));
            return;
        }
        if (c == '{') {
            RawGet();
            parts.push_back(ReadBracedParameter(dquote));
            return;
        }
        if (c == '(') {
            RawGet();
            if (Peek() == '(') {
                RawGet();
                parts.push_back(ReadArithmetic());
            } else {
                parts.push_back(ReadCommandSubstitution());
            }
            return;
        }
        AppendChar(parts, plain, '$');
    }

    // Reads the name of a braced parameter: [A-Za-z_][A-Za-z0-9_]*, digits, or
    // one special character. Consumes nothing when there is no name.
    std::optional<std::string> ReadBracedName() {
        int c = Peek();
        std::string name;
        if (c != kEof && IsNameStart(c)) {
            while (true) {
                c = Peek();
                if (c == kEof || !IsNameChar(c)) {
                    break;
                }
                RawGet();
                name += static_cast<char>(c);
            }
        } else if (IsDigit(c)) {
            while (true) {
                c = Peek();
                if (!IsDigit(c)) {
                    break;
                }
                RawGet();
                name += static_cast<char>(c);
            }
        } else if (c != kEof && IsSpecialParameterChar(c)) {
            RawGet();
            name += static_cast<char>(c);
        } else {
            return std::nullopt;
        }
        return name;
    }

    // The rest of a Bad parameter: lexed as an operand and discarded, so the
    // matching '}' is found past any nested substitution. Not a syntax error.
    WordPart FinishBadParameter(bool dquote) {
        WordPart part;
        part.kind = WordPartKind::Parameter;
        part.op = ParameterOp::Bad;
        std::vector<WordPart> discarded;
        ReadOperand(discarded, dquote);
        return part;
    }

    // Reads a ${...} parameter; the '{' has been consumed.
    WordPart ReadBracedParameter(bool dquote) {
        WordPart part;
        part.kind = WordPartKind::Parameter;
        int c = Peek();
        if (c == '#') {
            RawGet();
            int d = Peek();
            if (d == '}') { // ${#}: the parameter '#'
                RawGet();
                part.text = "#";
                return part;
            }
            std::optional<std::string> name = ReadBracedName();
            if (name) {
                part.op = ParameterOp::Length;
                part.text = *name;
                if (Peek() != '}') {
                    return FinishBadParameter(dquote);
                }
                RawGet();
                return part;
            }
            // '#' followed by no name is the parameter '#' itself, with an op
            // (${#:-x}, as dash).
            part.text = "#";
        } else {
            std::optional<std::string> name = ReadBracedName();
            if (!name) {
                return FinishBadParameter(dquote);
            }
            part.text = *name;
        }
        int d = Peek();
        if (d == '}') {
            RawGet();
            return part;
        }
        if (d == '%' || d == '#') {
            RawGet();
            if (Peek() == d) {
                RawGet();
                part.op = d == '%' ? ParameterOp::RemoveLargestSuffix
                                   : ParameterOp::RemoveLargestPrefix;
            } else {
                part.op = d == '%' ? ParameterOp::RemoveSmallestSuffix
                                   : ParameterOp::RemoveSmallestPrefix;
            }
            // A pattern operand keeps its own quoting even inside double
            // quotes: "${x#'a'}" removes the a, not the literal 'a'.
            ReadOperand(part.parts, dquote, true);
            return part;
        } else {
            bool colon = false;
            if (d == ':') {
                colon = true;
                RawGet();
                d = Peek();
            }
            switch (d) {
                case '-': part.op = colon ? ParameterOp::UseDefault : ParameterOp::UseDefaultIfUnset; break;
                case '=': part.op = colon ? ParameterOp::AssignDefault : ParameterOp::AssignDefaultIfUnset; break;
                case '?': part.op = colon ? ParameterOp::ErrorIfNull : ParameterOp::ErrorIfUnset; break;
                case '+': part.op = colon ? ParameterOp::UseAlternative : ParameterOp::UseAlternativeIfSet; break;
                default:  return FinishBadParameter(dquote);
            }
            RawGet();
        }
        ReadOperand(part.parts, dquote, false);
        return part;
    }

    // Reads the operand of a ${...}, up to and including the matching '}'.
    // |patternOperand|: the operand of #, ##, % or %% -- one place where a
    // single quote opens real quoting even inside double quotes, as dash does
    // (everywhere else in a double-quoted operand a ' is a plain character).
    void ReadOperand(std::vector<WordPart>& parts, bool dquote, bool patternOperand = false) {
        while (true) {
            int c = Peek();
            if (c == kEof) {
                throw ShellError("Syntax error: Missing '}'", line, true);
            }
            if (c == '}') {
                RawGet();
                return;
            }
            WordPartKind plain = dquote ? WordPartKind::Quoted : WordPartKind::Literal;
            if (c == '\\') { // an escaped char is Quoted in every operand
                RawGet();
                int d = RawPeek();
                if (dquote) {
                    if (d == '$' || d == '`' || d == '"' || d == '\\' || d == '}') {
                        RawGet();
                        AppendChar(parts, WordPartKind::Quoted, static_cast<char>(d));
                    } else {
                        AppendChar(parts, WordPartKind::Quoted, '\\');
                        if (d != kEof) {
                            RawGet();
                            AppendChar(parts, WordPartKind::Quoted, static_cast<char>(d));
                        }
                    }
                } else {
                    if (d == kEof) {
                        AppendChar(parts, WordPartKind::Quoted, '\\');
                    } else {
                        RawGet();
                        AppendChar(parts, WordPartKind::Quoted, static_cast<char>(d));
                    }
                }
                continue;
            }
            if (c == '\'') {
                if (dquote && !patternOperand) {
                    RawGet();
                    AppendChar(parts, plain, '\'');
                } else {
                    ReadSingleQuoted(parts);
                }
                continue;
            }
            if (c == '"') {
                parts.push_back(ReadDoubleQuoted());
                continue;
            }
            if (c == '$') {
                ReadDollar(parts, plain, dquote);
                continue;
            }
            if (c == '`') {
                parts.push_back(ReadBackquote(dquote));
                continue;
            }
            RawGet();
            AppendChar(parts, plain, static_cast<char>(c));
        }
    }

    // Reads a $((...)) expression; the "$(( " has been consumed. Ends at the
    // second ')' of "))" at parenthesis depth 0.
    WordPart ReadArithmetic() {
        WordPart part;
        part.kind = WordPartKind::Arithmetic;
        int depth = 0;
        while (true) {
            int c = Peek();
            if (c == kEof) {
                throw ShellError("Syntax error: Missing '))'", line, true);
            }
            if (c == '(') {
                RawGet();
                ++depth;
                AppendChar(part.parts, WordPartKind::Literal, '(');
                continue;
            }
            if (c == ')') {
                RawGet();
                if (depth > 0) {
                    --depth;
                    AppendChar(part.parts, WordPartKind::Literal, ')');
                    continue;
                }
                if (Peek() == ')') {
                    RawGet();
                    return part;
                }
                throw ShellError("Syntax error: Missing '))'", line, false);
            }
            if (c == '\\') { // escaped as inside double quotes, but Literal text
                RawGet();
                int d = RawPeek();
                if (d == '$' || d == '`' || d == '"' || d == '\\') {
                    RawGet();
                    AppendChar(part.parts, WordPartKind::Literal, static_cast<char>(d));
                } else {
                    AppendChar(part.parts, WordPartKind::Literal, '\\');
                    if (d != kEof) {
                        RawGet();
                        AppendChar(part.parts, WordPartKind::Literal, static_cast<char>(d));
                    }
                }
                continue;
            }
            if (c == '$') {
                ReadDollar(part.parts, WordPartKind::Literal, false);
                continue;
            }
            if (c == '`') {
                part.parts.push_back(ReadBackquote(false));
                continue;
            }
            RawGet();
            AppendChar(part.parts, WordPartKind::Literal, static_cast<char>(c));
        }
    }

    // The frames of the $(...) scan: an open paren or an open case, the case
    // with a state.
    struct ScanFrame {
        bool isCase = false;
        enum { Subject, ExpectIn, Pattern, Body } state = Subject;
    };

    // Reads a $(...) substitution; the "$(" has been consumed. A sub-lexer over
    // the same source reads tokens -- so quotes, comments, nesting and heredocs
    // inside follow the ordinary rules -- until the ')' that closes it.
    WordPart ReadCommandSubstitution() {
        WordPart part;
        part.kind = WordPartKind::CommandSubstitution;
        part.line = line;
        size_t textBegin = pos;
        Impl sub(src, pos, line, interactive);
        std::vector<ScanFrame> stack;
        bool commandPosition = true;
        bool redirectTargetNext = false;
        while (true) {
            Token t = sub.NextToken();
            ScanFrame* top = stack.empty() ? nullptr : &stack.back();
            bool inPattern = top != nullptr && top->isCase && top->state == ScanFrame::Pattern;
            switch (t.kind) {
                case TokenKind::EndOfInput:
                    throw ShellError("Syntax error: end of file unexpected (expecting \")\")",
                                     sub.line, true);
                case TokenKind::LeftParen:
                    if (inPattern) {
                        break; // an optional leading ( before a pattern
                    }
                    stack.push_back(ScanFrame{});
                    commandPosition = true;
                    redirectTargetNext = false;
                    break;
                case TokenKind::RightParen:
                    if (inPattern) {
                        top->state = ScanFrame::Body;
                        commandPosition = true;
                        redirectTargetNext = false;
                        break;
                    }
                    if (top != nullptr && !top->isCase) {
                        stack.pop_back();
                        commandPosition = false;
                        redirectTargetNext = false;
                        break;
                    }
                    if (top != nullptr) {
                        break; // a ')' a case frame in Subject/ExpectIn/Body does not close
                    }
                    // The ')' that closes the substitution.
                    part.text = src->substr(textBegin, t.begin - textBegin);
                    for (const std::shared_ptr<HereDocument>& hd : sub.pendingHereDocs) {
                        hd->rawBody.clear();
                        hd->complete = true;
                        hd->terminated = false;
                        hd->sourceBegin = t.begin;
                        hd->sourceEnd = t.begin;
                    }
                    pos = sub.pos;
                    line = sub.line;
                    lastCharEnd = sub.lastCharEnd;
                    lastContinuationAtEnd = false;
                    return part;
                case TokenKind::Newline:
                case TokenKind::Semicolon:
                case TokenKind::Ampersand:
                case TokenKind::AndIf:
                case TokenKind::OrIf:
                case TokenKind::Pipe:
                    commandPosition = true;
                    redirectTargetNext = false;
                    break;
                case TokenKind::DoubleSemicolon:
                    if (top != nullptr && top->isCase && top->state == ScanFrame::Body) {
                        top->state = ScanFrame::Pattern;
                    }
                    commandPosition = true;
                    redirectTargetNext = false;
                    break;
                case TokenKind::IoNumber:
                    break;
                case TokenKind::Word: {
                    if (redirectTargetNext) {
                        redirectTargetNext = false;
                        break;
                    }
                    std::optional<std::string> literal = LiteralText(t.word);
                    if (top != nullptr && top->isCase) {
                        if (top->state == ScanFrame::Subject) {
                            top->state = ScanFrame::ExpectIn;
                            commandPosition = false;
                            break;
                        }
                        if (top->state == ScanFrame::ExpectIn) {
                            if (literal && *literal == "in") {
                                top->state = ScanFrame::Pattern;
                            }
                            commandPosition = false;
                            break;
                        }
                        if (top->state == ScanFrame::Pattern) {
                            if (literal && *literal == "esac") {
                                stack.pop_back();
                            }
                            commandPosition = false;
                            break;
                        }
                        // Body: "esac" in command position pops the frame.
                        if (commandPosition && literal && *literal == "esac") {
                            stack.pop_back();
                            commandPosition = false;
                            break;
                        }
                    }
                    if (commandPosition && literal && *literal == "case") {
                        ScanFrame frame;
                        frame.isCase = true;
                        stack.push_back(frame);
                        commandPosition = false;
                        break;
                    }
                    if (commandPosition && literal &&
                        (*literal == "if" || *literal == "then" || *literal == "else" ||
                         *literal == "elif" || *literal == "while" || *literal == "until" ||
                         *literal == "do" || *literal == "!" || *literal == "{" ||
                         *literal == "}")) {
                        commandPosition = true;
                        break;
                    }
                    commandPosition = false;
                    break;
                }
                default:
                    // A redirection operator: commandPosition unchanged, and the
                    // word after it (its target) changes nothing either.
                    if (IsRedirectionOperator(t.kind)) {
                        redirectTargetNext = true;
                    }
                    break;
            }
        }
    }

    // -- heredocs ---------------------------------------------------------------

    // The delimiter word's quotes, removed literally: '...' its contents,
    // "..." with \$ \` \" \\ unescaped, \c the bare c, everything else as is
    // (backslash-newline pairs, removed by the character layer, do not appear).
    static std::string UnquoteHereDocDelimiter(const std::string& source, bool& quoted) {
        quoted = false;
        std::string out;
        size_t i = 0;
        while (i < source.size()) {
            char c = source[i];
            if (c == '\'') {
                quoted = true;
                ++i;
                while (i < source.size() && source[i] != '\'') {
                    out += source[i++];
                }
                if (i < source.size()) {
                    ++i;
                }
            } else if (c == '"') {
                quoted = true;
                ++i;
                while (i < source.size() && source[i] != '"') {
                    if (source[i] == '\\' && i + 1 < source.size() &&
                        (source[i + 1] == '$' || source[i + 1] == '`' || source[i + 1] == '"' ||
                         source[i + 1] == '\\')) {
                        out += source[i + 1];
                        i += 2;
                    } else if (source[i] == '\\' && i + 1 < source.size() && source[i + 1] == '\n') {
                        i += 2; // a line continuation inside the quotes
                    } else {
                        out += source[i++];
                    }
                }
                if (i < source.size()) {
                    ++i;
                }
            } else if (c == '\\') {
                if (i + 1 < source.size() && source[i + 1] == '\n') {
                    i += 2; // a line continuation inside the word
                    continue;
                }
                quoted = true;
                ++i;
                if (i < source.size()) {
                    out += source[i++];
                } else {
                    out += '\\';
                }
            } else {
                out += c;
                ++i;
            }
        }
        return out;
    }

    // Lexes an unquoted-delimiter heredoc body: Quoted text, with \$ \` \\
    // unescaped and $/`-expansions read, at the body's own lines.
    Word LexHereDocBody(const std::string& rawBody, int bodyFirstLine) {
        Word body;
        body.source = rawBody;
        body.line = bodyFirstLine;
        Impl sub(std::make_shared<const std::string>(rawBody), 0, bodyFirstLine, false);
        while (true) {
            int c = sub.Peek();
            if (c == kEof) {
                return body;
            }
            if (c == '\\') {
                sub.RawGet();
                int d = sub.RawPeek();
                if (d == '$' || d == '`' || d == '\\') {
                    sub.RawGet();
                    AppendChar(body.parts, WordPartKind::Quoted, static_cast<char>(d));
                } else {
                    AppendChar(body.parts, WordPartKind::Quoted, '\\');
                    if (d != kEof) {
                        sub.RawGet();
                        AppendChar(body.parts, WordPartKind::Quoted, static_cast<char>(d));
                    }
                }
                continue;
            }
            if (c == '$') {
                // A heredoc body is like double quotes: so are its ${...} operands.
                sub.ReadDollar(body.parts, WordPartKind::Quoted, true);
                continue;
            }
            if (c == '`') {
                body.parts.push_back(sub.ReadBackquote(false));
                continue;
            }
            sub.RawGet();
            AppendChar(body.parts, WordPartKind::Quoted, static_cast<char>(c));
        }
    }

    // Reads the bodies of all pending heredocs, in order, from the current
    // position: raw lines, tabs stripped for <<-, up to the delimiter line.
    void ReadHereDocBodies() {
        if (pendingHereDocs.empty()) {
            return;
        }
        std::vector<std::shared_ptr<HereDocument>> docs;
        docs.swap(pendingHereDocs);
        for (const std::shared_ptr<HereDocument>& hd : docs) {
            int bodyLine = line;
            size_t bodyBegin = pos;
            std::string rawBody;
            bool terminated = false;
            while (pos < src->size()) {
                size_t lineStart = pos;
                while (pos < src->size() && (*src)[pos] != '\n') {
                    ++pos;
                }
                std::string raw = src->substr(lineStart, pos - lineStart);
                if (pos < src->size()) {
                    ++pos;
                    ++line;
                    lastCharEnd = pos;
                    lastContinuationAtEnd = false;
                    raw += '\n';
                }
                if (hd->stripTabs) {
                    size_t i = 0;
                    while (i < raw.size() && raw[i] == '\t') {
                        ++i;
                    }
                    raw.erase(0, i);
                }
                if (!hd->quoted) {
                    // A backslash-newline inside an unquoted heredoc body is a
                    // line continuation: join the lines before the delimiter
                    // comparison, as dash does (a quoted delimiter's body keeps
                    // raw lines).
                    while (raw.size() >= 2 && raw[raw.size() - 2] == '\\' &&
                           raw.back() == '\n' && pos < src->size()) {
                        raw.erase(raw.size() - 2);
                        lineStart = pos;
                        while (pos < src->size() && (*src)[pos] != '\n') {
                            ++pos;
                        }
                        std::string next = src->substr(lineStart, pos - lineStart);
                        if (pos < src->size()) {
                            ++pos;
                            ++line;
                            lastCharEnd = pos;
                            lastContinuationAtEnd = false;
                            next += '\n';
                        }
                        if (hd->stripTabs) {
                            size_t i = 0;
                            while (i < next.size() && next[i] == '\t') {
                                ++i;
                            }
                            next.erase(0, i);
                        }
                        raw += next;
                    }
                }
                std::string cmp = raw;
                if (!cmp.empty() && cmp.back() == '\n') {
                    cmp.pop_back();
                }
                if (cmp == hd->delimiter) {
                    terminated = true;
                    break;
                }
                rawBody += raw;
            }
            hd->sourceBegin = bodyBegin;
            hd->sourceEnd = pos;
            hd->rawBody = rawBody;
            hd->complete = true;
            hd->terminated = terminated;
            if (hd->quoted) {
                hd->body.source = rawBody;
                hd->body.line = bodyLine;
                if (!rawBody.empty()) {
                    WordPart part;
                    part.kind = WordPartKind::Quoted;
                    part.text = rawBody;
                    hd->body.parts.push_back(std::move(part));
                }
            } else {
                hd->body = LexHereDocBody(rawBody, bodyLine);
            }
            if (!terminated) {
                incompleteHereDoc = true;
            }
        }
    }

    // -- tokens --------------------------------------------------------------

    // The unquoted-word character loop, ending at a blank, a newline or an
    // operator character.
    void ReadWordCharacters(std::vector<WordPart>& parts) {
        while (true) {
            int c = Peek();
            if (c == kEof || IsBlank(c) || c == '\n' || IsOperatorChar(c)) {
                return;
            }
            if (c == '\'') {
                ReadSingleQuoted(parts);
                continue;
            }
            if (c == '"') {
                parts.push_back(ReadDoubleQuoted());
                continue;
            }
            if (c == '\\') {
                RawGet();
                int d = RawPeek();
                if (d == kEof) {
                    AppendChar(parts, WordPartKind::Quoted, '\\');
                } else {
                    RawGet();
                    AppendChar(parts, WordPartKind::Quoted, static_cast<char>(d));
                }
                continue;
            }
            if (c == '$') {
                ReadDollar(parts, WordPartKind::Literal, false);
                continue;
            }
            if (c == '`') {
                parts.push_back(ReadBackquote(false));
                continue;
            }
            RawGet();
            AppendChar(parts, WordPartKind::Literal, static_cast<char>(c));
        }
    }

    Token NextToken() {
        bool expectHd = expectHereDocDelimiter;
        bool hdTabs = hereDocStripsTabs;
        expectHereDocDelimiter = false;

        while (true) { // blanks separate tokens
            int c = Peek();
            if (IsBlank(c)) {
                RawGet();
            } else {
                break;
            }
        }
        int c = Peek();
        if (c == '#') { // a comment runs to the end of the line, raw
            while (true) {
                int d = RawPeek();
                if (d == kEof || d == '\n') {
                    break;
                }
                RawGet();
            }
            c = Peek();
        }

        Token t;
        t.line = line;
        t.begin = pos;

        if (c == kEof) {
            ReadHereDocBodies();
            t.kind = TokenKind::EndOfInput;
            t.begin = src->size();
            t.end = src->size();
            t.line = line;
            if (interactive && (incompleteHereDoc || lastContinuationAtEnd)) {
                throw ShellError("Syntax error: end of file unexpected", line, true);
            }
            return t;
        }

        if (c == '\n') {
            RawGet();
            t.kind = TokenKind::Newline;
            t.end = pos;
            ReadHereDocBodies();
            return t;
        }

        if (IsDigit(c)) { // an IoNumber: one digit directly before < or >
            RawGet();
            int d = Peek();
            if (d == '<' || d == '>') {
                t.kind = TokenKind::IoNumber;
                t.ioNumber = c - '0';
                t.end = lastCharEnd;
                return t;
            }
            return ReadWordRest(t, expectHd, hdTabs, static_cast<char>(c));
        }

        if (IsOperatorChar(c)) {
            t.kind = ReadOperator();
            t.end = lastCharEnd;
            if (t.kind == TokenKind::DoubleLess || t.kind == TokenKind::DoubleLessDash) {
                expectHereDocDelimiter = true;
                hereDocStripsTabs = (t.kind == TokenKind::DoubleLessDash);
            }
            return t;
        }

        return ReadWordRest(t, expectHd, hdTabs, '\0');
    }

    // Longest match first: && &> & || | ;; ; ( ) <<< <<- << <& <> < >> >& >| >
    TokenKind ReadOperator() {
        int c = RawGet();
        switch (c) {
            case '&': {
                int d = Peek();
                if (d == '&') { RawGet(); return TokenKind::AndIf; }
                if (d == '>') { RawGet(); return TokenKind::AndGreat; }
                return TokenKind::Ampersand;
            }
            case '|':
                if (Peek() == '|') { RawGet(); return TokenKind::OrIf; }
                return TokenKind::Pipe;
            case ';':
                if (Peek() == ';') { RawGet(); return TokenKind::DoubleSemicolon; }
                return TokenKind::Semicolon;
            case '(':
                return TokenKind::LeftParen;
            case ')':
                return TokenKind::RightParen;
            case '<': {
                int d = Peek();
                if (d == '<') {
                    RawGet();
                    int e = Peek();
                    if (e == '-') { RawGet(); return TokenKind::DoubleLessDash; }
                    if (e == '<') { RawGet(); return TokenKind::TripleLess; }
                    return TokenKind::DoubleLess;
                }
                if (d == '&') { RawGet(); return TokenKind::LessAnd; }
                if (d == '>') { RawGet(); return TokenKind::LessGreat; }
                return TokenKind::Less;
            }
            case '>':
            default: {
                int d = Peek();
                if (d == '>') { RawGet(); return TokenKind::DoubleGreat; }
                if (d == '&') { RawGet(); return TokenKind::GreatAnd; }
                if (d == '|') { RawGet(); return TokenKind::Clobber; }
                return TokenKind::Great;
            }
        }
    }

    // Reads a word token whose first character (if any) was already consumed
    // (a digit that turned out not to be an IoNumber).
    Token ReadWordRest(Token t, bool expectHd, bool hdTabs, char first) {
        if (first != '\0') {
            AppendChar(t.word.parts, WordPartKind::Literal, first);
        }
        t.word.line = t.line;
        ReadWordCharacters(t.word.parts);
        t.kind = TokenKind::Word;
        t.end = lastCharEnd;
        t.word.source = src->substr(t.begin, t.end - t.begin);
        if (expectHd) { // the word after << / <<- carries the heredoc
            auto hd = std::make_shared<HereDocument>();
            hd->stripTabs = hdTabs;
            bool quoted = false;
            hd->delimiter = UnquoteHereDocDelimiter(t.word.source, quoted);
            hd->quoted = quoted;
            t.hereDoc = hd;
            pendingHereDocs.push_back(hd);
        }
        return t;
    }
};

Lexer::Lexer(std::string source, LexerOptions options)
    : m_impl(std::make_unique<Impl>(std::make_shared<const std::string>(std::move(source)),
                                    0, options.firstLine, options.interactive)) {}

Lexer::Lexer(Lexer&& other) noexcept = default;

Lexer& Lexer::operator=(Lexer&& other) noexcept = default;

Lexer::~Lexer() = default;

Token Lexer::Next() {
    return m_impl->NextToken();
}

int Lexer::Line() const {
    return m_impl->line;
}

} // namespace Haisos::Hsh
