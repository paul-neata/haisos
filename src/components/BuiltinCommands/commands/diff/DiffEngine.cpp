#include <algorithm>
#include <cctype>
#include <cstdint>
#include <deque>
#include <string>
#include <unordered_map>
#include <vector>
#include "commands/diff/DiffEngine.h"

namespace Haisos {

namespace {

bool IsSpaceByte(unsigned char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' || c == '\r';
}

// The key of one line, normalised for the mode (see the header). The line is
// given without its '\n'. An incomplete last line is marked with a trailing
// '\x01' (a byte that cannot appear in a key any mode would strip, and one
// no complete line's key ends with), so it equals only the other file's
// incomplete last line.
std::string MakeLineKey(std::string_view line, const DiffLineOptions& options, bool incomplete) {
    std::string key;
    key.reserve(line.size() + 1);
    const size_t tabSize = options.tabSize ? options.tabSize : 8;
    switch (options.whiteSpace) {
        case DiffWhiteSpace::None:
            key.assign(line.data(), line.size());
            break;
        case DiffWhiteSpace::AllSpace:
            for (char c : line) {
                if (!IsSpaceByte(static_cast<unsigned char>(c))) {
                    key += c;
                }
            }
            break;
        case DiffWhiteSpace::SpaceChange: {
            // Each run of whitespace one space, a run at the end removed.
            size_t end = line.size();
            while (end > 0 && IsSpaceByte(static_cast<unsigned char>(line[end - 1]))) {
                --end;
            }
            bool pendingSpace = false;
            for (size_t i = 0; i < end; ++i) {
                if (IsSpaceByte(static_cast<unsigned char>(line[i]))) {
                    pendingSpace = true;
                } else {
                    if (pendingSpace) {
                        key += ' ';
                        pendingSpace = false;
                    }
                    key += line[i];
                }
            }
            break;
        }
        case DiffWhiteSpace::TrailingSpace: {
            size_t end = line.size();
            while (end > 0 && IsSpaceByte(static_cast<unsigned char>(line[end - 1]))) {
                --end;
            }
            key.assign(line.data(), end);
            break;
        }
        case DiffWhiteSpace::TabExpansion: {
            // Tabs expanded to the next multiple of tabSize, every byte one
            // column, up to the first '\b' or '\r'; after that the line is
            // kept as is.
            size_t column = 0;
            size_t i = 0;
            for (; i < line.size(); ++i) {
                const char c = line[i];
                if (c == '\b' || c == '\r') {
                    break;
                }
                if (c == '\t') {
                    const size_t to = (column / tabSize + 1) * tabSize;
                    key.append(to - column, ' ');
                    column = to;
                } else {
                    key += c;
                    ++column;
                }
            }
            key.append(line.substr(i));
            break;
        }
    }
    if (options.ignoreCase) {
        for (char& c : key) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
    }
    if (incomplete) {
        key += '\x01';
    }
    return key;
}

// One region being compared by the linear-space search: [x0, x1) of file 0's
// key numbers against [y0, y1) of file 1's. The greedy tables are indexed by
// diagonal k + an offset so -M..N fit.
struct Box {
    int64_t x0, y0, x1, y1;
};

} // namespace

DiffText MakeDiffText(std::string bytes, bool stripTrailingCr, bool robustOutput) {
    DiffText text;
    if (stripTrailingCr) {
        std::string stripped;
        stripped.reserve(bytes.size());
        for (size_t i = 0; i < bytes.size(); ++i) {
            if (bytes[i] == '\r' && i + 1 < bytes.size() && bytes[i + 1] == '\n') {
                continue;  // "\r\n" becomes "\n"
            }
            stripped += bytes[i];
        }
        bytes.swap(stripped);
    }
    text.missingNewline = !bytes.empty() && bytes.back() != '\n';
    if (text.missingNewline && !robustOutput) {
        bytes += '\n';  // ed scripts need a complete last line
    }
    text.lineStarts.push_back(0);
    for (size_t i = 0; i < bytes.size(); ++i) {
        if (bytes[i] == '\n') {
            text.lineStarts.push_back(i + 1);
        }
    }
    if (text.lineStarts.back() != bytes.size()) {
        text.lineStarts.push_back(bytes.size());  // an incomplete last line
    }
    text.bytes = std::move(bytes);
    return text;
}

namespace {

// The key number of every line of one text, through |numberFor|. An
// incomplete last line keeps its marker only when the output is robust
// (not ed, where the newline is appended first) and the mode keeps it.
std::vector<int64_t> KeyNumbersOf(const DiffText& text, const DiffLineOptions& options,
                                  std::unordered_map<std::string, int64_t>& numberFor, bool markIncomplete) {
    std::vector<int64_t> keys(text.LineCount());
    const bool markerDropped = options.whiteSpace == DiffWhiteSpace::TrailingSpace
        || options.whiteSpace == DiffWhiteSpace::SpaceChange
        || options.whiteSpace == DiffWhiteSpace::AllSpace;
    for (size_t i = 0; i < text.LineCount(); ++i) {
        size_t start = text.lineStarts[i];
        size_t end = text.lineStarts[i + 1];
        if (end > start && text.bytes[end - 1] == '\n') {
            --end;  // the line without its '\n'
        }
        const bool incomplete = text.missingNewline && i + 1 == text.LineCount();
        std::string key = MakeLineKey(std::string_view(text.bytes).substr(start, end - start), options,
                                       incomplete && markIncomplete && !markerDropped);
        auto found = numberFor.find(key);
        if (found != numberFor.end()) {
            keys[i] = found->second;
        } else {
            keys[i] = static_cast<int64_t>(numberFor.size());
            numberFor.emplace(std::move(key), keys[i]);
        }
    }
    return keys;
}

// A greedy reach table: per diagonal k (x - y in the forward search's box
// coordinates, u - v in the backward one), the furthest reach found at the
// step being built. kNoReach marks a diagonal with no value at that step.
constexpr int64_t kNoReach = -2;

struct ReachTable {
    std::vector<int64_t> values;
    int64_t offset = 0;  // diagonal k lives at values[k + offset]; offset = M

