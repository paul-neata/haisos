#include "RegexProgram.h"

#include <algorithm>

#include "Regex.h"

namespace Haisos {
namespace {

// The Pike VM: all candidate threads advance in lockstep, one byte of text
// per step, at most one thread per program counter per list (a lower-priority
// way to the same pc is dropped: at one position it has the same future).
// Used for every program without back-references: O(text x program) whatever
// the pattern, with no recursion at all -- the closure over the Jump/Split/
// Save paths runs on an explicit stack, so neither a long text nor a deeply
// nested pattern can grow the call stack.
class PikeVM {
public:
    explicit PikeVM(const RegexProgram& program)
        : m_program(program), m_longest(program.longest), m_slotCount(program.slotCount) {}

    bool Run(std::string_view text, size_t start, int flags, std::vector<std::ptrdiff_t>& out);

private:
    // The explicit closure stack: pc >= 0 explores that pc; pc == -1 restores
    // |slot| to |value| (what a Save on a sibling path overwrote).
    struct Entry {
        int pc = 0;
        int slot = 0;
        std::ptrdiff_t value = -1;
    };

    struct Thread {
        int pc = 0;
        size_t block = 0;  // offset into ThreadList::slots, in elements
    };

    struct ThreadList {
        std::vector<Thread> threads;        // in priority order
        std::vector<int> sparse;            // pcs in the list, stamped with the generation
        int generation = 0;                 // bumping this clears the sparse set in O(1)
        std::vector<std::ptrdiff_t> slots;  // slot storage: a bump arena of blocks
        size_t used = 0;                    // arena top, in elements

        void Clear() {
            threads.clear();
            ++generation;  // the sparse set is cleared without touching it
            used = 0;      // the arena is reused position by position, never freed
        }
    };

    const RegexProgram& m_program;
    bool m_longest;
    size_t m_slotCount;
    std::string_view m_text;
    size_t m_n = 0;
    int m_flags = 0;
    ThreadList m_current;
    ThreadList m_next;
    std::vector<std::ptrdiff_t> m_work;   // one working slot array for closures
    std::vector<std::ptrdiff_t> m_fresh;  // all -1: a new thread's slots
    std::vector<Entry> m_stack;
    std::vector<std::ptrdiff_t> m_result;

    bool PossibleStart(size_t p) const {
        if (p > m_n) return false;
        if (m_program.anchoredAtTextStart && p != 0) return false;
        if (m_program.canSkip) {
            if (p == m_n) return false;
            if (!m_program.firstBytes[static_cast<unsigned char>(m_text[p])]) return false;
        }
        return true;
    }

    // Adds the thread starting at |pc| (with |slots|, copied) at position
    // |pos| to |list|: follows Jump, Split (preferred target first), Save,
    // ProgressMark/Check and Assert until every path has reached a Byte or a
    // Match, appending one thread per such pc unless it is already there.
    // Iterative: the preferred target is walked in place, and only the other
    // target of a Split (and the value a Save overwrote, for the branches
    // after it) go on the stack.
    void AddThread(ThreadList& list, int pc, const std::ptrdiff_t* slots, size_t pos) {
        std::copy(slots, slots + m_slotCount, m_work.begin());
        size_t top = 0;
        size_t branches = 0;  // pc-entries in the stack: the paths still to walk
        int cur = pc;
        for (;;) {
            if (list.sparse[cur] != list.generation) {
                list.sparse[cur] = list.generation;
                const RegexInstruction& inst = m_program.instructions[cur];
                switch (inst.op) {
                case RegexOp::Byte:
                case RegexOp::Match: {
                    size_t block = list.used;
                    list.used += m_slotCount;
                    if (list.used > list.slots.size()) list.slots.resize(list.used);
                    std::copy(m_work.begin(), m_work.end(), list.slots.begin() + block);
                    list.threads.push_back(Thread{cur, block});
                    break;  // this path ends here
                }
                case RegexOp::Jump:
                    cur = inst.x;
                    continue;
                case RegexOp::Split:
                    m_stack[top++] = Entry{inst.y, 0, -1};
                    ++branches;
                    cur = inst.x;  // the preferred target, in place
                    continue;
                case RegexOp::Save:
                    // The overwritten value must come back only for a branch
                    // still stacked below; with none, no later path of this
                    // closure can observe it.
                    if (branches > 0) m_stack[top++] = Entry{-1, inst.x, m_work[inst.x]};
                    m_work[inst.x] = static_cast<std::ptrdiff_t>(pos);
                    cur = cur + 1;
                    continue;
                case RegexOp::Assert:
                    if (RegexAssertionHolds(inst, m_text, pos, m_flags)) {
                        cur = cur + 1;
                        continue;
                    }
                    break;  // the assertion fails: this path dies
                case RegexOp::ProgressMark:
                case RegexOp::ProgressCheck:  // no-ops: one thread per pc stops empty loops
                    cur = cur + 1;
                    continue;
                case RegexOp::BackRef:
                    break;  // unreachable: Pike runs only programs without back-references
                }
            }
            // The path through |cur| is done: take the next stacked branch,
            // restoring what a Save on a sibling path overwrote.
            bool resumed = false;
            while (top > 0) {
                Entry entry = m_stack[--top];
                if (entry.pc >= 0) {
                    cur = entry.pc;
                    --branches;
                    resumed = true;
                    break;
                }
                m_work[entry.slot] = entry.value;
            }
            if (!resumed) return;  // every way through has been walked
        }
    }

