#include "Regex.h"

#include <utility>

#include "RegexParser.h"
#include "RegexProgram.h"

namespace Haisos {

struct Regex::Compiled {
    RegexOptions options;
    RegexTree tree;
    RegexProgram program;
};

Regex::Regex(std::unique_ptr<Compiled> compiled) : m_compiled(std::move(compiled)) {}

Regex::~Regex() = default;

std::shared_ptr<const Regex> Regex::Compile(std::string_view pattern, const RegexOptions& options, std::string& error) {
    RegexTree tree;
    if (!ParseRegex(pattern, options, tree, error)) return nullptr;
    RegexProgram program;
    if (!CompileRegexProgram(tree, options.syntax, program, error)) return nullptr;
    auto compiled = std::make_unique<Compiled>();
    compiled->options = options;
    compiled->tree = std::move(tree);
    compiled->program = std::move(program);
    return std::shared_ptr<const Regex>(new Regex(std::move(compiled)));
}

bool Regex::Search(std::string_view text, size_t start, RegexMatch& match, int flags) const {
    const Compiled& compiled = *m_compiled;
    if (start > text.size()) return false;
    std::vector<std::ptrdiff_t> slots;
    const bool found = compiled.program.hasBackReferences
        ? BacktrackSearch(compiled.program, text, start, flags, slots)
        : PikeSearch(compiled.program, text, start, flags, slots);
    if (!found) return false;
    match.groups.assign(compiled.tree.groupCount + 1, std::make_pair<std::ptrdiff_t, std::ptrdiff_t>(-1, -1));
    for (size_t g = 0; g <= compiled.tree.groupCount; ++g) {
        std::ptrdiff_t first = slots[2 * g];
        std::ptrdiff_t last = slots[2 * g + 1];
        if (first >= 0 && last >= 0) match.groups[g] = {first, last};
    }
    return true;
}

size_t Regex::GroupCount() const {
    return m_compiled->tree.groupCount;
}

const std::vector<std::string>& Regex::GroupNames() const {
    return m_compiled->tree.groupNames;
}

const RegexOptions& Regex::Options() const {
    return m_compiled->options;
}

} // namespace Haisos