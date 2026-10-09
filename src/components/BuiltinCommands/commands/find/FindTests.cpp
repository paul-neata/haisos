// The tests, positional options and global options of find (the actions come
// in a later task, as FindActionPrimaries()): every row GNU find has, with
// its messages, its number formats and its window arithmetic, and the
// primaries they parse to. -perm's mode is GNU's grammar, applied to 0.
#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string>
#include <vector>

#include "BuiltinCommand.h"
#include "BuiltinCommandList.h"
#include "BuiltinDate.h"
#include "BuiltinFnmatch.h"
#include "BuiltinText.h"
#include "commands/find/FindExpression.h"
#include "interfaces/IFileSystemService.h"
#include "src/components/Regex/Regex.h"

namespace Haisos::Find {
namespace {

// One number with its GNU comparison prefix: +N, -N or N.
enum class FindComparison { Equal, Greater, Less };

// A primary of its own kind, as the one pointer type every parser hands
// around: so a lambda may return one of these or an empty result with the
// same deduced type.
template <typename T, typename... A>
std::unique_ptr<FindPrimary> MakePrimary(A&&... args) {
    return std::make_unique<T>(std::forward<A>(args)...);
}

// GNU's get_num: an optional single sign, then digits only.
bool ParseInteger(const std::string& raw, FindComparison& kind, int64_t& value) {
    size_t i = 0;
    kind = FindComparison::Equal;
    if (i < raw.size() && (raw[i] == '+' || raw[i] == '-')) {
        kind = raw[i] == '+' ? FindComparison::Greater : FindComparison::Less;
        ++i;
    }
    if (i >= raw.size()) {
        return false;
    }
    for (size_t j = i; j < raw.size(); ++j) {
        if (raw[j] < '0' || raw[j] > '9') {
            return false;
        }
    }
    value = std::strtoll(raw.c_str() + i, nullptr, 10);
    return true;
}

// GNU's get_relative_timestamp number: an optional sign, then digits with at
// most one '.'.
bool ParseTimeCount(const std::string& raw, FindComparison& kind, double& value) {
    size_t i = 0;
    kind = FindComparison::Equal;
    if (i < raw.size() && (raw[i] == '+' || raw[i] == '-')) {
        kind = raw[i] == '+' ? FindComparison::Greater : FindComparison::Less;
        ++i;
    }
    const size_t numberStart = i;
    bool sawDot = false;
    bool sawDigit = false;
    for (; i < raw.size(); ++i) {
        if (raw[i] == '.') {
            if (sawDot) {
                return false;
            }
            sawDot = true;
        } else if (raw[i] >= '0' && raw[i] <= '9') {
            sawDigit = true;
        } else {
            return false;
        }
    }
    if (!sawDigit) {
        return false;
    }
    value = std::strtod(raw.c_str() + numberStart, nullptr);
    return true;
}

// GNU's insert_depthspec number: plain digits only, with its own message.
// Returns -1 after Fail(...) (an overflow is reported too).
int ParseDepth(FindParser& parser, const std::string& name, const std::string& arg) {
    if (arg.empty() || arg.find_first_not_of("0123456789") != std::string::npos) {
        parser.Fail("Expected a positive decimal integer argument to " + name + ", but got " + GnuQuote(arg));
        return -1;
    }
    errno = 0;
    std::strtol(arg.c_str(), nullptr, 10);
    if (errno == ERANGE) {
        parser.Fail(arg + ": Numerical result out of range");
        return -1;
    }
    return static_cast<int>(std::strtol(arg.c_str(), nullptr, 10));
}

// A whole FileDateTime as nanoseconds (every timestamp here fits int64_t).
int64_t NanosecondsOf(const FileDateTime& time) {
    return time.seconds * 1000000000 + time.nanoseconds;
}

std::unique_ptr<FindPrimary> AlwaysTrue() {
    class TruePrimary : public FindPrimary {
    public:
        bool Evaluate(FindRun&, const FindFile&) override { return true; }
    };
    return MakePrimary<TruePrimary>();
}

std::unique_ptr<FindPrimary> AlwaysFalse() {
    class FalsePrimary : public FindPrimary {
    public:
        bool Evaluate(FindRun&, const FindFile&) override { return false; }
    };
    return MakePrimary<FalsePrimary>();
}

// Which file time a primary looks at.
enum class FindField { Access, Modification, Change };

const FileDateTime& FieldTime(const FindFile& file, FindField field) {
    switch (field) {
    case FindField::Access:
        return file.status.accessTime;
    case FindField::Change:
        return file.status.changeTime;
    default:
        return file.status.modificationTime;
    }
}

// -name/-iname and -path/-ipath: a glob on the base name or the whole path.
class FindGlobPrimary : public FindPrimary {
public:
    FindGlobPrimary(std::string pattern, bool fold, bool wholePath)
        : m_pattern(std::move(pattern)), m_fold(fold), m_wholePath(wholePath) {}

    bool Evaluate(FindRun&, const FindFile& file) override {
        return FnMatch(m_pattern, m_wholePath ? file.path : file.name, m_fold ? kFnmCaseFold : 0);
    }

private:
    std::string m_pattern;
    bool m_fold;
    bool m_wholePath;
};

// -regex/-iregex: the pattern matches the whole path.
class FindRegexPrimary : public FindPrimary {
public:
    explicit FindRegexPrimary(std::shared_ptr<const Regex> regex) : m_regex(std::move(regex)) {}

    bool Evaluate(FindRun&, const FindFile& file) override {
        RegexMatch match;
        if (!m_regex->Search(file.path, 0, match)) {
            return false;
        }
        return match.groups[0].first == 0
            && static_cast<size_t>(match.groups[0].second) == file.path.size();
    }

private:
    std::shared_ptr<const Regex> m_regex;
};

// -type/-xtype: a set of file types. b, p, l, s and D name types HaisosOS
// has none of, so they never match (GNU dies on D; the plan has it taken).
class FindTypePrimary : public FindPrimary {
public:
    FindTypePrimary(bool file, bool dir, bool charDevice)
        : m_file(file), m_dir(dir), m_charDevice(charDevice) {}

