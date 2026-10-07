#include "commands/hsh/HshBuiltins.h"

#include <algorithm>
#include <climits>
#include <string>

#include "src/components/Filesystem/FilesystemUtils.h"
#include "commands/hsh/HshParser.h"
#include "commands/hsh/HshShell.h"
#include "interfaces/IFileIO.h"

namespace Haisos::Hsh {

namespace {

// One or more decimal digits fitting an int; |value| gets it. The numeric
// argument syntax of break / continue / return (dash's number()).
bool DecimalArgument(const std::string& text, int& value) {
    if (text.empty()) {
        return false;
    }
    value = 0;
    for (const char c : text) {
        const int digit = c - '0';
        if (c < '0' || c > '9' || value > (INT_MAX - digit) / 10) {
            return false;
        }
        value = value * 10 + digit;
    }
    return true;
}

} // namespace

// break [n] / continue [n], as dash: n defaults to 1, must be digits and at
// least 1 ("break: Illegal number: <n>", fatal -- both are special builtins),
// further arguments ignored. Outside a loop nothing happens (status 0); else
// a LoopControl unwinds to the loops, one loop per level.
static int BuiltinBreakContinue(Shell& shell, const std::vector<std::string>& args, bool isBreak) {
    const std::string name = isBreak ? "break" : "continue";
    int levels = 1;
    if (args.size() > 1) {
        if (!DecimalArgument(args[1], levels) || levels < 1) {
            shell.Fail(name + ": Illegal number: " + args[1]);
        }
    }
    if (shell.LoopDepth() == 0) {
        return 0;
    }
    throw LoopControl{isBreak, std::min(levels, shell.LoopDepth())};
}

int BuiltinBreak(Shell& shell, const std::vector<std::string>& args) {
    return BuiltinBreakContinue(shell, args, true);
}

int BuiltinContinue(Shell& shell, const std::vector<std::string>& args) {
    return BuiltinBreakContinue(shell, args, false);
}

// return [n], as dash: n defaults to $? and is digits only ("return: Illegal
// number: <n>", fatal -- return is a special builtin). Inside a function or a
// dot script it unwinds there, elsewhere it ends the script or -c string.
int BuiltinReturn(Shell& shell, const std::vector<std::string>& args) {
    int status = shell.State().lastExitStatus;
    if (args.size() > 1) {
        int value = 0;
        if (!DecimalArgument(args[1], value)) {
            shell.Fail("return: Illegal number: " + args[1]);
        }
        status = value;
    }
    if (shell.FunctionDepth() > 0 || shell.DotDepth() > 0) {
        throw FunctionReturn{status & 0xFF};
    }
    throw ShellExit{status & 0xFF};
}

// . file [args], as dash's dotcmd: no operand is status 0; a name with a '/'
// is used as given (it must exist), another is searched in PATH; ".: <file>:
// not found" / ".: cannot open <file>: <reason>" are fatal (. is special).
// Further operands are ignored, as dash does not set $1 from them. The text
// runs one complete command at a time in this shell; a syntax error is fatal,
// named after the file ("hsh: 2: ./bad.sh: Syntax error: \"fi\" unexpected");
// a `return` in it ends the script with its status.
int BuiltinDot(Shell& shell, const std::vector<std::string>& args) {
    if (args.size() == 1) {
        return 0;
    }
    const std::string& file = args[1];
    std::string path = file;
    if (file.find('/') != std::string::npos) {
        FileStatus status;
        if (shell.IO().Stat(file, status) != 0) {
            shell.Fail(".: " + file + ": not found");
        }
    } else {
        const Shell::CommandLookup lookup = shell.LookUpCommand(file);
        if (lookup.result != Shell::CommandLookup::Result::Found) {
            shell.Fail(".: " + file + ": not found");
        }
        path = lookup.path;
    }
    std::string text;
    const std::shared_ptr<IFileDescriptor> in = shell.IO().OpenFile(path, kFileOpenReadOnly);
    if (!in || !ReadWholeDescriptor(*in, text)) {
        shell.Fail(".: cannot open " + file + ": " + OpenFailureReason(shell.IO(), path, false));
    }
    ++shell.m_dotDepth;
    int status = 0;
    try {
        Parser parser(text, {1, false, "end of file"});
        for (;;) {
            ParseResult result = parser.ParseNext();
            if (result.status == ParseResult::Status::EndOfInput) {
                break;
            }
            if (result.status == ParseResult::Status::Error) {
                throw ShellError(file + ": " + result.errorMessage, result.errorLine);
            }
            try {
                status = shell.ExecuteList(result.commands);
            } catch (const FunctionReturn& ret) {
                // A return in the script ends it with that status.
                status = ret.status;
                break;
            }
        }
    } catch (...) {
        --shell.m_dotDepth;
        throw;
    }
    --shell.m_dotDepth;
    return status;
}

// eval args..., as dash's evalcmd: the arguments joined with single spaces
// are run as commands, one complete command at a time; nothing to run is
// status 0. A syntax error is fatal ("eval: Syntax error: ...", eval is a
// special builtin). The status is the last command's.
int BuiltinEval(Shell& shell, const std::vector<std::string>& args) {
    std::string text;
    for (size_t i = 1; i < args.size(); ++i) {
        if (i > 1) {
            text += ' ';
        }
        text += args[i];
    }
    if (text.empty()) {
        return 0;
    }
    Parser parser(text, {shell.CurrentLine(), false, "end of file"});
    int status = 0;
    for (;;) {
        ParseResult result = parser.ParseNext();
        if (result.status == ParseResult::Status::EndOfInput) {
            break;
        }
        if (result.status == ParseResult::Status::Error) {
            shell.Fail("eval: " + result.errorMessage);
        }
        status = shell.ExecuteList(result.commands);
    }
    return status;
}

} // namespace Haisos::Hsh
