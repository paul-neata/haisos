#pragma once
#include <bitset>
#include <string>
#include <vector>

namespace Haisos {

enum class RegexNodeType { Empty, Bytes, Concat, Alternate, Repeat, Group, Assert, BackRef };

enum class RegexAssertion {
    LineStart,          // ^ (honours kRegexNotBol; with node.multiline also after '\n')
    LineEnd,            // $ (honours kRegexNotEol; with node.multiline also before '\n')
    LineEndPerl,        // Perl $ without (?m): end, or before a final '\n' (honours kRegexNotEol)
    TextStart,          // \` and Perl \A
    TextEnd,            // \' and Perl \z
    TextEndBeforeNewline, // Perl \Z
    WordBoundary,       // \b
    NotWordBoundary,    // \B
    WordStart,          // \<
    WordEnd,            // \>
};

struct RegexNode {
    RegexNodeType type = RegexNodeType::Empty;
    std::bitset<256> bytes;          // Bytes: the bytes matched (ignoreCase already folded in)
    std::vector<int> children;       // Concat, Alternate (>= 2, in order); Repeat and Group: exactly 1
    int min = 0;                     // Repeat
    int max = -1;                    // Repeat; -1 = no upper bound
    bool greedy = true;              // Repeat; false for Perl's lazy forms
    int group = 0;                   // Group: capture number, 1-based; BackRef: the group referred to
    RegexAssertion assertion = RegexAssertion::LineStart;  // Assert
    bool multiline = false;          // Assert LineStart/LineEnd
    bool ignoreCase = false;         // BackRef: compare ASCII case-insensitively
};

struct RegexTree {
    std::vector<RegexNode> nodes;    // nodes refer to each other by index
    int root = -1;
    size_t groupCount = 0;
    std::vector<std::string> groupNames;  // size groupCount + 1
    bool hasBackReferences = false;
};

// The tree as one line, for tests and diagnostics -- format below.
std::string DumpRegexTree(const RegexTree& tree);

}