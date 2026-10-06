#include "commands/hsh/HshParser.h"

#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "commands/hsh/HshError.h"

namespace Haisos::Hsh {

namespace {

bool IsNameStartChar(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
}

bool IsNameChar(char c) {
    return IsNameStartChar(c) || (c >= '0' && c <= '9');
}

bool IsDigitChar(char c) {
    return c >= '0' && c <= '9';
}

RedirectionKind RedirectionKindFor(TokenKind kind) {
    switch (kind) {
        case TokenKind::Less:           return RedirectionKind::Input;
        case TokenKind::Great:          return RedirectionKind::Output;
        case TokenKind::Clobber:        return RedirectionKind::OutputClobber;
        case TokenKind::DoubleGreat:    return RedirectionKind::Append;
        case TokenKind::LessGreat:      return RedirectionKind::ReadWrite;
        case TokenKind::LessAnd:        return RedirectionKind::DupInput;
        case TokenKind::GreatAnd:       return RedirectionKind::DupOutput;
        case TokenKind::DoubleLess:     return RedirectionKind::HereDoc;
        case TokenKind::DoubleLessDash: return RedirectionKind::HereDoc;
        case TokenKind::TripleLess:     return RedirectionKind::HereString;
        case TokenKind::AndGreat:       return RedirectionKind::OutputAndError;
        default:                        return RedirectionKind::Input;
    }
}

int DefaultRedirectionFd(RedirectionKind kind) {
    switch (kind) {
        case RedirectionKind::Input:
        case RedirectionKind::ReadWrite:
        case RedirectionKind::DupInput:
        case RedirectionKind::HereDoc:
        case RedirectionKind::HereString:
            return 0;
        default:
            return 1;
    }
}

// Whether the word holds anything to expand (a $-part, a substitution, an
// arithmetic part) at any depth; with none, its text is known now.
bool PartsHaveExpansions(const std::vector<WordPart>& parts) {
    for (const WordPart& part : parts) {
        switch (part.kind) {
            case WordPartKind::Literal:
            case WordPartKind::Quoted:
                break;
            case WordPartKind::DoubleQuoted:
                if (PartsHaveExpansions(part.parts)) {
                    return true;
                }
                break;
            default:
                return true;
        }
    }
    return false;
}

void AppendPlainText(const std::vector<WordPart>& parts, std::string& out) {
    for (const WordPart& part : parts) {
        if (part.kind == WordPartKind::DoubleQuoted) {
            AppendPlainText(part.parts, out);
        } else {
            out += part.text;
        }
    }
}

// The text of a word with no expansion parts, after quote removal.
std::string PlainText(const Word& word) {
    std::string out;
    AppendPlainText(word.parts, out);
    return out;
}

} // namespace

struct Parser::Impl {
    std::string source;
    Lexer lexer;
    std::string endOfInputName;

    Token peeked;
    bool hasPeek = false;
    size_t lastEnd = 0;  // the end offset of the last token taken
    // One frame per item or function definition being parsed; a heredoc
    // delimiter taken is noted on every open frame, so an enclosing item sees
    // the bodies of the constructs inside it too.
    std::vector<std::vector<std::shared_ptr<HereDocument>>> hereDocFrames;
    std::optional<ParseResult> errorResult;

    Impl(std::string sourceText, const ParserOptions& options)
        : source(sourceText),
          lexer(std::string(sourceText), LexerOptions{options.firstLine, options.interactive}),
          endOfInputName(options.endOfInputName) {}

    // -- the token stream ------------------------------------------------------

    const Token& Peek() {
        if (!hasPeek) {
            peeked = lexer.Next();
            hasPeek = true;
        }
        return peeked;
    }

    Token Take() {
        Token token = Peek();
        hasPeek = false;
        if (token.kind != TokenKind::EndOfInput) {
            lastEnd = token.end;
        }
        if (token.hereDoc) {
            for (auto& frame : hereDocFrames) {
                frame.push_back(token.hereDoc);
            }
        }
        return token;
    }

    void SkipNewlines() {
        while (Peek().kind == TokenKind::Newline) {
            Take();
        }
    }

    // -- errors ---------------------------------------------------------------

