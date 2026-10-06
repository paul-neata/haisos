#pragma once
#include <gtest/gtest.h>
#include <sys/stat.h>
#include <algorithm>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <regex>
#include <string>
#include <vector>
#include "Factory.h"
#include "BuiltinCommandList.h"
#include "src/components/Filesystem/FilesystemUtils.h"

namespace Haisos {

constexpr uint64_t kWaitMs = 10000;

// A physical console that keeps what is written to it: the raw bytes of each
// stream separately (m_out, m_err) and of both in the order written (m_all).
class CapturingConsole : public IPhysicalConsole {
public:
    void Write(const std::string& bytes) override {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_out += bytes;
        m_all += bytes;
    }
    void WriteError(const std::string& bytes) override {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_err += bytes;
        m_all += bytes;
    }
    std::optional<std::string> ReadLine() override { return std::nullopt; }
    void Start() override {}
    void Stop() override {}

    // Everything written so far, split on '\n': no empty last element for a
    // trailing newline, a trailing partial line kept. Clears all three strings.
    std::vector<std::string> TakeLines() {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::vector<std::string> lines;
        std::string line;
        for (char c : m_all) {
            if (c == '\n') {
                lines.push_back(line);
                line.clear();
            } else {
                line += c;
            }
        }
        if (!line.empty()) {
            lines.push_back(line);
        }
        m_out.clear();
        m_err.clear();
        m_all.clear();
        return lines;
    }

    // The raw string its own stream got, taken (only that stream is cleared, so
    // TakeOut() and TakeErr() may be asked in either order).
    std::string TakeOut() {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::string result;
        result.swap(m_out);
        return result;
    }
    std::string TakeErr() {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::string result;
        result.swap(m_err);
        return result;
    }

private:
    std::mutex m_mutex;
    std::string m_out;
    std::string m_err;
    std::string m_all;
};

#ifdef _WIN32
constexpr int kDirMode = _S_IREAD | _S_IWRITE;
#else
constexpr int kDirMode = S_IRWXU;
#endif

using Lines = std::vector<std::string>;

// ls -l lines with their time column ("Sep 25 13:22" or "Sep 25  2025")
// replaced by "<time>", for comparing what does not depend on the clock.
inline Lines WithoutTimes(Lines lines) {
    static const std::regex kTime("[A-Z][a-z]{2} [ 123][0-9] ([0-9]{2}:[0-9]{2}| [0-9]{4}) ");
    for (auto& line : lines) {
        line = std::regex_replace(line, kTime, "<time> ", std::regex_constants::format_first_only);
    }
    return lines;
}

// A whole standard output (or error) captured byte for byte, split on '\n':
// no empty last element for a trailing newline -- TakeLines' shape.
inline Lines SplitLines(const std::string& text) {
    Lines lines;
    std::string line;
    for (char c : text) {
        if (c == '\n') {
            lines.push_back(line);
            line.clear();
        } else {
            line += c;
        }
    }
    if (!line.empty()) {
        lines.push_back(line);
    }
    return lines;
}

// An OS on an in-memory root with every builtin in /bin, the capturing
// console above, and the fixture's usual test files. Run() drives a builtin
// with the console as its standard streams; RunCaptured() connects them to
// in-memory files instead (not terminals), giving stdout and stderr back byte
// for byte.
class BuiltinCommandsTest : public ::testing::Test {
protected:
    void SetUp() override {
        root = factory->CreateServicesCreator()->CreateFileSystemService()->CreateEmptyInMemFileSystem();
        // The files standing in for standard streams in RunCaptured live on a
        // filesystem of their own, mounted nowhere, so they never show in a
        // listing.
        streams = factory->CreateServicesCreator()->CreateFileSystemService()->CreateEmptyInMemFileSystem();
        ASSERT_EQ(root->CreateDirectory("/bin", kDirMode), 0);
        auto configurator = factory->CreateBuiltinConfigurator();
        for (const auto& name : builtins->GetCommands()) {
            std::string error;
            ASSERT_TRUE(configurator->AddBuiltinCommand(root, "/bin/" + name, name, &error)) << error;
        }
        WriteFile("/notes.txt", "one\ntwo\n\n\n\tthree\n");
        WriteFile("/.hidden", "h");
        ASSERT_EQ(root->CreateDirectory("/docs", kDirMode), 0);
        WriteFile("/docs/a.md", "alpha");
        ASSERT_EQ(root->CreateDirectory("/docs/sub", kDirMode), 0);
        WriteFile("/docs/sub/b.md", "bravo!");

        auto environment = factory->CreateEnvironment();
        os = factory->CreateHaisosOS(factory->CreateServicesCreator(), console, root, builtins, environment);
        ASSERT_NE(os, nullptr);
    }

