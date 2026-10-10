#pragma once

#include <string>

#include "commands/jq/JqValue.h"

namespace Haisos::Jq {

// @<name> applied to |value|: a string. "text" is tostring (a string as
// it is, anything else its compact JSON), "json" the compact JSON of
// anything, a string included; any other name is a JqError
// "<name> is not a valid format" (tools--jq-text adds the rest).
Value ApplyFormat(const std::string& name, const Value& value);

} // namespace Haisos::Jq