#pragma once

#include <string>
#include <vector>

#include "IFileSystemService.h"

namespace Haisos::Hsh {

// Where pathname expansion reads directories from. The executor implements it
// with the running process's IFileIO (ReadDirectory, Stat) -- so globbing goes
// through ICurrentProcess like every other file access, never around it.
class IPathnameSource {
public:
    virtual ~IPathnameSource() = default;
    // The entries of the directory at |path| ("." and ".." included, as
    // IFileIO::ReadDirectory lists them); a relative path is relative to the
    // working directory; empty when it is not a directory.
    virtual std::vector<DirectoryEntry> ReadDirectory(const std::string& path) = 0;
    // Whether anything is at |path| (IFileIO::Stat succeeds).
    virtual bool Exists(const std::string& path) = 0;
};

// Every pathname |pattern| matches, sorted byte by byte (std::string's
// operator<, as dash sorts with strcmp: "B" before "a"); empty when nothing
// matches -- the caller then keeps the word as it was. |pattern| is in
// MatchPattern's syntax; an unescaped '/' separates components. Names that
// start with '.' match only a component starting with a literal '.', so ".*"
// (only) also finds "." and "..". Called only for words with pattern
// characters (the caller checks HasPatternCharacters).
std::vector<std::string> ExpandPathname(const std::string& pattern, IPathnameSource& source);

} // namespace Haisos::Hsh