    void WriteFile(const std::string& path, const std::string& content) {
        auto file = root->OpenFile(path, kFileOpenWriteCreateTruncate, kFileCreateMode);
        ASSERT_NE(file, nullptr) << path;
        file->Write(content.data(), content.size());
    }

    // Places empty files cat, echo, ls, mkdir, pwd in /five: a directory whose
    // listing does not change as builtins are added to /bin.
    void MakeFiveNames() {
        ASSERT_EQ(root->CreateDirectory("/five", kDirMode), 0);
        for (const char* name : {"cat", "echo", "ls", "mkdir", "pwd"}) {
            WriteFile(std::string("/five/") + name, "");
        }
    }

    // Runs /bin/<command> with args from workingDirectory, waits for it, and
    // returns what it printed; its exit code goes to *status.
    std::vector<std::string> Run(const std::string& command, const std::vector<std::string>& args,
                                 int* status = nullptr, const std::string& workingDirectory = "/") {
        auto process = os->StartProcess(os->GetOsEnvironment()->Clone(), "/bin/" + command, args, workingDirectory, StartProcessOptions{});
        EXPECT_NE(process, nullptr) << command;
        if (!process) {
            return {};
        }
        EXPECT_TRUE(process->WaitToFinish(kWaitMs)) << command;
        if (status) {
            // The exit code is set before the process reports finished, so
            // after the wait it is always there.
            EXPECT_TRUE(process->ExitCode().has_value()) << command;
            *status = process->ExitCode().value_or(-1);
        }
        return console->TakeLines();
    }

    struct Captured {
        std::string out;
        std::string err;
        int status = -1;
    };

    // Runs /bin/<command> with stdout and stderr connected to files (not
    // terminals) and, if |input| is given, stdin reading it (else the default
    // empty input). Returns both streams byte for byte and the exit status.
    Captured RunCaptured(const std::string& command, const std::vector<std::string>& args,
                         const std::optional<std::string>& input = std::nullopt,
                         const std::string& workingDirectory = "/") {
        Captured captured;
        StartProcessOptions options;
        if (input) {
            auto inFile = streams->OpenFile("/in", kFileOpenWriteCreateTruncate, kFileCreateMode);
            EXPECT_NE(inFile, nullptr);
            if (inFile) {
                inFile->Write(input->data(), input->size());
            }
            inFile.reset();
            options.stdIn = streams->OpenFile("/in", kFileOpenReadOnly);
        }
        options.stdOut = streams->OpenFile("/out", kFileOpenWriteCreateTruncate, kFileCreateMode);
        options.stdErr = streams->OpenFile("/err", kFileOpenWriteCreateTruncate, kFileCreateMode);
        EXPECT_NE(options.stdOut, nullptr);
        EXPECT_NE(options.stdErr, nullptr);
        auto process = os->StartProcess(os->GetOsEnvironment()->Clone(), "/bin/" + command, args, workingDirectory, options);
        EXPECT_NE(process, nullptr) << command;
        if (process) {
            EXPECT_TRUE(process->WaitToFinish(kWaitMs)) << command;
            captured.status = process->ExitCode().value_or(-1);
        }
        options.stdIn.reset();
        options.stdOut.reset();
        options.stdErr.reset();
        ReadWholeFile(*streams, "/out", captured.out);
        ReadWholeFile(*streams, "/err", captured.err);
        return captured;
    }

    static bool Contains(const std::vector<std::string>& lines, const std::string& text) {
        return std::any_of(lines.begin(), lines.end(),
            [&text](const std::string& line) { return line.find(text) != std::string::npos; });
    }

    std::shared_ptr<IFactory> factory = CreateFactory();
    std::shared_ptr<IBuiltinCommands> builtins = factory->CreateBuiltinCommands();
    std::shared_ptr<CapturingConsole> console = std::make_shared<CapturingConsole>();
    std::shared_ptr<IFileSystem> root;
    std::shared_ptr<IFileSystem> streams;
    std::shared_ptr<IHaisosOS> os;
};

} // namespace Haisos
