#pragma once
#include <string>
#include "BuiltinCommand.h"
#include "BuiltinPrompt.h"

namespace Haisos {

struct RemoveOptions {
    bool recursive = false;        // -r/-R
    bool emptyDirectories = false; // -d
    bool ignoreMissing = false;    // -f: a missing operand is no error and no message
    bool interactive = false;      // -i: ask before each removal (needs a prompt)
    bool verbose = false;          // -v: "removed 'x'", "removed directory 'x'" on stdout
    bool preserveRoot = true;      // refuse "/" with -r
};

// Removes one operand as GNU rm does, |path| as the user wrote it (relative
// paths go through context.IO(), messages show |path| and the paths built
// from it). |prompt| may be null when options.interactive is false. Messages
// start with context.Name() ("rm: ..." in rm, "mv: ..." when mv calls it).
// Returns false if anything failed (the exit status becomes 1); a declined
// prompt is not a failure.
bool RemoveOperand(BuiltinContext& context, BuiltinPrompt* prompt,
                   const std::string& path, const RemoveOptions& options);

} // namespace Haisos