#include "commands/jq/JqParser.h"

#include <limits>
#include <utility>

#include "commands/jq/JqLexer.h"

namespace Haisos::Jq {
namespace {

// The offset jq's top-level-not-given error carries: it is printed without
// any location.
constexpr size_t kNoOffset = std::numeric_limits<size_t>::max();

// Nesting (open brackets and constructs) is refused past this depth, a
// documented exception: jq has no such limit.
constexpr int kMaxDepth = 256;

// Stops parsing at the first error; the CompileError is already recorded.
struct StopParsing {};

// An open 'if' or 'try' the parser is inside, kept for jq's unterminated
// notes.
struct Construct {
    bool isTry = false;
    size_t offset = 0;       // the 'if' / 'try' itself
    size_t thenOffset = 0;   // 'if': the 'then'; 0 before it
    size_t catchFirst = 0;   // 'try': where the catch body should start
};

// The recursive-descent parser of jq programs, one token of lookahead: the
// levels below ParsePipe mirror the language's precedence, lowest first.
// The first syntax error stops parsing (jq can find several; reporting only
// the first is a documented exception).
class Parser {
public:
    explicit Parser(std::string_view program)
        : m_lexer(program) {
        // The offset of each line's first byte, for $__loc__ and errors.
        m_lineStarts.push_back(0);
        for (size_t i = 0; i < program.size(); ++i)
            if (program[i] == '\n')
                m_lineStarts.push_back(i + 1);
    }

    void Run(ParseResult& result);

private:
    // --- error handling ---
    // Records |message| at |offset| (jq's padding rules apply), then jq's
    // unterminated-if/try note when one is open there, and stops parsing.
    // A |noteOffset| instead adds jq's object-key note there.
    [[noreturn]] void StopWith(const std::string& message, size_t offset,
                               size_t noteOffset = kNoOffset,
                               bool withNotes = true);
    [[noreturn]] void Fail(const Token& token, const char* expecting,
                           size_t noteOffset = kNoOffset);
    void AddUnterminatedNote(size_t errorOffset);
    // Where jq places an error: a real token at its begin, an end of input
    // at the begin of the last token before it.
    size_t EffectiveOffset(const Token& token) const;
    int LineOf(size_t offset) const;
    // The 'expecting' of a token that cannot start an expression: at the
    // very start of the top-level program jq says "end of file".
    const char* StartExpecting() const { return m_atTopStart ? "end of file" : nullptr; }

    // --- tokens ---
    void Advance() {
        m_previous = m_token;
        m_token = m_lexer.Next();
        m_atTopStart = false;
    }
    bool IsChar(char c) const {
        return m_token.type == TokenType::Char && m_token.text.size() == 1 &&
               m_token.text[0] == c;
    }
    bool IsOp(const char* spelling) const {
        return m_token.type == TokenType::Operator && m_token.text == spelling;
    }
    bool IsKeyword(const char* spelling) const {
        return m_token.type == TokenType::Keyword && m_token.text == spelling;
    }
    bool IsAssignOp() const {
        if (IsChar('=')) return true;
        return m_token.type == TokenType::Operator &&
               (m_token.text == "|=" || m_token.text == "+=" ||
                m_token.text == "-=" || m_token.text == "*=" ||
                m_token.text == "/=" || m_token.text == "%=" ||
                m_token.text == "//=");
    }
    bool IsCompareOp() const {
        if (IsChar('<') || IsChar('>')) return true;
        return m_token.type == TokenType::Operator &&
               (m_token.text == "==" || m_token.text == "!=" ||
                m_token.text == "<=" || m_token.text == ">=");
    }

    // --- depth and brackets ---
    // A construct's nesting: one level per open bracket or construct.
    class Depth {
    public:
        explicit Depth(Parser& parser, const Token& at) : m_parser(parser) {
            if (++parser.m_depth > kMaxDepth)
                parser.Fail(at, nullptr);
        }
        ~Depth() { --m_parser.m_depth; }

    private:
        Parser& m_parser;
    };
    friend class Depth;
    void PushBracket(size_t begin) { m_brackets.push_back(begin); }
    void PopBracket() { m_brackets.pop_back(); }

    // --- nodes ---
    std::unique_ptr<Node> MakeNode(NodeType type, size_t begin) {
        auto node = std::make_unique<Node>();
        node->type = type;
        node->begin = begin;
        node->end = m_previous.end;
        node->line = LineOf(begin);
        return node;
    }
    std::unique_ptr<Node> MakeBinaryNode(NodeType type, const std::string& text,
                                         std::unique_ptr<Node> left,
                                         std::unique_ptr<Node> right) {
        auto node = MakeNode(type, left->begin);
        node->text = text;
        node->children.push_back(std::move(left));
        node->children.push_back(std::move(right));
        return node;
    }

