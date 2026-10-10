#pragma once
#include <string>
#include <vector>

#include "commands/awk/AwkValue.h"

namespace Haisos::Awk {

// awk's printf/sprintf: |format| applied to |arguments| (already evaluated,
// in order), as gawk --posix does. |convfmt| converts a number for %s.
// Throws AwkFatal: a length modifier ("`l' is not permitted in POSIX awk
// formats"), or the arguments running out (the message with the format and
// the caret at the place that ran out).
std::string FormatAwkPrintf(const std::string& format, const std::vector<Value>& arguments,
                            const std::string& convfmt);

} // namespace Haisos::Awk