    std::string FoundText(const Token& token, bool reservedPosition) const {
        switch (token.kind) {
            case TokenKind::EndOfInput:
                return endOfInputName;
            case TokenKind::Newline:
                return "newline";
            case TokenKind::IoNumber:
                return "redirection";
            case TokenKind::Word: {
                std::optional<ReservedWord> word = AsReservedWord(token);
                if (reservedPosition && word) {
                    return "\"" + std::string(ReservedWordText(*word)) + "\"";
                }
                return "word";
            }
            default:
                if (IsRedirectionOperator(token.kind)) {
                    return "redirection";
                }
                return "\"" + std::string(OperatorText(token.kind)) + "\"";
        }
    }

    [[noreturn]] void ThrowUnexpected(const Token& found, bool reservedPosition,
                                      const char* expected = nullptr) {
        std::string message =
            "Syntax error: " + FoundText(found, reservedPosition) + " unexpected";
        if (expected != nullptr) {
            message += " (expecting " + std::string(expected) + ")";
        }
        // dash reports a found newline on the line it ends into.
        int line = found.kind == TokenKind::EndOfInput ? lexer.Line()
                 : found.kind == TokenKind::Newline    ? found.line + 1
                                                       : found.line;
        throw ShellError(message, line, found.kind == TokenKind::EndOfInput);
    }

    // Where a list of commands ends: `if a; then` ends `a`'s list at `then`.
    bool AtListEnd(const Token& token) const {
        switch (token.kind) {
            case TokenKind::EndOfInput:
            case TokenKind::RightParen:
            case TokenKind::DoubleSemicolon:
                return true;
            case TokenKind::Word: {
                std::optional<ReservedWord> word = AsReservedWord(token);
                if (!word) {
                    return false;
                }
                switch (*word) {
                    case ReservedWord::Then:
                    case ReservedWord::Else:
                    case ReservedWord::Elif:
                    case ReservedWord::Fi:
                    case ReservedWord::Do:
                    case ReservedWord::Done:
                    case ReservedWord::Esac:
                    case ReservedWord::RightBrace:
                        return true;
                    default:
                        return false;
                }
            }
            default:
                return false;
        }
    }

    bool CanStartCommand(const Token& token) const {
        return token.kind == TokenKind::Word || token.kind == TokenKind::IoNumber ||
               token.kind == TokenKind::LeftParen || IsRedirectionOperator(token.kind);
    }

    // -- source text ------------------------------------------------------------

    struct ItemMeta {
        size_t begin = 0;
        size_t end = 0;
        bool hadSeparator = false;  // the item was followed by ; or &
        std::vector<std::shared_ptr<HereDocument>> hereDocs;
    };

    // The item's (or function's) text cut from the source by token offsets,
    // with the bodies of heredocs whose bodies follow the range appended.
    std::string BuildSourceText(size_t begin, size_t end,
                                const std::vector<std::shared_ptr<HereDocument>>& hereDocs) const {
        std::string text = source.substr(begin, end - begin);
        bool appendedNewline = false;
        for (const std::shared_ptr<HereDocument>& hd : hereDocs) {
            if (hd->complete && hd->sourceBegin >= end) {
                if (!appendedNewline) {
                    text += '\n';
                    appendedNewline = true;
                }
                text += source.substr(hd->sourceBegin, hd->sourceEnd - hd->sourceBegin);
            }
        }
        return text;
    }

    // -- lists ------------------------------------------------------------------

    ListItem ParseItem(ItemMeta& meta) {
        ListItem item;
        meta.begin = Peek().begin;
        hereDocFrames.push_back({});
        item.andOr = ParseAndOr();
        meta.end = lastEnd;
        meta.hereDocs = std::move(hereDocFrames.back());
        hereDocFrames.pop_back();
        if (Peek().kind == TokenKind::Ampersand) {
            Take();
            item.background = true;
            meta.hadSeparator = true;
        } else if (Peek().kind == TokenKind::Semicolon) {
            Take();
            meta.hadSeparator = true;
        }
        return item;
    }

    void FillSourceTexts(CommandList& list, std::vector<ItemMeta>& metas) const {
        for (size_t i = 0; i < list.items.size(); ++i) {
            list.items[i].sourceText =
                BuildSourceText(metas[i].begin, metas[i].end, metas[i].hereDocs);
        }
    }

