#include "commands/hsh/HshGlob.h"

#include <algorithm>

#include "commands/hsh/HshPattern.h"

namespace Haisos::Hsh {

namespace {

// The components of |pattern|, split at each unescaped '/' (a bracket
// expression never spans a '/'). Escapes stay in the component text -- each
// component is MatchPattern syntax of its own.
std::vector<std::string> SplitComponents(const std::string& pattern) {
    std::vector<std::string> components;
    std::string current;
    for (size_t i = 0; i < pattern.size();) {
        char c = pattern[i];
        if (c == '\\' && i + 1 < pattern.size()) {
            current += c;
            current += pattern[i + 1];
            i += 2;
        } else if (c == '[' && PatternBracketEnd(pattern, i) != std::string_view::npos) {
            size_t end = PatternBracketEnd(pattern, i);
            current.append(pattern, i, end - i);
            i = end;
        } else if (c == '/') {
            components.push_back(std::move(current));
            current.clear();
            ++i;
        } else {
            current += c;
            ++i;
        }
    }
    components.push_back(std::move(current));
    return components;
}

// The directory |prefix| names, as IPathnameSource wants it: "." for the
// empty (relative) prefix, "/" for the root, the trailing '/' off otherwise.
std::string DirectoryOf(const std::string& prefix) {
    if (prefix.empty()) return ".";
    if (prefix == "/") return "/";
    return prefix.substr(0, prefix.size() - 1);
}

} // namespace

std::vector<std::string> ExpandPathname(const std::string& pattern, IPathnameSource& source) {
    bool absolute = !pattern.empty() && pattern[0] == '/';
    std::vector<std::string> components = SplitComponents(pattern);
    std::vector<std::string> prefixes{absolute ? "/" : ""};
    for (size_t c = absolute ? 1 : 0; c < components.size() && !prefixes.empty(); ++c) {
        const std::string& component = components[c];
        bool last = c + 1 == components.size();
        std::vector<std::string> next;
        if (!HasPatternCharacters(component)) {
            // A literal component names one thing: append it without reading.
            // As the last component it must exist ("*/f"); a trailing empty
            // component ("d*/") keeps the '/'.
            std::string text = UnescapePattern(component);
            for (const std::string& prefix : prefixes) {
                std::string candidate = prefix + text;
                if (last) {
                    if (source.Exists(candidate)) next.push_back(std::move(candidate));
                } else {
                    next.push_back(candidate + "/");
                }
            }
        } else {
            bool dotOk = component[0] == '.'
                || (component.size() >= 2 && component[0] == '\\' && component[1] == '.');
            for (const std::string& prefix : prefixes) {
                for (const DirectoryEntry& entry : source.ReadDirectory(DirectoryOf(prefix))) {
                    if (!dotOk && !entry.name.empty() && entry.name[0] == '.') continue;
                    if (!MatchPattern(component, entry.name)) continue;
                    if (!last && entry.type != DirectoryEntryType::Dir) continue;
                    next.push_back(last ? prefix + entry.name : prefix + entry.name + "/");
                }
            }
        }
        prefixes = std::move(next);
    }
    std::sort(prefixes.begin(), prefixes.end());
    return prefixes;
}

} // namespace Haisos::Hsh
