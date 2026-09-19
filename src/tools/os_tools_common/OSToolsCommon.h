#pragma once
#include <memory>
#include <string>
#include "interfaces/IHaisosOS.h"
#include "interfaces/ILLMService.h"
#include "src/components/Filesystem/VirtualPath.h"
#include "src/components/libheaders/CurrentProcessHandle.h"

namespace Haisos::Tools {

// What every os_* tool needs before it can do anything: the process that called
// it, and the OS that process is part of. Both come from ICurrentProcess, which
// is the only door out of a process -- see the Security section of the root
// CLAUDE.md. A tool never holds an IHaisosOS of its own, so whatever OS the
// calling process was given is exactly what the tool can reach, and no more.
struct OSToolContext {
    std::shared_ptr<ICurrentProcess> process;
    std::shared_ptr<IHaisosOS> os;

    bool IsValid() const { return process != nullptr && os != nullptr; }

    // Resolves a path the way the calling process means it: against its own
    // working directory. A filesystem has no current directory of its own (see
    // IFileSystem), so this is the only place the two are brought together.
    std::string ResolvePath(const std::string& path) const {
        return NormalizeVirtualPath(path, process->GetCurrentDirectory());
    }
};

// Returns an empty context when there is no process to act for, or when the OS
// it belonged to is already gone. Both mean the tool must refuse rather than
// fall back to some wider OS.
inline OSToolContext GetOSToolContext(const std::shared_ptr<CurrentProcessHandle>& handle) {
    OSToolContext context;
    if (!handle) {
        return context;
    }
    context.process = handle->Get();
    if (!context.process) {
        return context;
    }
    context.os = context.process->GetHaisosOS();
    return context;
}

inline ToolResult NoCurrentProcessError(const std::string& toolName) {
    return ToolResult{toolName + ": no current process to act for", true};
}

} // namespace Haisos::Tools
