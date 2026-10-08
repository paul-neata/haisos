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

// One coloured piece, GNU's SGR bytes: ESC [ V m, (ESC [ K), the text,
// ESC [ m, (ESC [ K) -- the ESC [ K bytes dropped with GREP_COLORS' ne, and
// nothing at all added for an empty capability value.
std::string ColorPiece(const GrepColors& colors, const std::string& value, std::string_view text) {
    if (value.empty()) {
        return std::string(text);
    }
    std::string out = "\x1b[";
    out += value;
    out += 'm';
    if (!colors.ne) {
        out += "\x1b[K";
    }
    out += text;
    out += "\x1b[m";
    if (!colors.ne) {
        out += "\x1b[K";
    }
    return out;
}

// The start and the end of the colour of a whole line (sl/cx): the start is
// written at the line's start and again after each highlighted match, the
// end before the line terminator; both are nothing for an empty value.
std::string ColorStart(const GrepColors& colors, const std::string& value) {
    if (value.empty()) {
        return "";
    }
    std::string out = "\x1b[";
    out += value;
    out += 'm';
    if (!colors.ne) {
        out += "\x1b[K";
    }
    return out;
}

std::string ColorEnd(const GrepColors& colors) {
    std::string out = "\x1b[m";
    if (!colors.ne) {
        out += "\x1b[K";
    }
    return out;
}

} // namespace

