#pragma once

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace Haisos::Jq {

// The kinds of the syntax tree's nodes.
enum class NodeType {
    Identity, RecurseDefault, Index, Slice, Iterate, Try, Literal, String,
    Format, Array, Object, Negate, Pipe, Comma, Alternative, Binary, And, Or,
    Assign, If, Reduce, Foreach, Bind, Defs, Call, Variable, Loc, Label, Break,
};

enum class PatternType { Variable, Array, Object };

struct Node;

// A destructuring pattern: what follows 'as' (one alternative of it).
struct Pattern {
    PatternType type = PatternType::Variable;
    std::string name;                     // Variable: without '$'
    std::vector<Pattern> elements;       // Array
    struct Entry {
        std::string variable;             // "$a" or "$a: P" form: "a"; else empty
        std::unique_ptr<Node> key;        // ident/keyword/string/(expr) key; null for "$a"
        std::unique_ptr<Pattern> value;   // null for a bare "$a"
    };
    std::vector<Entry> entries;           // Object
};

struct FunctionDefinition {
    std::string name;
    std::vector<std::string> params;      // "f" for a filter param, "$x" for a value param
    std::unique_ptr<Node> body;
    size_t begin = 0;                     // offset of "def"
};

// A node of the syntax tree, one kind per construct of the language. The
// tree is owned by one program, never shared, so it is held by
// std::unique_ptr. Children by kind, nullptr where something is absent:
// Index [target, key] (".a" is Index(Identity, String "a"), likewise
// ."s", .[e] and their postfix forms); Slice [target, from-or-null,
// to-or-null]; Iterate [target]; Try [body, catch-or-absent] (also the
// postfix '?'); Literal: text is the number as written, or true/false/null;
// String: interpolations, text the format name or ""; Format: applied to '.';
// Array [body] or none; Object: entries, shorthands desugared by the parser;
// Negate [operand]; Pipe, Comma, Alternative, And, Or [left, right]; Binary:
// text is the operator, [left, right]; Assign likewise; If [cond1, then1,
// cond2, then2, ..., else-if-hasElse]; Reduce [source, init, update];
// Foreach [source, init, update, extract-if-given]; Bind [source, body];
// Defs: definitions, then [scope]; Call: text the name, children the
// arguments; Variable: text the name; Loc: line is used; Label: text the
// name, [body]; Break: text the name.
struct Node {
    NodeType type;
    size_t begin = 0, end = 0;   // byte offsets of the construct in the program
    int line = 1;                // 1-based line of |begin| (for $__loc__ and errors)
    std::string text;            // see the kinds above
    std::vector<std::unique_ptr<Node>> children;
    std::vector<std::string> stringParts;  // String: literal parts; children are
                                           // the interpolations, stringParts.size()
                                           // == children.size() + 1
    std::vector<std::pair<std::unique_ptr<Node>, std::unique_ptr<Node>>> entries;  // Object: key, value
    std::vector<Pattern> patterns;            // Bind/Reduce/Foreach: alternatives joined by ?//
    std::vector<FunctionDefinition> definitions;  // Defs
    bool hasCatch = false;  // Try
    bool hasElse = false;   // If
};

// One line naming the node, what the tests compare: '.' '..' '(index T K)'
// '(slice T F E)' with '_' for an absent bound, '(iterate T)', '(try B)'
// '(try B C)', a Literal's text, a String with no interpolation and no
// format as a JSON string, otherwise '(string FMT P0 E1 P1 ...)' with FMT
// '@name' or '_', '(format @base64)', '(array)' or '(array B)',
// '(object (K V) ...)', '(neg E)', '(| A B)', '(, A B)', '(// A B)',
// '(and A B)', '(or A B)', '(+ A B)' for any Binary or Assign, '(if C T ...
// E)' with '_' for no else, '(reduce S PAT I U)', '(foreach S PAT I U [X])',
// '(as S PAT B)', '(defs (def NAME (PARAMS) BODY) ... SCOPE)',
// '(call NAME ARG...)', '$name', '(loc LINE)', '(label $name B)',
// '(break $name)'. A pattern: '$x', '(arr P ...)', '(obj ENTRY ...)' with
// an entry '($a)', '($a P)' or '(K P)'; alternatives '(?// P1 P2 ...)'.
std::string DumpNode(const Node& node);

} // namespace Haisos::Jq