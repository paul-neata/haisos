#pragma once
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include "BuiltinCommand.h"
#include "interfaces/IFileDescriptor.h"

namespace Haisos {

class IEnvironment;

// dash's default PATH, used when the environment has none -- what hsh starts
// its own command lookup with, shared here so the two never differ.
inline constexpr const char* kBuiltinDefaultSearchPath =
    "/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin";

// PATH split at ':', every entry kept, empty ones as "" (the working
// directory). PATH is |environment|'s when given, else the caller's own
// (context.Process().GetEnvironment()); kBuiltinDefaultSearchPath's entries
// when that has no PATH.
std::vector<std::string> SearchPathEntries(BuiltinContext& context,
                                           const IEnvironment* environment = nullptr);

// Where |name| runs from, as an absolute path for StartProcess, or nullopt.
// A name holding '/' is taken as is (resolved by context.IO()) if something
// that is not a directory is there. Otherwise each SearchPathEntries entry in
// order: "<entry>/<name>" (no doubled '/'; an empty entry gives "<name>", the
// working directory), the first existing non-directory wins -- a builtin, a
// .md agent, a .lua script, any file (StartProcess decides what it can run).
// An empty name finds nothing. hsh's rules exactly.
std::optional<std::string> FindProgramInPath(BuiltinContext& context, const std::string& name,
                                             const IEnvironment* environment = nullptr);

struct RunProgramOptions {
    // The child's descriptors 0, 1 and 2. Null: the caller's own slot 0/1/2;
    // an empty slot goes as Hsh::ClosedDescriptor, never as null.
    std::shared_ptr<IFileDescriptor> stdIn;
    std::shared_ptr<IFileDescriptor> stdOut;
    std::shared_ptr<IFileDescriptor> stdErr;
    // Resolved by context.IO(); nullopt: the caller's working directory.
    std::optional<std::string> workingDirectory;
    // The child's environment. Null: a clone of the caller's own
    // (context.Process().GetEnvironment(), which is already a clone).
    std::shared_ptr<IEnvironment> environment;
};

// Starts |programPath| (absolute, e.g. from FindProgramInPath) with |args|
// (argv[1] onwards) through context.Process().OS()->StartProcess -- never
// another way -- and waits for it. Returns its exit code; 127 if it could not
// be started (no OS, or StartProcess returned null), with *started set to
// false (true otherwise) when |started| is given. When the caller is asked to
// stop while waiting, the child is stopped (TriggerStop once, then up to
// 5000 ms more) and 143 is returned if it has not finished.
//
// The caller's buffered stdout is flushed before the child starts, so output
// the caller has written reaches its descriptor before the child writes to
// the same one.
int RunProgramAndWait(BuiltinContext& context, const std::string& programPath,
                      const std::vector<std::string>& args,
                      const RunProgramOptions& options = {}, bool* started = nullptr);

} // namespace Haisos