    // --- the grammar's levels (lowest first) ---
    std::unique_ptr<Node> ParsePipe();
    // A pipe context, closing its 'as' bindings without recursion: comma
    // segments, '|' between them, until a segment opens no binding and no
    // '|' follows; the bindings opened inside are folded in at the end (a
    // binding's body is the rest of the context it was opened in). |first|
    // is a try's body or catch already parsed (null for a whole pipe
    // expression), with |firstOpenBefore| the binding count before it was.
    std::unique_ptr<Node> ParsePipeSegments(std::unique_ptr<Node> first,
                                            size_t firstOpenBefore);
    std::unique_ptr<Node> ParseComma();
    std::unique_ptr<Node> ParseAlt();
    std::unique_ptr<Node> ParseAssign();
    std::unique_ptr<Node> ParseOr();
    std::unique_ptr<Node> ParseAnd();
    std::unique_ptr<Node> ParseCompare();
    std::unique_ptr<Node> ParseAdd();
    std::unique_ptr<Node> ParseMul();
    // reduce, foreach, if, try, def, label, negation and 'as' bindings;
    // |inTryBody| passes the try body its restricted '?' (see ParsePostfix).
    std::unique_ptr<Node> ParseUnary(bool inTryBody);
    std::unique_ptr<Node> ParseReduce(const Token& at);
    std::unique_ptr<Node> ParseForeach(const Token& at);
    std::unique_ptr<Node> ParseIf(const Token& at);
    std::unique_ptr<Node> ParseTry(const Token& at);
    std::unique_ptr<Node> ParseLabel(const Token& at);
    // Suffixes on a term. |restricted| is the object-value / try-body form:
    // a '?' is taken only directly after a suffix, once -- anywhere else it
    // is left for the caller to reject.
    std::unique_ptr<Node> ParsePostfix(bool restricted);
    // |endsWithSuffix| (when not null) is set true by the forms that end
    // with a suffix -- a field, ."s" and .@fmt "s" -- which is where a
    // restricted '?' may follow.
    std::unique_ptr<Node> ParsePrimary(bool* endsWithSuffix = nullptr);
    std::unique_ptr<Node> ParseCallRest(const Token& name);
    // The index and slice suffix '[...]', the '[' the current token.
    std::unique_ptr<Node> ParseBracketSuffix(std::unique_ptr<Node> target);
    // A string literal, the current token being its StringStart: its parts
    // and interpolations, with |format| ("" for none) as its format.
    std::unique_ptr<Node> ParseStringNode(const Token& format,
                                           const Token& start);
    std::unique_ptr<Node> ParseObject(const Token& open);
    std::unique_ptr<Node> ParseObjectValue();
    std::unique_ptr<Node> ParseObjectValueTerm();
    std::vector<Pattern> ParseDestructuring();
    Pattern ParseDestructure();
    void ParsePatternEntry(Pattern& pattern);
    void ParseDefinition(std::vector<FunctionDefinition>& definitions);
    std::unique_ptr<Node> CloneNode(const Node& node);
    void ClonePattern(Pattern& into, const Pattern& from);

