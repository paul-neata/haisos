#pragma once
#include <memory>
#include <mutex>
#include "interfaces/IProcess.h"

namespace Haisos {

// Somewhere to put the process a tool set belongs to, created before the
// process itself exists.
//
// It is here to break a knot: a process's tools have to reach the process that
// called them (ICurrentProcess is the only door out of a process -- see the
// Security section of the root CLAUDE.md), but an agent needs its tools before
// the process wrapping that agent can be built. So the handle is created first,
// handed to the tool factory, and filled in by whoever builds the process --
// always before the process goes live, so no tool can ever observe it empty.
//
// The reference is weak, because the process owns the tool set that holds this.
class CurrentProcessHandle {
public:
    static std::shared_ptr<CurrentProcessHandle> Create() {
        return std::shared_ptr<CurrentProcessHandle>(new CurrentProcessHandle());
    }

    // The process, or null once it is gone (or before it has been set, which
    // callers should never see -- see above).
    std::shared_ptr<ICurrentProcess> Get() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_process.lock();
    }

    // Called once, by whoever builds the process, before it goes live.
    void Set(std::weak_ptr<ICurrentProcess> process) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_process = std::move(process);
    }

private:
    CurrentProcessHandle() = default;

    mutable std::mutex m_mutex;
    std::weak_ptr<ICurrentProcess> m_process;
};

}