    // The body of `{ }`, `( )`, `if`, a loop or a case item: items separated
    // by `;`, `&` or newlines. Ends at end of input, `)`, `;;` or a closing
    // reserved word, which is left for the caller.
    CommandList ParseCompoundList(bool requireFirst) {
        CommandList list;
        std::vector<ItemMeta> metas;
        SkipNewlines();
        if (Peek().kind == TokenKind::EndOfInput) {
            return list;  // the caller complains with its own expectation
        }
        if (AtListEnd(Peek())) {
            if (requireFirst) {
                ThrowUnexpected(Peek(), true);
            }
            return list;
        }
        while (true) {
            if (!CanStartCommand(Peek())) {
                ThrowUnexpected(Peek(), true);
            }
            ItemMeta meta;
            list.items.push_back(ParseItem(meta));
            metas.push_back(std::move(meta));
            bool hadSeparator = metas.back().hadSeparator;
            while (Peek().kind == TokenKind::Newline) {
                Take();
                hadSeparator = true;
            }
            if (AtListEnd(Peek()) || !hadSeparator) {
                break;
            }
        }
        FillSourceTexts(list, metas);
        return list;
    }

    // A complete command, as ParseNext returns it: items joined by `;`/`&`,
    // up to the newline that ends the line or the end of input.
    CommandList ParseCompleteCommand() {
        CommandList list;
        std::vector<ItemMeta> metas;
        while (true) {
            if (!CanStartCommand(Peek())) {
                ThrowUnexpected(Peek(), true);
            }
            ItemMeta meta;
            list.items.push_back(ParseItem(meta));
            metas.push_back(std::move(meta));
            const Token& next = Peek();
            if (next.kind == TokenKind::Newline) {
                Take();
                break;
            }
            if (next.kind == TokenKind::EndOfInput) {
                break;
            }
            if (!metas.back().hadSeparator) {
                ThrowUnexpected(next, true);
            }
        }
        FillSourceTexts(list, metas);
        return list;
    }

    // -- and-or lists, pipelines -------------------------------------------------

    AndOrList ParseAndOr() {
        AndOrList andOr;
        andOr.pipelines.push_back(ParsePipeline());
        while (Peek().kind == TokenKind::AndIf || Peek().kind == TokenKind::OrIf) {
            andOr.operators.push_back(Peek().kind == TokenKind::AndIf ? AndOrOperator::And
                                                                      : AndOrOperator::Or);
            Take();
            SkipNewlines();
            andOr.pipelines.push_back(ParsePipeline());
        }
        return andOr;
    }

    Pipeline ParsePipeline() {
        Pipeline pipeline;
        if (Peek().kind == TokenKind::Word &&
            AsReservedWord(Peek()) == ReservedWord::Bang) {
            Token bang = Take();
            pipeline.negated = true;
            pipeline.line = bang.line;
        }
        pipeline.commands.push_back(ParseCommand());
        if (pipeline.line == 0) {
            pipeline.line = pipeline.commands.front()->line;
        }
        while (Peek().kind == TokenKind::Pipe) {
            Take();
            SkipNewlines();
            pipeline.commands.push_back(ParseCommand());
        }
        return pipeline;
    }

    // -- commands -----------------------------------------------------------------

    CommandPtr ParseCommand() {
        const Token& token = Peek();
        CommandPtr command;
        if (token.kind == TokenKind::Word) {
            std::optional<ReservedWord> word = AsReservedWord(token);
            if (word) {
                switch (*word) {
                    case ReservedWord::If:    command = ParseIf(); break;
                    case ReservedWord::While: command = ParseLoop(CommandKind::While); break;
                    case ReservedWord::Until: command = ParseLoop(CommandKind::Until); break;
                    case ReservedWord::For:   command = ParseFor(); break;
                    case ReservedWord::Case:  command = ParseCase(); break;
                    case ReservedWord::LeftBrace: command = ParseBraceGroup(); break;
                    default:
                        ThrowUnexpected(token, true);
                }
            } else {
                command = ParseSimpleCommand();
            }
        } else if (token.kind == TokenKind::LeftParen) {
            command = ParseSubshell();
        } else if (token.kind == TokenKind::IoNumber || IsRedirectionOperator(token.kind)) {
            command = ParseSimpleCommand();
        } else {
            ThrowUnexpected(token, true);
        }
        if (command->kind != CommandKind::Simple &&
            command->kind != CommandKind::FunctionDefinition) {
            while (Peek().kind == TokenKind::IoNumber || IsRedirectionOperator(Peek().kind)) {
                command->redirections.push_back(ParseRedirection());
            }
        }
        return command;
    }

