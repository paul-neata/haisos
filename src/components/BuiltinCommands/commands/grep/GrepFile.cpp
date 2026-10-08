#include "commands/grep/GrepFile.h"
#include <cstring>
#include <string>
#include <vector>

namespace Haisos {
namespace {

// GNU's read buffer: 96 KiB. Binary detection is per buffer, before any line
// of the buffer is printed -- so for a regular file this size or smaller the
// decision covers the whole file, as GNU's does.
constexpr size_t kGrepReadChunk = 98304;
// GNU pads -T's numbers to the digits of INTMAX_MAX when it cannot know the
// size (a pipe); a descriptor in Haisos has no size, so standard input
// always pads this wide.
constexpr size_t kStdinTabWidth = 19;

size_t DigitCount(uint64_t v) {
    size_t n = 1;
    while (v >= 10) {
        v /= 10;
        ++n;
    }
    return n;
}

} // namespace

GrepFileResult GrepOneInput(BuiltinContext& context, const GrepSettings& settings, const GrepMatcher& matcher,
                            IFileDescriptor& input, const std::string& shownName,
                            std::optional<uint64_t> sizeForTab) {
    GrepFileResult result;
    const char eol = settings.nullData ? '\0' : '\n';
    const bool textMode = settings.binaryFiles == GrepBinaryFiles::Text;
    const bool showName = settings.withFilename.value_or(false);

    // -T's padding: the line number to the digits of size + 1, the byte
    // offset to the digits of size (GNU: the two widest they can reach in
    // this file), or 19 when the size is unknown.
    const auto paddedNumber = [&](uint64_t v, bool isLineNumber) {
        std::string text = std::to_string(v);
        if (!settings.initialTab) {
            return text;
        }
        const size_t width = sizeForTab
            ? DigitCount(*sizeForTab + (isLineNumber ? 1 : 0))
            : kStdinTabWidth;
        if (text.size() < width) {
            text.insert(0, width - text.size(), ' ');
        }
        return text;
    };

    // The prefix of an output line, in this order: name, line number, byte
    // offset, then with -T a tab when anything was printed before it.
    const auto printPrefixed = [&](const std::string_view text, uint64_t lineNo, uint64_t offsetValue) {
        std::string out;
        if (showName) {
            out += shownName + ":";
        }
        if (settings.lineNumbers) {
            out += paddedNumber(lineNo, true) + ":";
        }
        if (settings.byteOffsets) {
            out += paddedNumber(offsetValue, false) + ":";
        }
        if (settings.initialTab && !out.empty()) {
            out += '\t';
        }
        out += text;
        out += eol;
        context.Out(out);
        if (settings.lineBuffered) {
            context.Flush();
        }
    };

    uint64_t lineNo = 0;
    uint64_t offset = 0;  // the byte offset of the next line's first byte
    bool binary = false;  // this input became binary
    bool binaryMatched = false;

    // One line (without its terminator). Returns whether to stop reading
    // this input.
    const auto processLine = [&](std::string_view line) -> bool {
        ++lineNo;
        const bool selected = matcher.Matches(line) != settings.invert;
        if (!selected) {
            return false;
        }
        ++result.selected;
        if (settings.quiet) {
            result.stopped = true;  // -q: stop everything (the caller exits 0)
            return true;
        }
        if (binary) {
            binaryMatched = true;
            if (!settings.count) {
                return true;  // the search of this input ends at its first selected line
            }
        }
        if (settings.listFiles != GrepListFiles::None) {
            return true;
        }
        if (settings.count) {
            // The counted line counts towards -m too.
        } else if (settings.onlyMatching && !settings.invert) {
            // Each non-empty match of the line; an empty match advances one
            // byte and prints nothing (which is why -o 'x*' is quiet).
            size_t pos = 0;
            while (pos <= line.size()) {
                size_t begin = 0;
                size_t end = 0;
                if (!matcher.Find(line, pos, begin, end)) {
                    break;
                }
                if (end > begin) {
                    printPrefixed(line.substr(begin, end - begin), lineNo, offset + begin);
                }
                pos = end > begin ? end : begin + 1;
            }
        } else if (!settings.onlyMatching) {
            // -o -v prints nothing, as GNU's (the matched parts of lines it
            // did not select).
            printPrefixed(line, lineNo, offset);
        }
        // GNU stops reading the input after the NUMth matching line, counted
        // and printed above.
        return settings.maxCount && result.selected >= *settings.maxCount;
    };

    std::string data;  // the carry: the partial line of the previous chunk
    bool eof = false;
    bool binarySkip = false;  // -I: the input has no selected line at all
    // -m 0: the input is not read at all.
    const bool noLimitZero = !settings.maxCount || *settings.maxCount > 0;

    if (noLimitZero) {
        std::vector<char> buffer(kGrepReadChunk);
        while (true) {
            if (context.StopRequested()) {
                result.stopped = true;
                break;
            }
            const ssize_t n = input.Read(buffer.data(), buffer.size());
            if (n == 0) {
                eof = true;
                break;
            }
            if (n == kIOInterrupted) {
                result.stopped = true;
                break;
            }
            if (n < 0) {
                if (!settings.noMessages) {
                    context.ErrorText("grep: " + shownName + ": Input/output error\n");
                }
                result.error = true;
                break;
            }
            data.append(buffer.data(), static_cast<size_t>(n));
            // A chunk holding a NUL byte makes the input binary from this
            // chunk on (lines already printed stay), unless -a or -z; only
            // a NUL makes a file binary in the C locale.
            if (!binary && !textMode && !settings.nullData
                && std::memchr(buffer.data(), '\0', static_cast<size_t>(n)) != nullptr) {
                if (settings.binaryFiles == GrepBinaryFiles::WithoutMatch) {
                    // -I: the input counts as having no selected line at
                    // all, exactly as for a file with no match.
                    result.selected = 0;
                    binaryMatched = false;
                    data.clear();
                    binarySkip = true;
                    break;
                }
                binary = true;
            }
            // Split the chunk into complete lines. Once binary, a NUL ends
            // a line too (for matching, counting and line numbers).
            size_t pos = 0;
            bool stopInput = false;
            while (pos < data.size()) {
                if (context.StopRequested()) {
                    result.stopped = true;
                    break;
                }
                size_t end = data.find(eol, pos);
                if (binary) {
                    const size_t nul = data.find('\0', pos);
                    if (nul != std::string::npos && (end == std::string::npos || nul < end)) {
                        end = nul;
                    }
                }
                if (end == std::string::npos) {
                    break;
                }
                stopInput = processLine(std::string_view(data).substr(pos, end - pos));
                offset = end + 1;
                pos = end + 1;
                if (stopInput || result.stopped) {
                    break;
                }
            }
            data.erase(0, pos);
            if (stopInput || result.stopped) {
                break;
            }
        }
    }

    if (eof && !binarySkip && !data.empty()) {
        // A last line without its terminator is printed with one added.
        processLine(data);
    }

    if (!result.stopped) {
        if (settings.listFiles != GrepListFiles::None) {
            const bool list = settings.listFiles == GrepListFiles::Matching
                ? result.selected > 0
                : result.selected == 0;
            if (list) {
                context.Out(shownName + "\n");
                if (settings.lineBuffered) {
                    context.Flush();
                }
            }
        } else if (settings.count) {
            std::string text = showName ? shownName + ":" : "";
            text += std::to_string(result.selected);
            text += '\n';
            context.Out(text);
            if (settings.lineBuffered) {
                context.Flush();
            }
        }
        // A binary file that matched says so on stderr once the search of
        // this input has ended, unless -c -l -L -q: -s does not hide it, and
        // it is not an error (the exit status is untouched).
        if (binaryMatched && !settings.count && settings.listFiles == GrepListFiles::None
            && !settings.quiet) {
            context.ErrorText("grep: " + shownName + ": binary file matches\n");
        }
    }
    return result;
}

} // namespace Haisos