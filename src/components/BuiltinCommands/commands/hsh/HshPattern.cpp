#include "commands/hsh/HshPattern.h"

#include <cctype>

namespace Haisos::Hsh {

namespace {

constexpr size_t kNpos = std::string_view::npos;

size_t BracketEnd(std::string_view pattern, size_t open) {
    size_t i = open + 1;
    if (i < pattern.size() && pattern[i] == '!') ++i;
    if (i < pattern.size() && pattern[i] == ']') ++i; // a ']' first is a member
    while (i < pattern.size()) {
        if (pattern[i] == '\\' && i + 1 < pattern.size()) { i += 2; continue; }
        if (pattern[i] == '[' && i + 1 < pattern.size() && pattern[i + 1] == ':') {
            size_t j = pattern.find(":]", i + 2);
            if (j != kNpos) { i = j + 2; continue; }
        }
        if (pattern[i] == ']') return i + 1; // one past the ']'
        ++i;
    }
    return kNpos;
}

// The [:class:] members, ASCII as the C locale; an unknown class matches nothing.
bool ClassMatches(std::string_view name, unsigned char c) {
    if (name == "alnum")  return std::isalnum(c) != 0;
    if (name == "alpha")  return std::isalpha(c) != 0;
    if (name == "blank")  return c == ' ' || c == '\t';
    if (name == "cntrl")  return std::iscntrl(c) != 0;
    if (name == "digit")  return std::isdigit(c) != 0;
    if (name == "graph")  return std::isgraph(c) != 0;
    if (name == "lower")  return std::islower(c) != 0;
    if (name == "print")  return std::isprint(c) != 0;
    if (name == "punct")  return std::ispunct(c) != 0;
    if (name == "space")  return std::isspace(c) != 0;
    if (name == "upper")  return std::isupper(c) != 0;
    if (name == "xdigit") return std::isxdigit(c) != 0;
    return false;
}

// Whether the bracket expression at |open| (|end| from BracketEnd) holds |ch|.
bool BracketMatches(std::string_view pattern, size_t open, size_t end, unsigned char ch) {
    size_t limit = end - 1; // the closing ']' itself
    size_t i = open + 1;
    bool negated = i < limit && pattern[i] == '!';
    if (negated) ++i;
    bool matched = false;
    while (i < limit) {
        if (pattern[i] == '[' && i + 1 < limit && pattern[i + 1] == ':') {
            size_t j = pattern.find(":]", i + 2);
            if (j != kNpos && j + 2 <= limit) {
                if (ClassMatches(pattern.substr(i + 2, j - (i + 2)), ch)) matched = true;
                i = j + 2;
                continue;
            }
        }
        unsigned char lo;
        if (pattern[i] == '\\' && i + 1 < limit) { lo = static_cast<unsigned char>(pattern[i + 1]); i += 2; }
        else lo = static_cast<unsigned char>(pattern[i++]);
        if (i < limit && pattern[i] == '-' && i + 1 < limit) {
            unsigned char hi;
            size_t after = i + 1;
            if (pattern[after] == '\\' && after + 1 < limit) { hi = static_cast<unsigned char>(pattern[after + 1]); after += 2; }
            else { hi = static_cast<unsigned char>(pattern[after]); ++after; }
            if (lo <= ch && ch <= hi) matched = true;
            i = after;
        } else if (lo == ch) {
            matched = true;
        }
    }
    return negated ? !matched : matched;
}

} // namespace

size_t PatternBracketEnd(std::string_view pattern, size_t pos) {
    return BracketEnd(pattern, pos);
}

bool MatchPattern(std::string_view pattern, std::string_view text) {
    // Two indices with backtracking to the last '*': every non-'*' element
    // consumes exactly one character, so the last star position is all the
    // state needed. No recursion.
    size_t p = 0, t = 0;
    size_t starAfter = kNpos, starText = 0;
    while (t < text.size()) {
        if (p < pattern.size()) {
            char c = pattern[p];
            if (c == '*') { starAfter = p + 1; starText = t; ++p; continue; }
            bool matches;
            size_t next;
            if (c == '?') {
                matches = true;
                next = p + 1;
            } else if (c == '\\' && p + 1 < pattern.size()) {
                matches = static_cast<unsigned char>(text[t]) == static_cast<unsigned char>(pattern[p + 1]);
                next = p + 2;
            } else if (c == '[' && BracketEnd(pattern, p) != kNpos) {
                size_t end = BracketEnd(pattern, p);
                matches = BracketMatches(pattern, p, end, static_cast<unsigned char>(text[t]));
                next = end;
            } else {
                matches = text[t] == c;
                next = p + 1;
            }
            if (matches) { p = next; ++t; continue; }
        }
        if (starAfter != kNpos) { p = starAfter; t = ++starText; continue; }
        return false;
    }
    while (p < pattern.size() && pattern[p] == '*') ++p;
    return p == pattern.size();
}

bool HasPatternCharacters(std::string_view pattern) {
    for (size_t i = 0; i < pattern.size();) {
        char c = pattern[i];
        if (c == '\\' && i + 1 < pattern.size()) { i += 2; continue; }
        if (c == '*' || c == '?') return true;
        if (c == '[' && BracketEnd(pattern, i) != kNpos) return true;
        ++i;
    }
    return false;
}

std::string UnescapePattern(std::string_view pattern) {
    std::string out;
    out.reserve(pattern.size());
    for (size_t i = 0; i < pattern.size();) {
        if (pattern[i] == '\\' && i + 1 < pattern.size()) { out += pattern[i + 1]; i += 2; }
        else out += pattern[i++];
    }
    return out;
}

std::string EscapeForPattern(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (char c : text) {
        if (c == '\\' || c == '*' || c == '?' || c == '[') out += '\\';
        out += c;
    }
    return out;
}

std::string RemovePattern(std::string_view value, std::string_view pattern, PatternRemoval which) {
    bool prefix = which == PatternRemoval::SmallestPrefix || which == PatternRemoval::LargestPrefix;
    bool smallest = which == PatternRemoval::SmallestPrefix || which == PatternRemoval::SmallestSuffix;
    size_t n = value.size();
    size_t cut = n + 1; // a length; n + 1: none matched
    if (smallest) {
        for (size_t len = 0; len <= n; ++len) {
            if (MatchPattern(pattern, prefix ? value.substr(0, len) : value.substr(n - len))) { cut = len; break; }
        }
    } else {
        for (size_t len = n;; --len) {
            if (MatchPattern(pattern, prefix ? value.substr(0, len) : value.substr(n - len))) { cut = len; break; }
            if (len == 0) break;
        }
    }
    if (cut > n) return std::string(value);
    return std::string(prefix ? value.substr(cut) : value.substr(0, n - cut));
}

} // namespace Haisos::Hsh
