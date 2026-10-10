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
// "sed: file F line L: ...") and returns false; the status is then
// script.parseErrorStatus (1 for a bad script, 4 for a w-file that could
// not be opened or a label a jump cannot find). |sandbox| refuses the
// e/r/R/w/W commands and s///w, as GNU's --sandbox does.
bool ParseScript(BuiltinContext& context, const std::vector<ScriptPiece>& pieces,
                 bool extendedRegex, bool sandbox, Script& script);

} // namespace Haisos::Sed