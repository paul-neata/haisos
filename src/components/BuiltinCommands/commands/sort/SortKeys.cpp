#include "commands/sort/SortKeys.h"
#include <cstdint>
#include <cstring>
#include "BuiltinCompare.h"
#include "BuiltinText.h"

namespace Haisos {
namespace {

// What GNU sort calls a blank: space, tab -- and the newline a -z record may
// hold inside itself, which splits fields the same way.
bool IsSortBlank(char c) {
    return c == ' ' || c == '\t' || c == '\n';
}

bool IsAsciiAlnum(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

char FoldCase(char c) {
    return (c >= 'a' && c <= 'z') ? static_cast<char>(c - 'a' + 'A') : c;
}

// Plain byte order: memcmp of the common length, the shorter text smaller.
int CompareBytes(std::string_view a, std::string_view b) {
    const size_t common = a.size() < b.size() ? a.size() : b.size();
    if (common != 0) {
        const int diff = std::memcmp(a.data(), b.data(), common);
        if (diff != 0) {
            return diff < 0 ? -1 : 1;
        }
    }
    if (a.size() != b.size()) {
        return a.size() < b.size() ? -1 : 1;
    }
    return 0;
}

// Whether one text's modifier flags are set at all -- the test GNU's
// inheritance uses: a key with no modifier takes the global options, a key
// with any takes none of them.
// (KeyHasModifier's exported twin lives at the bottom of this file.)

// A byte ignored by -d or -i: not a blank or alphanumeric (d), or not one of
// the C locale's printable 0x20-0x7E (i).
bool IgnoredByModifiers(char c, const SortKey& key) {
    if (key.dictionary && !IsSortBlank(c) && !IsAsciiAlnum(c)) {
        return true;
    }
    const unsigned char byte = static_cast<unsigned char>(c);
    if (key.ignoreNonprinting && (byte < 0x20 || byte > 0x7e)) {
        return true;
    }
    return false;
}

// The key's text as -R hashes it: case folded, the bytes -d/-i ignore
// skipped, in one owned string.
std::string RandomTransformedText(std::string_view text, const SortKey& key) {
    std::string out;
    for (const char c : text) {
        if (IgnoredByModifiers(c, key)) {
            continue;
        }
        out += FoldCase(c);
    }
    return out;
}

// FNV-1a over the salt bytes then the key's bytes, finished with a
// splitmix64 step: enough to scatter, and cheap per line.
uint64_t HashKeyText(const std::string& text, const SortSettings& settings) {
    uint64_t hash = 1469598103934665603ULL;  // FNV-1a 64 offset basis
    const auto mixByte = [&hash](unsigned char byte) {
        hash ^= byte;
        hash *= 1099511628211ULL;  // FNV-1a 64 prime
    };
    for (const uint64_t salt : settings.randomSalt) {
        for (int byte = 0; byte < 8; ++byte) {
            mixByte(static_cast<unsigned char>((salt >> (8 * byte)) & 0xFF));
        }
    }
    for (const unsigned char byte : text) {
        mixByte(byte);
    }
    hash += 0x9E3779B97F4A7C15ULL;
    hash = (hash ^ (hash >> 30)) * 0xBF58476D1CE4E5B9ULL;
    hash = (hash ^ (hash >> 27)) * 0x94D049BB133111EBULL;
    return hash ^ (hash >> 31);
}

// sort -R: by the hash of each key's transformed text, with the run's salt;
// equal hashes fall back to the transformed bytes, so distinct keys never
// tie and equal keys always do.
int CompareRandomTexts(std::string_view a, std::string_view b, const SortKey& key,
                       const SortSettings& settings) {
    const std::string textA = RandomTransformedText(a, key);
    const std::string textB = RandomTransformedText(b, key);
    const uint64_t hashA = HashKeyText(textA, settings);
    const uint64_t hashB = HashKeyText(textB, settings);
    if (hashA != hashB) {
        return hashA < hashB ? -1 : 1;
    }
    return CompareBytes(textA, textB);
}

// The comparison of one key's texts: by its kind, or as text with the
// modifiers. The leading blanks of the key text are skipped for n, h and M
// (g relies on strtold, which skips whitespace itself).
int CompareKeyTexts(std::string_view a, std::string_view b, const SortKey& key,
                    const SortSettings& settings) {
    if (key.numeric || key.humanNumeric || key.month) {
        size_t aStart = 0;
        size_t bStart = 0;
        while (aStart < a.size() && IsSortBlank(a[aStart])) {
            ++aStart;
        }
        while (bStart < b.size() && IsSortBlank(b[bStart])) {
            ++bStart;
        }
        if (key.numeric) {
            return CompareNumeric(a.substr(aStart), b.substr(bStart));
        }
        if (key.humanNumeric) {
            return CompareHumanNumeric(a.substr(aStart), b.substr(bStart));
        }
        return CompareMonth(a.substr(aStart), b.substr(bStart));
    }
    if (key.generalNumeric) {
        return CompareGeneralNumeric(a, b);
    }
    if (key.random) {
        return CompareRandomTexts(a, b, key, settings);
    }
    if (key.version) {
        return CompareVersion(a, b);
    }
    if (!key.dictionary && !key.ignoreNonprinting && !key.foldCase) {
        return CompareBytes(a, b);
    }
    // Walk both texts skipping ignored bytes, folding case, the one that runs
    // out first the smaller.
    size_t i = 0;
    size_t j = 0;
    while (i < a.size() && j < b.size()) {
        while (i < a.size() && IgnoredByModifiers(a[i], key)) {
            ++i;
        }
        while (j < b.size() && IgnoredByModifiers(b[j], key)) {
            ++j;
        }
        if (i >= a.size() || j >= b.size()) {
            break;
        }
        // As unsigned bytes, as GNU's to_uchar: 0xC3 sorts after 'A'.
        const unsigned char ca = static_cast<unsigned char>(FoldCase(a[i]));
        const unsigned char cb = static_cast<unsigned char>(FoldCase(b[j]));
        if (ca != cb) {
            return ca < cb ? -1 : 1;
        }
        ++i;
        ++j;
    }
    while (i < a.size() && IgnoredByModifiers(a[i], key)) {
        ++i;
    }
    while (j < b.size() && IgnoredByModifiers(b[j], key)) {
        ++j;
    }
    if (i < a.size()) {
        return 1;
    }
    if (j < b.size()) {
        return -1;
    }
    return 0;
}

// One decimal count of a KEYDEF: an optional '+', then digits, saturating at
// SIZE_MAX (GNU accepts "-k 99999999999999999999" as "beyond the line").
// |restFrom| is where the "invalid count at start of" diagnostic's remainder
// starts -- before the '+', as GNU's does. Returns false when no digit fits.
bool ParseKeyCount(const std::string& keydef, size_t& pos, size_t& restFrom, size_t& value) {
    restFrom = pos;
    if (pos < keydef.size() && keydef[pos] == '+') {
        ++pos;
    }
    const size_t digitsStart = pos;
    size_t result = 0;
    bool overflow = false;
    while (pos < keydef.size() && keydef[pos] >= '0' && keydef[pos] <= '9') {
        const size_t digit = static_cast<size_t>(keydef[pos] - '0');
        if (!overflow && result > (SIZE_MAX - digit) / 10) {
            overflow = true;
        } else if (!overflow) {
            result = result * 10 + digit;
        }
        ++pos;
    }
    if (overflow) {
        result = SIZE_MAX;
    }
    value = result;
    return pos > digitsStart;
}

std::string RestOf(const std::string& keydef, size_t from) {
    return GnuQuote(std::string_view(keydef).substr(from));
}

void SkipBlanks(std::string_view text, size_t& pos) {
    while (pos < text.size() && IsSortBlank(text[pos])) {
        ++pos;
    }
}

} // namespace

bool ApplySortModifier(char letter, SortKey& key, bool forStart) {
    switch (letter) {
        case 'b':
            if (forStart) {
                key.skipStartBlanks = true;
            } else {
                key.skipEndBlanks = true;
            }
            return true;
        // GNU keeps only -d of -d and -i together, whichever order they
        // came in: 'd' always sets its own mode, 'i' only when none is set.
        case 'd':
            key.dictionary = true;
            key.ignoreNonprinting = false;
            return true;
        case 'f': key.foldCase = true; return true;
        case 'g': key.generalNumeric = true; return true;
        case 'i':
            if (!key.dictionary) {
                key.ignoreNonprinting = true;
            }
            return true;
        case 'M': key.month = true; return true;
        case 'h': key.humanNumeric = true; return true;
        case 'n': key.numeric = true; return true;
        case 'R': key.random = true; return true;
        case 'r': key.reverse = true; return true;
        case 'V': key.version = true; return true;
        default: return false;
    }
}

bool ParseSortKey(const std::string& keydef, SortKey& key, std::string& error) {
    size_t pos = 0;
    size_t restFrom = 0;
    size_t value = 0;
    if (!ParseKeyCount(keydef, pos, restFrom, value)) {
        error = "invalid number at field start: invalid count at start of " + RestOf(keydef, restFrom);
        return false;
    }
    if (value == 0) {
        error = "field number is zero: invalid field specification " + GnuQuote(keydef);
        return false;
    }
    key.startField = value - 1;
    if (pos < keydef.size() && keydef[pos] == '.') {
        ++pos;
        if (!ParseKeyCount(keydef, pos, restFrom, value)) {
            error = "invalid number after '.': invalid count at start of " + RestOf(keydef, restFrom);
            return false;
        }
        if (value == 0) {
            error = "character offset is zero: invalid field specification " + GnuQuote(keydef);
            return false;
        }
        key.startChar = value - 1;
    }
    while (pos < keydef.size() && ApplySortModifier(keydef[pos], key, /*forStart=*/true)) {
        ++pos;
    }
    if (pos < keydef.size() && keydef[pos] == ',') {
        ++pos;
        if (!ParseKeyCount(keydef, pos, restFrom, value)) {
            error = "invalid number after ',': invalid count at start of " + RestOf(keydef, restFrom);
            return false;
        }
        if (value == 0) {
            error = "field number is zero: invalid field specification " + GnuQuote(keydef);
            return false;
        }
        key.endField = value - 1;
        if (pos < keydef.size() && keydef[pos] == '.') {
            ++pos;
            if (!ParseKeyCount(keydef, pos, restFrom, value)) {
                error = "invalid number after '.': invalid count at start of " + RestOf(keydef, restFrom);
                return false;
            }
            key.endChar = value;  // 0: the end of the field
        }
        while (pos < keydef.size() && ApplySortModifier(keydef[pos], key, /*forStart=*/false)) {
            ++pos;
        }
    }
    if (pos < keydef.size()) {
        error = "stray character in field spec: invalid field specification " + GnuQuote(keydef);
        return false;
    }
    return true;
}

std::pair<size_t, size_t> SortKeyRange(std::string_view line, const SortKey& key, int tab) {
    const size_t len = line.size();
    size_t begin = 0;
    for (size_t i = 0; i < key.startField && begin < len; ++i) {
        if (tab >= 0) {
            while (begin < len && line[begin] != tab) {
                ++begin;
            }
            if (begin < len) {
                ++begin;
            }
        } else {
            SkipBlanks(line, begin);
            while (begin < len && !IsSortBlank(line[begin])) {
                ++begin;
            }
        }
    }
    if (key.skipStartBlanks) {
        SkipBlanks(line, begin);
    }
    begin += key.startChar;
    if (begin > len) {
        begin = len;
    }

    size_t end;
    if (key.endField == SIZE_MAX) {
        end = len;
    } else {
        size_t q = 0;
        const size_t words = key.endField + (key.endChar == 0 ? 1 : 0);
        for (size_t i = 0; i < words && q < len; ++i) {
            if (tab >= 0) {
                // To the next tab, stepping past it while more fields must be
                // crossed -- or always, when a character offset follows.
                while (q < len && line[q] != tab) {
                    ++q;
                }
                if (q < len && (i + 1 < words || key.endChar != 0)) {
                    ++q;
                }
            } else {
                SkipBlanks(line, q);
                while (q < len && !IsSortBlank(line[q])) {
                    ++q;
                }
            }
        }
        if (key.endChar != 0) {
            if (key.skipEndBlanks) {
                SkipBlanks(line, q);
            }
            q += key.endChar;
            if (q > len) {
                q = len;
            }
        }
        end = q;
    }
    if (end < begin) {
        end = begin;  // a key asked to end before it begins is empty
    }
    return {begin, end};
}

int CompareLines(std::string_view a, std::string_view b, const SortSettings& settings) {
    if (settings.keys.empty()) {
        // No -k at all: the whole lines are the only comparison, unique or
        // stable or not.
        int diff = CompareBytes(a, b);
        if (settings.reverse) {
            diff = -diff;
        }
        return diff;
    }
    for (const auto& key : settings.keys) {
        const auto [aBegin, aEnd] = SortKeyRange(a, key, settings.tab);
        const auto [bBegin, bEnd] = SortKeyRange(b, key, settings.tab);
        int diff = CompareKeyTexts(a.substr(aBegin, aEnd - aBegin), b.substr(bBegin, bEnd - bBegin), key, settings);
        if (key.reverse) {
            diff = -diff;
        }
        if (diff != 0) {
            return diff;
        }
    }
    // The last resort: GNU's whole-line byte compare, disabled by -u and -s.
    if (!settings.unique && !settings.stable) {
        int diff = CompareBytes(a, b);
        if (settings.reverse) {
            diff = -diff;
        }
        return diff;
    }
    return 0;
}

std::string IncompatibleOptions(const SortSettings& settings) {
    for (const auto& key : settings.keys) {
        const int count = (key.numeric ? 1 : 0) + (key.generalNumeric ? 1 : 0)
            + (key.humanNumeric ? 1 : 0) + (key.month ? 1 : 0)
            + ((key.version || key.random || key.dictionary || key.ignoreNonprinting) ? 1 : 0);
        if (count > 1) {
            // The key's options in the fixed order GNU lists them, b and r left
            // out.
            std::string opts;
            if (key.dictionary) opts += 'd';
            if (key.foldCase) opts += 'f';
            if (key.generalNumeric) opts += 'g';
            if (key.humanNumeric) opts += 'h';
            if (key.ignoreNonprinting) opts += 'i';
            if (key.month) opts += 'M';
            if (key.numeric) opts += 'n';
            if (key.random) opts += 'R';
            if (key.version) opts += 'V';
            return "options '-" + opts + "' are incompatible";
        }
    }
    return std::string();
}

bool SortKeyHasModifier(const SortKey& key) {
    return key.skipStartBlanks || key.skipEndBlanks || key.dictionary || key.foldCase
        || key.ignoreNonprinting || key.numeric || key.generalNumeric || key.humanNumeric
        || key.month || key.random || key.version || key.reverse;
}

} // namespace Haisos