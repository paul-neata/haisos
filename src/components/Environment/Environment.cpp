#include "Environment.h"

namespace Haisos {

namespace {

template <typename Map>
std::vector<std::string> NamesOf(const Map& entries) {
    std::vector<std::string> names;
    names.reserve(entries.size());
    for (const auto& entry : entries) {
        names.push_back(entry.first);
    }
    return names;
}

} // namespace

Environment::Environment(
    std::map<std::string, std::string> variables,
    std::map<std::string, std::string> secrets,
    std::map<std::string, LLMIdentifier> llmIdentifiers)
    : m_variables(std::move(variables))
    , m_secrets(std::move(secrets))
    , m_llmIdentifiers(std::move(llmIdentifiers))
{
}

std::shared_ptr<IEnvironment> Environment::Clone() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    // Copies the secrets across without handing them to the caller: the clone
    // is built here, from inside the environment that already holds them.
    return std::shared_ptr<Environment>(new Environment(m_variables, m_secrets, m_llmIdentifiers));
}

bool Environment::AddVariable(const std::string& name, const std::string& value) {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_variables.emplace(name, value).second;
}

void Environment::SetVariable(const std::string& name, const std::string& value) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_variables[name] = value;
}

bool Environment::RemoveVariable(const std::string& name) {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_variables.erase(name) > 0;
}

bool Environment::HasVariable(const std::string& name) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_variables.count(name) > 0;
}

std::optional<std::string> Environment::GetVariable(const std::string& name) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_variables.find(name);
    if (it == m_variables.end()) {
        return std::nullopt;
    }
    return it->second;
}

std::vector<std::string> Environment::GetVariableNames() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return NamesOf(m_variables);
}

bool Environment::AddSecret(const std::string& name, const std::string& value) {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_secrets.emplace(name, value).second;
}

void Environment::SetSecret(const std::string& name, const std::string& value) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_secrets[name] = value;
}

bool Environment::RemoveSecret(const std::string& name) {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_secrets.erase(name) > 0;
}

bool Environment::HasSecret(const std::string& name) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_secrets.count(name) > 0;
}

std::vector<std::string> Environment::GetSecretNames() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return NamesOf(m_secrets);
}

bool Environment::AddLLMIdentifier(const std::string& name, const LLMIdentifier& identifier) {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_llmIdentifiers.emplace(name, identifier).second;
}

void Environment::SetLLMIdentifier(const std::string& name, const LLMIdentifier& identifier) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_llmIdentifiers[name] = identifier;
}

bool Environment::RemoveLLMIdentifier(const std::string& name) {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_llmIdentifiers.erase(name) > 0;
}

bool Environment::HasLLMIdentifier(const std::string& name) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_llmIdentifiers.count(name) > 0;
}

std::optional<LLMIdentifier> Environment::GetLLMIdentifier(const std::string& name) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_llmIdentifiers.find(name);
    if (it == m_llmIdentifiers.end()) {
        return std::nullopt;
    }
    return it->second;
}

std::vector<std::string> Environment::GetLLMIdentifierNames() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return NamesOf(m_llmIdentifiers);
}

std::optional<std::string> Environment::ReadSecretValue(const std::string& name) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_secrets.find(name);
    if (it == m_secrets.end()) {
        return std::nullopt;
    }
    return it->second;
}

std::shared_ptr<IEnvironment> CreateEnvironment() {
    return Environment::Create();
}

}
