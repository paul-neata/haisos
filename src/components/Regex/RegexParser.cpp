#include "RegexParser.h"

#include <cstddef>
#include <functional>

namespace Haisos {

namespace {

// Parentheses nested deeper than this fail to compile: the recursive-descent
// parser would otherwise overflow the stack on pathological patterns.
// PCRE2's own limit, with its message; glibc has none, so Haisos reuses
// glibc's "Regular expression too big" there.
constexpr int kMaxNestingDepth = 1000;

// glibc's RE_DUP_MAX and PCRE2's limit on {m,n} values.
constexpr long long kMaxGnuRepeat = 32767;
constexpr long long kMaxPerlRepeat = 65535;

// Return values of the parse helpers.
constexpr int kParseError = -1;
constexpr int kNoAtom = -2;  // a token that produces no node ((?flags) in Perl)

constexpr size_t kNpos = static_cast<size_t>(-1);

bool IsDigit(char c) { return c >= '0' && c <= '9'; }
bool IsOctalDigit(char c) { return c >= '0' && c <= '7'; }
bool IsHexDigit(char c) { return IsDigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }
bool IsLetter(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
bool IsNameStart(char c) { return IsLetter(c) || c == '_'; }
bool IsNameBody(char c) { return IsLetter(c) || IsDigit(c) || c == '_'; }

int HexValue(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return c - 'A' + 10;
}

// A set of bytes holding an ASCII letter also holds its other case.
void FoldSet(std::bitset<256>& set) {
    for (int c = 'a'; c <= 'z'; ++c) {
        if (set[c]) set[c - 'a' + 'A'] = true;
        if (set[c - 'a' + 'A']) set[c] = true;
    }
}

void AddRange(std::bitset<256>& set, int lo, int hi) {
    for (int c = lo; c <= hi; ++c) set[c] = true;
}

std::bitset<256> Complement(const std::bitset<256>& set) {
    std::bitset<256> out;
    for (int c = 0; c < 256; ++c) out[c] = !set[c];
    return out;
}

// The byte sets behind \w \s and their Perl counterparts.
std::bitset<256> WordSet() {
    std::bitset<256> set;
    AddRange(set, '0', '9');
    AddRange(set, 'A', 'Z');
    AddRange(set, 'a', 'z');
    set['_'] = true;
    return set;
}
std::bitset<256> SpaceSet() {
    std::bitset<256> set;
    AddRange(set, 0x09, 0x0d);
    set[' '] = true;
    return set;
}
std::bitset<256> DigitSet() {
    std::bitset<256> set;
    AddRange(set, '0', '9');
    return set;
}
std::bitset<256> PerlHSpaceSet() {
    std::bitset<256> set;
    set['\t'] = true;
    set[' '] = true;
    set[0xa0] = true;
    return set;
}
std::bitset<256> PerlVSpaceSet() {
    std::bitset<256> set;
    AddRange(set, 0x0a, 0x0d);
    set[0x85] = true;
    return set;
}

// The POSIX character classes' members, ASCII as the C locale. False for a
// name the standard does not have. No <cctype>: it depends on the locale.
bool PosixClass(std::string_view name, std::bitset<256>& set) {
    set.reset();
    if (name == "alnum") { AddRange(set, '0', '9'); AddRange(set, 'A', 'Z'); AddRange(set, 'a', 'z'); }
    else if (name == "alpha") { AddRange(set, 'A', 'Z'); AddRange(set, 'a', 'z'); }
    else if (name == "blank") { set['\t'] = true; set[' '] = true; }
    else if (name == "cntrl") { AddRange(set, 0x00, 0x1f); set[0x7f] = true; }
    else if (name == "digit") AddRange(set, '0', '9');
    else if (name == "graph") AddRange(set, 0x21, 0x7e);
    else if (name == "lower") AddRange(set, 'a', 'z');
    else if (name == "print") AddRange(set, 0x20, 0x7e);
    else if (name == "punct") { AddRange(set, 0x21, 0x2f); AddRange(set, 0x3a, 0x40); AddRange(set, 0x5b, 0x60); AddRange(set, 0x7b, 0x7e); }
    else if (name == "space") { AddRange(set, 0x09, 0x0d); set[' '] = true; }
    else if (name == "upper") AddRange(set, 'A', 'Z');
    else if (name == "xdigit") { AddRange(set, '0', '9'); AddRange(set, 'A', 'F'); AddRange(set, 'a', 'f'); }
    else return false;
    return true;
}

class Parser {
public:
    Parser(std::string_view pattern, const RegexOptions& options, RegexTree& tree, std::string& error)
        : m_pattern(pattern), m_tree(tree), m_error(error),
          m_syntax(options.syntax), m_caseFold(options.ignoreCase), m_multiline(options.multiline) {}

    bool Run();

private:
    struct TermContext {
        bool repeatable = false;    // a quantifier may apply to what precedes
        bool branchStarted = false; // any token consumed in this branch yet
    };

    // ---- input ----
    std::string_view m_pattern;
    size_t m_pos = 0;
    RegexTree& m_tree;
    std::string& m_error;
    RegexSyntax m_syntax;

    // ---- state ----
    int m_groupTally = 0;              // groups opened so far (their numbers)
    std::vector<bool> m_closed;        // GNU: [n] once group n has been closed
    std::vector<int> m_perlBackRefs;    // Perl: checked against the final group count
    std::vector<std::string> m_groupNames;
    bool m_failed = false;
    int m_depth = 0;

    // In-pattern flags ((?i) etc.); for Basic/Extended these just hold the
    // options and m_dotAll stays false.
    bool m_caseFold;
    bool m_multiline;
    bool m_dotAll = false;

    bool Fail(const std::string& message) {
        if (!m_failed) {
            m_failed = true;
            m_error = message;
        }
        return false;
    }

    bool HasMore() const { return m_pos < m_pattern.size(); }
    char Peek(size_t ahead = 0) const {
        size_t i = m_pos + ahead;
        return i < m_pattern.size() ? m_pattern[i] : '\0';
    }
    bool PeekIs(size_t ahead, char c) const { return m_pos + ahead < m_pattern.size() && m_pattern[m_pos + ahead] == c; }
    void Advance(size_t n = 1) { m_pos += n; }

    int AddNode(RegexNode node) {
        m_tree.nodes.push_back(std::move(node));
        return static_cast<int>(m_tree.nodes.size()) - 1;
    }

    bool IsPerl() const { return m_syntax == RegexSyntax::Perl; }
    bool IsBasic() const { return m_syntax == RegexSyntax::Basic; }

    // ---- tree building ----
    int FinishConcat(const std::vector<int>& items) {
        if (items.empty()) {
            RegexNode node;
            node.type = RegexNodeType::Empty;
            return AddNode(std::move(node));
        }
        if (items.size() == 1) return items[0];
        RegexNode node;
        node.type = RegexNodeType::Concat;
        node.children = items;
        return AddNode(std::move(node));
    }

    int NewBytesRaw(std::bitset<256> set) {
        RegexNode node;
        node.type = RegexNodeType::Bytes;
        node.bytes = set;
        return AddNode(std::move(node));
    }
    int NewBytes(std::bitset<256> set) {
        if (m_caseFold) FoldSet(set);
        return NewBytesRaw(set);
    }
    int NewByte(unsigned char c) {
        std::bitset<256> set;
        set[c] = true;
        return NewBytes(set);
    }
    int NewAssert(RegexAssertion assertion, bool multiline) {
        RegexNode node;
        node.type = RegexNodeType::Assert;
        node.assertion = assertion;
        node.multiline = multiline;
        return AddNode(std::move(node));
    }
    int NewBackRef(int n) {
        RegexNode node;
        node.type = RegexNodeType::BackRef;
        node.group = n;
        node.ignoreCase = m_caseFold;
        return AddNode(std::move(node));
    }
    int ApplyRepeat(int item, int min, int max, bool greedy) {
        RegexNode node;
        node.type = RegexNodeType::Repeat;
        node.children.push_back(item);
        node.min = min;
        node.max = max;
        node.greedy = greedy;
        return AddNode(std::move(node));
    }

    // ---- structure ----
    int ParseAlternation();
    int ParseConcat();
    bool AtBranchEnd() const;
    bool ParseTerm(TermContext& ctx, std::vector<int>& items);
    int ParseAtom(TermContext& ctx);
    // Parses an atom and pushes it into |items|; |finish| updates the term
    // context once the node is in the list.
    bool ParseAtomInto(TermContext& ctx, std::vector<int>& items, const std::function<void(int)>& finish);
    // After '(' / '(?:' / '(?<...' is consumed; the flags to restore on exit.
    int ParseGroupBody(int number, bool restoreFold, bool restoreMulti, bool restoreDot);
    int ParseGnuEscape();             // m_pos at the backslash
    int ParsePerlEscape();            // m_pos at the backslash
    // 0: flags applied, no group; 1: a ':' follows, a scoped group follows;
    // false on error.
    bool ParseInlineFlags(int& outcome);
    bool ReadGroupName(char close, std::string& name);

    // ---- brackets ----
    int ParseGnuBracket();   // m_pos at '['
    int ParsePerlBracket();  // m_pos at '['
    // Reads a [:name:] / [:^name:] item, m_pos at '['. 1: the set was read;
    // -2: not a POSIX class (a literal '['); kParseError on failure.
    int ParsePerlPosixItem(std::bitset<256>& set);
    // Reads one GNU bracket item. 1: a single byte; 2: a set (a [:class:]);
    // kParseError on failure.
    int ReadGnuBracketItem(unsigned char& itemByte, std::bitset<256>& itemSet);
    // Reads [:name:], [.c.] or [=c=] starting at m_pos, with |lead| the ':' ,
    // '.' or '=' after '['. 1: a single byte; 2: a set; kParseError on failure.
    int ReadGnuBracketClass(unsigned char& itemByte, std::bitset<256>& itemSet, char lead);
    // Whether the bracket at m_pos is a whole [:name:] that PCRE2 rejects
    // outside a class.
    bool IsWholePosixClass() const;
    // Reads a Perl escape inside a bracket. 1: a single byte; 2: a set;
    // kParseError on failure. m_pos at the backslash.
    int ReadPerlClassEscape(unsigned char& byte, std::bitset<256>& set);

    // ---- helpers ----
    // Parses a GNU interval {m} {m,} {m,n} (Basic: the \{...\} form, m_pos at
    // the '{'). Always an interval, never a literal '{'.
    bool ParseGnuInterval(int& min, int& max);
    bool ReadGnuDigits(long long& value, bool& empty);
    // Where the '{' at m_pos forms a Perl quantifier: the position past its
    // closing '}', or kNpos. Bounds saturated at kMaxPerlRepeat + 1.
    size_t PerlQuantifierShape(long long& min, long long& max, bool& maxBounded) const;
    bool QuantifierAhead() const;

    void FailEscape(unsigned char c) {
        switch (c) {
        case 'G': case 'K': case 'p': case 'P': case 'X': case 'R':
        case 'N': case 'g': case 'k': case 'Q': case 'E':
            Fail(std::string("\\") + static_cast<char>(c) + " is not supported");
            break;
        default:
            Fail("unrecognized character follows \\");
            break;
        }
    }

    // \xhh or \x{...} or \cX, shared by the pattern and bracket escapes.
    bool ReadPerlHex(unsigned char& byte);
    bool ReadPerlControl(unsigned char& byte);
};

bool Parser::Run() {
    int root = ParseAlternation();
    if (root == kParseError || m_failed) return false;
    if (HasMore()) {
        // Only a closing parenthesis can stop ParseAlternation outside a group.
        Fail(IsPerl() ? "unmatched closing parenthesis" : "Unmatched ) or \\)");
        return false;
    }
    if (IsPerl()) {
        for (int n : m_perlBackRefs) {
            if (n > m_groupTally) {
                Fail("reference to non-existent subpattern");
                return false;
            }
        }
    }
    m_tree.root = root;
    m_tree.groupCount = static_cast<size_t>(m_groupTally);
    m_tree.groupNames.assign(m_groupTally + 1, std::string());
    for (size_t i = 0; i < m_groupNames.size() && i < m_tree.groupNames.size(); ++i)
        m_tree.groupNames[i] = m_groupNames[i];
    m_tree.hasBackReferences = false;
    for (const RegexNode& node : m_tree.nodes) {
        if (node.type == RegexNodeType::BackRef) m_tree.hasBackReferences = true;
    }
    return true;
}

int Parser::ParseAlternation() {
    std::vector<int> branches;
    for (;;) {
        int branch = ParseConcat();
        if (branch == kParseError) return kParseError;
        branches.push_back(branch);
        if (!HasMore()) break;
        if (IsBasic() ? (PeekIs(0, '\\') && PeekIs(1, '|')) : PeekIs(0, '|')) {
            Advance(IsBasic() ? 2 : 1);
            continue;
        }
        break;  // a closing parenthesis; the caller checks it
    }
    if (branches.size() == 1) return branches[0];
    RegexNode node;
    node.type = RegexNodeType::Alternate;
    node.children = branches;
    return AddNode(std::move(node));
}

bool Parser::AtBranchEnd() const {
    if (!HasMore()) return true;
    if (IsBasic()) return PeekIs(0, '\\') && (PeekIs(1, ')') || PeekIs(1, '|'));
    return PeekIs(0, ')') || PeekIs(0, '|');
}

int Parser::ParseConcat() {
    std::vector<int> items;
    TermContext ctx;
    while (HasMore() && !AtBranchEnd()) {
        if (!ParseTerm(ctx, items)) return kParseError;
    }
    return FinishConcat(items);
}

// Whether the token at m_pos would quantify what precedes it.
bool Parser::QuantifierAhead() const {
    if (!HasMore()) return false;
    char c = Peek();
    if (IsBasic()) {
        if (c == '*') return true;
        if (c != '\\') return false;
        return PeekIs(1, '+') || PeekIs(1, '?') || PeekIs(1, '{');
    }
    if (c == '*' || c == '+' || c == '?') return true;
    if (c != '{') return false;
    if (!IsPerl()) return true;  // Extended: '{' always starts an interval
    long long min, max;
    bool maxBounded;
    return PerlQuantifierShape(min, max, maxBounded) != kNpos;
}

// Parses one term into |items|: an atom (pushed), or a quantifier on the
// last item (which replaces it). Returns false with |m_error| set on failure.
bool Parser::ParseTerm(TermContext& ctx, std::vector<int>& items) {
    auto finish = [this, &ctx, &items](int node) {
        ctx.branchStarted = true;
        if (node == kNoAtom) {
            ctx.repeatable = false;  // ((?flags) in Perl: nothing to repeat yet)
            return;
        }
        switch (m_tree.nodes[static_cast<size_t>(node)].type) {
        case RegexNodeType::Assert:
            ctx.repeatable = false;  // a quantifier on ^ or $ is an error
            break;
        default:
            // Anything that forms an atom is repeatable: a byte set, a
            // group (or the raw content of a non-capturing one: Concat,
            // Alternate, Empty), a repeat, a back reference.
            ctx.repeatable = true;
            break;
        }
    };

    if (QuantifierAhead()) {
        if (!ctx.repeatable) {
            if (IsBasic()) {
                // '*', '\+' and '\?' are ordinary characters at the start of
                // a branch or after \(, \| or an anchor; '\{' there is an
                // error.
                int atom = kParseError;
                if (PeekIs(0, '*')) { Advance(); atom = NewByte('*'); }
                else if (PeekIs(0, '\\') && PeekIs(1, '+')) { Advance(2); atom = NewByte('+'); }
                else if (PeekIs(0, '\\') && PeekIs(1, '?')) { Advance(2); atom = NewByte('?'); }
                else { Fail("Invalid preceding regular expression"); return false; }
                items.push_back(atom);
                finish(atom);
                return true;
            }
            Fail(IsPerl() ? "quantifier does not follow a repeatable item"
                          : "Invalid preceding regular expression");
            return false;
        }
        if (IsPerl()) {
            long long min = 0, max = 0;
            bool maxBounded = true;
            char c = Peek();
            if (c == '*') { Advance(); min = 0; maxBounded = false; }
            else if (c == '+') { Advance(); min = 1; maxBounded = false; }
            else if (c == '?') { Advance(); min = 0; max = 1; }
            else {  // '{', a valid shape (QuantifierAhead checked it)
                size_t end = PerlQuantifierShape(min, max, maxBounded);
                m_pos = end;
            }
            if (min > kMaxPerlRepeat || (maxBounded && max > kMaxPerlRepeat)) {
                Fail("number too big in {} quantifier");
                return false;
            }
            if (maxBounded && min > max) {
                Fail("numbers out of order in {} quantifier");
                return false;
            }
            bool greedy = true;
            if (HasMore() && Peek() == '?') { greedy = false; Advance(); }
            items.back() = ApplyRepeat(items.back(), static_cast<int>(min),
                                       maxBounded ? static_cast<int>(max) : -1, greedy);
            if (HasMore() && Peek() == '+') {
                Fail("possessive quantifiers are not supported");
                return false;
            }
            if (QuantifierAhead()) {
                Fail("quantifier does not follow a repeatable item");
                return false;
            }
            finish(items.back());
            return true;
        }
        // GNU: quantifiers stack (a** is (a*)*).
        for (;;) {
            if (!QuantifierAhead()) break;
            int min, max;
            if (IsBasic() && PeekIs(0, '\\')) {
                char d = Peek(1);
                if (d == '+') { Advance(2); min = 1; max = -1; }
                else if (d == '?') { Advance(2); min = 0; max = 1; }
                else {  // '\{'
                    Advance();  // the backslash
                    if (!ParseGnuInterval(min, max)) return false;
                }
            } else {
                char c = Peek();
                if (c == '*') { Advance(); min = 0; max = -1; }
                else if (c == '+') { Advance(); min = 1; max = -1; }
                else if (c == '?') { Advance(); min = 0; max = 1; }
                else {  // '{' (Extended)
                    if (!ParseGnuInterval(min, max)) return false;
                }
            }
            if (m_failed) return false;
            items.back() = ApplyRepeat(items.back(), min, max, true);
        }
        finish(items.back());
        return true;
    }
    return ParseAtomInto(ctx, items, finish);
}

bool Parser::ParseAtomInto(TermContext& ctx, std::vector<int>& items, const std::function<void(int)>& finish) {
    int node = ParseAtom(ctx);
    if (node == kParseError) return false;
    if (node != kNoAtom) items.push_back(node);
    finish(node);
    return true;
}

int Parser::ParseAtom(TermContext& ctx) {
    char c = Peek();
    if (c == '.') {
        Advance();
        std::bitset<256> set;
        if (IsPerl() && !m_dotAll) {
            for (int b = 0; b < 256; ++b) set[b] = b != '\n';
        } else {
            for (int b = 0; b < 256; ++b) set[b] = true;
        }
        return NewBytes(set);
    }
    if (c == '[') {
        if (IsPerl()) {
            if (IsWholePosixClass()) {
                Fail("POSIX named classes are supported only within a class");
                return kParseError;
            }
            return ParsePerlBracket();
        }
        return ParseGnuBracket();
    }
    if (c == '\\' && !IsPerl()) return ParseGnuEscape();
    if (c == '\\' && IsPerl()) return ParsePerlEscape();
    if (c == '^') {
        Advance();
        // In Basic, '^' is an anchor only at the start of a branch.
        if (IsBasic() && ctx.branchStarted) return NewByte('^');
        return NewAssert(RegexAssertion::LineStart, m_multiline);
    }
    if (c == '$') {
        Advance();
        if (IsBasic()) {
            // ... and '$' only before the end, '\)' or '\|'.
            bool anchor = m_pos >= m_pattern.size() ||
                          (PeekIs(0, '\\') && (PeekIs(1, ')') || PeekIs(1, '|')));
            if (anchor) return NewAssert(RegexAssertion::LineEnd, m_multiline);
            return NewByte('$');
        }
        if (IsPerl() && !m_multiline) return NewAssert(RegexAssertion::LineEndPerl, false);
        return NewAssert(RegexAssertion::LineEnd, m_multiline);
    }
    if (c == '(' && !IsBasic()) {
        Advance();
        bool capture = true;
        std::string name;
        // The flags a group restores on exit: those around it, taken before
        // a (?flags:...) group applies its own.
        bool outerFold = m_caseFold, outerMulti = m_multiline, outerDot = m_dotAll;
        if (IsPerl() && PeekIs(0, '?')) {
            Advance();
            if (!HasMore()) {
                Fail("missing closing parenthesis");
                return kParseError;
            }
            char d = Peek();
            if (d == ':') { Advance(); capture = false; }
            else if (d == '=' || d == '!') { Fail("lookaround assertions are not supported"); return kParseError; }
            else if (d == '>') { Fail("atomic groups are not supported"); return kParseError; }
            else if (d == '<') {
                Advance();
                if (PeekIs(0, '=') || PeekIs(0, '!')) { Fail("lookaround assertions are not supported"); return kParseError; }
                if (!ReadGroupName('>', name)) return kParseError;
            }
            else if (d == '\'') {
                Advance();
                if (!ReadGroupName('\'', name)) return kParseError;
            }
            else if (d == 'P') {
                Advance();
                if (!PeekIs(0, '<')) { Fail("unrecognized character after (? or (?-"); return kParseError; }
                Advance();
                if (!ReadGroupName('>', name)) return kParseError;
            }
            else if (d == 'i' || d == 'm' || d == 's' || d == '-' || d == ')') {
                int outcome = -1;
                if (!ParseInlineFlags(outcome)) return kParseError;
                if (outcome == 0) return kNoAtom;
                capture = false;  // a scoped (?flags:...) group follows
            }
            else {
                Fail("unrecognized character after (? or (?-");
                return kParseError;
            }
        }
        int number = 0;
        if (capture) {
            ++m_groupTally;
            number = m_groupTally;
            if (m_groupNames.size() < static_cast<size_t>(number) + 1)
                m_groupNames.resize(static_cast<size_t>(number) + 1);
            if (!name.empty()) m_groupNames[static_cast<size_t>(number)] = name;
        }
        return ParseGroupBody(number, outerFold, outerMulti, outerDot);
    }
    // Any other character is an ordinary one: in Basic that includes the
    // operators of the other two syntaxes.
    Advance();
    return NewByte(static_cast<unsigned char>(c));
}

// The body and closing parenthesis of a group; |number| 1-based, 0 for a
// non-capturing one (which is just its content, no Group node). The flags
// restored on exit are the enclosing ones: ParseAtom takes them before a
// (?flags:...) group applies its own.
int Parser::ParseGroupBody(int number, bool restoreFold, bool restoreMulti, bool restoreDot) {
    if (++m_depth > kMaxNestingDepth) {
        Fail(IsPerl() ? "parentheses are too deeply nested" : "Regular expression too big");
        return kParseError;
    }
    int body = ParseAlternation();
    if (body == kParseError) return kParseError;
    bool atClose = IsBasic() ? (PeekIs(0, '\\') && PeekIs(1, ')')) : PeekIs(0, ')');
    if (!atClose) {
        Fail(IsPerl() ? "missing closing parenthesis" : "Unmatched ( or \\(");
        return kParseError;
    }
    Advance(IsBasic() ? 2 : 1);
    --m_depth;
    m_caseFold = restoreFold;
    m_multiline = restoreMulti;
    m_dotAll = restoreDot;
    if (number > 0) {
        if (m_closed.size() < static_cast<size_t>(number) + 1)
            m_closed.resize(static_cast<size_t>(number) + 1, false);
        m_closed[static_cast<size_t>(number)] = true;
        RegexNode node;
        node.type = RegexNodeType::Group;
        node.group = number;
        node.children.push_back(body);
        return AddNode(std::move(node));
    }
    return body;
}

bool Parser::ParseInlineFlags(int& outcome) {
    bool fold = m_caseFold, multi = m_multiline, dot = m_dotAll;
    bool adding = true;
    for (;;) {
        if (!HasMore()) {
            Fail("missing closing parenthesis");
            return false;
        }
        char f = Peek();
        if (f == ')' || f == ':') {
            m_caseFold = fold;
            m_multiline = multi;
            m_dotAll = dot;
            outcome = (f == ')') ? 0 : 1;
            Advance();
            return true;
        }
        if (f == '-') {
            if (!adding) {  // a second '-'
                Fail("unrecognized character after (? or (?-");
                return false;
            }
            adding = false;
            Advance();
            continue;
        }
        if (f == 'i') fold = adding;
        else if (f == 'm') multi = adding;
        else if (f == 's') dot = adding;
        else {
            Fail("unrecognized character after (? or (?-");
            return false;
        }
        Advance();
    }
}

bool Parser::ReadGroupName(char close, std::string& name) {
    size_t start = m_pos;
    if (HasMore() && IsNameStart(Peek())) {
        Advance();
        while (HasMore() && IsNameBody(Peek())) Advance();
    }
    if (m_pos == start || !PeekIs(0, close)) {
        Fail("subpattern name expected");
        return false;
    }
    name = std::string(m_pattern.substr(start, m_pos - start));
    Advance();
    return true;
}

int Parser::ParseGnuEscape() {
    if (m_pos + 1 >= m_pattern.size()) {
        Fail("Trailing backslash");
        return kParseError;
    }
    char d = Peek(1);
    if (d == '(' && IsBasic()) {  // in Extended, \( is the character '('
        Advance(2);
        ++m_groupTally;
        return ParseGroupBody(m_groupTally, m_caseFold, m_multiline, m_dotAll);
    }
    if (d == 'w') { Advance(2); return NewBytes(WordSet()); }
    if (d == 'W') { Advance(2); return NewBytes(Complement(WordSet())); }
    if (d == 's') { Advance(2); return NewBytes(SpaceSet()); }
    if (d == 'S') { Advance(2); return NewBytes(Complement(SpaceSet())); }
    if (d == 'b') { Advance(2); return NewAssert(RegexAssertion::WordBoundary, false); }
    if (d == 'B') { Advance(2); return NewAssert(RegexAssertion::NotWordBoundary, false); }
    if (d == '<') { Advance(2); return NewAssert(RegexAssertion::WordStart, false); }
    if (d == '>') { Advance(2); return NewAssert(RegexAssertion::WordEnd, false); }
    if (d == '`') { Advance(2); return NewAssert(RegexAssertion::TextStart, false); }
    if (d == '\'') { Advance(2); return NewAssert(RegexAssertion::TextEnd, false); }
    if (d >= '1' && d <= '9') {
        int n = d - '0';
        if (n >= static_cast<int>(m_closed.size()) || !m_closed[static_cast<size_t>(n)]) {
            Fail("Invalid back reference");
            return kParseError;
        }
        Advance(2);
        return NewBackRef(n);
    }
    // Any other escaped character is that character (\n is 'n', \0 is '0').
    Advance(2);
    return NewByte(static_cast<unsigned char>(d));
}

int Parser::ParsePerlEscape() {
    Advance();  // the backslash
    if (!HasMore()) {
        Fail("\\ at end of pattern");
        return kParseError;
    }
    char c = Peek();
    Advance();
    switch (c) {
    case 'd': return NewBytes(DigitSet());
    case 'D': return NewBytes(Complement(DigitSet()));
    case 'w': return NewBytes(WordSet());
    case 'W': return NewBytes(Complement(WordSet()));
    case 's': return NewBytes(SpaceSet());
    case 'S': return NewBytes(Complement(SpaceSet()));
    case 'h': return NewBytes(PerlHSpaceSet());
    case 'H': return NewBytes(Complement(PerlHSpaceSet()));
    case 'v': return NewBytes(PerlVSpaceSet());
    case 'V': return NewBytes(Complement(PerlVSpaceSet()));
    case 'b': return NewAssert(RegexAssertion::WordBoundary, false);
    case 'B': return NewAssert(RegexAssertion::NotWordBoundary, false);
    case 'A': return NewAssert(RegexAssertion::TextStart, false);
    case 'z': return NewAssert(RegexAssertion::TextEnd, false);
    case 'Z': return NewAssert(RegexAssertion::TextEndBeforeNewline, false);
    case 't': return NewByte(0x09);
    case 'n': return NewByte(0x0a);
    case 'r': return NewByte(0x0d);
    case 'f': return NewByte(0x0c);
    case 'e': return NewByte(0x1b);
    case 'a': return NewByte(0x07);
    case 'x': {
        unsigned char byte;
        if (!ReadPerlHex(byte)) return kParseError;
        return NewByte(byte);
    }
    case 'c': {
        unsigned char byte;
        if (!ReadPerlControl(byte)) return kParseError;
        return NewByte(byte);
    }
    case '0': {
        long long value = 0;
        int count = 1;
        while (count < 3 && HasMore() && IsOctalDigit(Peek())) {
            value = value * 8 + (Peek() - '0');
            Advance();
            ++count;
        }
        if (value > 0xff) {
            Fail("octal value is greater than \\377 in 8-bit non-UTF-8 mode");
            return kParseError;
        }
        return NewByte(static_cast<unsigned char>(value));
    }
    default:
        if (c >= '1' && c <= '9') {
            m_perlBackRefs.push_back(c - '0');
            return NewBackRef(c - '0');
        }
        if (IsLetter(c) || IsDigit(c)) {
            FailEscape(static_cast<unsigned char>(c));
            return kParseError;
        }
        return NewByte(static_cast<unsigned char>(c));  // any non-alphanumeric is itself
    }
}

bool Parser::ReadPerlHex(unsigned char& byte) {
    if (PeekIs(0, '{')) {
        Advance();
        long long value = 0;
        bool any = false;
        while (HasMore() && IsHexDigit(Peek())) {
            any = true;
            value = value * 16 + HexValue(Peek());
            if (value > 0xff) value = 0x100000;
            Advance();
        }
        if (!any) {
            Fail("digits missing in \\x{} or \\o{} or \\N{U+");
            return false;
        }
        if (!PeekIs(0, '}')) {
            Fail("non-hex character in \\x{} (closing brace missing?)");
            return false;
        }
        Advance();
        if (value > 0xff) {
            Fail("character code point value in \\x{} or \\o{} is too large");
            return false;
        }
        byte = static_cast<unsigned char>(value);
        return true;
    }
    long long value = 0;
    int count = 0;
    while (count < 2 && HasMore() && IsHexDigit(Peek())) {
        value = value * 16 + HexValue(Peek());
        Advance();
        ++count;
    }
    byte = static_cast<unsigned char>(value);  // zero digits: NUL, as PCRE2
    return true;
}

bool Parser::ReadPerlControl(unsigned char& byte) {
    if (!HasMore()) {
        Fail("\\c at end of pattern");
        return false;
    }
    char d = Peek();
    // PCRE2 uppercases the letter first: \cA and \ca are both 0x01.
    char up = (d >= 'a' && d <= 'z') ? static_cast<char>(d - 'a' + 'A') : d;
    byte = static_cast<unsigned char>(up ^ 0x40);
    Advance();
    return true;
}

// ---- GNU brackets ----

int Parser::ReadGnuBracketClass(unsigned char& itemByte, std::bitset<256>& itemSet, char lead) {
    // m_pos at '[', pattern[m_pos + 1] == lead.
    size_t start = m_pos + 2;
    char close1 = lead;
    size_t i = start;
    while (i + 1 < m_pattern.size() && !(m_pattern[i] == close1 && m_pattern[i + 1] == ']')) ++i;
    if (i + 1 >= m_pattern.size()) {
        Fail("Unmatched [, [^, [:, [., or [=");
        return kParseError;
    }
    std::string_view content = m_pattern.substr(start, i - start);
    m_pos = i + 2;
    if (lead == ':') {
        if (!PosixClass(content, itemSet)) {
            Fail("Invalid character class name");
            return kParseError;
        }
        return 2;
    }
    // A collating element or equivalence class: exactly one character.
    if (content.size() != 1) {
        Fail("Invalid collation character");
        return kParseError;
    }
    itemByte = static_cast<unsigned char>(content[0]);
    return 1;
}

int Parser::ReadGnuBracketItem(unsigned char& itemByte, std::bitset<256>& itemSet) {
    char c = Peek();
    if (c == '[' && m_pos + 1 < m_pattern.size()) {
        char lead = Peek(1);
        if (lead == ':' || lead == '.' || lead == '=')
            return ReadGnuBracketClass(itemByte, itemSet, lead);
    }
    Advance();
    itemByte = static_cast<unsigned char>(c);
    return 1;
}

int Parser::ParseGnuBracket() {
    Advance();  // '['
    bool negated = false;
    if (PeekIs(0, '^')) { negated = true; Advance(); }
    std::bitset<256> set;
    bool first = true;
    bool havePrev = false;  // the previous item is a single byte that can start a range
    unsigned char prevByte = 0;
    for (;;) {
        if (!HasMore()) {
            Fail("Unmatched [, [^, [:, [., or [=");
            return kParseError;
        }
        if (PeekIs(0, ']') && !first) { Advance(); break; }
        // A '-' that is not first and not last is a range operator -- even
        // right after a range or a class, where glibc says "Invalid range end".
        if (PeekIs(0, '-') && !first && m_pos + 1 < m_pattern.size() && !PeekIs(1, ']')) {
            Advance();  // the '-'
            unsigned char endByte;
            std::bitset<256> endSet;
            int kind = ReadGnuBracketItem(endByte, endSet);
            if (kind == kParseError) return kParseError;
            if (kind == 2 || !havePrev || endByte < prevByte) {
                Fail("Invalid range end");
                return kParseError;
            }
            AddRange(set, prevByte, endByte);
            havePrev = false;
            first = false;
            continue;
        }
        unsigned char itemByte;
        std::bitset<256> itemSet;
        int kind = ReadGnuBracketItem(itemByte, itemSet);
        if (kind == kParseError) return kParseError;
        if (kind == 2) {
            set |= itemSet;
            havePrev = false;
        } else {
            set[itemByte] = true;
            havePrev = true;
            prevByte = itemByte;
        }
        first = false;
    }
    if (m_caseFold) FoldSet(set);
    if (negated) set = Complement(set);
    return NewBytesRaw(set);
}

// ---- Perl brackets ----

bool Parser::IsWholePosixClass() const {
    // m_pattern[m_pos] == '['. PCRE2 rejects a [:name:] that is the whole
    // class, with any name; [:] (nothing between) stays a class of ':'.
    size_t i = m_pos + 1;
    if (i >= m_pattern.size() || m_pattern[i] != ':') return false;
    size_t start = i + 1;
    if (start < m_pattern.size() && m_pattern[start] == '^') ++start;
    size_t j = start;
    while (j + 1 < m_pattern.size() && !(m_pattern[j] == ':' && m_pattern[j + 1] == ']')) ++j;
    return j + 1 < m_pattern.size() && j > start;
}

int Parser::ParsePerlPosixItem(std::bitset<256>& set) {
    // m_pos at '['; Peek(1) == ':'.
    size_t start = m_pos + 2;
    bool negated = false;
    if (start < m_pattern.size() && m_pattern[start] == '^') { negated = true; ++start; }
    size_t i = start;
    while (i + 1 < m_pattern.size() && !(m_pattern[i] == ':' && m_pattern[i + 1] == ']')) ++i;
    if (i + 1 >= m_pattern.size()) return -2;  // no ":]": a literal '['
    std::string_view name = m_pattern.substr(start, i - start);
    std::bitset<256> classSet;
    if (!PosixClass(name, classSet)) {
        Fail("unknown POSIX class name");
        return kParseError;
    }
    m_pos = i + 2;
    set = negated ? Complement(classSet) : classSet;
    return 1;
}

int Parser::ReadPerlClassEscape(unsigned char& byte, std::bitset<256>& set) {
    Advance();  // the backslash
    if (!HasMore()) {
        Fail("\\ at end of pattern");
        return kParseError;
    }
    char c = Peek();
    Advance();
    switch (c) {
    case 'd': set = DigitSet(); return 2;
    case 'D': set = Complement(DigitSet()); return 2;
    case 'w': set = WordSet(); return 2;
    case 'W': set = Complement(WordSet()); return 2;
    case 's': set = SpaceSet(); return 2;
    case 'S': set = Complement(SpaceSet()); return 2;
    case 'h': set = PerlHSpaceSet(); return 2;
    case 'H': set = Complement(PerlHSpaceSet()); return 2;
    case 'v': set = PerlVSpaceSet(); return 2;
    case 'V': set = Complement(PerlVSpaceSet()); return 2;
    case 'b': byte = 0x08; return 1;  // backspace inside a class
    case 't': byte = 0x09; return 1;
    case 'n': byte = 0x0a; return 1;
    case 'r': byte = 0x0d; return 1;
    case 'f': byte = 0x0c; return 1;
    case 'e': byte = 0x1b; return 1;
    case 'a': byte = 0x07; return 1;
    case 'x': return ReadPerlHex(byte) ? 1 : kParseError;
    case 'c': return ReadPerlControl(byte) ? 1 : kParseError;
    default:
        if (c >= '0' && c <= '9') {
            long long value = c - '0';
            int count = 1;
            while (count < 3 && HasMore() && IsOctalDigit(Peek())) {
                value = value * 8 + (Peek() - '0');
                Advance();
                ++count;
            }
            if (value > 0xff) {
                Fail("octal value is greater than \\377 in 8-bit non-UTF-8 mode");
                return kParseError;
            }
            byte = static_cast<unsigned char>(value);
            return 1;
        }
        if (IsLetter(c)) {
            FailEscape(static_cast<unsigned char>(c));
            return kParseError;
        }
        byte = static_cast<unsigned char>(c);  // \] \\ \- ... are themselves
        return 1;
    }
}

int Parser::ParsePerlBracket() {
    Advance();  // '['
    bool negated = false;
    if (PeekIs(0, '^')) { negated = true; Advance(); }
    std::bitset<256> set;
    bool first = true;
    for (;;) {
        if (!HasMore()) {
            Fail("missing terminating ] for character class");
            return kParseError;
        }
        if (PeekIs(0, ']') && !first) { Advance(); break; }
        // A '-' first (after an optional '^') is a literal member.
        if (first && PeekIs(0, '-')) {
            Advance();
            set['-'] = true;
            first = false;
            continue;
        }
        unsigned char itemByte = 0;
        std::bitset<256> itemSet;
        bool isSet = false;
        char c = Peek();
        if (c == '[' && PeekIs(1, ':')) {
            int kind = ParsePerlPosixItem(itemSet);
            if (kind == kParseError) return kParseError;
            if (kind == -2) { itemByte = '['; Advance(); }
            else isSet = true;
        } else if (c == '\\') {
            int kind = ReadPerlClassEscape(itemByte, itemSet);
            if (kind == kParseError) return kParseError;
            isSet = (kind == 2);
        } else {
            Advance();
            itemByte = static_cast<unsigned char>(c);
        }
        first = false;
        // A class cannot be a range endpoint.
        bool rangeOp = PeekIs(0, '-') && m_pos + 1 < m_pattern.size() && !PeekIs(1, ']');
        if (isSet) {
            set |= itemSet;
            if (rangeOp) {
                Fail("invalid range in character class");
                return kParseError;
            }
            continue;
        }
        if (rangeOp) {
            Advance();  // the '-'
            unsigned char endByte = 0;
            std::bitset<256> endSet;
            char e = Peek();
            if (!HasMore()) {
                Fail("missing terminating ] for character class");
                return kParseError;
            }
            if (e == '[' && PeekIs(1, ':')) {
                int kind = ParsePerlPosixItem(endSet);
                if (kind == kParseError) return kParseError;
                if (kind == 1) { Fail("invalid range in character class"); return kParseError; }
                if (kind == -2) { Advance(); endByte = '['; }
                else { Fail("invalid range in character class"); return kParseError; }
            } else if (e == '\\') {
                int kind = ReadPerlClassEscape(endByte, endSet);
                if (kind == kParseError) return kParseError;
                if (kind == 2) { Fail("invalid range in character class"); return kParseError; }
            } else {
                Advance();
                endByte = static_cast<unsigned char>(e);
            }
            if (endByte < itemByte) {
                Fail("range out of order in character class");
                return kParseError;
            }
            AddRange(set, itemByte, endByte);
            continue;
        }
        set[itemByte] = true;
    }
    if (m_caseFold) FoldSet(set);
    if (negated) set = Complement(set);
    return NewBytesRaw(set);
}

// ---- GNU intervals ----

bool Parser::ReadGnuDigits(long long& value, bool& empty) {
    value = 0;
    empty = true;
    while (HasMore() && IsDigit(Peek())) {
        empty = false;
        if (value <= kMaxGnuRepeat) value = value * 10 + (Peek() - '0');
        Advance();
    }
    if (value > kMaxGnuRepeat) value = kMaxGnuRepeat + 1;
    return true;
}

// m_pos at the '{' (Basic has consumed the backslash before it). The closing
// token is '\}' in Basic, '}' in Extended.
bool Parser::ParseGnuInterval(int& min, int& max) {
    auto closeIs = [&]() { return IsBasic() ? (PeekIs(0, '\\') && PeekIs(1, '}')) : PeekIs(0, '}'); };
    long long m, n;
    bool mEmpty;
    Advance();  // the '{'
    if (!ReadGnuDigits(m, mEmpty)) return false;
    if (HasMore() && Peek() == ',') {
        Advance();
        bool nEmpty;
        if (!ReadGnuDigits(n, nEmpty)) return false;
        if (!HasMore()) { Fail("Unmatched \\{"); return false; }
        if (!closeIs()) { Fail("Invalid content of \\{\\}"); return false; }
        Advance(IsBasic() ? 2 : 1);
        min = mEmpty ? 0 : static_cast<int>(m);
        max = nEmpty ? -1 : static_cast<int>(n);
    } else {
        if (!HasMore()) { Fail("Unmatched \\{"); return false; }
        if (!closeIs()) { Fail("Invalid content of \\{\\}"); return false; }
        Advance(IsBasic() ? 2 : 1);
        if (mEmpty) { Fail("Invalid content of \\{\\}"); return false; }
        min = max = static_cast<int>(m);
    }
    if (min > kMaxGnuRepeat || (max != -1 && max > kMaxGnuRepeat)) {
        Fail("Regular expression too big");
        return false;
    }
    if (max != -1 && min > max) {
        Fail("Invalid content of \\{\\}");
        return false;
    }
    return true;
}

size_t Parser::PerlQuantifierShape(long long& min, long long& max, bool& maxBounded) const {
    min = 0;
    max = 0;
    maxBounded = true;
    size_t i = m_pos + 1;
    auto readNumber = [&](long long& value, bool& empty) {
        value = 0;
        empty = true;
        while (i < m_pattern.size() && IsDigit(m_pattern[i])) {
            empty = false;
            if (value <= kMaxPerlRepeat) value = value * 10 + (m_pattern[i] - '0');
            ++i;
        }
        if (value > kMaxPerlRepeat) value = kMaxPerlRepeat + 1;
    };
    long long m, n;
    bool mEmpty;
    readNumber(m, mEmpty);
    if (mEmpty) return kNpos;  // {,3} is a literal '{' (PCRE2)
    if (i < m_pattern.size() && m_pattern[i] == ',') {
        ++i;
        bool nEmpty;
        readNumber(n, nEmpty);
        if (i >= m_pattern.size() || m_pattern[i] != '}') return kNpos;
        min = m;
        max = nEmpty ? 0 : n;
        maxBounded = !nEmpty;
        return i + 1;
    }
    if (i >= m_pattern.size() || m_pattern[i] != '}') return kNpos;
    min = max = m;
    return i + 1;
}

} // namespace

bool ParseRegex(std::string_view pattern, const RegexOptions& options, RegexTree& tree, std::string& error) {
    tree = RegexTree{};
    Parser parser(pattern, options, tree, error);
    return parser.Run();
}

} // namespace Haisos