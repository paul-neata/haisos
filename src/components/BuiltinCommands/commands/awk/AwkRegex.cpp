#include "commands/awk/AwkRegex.h"

namespace Haisos::Awk {
namespace {

// The characters POSIX ERE treats specially outside a bracket expression.
// A '\' before one of them is kept: the escaped character, matching itself.
bool IsAwkRegexMetachar(char c) {
    return c == '[' || c == ']' || c == '.' || c == '(' || c == ')' || c == '*'
        || c == '+' || c == '?' || c == '{' || c == '}' || c == '|'
        || c == '^' || c == '$' || c == '\\';
}

bool IsOctalDigit(char c) {
    return c >= '0' && c <= '7';
}

// A '\' escape that means one control byte, as awk's string escapes do.
bool AwkControlEscape(char c, char& byte) {
    switch (c) {
        case 'a': byte = '\a'; return true;
        case 'b': byte = '\b'; return true;
        case 'f': byte = '\f'; return true;
        case 'n': byte = '\n'; return true;
        case 'r': byte = '\r'; return true;
        case 't': byte = '\t'; return true;
        case 'v': byte = '\v'; return true;
    }
    return false;
}

std::string UnknownOperatorWarning(char c) {
    std::string message = "regexp escape sequence `\\";
    message += c;
    message += "' is not a known regexp operator";
    return message;
}

// The '\' escape at |i| (text[i] == '\\'), outside a bracket expression:
// appended to |out|, |i| moved past it. awk's own escapes are decoded; a
// metacharacter stays an escaped pair; a '\' before anything else is that
// character with gawk's warning.
void AppendOutsideEscape(std::string_view text, size_t& i, std::string& out,
                         std::vector<std::string>& warnings) {
    if (i + 1 >= text.size()) {  // a final lone backslash kept: the engine reports it
        out += '\\';
        ++i;
        return;
    }
    char d = text[i + 1];
    if (d == '/') {
        out += '/';
        i += 2;
        return;
    }
    if (d == '"') {
        out += '"';
        warnings.push_back(UnknownOperatorWarning(d));
        i += 2;
        return;
    }
    char byte = 0;
    if (AwkControlEscape(d, byte)) {
        out += byte;
        i += 2;
        return;
    }
    if (IsOctalDigit(d)) {
        int value = 0;
        int digits = 0;
        size_t j = i + 1;
        while (j < text.size() && digits < 3 && IsOctalDigit(text[j])) {
            value = value * 8 + (text[j] - '0');
            ++j;
            ++digits;
        }
        // Written raw: a metacharacter the digits spell stays a metacharacter.
        out += static_cast<char>(static_cast<unsigned char>(value));
        i = j;
        return;
    }
    if (IsAwkRegexMetachar(d)) {
        out += '\\';
        out += d;
        i += 2;
        return;
    }
    out += d;
    warnings.push_back(UnknownOperatorWarning(d));
    i += 2;
}

// ---- bracket expressions ----
//
// Haisos's regex engine reads bracket members as raw bytes (no '\' escapes
// inside), so the members are decoded here and the bracket re-emitted
// needing no escapes: a ']' first (right after '[' or '[^'), a '-' last
// (a trailing '-' is a member), a '^' never first (it would read as the
// negation), and a ']' '-' '^' of a range as a collating element.

struct BracketMember {
    enum class Kind { Char, Class, Range, RawDash };
    Kind kind = Kind::Char;
    char byte = 0;        // Char: the member byte; Range: the start byte
    char hi = 0;          // Range: the end byte
    std::string text;     // Class: the whole item; Range: the end when it was a class
    bool decoded = false; // Char: written as an escape
    bool firstRaw = false;  // Char: a raw ']' or '-' in the first member's place
};

// The '\' escape at |i| inside a bracket: the byte it means, |i| past it.
char DecodeBracketEscape(std::string_view text, size_t& i, std::vector<std::string>& warnings) {
    if (i + 1 >= text.size()) {
        ++i;
        return '\\';
    }
    char d = text[i + 1];
    i += 2;
    if (d == '"') {
        warnings.push_back(UnknownOperatorWarning(d));
        return '"';
    }
    char byte = 0;
    if (AwkControlEscape(d, byte))
        return byte;
    if (IsOctalDigit(d)) {
        int value = d - '0';
        int digits = 1;
        while (i < text.size() && digits < 3 && IsOctalDigit(text[i])) {
            value = value * 8 + (text[i] - '0');
            ++i;
            ++digits;
        }
        return static_cast<char>(static_cast<unsigned char>(value));
    }
    if (d != '/' && d != '-' && !IsAwkRegexMetachar(d))
        warnings.push_back(UnknownOperatorWarning(d));
    return d;
}

// One bracket member at |p|: a '[:...:]', '[.x.]' or '[=x=]' item whole, or
// one byte with awk's bracket escapes decoded. False when a class does not
// close (the bracket then cannot either).
bool ReadBracketMember(std::string_view text, size_t& p, BracketMember& member,
                       std::vector<std::string>& warnings) {
    char c = text[p];
    if (c == '[' && p + 1 < text.size()) {
        char lead = text[p + 1];
        if (lead == ':' || lead == '.' || lead == '=') {
            size_t j = p + 2;
            while (j + 1 < text.size() && !(text[j] == lead && text[j + 1] == ']'))
                ++j;
            if (j + 1 >= text.size())
                return false;
            member.kind = BracketMember::Kind::Class;
            member.text.assign(text.substr(p, j + 2 - p));
            p = j + 2;
            return true;
        }
    }
    if (c == '\\') {
        member.byte = DecodeBracketEscape(text, p, warnings);
        member.decoded = true;
    } else {
        member.byte = c;
        ++p;
    }
    return true;
}

// A range's endpoint byte, collated when writing it raw would be read as
// part of the bracket's own syntax.
void AppendRangeByte(char byte, std::string& out) {
    if (byte == ']' || byte == '-' || byte == '^') {
        out += "[.";
        out += byte;
        out += ".]";
    } else {
        out += byte;
    }
}

void AppendMember(const BracketMember& member, std::string& out) {
    switch (member.kind) {
        case BracketMember::Kind::Class:
            out += member.text;
            break;
        case BracketMember::Kind::RawDash:
            out += '-';
            break;
        case BracketMember::Kind::Range:
            AppendRangeByte(member.byte, out);
            out += '-';
            if (member.text.empty())
                AppendRangeByte(member.hi, out);
            else  // a class as the end: kept whole, the engine refuses it
                out += member.text;
            break;
        case BracketMember::Kind::Char:
            out += member.byte;
            break;
    }
}

// The bracket expression at |i| (text[i] == '['), re-emitted into |out| with
// its escapes decoded; |i| ends past it -- or at the text's end when the
// bracket does not close, the text passed through for the engine's
// "Unmatched [" error.
void EmitBracket(std::string_view text, size_t& i, std::string& out,
                 std::vector<std::string>& warnings) {
    const size_t n = text.size();
    size_t p = i + 1;
    bool negated = false;
    if (p < n && text[p] == '^') {
        negated = true;
        ++p;
    }
    bool first = true;    // a ']' here is a member
    bool closed = false;
    std::vector<BracketMember> members;
    int prev = -1;       // the member a '-' range starts from
    bool havePrev = false;
    for (;;) {
        if (p >= n)
            break;
        char c = text[p];
        if (c == ']' && !first) {
            ++p;
            closed = true;
            break;
        }
        if (c == '-' && !first && p + 1 < n && text[p + 1] != ']') {
            ++p;  // the engine reads a range here
            BracketMember end;
            if (!ReadBracketMember(text, p, end, warnings))
                break;
            bool range = false;
            if (havePrev) {
                const BracketMember& start = members[prev];
                range = end.kind == BracketMember::Kind::Char
                    && static_cast<unsigned char>(end.byte) >= static_cast<unsigned char>(start.byte);
            }
            if (range) {
                members[prev].kind = BracketMember::Kind::Range;
                members[prev].hi = end.byte;
            } else {
                // The engine refuses this range ("Invalid range end"): the
                // '-' and the end kept where they are, so it refuses the
                // re-emitted text the same way.
                BracketMember dash;
                dash.kind = BracketMember::Kind::RawDash;
                dash.byte = '-';
                members.push_back(dash);
                members.push_back(end);
            }
            havePrev = false;
            first = false;
            continue;
        }
        BracketMember member;
        if (!ReadBracketMember(text, p, member, warnings))
            break;
        member.firstRaw = members.empty() && !member.decoded && (member.byte == ']' || member.byte == '-');
        members.push_back(member);
        if (member.kind == BracketMember::Kind::Char) {
            havePrev = true;
            prev = static_cast<int>(members.size()) - 1;
        } else {
            havePrev = false;
        }
        first = false;
    }
    if (!closed) {
        out.append(text.substr(i, n - i));
        i = n;
        return;
    }
    out += '[';
    if (negated)
        out += '^';
    // A ']' member first, where a raw ']' is one. One byte of it is enough:
    // the members are a set, and a second ']' there would be read as the
    // bracket's close.
    bool closeMember = false;
    for (const BracketMember& member : members) {
        if (member.kind == BracketMember::Kind::Char && member.byte == ']'
            && (member.decoded || member.firstRaw))
            closeMember = true;
    }
    if (closeMember)
        out += ']';
    // Then the rest, a decoded '^' only once something precedes it, a decoded
    // '-' last.
    std::string hats;
    std::string dashes;
    for (const BracketMember& member : members) {
        if (member.kind == BracketMember::Kind::Char && member.decoded && member.byte == '^') {
            hats += '^';
            continue;
        }
        if (member.kind == BracketMember::Kind::Char && member.decoded && member.byte == '-') {
            dashes += '-';
            continue;
        }
        if (member.kind == BracketMember::Kind::Char && member.byte == ']'
            && (member.decoded || member.firstRaw))
            continue;  // emitted first above
        if (member.kind == BracketMember::Kind::Char && member.firstRaw && member.byte == '-') {
            // A leading raw '-' is a member only right after '['; a ']' member
            // before it is written first, so it goes collated here.
            out += "[.-.]";
            continue;
        }
        AppendMember(member, out);
        if (!hats.empty()) {  // something now precedes the '^' members
            out += hats;
            hats.clear();
        }
    }
    for (size_t h = 0; h < hats.size(); ++h)
        out += "[.^.]";  // a member set of '^' alone: a collating element
    out += dashes;
    out += ']';
    i = p;
}

}  // namespace

std::string TranslateAwkRegex(std::string_view awkRegex, std::vector<std::string>& warnings) {
    std::string out;
    const size_t n = awkRegex.size();
    size_t i = 0;
    while (i < n) {
        char c = awkRegex[i];
        if (c == '\\')
            AppendOutsideEscape(awkRegex, i, out, warnings);
        else if (c == '[')
            EmitBracket(awkRegex, i, out, warnings);
        else {
            out += c;
            ++i;
        }
    }
    return out;
}

std::string AwkRegexAsWritten(std::string_view awkRegex) {
    std::string out;
    const size_t n = awkRegex.size();
    size_t i = 0;
    bool inBracket = false;
    bool firstInBracket = true;
    while (i < n) {
        char c = awkRegex[i];
        if (c == '\\' && i + 1 < n) {
            out += c;
            out += awkRegex[i + 1];
            i += 2;
            continue;
        }
        if (inBracket) {
            if (c == ']' && !firstInBracket) {
                inBracket = false;
            } else if (c == '[' && i + 1 < n) {
                char lead = awkRegex[i + 1];
                if (lead == ':' || lead == '.' || lead == '=') {
                    size_t j = i + 2;
                    while (j + 1 < n && !(awkRegex[j] == lead && awkRegex[j + 1] == ']'))
                        ++j;
                    if (j + 1 < n) {
                        out.append(awkRegex.substr(i, j + 2 - i));
                        i = j + 2;
                        firstInBracket = false;
                        continue;
                    }
                }
            }
            firstInBracket = false;
            out += c;
            ++i;
            continue;
        }
        if (c == '[') {
            inBracket = true;
            firstInBracket = true;
            out += c;
            ++i;
            if (i < n && awkRegex[i] == '^') {  // the negation, not a member
                out += '^';
                ++i;
            }
            continue;
        }
        if (c == '/')
            out += "\\/";  // it can only have been written so
        else
            out += c;
        ++i;
    }
    return out;
}

std::shared_ptr<const Regex> AwkRegexCache::Get(const std::string& awkRegex,
                                                std::vector<std::string>& warnings,
                                                std::string& error) {
    auto it = m_compiled.find(awkRegex);
    if (it != m_compiled.end())
        return it->second;
    if (m_compiled.size() >= 256)
        m_compiled.clear();
    std::vector<std::string> translatedWarnings;
    std::string translated = TranslateAwkRegex(awkRegex, translatedWarnings);
    std::string compileError;
    std::shared_ptr<const Regex> compiled =
        Regex::Compile(translated, RegexOptions{RegexSyntax::Extended}, compileError);
    warnings = std::move(translatedWarnings);
    if (!compiled) {
        error = compileError;
        return nullptr;
    }
    m_compiled.emplace(awkRegex, compiled);
    return compiled;
}

void SplitByRegex(std::string_view text, const Regex& regex, std::vector<std::string>& fields) {
    if (text.empty())
        return;
    size_t searchFrom = 0;
    size_t pieceStart = 0;
    RegexMatch match;
    while (searchFrom <= text.size()) {
        if (!regex.Search(text, searchFrom, match))
            break;
        ptrdiff_t start = match.groups[0].first;
        ptrdiff_t end = match.groups[0].second;
        if (start == end) {
            // An empty match never separates; and no non-empty match can
            // start at its position (leftmost-longest), so moving past it
            // misses nothing.
            searchFrom = static_cast<size_t>(start) + 1;
            continue;
        }
        fields.emplace_back(text.substr(pieceStart, static_cast<size_t>(start) - pieceStart));
        pieceStart = static_cast<size_t>(end);
        searchFrom = static_cast<size_t>(end);
    }
    fields.emplace_back(text.substr(pieceStart));
}

}  // namespace Haisos::Awk