#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "commands/jq/JqValue.h"

namespace Haisos::Jq {

// Reads JSON from a stream of bytes, one value after another, byte by byte:
// no recursion, a container in progress on a stack. jq's own extensions are
// accepted (bare `nan`/`inf`/`infinity`, numbers like `.5` or `01`, any
// value type as an object key until the ':' checks it), and every mistake
// is jq's message, byte for byte: "<message>[ at EOF] at line L, column C".
// A value is appended to |out| as soon as it is whole; the values read
// before a later error stay there.
class JsonReader {
public:
    explicit JsonReader(std::vector<Value>& out);

    // Feeds the next bytes. Returns false when the input holds an error;
    // the reader then keeps the error and refuses further work.
    bool Feed(std::string_view bytes);
    // Ends the input. Returns false on error.
    bool Finish();
    // jq's error text, empty while there is none.
    const std::string& Error() const;

private:
    enum class State {
        TopValue,
        ArrayFirst,
        ArrayElement,
        ArrayDone,
        ObjectFirst,
        ObjectKey,
        ObjectColon,
        ObjectValue,
        ObjectDone,
        Token,
        StringText,
        StringEscape,
        StringHex,
        StringSurrogate,
        StringSurrogateU,
        StringSurrogateHex,
    };

    // Where a finished value lands, taken from the state it started in.
    enum class Delivery {
        Top,           // a top-level value: done, to the output
        ArrayElement,  // an element of the innermost array
        ObjectKey,     // an object's key (checked at the ':')
        ObjectValue,   // an object member's value
        Separator,     // after an element: an error, a separator was due
    };

    // A container being read. The finished container is delivered to
    // |delivery|; an object holds its members and the key waiting for
    // its ':' -- and, from kMemberIndexThreshold members on, a
    // key-to-position map, so a large object's repeated keys are found
    // without scanning every member.
    struct Frame {
        bool isArray = true;
        std::vector<Value> elements;
        std::vector<ObjectEntry> members;
        std::unique_ptr<std::unordered_map<std::string, size_t>> memberIndex;
        Value pendingKey;
        Delivery delivery = Delivery::Top;
    };

    static Delivery DeliveryOf(State s);
    static bool InString(State s);

    bool ConsumeByte(char c);
    bool Dispatch(char c);
    bool ConsumeStringByte(char c);
    bool EndHexEscape();
    bool EndSurrogateHex();
    bool CloseString();
    bool CloseContainer();
    bool FinishToken(bool atEof);
    bool Deliver(const Value& value, Delivery delivery, bool atEof);
    bool Fail(const std::string& message, bool atEof);
    bool ClassifyToken(const std::string& token, Value& out);

    std::vector<Value>& m_out;
    std::vector<Frame> m_stack;
    State m_state = State::TopValue;
    Delivery m_delivery = Delivery::Top;  // where the token/string/value in
                                          // progress lands
    std::string m_token;                  // the bare word or number so far
    std::string m_content;                // the string's bytes so far
    std::string m_problem;                // a string's first problem, if any
    unsigned int m_hexValue = 0;          // the \uXXXX digits so far
    int m_hexCount = 0;                   // how many of the 4 are read
    bool m_hexBad = false;                // a non-hex byte among them
    unsigned int m_highSurrogate = 0;     // the \uD800-DBFF half read
    std::string m_invalidTokenMessage;    // "Invalid literal" or
                                          // "Invalid numeric literal"
    long m_line = 1;
    long m_column = 0;
    std::string m_error;
    bool m_failed = false;
};

// One JSON value from |bytes|, as jq's fromjson reads it: |out| gets the
// value. Returns false on error, with |error| holding the reader's message
// -- or "Expected JSON value" when there is no value, "Unexpected extra
// JSON values" when there are several.
bool ParseSingleJson(std::string_view bytes, Value& out, std::string& error);

} // namespace Haisos::Jq