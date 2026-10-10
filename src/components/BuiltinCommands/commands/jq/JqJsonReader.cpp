#include "commands/jq/JqJsonReader.h"

#include <cmath>
#include <cstring>

#include "commands/jq/JqUtf8.h"

namespace Haisos::Jq {
namespace {

bool IsWhitespaceByte(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

// The bytes that end a bare word or number.
bool IsTokenDelimiter(char c) {
    return IsWhitespaceByte(c) || c == '"' || c == '[' || c == ']' ||
           c == '{' || c == '}' || c == ',' || c == ':';
}

int HexDigitValue(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

const char kSurrogateMessage[] = "Invalid \\uXXXX\\uXXXX surrogate pair escape";

} // namespace

JsonReader::JsonReader(std::vector<Value>& out) : m_out(out) {}

bool JsonReader::InString(State s) { return s >= State::StringText; }

JsonReader::Delivery JsonReader::DeliveryOf(State s) {
    switch (s) {
        case State::TopValue: return Delivery::Top;
        case State::ArrayFirst:
        case State::ArrayElement: return Delivery::ArrayElement;
        case State::ObjectFirst:
        case State::ObjectKey: return Delivery::ObjectKey;
        case State::ObjectValue: return Delivery::ObjectValue;
        default: return Delivery::Separator;
    }
}

bool JsonReader::Fail(const std::string& message, bool atEof) {
    m_error = message;
    if (atEof)
        m_error += " at EOF";
    m_error += " at line " + std::to_string(m_line) + ", column " +
               std::to_string(m_column);
    m_failed = true;
    return false;
}

bool JsonReader::Feed(std::string_view bytes) {
    if (m_failed)
        return false;
    for (const char c : bytes) {
        if (c == '\n') {
            ++m_line;
            m_column = 0;
        } else {
            ++m_column;
        }
        if (!ConsumeByte(c))
            return false;
    }
    return true;
}

bool JsonReader::ConsumeByte(char c) {
    if (m_state == State::Token) {
        if (!IsTokenDelimiter(c)) {
            m_token += c;
            return true;
        }
        // The delimiter is consumed (it is counted already); the token is
        // checked and delivered, and the delimiter then taken afresh in
        // the state the delivery left. A top-level value is only jq's once
        // the delimiter after it is accepted: an erroring one takes it
        // back, as `1,` shows.
        const size_t outBefore = m_out.size();
        if (!FinishToken(false))
            return false;
        if (!Dispatch(c)) {
            if (m_out.size() > outBefore)
                m_out.pop_back();
            return false;
        }
        return true;
    }
    if (InString(m_state))
        return ConsumeStringByte(c);
    return Dispatch(c);
}

// A value lands where the state it started in says it lands.
bool JsonReader::Deliver(const Value& value, Delivery delivery, bool atEof) {
    switch (delivery) {
        case Delivery::Top:
            m_out.push_back(value);
            m_state = State::TopValue;
            return true;
        case Delivery::ArrayElement:
            m_stack.back().elements.push_back(value);
            m_state = State::ArrayDone;
            return true;
        case Delivery::ObjectKey:
            m_stack.back().pendingKey = value;
            m_state = State::ObjectColon;
            return true;
        case Delivery::ObjectValue: {
            Frame& frame = m_stack.back();
            const std::string& key = frame.pendingKey.AsString();
            // A repeated key keeps its first place and takes the last
            // value, as jq reads it.
            for (ObjectEntry& entry : frame.members) {
                if (entry.first == key) {
                    entry.second = value;
                    m_state = State::ObjectDone;
                    return true;
                }
            }
            frame.members.emplace_back(key, value);
            m_state = State::ObjectDone;
            return true;
        }
        case Delivery::Separator:
            return Fail("Expected separator between values", atEof);
    }
    return false;
}

bool JsonReader::CloseContainer() {
    Frame frame = std::move(m_stack.back());
    m_stack.pop_back();
    const Value closed = frame.isArray
        ? Value::Array(std::move(frame.elements))
        : Value::Object(std::move(frame.members));
    return Deliver(closed, frame.delivery, false);
}

// A structural byte in a state that is not Token and not inside a string.
bool JsonReader::Dispatch(char c) {
    if (IsWhitespaceByte(c))
        return true;
    switch (c) {
        case '[':
        case '{': {
            const Delivery delivery = DeliveryOf(m_state);
            if (delivery == Delivery::Separator)
                return Fail("Expected separator between values", false);
            if (m_stack.size() >= 256)
                return Fail("Exceeds depth limit for parsing", false);
            m_stack.emplace_back();
            m_stack.back().isArray = c == '[';
            m_stack.back().delivery = delivery;
            m_state = c == '[' ? State::ArrayFirst : State::ObjectFirst;
            return true;
        }
        case ',': {
            switch (m_state) {
                case State::ArrayDone:
                    m_state = State::ArrayElement;
                    return true;
                case State::ObjectDone:
                    m_state = State::ObjectKey;
                    return true;
                case State::ObjectColon:
                    return Fail("Objects must consist of key:value pairs", false);
                default:
                    return Fail("Expected value before ','", false);
            }
        }
        case ':': {
            switch (m_state) {
                case State::ObjectColon: {
                    // Any type was held as the key; the ':' is where jq
                    // checks that it is a string.
                    if (m_stack.back().pendingKey.GetKind() != Kind::String)
                        return Fail("Object keys must be strings", false);
                    m_state = State::ObjectValue;
                    return true;
                }
                case State::ArrayDone:
                case State::ObjectDone:
                    return Fail("':' not as part of an object", false);
                default:
                    return Fail("Expected string key before ':'", false);
            }
        }
        case ']': {
            switch (m_state) {
                case State::ArrayFirst:
                case State::ArrayDone:
                    return CloseContainer();
                case State::ArrayElement:
                    return Fail("Expected another array element", false);
                default:
                    return Fail("Unmatched ']'", false);
            }
        }
        case '}': {
            switch (m_state) {
                case State::ObjectFirst:
                case State::ObjectDone:
                    return CloseContainer();
                case State::ObjectKey:
                    return Fail("Expected another key-value pair", false);
                case State::ArrayDone:
                case State::ObjectColon:
                    return Fail("Objects must consist of key:value pairs", false);
                default:
                    return Fail("Unmatched '}'", false);
            }
        }
        case '"':
            m_delivery = DeliveryOf(m_state);
            m_content.clear();
            m_problem.clear();
            m_hexCount = 0;
            m_hexValue = 0;
            m_hexBad = false;
            m_state = State::StringText;
            return true;
        default:
            m_delivery = DeliveryOf(m_state);
            m_token.assign(1, c);
            m_state = State::Token;
            return true;
    }
}

bool JsonReader::EndHexEscape() {
    if (m_hexBad) {
        if (m_problem.empty())
            m_problem = "Invalid characters in \\uXXXX escape";
        m_state = State::StringText;
        return true;
    }
    if (m_hexValue >= 0xD800 && m_hexValue <= 0xDBFF) {
        m_highSurrogate = m_hexValue;
        m_state = State::StringSurrogate;
        return true;
    }
    if (m_hexValue >= 0xDC00 && m_hexValue <= 0xDFFF)
        AppendUtf8(m_content, 0xFFFD);  // a low surrogate on its own
    else
        AppendUtf8(m_content, m_hexValue);
    m_state = State::StringText;
    return true;
}

bool JsonReader::EndSurrogateHex() {
    if (!m_hexBad && m_hexValue >= 0xDC00 && m_hexValue <= 0xDFFF) {
        AppendUtf8(m_content,
                   0x10000 + ((m_highSurrogate - 0xD800) << 10) + (m_hexValue - 0xDC00));
    } else if (m_problem.empty()) {
        m_problem = kSurrogateMessage;
    }
    m_state = State::StringText;
    return true;
}

// The bytes inside a "...". A string's first problem in scan order is
// remembered and reported when the string closes; some problems (the ones
// a '"' runs into) close the string right there.
bool JsonReader::ConsumeStringByte(char c) {
    switch (m_state) {
        case State::StringText:
            if (c == '"')
                return CloseString();
            if (c == '\\') {
                m_state = State::StringEscape;
                return true;
            }
            if (static_cast<unsigned char>(c) < 0x20 && m_problem.empty()) {
                m_problem = "Invalid string: control characters from "
                            "U+0000 through U+001F must be escaped";
            }
            m_content += c;
            return true;
        case State::StringEscape:
            switch (c) {
                case '"': m_content += '"'; break;
                case '\\': m_content += '\\'; break;
                case '/': m_content += '/'; break;
                case 'b': m_content += '\b'; break;
                case 'f': m_content += '\f'; break;
                case 'n': m_content += '\n'; break;
                case 'r': m_content += '\r'; break;
                case 't': m_content += '\t'; break;
                case 'u':
                    m_hexCount = 0;
                    m_hexValue = 0;
                    m_hexBad = false;
                    m_state = State::StringHex;
                    return true;
                default:
                    if (m_problem.empty())
                        m_problem = "Invalid escape";
                    break;
            }
            m_state = State::StringText;
            return true;
        case State::StringHex:
            if (c == '"') {
                // The four digits never came: the quote ends it here.
                if (m_problem.empty())
                    m_problem = "Invalid \\uXXXX escape";
                return Fail(m_problem, false);
            }
            if (HexDigitValue(c) < 0)
                m_hexBad = true;
            else
                m_hexValue = m_hexValue * 16 + static_cast<unsigned int>(HexDigitValue(c));
            if (++m_hexCount == 4)
                return EndHexEscape();
            return true;
        case State::StringSurrogate:
            if (c == '\\') {
                m_state = State::StringSurrogateU;
                return true;
            }
            if (c == '"') {
                if (m_problem.empty())
                    m_problem = kSurrogateMessage;
                return Fail(m_problem, false);
            }
            if (m_problem.empty())
                m_problem = kSurrogateMessage;
            m_content += c;
            m_state = State::StringText;
            return true;
        case State::StringSurrogateU:
            if (c == 'u') {
                m_hexCount = 0;
                m_hexValue = 0;
                m_hexBad = false;
                m_state = State::StringSurrogateHex;
                return true;
            }
            if (m_problem.empty())
                m_problem = kSurrogateMessage;
            m_content += c;
            m_state = State::StringText;
            return true;
        case State::StringSurrogateHex:
            if (c == '"') {
                if (m_problem.empty())
                    m_problem = kSurrogateMessage;
                return Fail(m_problem, false);
            }
            if (HexDigitValue(c) < 0)
                m_hexBad = true;
            else
                m_hexValue = m_hexValue * 16 + static_cast<unsigned int>(HexDigitValue(c));
            if (++m_hexCount == 4)
                return EndSurrogateHex();
            return true;
        default:
            return true;
    }
}

bool JsonReader::CloseString() {
    if (!m_problem.empty())
        return Fail(m_problem, false);
    const Value value = Value::String(RepairUtf8(m_content));
    return Deliver(value, m_delivery, false);
}

// A bare word: true/false/null, a number, or one of jq's nan/inf forms.
// False when it is none of them, with m_invalidTokenMessage set.
bool JsonReader::ClassifyToken(const std::string& token, Value& out) {
    if (token == "true") {
        out = Value::Boolean(true);
        return true;
    }
    if (token == "false") {
        out = Value::Boolean(false);
        return true;
    }
    if (token == "null") {
        out = Value::Null();
        return true;
    }
    if (token[0] == 't' || token[0] == 'f' ||
        (token[0] == 'n' && token.size() >= 2 && token[1] == 'u')) {
        m_invalidTokenMessage = "Invalid literal";
        return false;
    }
    std::string canonical;
    double value = 0.0;
    if (CanonicalNumberLiteral(token, canonical, value)) {
        out = Value::NumberLiteral(value, canonical);
        return true;
    }
    // jq's nan and infinity, either sign, any case: nan reads as null, the
    // infinities as the largest number jq prints.
    size_t i = 0;
    if (token[i] == '+' || token[i] == '-')
        ++i;
    const bool negative = token[0] == '-';
    const std::string word = token.substr(i);
    const auto EqualsCaseInsensitive = [&word](const char* literal) {
        if (word.size() != std::strlen(literal))
            return false;
        for (size_t k = 0; k < word.size(); ++k) {
            char lower = word[k];
            if (lower >= 'A' && lower <= 'Z')
                lower = static_cast<char>(lower - 'A' + 'a');
            if (lower != literal[k])
                return false;
        }
        return true;
    };
    if (EqualsCaseInsensitive("nan")) {
        out = Value::Null();
        return true;
    }
    if (EqualsCaseInsensitive("inf") || EqualsCaseInsensitive("infinity")) {
        out = Value::Number(negative ? -1.7976931348623157e308
                                     : 1.7976931348623157e308);
        return true;
    }
    m_invalidTokenMessage = "Invalid numeric literal";
    return false;
}

bool JsonReader::FinishToken(bool atEof) {
    Value value;
    if (!ClassifyToken(m_token, value))
        return Fail(m_invalidTokenMessage, atEof);
    return Deliver(value, m_delivery, atEof);
}

bool JsonReader::Finish() {
    if (m_failed)
        return false;
    if (InString(m_state))
        return Fail("Unfinished string", true);
    if (m_state == State::Token && !FinishToken(true))
        return false;
    if (m_state != State::TopValue || !m_stack.empty())
        return Fail("Unfinished JSON term", true);
    return true;
}

const std::string& JsonReader::Error() const { return m_error; }

bool ParseSingleJson(std::string_view bytes, Value& out, std::string& error) {
    std::vector<Value> values;
    JsonReader reader(values);
    if (!reader.Feed(bytes) || !reader.Finish()) {
        error = reader.Error();
        return false;
    }
    if (values.empty()) {
        error = "Expected JSON value";
        return false;
    }
    if (values.size() > 1) {
        error = "Unexpected extra JSON values";
        return false;
    }
    out = values[0];
    return true;
}

} // namespace Haisos::Jq