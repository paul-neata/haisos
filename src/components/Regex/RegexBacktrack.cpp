#include "RegexProgram.h"

#include <algorithm>

#include "Regex.h"

namespace Haisos {
namespace {

bool BackRefBytesEqual(std::string_view text, size_t a, size_t b, size_t len, bool ignoreCase) {
    for (size_t i = 0; i < len; ++i) {
        unsigned char x = static_cast<unsigned char>(text[a + i]);
        unsigned char y = static_cast<unsigned char>(text[b + i]);
        if (ignoreCase) {  // ASCII fold
            if (x >= 'A' && x <= 'Z') x = static_cast<unsigned char>(x - 'A' + 'a');
            if (y >= 'A' && y <= 'Z') y = static_cast<unsigned char>(y - 'A' + 'a');
        }
        if (x != y) return false;
    }
    return true;
}

bool PossibleStart(const RegexProgram& program, std::string_view text, size_t p) {
    if (p > text.size()) return false;
    if (program.anchoredAtTextStart && p != 0) return false;
    if (program.canSkip) {
        if (p == text.size()) return false;
        if (!program.firstBytes[static_cast<unsigned char>(text[p])]) return false;
    }
    return true;
}

// The depth-first engine, for programs with back-references (they cannot be
// simulated in parallel): at each start, the program is walked in priority
// order with an explicit stack, so nothing recurses and no input byte can
// overflow the call stack. As in GNU, patterns with back-references can
// take exponential time.
bool Run(const RegexProgram& program, std::string_view text, size_t start, int flags,
         std::vector<std::ptrdiff_t>& out) {
    const size_t n = text.size();
    if (start > n) return false;

    struct Frame {
        bool isTry = false;
        int pc = 0;                  // Try: the pc to run
        size_t p = 0;                // Try: the position to run it at
        int slot = 0;                // restore: the slot (capture or mark)
        std::ptrdiff_t value = -1;   // restore: the value to write back
    };

    std::vector<std::ptrdiff_t> slots(program.slotCount, -1);
    std::vector<std::ptrdiff_t> marks(program.markCount, -1);
    std::vector<std::ptrdiff_t> best(program.slotCount, -1);
    std::vector<Frame> stack;

    for (size_t s = start; s <= n; ++s) {
        if (!PossibleStart(program, text, s)) continue;
        std::fill(slots.begin(), slots.end(), -1);
        std::fill(marks.begin(), marks.end(), -1);
        stack.clear();
        stack.push_back(Frame{true, 0, s, 0, -1});
        bool have = false;
        std::ptrdiff_t bestEnd = -1;

        while (!stack.empty()) {
            Frame frame = stack.back();
            stack.pop_back();
            if (!frame.isTry) {
                if (frame.slot < static_cast<int>(slots.size()))
                    slots[frame.slot] = frame.value;
                else
                    marks[frame.slot - static_cast<int>(slots.size())] = frame.value;
                continue;
            }
            const RegexInstruction& inst = program.instructions[frame.pc];
            size_t p = frame.p;
            switch (inst.op) {
            case RegexOp::Byte:
                if (p < n && program.byteSets[inst.x][static_cast<unsigned char>(text[p])])
                    stack.push_back(Frame{true, frame.pc + 1, p + 1, 0, -1});
                break;
            case RegexOp::Jump:
                stack.push_back(Frame{true, inst.x, p, 0, -1});
                break;
            case RegexOp::Split:  // preferred target first
                stack.push_back(Frame{true, inst.y, p, 0, -1});
                stack.push_back(Frame{true, inst.x, p, 0, -1});
                break;
            case RegexOp::Save:
                stack.push_back(Frame{false, 0, 0, inst.x, slots[inst.x]});
                slots[inst.x] = static_cast<std::ptrdiff_t>(p);
                stack.push_back(Frame{true, frame.pc + 1, p, 0, -1});
                break;
            case RegexOp::Assert:
                if (RegexAssertionHolds(inst, text, p, flags))
                    stack.push_back(Frame{true, frame.pc + 1, p, 0, -1});
                break;
            case RegexOp::BackRef: {
                std::ptrdiff_t groupStart = slots[2 * inst.x];
                std::ptrdiff_t groupEnd = slots[2 * inst.x + 1];
                if (groupStart < 0 || groupEnd < 0) break;  // unset: fails, as in GNU
                size_t len = static_cast<size_t>(groupEnd - groupStart);
                if (p + len <= n && BackRefBytesEqual(text, groupStart, p, len, inst.ignoreCase))
                    stack.push_back(Frame{true, frame.pc + 1, p + len, 0, -1});
                break;
            }
            case RegexOp::ProgressMark:
                stack.push_back(Frame{false, 0, 0,
                                      static_cast<int>(slots.size()) + inst.x, marks[inst.x]});
                marks[inst.x] = static_cast<std::ptrdiff_t>(p);
                stack.push_back(Frame{true, frame.pc + 1, p, 0, -1});
                break;
            case RegexOp::ProgressCheck:  // an empty iteration: leave, keeping its captures
                stack.push_back(Frame{true, marks[inst.x] == static_cast<std::ptrdiff_t>(p)
                                           ? inst.y
                                           : frame.pc + 1,
                                      p, 0, -1});
                break;
            case RegexOp::Match:
                if (!program.longest) {  // leftmost-first: the first match wins
                    best = slots;
                    have = true;
                    stack.clear();
                    break;
                }
                if (!have || static_cast<std::ptrdiff_t>(p) > bestEnd) {  // ties keep the first
                    best = slots;
                    bestEnd = static_cast<std::ptrdiff_t>(p);
                    have = true;
                }
                if (p == n) {  // no match from this start can be longer
                    stack.clear();
                }
                break;
            }
        }
        if (have) {
            out.assign(best.begin(), best.end());
            return true;
        }
    }
    return false;
}

} // namespace

bool BacktrackSearch(const RegexProgram& program, std::string_view text, size_t start, int flags, std::vector<std::ptrdiff_t>& slots) {
    return Run(program, text, start, flags, slots);
}

} // namespace Haisos