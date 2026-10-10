#pragma once

#include <functional>

#include "commands/jq/JqValue.h"

namespace Haisos::Jq {

// .[key], the indexing rules of jq 1.7.1: a null target gives null (a
// string or number key, an object in slice form) or fails with "Cannot
// index null with <kind>"; an object takes strings; an array takes numbers
// (floored, negative from the end, out of range null), arrays (the indices
// where the sub-array starts) and slice-form objects; a string key on
// anything else names it while shorter than 30 bytes; every other pair
// fails with "Cannot index <kind> with <keykind>". Throws a JqError.
Value IndexValue(const Value& target, const Value& key);

// .[from:to] on arrays and strings (strings by code points), null bounds
// meaning the start/end, negatives from the end, clamped, from floored and
// to ceiled; a null target is null whatever the bounds; slicing an object
// fails "Cannot index object with object", anything else "Cannot index
// <kind> with object"; a bound that is neither number nor null fails
// "Array/string slice indices must be integers". Throws a JqError.
Value SliceValue(const Value& target, const Value& from, const Value& to);

// .[]: an array's elements, an object's values in order; anything else
// fails "Cannot iterate over <kind> (<dump 15>)". Throws a JqError.
void IterateValue(const Value& target,
                   const std::function<void(const Value&)>& each);

} // namespace Haisos::Jq