#include "commands/hsh/HshBuiltins.h"

#include <string>

#include "BuiltinTestExpression.h"
#include "commands/hsh/HshShell.h"

namespace Haisos::Hsh {

// test [expression] / [ expression ]: dash's testcmd. "[" requires its last
// argument to begin with ']' ("[: missing ]" when not). Errors are reported
// with status 2; both are regular builtins, so the shell goes on.
int BuiltinTest(Shell& shell, const std::vector<std::string>& args) {
    const std::string& command = args[0];
    size_t end = args.size();
    if (command == "[") {
        if (end == 1 || args[end - 1][0] != ']') {
            shell.Report("[: missing ]");
            return 2;
        }
        --end;
    }
    return EvaluateTestExpression(TestDialect::Dash, command, args, 1, end,
        shell.IO(), [&shell, &command](const std::string& message) {
            shell.Report(command + ": " + message);
        });
}

} // namespace Haisos::Hsh