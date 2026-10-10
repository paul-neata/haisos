#pragma once
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include "src/components/Regex/Regex.h"

namespace Haisos::Awk {

// awk's regex text (a literal's, or a dynamic regex's string value) as the
// ERE Regex::Compile takes: POSIX ERE, awk's own escapes translated.
// |warnings| gets one message per unknown escape, gawk's
// "regexp escape sequence `\\c' is not a known regexp operator".
std::string TranslateAwkRegex(std::string_view awkRegex, std::vector<std::string>& warnings);

// The regex as it was written, for error messages: every '/' outside a
// bracket expression shown '\/' (it can only have been written so); one
// inside is shown bare (a documented difference: gawk writes it '\/').
std::string AwkRegexAsWritten(std::string_view awkRegex);

// Compiled dynamic regexes by their awk text, so a regex held in a variable
// is compiled once. Bounded: at 256 entries it is cleared.
class AwkRegexCache {
public:
    // Null with |error| set (Regex's message) when it does not compile;
    // |warnings| filled only when the text is compiled (not on a hit).
    std::shared_ptr<const Regex> Get(const std::string& awkRegex,
                                     std::vector<std::string>& warnings, std::string& error);

private:
    std::unordered_map<std::string, std::shared_ptr<const Regex>> m_compiled;
};

// |text| cut at every match of |regex|: the fields are the text between
// matches; a match at the very start gives an empty first field, one at the
// end an empty last field; an empty match never separates; an empty text has
// no fields.
void SplitByRegex(std::string_view text, const Regex& regex, std::vector<std::string>& fields);

} // namespace Haisos::Awk