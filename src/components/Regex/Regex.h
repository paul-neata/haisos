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
    // "Unmatched [, [^, [:, [., or [=".
    static std::shared_ptr<const Regex> Compile(std::string_view pattern, const RegexOptions& options, std::string& error);

    // Finds the leftmost match starting at or after |start| (<= text.size());
    // ^, \b, \< ... look at text[start - 1] when start > 0, as re_search does.
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