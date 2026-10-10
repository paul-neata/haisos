#pragma once
#include <string>
#include <string_view>
#include <vector>

namespace Haisos {

// rg's file types: a built-in subset of ripgrep 14's list (rg has 200+),
// each row exactly what its `--type-list` line prints, in the table's order.
// Matched against a file's base name with FnMatch(glob, name, 0).

// One row: the type's name and its globs.
struct RgFileType {
    std::string_view name;
    std::vector<std::string_view> globs;
};

// The whole table, in the order --type-list prints it.
const std::vector<RgFileType>& RgFileTypes();

// The globs of a type, or null when no such type is in the table.
const std::vector<std::string_view>* FindRgTypeGlobs(std::string_view name);

// What --type-list prints: "name: glob, glob, ...\n" per row.
std::string RgTypeListText();

} // namespace Haisos