    // The token must be the reserved word given, and is taken.
    void ExpectReserved(ReservedWord word, const char* expected) {
        const Token& token = Peek();
        if (token.kind != TokenKind::Word || AsReservedWord(token) != word) {
            ThrowUnexpected(token, true, expected);
        }
        Take();
    }

    CommandPtr ParseIf() {
        Token keyword = Take();  // if
        auto command = std::make_shared<IfCommand>();
        command->line = keyword.line;
        IfBranch first;
        first.condition = ParseCompoundList(true);
        ExpectReserved(ReservedWord::Then, "\"then\"");
        first.body = ParseCompoundList(true);
        command->branches.push_back(std::move(first));
        while (Peek().kind == TokenKind::Word && AsReservedWord(Peek()) == ReservedWord::Elif) {
            Take();
            IfBranch branch;
            branch.condition = ParseCompoundList(true);
            ExpectReserved(ReservedWord::Then, "\"then\"");
            branch.body = ParseCompoundList(true);
            command->branches.push_back(std::move(branch));
        }
        if (Peek().kind == TokenKind::Word && AsReservedWord(Peek()) == ReservedWord::Else) {
            Take();
            command->elseBody = ParseCompoundList(true);
        }
        ExpectReserved(ReservedWord::Fi, "\"fi\"");
        return command;
    }

    CommandPtr ParseLoop(CommandKind kind) {
        Token keyword = Take();  // while / until
        auto command = std::make_shared<LoopCommand>(kind);
        command->line = keyword.line;
        command->condition = ParseCompoundList(true);
        ExpectReserved(ReservedWord::Do, "\"do\"");
        command->body = ParseCompoundList(true);
        ExpectReserved(ReservedWord::Done, "\"done\"");
        return command;
    }

    CommandPtr ParseFor() {
        Token keyword = Take();  // for
        auto command = std::make_shared<ForCommand>();
        command->line = keyword.line;
        Token nameToken = Take();
        std::optional<std::string> name =
            nameToken.kind == TokenKind::Word ? LiteralText(nameToken.word) : std::nullopt;
        if (!name || !IsValidShellName(*name)) {
            throw ShellError("Syntax error: Bad for loop variable", nameToken.line);
        }
        command->variable = *name;
        SkipNewlines();
        if (Peek().kind == TokenKind::Word && AsReservedWord(Peek()) == ReservedWord::In) {
            Take();
            command->hasIn = true;
            while (true) {
                const Token& token = Peek();
                if (token.kind == TokenKind::Word) {
                    command->words.push_back(Take().word);
                } else if (token.kind == TokenKind::Semicolon) {
                    Take();
                    break;
                } else if (token.kind == TokenKind::Newline) {
                    break;  // the linebreak below takes it
                } else {
                    ThrowUnexpected(token, false);
                }
            }
        } else if (Peek().kind == TokenKind::Semicolon) {
            Take();
        }
        SkipNewlines();
        ExpectReserved(ReservedWord::Do, "\"do\"");
        command->body = ParseCompoundList(true);
        ExpectReserved(ReservedWord::Done, "\"done\"");
        return command;
    }

