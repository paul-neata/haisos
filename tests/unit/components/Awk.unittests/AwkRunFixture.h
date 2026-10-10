#pragma once
#include <memory>
#include <string>
#include <vector>
#include "BuiltinCommandsFixture.h"
#include "BuiltinCommand.h"
#include "interfaces/IFileDescriptor.h"
#include "interfaces/IHaisosOS.h"
#include "interfaces/IProcess.h"
#include "src/components/Filesystem/FilesystemUtils.h"
#include "src/components/libheaders/ExitCodes.h"

namespace Haisos {

// The awk builtin run end to end: a program over the fixture's files and
// the standard input. Every expectation is gawk --posix 5.2.1's own output.
// Shared by AwkInterpreterTest.cpp and AwkFunctionsTest.cpp.
class AwkRunTest : public BuiltinCommandsTest {
protected:
    void SetUp() override {
        BuiltinCommandsTest::SetUp();
        WriteFile("/abc.txt", "a b c\nd e f\n");
        WriteFile("/data.csv", "x,1,2.5\ny,2,3.25\nz,3,4\n");
        WriteFile("/para.txt", "p1 l1\np1 l2\n\n\n\np2 l1\n\n");
    }

    // Runs /bin/awk with |stdIn| as its standard input and files as its
    // other two streams (RunCaptured's, without its input handling).
    std::shared_ptr<IProcess> StartAwk(const std::vector<std::string>& args,
                                       const std::shared_ptr<IFileDescriptor>& stdIn) {
        StartProcessOptions options;
        options.stdIn = stdIn;
        options.stdOut = streams->OpenFile("/out", kFileOpenWriteCreateTruncate, kFileCreateMode);
        options.stdErr = streams->OpenFile("/err", kFileOpenWriteCreateTruncate, kFileCreateMode);
        return os->StartProcess(os->GetOsEnvironment()->Clone(), "/bin/awk", args, "/", options);
    }
};

} // namespace Haisos