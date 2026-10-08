#pragma once
#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Haisos {

enum class RegexSyntax { Basic, Extended, Perl };

struct RegexOptions {
    RegexSyntax syntax = RegexSyntax::Basic;
    bool ignoreCase = false;  // ASCII letters only
    bool multiline = false;   // sed's M flag: ^ and $ also match after/before each '\n'
};

// Search flags, as regexec()'s REG_NOTBOL / REG_NOTEOL.
constexpr int kRegexNotBol = 1;  // ^ does not match at the start of the text (\` still does)
constexpr int kRegexNotEol = 2;  // $ does not match at the end of the text (\' still does)

struct RegexMatch {
    // groups[0] is the whole match, groups[n] capture group n, as byte
    // offsets [first, second) into the text searched; (-1, -1) for a group
    // that took no part. Size GroupCount() + 1 after a successful Search.
    std::vector<std::pair<std::ptrdiff_t, std::ptrdiff_t>> groups;
};

class Regex {
public:
    // Null on a bad pattern, |error| then holding the message GNU prints for
    // it (Basic/Extended: glibc's regerror text; Perl: PCRE2's), e.g.
    // "Unmatched [, [^, [:, [., or [=". Limits: nesting 250 parentheses
    // (PCRE2's default; deeper would risk a 1 MB Windows stack), and a
    // compiled program of at most a million instructions -- "Regular
    // expression too big" (glibc's text) for Basic/Extended,
    // "regular expression is too large" (PCRE2's) for Perl.
    static std::shared_ptr<const Regex> Compile(std::string_view pattern, const RegexOptions& options, std::string& error);

    // Finds the leftmost match starting at or after |start| (<= text.size()),
    // with every capture group; |match| is left untouched when nothing matches.
    // ^, \b, \< ... look at text[start - 1] when start > 0, as re_search does.
    //
    // Semantics, by syntax: Basic and Extended match leftmost-longest, as
    // glibc's regexec -- of all matches take those starting leftmost, of those
    // the longest, and on a tie the groups of the first way through the
    // pattern in priority order (alternatives left before right, a greedy
    // repetition preferring one more iteration), which is what glibc reports
    // and not always what strict POSIX subexpression rules would give. Perl
    // matches leftmost-first, as PCRE2: the first match in priority order
    // from the leftmost start. An empty match is a match (a* finds (0,0) on
    // "baaa"); a group that took no part is (-1, -1); a group inside a
    // repetition reports its last iteration. On success match.groups holds
    // GroupCount() + 1 pairs of byte offsets.
    //
    // const and thread-safe: no mutable state, so concurrent Search calls on
    // one Regex are fine.
    bool Search(std::string_view text, size_t start, RegexMatch& match, int flags = 0) const;

    // The number of capture groups, ( ... ) / \( ... \), not counting group 0.
    size_t GroupCount() const;
    // Size GroupCount() + 1; [n] is group n's name ((?<name>...) in Perl), "" if unnamed.
    const std::vector<std::string>& GroupNames() const;
    const RegexOptions& Options() const;

    ~Regex();

private:
    struct Compiled;
    explicit Regex(std::unique_ptr<Compiled> compiled);
    std::unique_ptr<Compiled> m_compiled;
};

}