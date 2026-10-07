#pragma once
#include <memory>
#include <mutex>
#include <string>
#include "interfaces/IFileDescriptor.h"
#include "interfaces/IPhysicalConsole.h"

namespace Haisos {

// The console as processes see it: descriptors (IFileDescriptor), the only way
// the physical console reaches a process. An OS starts every process with
// three of them as its descriptors 0, 1 and 2 by default (see
// StartProcessOptions in interfaces/IHaisosOS.h).
//
// Output and error are plain terminals: bytes written reach the host's stdout
// / stderr exactly as they are, untagged and unbuffered (a prompt shows at
// once). Input returns the lines typed on the console, each one with its "\n",
// then end of file. The empty input is the stdin of a non-interactive process,
// as /dev/null is: a read ends at once.
//
// None of them buffers output or adds anything to it.

// A terminal's standard output: writes go to IPhysicalConsole::Write.
class ConsoleOutputDescriptor : public IFileDescriptor {
public:
    static std::shared_ptr<ConsoleOutputDescriptor> Create(std::shared_ptr<IPhysicalConsole> console);
    ~ConsoleOutputDescriptor() override = default;

    ConsoleOutputDescriptor(const ConsoleOutputDescriptor&) = delete;
    ConsoleOutputDescriptor& operator=(const ConsoleOutputDescriptor&) = delete;

    ssize_t Read(void* buf, size_t count) override;
    ssize_t Write(const void* buf, size_t count) override;
    bool IsTerminal() const override;

private:
    explicit ConsoleOutputDescriptor(std::shared_ptr<IPhysicalConsole> console);

    std::shared_ptr<IPhysicalConsole> m_console;
};

// A terminal's standard error: writes go to IPhysicalConsole::WriteError.
class ConsoleErrorDescriptor : public IFileDescriptor {
public:
    static std::shared_ptr<ConsoleErrorDescriptor> Create(std::shared_ptr<IPhysicalConsole> console);
    ~ConsoleErrorDescriptor() override = default;

    ConsoleErrorDescriptor(const ConsoleErrorDescriptor&) = delete;
    ConsoleErrorDescriptor& operator=(const ConsoleErrorDescriptor&) = delete;

    ssize_t Read(void* buf, size_t count) override;
    ssize_t Write(const void* buf, size_t count) override;
    bool IsTerminal() const override;

private:
    explicit ConsoleErrorDescriptor(std::shared_ptr<IPhysicalConsole> console);

    std::shared_ptr<IPhysicalConsole> m_console;
};

// A terminal's input: every line typed on the console, delivered with its "\n"
// restored, until the console reports end of input. A Read blocks while no
// input is pending and the console is not at end, and it cannot be
// interrupted: a line being read from the host's terminal cannot be abandoned.
class ConsoleInputDescriptor : public IFileDescriptor {
public:
    static std::shared_ptr<ConsoleInputDescriptor> Create(std::shared_ptr<IPhysicalConsole> console);
    ~ConsoleInputDescriptor() override = default;

    ConsoleInputDescriptor(const ConsoleInputDescriptor&) = delete;
    ConsoleInputDescriptor& operator=(const ConsoleInputDescriptor&) = delete;

    ssize_t Read(void* buf, size_t count) override;
    ssize_t Write(const void* buf, size_t count) override;
    bool IsTerminal() const override;

private:
    explicit ConsoleInputDescriptor(std::shared_ptr<IPhysicalConsole> console);

    std::shared_ptr<IPhysicalConsole> m_console;
    std::mutex m_mutex;
    // The rest of a line not yet read out (a line may span several Reads).
    std::string m_pending;
    // Set once the console reports end of input: every later Read returns 0.
    bool m_atEnd = false;
};

// No input at all: reads return 0 at once, as /dev/null does. Not a terminal.
class EmptyInputDescriptor : public IFileDescriptor {
public:
    static std::shared_ptr<EmptyInputDescriptor> Create();
    ~EmptyInputDescriptor() override = default;

    EmptyInputDescriptor(const EmptyInputDescriptor&) = delete;
    EmptyInputDescriptor& operator=(const EmptyInputDescriptor&) = delete;

    ssize_t Read(void* buf, size_t count) override;
    ssize_t Write(const void* buf, size_t count) override;
    bool IsTerminal() const override;

private:
    EmptyInputDescriptor() = default;
};

}
