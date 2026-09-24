#pragma once
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace Haisos {

// Everything needed to reach one LLM: where to send the request, which model to
// ask for, and how to authenticate. The credential is either an API token held
// in the clear or -- better -- the name of a secret in the same environment,
// which this program can name but never read; both may be absent for an
// endpoint that wants no authentication, such as a local Ollama. Model options
// (effort, reasoning, ...), generic and provider-specific, will join them here.
struct LLMIdentifier {
    std::string endpoint;
    std::string modelName;
    std::optional<std::string> apiToken;
    std::optional<std::string> secretName;
};

// What an OS or a process is given to run with. Deliberately not something a
// process can discover about the host: an environment is handed to an OS at
// creation, and to each process and sub-OS as it is started -- typically as a
// Clone() of the creator's own, after which the two are independent. Nothing is
// inherited implicitly; whoever starts something passes it an environment.
//
// It holds three separate sets of named entries:
//   * variables       -- plain key/value strings (the haisosfile's ENV lines)
//   * secrets         -- credentials that live outside the OS (passwords, API
//                        tokens): they can be named, copied and handed on, but
//                        deliberately not read, hence no GetSecret()
//   * LLM identifiers -- how to reach a model (see LLMIdentifier)
//
// Every set offers the same operations: Add (fails if the name is taken), Set
// (overrides whatever was there), Remove, Has, enumerate the names (in no
// particular order) and -- for everything but secrets -- Get.
class IEnvironment {
public:
    virtual ~IEnvironment() = default;

    // A deep, independent copy: the same variables, secrets and LLM
    // identifiers, with no further link to this one. Secrets come across
    // without being revealed to whoever asked for the clone.
    virtual std::shared_ptr<IEnvironment> Clone() const = 0;

    // --- Variables ---
    virtual bool AddVariable(const std::string& name, const std::string& value) = 0;
    virtual void SetVariable(const std::string& name, const std::string& value) = 0;
    virtual bool RemoveVariable(const std::string& name) = 0;
    virtual bool HasVariable(const std::string& name) const = 0;
    virtual std::optional<std::string> GetVariable(const std::string& name) const = 0;
    virtual std::vector<std::string> GetVariableNames() const = 0;

    // --- Secrets ---
    virtual bool AddSecret(const std::string& name, const std::string& value) = 0;
    virtual void SetSecret(const std::string& name, const std::string& value) = 0;
    virtual bool RemoveSecret(const std::string& name) = 0;
    virtual bool HasSecret(const std::string& name) const = 0;
    virtual std::vector<std::string> GetSecretNames() const = 0;

    // Copy a secret out of another environment under the name |name|, so a
    // secret can be handed on without anyone in between learning its value:
    // it travels environment-to-environment, never through the caller. Both
    // return false if |source| has no secret called |sourceName|; AddSecretFrom
    // also returns false if |name| is already taken here.
    bool AddSecretFrom(const std::string& name, const IEnvironment& source, const std::string& sourceName) {
        auto value = source.ReadSecretValue(sourceName);
        if (!value) {
            return false;
        }
        return AddSecret(name, *value);
    }

    bool SetSecretFrom(const std::string& name, const IEnvironment& source, const std::string& sourceName) {
        auto value = source.ReadSecretValue(sourceName);
        if (!value) {
            return false;
        }
        SetSecret(name, *value);
        return true;
    }

    // --- LLM identifiers ---
    virtual bool AddLLMIdentifier(const std::string& name, const LLMIdentifier& identifier) = 0;
    virtual void SetLLMIdentifier(const std::string& name, const LLMIdentifier& identifier) = 0;
    virtual bool RemoveLLMIdentifier(const std::string& name) = 0;
    virtual bool HasLLMIdentifier(const std::string& name) const = 0;
    virtual std::optional<LLMIdentifier> GetLLMIdentifier(const std::string& name) const = 0;
    virtual std::vector<std::string> GetLLMIdentifierNames() const = 0;

protected:
    // The only door onto a secret's value, and deliberately not a public one:
    // an agent, a tool or a process can name a secret and pass it on, but never
    // read it. Only another IEnvironment reaches through here -- that is what
    // makes AddSecretFrom/SetSecretFrom above safe -- plus, in the future,
    // whatever authenticates an outgoing request on the environment's behalf.
    virtual std::optional<std::string> ReadSecretValue(const std::string& name) const = 0;
};

// The variables the LLM configuration is read from. They are plain environment
// variables so that a haisosfile can set them with `ENV`, and so that a sub-OS
// inherits the configuration along with the rest of the environment it is given.
inline constexpr const char* kEnvEndpoint = "HAISOS_ENDPOINT";
inline constexpr const char* kEnvModel = "HAISOS_MODEL";
inline constexpr const char* kEnvApiKey = "HAISOS_API_KEY";

}
