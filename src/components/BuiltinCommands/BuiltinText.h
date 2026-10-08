#pragma once
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include "BuiltinCommand.h"
#include "interfaces/IFileIO.h"
#include "src/components/Filesystem/FilesystemUtils.h"

namespace Haisos {

// GNU's quote() in the C locale (locale_quoting_style): the text in '...',
// with \ written \\, ' written \', \a \b \f \n \r \t \v as those escapes, and
// every other byte below 0x20, 0x7F and every byte >= 0x80 as a 3-digit octal
// escape (\001, \303\251). Used in "invalid argument 'x' for '--sort'",
// "extra operand 'x'", "multi-character tab 'ab'", tr's and cut's messages.
std::string GnuQuote(std::string_view text);

// One name an ArgMatch choice can be written as; synonyms share a value.
struct ArgChoice {
    std::string name;   // "quiet"
    int value;          // what it means; synonyms share a value
};

// GNU's XARGMATCH: |value| matched against the names exactly, else as a prefix
// of them -- unambiguous when every name it prefixes has the same value.
// Returns that value; otherwise prints, and returns nullopt:
//   <cmd>: invalid argument 'foo' for '--sort'      (or "ambiguous argument")
//   Valid arguments are:
//     - 'general-numeric'
//     - 'quiet', 'silent'          <- names sharing a value, on one line, in table order
//   Try '<cmd> --help' for more information.
// (all quoting with GnuQuote). The caller then returns 1 -- GNU's exit status
// for an argmatch failure, whatever the command's usual error status.
std::optional<int> ArgMatch(BuiltinContext& context, const std::string& longOption /* "--sort" */,
                            const std::string& value, const std::vector<ArgChoice>& choices);

// How opening an input operand went: each command words the failure itself.
enum class InputOpenFailure { None, Missing, Directory, Denied, BadDescriptor };

// An input operand as a text filter takes it: "-" is descriptor 0 (null slot ->
// BadDescriptor); a name is Stat-ed first (absent -> Missing, a directory ->
// Directory -- GNU opens it and fails on read, so callers word that as their
// read error), then opened read-only (null -> Denied).
std::shared_ptr<IFileDescriptor> OpenInputOperand(BuiltinContext& context, const std::string& name,
                                                  InputOpenFailure& failure);

enum class LineReadResult { Line, End, Error, Stopped };

// Lines split on |delimiter| ('\n', or '\0' for -z), byte for byte: nothing
// stripped (a '\r' stays). Reads 64 KiB at a time. Next() gives the line
// without its delimiter; |delimited| is false only for a last line the input
// ended without one. Stopped when context.StopRequested() or a read returned
// kIOInterrupted; Error on any other negative read (the caller reports it).
//
// A plain class (it implements no interface), held by value on the stack.
class BuiltinLineReader {
public:
    BuiltinLineReader(BuiltinContext& context, IFileDescriptor& input, char delimiter);

    LineReadResult Next(std::string& line, bool& delimited);

private:
    LineReadResult Fill();

    BuiltinContext& m_context;
    IFileDescriptor& m_input;
    char m_delimiter;
    std::string m_buffer;
    size_t m_pos = 0;   // where the unconsumed bytes of m_buffer start
    bool m_atEnd = false;
};

// Writes all of |bytes| to |out|, looping over partial writes. Returns
// bytes.size(), or the first negative result (kIOError, kIOBrokenPipe, ...).
// For outputs that are not stdout (sort -o, uniq OUTPUT, tee's files);
// stdout goes through context.Out.
ssize_t WriteFully(IFileDescriptor& out, std::string_view bytes);

} // namespace Haisos