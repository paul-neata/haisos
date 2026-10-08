#include "RegexProgram.h"

#include "Regex.h"

namespace Haisos {

// Whether |instruction| (an Assert) holds at position |p| of |text|, with
// |flags| (kRegexNotBol/kRegexNotEol). Outside the text counts as a non-word
// byte. Shared by the two engines.
bool RegexAssertionHolds(const RegexInstruction& instruction, std::string_view text, size_t p, int flags) {
    const size_t n = text.size();
    auto word = [&](size_t i) {
        if (i >= n) return false;  // outside the text is a non-word byte
        unsigned char c = static_cast<unsigned char>(text[i]);
        return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
    };
    switch (instruction.assertion) {
    case RegexAssertion::LineStart:
        return (p == 0 && !(flags & kRegexNotBol)) ||
               (instruction.multiline && p > 0 && text[p - 1] == '\n');
    case RegexAssertion::LineEnd:
        return (p == n && !(flags & kRegexNotEol)) ||
               (instruction.multiline && p < n && text[p] == '\n');
    case RegexAssertion::LineEndPerl:
        return !(flags & kRegexNotEol) && (p == n || (p == n - 1 && text[p] == '\n'));
    case RegexAssertion::TextStart:
        return p == 0;
    case RegexAssertion::TextEnd:
        return p == n;
    case RegexAssertion::TextEndBeforeNewline:
        return p == n || (p == n - 1 && text[p] == '\n');
    case RegexAssertion::WordBoundary:
        return word(p > 0 ? p - 1 : 0) != word(p);
    case RegexAssertion::NotWordBoundary:
        return word(p > 0 ? p - 1 : 0) == word(p);
    case RegexAssertion::WordStart:
        return !word(p > 0 ? p - 1 : 0) && word(p);
    case RegexAssertion::WordEnd:
        return word(p > 0 ? p - 1 : 0) && !word(p);
    }
    return false;
}

namespace {

// Sizes saturate at this, so a{1000}{1000} is rejected in one multiplication.
constexpr size_t kSizeCap = kMaxRegexInstructions + 1;

size_t Saturate(size_t v) { return v > kSizeCap ? kSizeCap : v; }
size_t SaturatingAdd(size_t a, size_t b) { return Saturate(a + b); }
size_t SaturatingMul(size_t a, size_t b) {
    if (a == 0 || b == 0) return 0;
    if (a > kSizeCap / b) return kSizeCap;
    return Saturate(a * b);
}

class ProgramBuilder {
public:
    ProgramBuilder(const RegexTree& tree, RegexProgram& program)
        : m_tree(tree), m_program(program), m_nullable(tree.nodes.size(), 0) {}

    bool Run(RegexSyntax syntax, std::string& error) {
        m_program.longest = syntax != RegexSyntax::Perl;
        m_program.hasBackReferences = m_tree.hasBackReferences;
        m_program.slotCount = 2 * (m_tree.groupCount + 1);

        size_t total = SaturatingAdd(4, m_tree.root >= 0 ? NodeSize(m_tree.root) : 0);
        if (total > kMaxRegexInstructions) {
            error = syntax == RegexSyntax::Perl ? "regular expression is too large"
                                                : "Regular expression too big";
            return false;
        }

        Add(RegexOp::Save, 0);
        if (m_tree.root >= 0) EmitNode(m_tree.root);
        Add(RegexOp::Save, 1);
        Add(RegexOp::Match);

        ComputeFirstBytes();
        ComputeAnchoredAtTextStart();
        return true;
    }

private:
    const RegexTree& m_tree;
    RegexProgram& m_program;
    std::vector<char> m_nullable;  // 0 unknown, 1 nullable, 2 not

    void Add(RegexOp op, int x = 0) {
        RegexInstruction inst;
        inst.op = op;
        inst.x = x;
        m_program.instructions.push_back(inst);
    }

    int CurrentPc() const { return static_cast<int>(m_program.instructions.size()); }

