#pragma once

#include <string>
#include <string_view>

namespace Haisos::Hsh {

// Whether |text| as a whole matches the shell pattern |pattern|. The one
// matcher of pathname expansion, `case` and ${x#pattern}/${x%pattern}:
// '*' any string, '?' one byte, '\c' c literally (a trailing lone '\' is a
// literal '\'), and bracket expressions '[...]' -- '!' right after '['
// negates ('^' does NOT: "[^a]" is the set of '^' and 'a'), a ']' right
// after '[' or '[!' is a member, 'a-z' is a byte range, '-' first or last is
// a member, '[:alpha:]' the ASCII classes. A '[' with no closing ']' is an
// ordinary character. Characters are bytes: no multibyte, no locale.
bool MatchPattern(std::string_view pattern, std::string_view text);

// Whether |pattern| has anything MatchPattern treats specially: an unescaped
// '*' or '?', or an unescaped '[' that opens a complete bracket expression.
bool HasPatternCharacters(std::string_view pattern);

// The text a pattern without special characters matches: its escapes removed
// ("a\*" -> "a*").
std::string UnescapePattern(std::string_view pattern);

// |text| with '\\', '*', '?' and '[' escaped, so that it matches only itself.
std::string EscapeForPattern(std::string_view text);

enum class PatternRemoval { SmallestPrefix, LargestPrefix, SmallestSuffix, LargestSuffix };

// ${name#pattern}, ${name##pattern}, ${name%pattern}, ${name%%pattern}:
// |value| without the shortest/longest prefix/suffix |pattern| matches, or
// |value| unchanged when none does. An empty pattern removes nothing.
std::string RemovePattern(std::string_view value, std::string_view pattern, PatternRemoval which);

// The position one past a bracket expression opening at |pos|
// (|pattern|[pos] == '['), or npos when it is not complete. Pathname
// splitting uses it to jump over brackets: a bracket never spans a '/'.
size_t PatternBracketEnd(std::string_view pattern, size_t pos);

} // namespace Haisos::Hsh
