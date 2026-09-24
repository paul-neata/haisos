#pragma once
#include <memory>
#include <string>
#include "interfaces/IFileIO.h"
#include "interfaces/IHaisosOS.h"
#include "interfaces/ILLMService.h"
#include "src/components/libheaders/CurrentProcessHandle.h"

namespace Haisos::Tools {

// What every os_* tool needs before it can do anything: the process that called
// it, its file I/O, and the OS it is part of. All three come from
// ICurrentProcess, which is the only door out of a process -- see the Security
// section of the root CLAUDE.md. A tool never holds an IHaisosOS or an
// IFileSystem of its own, so whatever the calling process was given is exactly
// what the tool can reach, and no more.
//
// File access goes through io, never through os->GetRootFileSystem(): only
// IFileIO knows where the process currently is, so only it can make sense of
// "notes.txt" or "../shared/notes.txt". A filesystem understands absolute
// paths alone.
struct OSToolContext {
    std::shared_ptr<ICurrentProcess> process;
    std::shared_ptr<IFileIO> io;
    std::shared_ptr<IHaisosOS> os;

    bool IsValid() const { return process != nullptr && io != nullptr && os != nullptr; }
};

// Returns an invalid context when there is no process to act for, or when the
// OS it belonged to is already gone. Both mean the tool must refuse rather than
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
    context.io = context.process->IO();
    context.os = context.process->OS();
    return context;
}

inline ToolResult NoCurrentProcessError(const std::string& toolName) {
    return ToolResult{toolName + ": no current process to act for", true};
}

} // namespace Haisos::Tools
