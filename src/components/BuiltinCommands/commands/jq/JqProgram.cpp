#include "commands/jq/JqProgram.h"

#include <algorithm>
#include <cassert>
#include <utility>

#include "commands/jq/JqInterpreter.h"
#include "commands/jq/JqJsonWriter.h"
#include "commands/jq/JqNatives.h"
#include "commands/jq/JqPrelude.h"

namespace Haisos::Jq {
namespace {

// jq 1.7.1's compile-time name checks: every function call, variable, label
// break and object key checked against what is in scope where it stands.
// The scope is a stack of levels, each a set of names; a definition adds a
// level over the part of the tree it scopes over, and the names of every
// enclosing level are visible inside it.
class NameChecker {
public:
    NameChecker(const NativeRegistry& natives, std::vector<CompileError>& errors)
        : m_natives(natives), m_errors(errors) {
        m_functions.emplace_back();
        m_variables.emplace_back();
        m_labels.emplace_back();
    }

    // The prelude's definitions: checked as a program of their own, then in
    // every program's scope (the base level keeps them). The global $names
    // too (the command running the program supplies their values).
    void SeedPrelude(const Node& root,
                     const std::vector<std::string>& globalNames) {
        Check(root);
        if (root.type == NodeType::Defs) {
            for (const FunctionDefinition& definition : root.definitions)
                m_functions.front().push_back(
                    {definition.name,
                     static_cast<int>(definition.params.size())});
        }
        for (const std::string& name : globalNames)
            m_variables.front().push_back(name);
    }

    // A whole program tree; the scope stack ends as it began (a definition
    // scopes only over the tree it is written in).
    void Check(const Node& node) {
        switch (node.type) {
            case NodeType::Identity:
            case NodeType::RecurseDefault:
            case NodeType::Format:
            case NodeType::Loc:
                return;
            case NodeType::Literal:
                return;
            case NodeType::Variable:
                CheckVariable(node);
                return;
            case NodeType::Assign:
                CheckChildren(node);
                return;
            case NodeType::Index:
            case NodeType::Slice:
            case NodeType::Iterate:
            case NodeType::Negate:
            case NodeType::Pipe:
            case NodeType::Comma:
            case NodeType::Alternative:
            case NodeType::And:
            case NodeType::Or:
            case NodeType::Binary:
                CheckChildren(node);
                return;
            case NodeType::Try:
                CheckChildren(node);
                return;
            case NodeType::String:
                CheckChildren(node);
                return;
            case NodeType::Array:
                if (!node.children.empty() && node.children[0])
                    Check(*node.children[0]);
                return;
            case NodeType::Object:
                for (const auto& entry : node.entries) {
                    CheckObjectKey(*entry.first);
                    Check(*entry.second);
                }
                return;
            case NodeType::If: {
                const size_t pairs =
                    (node.children.size() - (node.hasElse ? 1 : 0)) / 2;
                for (size_t p = 0; p < pairs; ++p) {
                    Check(*node.children[2 * p]);
                    Check(*node.children[2 * p + 1]);
                }
                if (node.hasElse)
                    Check(*node.children.back());
                return;
            }
            case NodeType::Reduce:
            case NodeType::Foreach: {
                Check(*node.children[0]);
                // The pattern's variables are not in scope for the init.
                Check(*node.children[1]);
                const size_t firstState = 3;
                PushPatterns(node.patterns);
                Check(*node.children[2]);
                if (node.children.size() > firstState)
                    Check(*node.children[firstState]);
                PopPatterns(node.patterns);
                return;
            }
            case NodeType::Bind: {
                Check(*node.children[0]);
                PushPatterns(node.patterns);
                Check(*node.children[1]);
                PopPatterns(node.patterns);
                return;
            }
            case NodeType::Defs:
                CheckDefs(node);
                return;
            case NodeType::Call:
                CheckCall(node);
                return;
            case NodeType::Label:
                m_labels.back().push_back(node.text);
                Check(*node.children[0]);
                m_labels.back().pop_back();
                return;
            case NodeType::Break:
                if (!HasLabel(node.text))
                    Fail("$*label-" + node.text + " is not defined", node);
                return;
        }
    }

private:
    using NamedArity = std::pair<std::string, int>;

