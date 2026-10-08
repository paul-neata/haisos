#include "commands/grep/GrepMatcher.h"
#include <algorithm>

namespace Haisos {
namespace {

// A word constituent byte, as in the C locale: [A-Za-z0-9_].
bool IsWordByte(char c) {
    const unsigned char u = static_cast<unsigned char>(c);
    return (u >= 'a' && u <= 'z') || (u >= 'A' && u <= 'Z') || (u >= '0' && u <= '9') || u == '_';
}

// ASCII case folding.
char FoldByte(char c) {
    const unsigned char u = static_cast<unsigned char>(c);
    return static_cast<char>(u >= 'A' && u <= 'Z' ? u - 'A' + 'a' : u);
}

bool BytesEqual(std::string_view a, std::string_view b, bool ignoreCase) {
    if (a.size() != b.size()) {
        return false;
    }
    if (!ignoreCase) {
        return a == b;
    }
    for (size_t i = 0; i < a.size(); ++i) {
        if (FoldByte(a[i]) != FoldByte(b[i])) {
            return false;
        }
    }
    return true;
}

// Whether a Basic/Extended pattern holds a back-reference, \1-\9.
bool HasBackReference(const std::string& pattern) {
    for (size_t i = 0; i + 1 < pattern.size(); ++i) {
        if (pattern[i] != '\\') {
            continue;
        }
        if (pattern[i + 1] >= '1' && pattern[i + 1] <= '9') {
            return true;
        }
        ++i; // an escaped byte: not a back-reference
    }
    return false;
}

// GNU grep's own refusal, its dfa hard-coded: a bracket expression whose
// content starts and ends with ':' ([:space:], x[:a:]) is not a bracket
// around a character class but a mistake, and it says so before glibc could.
bool GnuClassSyntaxMistake(const std::string& pattern) {
    const size_t size = pattern.size();
    size_t i = 0;
    while (i < size) {
        const char c = pattern[i];
        if (c == '\\') {
            i += 2; // an escaped byte: never opens a bracket
            continue;
        }
        if (c != '[') {
            ++i;
            continue;
        }
        // A bracket opens. Its content runs to the first ']' that is not
        // right after the '[' (or after '[^'), skipping [:...:], [. .] and
        // [= =] items inside it.
        size_t k = i + 1;
        if (k < size && pattern[k] == '^') {
            ++k;
        }
        const size_t contentStart = k;
        if (k < size && pattern[k] == ']') {
            ++k; // a ']' right after '[' or '[^' is a literal member
        }
        while (k < size) {
            const char d = pattern[k];
            if (d == '[' && k + 1 < size
                && (pattern[k + 1] == ':' || pattern[k + 1] == '.' || pattern[k + 1] == '=')) {
                const char marker = pattern[k + 1];
                size_t m = k + 2;
                while (m < size && pattern[m] != marker) {
                    ++m;
                }
                if (m == size) {
                    break; // unterminated: no closing to find
                }
                if (m + 1 < size && pattern[m + 1] == ']') {
                    k = m + 2; // a whole [:...:] style item
                    continue;
                }
                k = m + 1; // not a well-formed item after all
                continue;
            }
            if (d == ']') {
                break;
            }
            ++k;
        }
        if (k == size) {
            break; // unterminated: glibc's message, not this one
        }
        const size_t contentLength = k - contentStart;
        if (contentLength >= 2 && pattern[contentStart] == ':' && pattern[k - 1] == ':') {
            return true;
        }
        i = k + 1;
    }
    return false;
}

} // namespace

std::shared_ptr<const GrepMatcher> GrepMatcher::Create(
    const std::vector<std::string>& patterns, const GrepMatcherOptions& options, std::string& error) {
    error.clear();
    auto matcher = std::shared_ptr<GrepMatcher>(new GrepMatcher());
    matcher->m_options = options;

    if (options.syntax == GrepSyntax::Fixed) {
        matcher->m_fixed = true;
        matcher->m_fixedPatterns = patterns;
        return matcher;
    }

    const bool perl = options.syntax == GrepSyntax::Perl;
    RegexOptions regexOptions;
    regexOptions.syntax = perl ? RegexSyntax::Perl
        : (options.syntax == GrepSyntax::Extended ? RegexSyntax::Extended : RegexSyntax::Basic);
    regexOptions.ignoreCase = options.ignoreCase;

    // GNU's own check first, before compiling.
    if (!perl) {
        for (const auto& pattern : patterns) {
            if (GnuClassSyntaxMistake(pattern)) {
                error = "character class syntax is [[:space:]], not [:space:]";
                return nullptr;
            }
        }
    }

    // Perl: one regex per pattern, wrapped for -w/-x (GNU's
    // PCRE2_EXTRA_MATCH_WORD/LINE).
    if (perl) {
        for (const auto& pattern : patterns) {
            std::string wrapped = pattern;
            if (options.wholeWord == GrepWholeWord::PerlBoundaries) {
                wrapped = "\\b(?:" + wrapped + ")\\b";
            }
            if (options.wholeLine) {
                wrapped = "\\A(?:" + wrapped + ")\\z";
            }
            std::string compileError;
            auto regex = Regex::Compile(wrapped, regexOptions, compileError);
            if (!regex) {
                error = compileError;
                return nullptr;
            }
            matcher->m_regexes.push_back(std::move(regex));
        }
        return matcher;
    }

    // Basic/Extended: GNU compiles every pattern into one matcher. Without
    // back-references that is one regex -- the alternation of the patterns,
    // which keeps leftmost-longest across them; with them, or when the join
    // will not compile (a program past the size limit, say), each pattern
    // is its own regex.
    const bool join = patterns.size() > 1
        && std::none_of(patterns.begin(), patterns.end(), HasBackReference);
    if (join) {
        const bool basic = regexOptions.syntax == RegexSyntax::Basic;
        std::string joined;
        for (size_t i = 0; i < patterns.size(); ++i) {
            if (i > 0) {
                joined += basic ? "\\|" : "|";
            }
            joined += basic ? "\\(" + patterns[i] + "\\)" : "(" + patterns[i] + ")";
        }
        std::string compileError;
        auto regex = Regex::Compile(joined, regexOptions, compileError);
        if (regex) {
            matcher->m_regexes.push_back(std::move(regex));
            return matcher;
        }
        // The join may be what failed; compile each alone so the error is
        // the one of the pattern at fault.
        for (const auto& pattern : patterns) {
            std::string aloneError;
            auto alone = Regex::Compile(pattern, regexOptions, aloneError);
            if (!alone) {
                error = aloneError;
                return nullptr;
            }
        }
    }
    for (const auto& pattern : patterns) {
        std::string compileError;
        auto regex = Regex::Compile(pattern, regexOptions, compileError);
        if (!regex) {
            error = compileError;
            return nullptr;
        }
        matcher->m_regexes.push_back(std::move(regex));
    }
    return matcher;
}

bool GrepMatcher::FixedAt(std::string_view line, size_t pos, size_t& length) const {
    bool any = false;
    length = 0;
    for (const auto& pattern : m_fixedPatterns) {
        if (pos + pattern.size() > line.size()) {
            continue;
        }
        if (BytesEqual(line.substr(pos, pattern.size()), pattern, m_options.ignoreCase)
            && (!any || pattern.size() > length)) {
            length = pattern.size();
            any = true;
        }
    }
    return any;
}

bool GrepMatcher::RawFind(std::string_view line, size_t start, int regexFlags,
                          size_t& begin, size_t& end) const {
    if (start > line.size()) {
        return false;
    }
    if (m_fixed) {
        // Leftmost start, then the longest pattern there (an empty pattern
        // occurs at every position, the empty string included).
        for (size_t pos = start; ; ++pos) {
            size_t length = 0;
            if (FixedAt(line, pos, length)) {
                begin = pos;
                end = pos + length;
                return true;
            }
            if (pos == line.size()) {
                return false;
            }
        }
    }
    // Several regexes (back-references, or a join that will not compile):
    // run each, keep the smallest begin, then the largest end.
    bool found = false;
    size_t bestBegin = 0;
    size_t bestEnd = 0;
    for (const auto& regex : m_regexes) {
        RegexMatch match;
        if (!regex->Search(line, start, match, regexFlags)) {
            continue;
        }
        const auto& whole = match.groups[0];
        const size_t b = static_cast<size_t>(whole.first);
        const size_t e = static_cast<size_t>(whole.second);
        if (!found || b < bestBegin || (b == bestBegin && e > bestEnd)) {
            bestBegin = b;
            bestEnd = e;
            found = true;
        }
    }
    if (!found) {
        return false;
    }
    begin = bestBegin;
    end = bestEnd;
    return true;
}

bool GrepMatcher::WholeLineMatches(std::string_view line) const {
    if (m_fixed) {
        for (const auto& pattern : m_fixedPatterns) {
            if (BytesEqual(pattern, line, m_options.ignoreCase)) {
                return true;
            }
        }
        return false;
    }
    for (const auto& regex : m_regexes) {
        RegexMatch match;
        if (regex->Search(line, 0, match)
            && match.groups[0].first == 0
            && match.groups[0].second == static_cast<std::ptrdiff_t>(line.size())) {
            return true;
        }
    }
    return false;
}

bool GrepMatcher::FindWholeWord(std::string_view line, size_t start, size_t& begin, size_t& end) const {
    // GNU's EGexecute loop, emulated exactly: find the leftmost match; when
    // its neighbours are word bytes, try shorter matches anchored at the
    // same start (each the longest that still fits), then look further on.
    const size_t size = line.size();
    size_t s = start;
    while (true) {
        size_t b = 0;
        size_t e = 0;
        if (!RawFind(line, s, /*regexFlags=*/0, b, e)) {
            return false;
        }
        size_t len = e - b;
        while (true) {
            const bool leftOk = b == 0 || !IsWordByte(line[b - 1]);
            const bool rightOk = e == size || !IsWordByte(line[e]);
            if (leftOk && rightOk) {
                begin = b;
                end = e;
                return true;
            }
            if (len > 0) {
                --len;
                // The longest match starting exactly at b within line[0, b+len)
                // (leftmost-longest: Search on the truncated text gives it).
                size_t b2 = 0;
                size_t e2 = 0;
                if (RawFind(line.substr(0, b + len), b, kRegexNotEol, b2, e2)
                    && b2 == b && e2 > b) {
                    e = e2;
                    len = e - b;
                    continue;
                }
            }
            s = b + 1;
            if (s > size) {
                return false;
            }
            break;
        }
    }
}

bool GrepMatcher::Find(std::string_view line, size_t start, size_t& begin, size_t& end) const {
    if (start > line.size()) {
        return false;
    }
    // -x (a match must span the whole line) is the matcher's own wrap for
    // Perl; for the others, GNU anchors the pattern to the line, which for
    // leftmost-longest is: the match at 0 is the whole line. With -w too, a
    // whole-line match has no neighbours, so it is a whole word.
    if (m_options.syntax != GrepSyntax::Perl && m_options.wholeLine) {
        if (start != 0 || !WholeLineMatches(line)) {
            return false;
        }
        begin = 0;
        end = line.size();
        return true;
    }
    if (m_options.wholeWord == GrepWholeWord::NonWordNeighbours) {
        return FindWholeWord(line, start, begin, end);
    }
    return RawFind(line, start, /*regexFlags=*/0, begin, end);
}

bool GrepMatcher::Matches(std::string_view line) const {
    size_t begin = 0;
    size_t end = 0;
    return Find(line, 0, begin, end);
}

} // namespace Haisos