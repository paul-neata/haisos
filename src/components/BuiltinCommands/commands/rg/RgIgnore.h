#pragma once
#include <string>
#include <string_view>
#include <vector>

namespace Haisos {

// What matching a path against one gitignore-syntax pattern set came to.
enum class RgMatch { None, Ignore, Whitelist };

// One gitignore-syntax file (or the -g globs): its patterns in order.
class RgGitignore {
public:
    RgGitignore() = default;

    // |text| the file's content; |caseFold| for --iglob/--glob-case-insensitive.
    static RgGitignore Parse(std::string_view text, bool caseFold) {
        RgGitignore parsed;
        parsed.AddLines(text, caseFold);
        return parsed;
    }

    // More lines, keeping their order (so -g and --iglob may mix, each
    // pattern with its own caseFold).
    void AddLines(std::string_view text, bool caseFold);

    // |relativePath|: '/'-separated, relative to the file's directory (no
    // leading "./"). The last pattern that matches decides: Ignore, or
    // Whitelist for a '!' pattern; None when no pattern matches.
    RgMatch Match(std::string_view relativePath, bool isDirectory) const;
    bool HasWhitelist() const;  // any '!'-less pattern (for -g semantics)
    bool Empty() const { return m_patterns.empty(); }

private:
    struct Pattern {
        std::vector<std::string> components;  // the pattern split on '/'
        bool anchored = false;                 // holds a '/': the whole path
        bool dirOnly = false;                  // a trailing '/'
        bool negate = false;                   // a leading '!'
        bool caseFold = false;
    };

    static void ParseLine(std::string_view line, bool caseFold,
                          std::vector<Pattern>& out);

    // Whether the pattern's components |pi..| match the path's components
    // |qi..|: each ordinary component through FnMatch (a '*' never crosses a
    // '/', for components hold none), a '**' component every number of path
    // components -- zero when it leads or sits in the middle, one or more
    // when it trails. Recursive per pattern component, never per byte.
    static bool MatchComponents(const std::vector<std::string>& pattern, size_t patternIndex,
                                const std::vector<std::string_view>& path, size_t pathIndex,
                                bool caseFold);

    std::vector<Pattern> m_patterns;
};

} // namespace Haisos