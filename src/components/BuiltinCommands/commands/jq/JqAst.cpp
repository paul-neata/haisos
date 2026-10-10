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

} // namespace Haisos::Jq