    void CheckChildren(const Node& node) {
        for (const auto& child : node.children)
            if (child)
                Check(*child);
    }

    void CheckVariable(const Node& node) {
        if (!HasVariable(node.text))
            Fail("$" + node.text + " is not defined", node);
    }

    void CheckCall(const Node& node) {
        if (!HasFunction(node.text, static_cast<int>(node.children.size())))
            Fail(node.text + "/" + std::to_string(node.children.size()) +
                     " is not defined",
                 node);
        CheckChildren(node);
    }

    // A computed object key of a constant kind jq refuses: a Literal in key
    // position (everything else is a string, or computed at run time).
    void CheckObjectKey(const Node& key) {
        if (key.type == NodeType::Literal) {
            const Value value = LiteralValueOf(key.text);
            if (value.GetKind() != Kind::String) {
                std::string message = "Cannot use ";
                message += KindName(value.GetKind());
                message += " (";
                message += DumpTruncated(value, 15);
                message += ") as object key";
                Fail(std::move(message), key);
            }
            return;
        }
        Check(key);
    }

    void PushPatterns(const std::vector<Pattern>& patterns) {
        for (const Pattern& pattern : patterns) {
            std::vector<std::string> names;
            CollectNames(pattern, names);
            for (const std::string& name : names)
                m_variables.back().push_back(name);
            // The variables stay until PopPatterns: every alternative binds
            // them all (null where its pattern does not), so a body sees
            // them whatever alternative ran.
            CheckPatternKeys(pattern);
        }
    }

    void PopPatterns(const std::vector<Pattern>& patterns) {
        for (const Pattern& pattern : patterns) {
            std::vector<std::string> names;
            CollectNames(pattern, names);
            for (size_t i = 0; i < names.size(); ++i)
                m_variables.back().pop_back();
        }
    }

    void CollectNames(const Pattern& pattern, std::vector<std::string>& out) {
        // The same walk the interpreter's CollectPatternVariables does; the
        // pattern is tree-shaped and shallow (the parser bounds nesting).
        switch (pattern.type) {
            case PatternType::Variable:
                out.push_back(pattern.name);
                break;
            case PatternType::Array:
                for (const Pattern& element : pattern.elements)
                    CollectNames(element, out);
                break;
            case PatternType::Object:
                for (const auto& entry : pattern.entries) {
                    if (!entry.variable.empty())
                        out.push_back(entry.variable);
                    if (entry.value)
                        CollectNames(*entry.value, out);
                }
                break;
        }
    }

    void CheckPatternKeys(const Pattern& pattern) {
        switch (pattern.type) {
            case PatternType::Variable:
                return;
            case PatternType::Array:
                for (const Pattern& element : pattern.elements)
                    CheckPatternKeys(element);
                return;
            case PatternType::Object:
                for (const auto& entry : pattern.entries) {
                    if (entry.key)
                        CheckObjectKey(*entry.key);
                    if (entry.value)
                        CheckPatternKeys(*entry.value);
                }
                return;
        }
    }

    void CheckDefs(const Node& node) {
        // Each definition sees itself (recursion) and the ones before it;
        // its parameters scope over its body alone.
        for (const FunctionDefinition& definition : node.definitions) {
            m_functions.back().push_back(
                {definition.name, static_cast<int>(definition.params.size())});
            for (const std::string& param : definition.params) {
                if (!param.empty() && param[0] == '$') {
                    m_variables.back().push_back(param.substr(1));
                    m_functions.back().push_back({param.substr(1), 0});
                } else {
                    m_functions.back().push_back({param, 0});
                }
            }
            Check(*definition.body);
            for (const std::string& param : definition.params) {
                m_functions.back().pop_back();
                if (!param.empty() && param[0] == '$')
                    m_variables.back().pop_back();
            }
            // The definition itself stays: it scopes over the rest.
        }
        Check(*node.children[0]);
        for (size_t i = 0; i < node.definitions.size(); ++i)
            m_functions.back().pop_back();
    }

    bool HasFunction(const std::string& name, int arity) const {
        for (auto scope = m_functions.rbegin(); scope != m_functions.rend();
             ++scope)
            for (auto it = scope->rbegin(); it != scope->rend(); ++it)
                if (it->first == name && it->second == arity)
                    return true;
        return m_natives.Find(name, arity) != nullptr;
    }