    // Whether a node can match the empty string. A BackRef counts as nullable
    // (its group may have captured nothing); a Repeat whose body is nullable
    // is nullable whatever its min, so a loop around it always gets the
    // ProgressMark/ProgressCheck pair that keeps the backtracker finite.
    bool Nullable(int idx) {
        char& state = m_nullable[idx];
        if (state != 0) return state == 1;
        const RegexNode& node = m_tree.nodes[idx];
        bool result = false;
        switch (node.type) {
        case RegexNodeType::Empty:
        case RegexNodeType::Assert:
        case RegexNodeType::BackRef:
            result = true;
            break;
        case RegexNodeType::Bytes:
            result = false;
            break;
        case RegexNodeType::Group:
            result = Nullable(node.children[0]);
            break;
        case RegexNodeType::Concat:
            result = true;
            for (int child : node.children) result = result && Nullable(child);
            break;
        case RegexNodeType::Alternate:
            for (int child : node.children) {
                if (Nullable(child)) {
                    result = true;
                    break;
                }
            }
            break;
        case RegexNodeType::Repeat:
            result = node.min == 0 || Nullable(node.children[0]);
            break;
        }
        state = result ? 1 : 2;
        return result;
    }

    size_t NodeSize(int idx) {
        const RegexNode& node = m_tree.nodes[idx];
        size_t body = 0;
        bool oneBody = node.type == RegexNodeType::Group || node.type == RegexNodeType::Repeat;
        if (oneBody) body = NodeSize(node.children[0]);
        switch (node.type) {
        case RegexNodeType::Empty:
            return 0;
        case RegexNodeType::Bytes:
        case RegexNodeType::Assert:
        case RegexNodeType::BackRef:
            return 1;
        case RegexNodeType::Group:
            return SaturatingAdd(body, 2);
        case RegexNodeType::Concat: {
            size_t size = 0;
            for (int child : node.children) size = SaturatingAdd(size, NodeSize(child));
            return size;
        }
        case RegexNodeType::Alternate: {
            size_t size = 0;
            for (int child : node.children) size = SaturatingAdd(size, NodeSize(child));
            // k - 1 Splits and k - 1 Jumps around the k children
            return SaturatingAdd(size, SaturatingMul(2, node.children.size() - 1));
        }
        case RegexNodeType::Repeat: {
            size_t size = SaturatingMul(body, node.min);
            if (node.max == -1) {
                // Split, [ProgressMark,] body, [ProgressCheck,] Jump
                return SaturatingAdd(size, SaturatingAdd(3, Nullable(node.children[0]) ? 2 : 0));
            }
            size_t optional = static_cast<size_t>(node.max - node.min);
            // optional copies, each opened by a Split
            return SaturatingAdd(size, SaturatingAdd(SaturatingMul(body, optional), optional));
        }
        }
        return 0;
    }

    int ByteSetIndex(const std::bitset<256>& bytes) {
        for (size_t i = 0; i < m_program.byteSets.size(); ++i)
            if (m_program.byteSets[i] == bytes) return static_cast<int>(i);
        int index = static_cast<int>(m_program.byteSets.size());
        m_program.byteSets.push_back(bytes);
        return index;
    }

    void EmitNode(int idx) {
        const RegexNode& node = m_tree.nodes[idx];
        switch (node.type) {
        case RegexNodeType::Empty:
            break;
        case RegexNodeType::Bytes: {
            RegexInstruction inst;
            inst.op = RegexOp::Byte;
            inst.x = ByteSetIndex(node.bytes);
            m_program.instructions.push_back(inst);
            break;
        }
        case RegexNodeType::Concat:
            for (int child : node.children) EmitNode(child);
            break;
        case RegexNodeType::Alternate:
            EmitAlternate(node);
            break;
        case RegexNodeType::Group:
            Add(RegexOp::Save, 2 * node.group);
            EmitNode(node.children[0]);
            Add(RegexOp::Save, 2 * node.group + 1);
            break;
        case RegexNodeType::Assert: {
            RegexInstruction inst;
            inst.op = RegexOp::Assert;
            inst.assertion = node.assertion;
            inst.multiline = node.multiline;
            m_program.instructions.push_back(inst);
            break;
        }
        case RegexNodeType::BackRef: {
            RegexInstruction inst;
            inst.op = RegexOp::BackRef;
            inst.x = node.group;
            inst.ignoreCase = node.ignoreCase;
            m_program.instructions.push_back(inst);
            break;
        }
        case RegexNodeType::Repeat:
            EmitRepeat(node);
            break;
        }
    }

    void EmitAlternate(const RegexNode& node) {
        std::vector<int> jumps;
        for (size_t i = 0; i + 1 < node.children.size(); ++i) {
            int split = CurrentPc();
            Add(RegexOp::Split);  // x = this alternative, y = the next Split
            m_program.instructions[split].x = split + 1;
            EmitNode(node.children[i]);
            jumps.push_back(CurrentPc());
            Add(RegexOp::Jump);
            m_program.instructions[split].y = CurrentPc();
        }
        EmitNode(node.children.back());
        int end = CurrentPc();
        for (int jump : jumps) m_program.instructions[jump].x = end;
    }

