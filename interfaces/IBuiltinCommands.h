#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include "IEnvironment.h"
#include "IFileSystemService.h"
#include "IHaisosOS.h"
#include "ILLMService.h"
#include "IProcess.h"

namespace Haisos {

// What only the OS starting a builtin knows about the process it is starting,
// handed to IBuiltinCommands::RunCommand alongside what IHaisosOS::StartProcess
// was given. A builtin, like every program, reaches the world only through its
// ICurrentProcess (see the Security section of the root CLAUDE.md); this is
// what that process is built from.
struct BuiltinCommandHost {
    // The OS the process runs under, and the one its IO() and OS() reach.
    // Weak, because an OS owns its processes.
    std::weak_ptr<IHaisosOS> os;
    uint64_t pid = 0;
    uint64_t parentPid = 0;
    // The path the command was started from (what IProcess::Path() reports),
    // as opposed to the builtin's own name.
    std::string programPath;
    // Where the command's output goes: there is no stdout yet, so each line a
    // builtin prints is one Write here.
    std::shared_ptr<IAgentConsole> console;
};

// The commands compiled into Haisos itself -- echo, cat, ls, pwd, mkdir -- as
// opposed to the programs (.md agents, .lua scripts) that live as files on a
// filesystem. A builtin is placed on a filesystem at a path (see
// IBuiltinConfigurator); IHaisosOS::StartProcess notices a path its root
// filesystem says is a builtin and runs it from here instead of loading a file.
//
// Each command takes the same arguments as the real command of that name, as
// far as Haisos can honour them: features that need what an OS here does not
// have yet (stdin, stdout, stderr, permissions, file times) are left out, and
// a command's --help says what it supports.
class IBuiltinCommands {
public:
    virtual ~IBuiltinCommands() = default;

    // Every builtin's name, sorted.
    virtual std::vector<std::string> GetCommands() const = 0;

    // The version of one builtin (what its --version prints), or an empty
    // string if there is no builtin of that name.
    virtual std::string GetBuiltinVersion(const std::string& builtinName) const = 0;

    // Starts builtinName as a process and returns straight away: the command
    // runs on a thread of its own. The parameters after host are those of
    // IHaisosOS::StartProcess, with the builtin's name in place of a program
    // path; options.interactiveAgent is meaningless for a builtin and ignored.
    // Returns null for an unknown builtin, a null environment, or a host with
    // no OS.
    virtual std::shared_ptr<IProcess> RunCommand(
        const BuiltinCommandHost& host,
        std::shared_ptr<IEnvironment> environment,
        const std::string& builtinName,
        const std::vector<std::string>& args,
        const std::string& workingDirectory,
        const StartProcessOptions& options) = 0;
};

// Places builtin commands on filesystems, and takes them off again. It is the
// front door onto IFileSystem::AddBuiltinCommand/RemoveBuiltinCommand, adding
// the rules a placement must follow:
//   * the directory the builtin goes in must already exist, and
//   * nothing -- no file, directory or other builtin -- may be at the path.
// Once placed, the filesystem itself keeps the builtin's file unwritable and
// its directory undeletable for as long as the builtin is there.
//
// Like mounting, this is how an OS is assembled, so nothing running inside an
// OS is handed one.
class IBuiltinConfigurator {
public:
    virtual ~IBuiltinConfigurator() = default;

    // Places builtinName at builtinPath (absolute) on filesystem. Returns false
    // if a rule above is broken or the filesystem refuses; outError, when
    // given, then says why. builtinName is not checked against any
    // IBuiltinCommands: a path naming a builtin that does not exist simply
    // fails to start.
    virtual bool AddBuiltinCommand(
        std::shared_ptr<IFileSystem> filesystem,
        const std::string& builtinPath,
        const std::string& builtinName,
        std::string* outError = nullptr) = 0;

    // Removes the builtin placed at builtinPath on filesystem itself. Returns
    // false if that filesystem placed none there.
    virtual bool RemoveBuiltin(std::shared_ptr<IFileSystem> filesystem, const std::string& builtinPath) = 0;
};

}
