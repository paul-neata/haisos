// The find command line: GNU's parser in its two passes. First every
// argument is classified -- a starting point, or an expression token, with
// the primaries' own arguments consumed and their own errors reported as
// they are met (a path after the expression has begun is "paths must
// precede expression"). Then the expression tokens are folded into a tree,
// with GNU's every diagnostic of the operators and the parentheses.
#include <string>
#include <vector>

#include "BuiltinCommand.h"
#include "BuiltinText.h"
#include "commands/find/FindExpression.h"
#include "interfaces/IFileIO.h"
#include "src/components/Filesystem/FilesystemUtils.h"

namespace Haisos::Find {
namespace {

// GNU's looks_like_expression: a word beginning with '-' is one (a lone "-"
// is a path), "!" and "(" always are, and ")" and "," only once the
// expression has begun -- while starting points are still being collected
// they are paths, as `find ')'` and `find ','` show.
bool LooksLikeExpression(const std::string& arg, bool leading) {
    if (!arg.empty() && arg[0] == '-') {
        return arg.size() > 1;
    }
    if (arg == "!" || arg == "(") {
        return true;
    }
    if (leading) {
        return false;
    }
    return arg == ")" || arg == ",";
}

// One expression token, as the first pass hands it to the second.
struct Token {
    bool isOperator = false;
    std::string spelling;  // as given: "-name", "!", "(", "-and", ...
    // Operator tokens: which operator ('n' !/-not, 'p' (, 'q' ), 'c' ',',
    // 'a' -a/-and, 'o' -o/-or).
    char op = 0;
    // Primary tokens:
    bool suppressesDefaultPrint = false;
    std::unique_ptr<FindPrimary> primary;
};

// The second pass: the tree, with the operator errors. |pending| is the
// operator whose right-hand expression is being looked for, as spelled --
// what "expected an expression after ..." and the ')' messages name.
class TreeBuilder {
public:
    TreeBuilder(BuiltinContext& context, std::vector<Token>& tokens)
        : m_context(context), m_tokens(tokens) {}

    bool Build(std::unique_ptr<FindNode>& out) {
        if (m_tokens.empty()) {
            return true;  // no expression at all
        }
        return BuildComma(out);
    }

    // After a full expression: ")" left over is one too many. Nothing else
    // can be left (the loops below consume every other token).
    bool Done() {
        if (m_pos >= m_tokens.size()) {
            return true;
        }
        if (m_tokens[m_pos].op == 'q') {
            Fail("you have too many ')'");
        }
        return m_ok;
    }

private:
    bool BuildComma(std::unique_ptr<FindNode>& out) {
        if (!BuildOr(out)) {
            return false;
        }
        while (const Token* token = Peek()) {
            if (token->op != 'c') {
                break;
            }
            SetPending(*token);
            std::unique_ptr<FindNode> right;
            if (!BuildOr(right)) {
                return false;
            }
            out = Join(FindNode::Kind::Comma, std::move(out), std::move(right));
        }
        return true;
    }

    bool BuildOr(std::unique_ptr<FindNode>& out) {
        if (!BuildAnd(out)) {
            return false;
        }
        while (const Token* token = Peek()) {
            if (token->op != 'o') {
                break;
            }
            SetPending(*token);
            std::unique_ptr<FindNode> right;
            if (!BuildAnd(right)) {
                return false;
            }
            out = Join(FindNode::Kind::Or, std::move(out), std::move(right));
        }
        return true;
    }

    bool BuildAnd(std::unique_ptr<FindNode>& out) {
        if (!BuildUnary(out)) {
            return false;
        }
        while (const Token* token = Peek()) {
            if (token->op == 'a') {
                SetPending(*token);
                std::unique_ptr<FindNode> right;
                if (!BuildUnary(right)) {
                    return false;
                }
                out = Join(FindNode::Kind::And, std::move(out), std::move(right));
            } else if (!token->isOperator || token->op == 'n' || token->op == 'p') {
                // An implicit -a between two operands.
                std::unique_ptr<FindNode> right;
                if (!BuildUnary(right)) {
                    return false;
                }
                out = Join(FindNode::Kind::And, std::move(out), std::move(right));
            } else {
                break;  // -o, "," or ")": the caller's business
            }
        }
        return true;
    }

