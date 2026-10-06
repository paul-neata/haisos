#pragma once
#include <optional>
#include <string>

namespace Haisos {

// The real, physical console: the host's stdout and stderr, shared across
// whatever writes to them. Distinct from IAgentConsole, which is the minimal
// per-agent view onto it (or onto nothing at all, for an in-memory-only
// console).
//
// A process never holds an IPhysicalConsole directly: it reaches the host's
// terminal only through console descriptors (ConsoleDescriptors.h in the
// Console component), which its OS hands it as its descriptors 0, 1 and 2.
class IPhysicalConsole {
public:
    virtual ~IPhysicalConsole() = default;
    // Writes exactly these bytes to the host's standard output, in order with
    // every other Write/WriteError, adding nothing: no newline, no label. An
    // empty string writes nothing. A console does not know who is writing to
    // it and labels nothing: a writer that wants to be identifiable says so in
    // the bytes it passes.
    virtual void Write(const std::string& bytes) = 0;

    // As Write, on the host's standard error.
    virtual void WriteError(const std::string& bytes) = 0;

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
