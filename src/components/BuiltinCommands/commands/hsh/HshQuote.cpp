#include "commands/hsh/HshQuote.h"

namespace Haisos::Hsh {

std::string ShellSingleQuote(const std::string& value) {
    std::string result;
    size_t pos = 0;
    for (;;) {
        // The run of non-quote bytes, in single quotes.
        const size_t quote = value.find('\'', pos);
        const size_t len = (quote == std::string::npos ? value.size() : quote) - pos;
        result += '\'';
        result.append(value, pos, len);
        result += '\'';
        pos += len;
        size_t run = 0;
        while (pos + run < value.size() && value[pos + run] == '\'') {
            ++run;
        }
        if (run == 0) {
            break;
        }
        // The run of single quotes, in double quotes.
        result += '"';
        result.append(value, pos, run);
        result += '"';
        pos += run;
        if (pos == value.size()) {
            break;
        }
    }
    return result;
}

} // namespace Haisos::Hsh
