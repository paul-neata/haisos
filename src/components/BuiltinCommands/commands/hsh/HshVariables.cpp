#include "commands/hsh/HshVariables.h"

#include "commands/hsh/HshWord.h"

namespace Haisos::Hsh {

void ShellVariables::ImportFrom(const IEnvironment& environment) {
    for (const std::string& name : environment.GetVariableNames()) {
        if (!IsValidShellName(name)) {
            continue;
        }
        std::optional<std::string> value = environment.GetVariable(name);
        if (!value) {
            continue;
        }
        Entry& entry = m_variables[name];
        entry.value = *value;
        entry.exported = true;
    }
}

std::optional<std::string> ShellVariables::Get(const std::string& name) const {
    auto it = m_variables.find(name);
    if (it == m_variables.end()) {
        return std::nullopt;
    }
    return it->second.value;
}

bool ShellVariables::IsSet(const std::string& name) const {
    auto it = m_variables.find(name);
    return it != m_variables.end() && it->second.value.has_value();
}

bool ShellVariables::Set(const std::string& name, const std::string& value) {
    Entry& entry = m_variables[name];
    if (entry.readonly) {
        return false;
    }
    entry.value = value;
    return true;
}

bool ShellVariables::Unset(const std::string& name) {
    auto it = m_variables.find(name);
    if (it == m_variables.end()) {
        return true;
    }
    if (it->second.readonly) {
        return false;
    }
    m_variables.erase(it);
    return true;
}

void ShellVariables::Export(const std::string& name) {
    m_variables[name].exported = true;
}

bool ShellVariables::IsExported(const std::string& name) const {
    auto it = m_variables.find(name);
    return it != m_variables.end() && it->second.exported;
}

void ShellVariables::MakeReadonly(const std::string& name) {
    m_variables[name].readonly = true;
}

bool ShellVariables::IsReadonly(const std::string& name) const {
    auto it = m_variables.find(name);
    return it != m_variables.end() && it->second.readonly;
}

std::vector<std::string> ShellVariables::Names() const {
    std::vector<std::string> names;
    names.reserve(m_variables.size());
    for (const auto& [name, entry] : m_variables) {
        names.push_back(name);
    }
    return names; // a map's order is the byte order
}

std::vector<std::pair<std::string, std::string>> ShellVariables::ExportedVariables() const {
    std::vector<std::pair<std::string, std::string>> exported;
    for (const auto& [name, entry] : m_variables) {
        if (entry.exported && entry.value.has_value()) {
            exported.emplace_back(name, *entry.value);
        }
    }
    return exported;
}

std::string OptionLetters(const ShellOptions& options) {
    std::string letters;
    if (options.nounset) letters += 'u';
    if (options.allexport) letters += 'a';
    if (options.noclobber) letters += 'C';
    if (options.verbose) letters += 'v';
    if (options.xtrace) letters += 'x';
    if (options.stdinInput) letters += 's';
    if (options.noexec) letters += 'n';
    if (options.interactive) letters += 'i';
    if (options.noglob) letters += 'f';
    if (options.errexit) letters += 'e';
    return letters;
}

} // namespace Haisos::Hsh
