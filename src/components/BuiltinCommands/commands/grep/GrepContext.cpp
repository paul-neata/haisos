#include "commands/grep/GrepContext.h"

namespace Haisos {

GrepContext::GrepContext(size_t before, size_t after, bool enabled, bool separatorAcrossFiles,
                         PrintLine printLine, PrintSeparator printSeparator)
    : m_before(before), m_after(after), m_enabled(enabled),
      m_separatorAcrossFiles(separatorAcrossFiles),
      m_printLine(std::move(printLine)), m_printSeparator(std::move(printSeparator)) {}

void GrepContext::SetPrinters(PrintLine printLine, PrintSeparator printSeparator) {
    m_printLine = std::move(printLine);
    m_printSeparator = std::move(printSeparator);
}

void GrepContext::BeginFile() {
    m_ring.clear();
    m_owed = 0;
    m_lastPrintedLine.reset();
}

void GrepContext::Line(std::string_view text, uint64_t lineNumber, uint64_t byteOffset,
                       bool selected, bool extendsAfter) {
    if (selected) {
        // The before-context, oldest first, then the line; from here on
        // |after| more lines are owed.
        for (const RingEntry& entry : m_ring) {
            Print(entry.text, entry.lineNumber, entry.byteOffset, false);
        }
        m_ring.clear();
        Print(text, lineNumber, byteOffset, true);
        if (extendsAfter) {
            m_owed = m_after;
        } else if (m_owed > 0) {
            // A match inside the trailing context (grep -m): printed as a
            // match, but it counts as one of the owed lines, restarting
            // nothing.
            --m_owed;
        }
        return;
    }
    if (m_owed > 0) {
        Print(text, lineNumber, byteOffset, false);
        --m_owed;
        return;
    }
    // Not owed anything: the line may still become before-context of a later
    // selected one. Keep the last |before| such lines, oldest dropped first.
    if (m_before == 0) {
        return;
    }
    if (m_ring.size() == m_before) {
        m_ring.pop_front();
    }
    m_ring.push_back({std::string(text), lineNumber, byteOffset});
}

void GrepContext::Print(std::string_view text, uint64_t lineNumber, uint64_t byteOffset, bool selected) {
    // A separator goes before a line when context was asked for at all,
    // something was printed before it (in this input, or in any earlier one
    // when the separator spans files) and it does not follow the line last
    // printed of the same input.
    const bool printedBefore = m_lastPrintedLine.has_value()
        || (m_separatorAcrossFiles && m_anyPrinted);
    const bool contiguous = m_lastPrintedLine.has_value() && lineNumber == *m_lastPrintedLine + 1;
    if (m_enabled && printedBefore && !contiguous) {
        m_printSeparator();
    }
    // The line counts as printed even when the caller's printer writes
    // nothing (grep -o writes no context lines, but the separators stay).
    m_printLine(text, lineNumber, byteOffset, selected);
    m_lastPrintedLine = lineNumber;
    m_anyPrinted = true;
}

} // namespace Haisos