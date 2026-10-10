#pragma once
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace Haisos {

// Before/after context, GNU grep's way: which lines to print around the
// selected ones, and where a group separator goes. The caller prints.
class GrepContext {
public:
    using PrintLine = std::function<void(std::string_view text, uint64_t lineNumber, uint64_t byteOffset, bool selected)>;
    using PrintSeparator = std::function<void()>;

    // |before|/|after|: -B/-A (0 allowed). |enabled|: whether context was
    // asked for at all (GNU: -A/-B/-C/-NUM given, even 0) -- separators are
    // printed only then. |separatorAcrossFiles|: a separator also goes
    // between groups of different files (grep; rg without headings).
    GrepContext(size_t before, size_t after, bool enabled, bool separatorAcrossFiles,
                PrintLine printLine, PrintSeparator printSeparator);

    // The printers of the input about to be read: one context spans a whole
    // run, while each input prints under its own name and numbering.
    void SetPrinters(PrintLine printLine, PrintSeparator printSeparator);

    // Starts an input: the ring, the trailing context owed and the line
    // last printed in this file are forgotten; a separator may still follow
    // one printed by an earlier input.
    void BeginFile();

    // Every line of the file, in order. |extendsAfter| false: a selected line
    // that does not restart the trailing context (rg after -m).
    void Line(std::string_view text, uint64_t lineNumber, uint64_t byteOffset, bool selected, bool extendsAfter = true);

    // Trailing context still owed after the last selected line.
    bool AfterPending() const { return m_owed > 0; }

private:
    // Prints one line, with a separator before it when one belongs there.
    void Print(std::string_view text, uint64_t lineNumber, uint64_t byteOffset, bool selected);

    struct RingEntry {
        std::string text;
        uint64_t lineNumber;
        uint64_t byteOffset;
    };

    size_t m_before;
    size_t m_after;
    bool m_enabled;
    bool m_separatorAcrossFiles;
    PrintLine m_printLine;
    PrintSeparator m_printSeparator;
    std::deque<RingEntry> m_ring;  // the last lines not printed, oldest first
    size_t m_owed = 0;             // after-context lines still owed
    // The line last printed in this input, and whether anything was printed
    // at all (any input) -- what decides where a separator goes.
    std::optional<uint64_t> m_lastPrintedLine;
    bool m_anyPrinted = false;
};

} // namespace Haisos