    // The result of a Match thread at position |p|: the priority order of the
    // two lists decides, as the two modes spell out.
    void RecordMatch(const std::ptrdiff_t* slots, size_t p, bool& haveResult,
                     std::ptrdiff_t& resultStart, std::ptrdiff_t& resultEnd, bool& stopWalk) {
        if (!m_longest) {
            // Leftmost-first: the highest-priority surviving match wins; any
            // match recorded before this one came from a lower-priority
            // thread (those died when it was recorded).
            std::copy(slots, slots + m_slotCount, m_result.begin());
            haveResult = true;
            stopWalk = true;  // everything after this thread is lower priority
            return;
        }
        // Leftmost-longest: record the smaller start, else the same start with
        // a larger end; never on a tie (the first way in priority order wins).
        std::ptrdiff_t matchStart = slots[0];
        std::ptrdiff_t matchEnd = static_cast<std::ptrdiff_t>(p);
        if (!haveResult || matchStart < resultStart ||
            (matchStart == resultStart && matchEnd > resultEnd)) {
            std::copy(slots, slots + m_slotCount, m_result.begin());
            resultStart = matchStart;
            resultEnd = matchEnd;
            haveResult = true;
        }
    }
};

bool PikeVM::Run(std::string_view text, size_t start, int flags, std::vector<std::ptrdiff_t>& out) {
    m_text = text;
    m_n = text.size();
    m_flags = flags;
    if (start > m_n) return false;
    m_current.sparse.assign(m_program.instructions.size(), 0);
    m_next.sparse.assign(m_program.instructions.size(), 0);
    m_current.generation = 1;
    m_next.generation = 1;
    m_current.threads.reserve(m_program.instructions.size());
    m_next.threads.reserve(m_program.instructions.size());
    m_stack.reserve(m_program.instructions.size());
    m_work.assign(m_slotCount, -1);
    m_fresh.assign(m_slotCount, -1);
    m_result.assign(m_slotCount, -1);
    bool haveResult = false;
    std::ptrdiff_t resultStart = -1;
    std::ptrdiff_t resultEnd = -1;

    size_t p = start;
    while (true) {
        if (!haveResult && PossibleStart(p)) AddThread(m_current, 0, m_fresh.data(), p);
        if (m_current.threads.empty()) {
            if (haveResult) break;
            size_t q = p;  // nothing is alive: jump to the next possible start
            do {
                ++q;
            } while (q <= m_n && !PossibleStart(q));
            if (q > m_n) break;
            m_current.Clear();  // a closure that died at p marked pcs for p
            p = q;
            continue;
        }

        bool stopWalk = false;
        for (size_t i = 0; i < m_current.threads.size() && !stopWalk; ++i) {
            int pc = m_current.threads[i].pc;
            const RegexInstruction& inst = m_program.instructions[pc];
            const std::ptrdiff_t* slots = m_current.slots.data() + m_current.threads[i].block;
            if (inst.op == RegexOp::Match) {
                RecordMatch(slots, p, haveResult, resultStart, resultEnd, stopWalk);
                continue;
            }
            if (inst.op != RegexOp::Byte) continue;  // only Byte and Match are listed
            if (m_longest && haveResult && slots[0] > resultStart)
                continue;  // a later start can only do worse
            if (p < m_n && m_program.byteSets[inst.x][static_cast<unsigned char>(m_text[p])]) {
                // A continuation already in next would be dropped by AddThread
                // at its first check; looking before the slot copy is the same.
                if (m_next.sparse[pc + 1] == m_next.generation) continue;
                AddThread(m_next, pc + 1, slots, p + 1);
            }
        }

        if (m_next.threads.empty() && haveResult) break;
        if (p == m_n) break;
        std::swap(m_current, m_next);
        m_next.Clear();
        ++p;
    }

    if (!haveResult) return false;
    out.assign(m_result.begin(), m_result.end());
    return true;
}

} // namespace

bool PikeSearch(const RegexProgram& program, std::string_view text, size_t start, int flags, std::vector<std::ptrdiff_t>& slots) {
    PikeVM vm(program);
    return vm.Run(text, start, flags, slots);
}

} // namespace Haisos