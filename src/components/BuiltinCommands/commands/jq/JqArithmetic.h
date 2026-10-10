#pragma once

#include "commands/jq/JqValue.h"

namespace Haisos::Jq {

// jq's arithmetic, as its binary operators apply it (JqInterpreter.cpp
// decides which one); a failure throws a JqError holding jq's message:
// "<kind> (<dump 15>) and <kind> (<dump 15>) cannot be added" -- subtracted,
// multiplied, divided, divided (remainder) --, the zero-divisor forms
// "... cannot be divided[ (remainder)] because the divisor is zero".
// Results are computed numbers: a literal never survives arithmetic.
Value Add(const Value& a, const Value& b);
Value Subtract(const Value& a, const Value& b);
Value Multiply(const Value& a, const Value& b);
Value Divide(const Value& a, const Value& b);
Value Modulo(const Value& a, const Value& b);

} // namespace Haisos::Jq