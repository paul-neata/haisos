#include "commands/jq/JqJsonWriter.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "src/components/Unicode/Unicode.h"
#include "commands/jq/JqUtf8.h"

namespace Haisos::Jq {
namespace {

// The escape of a control byte: the six JSON names, else \u00XX (0x7F
// included), lowercase hex.
void AppendControlEscape(std::string& out, unsigned char c) {
    switch (c) {
        case '\b': out += "\\b"; return;
        case '\f': out += "\\f"; return;
        case '\n': out += "\\n"; return;
        case '\r': out += "\\r"; return;
        case '\t': out += "\\t"; return;
        default: break;
    }
    char buf[7];
    std::snprintf(buf, sizeof buf, "\\u%04x", static_cast<unsigned int>(c));
    out += buf;
}

void AppendUnicodeEscape(std::string& out, unsigned int codePoint) {
    if (codePoint > 0xFFFF) {
        // A surrogate pair, as two escapes.
        const unsigned int above = codePoint - 0x10000;
        AppendUnicodeEscape(out, 0xD800 + (above >> 10));
        AppendUnicodeEscape(out, 0xDC00 + (above & 0x3FF));
        return;
    }
    char buf[7];
    std::snprintf(buf, sizeof buf, "\\u%04x", codePoint);
    out += buf;
}

// -- jq's computed numbers -------------------------------------------------

// The shortest digits that read back as |abs|, with their power of ten:
// the %e text of a precision, its mantissa digits and exponent.
struct ShortestDigits {
    std::string digits;  // no trailing zeros
    int exponent = 0;    // the power of ten of the first digit
};

ShortestDigits FindShortestDigits(double abs) {
    char buf[40];
    for (int precision = 1; precision <= 17; ++precision) {
        std::snprintf(buf, sizeof buf, "%.*e", precision - 1, abs);
        if (std::strtod(buf, nullptr) == abs)
            break;
    }
    ShortestDigits out;
    const char* p = buf;
    out.digits += *p++;  // the digit before the '.'
    if (*p == '.') {
        ++p;
        while (*p != 'e' && *p != 'E')
            out.digits += *p++;
    }
    out.exponent = static_cast<int>(std::strtol(p + 1, nullptr, 10));
    const size_t last = out.digits.find_last_not_of('0');
    out.digits.erase(last == std::string::npos ? 0 : last + 1);
    return out;
}

} // namespace

std::string FormatNumber(const Value& value) {
    if (const std::string* literal = value.Literal())
        return *literal;
    const double x = value.AsNumber();
    if (std::isnan(x))
        return "null";
    if (std::isinf(x))
        return x > 0 ? "1.7976931348623157e+308" : "-1.7976931348623157e+308";
    if (x == 0.0)
        return std::signbit(x) ? "-0" : "0";

    const bool negative = std::signbit(x);
    const ShortestDigits d = FindShortestDigits(std::fabs(x));
    const size_t nd = d.digits.size();
    std::string out;
    if (d.exponent < -4 || d.exponent >= static_cast<int>(nd) + 15) {
        // jq's exponential form: e, a sign, at least two digits.
        out = d.digits[0];
        if (nd > 1) {
            out += '.';
            out.append(d.digits, 1, std::string::npos);
        }
        out += 'e';
        const int mag = d.exponent >= 0 ? d.exponent : -d.exponent;
        out += d.exponent >= 0 ? '+' : '-';
        if (mag < 10)
            out += '0';
        out += std::to_string(mag);
    } else if (d.exponent < 0) {
        out = "0.";
        out.append(static_cast<size_t>(-d.exponent - 1), '0');
        out += d.digits;
    } else if (static_cast<size_t>(d.exponent) + 1 >= nd) {
        out = d.digits;
        out.append(static_cast<size_t>(d.exponent) + 1 - nd, '0');
    } else {
        out.append(d.digits, 0, static_cast<size_t>(d.exponent) + 1);
        out += '.';
        out.append(d.digits, static_cast<size_t>(d.exponent) + 1, std::string::npos);
    }
    return negative ? "-" + out : out;
}

std::string QuoteJsonString(const std::string& utf8, bool ascii) {
    std::string out = "\"";
    size_t i = 0;
    while (i < utf8.size()) {
        const unsigned char c = static_cast<unsigned char>(utf8[i]);
        if (c == '"') {
            out += "\\\"";
            ++i;
        } else if (c == '\\') {
            out += "\\\\";
            ++i;
        } else if (c < 0x20 || c == 0x7F) {
            AppendControlEscape(out, c);
            ++i;
        } else if (c < 0x80 || !ascii) {
            out += static_cast<char>(c);
            ++i;
        } else {
            // Non-ASCII, escaped: the character's code point, or U+FFFD
            // where the bytes hold no whole one.
            Unicode::DecodedChar decoded = Unicode::DecodeUtf8(utf8.data() + i,
                                                              utf8.size() - i);
            size_t taken = decoded.length;
            if (decoded.status != Unicode::DecodeStatus::Ok) {
                AppendUnicodeEscape(out, 0xFFFD);
                if (taken == 0)
                    taken = 1;
            } else {
                AppendUnicodeEscape(out, static_cast<unsigned int>(decoded.codePoint));
            }
            i += taken;
        }
    }
    out += '"';
    return out;
}

namespace {
namespace writer {

// jq's colours: containers and structure bright, keys blue, null grey,
// false/true/numbers and strings plain.
const char kReset[] = "\x1b[0m";
const char kStructure[] = "\x1b[1;39m";
const char kKey[] = "\x1b[1;34m";
const char kNull[] = "\x1b[0;90m";
const char kScalar[] = "\x1b[0;39m";
const char kString[] = "\x1b[0;32m";

} // namespace writer

void AppendIndent(std::string& out, const WriteOptions& options, int level) {
    for (int i = 0; i < level; ++i) {
        if (options.tab)
            out += '\t';
        else
            out.append(static_cast<size_t>(options.indent), ' ');
    }
}

void WriteValue(const Value& value, const WriteOptions& options, bool pretty,
                int level, std::string& out);

void WriteArray(const Value& value, const WriteOptions& options, bool pretty,
                int level, std::string& out) {
    const std::vector<Value>& elements = value.AsArray();
    if (options.color)
        out += writer::kStructure;
    out += '[';
    if (elements.empty()) {
        out += ']';
        if (options.color)
            out += writer::kReset;
        return;
    }
    if (pretty) {
        out += '\n';
        AppendIndent(out, options, level + 1);
    }
    for (size_t i = 0; i < elements.size(); ++i) {
        if (i > 0) {
            if (options.color)
                out += writer::kStructure;
            out += ',';
            if (pretty) {
                out += '\n';
                AppendIndent(out, options, level + 1);
            }
        }
        WriteValue(elements[i], options, pretty, level + 1, out);
    }
    if (options.color)
        out += writer::kStructure;
    if (pretty) {
        out += '\n';
        AppendIndent(out, options, level);
    }
    if (options.color)
        out += writer::kStructure;
    out += ']';
    if (options.color)
        out += writer::kReset;
}

void WriteObject(const Value& value, const WriteOptions& options, bool pretty,
                 int level, std::string& out) {
    const std::vector<ObjectEntry>& members = value.AsObject();
    std::vector<const ObjectEntry*> ordered;
    ordered.reserve(members.size());
    for (const ObjectEntry& entry : members)
        ordered.push_back(&entry);
    if (options.sortKeys) {
        std::sort(ordered.begin(), ordered.end(),
                  [](const ObjectEntry* a, const ObjectEntry* b) { return a->first < b->first; });
    }
    if (options.color)
        out += writer::kStructure;
    out += '{';
    if (ordered.empty()) {
        out += '}';
        if (options.color)
            out += writer::kReset;
        return;
    }
    if (pretty) {
        out += '\n';
        AppendIndent(out, options, level + 1);
    }
    for (size_t i = 0; i < ordered.size(); ++i) {
        if (i > 0) {
            if (options.color)
                out += writer::kStructure;
            out += ',';
            if (pretty) {
                out += '\n';
                AppendIndent(out, options, level + 1);
            }
        }
        if (options.color) {
            out += writer::kReset;
            out += writer::kKey;
        }
        out += QuoteJsonString(ordered[i]->first, options.ascii);
        if (options.color) {
            out += writer::kReset;
            out += writer::kStructure;
        }
        out += ':';
        if (pretty)
            out += ' ';
        if (options.color)
            out += writer::kReset;
        WriteValue(ordered[i]->second, options, pretty, level + 1, out);
    }
    if (options.color)
        out += writer::kStructure;
    if (pretty) {
        out += '\n';
        AppendIndent(out, options, level);
    }
    if (options.color)
        out += writer::kStructure;
    out += '}';
    if (options.color)
        out += writer::kReset;
}

void WriteValue(const Value& value, const WriteOptions& options, bool pretty,
                int level, std::string& out) {
    const bool color = options.color;
    switch (value.GetKind()) {
        case Kind::Null:
            if (color) out += writer::kNull;
            out += "null";
            break;
        case Kind::False:
            if (color) out += writer::kScalar;
            out += "false";
            break;
        case Kind::True:
            if (color) out += writer::kScalar;
            out += "true";
            break;
        case Kind::Number:
            if (color) out += writer::kScalar;
            out += FormatNumber(value);
            break;
        case Kind::String:
            if (color) out += writer::kString;
            out += QuoteJsonString(value.AsString(), options.ascii);
            break;
        case Kind::Array:
            WriteArray(value, options, pretty, level, out);
            return;  // WriteArray holds its own colour
        case Kind::Object:
            WriteObject(value, options, pretty, level, out);
            return;
    }
    if (color)
        out += writer::kReset;
}

} // namespace

void WriteJson(const Value& value, const WriteOptions& options, std::string& out) {
    const bool pretty = options.tab || options.indent > 0;
    WriteValue(value, options, pretty, 0, out);
}

std::string DumpTruncated(const Value& value, size_t bufferSize) {
    WriteOptions options;
    options.indent = 0;  // one line, as jq's error messages hold a value
    std::string dump;
    WriteJson(value, options, dump);
    if (dump.size() < bufferSize)
        return dump;
    const size_t keep = bufferSize > 4 ? bufferSize - 4 : 0;
    return RepairUtf8(dump.substr(0, keep)) + "...";
}

} // namespace Haisos::Jq