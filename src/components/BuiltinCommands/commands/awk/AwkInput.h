#pragma once
#include <memory>
#include <string>
#include "BuiltinCommand.h"
#include "interfaces/IFileDescriptor.h"

namespace Haisos::Awk {

enum class RecordReadResult { Record, End, Error, Stopped };

// Reads records from one input (a file, standard input; later getline's
// files and commands), in 64 KiB blocks, never past what it needs.
class RecordReader {
public:
    RecordReader(BuiltinContext& context, std::shared_ptr<IFileDescriptor> input);
    // The next record, separated by the first byte of |rs| (RS as it is at
    // this call: a change applies from the next record on; gawk --posix
    // uses only RS's first byte). The separator is not part of the record;
    // a last record without one still counts; an input ending right after a
    // separator has no empty record after it ("a\n\n" gives "a" and "").
    // |rs| == "" (paragraph mode) is awk--records' (until then the caller
    // never passes it). Stopped when context.StopRequested() or a read
    // returned kIOInterrupted; Error on any other negative read.
    RecordReadResult Next(const std::string& rs, std::string& record);

private:
    BuiltinContext& m_context;
    std::shared_ptr<IFileDescriptor> m_input;
    // What was read past the record last returned: the bytes of the records
    // still to come (this reader is the only reader of its descriptor).
    std::string m_buffer;
    bool m_eof = false;
};

} // namespace Haisos::Awk