    CommandPtr ParseCase() {
        Token keyword = Take();  // case
        auto command = std::make_shared<CaseCommand>();
        command->line = keyword.line;
        {
            const Token& subject = Peek();
            if (subject.kind != TokenKind::Word) {
                ThrowUnexpected(subject, false, "word");
            }
            command->subject = Take().word;
        }
        SkipNewlines();
        ExpectReserved(ReservedWord::In, "\"in\"");
        bool closed = false;
        while (!closed) {
            SkipNewlines();
            if (Peek().kind == TokenKind::Word && AsReservedWord(Peek()) == ReservedWord::Esac) {
                Take();
                break;
            }
            if (Peek().kind == TokenKind::LeftParen) {
                Take();
            }
            CaseItem item;
            if (Peek().kind != TokenKind::Word) {
                ThrowUnexpected(Peek(), false, "\")\"");
            }
            item.patterns.push_back(Take().word);
            while (Peek().kind == TokenKind::Pipe) {
                Take();
                if (Peek().kind != TokenKind::Word) {
                    ThrowUnexpected(Peek(), false, "\")\"");
                }
                item.patterns.push_back(Take().word);
            }
            if (Peek().kind != TokenKind::RightParen) {
                ThrowUnexpected(Peek(), false, "\")\"");
            }
            Take();
            item.body = ParseCompoundList(false);
            if (Peek().kind == TokenKind::DoubleSemicolon) {
                Take();
            } else if (Peek().kind == TokenKind::Word &&
                       AsReservedWord(Peek()) == ReservedWord::Esac) {
                // The body list ended at `esac` (after a separator, newline or
                // with an empty body): the last item needs no `;;`, as POSIX.
                Take();
                closed = true;
            } else {
                ThrowUnexpected(Peek(), true, "\";;\"");
            }
            command->items.push_back(std::move(item));
        }
        return command;
    }

    CommandPtr ParseBraceGroup() {
        Token keyword = Take();  // {
        auto command = std::make_shared<BraceGroup>();
        command->line = keyword.line;
        command->body = ParseCompoundList(true);
        ExpectReserved(ReservedWord::RightBrace, "\"}\"");
        return command;
    }

    CommandPtr ParseSubshell() {
        Token keyword = Take();  // (
        auto command = std::make_shared<Subshell>();
        command->line = keyword.line;
        command->body = ParseCompoundList(true);
        if (Peek().kind != TokenKind::RightParen) {
            ThrowUnexpected(Peek(), true, "\")\"");
        }
        Take();
        return command;
    }

    // The word's first part is a Literal starting with "<valid name>=": the
    // length of the name, else 0 (0 is never a valid name length).
    static size_t AssignmentNameLength(const Word& word) {
        if (word.parts.empty() || word.parts.front().kind != WordPartKind::Literal) {
            return 0;
        }
        const std::string& text = word.parts.front().text;
        if (text.empty() || !IsNameStartChar(text[0])) {
            return 0;
        }
        size_t i = 1;
        while (i < text.size() && IsNameChar(text[i])) {
            ++i;
        }
        return i < text.size() && text[i] == '=' ? i : 0;
    }

    CommandPtr ParseSimpleCommand() {
        auto simple = std::make_shared<SimpleCommand>();
        Token firstWord;        // the one word, when it may name a function
        bool firstWordSet = false;
        while (true) {
            const Token& token = Peek();
            if (token.kind == TokenKind::Word) {
                if (simple->line == 0) {
                    simple->line = token.line;
                }
                size_t nameLength =
                    simple->words.empty() ? AssignmentNameLength(token.word) : 0;
                if (nameLength > 0) {
                    Token taken = Take();
                    Assignment assignment;
                    assignment.name = taken.word.parts.front().text.substr(0, nameLength);
                    assignment.value.line = taken.word.line;
                    const std::string& rest = taken.word.parts.front().text;
                    if (nameLength + 1 < rest.size()) {
                        WordPart tail;
                        tail.kind = WordPartKind::Literal;
                        tail.text = rest.substr(nameLength + 1);
                        assignment.value.parts.push_back(std::move(tail));
                    }
                    for (size_t i = 1; i < taken.word.parts.size(); ++i) {
                        assignment.value.parts.push_back(taken.word.parts[i]);
                    }
                    size_t equals = taken.word.source.find('=');
                    if (equals != std::string::npos) {
                        assignment.value.source = taken.word.source.substr(equals + 1);
                    }
                    simple->assignments.push_back(std::move(assignment));
                    continue;
                }
                Token taken = Take();
                if (!firstWordSet) {
                    firstWord = taken;
                    firstWordSet = true;
                }
                simple->words.push_back(std::move(taken.word));
                continue;
            }
            if (token.kind == TokenKind::IoNumber || IsRedirectionOperator(token.kind)) {
                if (simple->line == 0) {
                    simple->line = token.line;
                }
                simple->redirections.push_back(ParseRedirection());
                continue;
            }
            if (token.kind == TokenKind::LeftParen && simple->words.size() == 1 &&
                simple->assignments.empty() && simple->redirections.empty()) {
                return ParseFunctionDefinition(firstWord);
            }
            break;
        }
        return simple;
    }