    bool HasVariable(const std::string& name) const {
        for (auto scope = m_variables.rbegin(); scope != m_variables.rend();
             ++scope)
            if (std::find(scope->begin(), scope->end(), name) != scope->end())
                return true;
        return false;
    }

    bool HasLabel(const std::string& name) const {
        for (auto scope = m_labels.rbegin(); scope != m_labels.rend(); ++scope)
            if (std::find(scope->begin(), scope->end(), name) != scope->end())
                return true;
        return false;
    }

    void Fail(std::string message, const Node& node) {
        m_errors.push_back(CompileError{std::move(message), node.begin});
    }

    const NativeRegistry& m_natives;
    std::vector<CompileError>& m_errors;
    std::vector<std::vector<NamedArity>> m_functions;
    std::vector<std::vector<std::string>> m_variables;
    std::vector<std::vector<std::string>> m_labels;
};

} // namespace

namespace {

// Every Literal of a tree into |out| (the cache the interpreter reads).
// Trees are at most kMaxTreeDepth high, so the recursion is bounded.
void CollectPatternLiterals(const Pattern& pattern,
                            std::unordered_map<const Node*, Value>& out);
void CollectLiterals(const Node& node,
                     std::unordered_map<const Node*, Value>& out) {
    if (node.type == NodeType::Literal)
        out.emplace(&node, LiteralValueOf(node.text));
    for (const auto& child : node.children)
        if (child)
            CollectLiterals(*child, out);
    for (const auto& entry : node.entries) {
        CollectLiterals(*entry.first, out);
        CollectLiterals(*entry.second, out);
    }
    for (const Pattern& pattern : node.patterns)
        CollectPatternLiterals(pattern, out);
    for (const FunctionDefinition& definition : node.definitions)
        CollectLiterals(*definition.body, out);
}

void CollectPatternLiterals(const Pattern& pattern,
                            std::unordered_map<const Node*, Value>& out) {
    switch (pattern.type) {
        case PatternType::Variable:
            return;
        case PatternType::Array:
            for (const Pattern& element : pattern.elements)
                CollectPatternLiterals(element, out);
            return;
        case PatternType::Object:
            for (const auto& entry : pattern.entries) {
                if (entry.key)
                    CollectLiterals(*entry.key, out);
                if (entry.value)
                    CollectPatternLiterals(*entry.value, out);
            }
            return;
    }
}

} // namespace

const std::vector<CompileError>& Program::Errors() const {
    return m_errors;
}

bool Program::Ok() const {
    return m_errors.empty();
}

std::shared_ptr<const Program> Program::Compile(
    std::string_view source, const std::vector<std::string>& globalNames) {
    auto program = std::shared_ptr<Program>(new Program());
    ParseResult parsed = ParseProgram(source);
    if (!parsed.errors.empty()) {
        program->m_errors = std::move(parsed.errors);
        return program;
    }
    {
        NameChecker checker(NativeRegistry::Standard(), program->m_errors);
        checker.SeedPrelude(*ParsedPrelude().root, globalNames);
        checker.Check(*parsed.root);
    }
    if (!program->m_errors.empty()) {
        std::stable_sort(program->m_errors.begin(), program->m_errors.end(),
                         [](const CompileError& a, const CompileError& b) {
                             return a.offset < b.offset;
                         });
        return program;
    }
    CollectLiterals(*parsed.root, program->m_literals);
    CollectLiterals(*ParsedPrelude().root, program->m_literals);
    program->m_root = std::move(parsed.root);
    return program;
}

void Program::Run(const Value& input,
                  const std::map<std::string, Value>& globals, JqHost& host,
                  const Emit& emit) const {
    // The scope a program runs in: the $variables given to it, then the
    // prelude's definitions, the innermost bindings last.
    EnvPtr env;
    for (const auto& binding : globals)
        env = MakeVariableEnv(env, binding.first, binding.second);
    const Node& prelude = *ParsedPrelude().root;
    if (prelude.type == NodeType::Defs)
        for (const FunctionDefinition& definition : prelude.definitions)
            env = MakeFunctionEnv(env, definition);
    Interpreter interpreter(NativeRegistry::Standard(), host, &m_literals);
    interpreter.Eval(*m_root, input, env, emit);
}

} // namespace Haisos::Jq