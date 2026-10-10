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
    // RS's first byte is the separator; gawk --posix reads no further byte
    // of it. An empty |rs| (paragraph mode) is awk--records' -- until then
    // the caller never passes one, and a newline stands in its place.
    const char separator = rs.empty() ? '\n' : rs.front();
    while (true) {
        const size_t separatorPos = m_buffer.find(separator);
        if (separatorPos != std::string::npos) {
            record.assign(m_buffer, 0, separatorPos);
            m_buffer.erase(0, separatorPos + 1);
            return RecordReadResult::Record;
        }
        if (m_eof) {
            // The end of the input: the bytes still buffered are the last
            // record (it had no separator), or there is none at all.
            if (m_buffer.empty()) {
                return RecordReadResult::End;
            }
            record = std::move(m_buffer);
            m_buffer.clear();
            return RecordReadResult::Record;
        }
        if (m_context.StopRequested()) {
            return RecordReadResult::Stopped;
        }
        char block[kReadBlockSize];
        const ssize_t read = m_input->Read(block, sizeof(block));
        if (read == 0) {
            m_eof = true;
            continue;
        }
        if (read == kIOInterrupted) {
            return RecordReadResult::Stopped;
        }
        if (read < 0) {
            return RecordReadResult::Error;
        }
        m_buffer.append(block, static_cast<size_t>(read));
    }
}

} // namespace Haisos::Awk