    // The "(" and ")" after the one word, then any command as the body.
    CommandPtr ParseFunctionDefinition(const Token& nameToken) {
        Take();  // (
        if (Peek().kind != TokenKind::RightParen) {
            ThrowUnexpected(Peek(), false, "\")\"");
        }
        Take();  // )
        std::optional<std::string> name = LiteralText(nameToken.word);
        if (!name || !IsValidShellName(*name)) {
            throw ShellError("Syntax error: Bad function name", nameToken.line);
        }
        auto function = std::make_shared<FunctionDefinition>();
        function->name = *name;
        function->line = nameToken.line;
        SkipNewlines();
        hereDocFrames.push_back({});
        function->body = ParseCommand();
        size_t end = lastEnd;
        std::vector<std::shared_ptr<HereDocument>> hereDocs = std::move(hereDocFrames.back());
        hereDocFrames.pop_back();
        function->sourceText = BuildSourceText(nameToken.begin, end, hereDocs);
        return function;
    }

    Redirection ParseRedirection() {
        Redirection redirection;
        int fd = -1;
        const Token& token = Peek();
        redirection.line = token.line;
        if (token.kind == TokenKind::IoNumber) {
            fd = Take().ioNumber;
        }
        const Token& op = Peek();
        if (!IsRedirectionOperator(op.kind)) {
            ThrowUnexpected(op, false);  // the lexer never leaves an IoNumber elsewhere
        }
        Token opTaken = Take();
        redirection.kind = RedirectionKindFor(opTaken.kind);
        redirection.fd = fd >= 0 ? fd : DefaultRedirectionFd(redirection.kind);
        {
            const Token& target = Peek();
            if (target.kind != TokenKind::Word) {
                ThrowUnexpected(target, false);
            }
            Token taken = Take();
            redirection.target = std::move(taken.word);
            if (opTaken.kind == TokenKind::DoubleLess ||
                opTaken.kind == TokenKind::DoubleLessDash) {
                redirection.hereDoc = taken.hereDoc;
            }
        }
        if (redirection.kind == RedirectionKind::DupInput ||
            redirection.kind == RedirectionKind::DupOutput) {
            if (!PartsHaveExpansions(redirection.target.parts)) {
                std::string text = PlainText(redirection.target);
                bool valid = text == "-" ||
                             (text.size() == 1 && IsDigitChar(text[0]));
                if (!valid) {
                    throw ShellError("Syntax error: Bad fd number", redirection.target.line);
                }
            }
        }
        return redirection;
    }

    // -- command substitutions ---------------------------------------------------

    void CheckParts(const std::vector<WordPart>& parts) {
        for (const WordPart& part : parts) {
            switch (part.kind) {
                case WordPartKind::CommandSubstitution: {
                    ParserOptions options;
                    options.firstLine = part.line;
                    options.endOfInputName = part.backquoted ? "end of file" : "\")\"";
                    ParseResult result = ParseProgram(part.text, options);
                    if (result.status == ParseResult::Status::Error) {
                        throw ShellError(result.errorMessage, result.errorLine,
                                         result.incomplete);
                    }
                    break;
                }
                case WordPartKind::DoubleQuoted:
                case WordPartKind::Parameter:
                case WordPartKind::Arithmetic:
                    CheckParts(part.parts);
                    break;
                default:
                    break;
            }
        }
    }

    void CheckWord(const Word& word) {
        CheckParts(word.parts);
    }

    void CheckRedirections(const std::vector<Redirection>& redirections) {
        for (const Redirection& redirection : redirections) {
            CheckWord(redirection.target);
            if (redirection.hereDoc) {
                CheckWord(redirection.hereDoc->body);
            }
        }
    }

