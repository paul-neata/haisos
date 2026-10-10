#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <vector>
#include "commands/diff/DiffEngine.h"
#include "interfaces/IFileSystemService.h"

namespace Haisos {

// Which output format a diff is printed in.
enum class DiffStyle { Normal, Unified, Context, Ed };

struct DiffOutputOptions {
    DiffStyle style = DiffStyle::Normal;
    int64_t context = 3;               // -u/-c/-U/-C
    bool initialTab = false;           // -T
    bool expandTabs = false;           // -t
    bool suppressBlankEmpty = false;   // --suppress-blank-empty
    bool ignoreBlankLines = false;     // -B
    size_t tabSize = 8;
    DiffWhiteSpace whiteSpace = DiffWhiteSpace::None; // for -B's blank test
};

// What a header line shows for one file: its label (--label), or its name
// and modification time.
struct DiffHeaderFile {
    std::string name;                  // as given/joined, before quoting
    std::optional<std::string> label;
    FileDateTime modificationTime;
};

// True when some change survives -B (else the files count as the same).
bool DiffHasRealChanges(const std::vector<DiffChange>& changes, const DiffText& a,
                        const DiffText& b, const DiffOutputOptions& options);

// A file name in a header: in double quotes when it is empty or holds a
// space, '"', '\', a byte below 0x20 or a byte 0x80 or above, with the
// escapes \a \b \t \n \v \f \r \" \\ and every other such byte as 3-digit
// octal; 0x7f and everything else as is. Labels never go through here.
std::string QuoteHeaderName(const std::string& name);

// The whole output for one pair of files, headers included; empty when
// every change is ignorable under -B.
std::string FormatDiff(const std::vector<DiffChange>& script, const DiffText& a, const DiffText& b,
                       const DiffHeaderFile& h0, const DiffHeaderFile& h1, const DiffOutputOptions& options);

} // namespace Haisos