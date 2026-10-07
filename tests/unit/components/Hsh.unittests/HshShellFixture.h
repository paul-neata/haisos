#pragma once

#include "BuiltinCommandList.h"
#include "BuiltinCommandsFixture.h"

namespace Haisos {

// The base of every hsh test that runs the shell through the OS: the
// BuiltinCommandsTest fixture (an in-memory root with every builtin in /bin)
// plus hsh helpers. Later hsh test files share it.
class HshShellTest : public BuiltinCommandsTest {
protected:
    // hsh -c <script> [args...], stdout and stderr captured byte for byte.
    Captured Sh(const std::string& script, const std::vector<std::string>& args = {},
                const std::optional<std::string>& input = std::nullopt,
                const std::string& workingDirectory = "/") {
        std::vector<std::string> argv = {"-c", script};
        argv.insert(argv.end(), args.begin(), args.end());
        return RunCaptured("hsh", argv, input, workingDirectory);
    }

    // A file of the root filesystem, whole ("" if it is not there).
    std::string ReadRootFile(const std::string& path) {
        std::string content;
        ReadWholeFile(*root, path, content);
        return content;
    }

    // "Parameter <spelling> is not treated by HaisosOS hsh v. <version>\n",
    // with the version of CreateHshCommand(): tests never spell the version.
    static std::string NotTreatedLine(const std::string& spelling) {
        return "Parameter " + spelling + " is not treated by HaisosOS hsh v. " + CreateHshCommand()->Version() + "\n";
    }

    // A script and what running it through `hsh -c` must print and return,
    // byte for byte (the expected values are dash's).
    struct ShellCase {
        std::string script;
        std::string out;
        std::string err;
        int status = 0;
        std::vector<std::string> args;
        std::optional<std::string> input;
        std::string workingDirectory = "/";
    };

    void ExpectSh(const ShellCase& expected, const char* caseName) {
        const Captured captured = Sh(expected.script, expected.args, expected.input, expected.workingDirectory);
        EXPECT_EQ(captured.out, expected.out) << caseName << ": " << expected.script;
        EXPECT_EQ(captured.err, expected.err) << caseName << ": " << expected.script;
        EXPECT_EQ(captured.status, expected.status) << caseName << ": " << expected.script;
    }
};

} // namespace Haisos
