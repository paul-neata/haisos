#pragma once
#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include "src/components/Regex/Regex.h"

namespace Haisos {

// The four matchers of grep: -G -E -F -P (fgrep fixes Fixed, egrep Extended).
enum class GrepSyntax { Basic, Extended, Fixed, Perl };

// How -w decides a match is a whole word.
enum class GrepWholeWord {
    No,
    NonWordNeighbours,  // GNU grep -G/-E/-F/-P (and rg): the bytes before and after the match are not word bytes
};

struct GrepMatcherOptions {
    GrepSyntax syntax = GrepSyntax::Basic;
    bool ignoreCase = false;                       // ASCII
    GrepWholeWord wholeWord = GrepWholeWord::No;   // -w
    bool wholeLine = false;                        // -x
};

// Patterns made into a "find the next match in a line" matcher, as GNU grep's
// execute() -- everything but reading and printing. rg (search--rg-search)
// reuses this class; Grep.cpp drives it through GrepFile.
class GrepMatcher {
public:
    // |patterns| one pattern each (the caller has split on '\n'); may be
    // empty (then nothing ever matches). Perl: at most one (the caller
    // reports GNU's message for more). Null with |error| set to the message
    // grep prints after "grep: ".
    static std::shared_ptr<const GrepMatcher> Create(const std::vector<std::string>& patterns,
                                                     const GrepMatcherOptions& options, std::string& error);

    // The first match in |line| (one line, without its terminator) starting
    // at or after |start| that -w/-x accept: [begin, end). Basic/Extended/
    // Fixed: leftmost, then longest across all patterns; Perl: the regex's
    // leftmost-first match.
    bool Find(std::string_view line, size_t start, size_t& begin, size_t& end) const;
    // Whether any match exists (Find from 0).
    bool Matches(std::string_view line) const;

private:
    GrepMatcher() = default;

    // The first raw match (no -w/-x), leftmost-then-longest (leftmost-first
    // for Perl), across all patterns; |regexFlags| are Regex::Search's.
    bool RawFind(std::string_view line, size_t start, int regexFlags, size_t& begin, size_t& end) const;
    // The longest fixed pattern occurring exactly at |pos| (an empty pattern
    // occurs everywhere): |length| its size. Whether any occurs.
    bool FixedAt(std::string_view line, size_t pos, size_t& length) const;
    // GNU grep's EGexecute -w loop: a match accepted only when the bytes
    // before and after it are not word bytes, trying shorter matches
    // anchored at the same start, then looking further on.
    bool FindWholeWord(std::string_view line, size_t start, size_t& begin, size_t& end) const;
    // Whether a match spans the whole line (GNU's -x for -G/-E/-F: the
    // pattern anchored to the line).
    bool WholeLineMatches(std::string_view line) const;

    GrepMatcherOptions m_options;
    bool m_fixed = false;
    std::vector<std::string> m_fixedPatterns;             // Fixed: the byte strings
    std::vector<std::shared_ptr<const Regex>> m_regexes;  // Basic/Extended/Perl
};

} // namespace Haisos