    bool BuildUnary(std::unique_ptr<FindNode>& out) {
        Token* token = Peek();
        if (!token) {
            // The command line ran out where an expression was expected.
            if (m_pending == "(") {
                Fail("invalid expression; expected to find a ')' but didn't see one."
                    " Perhaps you need an extra predicate after '('");
            } else {
                Fail("expected an expression after '" + m_pending + "'");
            }
            return false;
        }
        if (token->isOperator) {
            switch (token->op) {
            case 'n':  // ! / -not
                SetPending(*token);
                if (!BuildUnary(out)) {
                    return false;
                }
                m_pending.clear();
                out = Join(FindNode::Kind::Not, std::move(out), {});
                return true;
            case 'p':  // (
                SetPending(*token);
                if (!BuildComma(out)) {
                    return false;
                }
                if (m_pos >= m_tokens.size() || m_tokens[m_pos].op != 'q') {
                    Fail("invalid expression; I was expecting to find a ')' somewhere"
                        " but did not see one.");
                    return false;
                }
                ++m_pos;  // the ')'
                m_pending.clear();
                return true;
            case 'q':  // )
                if (m_pending == "(") {
                    Fail("invalid expression; empty parentheses are not allowed.");
                } else if (m_pending.empty()) {
                    Fail("you have too many ')'");
                } else {
                    Fail("expected an expression between '" + m_pending + "' and ')'");
                }
                return false;
            default:  // -a, -o or "," with nothing before it
                Fail("invalid expression; you have used a binary operator '" + token->spelling
                    + "' with nothing before it.");
                return false;
            }
        }
        ++m_pos;
        m_pending.clear();
        out = std::make_unique<FindNode>();
        out->kind = FindNode::Kind::Primary;
        out->primary = std::move(token->primary);
        return true;
    }

    Token* Peek() {
        return m_pos < m_tokens.size() ? &m_tokens[m_pos] : nullptr;
    }

    void SetPending(const Token& token) {
        m_pending = token.spelling;
        ++m_pos;
    }

    static std::unique_ptr<FindNode> Join(FindNode::Kind kind,
                                          std::unique_ptr<FindNode> left,
                                          std::unique_ptr<FindNode> right) {
        auto node = std::make_unique<FindNode>();
        node->kind = kind;
        node->left = std::move(left);
        node->right = std::move(right);
        return node;
    }

    void Fail(const std::string& message) {
        m_context.Error(message);
        m_ok = false;
    }

    BuiltinContext& m_context;
    std::vector<Token>& m_tokens;
    size_t m_pos = 0;
    std::string m_pending;
    bool m_ok = true;
};

}  // namespace

struct FindParser::Impl {
    Impl(BuiltinContext& context, const std::vector<std::string>& args)
        : context(context), args(args) {
        state.startTime = state.timeOrigin = CurrentFileDateTime();
        // Warnings are on by default only for a person at a terminal, as
        // GNU's default (find's stdin a tty).
        auto input = context.IO().GetDescriptor(IFileIO::kStdIn);
        state.warnings = input && input->IsTerminal();
    }