    void EmitRepeat(const RegexNode& node) {
        for (int i = 0; i < node.min; ++i) EmitNode(node.children[0]);
        if (node.max == -1) {
            // A body that can match empty completes one such iteration -- its
            // captures recorded, as glibc and PCRE2 both report them -- and
            // the ProgressCheck at the loop's bottom then leaves the loop
            // instead of turning that iteration into an infinite one. The
            // check must sit at the bottom, not the head: the wrap-around of
            // an empty iteration reaches it for the first time, before any
            // engine's visited-set could drop the path.
            bool guard = Nullable(node.children[0]);
            int markSlot = -1;
            if (guard) markSlot = static_cast<int>(m_program.markCount++);
            int loop = CurrentPc();
            int split = CurrentPc();
            Add(RegexOp::Split);
            int body = CurrentPc();
            if (guard) Add(RegexOp::ProgressMark, markSlot);
            EmitNode(node.children[0]);
            int check = -1;
            if (guard) check = CurrentPc();
            if (guard) Add(RegexOp::ProgressCheck, markSlot);  // y = out, set below
            Add(RegexOp::Jump, loop);
            int out = CurrentPc();
            if (guard) m_program.instructions[check].y = out;
            m_program.instructions[split].x = node.greedy ? body : out;
            m_program.instructions[split].y = node.greedy ? out : body;
            return;
        }
        size_t optional = static_cast<size_t>(node.max - node.min);
        if (optional == 0) return;
        std::vector<int> splits;
        for (size_t i = 0; i < optional; ++i) {
            splits.push_back(CurrentPc());
            Add(RegexOp::Split);
            EmitNode(node.children[0]);
        }
        int out = CurrentPc();
        for (int split : splits) {
            m_program.instructions[split].x = node.greedy ? split + 1 : out;
            m_program.instructions[split].y = node.greedy ? out : split + 1;
        }
    }

    // The bytes a match can start with, and whether every match must consume
    // one of them first (so a position whose byte is not in the set, or the
    // end of the text, can never start a match). Assertions are treated as
    // passing: the superset is safe.
    void ComputeFirstBytes() {
        m_program.firstBytes.reset();
        m_program.canSkip = true;
        std::vector<char> visited(m_program.instructions.size(), 0);
        std::vector<int> stack;
        stack.push_back(0);
        while (!stack.empty()) {
            int pc = stack.back();
            stack.pop_back();
            if (visited[pc]) continue;
            visited[pc] = 1;
            const RegexInstruction& inst = m_program.instructions[pc];
            switch (inst.op) {
            case RegexOp::Byte:
                m_program.firstBytes |= m_program.byteSets[inst.x];
                break;
            case RegexOp::Match:
            case RegexOp::BackRef:
                m_program.canSkip = false;  // an empty match may be possible
                break;
            case RegexOp::Save:
            case RegexOp::ProgressMark:
                stack.push_back(pc + 1);
                break;
            case RegexOp::ProgressCheck:  // leaves the loop after an empty iteration
                stack.push_back(inst.y);
                stack.push_back(pc + 1);
                break;
            case RegexOp::Jump:
                stack.push_back(inst.x);
                break;
            case RegexOp::Split:
                stack.push_back(inst.y);
                stack.push_back(inst.x);
                break;
            case RegexOp::Assert:
                stack.push_back(pc + 1);
                break;
            }
        }
    }

    // \` and a non-multiline ^ at the very start: no match can start past 0.
    void ComputeAnchoredAtTextStart() {
        m_program.anchoredAtTextStart = false;
        if (m_tree.root < 0) return;
        int first = m_tree.root;
        const RegexNode& root = m_tree.nodes[m_tree.root];
        if (root.type == RegexNodeType::Concat && !root.children.empty()) first = root.children[0];
        const RegexNode& node = m_tree.nodes[first];
        if (node.type == RegexNodeType::Assert &&
            (node.assertion == RegexAssertion::TextStart ||
             (node.assertion == RegexAssertion::LineStart && !node.multiline)))
            m_program.anchoredAtTextStart = true;
    }
};

} // namespace

bool CompileRegexProgram(const RegexTree& tree, RegexSyntax syntax, RegexProgram& program, std::string& error) {
    program = RegexProgram{};
    ProgramBuilder builder(tree, program);
    return builder.Run(syntax, error);
}

} // namespace Haisos