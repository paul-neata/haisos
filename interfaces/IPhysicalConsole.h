#pragma once
#include <optional>
#include <string>

namespace Haisos {

// The real, physical console (stdout, shared across whatever writes to it).
// Distinct from IAgentConsole, which is the minimal per-agent view onto it (or
// onto nothing at all, for an in-memory-only console).
class IPhysicalConsole {
public:
    virtual ~IPhysicalConsole() = default;
    // The message is written as given. A console does not know who is writing
    // to it and does not label anything: a writer that wants to be identifiable
    // says so in the text it passes (see AgentConsoleAdapter).
    virtual void Write(const std::string& message) = 0;

    // Blocks until a whole line has been typed and returns it, without its
    // line ending. Returns nullopt once there is no more input to read (end of
    // input, or input that cannot be read at all): nothing will ever arrive,
    // so a caller waiting on the user should give up rather than ask again.
    // Reading is independent of Start/Stop, which only drive output.
    virtual std::optional<std::string> ReadLine() = 0;

    virtual void Start() = 0;
    virtual void Stop() = 0;
};

}