    bool Evaluate(FindRun&, const FindFile& file) override {
        switch (file.status.type) {
        case DirectoryEntryType::File:
            return m_file;
        case DirectoryEntryType::Dir:
            return m_dir;
        case DirectoryEntryType::CharDevice:
            return m_charDevice;
        default:
            return false;
        }
    }

private:
    bool m_file;
    bool m_dir;
    bool m_charDevice;
};

// -empty: a zero-size file, a directory holding nothing but . and ..
class FindEmptyPrimary : public FindPrimary {
public:
    bool Evaluate(FindRun& run, const FindFile& file) override {
        if (file.status.type != DirectoryEntryType::Dir) {
            return file.status.size == 0;
        }
        for (const DirectoryEntry& entry : run.context.IO().ReadDirectory(file.path)) {
            if (entry.name != "." && entry.name != "..") {
                return false;
            }
        }
        return true;
    }
};

// -size: the file's size rounded up to the unit, compared with the count.
class FindSizePrimary : public FindPrimary {
public:
    FindSizePrimary(int64_t units, FindComparison kind, int64_t count)
        : m_units(units), m_kind(kind), m_count(count) {}

    bool Evaluate(FindRun&, const FindFile& file) override {
        const int64_t rounded = static_cast<int64_t>(
            (file.status.size + static_cast<uint64_t>(m_units) - 1) / static_cast<uint64_t>(m_units));
        switch (m_kind) {
        case FindComparison::Equal:
            return rounded == m_count;
        case FindComparison::Greater:
            return rounded > m_count;
        case FindComparison::Less:
            return rounded < m_count;
        }
        return false;
    }

private:
    int64_t m_units;
    FindComparison m_kind;
    int64_t m_count;
};

// -amin/-cmin/-mmin and -atime/-ctime/-mtime: the file's age from an origin
// every primary takes as it is parsed. The two families window their count
// differently, as GNU's pred_timewindow's two callers do.
class FindTimePrimary : public FindPrimary {
public:
    FindTimePrimary(FindField field, int64_t unitSeconds, FindComparison kind, double count,
                    FileDateTime origin)
        : m_field(field), m_kind(kind), m_origin(origin) {
        const int64_t unit = unitSeconds * 1000000000;
        // -N: newer than N units, both families (GNU drops the 86399 seconds
        // the -time one adds to the origin; the plan keeps none of it).
        m_lessThan = static_cast<int64_t>(std::llround(count * unit));
        // N: the window is [N*u, (N+1)*u) for the -time family (its origin
        // is a day early), [(N-1)*u, N*u) for the -min one.
        m_equalLow = static_cast<int64_t>(std::llround((count - 1.0) * unit));
        m_equalHigh = static_cast<int64_t>(std::llround(count * unit));
        if (unitSeconds == 86400) {
            m_equalLow = static_cast<int64_t>(std::llround(count * unit));
            m_equalHigh = static_cast<int64_t>(std::llround((count + 1.0) * unit));
        }
    }

    bool Evaluate(FindRun&, const FindFile& file) override {
        const int64_t age = NanosecondsOf(m_origin) - NanosecondsOf(FieldTime(file, m_field));
        switch (m_kind) {
        case FindComparison::Equal:
            return age >= m_equalLow && age < m_equalHigh;
        case FindComparison::Greater:
            return age > m_equalHigh;
        case FindComparison::Less:
            return age < m_lessThan;
        }
        return false;
    }

private:
    FindField m_field;
    FindComparison m_kind;
    FileDateTime m_origin;
    int64_t m_lessThan = 0;
    int64_t m_equalLow = 0;
    int64_t m_equalHigh = 0;
};

// -used: the time between the last change of content and the last read,
// false whenever the file was changed after it was last read.
class FindUsedPrimary : public FindPrimary {
public:
    FindUsedPrimary(FindComparison kind, double count) : m_kind(kind) {
        const int64_t unit = 86400LL * 1000000000LL;
        m_low = static_cast<int64_t>(std::llround((count - 1.0) * unit));
        m_high = static_cast<int64_t>(std::llround(count * unit));
    }

    bool Evaluate(FindRun&, const FindFile& file) override {
        if (file.status.accessTime < file.status.changeTime) {
            return false;
        }
        const int64_t used = NanosecondsOf(file.status.accessTime)
            - NanosecondsOf(file.status.changeTime);
        switch (m_kind) {
        case FindComparison::Equal:
            return used >= m_low && used < m_high;
        case FindComparison::Greater:
            return used > m_high;
        case FindComparison::Less:
            return used < m_high;
        }
        return false;
    }

private:
    FindComparison m_kind;
    int64_t m_low = 0;
    int64_t m_high = 0;
};

// -newer, -anewer, -cnewer and -newerXY: a file's time against a reference.
class FindNewerPrimary : public FindPrimary {
public:
    FindNewerPrimary(FindField field, FileDateTime reference)
        : m_field(field), m_reference(reference) {}

    bool Evaluate(FindRun&, const FindFile& file) override {
        return FieldTime(file, m_field) > m_reference;
    }

private:
    FindField m_field;
    FileDateTime m_reference;
};

// -perm: the mode every file is taken to have, 0777, against the pattern.
class FindPermPrimary : public FindPrimary {
public:
    enum class Kind { Exact, AtLeast, Any };

    FindPermPrimary(Kind kind, int64_t value) : m_kind(kind), m_value(value) {}

    bool Evaluate(FindRun&, const FindFile&) override {
        constexpr int64_t kFileMode = 0777;
        switch (m_kind) {
        case Kind::Exact:
            return kFileMode == m_value;
        case Kind::AtLeast:
            return (kFileMode & m_value) == m_value;
        case Kind::Any:
            return m_value == 0 || (kFileMode & m_value) != 0;
        }
        return false;
    }

private:
    Kind m_kind;
    int64_t m_value;
};

// -user and -group: every file's owner and group are haisos, id 0.
class FindOwnerPrimary : public FindPrimary {
public:
    explicit FindOwnerPrimary(int64_t id) : m_id(id) {}

    bool Evaluate(FindRun&, const FindFile&) override { return m_id == 0; }

private:
    int64_t m_id;
};

// -uid, -gid, -links, -inum: a number against a field of the file. HaisosOS
// reports no owner or group, so -uid and -gid compare with 0, and -inum,
// whose every inode is 0, can match nothing but a 0.
class FindNumberPrimary : public FindPrimary {
public:
    enum class Field { Id, Links, Inode };