GrepFileResult GrepOneInput(BuiltinContext& context, const GrepSettings& settings, const GrepMatcher& matcher,
                            IFileDescriptor& input, const std::string& shownName,
                            std::optional<uint64_t> sizeForTab, GrepContext* grepContext) {
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

    // The text of one output line, coloured when colour is on: the line's
    // own colour (sl for a selected line, cx for a context one, swapped by
    // rv with -v) around it, restarted after each highlighted match, and
    // the matches themselves in ms/mc. A line's matches are highlighted when
    // the run would select it (selected XOR -v): ms in a selected line, mc
    // in a context one; an empty match is not.
    const auto lineText = [&](std::string_view text, bool selectedLine) -> std::string {
        if (!settings.color) {
            return std::string(text);
        }
        const GrepColors& colors = settings.colors;
        const bool swap = colors.rv && settings.invert;
        const std::string& lineCap = selectedLine
            ? (swap ? colors.cx : colors.sl)
            : (swap ? colors.sl : colors.cx);
        std::string out = ColorStart(colors, lineCap);
        if (selectedLine != settings.invert) {
            const std::string& matchCap = selectedLine ? colors.ms : colors.mc;
            size_t pos = 0;
            size_t lastEnd = 0;
            while (pos <= text.size()) {
                size_t begin = 0;
                size_t end = 0;
                if (!matcher.Find(text, pos, begin, end)) {
                    break;
                }
                if (end > begin) {
                    out.append(text.substr(lastEnd, begin - lastEnd));
                    out += ColorPiece(colors, matchCap, text.substr(begin, end - begin));
                    out += ColorStart(colors, lineCap);
                    lastEnd = end;
                    pos = end;
                } else {
                    pos = begin + 1;
                }
            }
            out.append(text.substr(lastEnd));
        } else {
            out.append(text);
        }
        if (!lineCap.empty()) {
            out += ColorEnd(colors);
        }
        return out;
    };

    // The prefix of an output line, in this order: name, line number, byte
    // offset, then with -T a tab when anything was printed before it. A
    // selected line's separators are ':' (with -Z a bare NUL byte after the
    // name), a context line's '-'.
    const auto printPrefixed = [&](std::string_view text, uint64_t lineNo2, uint64_t offsetValue,
                                   bool selectedLine, bool matchText) {
        std::string out;
        const char sep = selectedLine ? ':' : '-';
        const auto appendSep = [&]() {
            if (settings.color) {
                out += ColorPiece(settings.colors, settings.colors.se, std::string(1, sep));
            } else {
                out += sep;
            }
        };
        if (showName) {
            if (settings.color) {
                out += ColorPiece(settings.colors, settings.colors.fn, shownName);
            } else {
                out += shownName;
            }
            if (settings.nullAfterName) {
                out += '\0';
            } else {
                appendSep();
            }
        }
        if (settings.lineNumbers) {
            const std::string number = paddedNumber(lineNo2, true);
            out += settings.color ? ColorPiece(settings.colors, settings.colors.ln, number) : number;
            appendSep();
        }
        if (settings.byteOffsets) {
            const std::string number = paddedNumber(offsetValue, false);
            out += settings.color ? ColorPiece(settings.colors, settings.colors.bn, number) : number;
            appendSep();
        }
        if (settings.initialTab && !out.empty()) {
            out += '\t';
        }
        // -o hands a match itself: coloured in the match colour (ms in a
        // selected line, mc in a context one), not searched again.
        if (matchText) {
            const std::string& matchCap = selectedLine ? settings.colors.ms : settings.colors.mc;
            out += settings.color ? ColorPiece(settings.colors, matchCap, text) : std::string(text);
        } else {
            out += lineText(text, selectedLine);
        }
        out += eol;
        context.Out(out);
        if (settings.lineBuffered) {
            context.Flush();
        }
    };

    // What one line (selected or context) prints, -o included: each match on
    // a line of its own with -o (a context line's matches too, as GNU's),
    // the whole line otherwise; an empty match prints nothing.
    const auto printLine = [&](std::string_view text, uint64_t lineNo2, uint64_t offset2, bool selectedLine) {
        if (settings.onlyMatching) {
            if (selectedLine && settings.invert) {
                return;  // -o -v: the selected lines hold no matches
            }
            size_t pos = 0;
            while (pos <= text.size()) {
                size_t begin = 0;
                size_t end = 0;
                if (!matcher.Find(text, pos, begin, end)) {
                    break;
                }
                if (end > begin) {
                    printPrefixed(text.substr(begin, end - begin), lineNo2, offset2 + begin,
                                  selectedLine, /*matchText=*/true);
                }
                pos = end > begin ? end : begin + 1;
            }
            return;
        }
        printPrefixed(text, lineNo2, offset2, selectedLine, /*matchText=*/false);
    };

    // The group separator, as a line of its own; --no-group-separator
    // prints none at all.
    const auto printSeparator = [&]() {
        if (!settings.groupSeparator) {
            return;
        }
        if (settings.color) {
            context.Out(ColorPiece(settings.colors, settings.colors.se, *settings.groupSeparator) + std::string(1, eol));
        } else {
            context.Out(*settings.groupSeparator + std::string(1, eol));
        }
        if (settings.lineBuffered) {
            context.Flush();
        }
    };

    // This input joins the run's context under its own name and numbering.
    if (grepContext) {
        grepContext->SetPrinters(printLine, printSeparator);
        grepContext->BeginFile();
    }

    uint64_t lineNo = 0;
    uint64_t offset = 0;  // the byte offset of the next line's first byte
    bool binary = false;  // this input became binary
    bool binaryMatched = false;
    // -m with context: the count is used up but trailing context is still
    // owed, so every further line is context, until the context runs out.
    bool limitReached = false;

    // One line (without its terminator). Returns whether to stop reading
    // this input.
    const auto processLine = [&](std::string_view line) -> bool {
        ++lineNo;
        if (limitReached) {
            // -m with context: every further line is trailing context, but it
            // is still matched -- a matching one prints as a match line and
            // restarts nothing (GNU: -m1 -A4 TODO on a.c with a match at 5
            // prints 5:five TODO and stops at 6). A binary input prints no
            // context at all.
            if (grepContext && !binary) {
                const bool selectedLine = matcher.Matches(line) != settings.invert;
                grepContext->Line(line, lineNo, offset, selectedLine, /*extendsAfter=*/false);
                return !grepContext->AfterPending();
            }
            return true;
        }
        const bool selected = matcher.Matches(line) != settings.invert;
        if (selected) {
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
            } else if (grepContext) {
                grepContext->Line(line, lineNo, offset, true);
            } else {
                printLine(line, lineNo, offset, true);
            }
            // GNU stops reading the input after the NUMth matching line,
            // counted and printed above -- with context still owed, it keeps
            // reading only as far as the trailing context reaches.
            if (settings.maxCount && result.selected >= *settings.maxCount) {
                if (grepContext && grepContext->AfterPending()) {
                    limitReached = true;
                    return false;
                }
                return true;
            }
            return false;
        }
        // A binary input prints nothing further, its selected line's message
        // excepted.
        if (grepContext && !binary) {
            grepContext->Line(line, lineNo, offset, false);
        }
        return false;
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
                // |data| is erased after every chunk: advance by the line's
                // length, never by an index into it.
                offset += end - pos + 1;
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
                std::string text;
                if (settings.color) {
                    text += ColorPiece(settings.colors, settings.colors.fn, shownName);
                } else {
                    text += shownName;
                }
                text += settings.nullAfterName ? '\0' : '\n';
                context.Out(text);
                if (settings.lineBuffered) {
                    context.Flush();
                }
            }
        } else if (settings.count) {
            std::string text;
            if (showName) {
                if (settings.color) {
                    text += ColorPiece(settings.colors, settings.colors.fn, shownName);
                } else {
                    text += shownName;
                }
                if (settings.nullAfterName) {
                    text += '\0';
                } else if (settings.color) {
                    text += ColorPiece(settings.colors, settings.colors.se, ":");
                } else {
                    text += ':';
                }
            }
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