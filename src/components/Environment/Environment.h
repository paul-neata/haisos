#pragma once
#include <map>
#include <mutex>
#include "interfaces/IEnvironment.h"

namespace Haisos {

// An in-memory IEnvironment. Every accessor is mutex-guarded: one environment
// is shared by an OS and whatever it hands it to, and those run on different
// threads (a process per thread, plus tool calls on the agents' threads).
class Environment : public IEnvironment {
public:
    static std::shared_ptr<Environment> Create() {
        return std::shared_ptr<Environment>(new Environment());
    }
    ~Environment() override = default;

    std::shared_ptr<IEnvironment> Clone() const override;

    bool AddVariable(const std::string& name, const std::string& value) override;
    void SetVariable(const std::string& name, const std::string& value) override;
    bool RemoveVariable(const std::string& name) override;
    bool HasVariable(const std::string& name) const override;
    std::optional<std::string> GetVariable(const std::string& name) const override;
    std::vector<std::string> GetVariableNames() const override;

    bool AddSecret(const std::string& name, const std::string& value) override;
    void SetSecret(const std::string& name, const std::string& value) override;
    bool RemoveSecret(const std::string& name) override;
    bool HasSecret(const std::string& name) const override;
    std::vector<std::string> GetSecretNames() const override;

    bool AddLLMIdentifier(const std::string& name, const LLMIdentifier& identifier) override;
    void SetLLMIdentifier(const std::string& name, const LLMIdentifier& identifier) override;
    bool RemoveLLMIdentifier(const std::string& name) override;
    bool HasLLMIdentifier(const std::string& name) const override;
    std::optional<LLMIdentifier> GetLLMIdentifier(const std::string& name) const override;
    std::vector<std::string> GetLLMIdentifierNames() const override;

protected:
    std::optional<std::string> ReadSecretValue(const std::string& name) const override;

private:
    Environment() = default;
    Environment(
        std::map<std::string, std::string> variables,
        std::map<std::string, std::string> secrets,
        std::map<std::string, LLMIdentifier> llmIdentifiers);

    mutable std::mutex m_mutex;
    std::map<std::string, std::string> m_variables;
    std::map<std::string, std::string> m_secrets;
    std::map<std::string, LLMIdentifier> m_llmIdentifiers;
};

std::shared_ptr<IEnvironment> CreateEnvironment();

}
