#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Haisos {

// One -k KEYDEF, after inheritance. Field numbers are 0-based (KEYDEF's F-1),
// endField SIZE_MAX meaning "to the end of the line"; characters are 0-based
// offsets from the position begfield finds (C-1), endChar 0 meaning "the end
// of field endField".
struct SortKey {
    size_t startField = 0;
    size_t startChar = 0;
    size_t endField = SIZE_MAX;
    size_t endChar = 0;
    bool skipStartBlanks = false;      // b on POS1 (or global -b)
    bool skipEndBlanks = false;        // b on POS2 (or global -b)
    bool dictionary = false;           // d (wins over -i: 'i' does nothing once set)
    bool foldCase = false;             // f
    bool ignoreNonprinting = false;    // i
    bool numeric = false;              // n
    bool generalNumeric = false;      // g
    bool humanNumeric = false;         // h
    bool month = false;                // M
    bool random = false;               // R
    bool version = false;              // V
    bool reverse = false;             // r
};

struct SortSettings {
    std::vector<SortKey> keys;   // after inheritance; empty: whole line, no options
    int tab = -1;                // -1: blank-to-nonblank fields; else the byte (0..255)
    bool reverse = false;        // global -r (also reverses the last resort)
    bool unique = false;         // -u
    bool stable = false;         // -s
    char delimiter = '\n';       // '\0' with -z
    // The salt -R hashes each key with: set once per run, so the order
    // changes from run to run as GNU's does.
    uint64_t randomSalt[2] = {0, 0};
};

// Parses one -k KEYDEF into |key|. On error returns false with |error| the
// whole diagnostic after "sort: ".
bool ParseSortKey(const std::string& keydef, SortKey& key, std::string& error);

// Adds one modifier letter (bdfgiMhnRrV) to a key; false for any other letter.
// |forStart|: 'b' sets skipStartBlanks on POS1 and skipEndBlanks on POS2;
// every other letter applies to the key wherever it is written.
bool ApplySortModifier(char letter, SortKey& key, bool forStart);

// Whether any of a key's modifier flags is set -- the test GNU's inheritance
// uses: a key with no modifier takes the global ordering options, a key with
// any takes none of them.
bool SortKeyHasModifier(const SortKey& key);

// GNU's begfield/limfield: [start, end) of |key| in |line|.
std::pair<size_t, size_t> SortKeyRange(std::string_view line, const SortKey& key, int tab);

// The comparison: keys in order, then -- unless unique or stable -- the last
// resort. Negative, zero, positive.
int CompareLines(std::string_view a, std::string_view b, const SortSettings& settings);

// "options '-gn' are incompatible" (the text after "sort: "), or empty when
// every key is consistent.
std::string IncompatibleOptions(const SortSettings& settings);

} // namespace Haisos