    FindNumberPrimary(Field field, FindComparison kind, int64_t count)
        : m_field(field), m_kind(kind), m_count(count) {}

    bool Evaluate(FindRun&, const FindFile& file) override {
        int64_t value = 0;
        if (m_field == Field::Links) {
            value = static_cast<int64_t>(file.status.linkCount);
        }
        switch (m_kind) {
        case FindComparison::Equal:
            return value == m_count;
        case FindComparison::Greater:
            return value > m_count;
        case FindComparison::Less:
            return value < m_count;
        }
        return false;
    }

private:
    Field m_field;
    FindComparison m_kind;
    int64_t m_count;
};

// -samefile: the resolved path against the reference's.
class FindSamefilePrimary : public FindPrimary {
public:
    explicit FindSamefilePrimary(std::string resolved) : m_resolved(std::move(resolved)) {}

    bool Evaluate(FindRun& run, const FindFile& file) override {
        return run.context.IO().ResolvePath(file.path) == m_resolved;
    }

private:
    std::string m_resolved;
};

// -print and -print0: the path, with a delimiter of their own.
class FindPrintPrimary : public FindPrimary {
public:
    explicit FindPrintPrimary(char delimiter) : m_delimiter(delimiter) {}

    bool Evaluate(FindRun& run, const FindFile& file) override {
        run.context.Out(file.path + m_delimiter);
        return true;
    }

private:
    char m_delimiter;
};

// -prune and -quit: they only set their flag; the walk and the tree's
// combinators act on it.
class FindPrunePrimary : public FindPrimary {
public:
    bool Evaluate(FindRun& run, const FindFile&) override {
        run.prune = true;
        return true;
    }
};

class FindQuitPrimary : public FindPrimary {
public:
    bool Evaluate(FindRun& run, const FindFile&) override {
        run.quit = true;
        return true;
    }
};

// -perm's mode: octal, or symbolic clauses [ugoa]*[-+=][rwxXst]* (a u, g or o
// alone as the right side copies those bits), applied to 0 with umask 0, X as
// x. -1 when the mode is not valid.
int64_t ParsePermMode(const std::string& spec) {
    if (!spec.empty() && spec.find_first_not_of("01234567") == std::string::npos) {
        const int64_t value = std::strtoll(spec.c_str(), nullptr, 8);
        return value > 07777 ? -1 : value;
    }
    const auto classBits = [](char who) -> int64_t {
        return who == 'u' ? 0700 : (who == 'g' ? 0070 : 0007);
    };
    const auto shiftOf = [](char who) -> int {
        return who == 'u' ? 6 : (who == 'g' ? 3 : 0);
    };
    int64_t value = 0;
    size_t i = 0;
    while (i < spec.size()) {
        std::string targets;  // the classes this clause touches, as "u", "g", "o"
        while (i < spec.size() && (spec[i] == 'u' || spec[i] == 'g' || spec[i] == 'o' || spec[i] == 'a')) {
            if (spec[i] == 'a') {
                targets = "ugo";
            } else if (targets != "ugo" && targets.find(spec[i]) == std::string::npos) {
                targets += spec[i];
            }
            ++i;
        }
        if (targets.empty()) {
            targets = "ugo";
        }
        if (i >= spec.size() || (spec[i] != '+' && spec[i] != '-' && spec[i] != '=')) {
            return -1;
        }
        const char op = spec[i++];
        int64_t permBits = 0;    // the rwx bits the clause asks for
        int64_t specialBits = 0;  // the setuid, setgid and sticky bits
        const bool copies = i < spec.size() && (spec[i] == 'u' || spec[i] == 'g' || spec[i] == 'o')
            && (i + 1 >= spec.size() || spec[i + 1] == ',');
        if (copies) {
            for (const char target : targets) {
                permBits |= ((value >> shiftOf(spec[i])) & 7) << shiftOf(target);
            }
            ++i;
        } else {
            bool sawPerm = false;
            while (i < spec.size() && (spec[i] == 'r' || spec[i] == 'w' || spec[i] == 'x'
                || spec[i] == 'X' || spec[i] == 's' || spec[i] == 't')) {
                sawPerm = true;
                const char perm = spec[i++];
                for (const char target : targets) {
                    const int shift = shiftOf(target);
                    if (perm == 'r') {
                        permBits |= 4 << shift;
                    } else if (perm == 'w') {
                        permBits |= 2 << shift;
                    } else if (perm == 'x' || perm == 'X') {
                        permBits |= 1 << shift;
                    } else if (perm == 's') {
                        specialBits |= target == 'u' ? 04000 : (target == 'g' ? 02000 : 0);
                    } else {
                        specialBits |= 01000;  // t
                    }
                }
            }
            if (!sawPerm) {
                return -1;
            }
        }
        int64_t whoBits = specialBits;
        for (const char target : targets) {
            whoBits |= classBits(target);
        }
        if (op == '=') {
            value = (value & ~whoBits) | permBits;
        } else if (op == '+') {
            value |= permBits;
        } else {
            value &= ~permBits;
        }
        if (i < spec.size()) {
            if (spec[i] != ',') {
                return -1;
            }
            ++i;
        }
    }
    return value;
}

// The findutils-default and emacs regex types are glibc BREs with + and ?
// special and { } plain, \{ \} the intervals: translated to Basic outside
// bracket expressions.
std::string TranslateEmacsRegex(const std::string& pattern) {
    std::string out;
    bool inBracket = false;
    size_t bracketStart = 0;
    for (size_t i = 0; i < pattern.size(); ++i) {
        const char c = pattern[i];
        if (inBracket) {
            out += c;
            if (c == ']') {
                // ']' right after '[' (or after "[^") is a literal member.
                const bool caret = bracketStart + 1 < pattern.size() && pattern[bracketStart + 1] == '^';
                const size_t place = i - bracketStart;
                if (place != 1 && !(caret && place == 2)) {
                    inBracket = false;
                }
            }
            continue;
        }
        if (c == '[') {
            inBracket = true;
            bracketStart = i;
            out += c;
            continue;
        }
        if (c == '\\' && i + 1 < pattern.size()) {
            const char next = pattern[i + 1];
            if (next == '+' || next == '?' || next == '{' || next == '}') {
                out += next;
            } else {
                out += c;
                out += next;
            }
            ++i;
            continue;
        }
        if (c == '+' || c == '?') {
            out += '\\';
        }
        out += c;
    }
    return out;
}

// fnmatch against every starting point, as the -path trailing-slash warning
// needs: "." when none was given.
bool MatchesStartPoint(FindParser& parser, const std::string& pattern, bool fold) {
    const int flags = fold ? kFnmCaseFold : 0;
    const FindSettings& settings = parser.Settings();
    if (settings.startingPoints.empty()) {
        return FnMatch(pattern, ".", flags);
    }
    for (const std::string& point : settings.startingPoints) {
        if (FnMatch(pattern, point, flags)) {
            return true;
        }
    }
    return false;
}

// The file time of a reference file, or nullopt after Fail(...) when it is
// not there.
std::optional<FileDateTime> ReferenceTime(FindParser& parser, const std::string& arg, FindField field) {
    FileStatus status;
    if (parser.Context().IO().Stat(arg, status) != 0) {
        parser.Fail(GnuQuote(arg) + ": No such file or directory");
        return std::nullopt;
    }
    switch (field) {
    case FindField::Access:
        return status.accessTime;
    case FindField::Change:
        return status.changeTime;
    default:
        return status.modificationTime;
    }
}

// -uid, -gid, -links and -inum: one GNU integer with its comparison prefix,
// against a number of the file.
std::unique_ptr<FindPrimary> ParseNumberPrimary(FindParser& parser, const std::string& name,
                                                FindNumberPrimary::Field field) {
    std::string arg;
    if (!parser.NextArgument(name, arg)) {
        return std::unique_ptr<FindPrimary>();
    }
    FindComparison kind;
    int64_t count = 0;
    if (!ParseInteger(arg, kind, count)) {
        parser.Fail("invalid argument `" + arg + "' to `" + name + "'");
        return std::unique_ptr<FindPrimary>();
    }
    return MakePrimary<FindNumberPrimary>(field, kind, count);
}

FindField FieldOf(char c) {
    return c == 'a' ? FindField::Access : (c == 'c' ? FindField::Change : FindField::Modification);
}

// The -amin/-cmin/-mmin and -atime/-ctime/-ctime rows share their parsing.
std::unique_ptr<FindPrimary> ParseAgePrimary(FindParser& parser, const std::string& name,
                                             FindField field, int64_t unitSeconds) {
    std::string arg;
    if (!parser.NextArgument(name, arg)) {
        return std::unique_ptr<FindPrimary>();
    }
    FindComparison kind;
    double count = 0.0;
    if (!ParseTimeCount(arg, kind, count)) {
        parser.Fail("invalid argument `" + arg + "' to `" + name + "'");
        return std::unique_ptr<FindPrimary>();
    }
    if (std::fabs(count) > 1e12) {
        parser.Fail("arithmetic overflow while converting " + arg
            + (unitSeconds == 60 ? " minutes" : " days") + " to a number of seconds");
        return std::unique_ptr<FindPrimary>();
    }
    return MakePrimary<FindTimePrimary>(field, unitSeconds, kind, count,
                                              parser.State().timeOrigin);
}

// -regex and -iregex, compiled as they are parsed, as GNU's are.
std::unique_ptr<FindPrimary> CompileRegexPrimary(FindParser& parser, const std::string& pattern,
                                                 bool fold) {
    const std::string translated = parser.State().regexEmacs
        ? TranslateEmacsRegex(pattern) : pattern;
    RegexOptions options;
    options.syntax = parser.State().regexSyntax;
    options.ignoreCase = fold;
    std::string error;
    auto regex = Regex::Compile(translated, options, error);
    if (!regex) {
        // A pattern ending in an open bracket is how the findutils regexes
        // say "an invalid regular expression", whatever the engine says.
        if (error == "Unmatched [, [^, [:, [., or [="
            && (!translated.empty())
            && (translated.back() == '['
                || (translated.size() >= 2 && translated.substr(translated.size() - 2) == "[^"))) {
            error = "Invalid regular expression";
        }
        parser.Fail("failed to compile regular expression '" + pattern + "': " + error);
        return std::unique_ptr<FindPrimary>();
    }
    return MakePrimary<FindRegexPrimary>(std::move(regex));
}

// -type and -xtype: the same list of letters, the same messages, each named
// as it was spelled.
std::unique_ptr<FindPrimary> ParseTypePrimary(FindParser& parser, const std::string& name) {
    std::string arg;
    if (!parser.NextArgument(name, arg)) {
        return std::unique_ptr<FindPrimary>();
    }
    if (arg.empty()) {
        parser.Fail("Arguments to " + name + " should contain at least one letter");
        return std::unique_ptr<FindPrimary>();
    }
    if (arg.find(',') == std::string::npos && arg.size() > 1) {
        parser.Fail("Must separate multiple arguments to " + name + " using: ','");
        return std::unique_ptr<FindPrimary>();
    }
    bool file = false;
    bool dir = false;
    bool charDevice = false;
    int mask = 0;
    size_t i = 0;
    while (i < arg.size()) {
        const char c = arg[i];
        int bit = 0;
        switch (c) {
        case 'b': bit = 1; break;
        case 'c': bit = 2; charDevice = true; break;
        case 'd': bit = 4; dir = true; break;
        case 'p': bit = 8; break;
        case 'f': bit = 16; file = true; break;
        case 'l': bit = 32; break;
        case 's': bit = 64; break;
        case 'D': bit = 128; break;  // HaisosOS has no doors; GNU dies on it
        default:
            parser.Fail("Unknown argument to " + name + ": " + std::string(1, c));
            return std::unique_ptr<FindPrimary>();
        }
        if ((mask & bit) != 0) {
            parser.Fail("Duplicate file type '" + std::string(1, c) + "' in the argument list to "
                + name + ".");
            return std::unique_ptr<FindPrimary>();
        }
        mask |= bit;
        ++i;
        if (i < arg.size() && arg[i] == ',') {
            ++i;
            if (i == arg.size()) {
                parser.Fail("Last file type in list argument to " + name
                    + " is missing, i.e., list is ending on: ','");
                return std::unique_ptr<FindPrimary>();
            }
        } else if (i < arg.size()) {
            parser.Fail("Must separate multiple arguments to " + name + " using: ','");
            return std::unique_ptr<FindPrimary>();
        }
    }
    return MakePrimary<FindTypePrimary>(file, dir, charDevice);
}

// -newerXY, spelled as it is: x and y are each a (or B), c, m or t, x never
// t; y t takes a date string, any other a file's y time.
std::unique_ptr<FindPrimary> ParseNewerXYPrimary(FindParser& parser, const std::string& name) {
    const char x = name[6];
    const char y = name[7];
    if (x == 'B' || y == 'B') {
        parser.Context().Error("This system does not provide a way to find the birth time of a file.");
        parser.Fail("invalid predicate `" + name + "'");
        return std::unique_ptr<FindPrimary>();
    }
    const std::string letters = "acmt";
    if (x == 't' || letters.find(x) == std::string::npos || letters.find(y) == std::string::npos) {
        parser.Fail("invalid predicate `" + name + "'");
        return std::unique_ptr<FindPrimary>();
    }
    if (!parser.HasArgument()) {
        parser.Fail("The " + GnuQuote(name) + " test needs an argument");
        return std::unique_ptr<FindPrimary>();
    }
    std::string arg;
    if (!parser.NextArgument(name, arg)) {
        return std::unique_ptr<FindPrimary>();
    }
    FileDateTime reference;
    if (y == 't') {
        if (!ParseDateString(arg, parser.State().startTime, reference)) {
            parser.Fail("I cannot figure out how to interpret " + GnuQuote(arg)
                + " as a date or time");
            return std::unique_ptr<FindPrimary>();
        }
    } else {
        auto time = ReferenceTime(parser, arg, FieldOf(y));
        if (!time) {
            return std::unique_ptr<FindPrimary>();
        }
        reference = *time;
    }
    return MakePrimary<FindNewerPrimary>(FieldOf(x), reference);
}

// GNU's valid -regextype names, in the order its message lists them.
const std::vector<std::string>& RegexTypeNames() {
    static const std::vector<std::string> names = {
        "findutils-default", "ed", "emacs", "gnu-awk", "grep", "posix-awk", "awk",
        "posix-basic", "posix-egrep", "egrep", "posix-extended", "posix-minimal-basic", "sed"
    };
    return names;
}

// -name and -path, with the warning each gives for a pattern it cannot
// match: -name a directory separator, -path a trailing one.
std::unique_ptr<FindPrimary> ParseGlobPrimary(FindParser& parser, const std::string& name,
                                              const std::string& alternative, bool wholePath,
                                              bool fold) {
    std::string arg;
    if (!parser.NextArgument(name, arg)) {
        return std::unique_ptr<FindPrimary>();
    }
    if (!wholePath && arg.find('/') != std::string::npos) {
        parser.Warn(GnuQuote(name) + " matches against basenames only, but the given pattern"
            " contains a directory separator ('/'), thus the expression will evaluate to false"
            " all the time.  Did you mean '" + alternative + "'?");
    }
    if (wholePath && !arg.empty() && arg.back() == '/' && !MatchesStartPoint(parser, arg, fold)) {
        parser.Context().Error("warning: " + name + " " + arg
            + " will not match anything because it ends with /.");
    }
    return MakePrimary<FindGlobPrimary>(arg, fold, wholePath);
}

}  // namespace

const std::vector<FindPrimaryEntry>& FindTestPrimaries() {
    static const std::vector<FindPrimaryEntry> rows = {
        {"-depth", FindPrimaryKind::GlobalOption, false,
            [](FindParser& parser, const std::string&) {
                parser.Settings().depthFirst = true;
                parser.Settings().depthGiven = true;
                return AlwaysTrue();
            }},
        {"-d", FindPrimaryKind::GlobalOption, false,
            [](FindParser& parser, const std::string&) {
                parser.Warn("the -d option is deprecated; please use -depth instead,"
                    " because the latter is a POSIX-compliant feature.");
                parser.Settings().depthFirst = true;
                parser.Settings().depthGiven = true;
                return AlwaysTrue();
            }},
        {"-maxdepth", FindPrimaryKind::GlobalOption, false,
            [](FindParser& parser, const std::string& name) {
                std::string arg;
                if (!parser.NextArgument(name, arg)) {
                    return std::unique_ptr<FindPrimary>();
                }
                const int depth = ParseDepth(parser, name, arg);
                if (depth < 0) {
                    return std::unique_ptr<FindPrimary>();
                }
                parser.Settings().maxDepth = depth;
                return AlwaysTrue();
            }},
        {"-mindepth", FindPrimaryKind::GlobalOption, false,
            [](FindParser& parser, const std::string& name) {
                std::string arg;
                if (!parser.NextArgument(name, arg)) {
                    return std::unique_ptr<FindPrimary>();
                }
                const int depth = ParseDepth(parser, name, arg);
                if (depth < 0) {
                    return std::unique_ptr<FindPrimary>();
                }
                parser.Settings().minDepth = depth;
                return AlwaysTrue();
            }},
        {"-mount", FindPrimaryKind::GlobalOption, false,
            [](FindParser& parser, const std::string& name) {
                parser.Context().NotTreated(name);
                return AlwaysTrue();
            }},
        {"-xdev", FindPrimaryKind::GlobalOption, false,
            [](FindParser& parser, const std::string& name) {
                parser.Context().NotTreated(name);
                return AlwaysTrue();
            }},
        {"-noleaf", FindPrimaryKind::GlobalOption, false,
            [](FindParser&, const std::string&) { return AlwaysTrue(); }},
        {"-ignore_readdir_race", FindPrimaryKind::GlobalOption, false,
            [](FindParser& parser, const std::string&) {
                parser.Settings().ignoreReaddirRace = true;
                return AlwaysTrue();
            }},
        {"-noignore_readdir_race", FindPrimaryKind::GlobalOption, false,
            [](FindParser& parser, const std::string&) {
                parser.Settings().ignoreReaddirRace = false;
                return AlwaysTrue();
            }},
        {"-files0-from", FindPrimaryKind::GlobalOption, false,
            [](FindParser& parser, const std::string& name) {
                std::string arg;
                if (!parser.NextArgument(name, arg)) {
                    return std::unique_ptr<FindPrimary>();
                }
                parser.Settings().files0From = arg;
                return AlwaysTrue();
            }},
        {"-help", FindPrimaryKind::GlobalOption, false,
            [](FindParser& parser, const std::string&) {
                parser.Context().Out(BuiltinHelpText(*CreateFindCommand()));
                return std::unique_ptr<FindPrimary>();  // ends the parse, exit 0
            }},
        {"-version", FindPrimaryKind::GlobalOption, false,
            [](FindParser& parser, const std::string&) {
                parser.Context().Out(BuiltinVersionText(*CreateFindCommand()));
                return std::unique_ptr<FindPrimary>();
            }},
        {"-daystart", FindPrimaryKind::PositionalOption, false,
            [](FindParser& parser, const std::string&) {
                FindParseState& state = parser.State();
                if (state.timeOrigin == state.startTime) {  // the first -daystart only
                    // The start of tomorrow, local: GNU's cur_day_start plus a
                    // day, the origin both time families measure their ages
                    // from, so that a window [0, D) is today.
                    int64_t seconds = state.startTime.seconds + 86400;
                    const std::tm local = LocalTimeOf(seconds);
                    seconds -= local.tm_sec + local.tm_min * 60 + local.tm_hour * 3600;
                    state.timeOrigin = FileDateTime{seconds, 0};
                }
                return AlwaysTrue();
            }},
        {"-follow", FindPrimaryKind::PositionalOption, false,
            [](FindParser&, const std::string&) { return AlwaysTrue(); }},
        {"-regextype", FindPrimaryKind::PositionalOption, false,
            [](FindParser& parser, const std::string& name) {
                std::string arg;
                if (!parser.NextArgument(name, arg)) {
                    return std::unique_ptr<FindPrimary>();
                }
                const std::vector<std::string>& names = RegexTypeNames();
                if (std::find(names.begin(), names.end(), arg) == names.end()) {
                    std::string message = "Unknown regular expression type " + GnuQuote(arg)
                        + "; valid types are ";
                    for (size_t i = 0; i < names.size(); ++i) {
                        message += "'" + names[i] + (i + 1 == names.size() ? "'." : "', ");
                    }
                    parser.Fail(message);
                    return std::unique_ptr<FindPrimary>();
                }
                FindParseState& state = parser.State();
                state.regexEmacs = arg == "findutils-default" || arg == "emacs";
                state.regexSyntax = (arg == "gnu-awk" || arg == "posix-awk" || arg == "awk"
                    || arg == "posix-egrep" || arg == "egrep" || arg == "posix-extended")
                    ? RegexSyntax::Extended : RegexSyntax::Basic;
                // The awk family is ERE with GNU's extras, the ed family BRE
                // with its own: Haisos's engine has neither extra.
                if (arg == "gnu-awk" || arg == "posix-awk" || arg == "awk"
                    || arg == "ed" || arg == "sed" || arg == "posix-minimal-basic") {
                    parser.Context().NotTreated("-regextype " + arg);
                }
                return AlwaysTrue();
            }},
        {"-warn", FindPrimaryKind::PositionalOption, false,
            [](FindParser& parser, const std::string&) {
                parser.State().warnings = true;
                return AlwaysTrue();
            }},
        {"-nowarn", FindPrimaryKind::PositionalOption, false,
            [](FindParser& parser, const std::string&) {
                parser.State().warnings = false;
                return AlwaysTrue();
            }},
        {"-true", FindPrimaryKind::Test, false,
            [](FindParser&, const std::string&) { return AlwaysTrue(); }},
        {"-false", FindPrimaryKind::Test, false,
            [](FindParser&, const std::string&) { return AlwaysFalse(); }},
        {"-name", FindPrimaryKind::Test, false,
            [](FindParser& parser, const std::string& name) {
                return ParseGlobPrimary(parser, name, "-wholename", false, false);
            }},
        {"-iname", FindPrimaryKind::Test, false,
            [](FindParser& parser, const std::string& name) {
                return ParseGlobPrimary(parser, name, "-iwholename", false, true);
            }},
        {"-path", FindPrimaryKind::Test, false,
            [](FindParser& parser, const std::string& name) {
                return ParseGlobPrimary(parser, name, "", true, false);
            }},
        {"-wholename", FindPrimaryKind::Test, false,
            [](FindParser& parser, const std::string& name) {
                return ParseGlobPrimary(parser, name, "", true, false);
            }},
        {"-ipath", FindPrimaryKind::Test, false,
            [](FindParser& parser, const std::string& name) {
                return ParseGlobPrimary(parser, name, "", true, true);
            }},
        {"-iwholename", FindPrimaryKind::Test, false,
            [](FindParser& parser, const std::string& name) {
                return ParseGlobPrimary(parser, name, "", true, true);
            }},
        {"-regex", FindPrimaryKind::Test, false,
            [](FindParser& parser, const std::string& name) {
                std::string arg;
                if (!parser.NextArgument(name, arg)) {
                    return std::unique_ptr<FindPrimary>();
                }
                return CompileRegexPrimary(parser, arg, false);
            }},
        {"-iregex", FindPrimaryKind::Test, false,
            [](FindParser& parser, const std::string& name) {
                std::string arg;
                if (!parser.NextArgument(name, arg)) {
                    return std::unique_ptr<FindPrimary>();
                }
                return CompileRegexPrimary(parser, arg, true);
            }},
        {"-type", FindPrimaryKind::Test, false,
            [](FindParser& parser, const std::string& name) {
                return ParseTypePrimary(parser, name);
            }},
        {"-xtype", FindPrimaryKind::Test, false,
            [](FindParser& parser, const std::string& name) {
                return ParseTypePrimary(parser, name);
            }},
        {"-empty", FindPrimaryKind::Test, false,
            [](FindParser&, const std::string&) {
                return MakePrimary<FindEmptyPrimary>();
            }},
        {"-size", FindPrimaryKind::Test, false,
            [](FindParser& parser, const std::string& name) {
                std::string arg;
                if (!parser.NextArgument(name, arg)) {
                    return std::unique_ptr<FindPrimary>();
                }
                if (arg.empty()) {
                    parser.Fail("invalid null argument to -size");
                    return std::unique_ptr<FindPrimary>();
                }
                int64_t units = 512;  // no suffix: 512-byte blocks, as -size b
                std::string number = arg;
                const char last = arg.back();
                if (last < '0' || last > '9') {
                    switch (last) {
                    case 'b': units = 512; break;
                    case 'c': units = 1; break;
                    case 'w': units = 2; break;
                    case 'k': units = 1024; break;
                    case 'M': units = 1024 * 1024; break;
                    case 'G': units = 1024 * 1024 * 1024; break;
                    default:
                        parser.Fail("invalid -size type `" + std::string(1, last) + "'");
                        return std::unique_ptr<FindPrimary>();
                    }
                    number = arg.substr(0, arg.size() - 1);
                }
                FindComparison kind;
                int64_t count = 0;
                if (!ParseInteger(number, kind, count)) {
                    parser.Fail("Invalid argument `" + arg + "' to -size");
                    return std::unique_ptr<FindPrimary>();
                }
                return MakePrimary<FindSizePrimary>(units, kind, count);
            }},
        {"-amin", FindPrimaryKind::Test, false,
            [](FindParser& parser, const std::string& name) {
                return ParseAgePrimary(parser, name, FindField::Access, 60);
            }},
        {"-cmin", FindPrimaryKind::Test, false,
            [](FindParser& parser, const std::string& name) {
                return ParseAgePrimary(parser, name, FindField::Change, 60);
            }},
        {"-mmin", FindPrimaryKind::Test, false,
            [](FindParser& parser, const std::string& name) {
                return ParseAgePrimary(parser, name, FindField::Modification, 60);
            }},
        {"-atime", FindPrimaryKind::Test, false,
            [](FindParser& parser, const std::string& name) {
                return ParseAgePrimary(parser, name, FindField::Access, 86400);
            }},
        {"-ctime", FindPrimaryKind::Test, false,
            [](FindParser& parser, const std::string& name) {
                return ParseAgePrimary(parser, name, FindField::Change, 86400);
            }},
        {"-mtime", FindPrimaryKind::Test, false,
            [](FindParser& parser, const std::string& name) {
                return ParseAgePrimary(parser, name, FindField::Modification, 86400);
            }},
        {"-used", FindPrimaryKind::Test, false,
            [](FindParser& parser, const std::string& name) {
                std::string arg;
                if (!parser.NextArgument(name, arg)) {
                    return std::unique_ptr<FindPrimary>();
                }
                FindComparison kind;
                double count = 0.0;
                if (!ParseTimeCount(arg, kind, count)) {
                    parser.Fail("Invalid argument " + arg + " to -used");
                    return std::unique_ptr<FindPrimary>();
                }
                if (std::fabs(count) > 1e12) {
                    parser.Fail("arithmetic overflow while converting " + arg
                        + " days to a number of seconds");
                    return std::unique_ptr<FindPrimary>();
                }
                return MakePrimary<FindUsedPrimary>(kind, count);
            }},
        {"-newer", FindPrimaryKind::Test, false,
            [](FindParser& parser, const std::string& name) {
                std::string arg;
                if (!parser.NextArgument(name, arg)) {
                    return std::unique_ptr<FindPrimary>();
                }
                auto reference = ReferenceTime(parser, arg, FindField::Modification);
                if (!reference) {
                    return std::unique_ptr<FindPrimary>();
                }
                return MakePrimary<FindNewerPrimary>(FindField::Modification, *reference);
            }},
        {"-anewer", FindPrimaryKind::Test, false,
            [](FindParser& parser, const std::string& name) {
                std::string arg;
                if (!parser.NextArgument(name, arg)) {
                    return std::unique_ptr<FindPrimary>();
                }
                auto reference = ReferenceTime(parser, arg, FindField::Modification);
                if (!reference) {
                    return std::unique_ptr<FindPrimary>();
                }
                return MakePrimary<FindNewerPrimary>(FindField::Access, *reference);
            }},
        {"-cnewer", FindPrimaryKind::Test, false,
            [](FindParser& parser, const std::string& name) {
                std::string arg;
                if (!parser.NextArgument(name, arg)) {
                    return std::unique_ptr<FindPrimary>();
                }
                auto reference = ReferenceTime(parser, arg, FindField::Modification);
                if (!reference) {
                    return std::unique_ptr<FindPrimary>();
                }
                return MakePrimary<FindNewerPrimary>(FindField::Change, *reference);
            }},
        {"-newerXY", FindPrimaryKind::Test, false,
            [](FindParser& parser, const std::string& name) {
                return ParseNewerXYPrimary(parser, name);
            }},
        {"-perm", FindPrimaryKind::Test, false,
            [](FindParser& parser, const std::string& name) {
                std::string arg;
                if (!parser.NextArgument(name, arg)) {
                    return std::unique_ptr<FindPrimary>();
                }
                FindPermPrimary::Kind kind = FindPermPrimary::Kind::Exact;
                std::string mode = arg;
                if (!mode.empty() && mode[0] == '-') {
                    kind = FindPermPrimary::Kind::AtLeast;
                    mode = mode.substr(1);
                } else if (!mode.empty() && mode[0] == '/') {
                    kind = FindPermPrimary::Kind::Any;
                    mode = mode.substr(1);
                }
                const int64_t value = ParsePermMode(mode);
                if (value < 0) {
                    parser.Fail("invalid mode " + GnuQuote(arg));
                    return std::unique_ptr<FindPrimary>();
                }
                if (kind == FindPermPrimary::Kind::Any && value == 0) {
                    parser.Context().Error("warning: you have specified a mode pattern " + arg
                        + " (which is equivalent to /000). The meaning of -perm /000 has now been"
                        " changed to be consistent with -perm -000; that is, while it used to"
                        " match no files, it now matches all files.");
                }
                return MakePrimary<FindPermPrimary>(kind, value);
            }},
        {"-user", FindPrimaryKind::Test, false,
            [](FindParser& parser, const std::string& name) {
                std::string arg;
                if (!parser.NextArgument(name, arg)) {
                    return std::unique_ptr<FindPrimary>();
                }
                if (arg.empty()) {
                    parser.Fail("The argument to -user should not be empty");
                    return std::unique_ptr<FindPrimary>();
                }
                if (arg == "haisos") {
                    return MakePrimary<FindOwnerPrimary>(0);
                }
                FindComparison kind;
                int64_t id = 0;
                if (!ParseInteger(arg, kind, id)) {
                    parser.Fail(GnuQuote(arg) + " is not the name of a known user");
                    return std::unique_ptr<FindPrimary>();
                }
                return MakePrimary<FindOwnerPrimary>(id);
            }},
        {"-group", FindPrimaryKind::Test, false,
            [](FindParser& parser, const std::string& name) {
                std::string arg;
                if (!parser.NextArgument(name, arg)) {
                    return std::unique_ptr<FindPrimary>();
                }
                if (arg.empty()) {
                    parser.Fail("argument to -group is empty, but should be a group name");
                    return std::unique_ptr<FindPrimary>();
                }
                if (arg != "haisos") {
                    FindComparison kind;
                    int64_t id = 0;
                    if (ParseInteger(arg, kind, id)) {
                        return MakePrimary<FindOwnerPrimary>(id);
                    }
                    // A name that begins like a numeric ID but does not end
                    // as one has its suffix named.
                    size_t digits = 0;
                    while (digits < arg.size() && arg[digits] >= '0' && arg[digits] <= '9') {
                        ++digits;
                    }
                    if (digits > 0) {
                        parser.Fail(GnuQuote(arg) + " is not the name of an existing group and it"
                            " does not look like a numeric group ID because it has the unexpected"
                            " suffix " + GnuQuote(arg.substr(digits)));
                        return std::unique_ptr<FindPrimary>();
                    }
                    parser.Fail(GnuQuote(arg) + " is not the name of an existing group");
                    return std::unique_ptr<FindPrimary>();
                }
                return MakePrimary<FindOwnerPrimary>(0);
            }},
        {"-uid", FindPrimaryKind::Test, false,
            [](FindParser& parser, const std::string& name) {
                return ParseNumberPrimary(parser, name, FindNumberPrimary::Field::Id);
            }},
        {"-gid", FindPrimaryKind::Test, false,
            [](FindParser& parser, const std::string& name) {
                return ParseNumberPrimary(parser, name, FindNumberPrimary::Field::Id);
            }},
        {"-nouser", FindPrimaryKind::Test, false,
            [](FindParser&, const std::string&) { return AlwaysFalse(); }},
        {"-nogroup", FindPrimaryKind::Test, false,
            [](FindParser&, const std::string&) { return AlwaysFalse(); }},
        {"-links", FindPrimaryKind::Test, false,
            [](FindParser& parser, const std::string& name) {
                return ParseNumberPrimary(parser, name, FindNumberPrimary::Field::Links);
            }},
        {"-inum", FindPrimaryKind::Test, false,
            [](FindParser& parser, const std::string& name) {
                return ParseNumberPrimary(parser, name, FindNumberPrimary::Field::Inode);
            }},
        {"-samefile", FindPrimaryKind::Test, false,
            [](FindParser& parser, const std::string& name) {
                std::string arg;
                if (!parser.NextArgument(name, arg)) {
                    return std::unique_ptr<FindPrimary>();
                }
                FileStatus status;
                if (parser.Context().IO().Stat(arg, status) != 0) {
                    parser.Fail(GnuQuote(arg) + ": No such file or directory");
                    return std::unique_ptr<FindPrimary>();
                }
                return MakePrimary<FindSamefilePrimary>(
                    parser.Context().IO().ResolvePath(arg));
            }},
        {"-lname", FindPrimaryKind::Test, false,
            [](FindParser& parser, const std::string& name) {
                std::string arg;
                if (!parser.NextArgument(name, arg)) {
                    return std::unique_ptr<FindPrimary>();
                }
                return AlwaysFalse();  // HaisosOS creates no symbolic links
            }},
        {"-ilname", FindPrimaryKind::Test, false,
            [](FindParser& parser, const std::string& name) {
                std::string arg;
                if (!parser.NextArgument(name, arg)) {
                    return std::unique_ptr<FindPrimary>();
                }
                return AlwaysFalse();
            }},
        {"-readable", FindPrimaryKind::Test, false,
            [](FindParser&, const std::string&) { return AlwaysTrue(); }},
        {"-writable", FindPrimaryKind::Test, false,
            [](FindParser&, const std::string&) { return AlwaysTrue(); }},
        {"-executable", FindPrimaryKind::Test, false,
            [](FindParser&, const std::string&) { return AlwaysTrue(); }},
        {"-fstype", FindPrimaryKind::Test, false,
            [](FindParser& parser, const std::string& name) {
                std::string arg;
                if (!parser.NextArgument(name, arg)) {
                    return std::unique_ptr<FindPrimary>();
                }
                parser.Context().NotTreated(name);
                return AlwaysFalse();
            }},
        {"-context", FindPrimaryKind::Test, false,
            [](FindParser& parser, const std::string& name) {
                std::string arg;
                if (!parser.NextArgument(name, arg)) {
                    return std::unique_ptr<FindPrimary>();
                }
                parser.Fail("invalid predicate -context: SELinux is not enabled.");
                return std::unique_ptr<FindPrimary>();
            }},
        {"-print", FindPrimaryKind::Action, true,
            [](FindParser&, const std::string&) {
                return MakePrimary<FindPrintPrimary>('\n');
            }},
        {"-print0", FindPrimaryKind::Action, true,
            [](FindParser&, const std::string&) {
                return MakePrimary<FindPrintPrimary>('\0');
            }},
        {"-prune", FindPrimaryKind::Action, false,
            [](FindParser&, const std::string&) {
                return MakePrimary<FindPrunePrimary>();
            }},
        {"-quit", FindPrimaryKind::Action, false,
            [](FindParser&, const std::string&) {
                return MakePrimary<FindQuitPrimary>();
            }},
    };
    return rows;
}

}  // namespace Haisos::Find