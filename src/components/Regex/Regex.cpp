#include "Regex.h"

#include <utility>

#include "RegexParser.h"

namespace Haisos {

struct Regex::Compiled {
    RegexOptions options;
    RegexTree tree;
};

Regex::Regex(std::unique_ptr<Compiled> compiled) : m_compiled(std::move(compiled)) {}

Regex::~Regex() = default;

std::shared_ptr<const Regex> Regex::Compile(std::string_view pattern, const RegexOptions& options, std::string& error) {
    RegexTree tree;
    if (!ParseRegex(pattern, options, tree, error)) return nullptr;
    auto compiled = std::make_unique<Compiled>();
    compiled->options = options;
    compiled->tree = std::move(tree);
    return std::shared_ptr<const Regex>(new Regex(std::move(compiled)));
}

bool Regex::Search(std::string_view text, size_t start, RegexMatch& match, int flags) const {
    // Implemented by base--regex-match, which compiles the parse tree into a
    // program and runs it; until then no pattern finds anything.
    (void)text;
    (void)start;
    (void)match;
    (void)flags;
    return false;
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