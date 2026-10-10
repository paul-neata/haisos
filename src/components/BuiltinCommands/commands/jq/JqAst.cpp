#include "commands/jq/JqAst.h"

#include <cstdio>

namespace Haisos::Jq {
namespace {

// A JSON string of |text|: what a String node with no interpolation and no
// format dumps as, and every (string ...) literal part.
std::string JsonString(const std::string& text) {
    std::string out = "\"";
    char buf[8];
    for (char c : text) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    std::snprintf(buf, sizeof buf, "\\u%04x", c);
                    out += buf;
                } else {
                    out += c;
                }
        }
    }
    out += "\"";
    return out;
}

void DumpInto(const Node& node, std::string& out);

// One pattern alternative, or the whole '(?// ...)' list when there are
// several.
void DumpPattern(const Pattern& pattern, std::string& out) {
    switch (pattern.type) {
        case PatternType::Variable:
            out += "$";
            out += pattern.name;
            break;
        case PatternType::Array:
            out += "(arr";
            for (const Pattern& element : pattern.elements) {
                out += " ";
                DumpPattern(element, out);
            }
            out += ")";
            break;
        case PatternType::Object:
            out += "(obj";
            for (const auto& entry : pattern.entries) {
                out += " (";
                if (entry.key) {
                    DumpInto(*entry.key, out);
                } else {
                    out += "$";
                    out += entry.variable;
                }
                if (entry.value) {
                    out += " ";
                    DumpPattern(*entry.value, out);
                }
                out += ")";
            }
            out += ")";
            break;
    }
}

void DumpAlternatives(const std::vector<Pattern>& patterns, std::string& out) {
    if (patterns.size() == 1) {
        DumpPattern(patterns[0], out);
    } else {
        out += "(?//";
        for (const Pattern& pattern : patterns) {
            out += " ";
            DumpPattern(pattern, out);
        }
        out += ")";
    }
}

void DumpInto(const Node& node, std::string& out) {
    switch (node.type) {
        case NodeType::Identity: out += "."; break;
        case NodeType::RecurseDefault: out += ".."; break;
        case NodeType::Index:
            out += "(index ";
            DumpInto(*node.children[0], out);
            out += " ";
            DumpInto(*node.children[1], out);
            out += ")";
            break;
        case NodeType::Slice:
            out += "(slice ";
            DumpInto(*node.children[0], out);
            out += " ";
            if (node.children[1]) DumpInto(*node.children[1], out); else out += "_";
            out += " ";
            if (node.children[2]) DumpInto(*node.children[2], out); else out += "_";
            out += ")";
            break;
        case NodeType::Iterate:
            out += "(iterate ";
            DumpInto(*node.children[0], out);
            out += ")";
            break;
        case NodeType::Try:
            out += "(try ";
            DumpInto(*node.children[0], out);
            if (node.hasCatch) {
                out += " ";
                DumpInto(*node.children[1], out);
            }
            out += ")";
            break;
        case NodeType::Literal: out += node.text; break;
        case NodeType::String:
            if (node.children.empty() && node.text.empty()) {
                out += JsonString(node.stringParts.empty() ? "" : node.stringParts[0]);
            } else {
                out += "(string ";
                out += node.text.empty() ? "_" : ("@" + node.text);
                for (size_t i = 0; i < node.stringParts.size(); ++i) {
                    out += " ";
                    out += JsonString(node.stringParts[i]);
                    if (i < node.children.size()) {
                        out += " ";
                        DumpInto(*node.children[i], out);
                    }
                }
                out += ")";
            }
            break;
        case NodeType::Format:
            out += "(format @";
            out += node.text;
            out += ")";
            break;
        case NodeType::Array:
            out += "(array";
            if (node.children.size() == 1 && node.children[0]) {
                out += " ";
                DumpInto(*node.children[0], out);
            }
            out += ")";
            break;
        case NodeType::Object:
            out += "(object";
            for (const auto& entry : node.entries) {
                out += " (";
                DumpInto(*entry.first, out);
                out += " ";
                DumpInto(*entry.second, out);
                out += ")";
            }
            out += ")";
            break;
        case NodeType::Negate:
            out += "(neg ";
            DumpInto(*node.children[0], out);
            out += ")";
            break;
        case NodeType::Pipe:
        case NodeType::Comma:
        case NodeType::Alternative:
        case NodeType::And:
        case NodeType::Or:
            out += "(";
            out += node.type == NodeType::Pipe ? "|" : node.type == NodeType::Comma ? ","
                 : node.type == NodeType::Alternative ? "//"
                 : node.type == NodeType::And ? "and" : "or";
            out += " ";
            DumpInto(*node.children[0], out);
            out += " ";
            DumpInto(*node.children[1], out);
            out += ")";
            break;
        case NodeType::Binary:
        case NodeType::Assign:
            out += "(";
            out += node.text;
            out += " ";
            DumpInto(*node.children[0], out);
            out += " ";
            DumpInto(*node.children[1], out);
            out += ")";
            break;
        case NodeType::If:
            out += "(if";
            for (size_t i = 0; i + 1 < node.children.size(); i += 2) {
                out += " ";
                DumpInto(*node.children[i], out);
                out += " ";
                DumpInto(*node.children[i + 1], out);
            }
            out += " ";
            if (node.hasElse) DumpInto(*node.children.back(), out); else out += "_";
            out += ")";
            break;
        case NodeType::Reduce:
            out += "(reduce ";
            DumpInto(*node.children[0], out);
            out += " ";
            DumpAlternatives(node.patterns, out);
            out += " ";
            DumpInto(*node.children[1], out);
            out += " ";
            DumpInto(*node.children[2], out);
            out += ")";
            break;
        case NodeType::Foreach:
            out += "(foreach ";
            DumpInto(*node.children[0], out);
            out += " ";
            DumpAlternatives(node.patterns, out);
            for (size_t i = 1; i < node.children.size(); ++i) {
                out += " ";
                DumpInto(*node.children[i], out);
            }
            out += ")";
            break;
        case NodeType::Bind:
            out += "(as ";
            DumpInto(*node.children[0], out);
            out += " ";
            DumpAlternatives(node.patterns, out);
            out += " ";
            DumpInto(*node.children[1], out);
            out += ")";
            break;
        case NodeType::Defs:
            out += "(defs";
            for (const auto& definition : node.definitions) {
                out += " (def ";
                out += definition.name;
                out += " (";
                for (size_t i = 0; i < definition.params.size(); ++i) {
                    if (i) out += " ";
                    out += definition.params[i];
                }
                out += ") ";
                DumpInto(*definition.body, out);
                out += ")";
            }
            out += " ";
            DumpInto(*node.children[0], out);
            out += ")";
            break;
        case NodeType::Call:
            out += "(call ";
            out += node.text;
            for (const auto& argument : node.children) {
                out += " ";
                DumpInto(*argument, out);
            }
            out += ")";
            break;
        case NodeType::Variable:
            out += "$";
            out += node.text;
            break;
        case NodeType::Loc:
            out += "(loc ";
            out += std::to_string(node.line);
            out += ")";
            break;
        case NodeType::Label:
            out += "(label $";
            out += node.text;
            out += " ";
            DumpInto(*node.children[0], out);
            out += ")";
            break;
        case NodeType::Break:
            out += "(break $";
            out += node.text;
            out += ")";
            break;
    }
}

} // namespace