    BuiltinContext& context;
    const std::vector<std::string>& args;
    size_t pos = 0;
    FindSettings* settings = nullptr;
    FindParseState state;
    bool failed = false;  // an error was reported: end with exit status 1
    bool stop = false;    // -help or -version printed: end with exit status 0
    // The last expression token seen, whatever it was -- the one the
    // "possible unquoted pattern" hint names; primaries' arguments do not
    // count, as `find -name x .` naming -name shows.
    std::string lastExpressionToken;
};

FindParser::FindParser(BuiltinContext& context, const std::vector<std::string>& args)
    : m_impl(std::make_unique<Impl>(context, args)) {}

FindParser::~FindParser() = default;

BuiltinContext& FindParser::Context() { return m_impl->context; }
FindSettings& FindParser::Settings() { return *m_impl->settings; }
FindParseState& FindParser::State() { return m_impl->state; }

void FindParser::Fail(const std::string& message) {
    m_impl->context.Error(message);
    m_impl->failed = true;
}

void FindParser::Warn(const std::string& message) {
    if (m_impl->state.warnings) {
        m_impl->context.Error("warning: " + message);
    }
}

bool FindParser::NextArgument(const std::string& primaryName, std::string& out) {
    if (m_impl->pos >= m_impl->args.size()) {
        Fail("missing argument to `" + primaryName + "'");
        return false;
    }
    out = m_impl->args[m_impl->pos++];
    return true;
}

bool FindParser::HasArgument() const {
    return m_impl->pos < m_impl->args.size();
}

namespace {

const FindPrimaryEntry* LookupPrimary(const std::string& name) {
    static const std::vector<FindPrimaryEntry>& rows = FindTestPrimaries();
    // --help and --version are the long spellings of the two.
    const std::string canonical = name == "--help" ? std::string("-help")
        : (name == "--version" ? std::string("-version") : name);
    for (const auto& row : rows) {
        if (std::string(row.name) == canonical) {
            return &row;
        }
    }
    // -newerXY, whatever x and y are, is one row that reads them off its own
    // spelling; a -newer... of another length is not one of them.
    if (name.size() == 8 && name.compare(0, 6, "-newer") == 0) {
        for (const auto& row : rows) {
            if (std::string(row.name) == "-newerXY") {
                return &row;
            }
        }
    }
    return nullptr;
}

// A path where an expression token belonged: GNU's message, with the hint
// when the name names something after all. |lastToken| is the last
// expression token seen, whatever it was.
void PathsMustPrecede(BuiltinContext& context, const std::string& arg,
                      const std::string& lastToken) {
    // GNU prints these with the argument raw, between its own backtick and
    // apostrophe, not through the quoting of its other messages.
    context.Error("paths must precede expression: `" + arg + "'");
    FileStatus status;
    if (context.IO().Stat(arg, status) == 0) {
        context.Error("possible unquoted pattern after predicate `" + lastToken + "'?");
    }
}

}  // namespace

bool FindParser::Parse(FindSettings& settings, std::unique_ptr<FindNode>& expression,
                       bool& anyAction, int& exitStatus) {
    Impl& impl = *m_impl;
    impl.settings = &settings;
    expression.reset();
    anyAction = false;

    // The leading options, before any starting point or expression token:
    // -H, -L and -P change nothing (no links), -O and -D are Haisos's not
    // treated, and "--" ends them.
    while (impl.pos < impl.args.size()) {
        const std::string& arg = impl.args[impl.pos];
        if (arg == "-H" || arg == "-L" || arg == "-P") {
            ++impl.pos;
        } else if (arg == "--") {
            ++impl.pos;
            break;
        } else if (arg == "-D") {
            if (impl.pos + 1 >= impl.args.size()) {
                impl.context.Error("Missing argument after the -D option.");
                impl.context.TryHelp();
                impl.failed = true;
                break;
            }
            impl.pos += 2;  // -D and its argument
            impl.context.NotTreated("-D");
        } else if (arg.size() >= 2 && arg[0] == '-' && arg[1] == 'O') {
            // -O takes its level attached ("-O3"); Haisos does not reorder
            // the expression, so every level is accepted, and reported.
            ++impl.pos;
            impl.context.NotTreated(arg);
        } else {
            break;
        }
        if (impl.failed) {
            // A -D without its argument: GNU ends here, with exit status 1.
            exitStatus = 1;
            return false;
        }
    }

    // The first pass: every argument is a starting point or an expression
    // token, and the primaries parse as they are met. Starting points end
    // with the first expression token; a path after that is an error.
    std::vector<Token> tokens;
    bool leading = true;
    std::string firstNonOption;  // the first test or action, for the warning below
    while (!impl.failed && impl.pos < impl.args.size()) {
        const std::string arg = impl.args[impl.pos];
        if (!LooksLikeExpression(arg, leading)) {
            if (leading) {
                settings.startingPoints.push_back(arg);
                ++impl.pos;
                continue;
            }
            PathsMustPrecede(impl.context, arg, impl.lastExpressionToken);
            impl.failed = true;
            break;
        }
        leading = false;
        ++impl.pos;
        impl.lastExpressionToken = arg;
        Token token;
        token.spelling = arg;
        if (arg == "!" || arg == "-not" || arg == "(" || arg == ")" || arg == ","
            || arg == "-a" || arg == "-and" || arg == "-o" || arg == "-or") {
            token.isOperator = true;
            token.op = (arg == "(" || arg == ")") ? (arg == "(" ? 'p' : 'q')
                : (arg == "," ? 'c'
                : (arg == "-a" || arg == "-and" ? 'a'
                : (arg == "-o" || arg == "-or" ? 'o' : 'n')));
            tokens.push_back(std::move(token));
            continue;
        }
        const FindPrimaryEntry* row = LookupPrimary(arg);
        if (row == nullptr) {
            Fail("unknown predicate `" + arg + "'");
            break;
        }
        // A global option after a test is still applied to it -- GNU says so
        // in a warning; the positional options are exempt, as is everything
        // before the first test or action.
        if (row->kind == FindPrimaryKind::GlobalOption && !firstNonOption.empty()) {
            Warn("you have specified the global option " + arg + " after the argument "
                + firstNonOption + ", but global options are not positional, i.e., " + arg
                + " affects tests specified before it as well as those specified after it."
                "  Please specify global options before other arguments.");
        }
        std::string primaryName = row->name;
        if (primaryName == "-newerXY") {
            primaryName = arg;  // the row reads x and y off the spelling
        }
        token.primary = row->parse(*this, primaryName);
        if (impl.failed) {
            break;
        }
        if (!token.primary) {
            impl.stop = true;  // -help or -version printed
            break;
        }
        if (row->kind == FindPrimaryKind::Test || row->kind == FindPrimaryKind::Action) {
            if (firstNonOption.empty()) {
                firstNonOption = arg;
            }
            if (row->suppressesDefaultPrint) {
                anyAction = true;
            }
        }
        token.suppressesDefaultPrint = row->suppressesDefaultPrint;
        tokens.push_back(std::move(token));
    }

    if (!impl.failed && !impl.stop) {
        TreeBuilder builder(impl.context, tokens);
        if (builder.Build(expression) && builder.Done()) {
            return true;
        }
        impl.failed = true;
    }
    exitStatus = impl.stop ? 0 : 1;
    return false;
}

bool EvaluateFindNode(FindNode& node, FindRun& run, const FindFile& file) {
    switch (node.kind) {
    case FindNode::Kind::Primary:
        return node.primary->Evaluate(run, file);
    case FindNode::Kind::Not:
        return !EvaluateFindNode(*node.left, run, file);
    case FindNode::Kind::And:
        if (!EvaluateFindNode(*node.left, run, file)) {
            return false;
        }
        if (run.quit) {
            return true;  // the walk ends; the right side never runs
        }
        return EvaluateFindNode(*node.right, run, file);
    case FindNode::Kind::Or:
        if (EvaluateFindNode(*node.left, run, file)) {
            return true;
        }
        if (run.quit) {
            return true;
        }
        return EvaluateFindNode(*node.right, run, file);
    case FindNode::Kind::Comma:
        EvaluateFindNode(*node.left, run, file);  // for its actions; the value dropped
        if (run.quit) {
            return true;
        }
        return EvaluateFindNode(*node.right, run, file);
    }
    return false;
}

}  // namespace Haisos::Find