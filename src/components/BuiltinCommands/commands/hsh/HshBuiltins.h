#pragma once

#include <string>
#include <vector>

namespace Haisos::Hsh {

class Shell;

// A builtin of the shell itself, run inside the shell. args[0] is its name.
using ShellBuiltinFunction = int (*)(Shell& shell, const std::vector<std::string>& args);

struct ShellBuiltin {
    const char* name;
    // POSIX special builtin: its errors are fatal (Shell::Fail) and the
    // prefix assignments of its command stay. Regular: errors are reported
    // (Shell::Report) with status 2, prefix assignments are temporary.
    bool special;
    ShellBuiltinFunction run;
};

// The shell builtins of past tasks; each later one lives in a file of its own.
int BuiltinColon(Shell& shell, const std::vector<std::string>& args);
int BuiltinExec(Shell& shell, const std::vector<std::string>& args);
int BuiltinExit(Shell& shell, const std::vector<std::string>& args);
int BuiltinFalse(Shell& shell, const std::vector<std::string>& args);
int BuiltinTrue(Shell& shell, const std::vector<std::string>& args);
int BuiltinWait(Shell& shell, const std::vector<std::string>& args);       // HshBuiltinWait.cpp
int BuiltinCd(Shell& shell, const std::vector<std::string>& args);         // HshBuiltinCd.cpp
int BuiltinExport(Shell& shell, const std::vector<std::string>& args);     // HshBuiltinVariables.cpp
int BuiltinReadonly(Shell& shell, const std::vector<std::string>& args);  // HshBuiltinVariables.cpp
int BuiltinUnset(Shell& shell, const std::vector<std::string>& args);      // HshBuiltinVariables.cpp
int BuiltinShift(Shell& shell, const std::vector<std::string>& args);      // HshBuiltinVariables.cpp
int BuiltinSet(Shell& shell, const std::vector<std::string>& args);        // HshBuiltinSet.cpp
int BuiltinTest(Shell& shell, const std::vector<std::string>& args);       // HshBuiltinTest.cpp ("test" and "[")

// The version of the hsh builtin command (Hsh.cpp), for `set`'s not-treated
// reports inside the shell, where BuiltinContext's version is not at hand.
const char* HshVersion();

// Every builtin hsh has, sorted by name (byte order). Later tasks add rows.
const std::vector<ShellBuiltin>& ShellBuiltins();
const ShellBuiltin* FindShellBuiltin(const std::string& name);  // null if none

} // namespace Haisos::Hsh
