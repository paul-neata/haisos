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
    // Stopped when context.StopRequested() or a read returned kIOInterrupted;
    // Error on any other negative read.
    //
    // |rs| == "" is paragraph mode: the record is a paragraph, its separator
    // a run of two or more newlines (a single newline is content, so a line
    // of blanks still belongs to its record); leading runs are skipped, and
    // at the end of the input the last record's trailing newlines are
    // stripped, with no empty record after them.
    RecordReadResult Next(const std::string& rs, std::string& record);

private:
    RecordReadResult NextParagraph(std::string& record);  // rs == ""
    // One more block appended to the buffer (m_eof set when the input is at
    // its end). False when the read was stopped (|stopped|) or failed.
    bool ReadMoreBlock(bool& stopped);

    BuiltinContext& m_context;
    std::shared_ptr<IFileDescriptor> m_input;
    // What was read past the record last returned: the bytes of the records
    // still to come (this reader is the only reader of its descriptor), with
    // m_start where the next record begins in it. The consumed front is
    // erased only just before a block is read (a record returned never costs
    // an erase of its own, so a block of short lines is not quadratic); the
    // search for the separator starts at m_start and resumes, within one
    // call, after the bytes already searched.
    std::string m_buffer;
    size_t m_start = 0;
    bool m_eof = false;
};

} // namespace Haisos::Awk