std::string DumpNode(const Node& node) {
    std::string out;
    DumpInto(node, out);
    return out;
}

namespace {

// Moves every key node of |pattern|'s entries out, at any nesting depth of
// the patterns (their own structure holds no nodes, and the parser's
// nesting limit bounds that walk).
void CollectPatternKeys(Pattern& pattern,
                        std::vector<std::unique_ptr<Node>>& out) {
    for (auto& entry : pattern.entries) {
        if (entry.key)
            out.push_back(std::move(entry.key));
        if (entry.value)
            CollectPatternKeys(*entry.value, out);
    }
    for (auto& element : pattern.elements)
        CollectPatternKeys(element, out);
}

// Pushes every key node of |pattern|'s entries onto |stack|, at |level|, at
// any nesting depth of the patterns.
void PushPatternKeys(const Pattern& pattern, size_t level,
                     std::vector<std::pair<const Node*, size_t>>& stack) {
    for (const auto& entry : pattern.entries) {
        if (entry.key)
            stack.push_back({entry.key.get(), level});
        if (entry.value)
            PushPatternKeys(*entry.value, level, stack);
    }
    for (const auto& element : pattern.elements)
        PushPatternKeys(element, level, stack);
}

} // namespace

Node::~Node() {
    // Iterative: a tree the parser refuses, or a syntax error leaves
    // half-built, can be as deep as the program is long. Each node moves
    // every subtree it owns into |pending| and is then set aside in |owned|
    // to be destroyed once its own subtrees were moved -- destroying it
    // right away would run ~Node one frame per level, the very recursion
    // this exists to avoid.
    std::vector<std::unique_ptr<Node>> pending;
    std::vector<std::unique_ptr<Node>> owned;
    Node* current = this;
    for (;;) {
        for (auto& child : current->children)
            if (child)
                pending.push_back(std::move(child));
        current->children.clear();
        for (auto& entry : current->entries) {
            if (entry.first)
                pending.push_back(std::move(entry.first));
            if (entry.second)
                pending.push_back(std::move(entry.second));
        }
        current->entries.clear();
        for (auto& alternative : current->patterns)
            CollectPatternKeys(alternative, pending);
        current->patterns.clear();
        for (auto& definition : current->definitions)
            if (definition.body)
                pending.push_back(std::move(definition.body));
        current->definitions.clear();
        if (pending.empty())
            break;
        owned.push_back(std::move(pending.back()));
        pending.pop_back();
        current = owned.back().get();
    }
    // Every node in |owned| was emptied above, so each destructor here is
    // one shallow walk of nothing.
}

size_t TreeHeight(const Node& root, size_t* deepestBegin) {
    size_t height = 0;
    std::vector<std::pair<const Node*, size_t>> stack;
    stack.push_back({&root, 1});
    while (!stack.empty()) {
        const Node* node = stack.back().first;
        const size_t level = stack.back().second;
        stack.pop_back();
        if (level > height) {
            height = level;
            if (deepestBegin)
                *deepestBegin = node->begin;
        } else if (level == height && deepestBegin &&
                   node->begin < *deepestBegin) {
            // Several nodes sit at the greatest level: the leftmost is named.
            *deepestBegin = node->begin;
        }
        for (const auto& child : node->children)
            if (child)
                stack.push_back({child.get(), level + 1});
        for (const auto& entry : node->entries) {
            if (entry.first)
                stack.push_back({entry.first.get(), level + 1});
            if (entry.second)
                stack.push_back({entry.second.get(), level + 1});
        }
        for (const auto& alternative : node->patterns)
            PushPatternKeys(alternative, level + 1, stack);
        for (const auto& definition : node->definitions)
            if (definition.body)
                stack.push_back({definition.body.get(), level + 1});
    }
    return height;
}

} // namespace Haisos::Jq