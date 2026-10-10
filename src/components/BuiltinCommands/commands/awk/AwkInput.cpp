#include "commands/awk/AwkInput.h"

namespace Haisos::Awk {

namespace {
// One block of the input read at a time.
constexpr size_t kReadBlockSize = 64 * 1024;
} // namespace

RecordReader::RecordReader(BuiltinContext& context, std::shared_ptr<IFileDescriptor> input)
    : m_context(context)
    , m_input(std::move(input)) {
}

RecordReadResult RecordReader::Next(const std::string& rs, std::string& record) {
    if (rs.empty()) {
        return NextParagraph(record);
    }
    // RS's first byte is the separator; gawk --posix reads no further byte
    // of it.
    const char separator = rs.front();
    size_t scan = m_start;   // where the search resumes within this call
    while (true) {
        const size_t separatorPos = m_buffer.find(separator, scan);
        if (separatorPos != std::string::npos) {
            record.assign(m_buffer, m_start, separatorPos - m_start);
            m_start = separatorPos + 1;
            return RecordReadResult::Record;
        }
        scan = m_buffer.size();
        if (m_eof) {
            // The end of the input: the bytes still buffered are the last
            // record (it had no separator), or there is none at all.
            if (m_start >= m_buffer.size()) {
                m_buffer.clear();
                m_start = 0;
                return RecordReadResult::End;
            }
            record.assign(m_buffer, m_start, std::string::npos);
            m_buffer.clear();
            m_start = 0;
            return RecordReadResult::Record;
        }
        // Drop the consumed front before the buffer grows again, so what is
        // read appends to the bytes still to come and nothing else.
        if (m_start > 0) {
            m_buffer.erase(0, m_start);
            scan -= m_start;
            m_start = 0;
        }
        bool stopped = false;
        if (!ReadMoreBlock(stopped)) {
            return stopped ? RecordReadResult::Stopped : RecordReadResult::Error;
        }
    }
}

bool RecordReader::ReadMoreBlock(bool& stopped) {
    if (m_context.StopRequested()) {
        stopped = true;
        return false;
    }
    char block[kReadBlockSize];
    const ssize_t read = m_input->Read(block, sizeof(block));
    if (read == kIOInterrupted) {
        stopped = true;
        return false;
    }
    if (read < 0) {
        stopped = false;   // a failed read, not a stop
        return false;
    }
    if (read == 0) {
        m_eof = true;
    } else {
        m_buffer.append(block, static_cast<size_t>(read));
    }
    return true;
}

RecordReadResult RecordReader::NextParagraph(std::string& record) {
    size_t scan = m_start;   // where the separator search resumes within this call
    while (true) {
        // The record's start: newlines there -- one or more, as gawk skips
        // them at the start of the input (a separator's whole run is
        // consumed with its record, so only the input's start has any) --
        // are skipped. A run reaching past what is read keeps going across
        // the reads below.
        for (;;) {
            if (m_start >= m_buffer.size()) {
                break;   // nothing read yet (or the tail: the loop below)
            }
            if (m_buffer[m_start] != '\n') {
                break;
            }
            size_t end = m_start;
            for (;;) {
                while (end < m_buffer.size() && m_buffer[end] == '\n') {
                    ++end;
                }
                if (end < m_buffer.size() || m_eof) {
                    break;
                }
                bool stopped = false;
                if (!ReadMoreBlock(stopped)) {
                    return stopped ? RecordReadResult::Stopped : RecordReadResult::Error;
                }
            }
            m_buffer.erase(0, end);
            m_start = 0;
            scan = 0;
        }
        // The record: up to the first run of two or more newlines, its whole
        // run the separator. A single newline is part of the record, so the
        // scan goes on past it, whatever is read.
        for (;;) {
            const size_t newline = m_buffer.find('\n', scan);
            if (newline == std::string::npos) {
                break;   // more is read below (or the tail: the loop after)
            }
            if (newline + 1 >= m_buffer.size()) {
                // The run may go on in what is not read: this newline is
                // rescanned once more is (or, at the end of the input, it is
                // part of the record's own stripped tail).
                scan = newline;
                break;
            }
            if (m_buffer[newline + 1] != '\n') {
                scan = newline + 1;   // a single newline: part of the record
                continue;
            }
            size_t end = newline;
            for (;;) {
                while (end < m_buffer.size() && m_buffer[end] == '\n') {
                    ++end;
                }
                if (end < m_buffer.size() || m_eof) {
                    break;
                }
                bool stopped = false;
                if (!ReadMoreBlock(stopped)) {
                    return stopped ? RecordReadResult::Stopped : RecordReadResult::Error;
                }
            }
            record.assign(m_buffer, m_start, newline - m_start);
            m_buffer.erase(0, end);
            m_start = 0;
            return RecordReadResult::Record;
        }
        if (m_eof) {
            // The last record, its trailing newlines stripped; no record at
            // all when only they are left.
            size_t end = m_buffer.size();
            while (end > m_start && m_buffer[end - 1] == '\n') {
                --end;
            }
            if (end == m_start) {
                m_buffer.clear();
                m_start = 0;
                return RecordReadResult::End;
            }
            record.assign(m_buffer, m_start, end - m_start);
            m_buffer.clear();
            m_start = 0;
            return RecordReadResult::Record;
        }
        // Drop the consumed front before the buffer grows again, so what is
        // read appends to the bytes still to come and nothing else.
        if (m_start > 0) {
            m_buffer.erase(0, m_start);
            scan -= m_start;
            m_start = 0;
        }
        bool stopped = false;
        if (!ReadMoreBlock(stopped)) {
            return stopped ? RecordReadResult::Stopped : RecordReadResult::Error;
        }
    }
}

} // namespace Haisos::Awk