    Lexer m_lexer;
    Token m_token;
    Token m_previous;
    std::vector<size_t> m_lineStarts;
    std::vector<size_t> m_brackets;   // open brackets, by begin offset
    std::vector<Construct> m_constructs;
    std::vector<CompileError> m_errors;
    // The 'as' bindings opened inside the current pipe context, outermost
    // first; each stays open (its body unfilled) until its context folds it
    // in. The operator loops below stop the moment a binding opens, so at
    // most one opens per comma segment.
    std::vector<Node*> m_openBindings;
    int m_depth = 0;
    int m_pipeDepth = 0;
    bool m_atTopStart = false;
};

size_t Parser::EffectiveOffset(const Token& token) const {
    return token.type == TokenType::End ? m_previous.begin : token.begin;
}

int Parser::LineOf(size_t offset) const {
    int line = 1;
    for (size_t start : m_lineStarts) {
        if (start > offset) break;
        ++line;
    }
    return line - 1 < 1 ? 1 : line - 1;
}

void Parser::AddUnterminatedNote(size_t errorOffset) {
    for (auto it = m_constructs.rbegin(); it != m_constructs.rend(); ++it) {
        if (it->isTry) {
            if (it->catchFirst != kNoOffset && errorOffset == it->catchFirst) {
                m_errors.push_back(
                    {"Possibly unterminated 'try' statement", it->offset});
                return;
            }
        } else if (it->thenOffset != 0 &&
                   (m_brackets.empty() || m_brackets.back() < it->thenOffset)) {
            m_errors.push_back(
                {"Possibly unterminated 'if' statement", it->offset});
            return;
        }
    }
}

void Parser::StopWith(const std::string& message, size_t offset,
                      size_t noteOffset, bool withNotes) {
    m_errors.push_back({message, offset});
    if (noteOffset != kNoOffset)
        m_errors.push_back(
            {"May need parentheses around object key expression", noteOffset});
    else if (withNotes)
        AddUnterminatedNote(offset);
    throw StopParsing{};
}

void Parser::Fail(const Token& token, const char* expecting,
                  size_t noteOffset) {
    std::string message = "syntax error, unexpected ";
    message += TokenNameInMessage(token);
    if (expecting) {
        message += ", expecting ";
        message += expecting;
    }
    message += " (Unix shell quoting issues?)";
    StopWith(message, EffectiveOffset(token), noteOffset);
}

// The lowest level: 'def' chains, then the right-associative '|'. A 'def'
// is scoped over everything that follows it, so the scope of a definition
// chain is the whole rest of the program, parsed here again.
std::unique_ptr<Node> Parser::ParsePipe() {
    ++m_pipeDepth;
    std::unique_ptr<Node> result;
    if (IsKeyword("def")) {
        std::vector<FunctionDefinition> definitions;
        while (IsKeyword("def"))
            ParseDefinition(definitions);
        // After a top-level definition, jq's "expecting end of file" for a
        // token that cannot start an expression applies again -- and a
        // definition chain with nothing after it is jq's top-level-not-given
        // error, not a syntax error.
        if (m_pipeDepth == 1) {
            m_atTopStart = true;
            if (m_token.type == TokenType::End) {
                m_errors.push_back({"Top-level program not given (try \".\")",
                                    kNoOffset});
                throw StopParsing{};
            }
        }
        auto scope = ParsePipe();
        auto node = MakeNode(NodeType::Defs, definitions.front().begin);
        node->definitions = std::move(definitions);
        node->children.push_back(std::move(scope));
        result = std::move(node);
    } else {
        result = ParsePipeSegments(nullptr, 0);
    }
    --m_pipeDepth;
    return result;
}

// A pipe context's segments and how each continues into the next: a '|', or
// a binding opened inside it (its body is what follows).
std::unique_ptr<Node> Parser::ParsePipeSegments(std::unique_ptr<Node> first,
                                                size_t firstOpenBefore) {
    struct Segment {
        std::unique_ptr<Node> node;
        size_t openBefore;  // the binding count when the segment started
        bool joinBinding;   // a binding opened inside it
    };
    std::vector<Segment> segments;
    if (first) {
        segments.push_back({std::move(first), firstOpenBefore, false});
    } else {
        const size_t openBefore = m_openBindings.size();
        segments.push_back({ParseComma(), openBefore, false});
    }
    for (;;) {
        Segment& last = segments.back();
        last.joinBinding = m_openBindings.size() > last.openBefore;
        if (!last.joinBinding) {
            if (!IsChar('|'))
                break;
            Advance();
        }
        const size_t openBefore = m_openBindings.size();
        segments.push_back({ParseComma(), openBefore, false});
    }
    // Fold right to left: '|' joins jq's right-associative way, and a
    // binding segment's Bind -- only one can have opened in it -- takes the
    // folded rest as its body, so a chain of bindings costs no recursion.
    std::unique_ptr<Node> result = std::move(segments.back().node);
    segments.pop_back();
    while (!segments.empty()) {
        Segment& segment = segments.back();
        if (segment.joinBinding) {
            Node* bind = m_openBindings[segment.openBefore];
            m_openBindings.erase(m_openBindings.begin() +
                                  static_cast<std::ptrdiff_t>(segment.openBefore));
            bind->children.push_back(std::move(result));
            result = std::move(segment.node);
        } else {
            auto pipe = MakeNode(NodeType::Pipe, segment.node->begin);
            pipe->children.push_back(std::move(segment.node));
            pipe->children.push_back(std::move(result));
            result = std::move(pipe);
        }
        segments.pop_back();
    }
    return result;
}

std::unique_ptr<Node> Parser::ParseComma() {
    const size_t bindings = m_openBindings.size();
    auto left = ParseAlt();
    while (m_openBindings.size() == bindings && IsChar(',')) {
        Advance();
        auto right = ParseAlt();
        left = MakeBinaryNode(NodeType::Comma, ",", std::move(left),
                              std::move(right));
    }
    return left;
}

// '//', right-associative, folded iteratively.
std::unique_ptr<Node> Parser::ParseAlt() {
    const size_t bindings = m_openBindings.size();
    std::vector<std::unique_ptr<Node>> parts;
    parts.push_back(ParseAssign());
    while (m_openBindings.size() == bindings && IsOp("//")) {
        Advance();
        parts.push_back(ParseAssign());
    }
    auto result = std::move(parts.back());
    parts.pop_back();
    while (!parts.empty()) {
        auto alt = MakeNode(NodeType::Alternative, parts.back()->begin);
        alt->children.push_back(std::move(parts.back()));
        parts.pop_back();
        alt->children.push_back(std::move(result));
        result = std::move(alt);
    }
    return result;
}

// The assignment operators bind looser than '//'; a second one is jq's
// error, with nothing expected.
std::unique_ptr<Node> Parser::ParseAssign() {
    const size_t bindings = m_openBindings.size();
    auto left = ParseOr();
    if (!IsAssignOp() || m_openBindings.size() != bindings)
        return left;
    const Token op = m_token;
    Advance();
    auto right = ParseOr();
    if (IsAssignOp() && m_openBindings.size() == bindings)
        Fail(m_token, nullptr);
    return MakeBinaryNode(NodeType::Assign, op.text, std::move(left),
                          std::move(right));
}

std::unique_ptr<Node> Parser::ParseOr() {
    const size_t bindings = m_openBindings.size();
    auto left = ParseAnd();
    while (m_openBindings.size() == bindings && IsKeyword("or")) {
        Advance();
        auto right = ParseAnd();
        left = MakeBinaryNode(NodeType::Or, "or", std::move(left),
                              std::move(right));
    }
    return left;
}

std::unique_ptr<Node> Parser::ParseAnd() {
    const size_t bindings = m_openBindings.size();
    auto left = ParseCompare();
    while (m_openBindings.size() == bindings && IsKeyword("and")) {
        Advance();
        auto right = ParseCompare();
        left = MakeBinaryNode(NodeType::And, "and", std::move(left),
                              std::move(right));
    }
    return left;
}

// Non-associative: '1 == 2 == 3' is jq's error at the second '=='.
std::unique_ptr<Node> Parser::ParseCompare() {
    const size_t bindings = m_openBindings.size();
    auto left = ParseAdd();
    if (!IsCompareOp() || m_openBindings.size() != bindings)
        return left;
    const Token op = m_token;
    Advance();
    auto right = ParseAdd();
    if (IsCompareOp() && m_openBindings.size() == bindings)
        Fail(m_token, nullptr);
    return MakeBinaryNode(NodeType::Binary, op.text, std::move(left),
                          std::move(right));
}

std::unique_ptr<Node> Parser::ParseAdd() {
    const size_t bindings = m_openBindings.size();
    auto left = ParseMul();
    while (m_openBindings.size() == bindings &&
           (IsChar('+') || IsChar('-'))) {
        const Token op = m_token;
        Advance();
        auto right = ParseMul();
        left = MakeBinaryNode(NodeType::Binary, op.text, std::move(left),
                              std::move(right));
    }
    return left;
}

std::unique_ptr<Node> Parser::ParseMul() {
    const size_t bindings = m_openBindings.size();
    auto left = ParseUnary(false);
    while (m_openBindings.size() == bindings &&
           (IsChar('*') || IsChar('/') || IsChar('%'))) {
        const Token op = m_token;
        Advance();
        auto right = ParseUnary(false);
        left = MakeBinaryNode(NodeType::Binary, op.text, std::move(left),
                              std::move(right));
    }
    return left;
}

// The unary level: negation, reduce, foreach, if, try, def, label -- and
// anything else, a postfix term with an optional 'as' binding over the whole
// rest of the program.
std::unique_ptr<Node> Parser::ParseUnary(bool inTryBody) {
    const Token at = m_token;
    if (IsChar('-')) {
        // The operand is a whole product, left-associative: -1 * 3 is
        // -(1 * 3).
        Depth depth(*this, at);
        Advance();
        auto node = MakeNode(NodeType::Negate, at.begin);
        node->children.push_back(ParseMul());
        return node;
    }
    if (IsKeyword("reduce"))
        return ParseReduce(at);
    if (IsKeyword("foreach"))
        return ParseForeach(at);
    if (IsKeyword("if"))
        return ParseIf(at);
    if (IsKeyword("try"))
        return ParseTry(at);
    if (IsKeyword("def")) {
        Depth depth(*this, at);
        std::vector<FunctionDefinition> definitions;
        ParseDefinition(definitions);
        auto node = MakeNode(NodeType::Defs, at.begin);
        node->definitions = std::move(definitions);
        node->children.push_back(ParsePipe());
        return node;
    }
    if (IsKeyword("label"))
        return ParseLabel(at);
    if (IsKeyword("import") || IsKeyword("include") ||
        IsKeyword("module") || IsKeyword("as") || IsKeyword("and") ||
        IsKeyword("or") || IsKeyword("then") || IsKeyword("elif") ||
        IsKeyword("else") || IsKeyword("end") || IsKeyword("catch")) {
        // A keyword that starts nothing here. Modules are not implemented:
        // import/include/module are syntax errors wherever they appear.
        Fail(m_token, StartExpecting());
    }

    auto node = ParsePostfix(inTryBody);
    if (IsKeyword("as")) {
        Advance();
        auto bind = MakeNode(NodeType::Bind, at.begin);
        bind->children.push_back(std::move(node));
        {
            // The binding's own nesting: its patterns alone -- the body
            // costs no recursion, the pipe context that owns it parses the
            // rest and folds it in (ParsePipeSegments).
            Depth depth(*this, at);
            bind->patterns = ParseDestructuring();
        }
        if (!IsChar('|'))
            Fail(m_token, "'|'");
        Advance();
        m_openBindings.push_back(bind.get());
        return bind;
    }
    return node;
}

std::unique_ptr<Node> Parser::ParseReduce(const Token& at) {
    Depth depth(*this, at);
    Advance();
    auto node = MakeNode(NodeType::Reduce, at.begin);
    node->children.push_back(ParsePostfix(false));
    if (!IsKeyword("as")) {
        // jq expects nothing after a lone '.' there, the postfix forms
        // otherwise.
        if (node->children[0]->type == NodeType::Identity)
            Fail(m_token, nullptr);
        Fail(m_token, "FIELD or as or '.' or '['");
    }
    Advance();
    node->patterns = ParseDestructuring();
    if (!IsChar('('))
        Fail(m_token, "'('");
    const size_t open = m_token.begin;
    Advance();
    PushBracket(open);
    node->children.push_back(ParsePipe());
    if (!IsChar(';'))
        Fail(m_token, nullptr);
    Advance();
    node->children.push_back(ParsePipe());
    if (!IsChar(')'))
        Fail(m_token, nullptr);
    Advance();
    PopBracket();
    while (IsChar('?')) {
        Advance();
        auto wrapped = MakeNode(NodeType::Try, node->begin);
        wrapped->children.push_back(std::move(node));
        node = std::move(wrapped);
    }
    return node;
}

std::unique_ptr<Node> Parser::ParseForeach(const Token& at) {
    Depth depth(*this, at);
    Advance();
    auto node = MakeNode(NodeType::Foreach, at.begin);
    node->children.push_back(ParsePostfix(false));
    if (!IsKeyword("as")) {
        // As reduce's: nothing expected after a lone '.'.
        if (node->children[0]->type == NodeType::Identity)
            Fail(m_token, nullptr);
        Fail(m_token, "FIELD or as or '.' or '['");
    }
    Advance();
    node->patterns = ParseDestructuring();
    if (!IsChar('('))
        Fail(m_token, "'('");
    const size_t open = m_token.begin;
    Advance();
    PushBracket(open);
    node->children.push_back(ParsePipe());
    if (!IsChar(';'))
        Fail(m_token, nullptr);
    Advance();
    node->children.push_back(ParsePipe());
    if (IsChar(';')) {
        Advance();
        node->children.push_back(ParsePipe());
    }
    if (!IsChar(')'))
        Fail(m_token, nullptr);
    Advance();
    PopBracket();
    while (IsChar('?')) {
        Advance();
        auto wrapped = MakeNode(NodeType::Try, node->begin);
        wrapped->children.push_back(std::move(node));
        node = std::move(wrapped);
    }
    return node;
}

std::unique_ptr<Node> Parser::ParseIf(const Token& at) {
    Depth depth(*this, at);
    Advance();
    Construct construct;
    construct.isTry = false;
    construct.offset = at.begin;
    m_constructs.push_back(construct);
    auto node = MakeNode(NodeType::If, at.begin);
    node->children.push_back(ParsePipe());
    if (!IsKeyword("then"))
        Fail(m_token, nullptr);
    Advance();
    m_constructs.back().thenOffset = m_previous.begin;
    node->children.push_back(ParsePipe());
    while (IsKeyword("elif")) {
        Advance();
        node->children.push_back(ParsePipe());
        if (!IsKeyword("then"))
            Fail(m_token, nullptr);
        Advance();
        node->children.push_back(ParsePipe());
    }
    if (IsKeyword("else")) {
        Advance();
        node->children.push_back(ParsePipe());
        node->hasElse = true;
    }
    if (!IsKeyword("end"))
        Fail(m_token, nullptr);
    Advance();
    m_constructs.pop_back();
    while (IsChar('?')) {
        Advance();
        auto wrapped = MakeNode(NodeType::Try, node->begin);
        wrapped->children.push_back(std::move(node));
        node = std::move(wrapped);
    }
    return node;
}

std::unique_ptr<Node> Parser::ParseTry(const Token& at) {
    Depth depth(*this, at);
    Advance();
    Construct construct;
    construct.isTry = true;
    construct.offset = at.begin;
    construct.catchFirst = kNoOffset;
    m_constructs.push_back(construct);
    auto node = MakeNode(NodeType::Try, at.begin);
    const size_t openBefore = m_openBindings.size();
    auto body = ParseUnary(true);
    // A binding opened in the body owns the rest of the try's pipe context
    // (try . as $x | $x catch .).
    if (m_openBindings.size() > openBefore)
        body = ParsePipeSegments(std::move(body), openBefore);
    node->children.push_back(std::move(body));
    if (IsChar('?')) {
        // The '?' belongs to the try itself: a catch cannot follow it.
        m_constructs.pop_back();
        while (IsChar('?')) {
            Advance();
            auto wrapped = MakeNode(NodeType::Try, node->begin);
            wrapped->children.push_back(std::move(node));
            node = std::move(wrapped);
        }
        return node;
    }
    if (IsKeyword("catch")) {
        Advance();
        m_constructs.back().catchFirst = EffectiveOffset(m_token);
        const size_t catchBefore = m_openBindings.size();
        auto catchBody = ParseUnary(false);
        if (m_openBindings.size() > catchBefore)
            catchBody = ParsePipeSegments(std::move(catchBody), catchBefore);
        node->children.push_back(std::move(catchBody));
        node->hasCatch = true;
    }
    m_constructs.pop_back();
    return node;
}

std::unique_ptr<Node> Parser::ParseLabel(const Token& at) {
    Depth depth(*this, at);
    Advance();
    if (m_token.type != TokenType::Variable)
        Fail(m_token, "BINDING");
    auto node = MakeNode(NodeType::Label, at.begin);
    node->text = m_token.text;
    Advance();
    if (!IsChar('|'))
        Fail(m_token, "'|'");
    Advance();
    node->children.push_back(ParsePipe());
    return node;
}

// A string key for an index: what follows '.' or a bare '.' itself, taken
// from |m_token|, a StringStart. |format| is the format name, "" for none.
std::unique_ptr<Node> Parser::ParseStringNode(const Token& format,
                                              const Token& start) {
    Advance();  // the StringStart
    auto node = MakeNode(NodeType::String, start.begin);
    node->text = format.text;
    std::string part;
    for (;;) {
        if (m_token.type == TokenType::StringText) {
            part += m_token.text;
            Advance();
        } else if (m_token.type == TokenType::InterpolationStart) {
            // An interpolation nests a whole program: one level deeper.
            Depth depth(*this, m_token);
            node->stringParts.push_back(part);
            part.clear();
            Advance();
            node->children.push_back(ParsePipe());
            if (m_token.type != TokenType::InterpolationEnd)
                Fail(m_token, nullptr);
            Advance();
        } else if (m_token.type == TokenType::StringEnd) {
            Advance();
            node->stringParts.push_back(part);
            return node;
        } else if (m_token.type == TokenType::Invalid) {
            // A bad escape: jq's message, at the run's first '\'.
            StopWith(m_lexer.Error(), m_token.begin, kNoOffset, false);
        } else {
            Fail(m_token,
                 "QQSTRING_TEXT or QQSTRING_INTERP_START or QQSTRING_END");
        }
    }
}

// The index and slice suffix '[...]' of a term, the '[' the current token.
std::unique_ptr<Node> Parser::ParseBracketSuffix(std::unique_ptr<Node> target) {
    Depth depth(*this, m_token);
    const size_t open = m_token.begin;
    Advance();
    PushBracket(open);
    auto node = MakeNode(NodeType::Index, target->begin);
    if (IsChar(']')) {
        Advance();
        PopBracket();
        auto iterate = MakeNode(NodeType::Iterate, target->begin);
        iterate->children.push_back(std::move(target));
        return iterate;
    }
    std::unique_ptr<Node> from, to;
    bool isSlice = false;
    if (IsChar(':')) {
        isSlice = true;
        Advance();
        to = ParsePipe();
    } else {
        from = ParsePipe();
        if (IsChar(':')) {
            isSlice = true;
            Advance();
            if (!IsChar(']'))
                to = ParsePipe();
        }
    }
    if (!IsChar(']'))
        Fail(m_token, nullptr);
    Advance();
    PopBracket();
    if (isSlice) {
        auto slice = MakeNode(NodeType::Slice, target->begin);
        slice->children.push_back(std::move(target));
        slice->children.push_back(std::move(from));
        slice->children.push_back(std::move(to));
        return slice;
    }
    node->children.push_back(std::move(target));
    node->children.push_back(std::move(from));
    return node;
}

std::unique_ptr<Node> Parser::ParsePostfix(bool restricted) {
    bool afterSuffix = false;
    auto node = ParsePrimary(&afterSuffix);
    for (;;) {
        if (m_token.type == TokenType::Field) {
            auto key = MakeNode(NodeType::String, m_token.begin);
            key->stringParts.push_back(m_token.text);
            const Token field = m_token;
            Advance();
            auto index = MakeNode(NodeType::Index, node->begin);
            index->children.push_back(std::move(node));
            index->children.push_back(std::move(key));
            index->end = field.end;
            node = std::move(index);
            afterSuffix = true;
            continue;
        }
        if (IsChar('.')) {
            Advance();
            if (m_token.type == TokenType::StringStart) {
                Token empty;  // no format
                auto key = ParseStringNode(empty, m_token);
                auto index = MakeNode(NodeType::Index, node->begin);
                index->children.push_back(std::move(node));
                index->children.push_back(std::move(key));
                index->end = m_previous.end;
                node = std::move(index);
                afterSuffix = true;
                continue;
            }
            if (m_token.type == TokenType::Format) {
                const Token format = m_token;
                Advance();
                if (m_token.type != TokenType::StringStart)
                    Fail(m_token, "QQSTRING_START");
                auto key = ParseStringNode(format, m_token);
                auto index = MakeNode(NodeType::Index, node->begin);
                index->children.push_back(std::move(node));
                index->children.push_back(std::move(key));
                index->end = m_previous.end;
                node = std::move(index);
                afterSuffix = true;
                continue;
            }
            if (IsChar('[')) {
                node = ParseBracketSuffix(std::move(node));
                afterSuffix = true;
                continue;
            }
            Fail(m_token, "FORMAT or QQSTRING_START or '['");
        }
        if (IsChar('[')) {
            node = ParseBracketSuffix(std::move(node));
            afterSuffix = true;
            continue;
        }
        if (IsChar('?')) {
            if (restricted && !afterSuffix)
                break;  // the object loop or try body rejects it
            Advance();
            auto wrapped = MakeNode(NodeType::Try, node->begin);
            wrapped->children.push_back(std::move(node));
            node = std::move(wrapped);
            if (restricted)
                afterSuffix = false;  // once per suffix, jq's way
            continue;
        }
        break;
    }
    return node;
}

std::unique_ptr<Node> Parser::ParsePrimary(bool* endsWithSuffix) {
    const Token at = m_token;
    switch (m_token.type) {
        case TokenType::Char:
            if (m_token.text == ".") {
                Advance();
                if (m_token.type == TokenType::StringStart) {
                    Token empty;  // no format
                    auto key = ParseStringNode(empty, m_token);
                    auto index = MakeNode(NodeType::Index, at.begin);
                    index->children.push_back(MakeNode(NodeType::Identity, at.begin));
                    index->children.push_back(std::move(key));
                    if (endsWithSuffix) *endsWithSuffix = true;
                    return index;
                }
                if (m_token.type == TokenType::Format) {
                    const Token format = m_token;
                    Advance();
                    if (m_token.type != TokenType::StringStart)
                        Fail(m_token, "QQSTRING_START");
                    auto key = ParseStringNode(format, m_token);
                    auto index = MakeNode(NodeType::Index, at.begin);
                    index->children.push_back(MakeNode(NodeType::Identity, at.begin));
                    index->children.push_back(std::move(key));
                    if (endsWithSuffix) *endsWithSuffix = true;
                    return index;
                }
                return MakeNode(NodeType::Identity, at.begin);
            }
            if (m_token.text == "$") {
                // A '$' with no name after it.
                Advance();
                Fail(m_token, "'$'");
            }
            if (m_token.text == "(") {
                Depth depth(*this, at);
                Advance();
                PushBracket(at.begin);
                auto inner = ParsePipe();
                if (!IsChar(')'))
                    Fail(m_token, nullptr);
                Advance();
                PopBracket();
                return inner;
            }
            if (m_token.text == "[") {
                Depth depth(*this, at);
                Advance();
                PushBracket(at.begin);
                if (IsChar(']')) {
                    Advance();
                    PopBracket();
                    return MakeNode(NodeType::Array, at.begin);
                }
                auto body = ParsePipe();
                if (!IsChar(']'))
                    Fail(m_token, nullptr);
                Advance();
                PopBracket();
                auto array = MakeNode(NodeType::Array, at.begin);
                array->children.push_back(std::move(body));
                return array;
            }
            if (m_token.text == "{")
                return ParseObject(at);
            break;
        case TokenType::Operator:
            if (m_token.text == "..") {
                Advance();
                return MakeNode(NodeType::RecurseDefault, at.begin);
            }
            break;
        case TokenType::Field: {
            auto key = MakeNode(NodeType::String, at.begin);
            key->stringParts.push_back(at.text);
            Advance();
            auto index = MakeNode(NodeType::Index, at.begin);
            index->children.push_back(MakeNode(NodeType::Identity, at.begin));
            index->children.push_back(std::move(key));
            if (endsWithSuffix) *endsWithSuffix = true;
            return index;
        }
        case TokenType::Number: {
            auto node = MakeNode(NodeType::Literal, at.begin);
            node->text = at.text;
            Advance();
            return node;
        }
        case TokenType::StringStart: {
            Token empty;  // no format
            return ParseStringNode(empty, m_token);
        }
        case TokenType::Format: {
            const Token format = m_token;
            Advance();
            if (m_token.type != TokenType::StringStart) {
                auto node = MakeNode(NodeType::Format, at.begin);
                node->text = format.text;
                return node;
            }
            return ParseStringNode(format, m_token);
        }
        case TokenType::Variable: {
            auto node = MakeNode(NodeType::Variable, at.begin);
            node->text = at.text;
            Advance();
            return node;
        }
        case TokenType::LocVariable: {
            auto node = MakeNode(NodeType::Loc, at.begin);
            node->line = LineOf(at.begin);
            Advance();
            return node;
        }
        case TokenType::Keyword:
            if (m_token.text == "break") {
                Advance();
                if (m_token.type != TokenType::Variable)
                    Fail(m_token, "BINDING");
                auto node = MakeNode(NodeType::Break, at.begin);
                node->text = m_token.text;
                Advance();
                return node;
            }
            break;
        case TokenType::Identifier:
            return ParseCallRest(at);
        default:
            break;
    }
    Fail(m_token, StartExpecting());
}

// A function call (or a plain name) after an identifier: the arguments, if
// '(' follows.
std::unique_ptr<Node> Parser::ParseCallRest(const Token& name) {
    Advance();
    auto node = MakeNode(NodeType::Call, name.begin);
    node->text = name.text;
    if (!IsChar('(')) {
        // true, false and null with no arguments are literals.
        if (node->text == "true" || node->text == "false" ||
            node->text == "null") {
            node->type = NodeType::Literal;
            return node;
        }
        return node;
    }
    Depth depth(*this, m_token);
    const size_t open = m_token.begin;
    Advance();
    PushBracket(open);
    node->children.push_back(ParsePipe());
    while (IsChar(';')) {
        Advance();
        node->children.push_back(ParsePipe());
    }
    if (!IsChar(')'))
        Fail(m_token, "';' or ')'");
    Advance();
    PopBracket();
    return node;
}

// An object, the '{' the current token. Every shorthand is desugared here:
// {a} is ("a", .["a"]), {$x} is ("x", $x), and a key with no ':' likewise.
std::unique_ptr<Node> Parser::ParseObject(const Token& open) {
    Depth depth(*this, open);
    Advance();
    PushBracket(open.begin);
    auto object = MakeNode(NodeType::Object, open.begin);
    if (IsChar('}')) {
        Advance();
        PopBracket();
        return object;
    }
    for (;;) {
        // One entry: a key, then ':' and a value, or a shorthand.
        const Token at = m_token;
        std::unique_ptr<Node> key;
        bool shorthand = false;
        auto entryValue = [&]() -> std::unique_ptr<Node> {
            Advance();  // the ':'
            return ParseObjectValue();
        };
        if (m_token.type == TokenType::Identifier ||
            m_token.type == TokenType::Keyword) {
            key = MakeNode(NodeType::String, at.begin);
            key->stringParts.push_back(at.text);
            Advance();
            if (!IsChar(':'))
                shorthand = true;
            else
                shorthand = false;
        } else if (m_token.type == TokenType::Variable) {
            key = MakeNode(NodeType::String, at.begin);
            key->stringParts.push_back(at.text);
            Advance();
            shorthand = !IsChar(':');
        } else if (m_token.type == TokenType::LocVariable) {
            key = MakeNode(NodeType::String, at.begin);
            key->stringParts.push_back("__loc__");
            Advance();
            shorthand = !IsChar(':');
        } else if (m_token.type == TokenType::StringStart) {
            Token empty;  // no format
            key = ParseStringNode(empty, m_token);
            shorthand = !IsChar(':');
        } else if (m_token.type == TokenType::Format) {
            const Token format = m_token;
            Advance();
            if (m_token.type != TokenType::StringStart) {
                // A format that is not a key by itself; jq's note comes only
                // when a ':' was what went wrong.
                const size_t noteOffset = IsChar(':') ? format.begin : kNoOffset;
                Fail(m_token, "QQSTRING_START", noteOffset);
            }
            key = ParseStringNode(format, m_token);
            shorthand = !IsChar(':');
        } else if (IsChar('(')) {
            const size_t parenOpen = m_token.begin;
            Advance();
            PushBracket(parenOpen);
            key = ParsePipe();
            if (!IsChar(')'))
                Fail(m_token, nullptr);
            Advance();
            PopBracket();
            // The key begins at its '(' (where a semantic error about it is
            // placed), not at whatever the parentheses opened with.
            key->begin = parenOpen;
            if (!IsChar(':'))
                Fail(m_token, "':'");
        } else {
            Fail(m_token, nullptr);
        }
        std::unique_ptr<Node> value;
        if (shorthand) {
            // The value is the key, applied to '.' (a bare $x or $__loc__
            // likewise stands for itself).
            if (m_previous.type == TokenType::Variable) {
                value = MakeNode(NodeType::Variable, m_previous.begin);
                value->text = m_previous.text;
            } else if (m_previous.type == TokenType::LocVariable) {
                value = MakeNode(NodeType::Loc, m_previous.begin);
                value->line = LineOf(m_previous.begin);
            } else {
                auto identity = MakeNode(NodeType::Identity, key->begin);
                auto index = MakeNode(NodeType::Index, key->begin);
                index->children.push_back(std::move(identity));
                // The key is cloned, recursively: its own height is bounded
                // first (the parenthesized expression can be a chain a
                // program's length deep).
                size_t deepest = 0;
                if (TreeHeight(*key, &deepest) > kMaxTreeDepth)
                    StopWith("program nested too deeply (more than " +
                                 std::to_string(kMaxTreeDepth) + " levels)",
                             deepest, kNoOffset, false);
                index->children.push_back(CloneNode(*key));
                value = std::move(index);
            }
        } else {
            value = entryValue();
        }
        object->entries.emplace_back(std::move(key), std::move(value));
        if (IsChar(',')) {
            Advance();
            if (IsChar('}')) {  // one trailing comma allowed
                Advance();
                PopBracket();
                return object;
            }
            continue;
        }
        if (IsChar('}')) {
            Advance();
            PopBracket();
            return object;
        }
        Fail(m_token, "'}'");
    }
}

// An object value: object-value terms joined by '|', right-associative,
// folded iteratively.
std::unique_ptr<Node> Parser::ParseObjectValue() {
    std::vector<std::unique_ptr<Node>> terms;
    terms.push_back(ParseObjectValueTerm());
    while (IsChar('|')) {
        Advance();
        terms.push_back(ParseObjectValueTerm());
    }
    auto result = std::move(terms.back());
    terms.pop_back();
    while (!terms.empty()) {
        auto pipe = MakeNode(NodeType::Pipe, terms.back()->begin);
        pipe->children.push_back(std::move(terms.back()));
        terms.pop_back();
        pipe->children.push_back(std::move(result));
        result = std::move(pipe);
    }
    return result;
}

std::unique_ptr<Node> Parser::ParseObjectValueTerm() {
    if (IsChar('-')) {
        const Token at = m_token;
        Depth depth(*this, at);
        Advance();
        auto node = MakeNode(NodeType::Negate, at.begin);
        node->children.push_back(ParseObjectValueTerm());
        return node;
    }
    return ParsePostfix(true);
}

// What follows 'as': one destructuring pattern, or several joined by '?//'.
std::vector<Pattern> Parser::ParseDestructuring() {
    std::vector<Pattern> patterns;
    patterns.push_back(ParseDestructure());
    while (IsOp("?//")) {
        Advance();
        patterns.push_back(ParseDestructure());
    }
    return patterns;
}

Pattern Parser::ParseDestructure() {
    const Token at = m_token;
    Pattern pattern;
    if (m_token.type == TokenType::Variable) {
        pattern.type = PatternType::Variable;
        pattern.name = m_token.text;
        Advance();
        return pattern;
    }
    if (IsChar('[')) {
        Depth depth(*this, at);
        const size_t open = m_token.begin;
        Advance();
        PushBracket(open);
        pattern.type = PatternType::Array;
        pattern.elements.push_back(ParseDestructure());
        while (IsChar(',')) {
            Advance();
            pattern.elements.push_back(ParseDestructure());
        }
        if (!IsChar(']'))
            Fail(m_token, "',' or ']'");
        Advance();
        PopBracket();
        return pattern;
    }
    if (IsChar('{')) {
        Depth depth(*this, at);
        const size_t open = m_token.begin;
        Advance();
        PushBracket(open);
        pattern.type = PatternType::Object;
        ParsePatternEntry(pattern);
        for (;;) {
            if (IsChar(',')) {
                Advance();
                ParsePatternEntry(pattern);
                continue;
            }
            if (IsChar('}')) {
                Advance();
                PopBracket();
                return pattern;
            }
            Fail(m_token, "',' or '}'");
        }
    }
    Fail(m_token, "BINDING or '[' or '{'");
}

// One entry of an object pattern: '$a', '$a: P' or a keyed 'K: P'.
void Parser::ParsePatternEntry(Pattern& pattern) {
    const Token at = m_token;
    if (m_token.type == TokenType::Variable) {
        Pattern::Entry entry;
        entry.variable = at.text;
        Advance();
        if (IsChar(':')) {
            Advance();
            entry.value = std::make_unique<Pattern>(ParseDestructure());
        }
        pattern.entries.push_back(std::move(entry));
        return;
    }
    std::unique_ptr<Node> key;
    if (m_token.type == TokenType::Identifier ||
        m_token.type == TokenType::Keyword) {
        key = MakeNode(NodeType::String, at.begin);
        key->stringParts.push_back(at.text);
        Advance();
    } else if (m_token.type == TokenType::StringStart) {
        Token empty;  // no format
        key = ParseStringNode(empty, m_token);
    } else if (IsChar('(')) {
        const size_t open = m_token.begin;
        Advance();
        PushBracket(open);
        key = ParsePipe();
        if (!IsChar(')'))
            Fail(m_token, nullptr);
        Advance();
        PopBracket();
        // The key begins at its '(', as an object construction key does.
        key->begin = open;
    } else {
        // $__loc__ included: it starts nothing here either.
        Fail(m_token, nullptr);
    }
    if (!IsChar(':'))
        Fail(m_token, "':'");
    Advance();
    Pattern::Entry entry;
    entry.key = std::move(key);
    entry.value = std::make_unique<Pattern>(ParseDestructure());
    pattern.entries.push_back(std::move(entry));
}

// One 'def', the 'def' the current token; the body is a whole pipe, ended
// by ';'.
void Parser::ParseDefinition(std::vector<FunctionDefinition>& definitions) {
    const Token at = m_token;
    Depth depth(*this, at);
    Advance();
    if (m_token.type != TokenType::Identifier)
        Fail(m_token, "IDENT");
    FunctionDefinition definition;
    definition.name = m_token.text;
    definition.begin = at.begin;
    Advance();
    if (IsChar('(')) {
        const size_t open = m_token.begin;
        Advance();
        PushBracket(open);
        for (;;) {
            if (m_token.type == TokenType::Identifier) {
                definition.params.push_back(m_token.text);
            } else if (m_token.type == TokenType::Variable) {
                definition.params.push_back("$" + m_token.text);
            } else {
                Fail(m_token, "IDENT or BINDING");
            }
            Advance();
            if (IsChar(';')) {
                Advance();
                continue;
            }
            if (IsChar(')')) {
                Advance();
                PopBracket();
                break;
            }
            Fail(m_token, nullptr);
        }
        if (!IsChar(':'))
            Fail(m_token, "':'");
    } else if (!IsChar(':')) {
        Fail(m_token, "'(' or ':'");
    }
    Advance();  // the ':'
    definition.body = ParsePipe();
    if (!IsChar(';'))
        Fail(m_token, nullptr);
    Advance();
    definitions.push_back(std::move(definition));
}

std::unique_ptr<Node> Parser::CloneNode(const Node& node) {
    auto copy = std::make_unique<Node>();
    copy->type = node.type;
    copy->begin = node.begin;
    copy->end = node.end;
    copy->line = node.line;
    copy->text = node.text;
    copy->stringParts = node.stringParts;
    copy->hasCatch = node.hasCatch;
    copy->hasElse = node.hasElse;
    for (const auto& child : node.children)
        copy->children.push_back(CloneNode(*child));
    for (const auto& entry : node.entries) {
        copy->entries.emplace_back(CloneNode(*entry.first),
                                    CloneNode(*entry.second));
    }
    copy->patterns.reserve(node.patterns.size());
    for (const auto& alternative : node.patterns) {
        Pattern cloned;
        ClonePattern(cloned, alternative);
        copy->patterns.push_back(std::move(cloned));
    }
    for (const auto& definition : node.definitions) {
        FunctionDefinition cloned;
        cloned.name = definition.name;
        cloned.params = definition.params;
        cloned.begin = definition.begin;
        cloned.body = CloneNode(*definition.body);
        copy->definitions.push_back(std::move(cloned));
    }
    return copy;
}

void Parser::ClonePattern(Pattern& into, const Pattern& from) {
    into.type = from.type;
    into.name = from.name;
    for (const auto& element : from.elements) {
        Pattern cloned;
        ClonePattern(cloned, element);
        into.elements.push_back(std::move(cloned));
    }
    for (const auto& entry : from.entries) {
        Pattern::Entry cloned;
        cloned.variable = entry.variable;
        if (entry.key)
            cloned.key = CloneNode(*entry.key);
        if (entry.value) {
            cloned.value = std::make_unique<Pattern>();
            ClonePattern(*cloned.value, *entry.value);
        }
        into.entries.push_back(std::move(cloned));
    }
}

} // namespace

ParseResult ParseProgram(std::string_view program) {
    ParseResult result;
    Parser parser(program);
    parser.Run(result);
    return result;
}

void Parser::Run(ParseResult& result) {
    try {
        Advance();
        if (m_token.type == TokenType::End) {
            // Empty, or only comments.
            m_errors.push_back({"Top-level program not given (try \".\")",
                                kNoOffset});
            throw StopParsing{};
        }
        m_atTopStart = true;
        result.root = ParsePipe();
        if (m_token.type != TokenType::End)
            Fail(m_token, "end of file");
        // The parser folds long chains without recursion, so the tree
        // itself can be as deep as the program is long: its height is
        // bounded here, once, in one non-recursive pass.
        size_t deepest = 0;
        if (TreeHeight(*result.root, &deepest) > kMaxTreeDepth)
            StopWith("program nested too deeply (more than " +
                         std::to_string(kMaxTreeDepth) + " levels)",
                     deepest, kNoOffset, false);
    } catch (const StopParsing&) {
    }
    result.errors = std::move(m_errors);
    if (!result.errors.empty())
        result.root.reset();
}

std::string FormatCompileError(std::string_view program,
                               const CompileError& error) {
    std::string out = "jq: error: ";
    out += error.message;
    if (error.offset == kNoOffset) {
        out += "\n";
        return out;
    }
    // The line holding the offset, and the offset's place in it.
    size_t lineStart = 0, line = 1;
    for (size_t i = 0; i < error.offset && i < program.size(); ++i) {
        if (program[i] == '\n') {
            ++line;
            lineStart = i + 1;
        }
    }
    size_t lineEnd = lineStart;
    while (lineEnd < program.size() && program[lineEnd] != '\n')
        ++lineEnd;
    out += " at <top-level>, line ";
    out += std::to_string(line);
    out += ":\n";
    out.append(program.substr(lineStart, lineEnd - lineStart));
    out.append(error.offset - lineStart, ' ');
    out += "\n";
    return out;
}

std::string FormatCompileErrorCount(size_t count) {
    std::string out = "jq: ";
    out += std::to_string(count);
    out += count == 1 ? " compile error\n" : " compile errors\n";
    return out;
}

} // namespace Haisos::Jq