#include "RgIgnore.h"
#include <string>
#include <string_view>
#include <vector>
#include "BuiltinFnmatch.h"

namespace Haisos {
namespace {

// The pattern's components: |text| split on '/'. A '**' that is not a whole
// component is left where it is -- FnMatch reads it as '*' twice.
std::vector<std::string> SplitComponents(std::string_view text) {
    std::vector<std::string> components;
    size_t pos = 0;
    while (true) {
        const size_t slash = text.find('/', pos);
        if (slash == std::string_view::npos) {
            components.emplace_back(text.substr(pos));
            return components;
        }
        components.emplace_back(text.substr(pos, slash - pos));
        pos = slash + 1;
    }
}

std::vector<std::string_view> SplitPathView(std::string_view text) {
    std::vector<std::string_view> components;
    size_t pos = 0;
    while (true) {
        const size_t slash = text.find('/', pos);
        if (slash == std::string_view::npos) {
            components.push_back(text.substr(pos));
            return components;
        }
        components.push_back(text.substr(pos, slash - pos));
        pos = slash + 1;
    }
}

// One component of a path against one component of a pattern: glibc's
// fnmatch in the C locale, case folded or not.
bool MatchComponent(const std::string& pattern, std::string_view name, bool caseFold) {
    return FnMatch(pattern, name, caseFold ? kFnmCaseFold : 0);
}

} // namespace

// One line of a gitignore file, as gitignore(5) reads it (and as ripgrep is
// observed to read the same rules): a trailing '\r' gone, blank lines and
// '#' comments skipped ('\#' a literal '#'), trailing spaces dropped unless
// escaped ('\ '), a leading '!' negating ('\!' a literal '!'), a trailing '/'
// making the pattern match directories only. What remains holds a '/' -- a
// leading one removed -- the pattern is anchored: matched against the whole
// relative path; otherwise against the last component only, at any depth.
void RgGitignore::ParseLine(std::string_view line, bool caseFold,
                            std::vector<Pattern>& out) {
    if (!line.empty() && line.back() == '\r') {
        line.remove_suffix(1);
    }
    if (line.empty() || line.front() == '#') {
        return;  // blank, or a comment ('\#' arrives without its backslash)
    }
    if (!line.empty() && line.front() == '\\' && line.size() > 1 && line[1] == '#') {
        line.remove_prefix(1);  // '\#': a literal '#'
    }
    bool negate = false;
    if (!line.empty() && line.front() == '!') {
        negate = true;
        line.remove_prefix(1);
    } else if (line.size() > 1 && line.front() == '\\' && line[1] == '!') {
        line.remove_prefix(1);  // '\!': a literal '!'
    }
    // Trailing spaces, unless the last one is escaped with a '\'.
    while (!line.empty() && line.back() == ' ') {
        if (line.size() >= 2 && line[line.size() - 2] == '\\') {
            break;
        }
        line.remove_suffix(1);
    }
    bool dirOnly = false;
    if (!line.empty() && line.back() == '/') {
        dirOnly = true;
        line.remove_suffix(1);
        while (!line.empty() && line.back() == ' ') {  // a '/' after the spaces
            if (line.size() >= 2 && line[line.size() - 2] == '\\') {
                break;
            }
            line.remove_suffix(1);
        }
    }
    if (line.empty()) {
        return;
    }
    bool anchored = line.find('/') != std::string_view::npos;
    if (anchored && line.front() == '/') {
        line.remove_prefix(1);
    }
    if (line.empty()) {
        return;  // '/' alone: the file's own directory, matched by nothing
    }
    Pattern pattern;
    pattern.components = SplitComponents(line);
    pattern.anchored = anchored;
    pattern.dirOnly = dirOnly;
    pattern.negate = negate;
    pattern.caseFold = caseFold;
    out.push_back(std::move(pattern));
}

void RgGitignore::AddLines(std::string_view text, bool caseFold) {
    while (!text.empty()) {
        size_t end = text.find('\n');
        if (end == std::string_view::npos) {
            ParseLine(text, caseFold, m_patterns);
            return;
        }
        ParseLine(text.substr(0, end), caseFold, m_patterns);
        text.remove_prefix(end + 1);
    }
}

bool RgGitignore::MatchComponents(const std::vector<std::string>& pattern,
                                  size_t patternIndex,
                                  const std::vector<std::string_view>& path,
                                  size_t pathIndex, bool caseFold) {
    while (patternIndex < pattern.size()) {
        const std::string& component = pattern[patternIndex];
        if (component == "**") {
            if (patternIndex + 1 == pattern.size() && patternIndex > 0) {
                // a trailing '/**': everything below -- one component or more
                return pathIndex < path.size();
            }
            // zero or more path components
            for (size_t skip = pathIndex; skip <= path.size(); ++skip) {
                if (MatchComponents(pattern, patternIndex + 1, path, skip, caseFold)) {
                    return true;
                }
            }
            return false;
        }
        if (pathIndex >= path.size()) {
            return false;
        }
        if (!MatchComponent(component, path[pathIndex], caseFold)) {
            return false;
        }
        ++patternIndex;
        ++pathIndex;
    }
    return pathIndex == path.size();
}

RgMatch RgGitignore::Match(std::string_view relativePath, bool isDirectory) const {
    if (m_patterns.empty() || relativePath.empty()) {
        return RgMatch::None;
    }
    const std::vector<std::string_view> path = SplitPathView(relativePath);
    RgMatch result = RgMatch::None;
    for (const auto& pattern : m_patterns) {
        bool matched = false;
        if (isDirectory || !pattern.dirOnly) {
            if (pattern.anchored) {
                matched = MatchComponents(pattern.components, 0, path, 0, pattern.caseFold);
            } else {
                // no '/': against the last component only, at any depth
                matched = MatchComponent(pattern.components.front(), path.back(),
                                          pattern.caseFold);
            }
        }
        if (matched) {
            result = pattern.negate ? RgMatch::Whitelist : RgMatch::Ignore;
        }
    }
    return result;
}

bool RgGitignore::HasWhitelist() const {
    for (const auto& pattern : m_patterns) {
        if (!pattern.negate) {
            return true;
        }
    }
    return false;
}

} // namespace Haisos