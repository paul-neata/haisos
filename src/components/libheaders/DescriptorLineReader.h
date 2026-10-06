#pragma once
#include <memory>
#include <optional>
#include <string>
#include "interfaces/IFileDescriptor.h"

namespace Haisos {

// Reads lines from a descriptor, for a reader that owns its input (it reads
// ahead, up to 4096 bytes at a time, and keeps the rest for the next line).
class DescriptorLineReader {
public:
    explicit DescriptorLineReader(std::shared_ptr<IFileDescriptor> input)
        : m_input(std::move(input)) {}

    // The next line without its '\n' (and without a '\r' before it); a last
    // line not ended by '\n' is returned as a line too. nullopt at end of
    // input, or once a read fails (any negative result, kIOInterrupted
    // included) -- from then on, always nullopt. A null input is at end.
    std::optional<std::string> ReadLine() {
        while (true) {
            const size_t newline = m_buffer.find('\n');
            if (newline != std::string::npos) {
                std::string line = m_buffer.substr(0, newline);
                if (!line.empty() && line.back() == '\r') {
                    line.pop_back();
                }
                m_buffer.erase(0, newline + 1);
                return line;
            }
            if (m_atEnd) {
                if (m_buffer.empty()) {
                    return std::nullopt;
                }
                // No '\n' ever came for these bytes: they are the last line.
                std::string line = std::move(m_buffer);
                m_buffer.clear();
                if (!line.empty() && line.back() == '\r') {
                    line.pop_back();
                }
                return line;
            }
            if (!m_input) {
                m_atEnd = true;
                continue;
            }
            char chunk[4096];
            const ssize_t n = m_input->Read(chunk, sizeof(chunk));
            if (n == 0) {
                m_atEnd = true;
                continue;
            }
            if (n < 0) {
                // A failed read is permanent: whatever may have been on its way
                // is gone with it.
                m_buffer.clear();
                m_input.reset();
                m_atEnd = true;
                return std::nullopt;
            }
            m_buffer.append(chunk, static_cast<size_t>(n));
        }
    }

private:
    std::shared_ptr<IFileDescriptor> m_input;
    // Bytes read ahead that no line was made of yet.
    std::string m_buffer;
    bool m_atEnd = false;
};

}
