#pragma once

namespace Haisos {

// A process's exit-code vocabulary, spelled here once (its meanings are on
// IProcess::ExitCode).
constexpr int kExitCodeBrokenPipe = 141;   // 128 + SIGPIPE
constexpr int kExitCodeStopped = 143;      // 128 + SIGTERM: TriggerStop is Haisos's SIGTERM
constexpr int kExitCodeNotStarted = 127;   // what a shell (and haisos) reports for a program it could not start

// How a process's program ended, as far as its exit code is concerned.
enum class ProcessEnd { Exited, Stopped, BrokenPipe };

// The exit code for a program that ended the given way. Exited: programCode's
// low 8 bits, as a shell takes them (exit(-1) is 255, exit(256) is 0);
// Stopped: 143; BrokenPipe: 141 (programCode ignored). BrokenPipe takes
// precedence over Stopped where both apply, since a broken pipe stops the
// process itself.
//
// Each runtime decides its ProcessEnd in one place (named where it is -- e.g.
// LuaProcess::RunThread), with BrokenPipe checked before Stopped: a process
// asked to stop after a broken pipe still reports 141.
inline int ExitCodeFor(ProcessEnd end, int programCode) {
    switch (end) {
        case ProcessEnd::Stopped:    return kExitCodeStopped;
        case ProcessEnd::BrokenPipe: return kExitCodeBrokenPipe;
        case ProcessEnd::Exited:
        default:                     return programCode & 0xFF;
    }
}

} // namespace Haisos
