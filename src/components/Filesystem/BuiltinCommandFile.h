#pragma once
#include <string>

namespace Haisos {

// What reading a builtin command's file gives back (see
// IFileSystem::AddBuiltinCommand). A builtin has no bytes of its own on any
// filesystem -- it is compiled into Haisos -- so its file says what it is
// instead.
inline std::string BuiltinCommandFileContent(const std::string& builtinName) {
    return "This is the HaisosOS builtin command " + builtinName +
        ". This file can be read if you have permissions but you cannot write it, "
        "nor delete the FS directory containing it.";
}

} // namespace Haisos