    void CheckCommand(const CommandPtr& command);

    void CheckList(const CommandList& list) {
        for (const ListItem& item : list.items) {
            for (const Pipeline& pipeline : item.andOr.pipelines) {
                for (const CommandPtr& command : pipeline.commands) {
                    CheckCommand(command);
                }
            }
        }
    }

    // -- top level -----------------------------------------------------------------

    ParseResult DoParseNext() {
        while (Peek().kind == TokenKind::Newline) {
            Take();
        }
        ParseResult result;
        if (Peek().kind == TokenKind::EndOfInput) {
            result.status = ParseResult::Status::EndOfInput;
            return result;
        }
        result.status = ParseResult::Status::Command;
        result.commands = ParseCompleteCommand();
        CheckList(result.commands);
        return result;
    }
};

void Parser::Impl::CheckCommand(const CommandPtr& command) {
    CheckRedirections(command->redirections);
    switch (command->kind) {
        case CommandKind::Simple: {
            const auto& simple = static_cast<const SimpleCommand&>(*command);
            for (const Assignment& assignment : simple.assignments) {
                CheckWord(assignment.value);
            }
            for (const Word& word : simple.words) {
                CheckWord(word);
            }
            break;
        }
        case CommandKind::BraceGroup:
            CheckList(static_cast<const BraceGroup&>(*command).body);
            break;
        case CommandKind::Subshell:
            CheckList(static_cast<const Subshell&>(*command).body);
            break;
        case CommandKind::If: {
            const auto& ifCommand = static_cast<const IfCommand&>(*command);
            for (const IfBranch& branch : ifCommand.branches) {
                CheckList(branch.condition);
                CheckList(branch.body);
            }
            if (ifCommand.elseBody) {
                CheckList(*ifCommand.elseBody);
            }
            break;
        }
        case CommandKind::While:
        case CommandKind::Until: {
            const auto& loop = static_cast<const LoopCommand&>(*command);
            CheckList(loop.condition);
            CheckList(loop.body);
            break;
        }
        case CommandKind::For: {
            const auto& forCommand = static_cast<const ForCommand&>(*command);
            for (const Word& word : forCommand.words) {
                CheckWord(word);
            }
            CheckList(forCommand.body);
            break;
        }
        case CommandKind::Case: {
            const auto& caseCommand = static_cast<const CaseCommand&>(*command);
            CheckWord(caseCommand.subject);
            for (const CaseItem& item : caseCommand.items) {
                for (const Word& pattern : item.patterns) {
                    CheckWord(pattern);
                }
                CheckList(item.body);
            }
            break;
        }
        case CommandKind::FunctionDefinition:
            CheckCommand(static_cast<const FunctionDefinition&>(*command).body);
            break;
    }
}

Parser::Parser(std::string source, ParserOptions options)
    : m_impl(std::make_unique<Impl>(std::move(source), options)) {}

Parser::Parser(Parser&&) noexcept = default;

Parser& Parser::operator=(Parser&&) noexcept = default;

Parser::~Parser() = default;

ParseResult Parser::ParseNext() {
    if (m_impl->errorResult) {
        return *m_impl->errorResult;
    }
    try {
        return m_impl->DoParseNext();
    } catch (const ShellError& e) {
        ParseResult result;
        result.status = ParseResult::Status::Error;
        result.errorMessage = e.what();
        result.errorLine = e.Line();
        result.incomplete = e.Incomplete();
        m_impl->errorResult = result;
        return result;
    }
}

ParseResult ParseProgram(const std::string& source, ParserOptions options) {
    Parser parser(std::string(source), options);
    CommandList all;
    while (true) {
        ParseResult result = parser.ParseNext();
        if (result.status == ParseResult::Status::EndOfInput) {
            break;
        }
        if (result.status == ParseResult::Status::Error) {
            return result;
        }
        for (ListItem& item : result.commands.items) {
            all.items.push_back(std::move(item));
        }
    }
    ParseResult result;
    result.status = ParseResult::Status::Command;
    result.commands = std::move(all);
    return result;
}

} // namespace Haisos::Hsh
