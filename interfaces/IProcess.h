#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include "IEnvironment.h"
#include "IFileIO.h"
#include "ILLMService.h"

namespace Haisos {

class IHaisosOS;

// What a process looks like from the outside -- to the OS, or to another
// process. Deliberately read-only about the process's own state: nothing here
// changes its environment or its working directory, because those belong to the
// process itself (see ICurrentProcess). What an outsider may do is ask it to
// stop and wait for it to finish.
//
// A process is either agent-backed (.md) or, in the future, backed by other
// runtimes (.lua, wasm, a real OS process acting as a driver, ...). A top-most
// process has a GetParentPid() of 0; process parentage is being redefined along
// with the process/agent split, so for now everything started through
// IHaisosOS::StartProcess is top-most.
class IProcess {
public:
    virtual ~IProcess() = default;

    virtual uint64_t GetPid() const = 0;
    virtual uint64_t GetParentPid() const = 0;

    // The program this process was started from, as it was resolved against the
    // OS's filesystem root.
    virtual std::string Path() const = 0;

    // The name of the agent running this process, or empty for a process whose
    // runtime is not an agent. Outsiders get the name only: the agent itself is
    // reachable from inside the process (see ICurrentProcess::AsAgent).
    virtual std::string StartingAgentName() const = 0;

    // A clone of the environment this process runs with, so reading it can
    // never change what the process sees. It is the process's own, not the
    // OS's (see IHaisosOS::StartProcess).
    virtual std::shared_ptr<IEnvironment> GetEnvironment() const = 0;

    // Asks the process to stop and returns immediately; it is a request, not a
    // guarantee, so pair it with WaitToFinish. Calling it on a process that has
    // already stopped does nothing.
    virtual void TriggerStop() = 0;

    // Waits up to timeoutMs for the process to finish, returning whether it
    // has. A timeout of 0 does not wait at all, so WaitToFinish(0) is how to ask
    // "has it finished?" without blocking.
    virtual bool WaitToFinish(uint64_t timeoutMs) = 0;
};

// What a process looks like from the inside -- the handle a process has on
// itself. It adds what only the process may do for itself: file I/O from where
// it currently is, reaching the agent running it, and reaching the OS it runs
// under.
//
// THIS IS THE ONLY DOOR OUT OF A PROCESS. Everything a running program reaches
// beyond its own memory -- the filesystem, other processes, the services -- it
// reaches through here, via OS() and IO(). That holds for every runtime alike:
// the tools an agent calls, the agent itself, and the globals a Lua script
// gets. Nothing is handed an IHaisosOS directly, and nothing keeps a private
// path to one.
//
// Two things follow, and they are the reason the rule exists:
//   * every runtime has exactly the same reach, so a .lua script can do
//     neither more nor less than a .md agent; and
//   * narrowing what one process may do is a matter of handing it a narrower
//     OS at startup, with no runtime needing to know. A security policy can
//     then be enforced in one place instead of in every tool (that policy is
//     still to come -- see the Security section of the root CLAUDE.md).
class ICurrentProcess : public IProcess {
public:
    // This process's file I/O: the OS's root filesystem plus the working
    // directory this process resolves relative paths against. All file access
    // a running program does goes through here -- the tools an agent calls and
    // the globals a Lua script gets alike -- never to an IFileSystem directly,
    // which would understand only absolute paths. Never null.
    virtual std::shared_ptr<IFileIO> IO() const = 0;

    // The agent running this process, or null for a process whose runtime is
    // not an agent.
    virtual std::shared_ptr<IAgent> AsAgent() = 0;

    // The OS this process is part of, and everything it may reach outside
    // itself beyond its own files (see the note above this class).
    //
    // Not necessarily the OS that started it: it may be a narrowed clone of
    // that one, confined by the root filesystem it was built with. When user
    // support arrives, the first process of a user will be given an OS whose
    // root is writable only under that user's home and a temp directory, and
    // nothing in the process needs to know that happened -- it simply cannot
    // reach any further.
    //
    // Returns null once the OS is gone, which outlives no running process in
    // practice: the reference is weak, because an OS owns its processes and a
    // strong one back would be a cycle neither could escape.
    virtual std::shared_ptr<IHaisosOS> OS() const = 0;
};

}
