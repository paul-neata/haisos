#pragma once
#include <string>
#include <vector>
#include "BuiltinCommand.h"
#include "SedScript.h"

namespace Haisos::Sed {

// One piece of the script: one -e, one -f or the script operand, in the
// order given (the operand script is added last, as GNU does).
struct ScriptPiece {
    std::string text;
    bool fromFile = false;
    std::string fileName;
};

// Parses every piece in order into |script|. On an error writes GNU's
// message with context.Error ("sed: -e expression #N, char M: ..." or
// "sed: file F line L: ...") and returns false (exit 1).
bool ParseScript(BuiltinContext& context, const std::vector<ScriptPiece>& pieces,
                 bool extendedRegex, Script& script);

} // namespace Haisos::Sed