    void Reset(const Box& box) {
        offset = box.y1 - box.y0;
        values.assign(static_cast<size_t>(box.x1 - box.x0 + box.y1 - box.y0 + 1), kNoReach);
    }
    int64_t& At(int64_t k) { return values[static_cast<size_t>(k + offset)]; }
    int64_t Get(int64_t k) const { return values[static_cast<size_t>(k + offset)]; }
};

// The middle snake of one box (Myers' section 4b): the greedy search runs
// forward from the top-left corner and backward from the bottom-right one, a
// step D each, and the first overlap ends it. |box| must be shrunk (equal
// corners matched) with both sides nonempty. The split point comes back in
// whole-file line indexes: the forward snake's end when Delta is odd, the
// backward snake's end when even. Returns false, |stopped| set, when |stop|
// asks to end early.
bool FindMiddleSnake(const std::vector<int64_t>& a, const std::vector<int64_t>& b, const Box& box,
                     const std::function<bool()>& stop, bool& stopped, int64_t& splitX, int64_t& splitY) {
    const int64_t n = box.x1 - box.x0;
    const int64_t m = box.y1 - box.y0;
    const int64_t delta = n - m;
    const auto equalF = [&](int64_t x, int64_t y) {
        return a[static_cast<size_t>(box.x0 + x)] == b[static_cast<size_t>(box.y0 + y)];
    };
    const auto equalB = [&](int64_t u, int64_t v) {
        return a[static_cast<size_t>(box.x1 - 1 - u)] == b[static_cast<size_t>(box.y1 - 1 - v)];
    };

    // One table per direction, kept across the steps: step D writes only the
    // diagonals of D's parity, so the other parity still holds step D-1's
    // values, exactly what the step reads -- no per-step copy is needed.
    ReachTable vf, vb;
    vf.Reset(box);
    vb.Reset(box);
    // D = 0: k = 0 starts at the corner and follows its snake. The box was
    // shrunk, so its first and last lines differ; the snakes are empty.
    int64_t x = 0, y = 0;
    while (x < n && y < m && equalF(x, y)) {
        ++x;
        ++y;
    }
    vf.At(0) = x;
    int64_t u = 0, v = 0;
    while (u < n && v < m && equalB(u, v)) {
        ++u;
        ++v;
    }
    vb.At(0) = u;

    for (int64_t d = 1;; ++d) {
        if (stop && stop()) {
            stopped = true;
            return false;
        }
        // The forward step visits its diagonals from D down to -D (the most
        // deletions first); each visited diagonal starts the step empty, so
        // one with no usable way in holds no value this step.
        for (int64_t k = d; k >= -d; k -= 2) {
            if (k < -m || k > n) {
                continue;
            }
            vf.At(k) = kNoReach;
            // Down from k+1 (an insertion) and right from k-1 (a deletion):
            // usable when that diagonal has a step D-1 value and the move
            // stays inside the box.
            // A diagonal outside [-M, N] never has a value (and is not in the table).
            const bool downPossible = k + 1 >= -d + 1 && k + 1 <= d - 1 && k + 1 <= n;
            const int64_t downReach = downPossible ? vf.Get(k + 1) : kNoReach;
            const bool haveDown = downReach != kNoReach && downReach - k <= m;
            const bool rightPossible = k - 1 >= -(d - 1) && k - 1 <= d - 1 && k - 1 >= -m;
            const int64_t rightReach = rightPossible ? vf.Get(k - 1) + 1 : kNoReach;
            const bool haveRight = rightReach != kNoReach && rightReach <= n;
            if (!haveDown && !haveRight) {
                continue;
            }
            if (haveDown && haveRight) {
                // Down at the bottom edge of the step, else the further of
                // the two reaches, down winning a tie (the paper's rule).
                x = (k == -d || rightReach <= downReach) ? downReach : rightReach;
            } else {
                x = haveDown ? downReach : rightReach;
            }
            y = x - k;
            while (x < n && y < m && equalF(x, y)) {
                ++x;
                ++y;
            }
            vf.At(k) = x;
            // Odd Delta: the overlap is checked against the backward step
            // D-1 table, split at this forward snake's end.
            if (delta % 2 != 0) {
                const int64_t kb = delta - k;
                if (kb >= -m && kb <= n && kb >= -(d - 1) && kb <= d - 1) {
                    const int64_t reach = vb.Get(kb);
                    if (reach != kNoReach && vf.At(k) + reach >= n) {
                        splitX = box.x0 + x;
                        splitY = box.y0 + y;
                        return true;
                    }
                }
            }
        }
        // The backward step, the same search on both sequences read from
        // their ends: diagonals from -D up to D.
        for (int64_t k = -d; k <= d; k += 2) {
            if (k < -m || k > n) {
                continue;
            }
            vb.At(k) = kNoReach;
            const bool downPossible = k + 1 >= -d + 1 && k + 1 <= d - 1 && k + 1 <= n;
            const int64_t downReach = downPossible ? vb.Get(k + 1) : kNoReach;
            const bool haveDown = downReach != kNoReach && downReach - k <= m;
            const bool rightPossible = k - 1 >= -(d - 1) && k - 1 <= d - 1 && k - 1 >= -m;
            const int64_t rightReach = rightPossible ? vb.Get(k - 1) + 1 : kNoReach;
            const bool haveRight = rightReach != kNoReach && rightReach <= n;
            if (!haveDown && !haveRight) {
                continue;
            }
            if (haveDown && haveRight) {
                u = (k == -d || rightReach <= downReach) ? downReach : rightReach;
            } else {
                u = haveDown ? downReach : rightReach;
            }
            v = u - k;
            while (u < n && v < m && equalB(u, v)) {
                ++u;
                ++v;
            }
            vb.At(k) = u;
            // Even Delta: checked against the forward step D table, split at
            // this backward snake's end.
            if (delta % 2 == 0) {
                const int64_t kf = delta - k;
                if (kf >= -m && kf <= n && kf >= -d && kf <= d) {
                    const int64_t reach = vf.Get(kf);
                    if (reach != kNoReach && reach + vb.At(k) >= n) {
                        splitX = box.x1 - u;
                        splitY = box.y1 - v;
                        return true;
                    }
                }
            }
        }
    }
}

} // namespace

namespace {

// Whether the block ending before line |e| lines up with a change of the
// other file: line e pairs with the other file's unchanged line |j| that has
// |u| unchanged lines before it in the region, and the block lines up when
// the line before j is changed -- the two changes would print as one hunk.
// With no such line left in the region, j is the region's end (the first
// fixed line after it, or the end of the file).
bool LinesUp(const std::vector<bool>& other, int64_t u, int64_t regionStart, int64_t otherRegionEnd) {
    int64_t j = regionStart;
    int64_t seen = 0;
    while (j < otherRegionEnd) {
        if (!other[static_cast<size_t>(j)]) {
            if (seen == u) {
                break;
            }
            ++seen;
        }
        ++j;
    }
    return j > regionStart && other[static_cast<size_t>(j - 1)];
}

// Unchanged lines of |flags| in [regionStart, e).
int64_t UnchangedBefore(const std::vector<bool>& flags, int64_t regionStart, int64_t e) {
    int64_t count = 0;
    for (int64_t i = regionStart; i < e; ++i) {
        if (!flags[static_cast<size_t>(i)]) {
            ++count;
        }
    }
    return count;
}

// Places one changed run [s, e) of |flags| by the sliding rules: as far down
// as it can slide (a run of repeated lines reads the same wherever in its
// range it sits), except at the lowest position where it lines up with a
// change of |other|. Sliding may absorb the runs it touches; they then slide
// as one. |keys| are the line keys of |flags|'s own file.
std::pair<int64_t, int64_t> PlaceBlock(std::vector<bool>& flags, const std::vector<bool>& other,
                                       const std::vector<int64_t>& keys, int64_t regionStart, int64_t regionEnd,
                                       int64_t otherRegionEnd, int64_t s, int64_t e) {
    const auto mark = [&](int64_t from, int64_t to, bool value) {
        for (int64_t i = from; i < to; ++i) {
            flags[static_cast<size_t>(i)] = value;
        }
    };
    // Unchanged lines of |flags| in [regionStart, e), kept as a running value
    // through the slides and merges below: a slide down turns the block's top
    // line unchanged (+1), a slide up over an unchanged line takes one back,
    // and a merge only takes in lines already changed.
    int64_t u = UnchangedBefore(flags, regionStart, e);
    bool merged = true;
    std::vector<std::pair<int64_t, int64_t>> noted;  // positions lining up, top to bottom
    while (merged) {
        merged = false;
        noted.clear();
        // (a) slide up while the line above equals the last line, absorbing
        // any run reached.
        while (s > regionStart && keys[static_cast<size_t>(s - 1)] == keys[static_cast<size_t>(e - 1)]) {
            const bool aboveChanged = flags[static_cast<size_t>(s - 1)];
            flags[static_cast<size_t>(s - 1)] = true;
            flags[static_cast<size_t>(e - 1)] = false;
            --s;
            --e;
            if (!aboveChanged) {
                --u;
            }
            if (s > regionStart && flags[static_cast<size_t>(s - 1)]) {
                int64_t p = s - 1;
                while (p > regionStart && flags[static_cast<size_t>(p - 1)]) {
                    --p;
                }
                s = p;
                merged = true;
            }
        }
        // (b) slide down as far as the line below equals the first line,
        // noting every position where the block lines up (the top included).
        if (LinesUp(other, u, regionStart, otherRegionEnd)) {
            noted.emplace_back(s, e);
        }
        while (e < regionEnd && keys[static_cast<size_t>(e)] == keys[static_cast<size_t>(s)]) {
            flags[static_cast<size_t>(s)] = false;
            flags[static_cast<size_t>(e)] = true;
            ++s;
            ++e;
            ++u;  // the block's top line, slid over, is unchanged now
            if (e < regionEnd && flags[static_cast<size_t>(e)]) {
                int64_t q = e;
                while (q < regionEnd && flags[static_cast<size_t>(q)]) {
                    ++q;
                }
                e = q;
                merged = true;
            }
            if (LinesUp(other, u, regionStart, otherRegionEnd)) {
                noted.emplace_back(s, e);
            }
        }
        // (c) a merge changes the run: the whole placement runs again.
    }
    // (d) the run sits at the lowest position noted, when one was.
    if (!noted.empty()) {
        const auto final = noted.back();
        if (final.first != s || final.second != e) {
            mark(s, e, false);
            mark(final.first, final.second, true);
        }
        return final;
    }
    return {s, e};
}

// Places every changed run of |flags| in the region, top to bottom, by the
// sliding rules (PlaceBlock).
void PlaceBlocks(std::vector<bool>& flags, const std::vector<bool>& other,
                 const std::vector<int64_t>& keys, int64_t regionStart, int64_t regionEnd,
                 int64_t otherRegionEnd) {
    int64_t cursor = regionStart;
    while (cursor < regionEnd) {
        if (!flags[static_cast<size_t>(cursor)]) {
            ++cursor;
            continue;
        }
        int64_t e = cursor;
        while (e < regionEnd && flags[static_cast<size_t>(e)]) {
            ++e;
        }
        const auto final = PlaceBlock(flags, other, keys, regionStart, regionEnd, otherRegionEnd, cursor, e);
        cursor = final.second;
    }
}

} // namespace

std::vector<DiffChange> ComputeDiff(const DiffText& a, const DiffText& b,
                                    const DiffAnalysisOptions& options,
                                    const std::function<bool()>& stop, bool& stopped) {
    stopped = false;
    const size_t n0 = a.LineCount();
    const size_t n1 = b.LineCount();
    std::vector<DiffChange> changes;

    // The key number of every line, one numbering for both files: lines are
    // equal when their keys are.
    std::unordered_map<std::string, int64_t> numberFor;
    const std::vector<int64_t> keys0 = KeyNumbersOf(a, options.lines, numberFor, options.robustOutput);
    const std::vector<int64_t> keys1 = KeyNumbersOf(b, options.lines, numberFor, options.robustOutput);

    // The fixed ends: lines byte-identical in both files, except the
    // horizonLines nearest the differences, are unchanged whatever the
    // comparison says, and no change is moved into them.
    const auto lineBytes = [](const DiffText& text, size_t line) {
        return std::string_view(text.bytes).substr(text.lineStarts[line],
                                                   text.lineStarts[line + 1] - text.lineStarts[line]);
    };
    const size_t common = n0 < n1 ? n0 : n1;
    size_t p = 0;
    while (p < common && lineBytes(a, p) == lineBytes(b, p)) {
        ++p;
    }
    const int64_t horizon = options.horizonLines > 0 ? options.horizonLines : 0;
    const size_t pre = p > static_cast<size_t>(horizon) ? p - static_cast<size_t>(horizon) : 0;
    size_t s = 0;
    while (s < common - pre && lineBytes(a, n0 - 1 - s) == lineBytes(b, n1 - 1 - s)) {
        ++s;
    }
    const size_t suf = s > static_cast<size_t>(horizon) ? s - static_cast<size_t>(horizon) : 0;

    std::vector<bool> flags0(n0, false), flags1(n1, false);
    const int64_t end0 = static_cast<int64_t>(n0 - suf);
    const int64_t end1 = static_cast<int64_t>(n1 - suf);

    // The comparison: Myers' linear-space divide and conquer over the region.
    if (pre < end0 || pre < end1) {
        std::vector<Box> stack;
        stack.push_back(Box{static_cast<int64_t>(pre), static_cast<int64_t>(pre), end0, end1});
        while (!stack.empty()) {
            const Box box = stack.back();
            stack.pop_back();
            Box inner = box;
            while (inner.x0 < inner.x1 && inner.y0 < inner.y1
                   && keys0[static_cast<size_t>(inner.x0)] == keys1[static_cast<size_t>(inner.y0)]) {
                ++inner.x0;
                ++inner.y0;
            }
            while (inner.x0 < inner.x1 && inner.y0 < inner.y1
                   && keys0[static_cast<size_t>(inner.x1 - 1)] == keys1[static_cast<size_t>(inner.y1 - 1)]) {
                --inner.x1;
                --inner.y1;
            }
            if (inner.x0 == inner.x1) {
                for (int64_t y = inner.y0; y < inner.y1; ++y) {
                    flags1[static_cast<size_t>(y)] = true;
                }
                continue;
            }
            if (inner.y0 == inner.y1) {
                for (int64_t x = inner.x0; x < inner.x1; ++x) {
                    flags0[static_cast<size_t>(x)] = true;
                }
                continue;
            }
            if (stop && stop()) {
                stopped = true;
                return {};
            }
            int64_t splitX = 0, splitY = 0;
            if (!FindMiddleSnake(keys0, keys1, inner, stop, stopped, splitX, splitY)) {
                return {};
            }
            stack.push_back(Box{splitX, splitY, box.x1, box.y1});
            stack.push_back(Box{box.x0, box.y0, splitX, splitY});
        }
        if (stopped) {
            return {};
        }

        // The placement: changed runs slide to where GNU's `diff -d` puts
        // them, file 0's first (against file 1's raw flags), then file 1's.
        PlaceBlocks(flags0, flags1, keys0, static_cast<int64_t>(pre), end0, end1);
        PlaceBlocks(flags1, flags0, keys1, static_cast<int64_t>(pre), end1, end0);
    }

    // The change list: both files' flags walked together, whole files.
    int64_t i0 = 0, i1 = 0;
    while (i0 < static_cast<int64_t>(n0) || i1 < static_cast<int64_t>(n1)) {
        const bool changed0 = i0 < static_cast<int64_t>(n0) && flags0[static_cast<size_t>(i0)];
        const bool changed1 = i1 < static_cast<int64_t>(n1) && flags1[static_cast<size_t>(i1)];
        if (!changed0 && !changed1) {
            ++i0;
            ++i1;
            continue;
        }
        int64_t deleted = 0;
        while (i0 + deleted < static_cast<int64_t>(n0) && flags0[static_cast<size_t>(i0 + deleted)]) {
            ++deleted;
        }
        int64_t inserted = 0;
        while (i1 + inserted < static_cast<int64_t>(n1) && flags1[static_cast<size_t>(i1 + inserted)]) {
            ++inserted;
        }
        changes.push_back(DiffChange{i0, i1, deleted, inserted});
        i0 += deleted;
        i1 += inserted;
    }
    return changes;
}

} // namespace Haisos