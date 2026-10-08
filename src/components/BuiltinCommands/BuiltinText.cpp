#include "BuiltinText.h"
#include <cstdio>

namespace Haisos {

std::string GnuQuote(std::string_view text) {
    std::string out = "'";
    for (const char byte : text) {
        const unsigned char c = static_cast<unsigned char>(byte);
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '\'': out += "\\'"; break;
            case '\a': out += "\\a"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            case '\v': out += "\\v"; break;
            default:
                if (c < 0x20 || c >= 0x7f) {
                    char octal[8];
                    std::snprintf(octal, sizeof(octal), "\\%03o", c);
                    out += octal;
                } else {
                    out += byte;
                }
                break;
        }
    }
    out += "'";
    return out;
}

namespace {

// The "Valid arguments are:" block: every choice on a line of its own, names
// sharing a value (consecutive, in table order) together on one line.
void WriteValidArguments(BuiltinContext& context, const std::vector<ArgChoice>& choices) {
    std::string text = "Valid arguments are:\n";
    size_t i = 0;
    while (i < choices.size()) {
        std::string line = "  - ";
        size_t j = i;
        while (j < choices.size() && choices[j].value == choices[i].value) {
            if (j > i) {
                line += ", ";
            }
            line += GnuQuote(choices[j].name);
            ++j;
        }
        text += line + "\n";
        i = j;
    }
    context.ErrorText(text);
}

} // namespace

std::optional<int> ArgMatch(BuiltinContext& context, const std::string& longOption,
                            const std::string& value, const std::vector<ArgChoice>& choices) {
    for (const auto& choice : choices) {
        if (value == choice.name) {
            return choice.value;
        }
    }
    // A prefix of the names: unambiguous when every name it prefixes means the
    // same thing.
    bool ambiguous = false;
    const ArgChoice* match = nullptr;
    for (const auto& choice : choices) {
        if (value.size() <= choice.name.size()
            && choice.name.compare(0, value.size(), value) == 0) {
            if (match && match->value != choice.value) {
                ambiguous = true;  // names it prefixes disagree
            }
            match = &choice;
        }
    }
    if (match && !ambiguous) {
        return match->value;
    }
    context.Error(std::string(ambiguous ? "ambiguous" : "invalid") + " argument "
        + GnuQuote(value) + " for " + GnuQuote(longOption));
    WriteValidArguments(context, choices);
    context.TryHelp();
    return std::nullopt;
}

std::shared_ptr<IFileDescriptor> OpenInputOperand(BuiltinContext& context, const std::string& name,
                                                  InputOpenFailure& failure) {
    failure = InputOpenFailure::None;
    if (name == "-") {
        auto input = context.IO().GetDescriptor(IFileIO::kStdIn);
        if (!input) {
            failure = InputOpenFailure::BadDescriptor;
            return nullptr;
        }
        return input;
    }
    IFileIO& io = context.IO();
    FileStatus status;
    if (io.Stat(name, status) != 0) {
        failure = InputOpenFailure::Missing;
        return nullptr;
    }
    // GNU opens a directory fine and fails on read (EISDIR); Haisos cannot
    // open one at all, so the caller words this as its read error.
    if (status.type == DirectoryEntryType::Dir) {
        failure = InputOpenFailure::Directory;
        return nullptr;
    }
    auto handle = io.OpenFile(name, kFileOpenReadOnly);
    if (!handle) {
        failure = InputOpenFailure::Denied;
        return nullptr;
    }
    return handle;
}

BuiltinLineReader::BuiltinLineReader(BuiltinContext& context, IFileDescriptor& input, char delimiter)
    : m_context(context)
    , m_input(input)
    , m_delimiter(delimiter)
{
}

LineReadResult BuiltinLineReader::Fill() {
    if (m_context.StopRequested()) {
        return LineReadResult::Stopped;
    }
    char buffer[64 * 1024];
    const ssize_t n = m_input.Read(buffer, sizeof(buffer));
    if (n == 0) {
        m_atEnd = true;
        return LineReadResult::End;
    }
    if (n == kIOInterrupted) {
        return LineReadResult::Stopped;
    }
    if (n < 0) {
        return LineReadResult::Error;
    }
    m_buffer.append(buffer, static_cast<size_t>(n));
    return LineReadResult::Line;
}

LineReadResult BuiltinLineReader::Next(std::string& line, bool& delimited) {
    while (true) {
        // Search only the bytes not searched yet: a long line read in many
        // chunks is scanned once, not once per chunk.
        const size_t found = m_buffer.find(m_delimiter, m_scan);
        if (found != std::string::npos) {
            line.assign(m_buffer, m_pos, found - m_pos);
            m_pos = found + 1;
            m_scan = m_pos;
            delimited = true;
            return LineReadResult::Line;
        }
        m_scan = m_buffer.size();
        if (m_atEnd) {
            if (m_pos >= m_buffer.size()) {
                m_buffer.clear();
                m_pos = 0;
                m_scan = 0;
                return LineReadResult::End;
            }
            line.assign(m_buffer, m_pos, m_buffer.size() - m_pos);
            m_buffer.clear();
            m_pos = 0;
            m_scan = 0;
            delimited = false;
            return LineReadResult::Line;
        }
        // Drop the lines already handed out once per read, not once per line:
        // erasing after every line would move the rest of the buffer each time.
        if (m_pos > 0) {
            m_buffer.erase(0, m_pos);
            m_scan -= m_pos;
            m_pos = 0;
        }
        const LineReadResult filled = Fill();
        if (filled == LineReadResult::Line) {
            continue;  // bytes were added: look for the delimiter again
        }
        if (filled == LineReadResult::End) {
            continue;  // the input is over: hand out the last line next time
        }
        return filled;  // Stopped or Error
    }
}

ssize_t WriteFully(IFileDescriptor& out, std::string_view bytes) {
    size_t written = 0;
    while (written < bytes.size()) {
        const ssize_t n = out.Write(bytes.data() + written, bytes.size() - written);
        if (n <= 0) {
            return n == 0 ? kIOError : n;
        }
        written += static_cast<size_t>(n);
    }
    return static_cast<ssize_t>(bytes.size());
}

} // namespace Haisos