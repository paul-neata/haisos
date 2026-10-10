#include <gtest/gtest.h>
#include <string>
#include <vector>
#include "commands/jq/JqJsonReader.h"
#include "commands/jq/JqJsonWriter.h"
#include "commands/jq/JqValue.h"

namespace {

using Haisos::Jq::JsonReader;
using Haisos::Jq::ParseSingleJson;
using Haisos::Jq::Value;
using Haisos::Jq::WriteJson;
using Haisos::Jq::WriteOptions;

std::string Dump(const Value& v) {
    WriteOptions compact;
    compact.indent = 0;
    std::string out;
    WriteJson(v, compact, out);
    return out;
}

// Feeds |text| in pieces of at most |pieceSize| bytes, then finishes.
// Returns false on error, filling |error|.
bool Read(const std::string& text, std::vector<Value>& values,
          std::string& error, size_t pieceSize = 1000000) {
    values.clear();
    error.clear();
    JsonReader reader(values);
    for (size_t i = 0; i < text.size(); i += pieceSize) {
        if (!reader.Feed(std::string_view(text).substr(i, pieceSize))) {
            error = reader.Error();
            return false;
        }
    }
    if (!reader.Finish()) {
        error = reader.Error();
        return false;
    }
    return true;
}

std::string ReadOne(const std::string& text) {
    std::vector<Value> values;
    std::string error;
    Read(text, values, error);
    std::string out;
    for (const Value& v : values) {
        out += Dump(v);
        out += "\n";
    }
    if (!error.empty())
        out += "ERR|" + error + "\n";
    return out;
}

TEST(JqJsonReader, ReadsSeveralValues) {
    // One value after another, the input fed in small pieces.
    std::vector<Value> values;
    std::string error;
    ASSERT_TRUE(Read("1 2 {\"a\":1}{\"a\":2}\n[3]", values, error, 3)) << error;
    ASSERT_EQ(values.size(), 5u);
    EXPECT_EQ(Dump(values[0]), "1");
    EXPECT_EQ(Dump(values[1]), "2");
    EXPECT_EQ(Dump(values[2]), "{\"a\":1}");
    EXPECT_EQ(Dump(values[3]), "{\"a\":2}");
    EXPECT_EQ(Dump(values[4]), "[3]");
}

TEST(JqJsonReader, KeepsLiterals) {
    EXPECT_EQ(ReadOne("[1.0, 1e2, -0]"), "[1.0,1E+2,-0]\n");
    EXPECT_EQ(ReadOne("100000000000000000001"),
              "100000000000000000001\n");
    EXPECT_EQ(ReadOne("1e999999999"), "1E+999999999\n");
    EXPECT_EQ(ReadOne("1e-999999999"), "1E-999999999\n");
    // Too large an exponent for a literal: the double decides.
    EXPECT_EQ(ReadOne("1e1000000000"), "1.7976931348623157e+308\n");
}

TEST(JqJsonReader, ObjectKeys) {
    // A repeated key keeps its first place and takes the last value.
    EXPECT_EQ(ReadOne("{\"a\":1,\"b\":2,\"a\":3}"), "{\"a\":3,\"b\":2}\n");
}

TEST(JqJsonReader, ErrorMessages) {
    EXPECT_EQ(ReadOne(","),
              "ERR|Expected value before ',' at line 1, column 1\n");
    EXPECT_EQ(ReadOne("1,"),  // the value is not jq's: the ',' was refused
              "ERR|Expected value before ',' at line 1, column 2\n");
    EXPECT_EQ(ReadOne("[1,]"),
              "ERR|Expected another array element at line 1, column 4\n");
    EXPECT_EQ(ReadOne("[1 true]"),
              "ERR|Expected separator between values at line 1, column 8\n");
    EXPECT_EQ(ReadOne("{a:1}"),  // 'a' is a bare word, not a string key
              "ERR|Invalid numeric literal at line 1, column 3\n");
    EXPECT_EQ(ReadOne(":\n"),
              "ERR|Expected string key before ':' at line 1, column 1\n");
    EXPECT_EQ(ReadOne(":\"a\""),
              "ERR|Expected string key before ':' at line 1, column 1\n");
    // A non-string key reaches the ':' as a value; the ':' checks it.
    EXPECT_EQ(ReadOne("{1:2}"),
              "ERR|Object keys must be strings at line 1, column 3\n");
    EXPECT_EQ(ReadOne("{[1]:2}"),
              "ERR|Object keys must be strings at line 1, column 5\n");
    // A value with no ':' after it is an unfinished pair.
    EXPECT_EQ(ReadOne("{1}"),
              "ERR|Objects must consist of key:value pairs at line 1, column 3\n");
    EXPECT_EQ(ReadOne("{\"a\":1 \"b\":2}"),
              "ERR|Expected separator between values at line 1, column 10\n");
    EXPECT_EQ(ReadOne("{\"a\":1,}"),
              "ERR|Expected another key-value pair at line 1, column 8\n");
    EXPECT_EQ(ReadOne("{\"a\":1]"),
              "ERR|Unmatched ']' at line 1, column 7\n");
    EXPECT_EQ(ReadOne("]"), "ERR|Unmatched ']' at line 1, column 1\n");
    EXPECT_EQ(ReadOne("}"), "ERR|Unmatched '}' at line 1, column 1\n");
    EXPECT_EQ(ReadOne("nullx"),
              "ERR|Invalid literal at EOF at line 1, column 5\n");
    EXPECT_EQ(ReadOne("tru"), "ERR|Invalid literal at EOF at line 1, column 3\n");
    EXPECT_EQ(ReadOne("0x10"),
              "ERR|Invalid numeric literal at EOF at line 1, column 4\n");
    // An unfinished value takes the whole input's position with it.
    EXPECT_EQ(ReadOne("[[1"),
              "ERR|Unfinished JSON term at EOF at line 1, column 3\n");
    EXPECT_EQ(ReadOne("[[1]"),  // the inner array delivered, the outer open
              "ERR|Unfinished JSON term at EOF at line 1, column 4\n");
    EXPECT_EQ(ReadOne("{\"a\":"),  // column 5: the input's end
              "ERR|Unfinished JSON term at EOF at line 1, column 5\n");
    EXPECT_EQ(ReadOne("[1 x]"),
              "ERR|Invalid numeric literal at line 1, column 5\n");
}

TEST(JqJsonReader, StringErrors) {
    EXPECT_EQ(ReadOne("\"a\\\""),  // the quote escaped: the string never closes
              "ERR|Unfinished string at EOF at line 1, column 4\n");
    EXPECT_EQ(ReadOne("\"\\u12\""),
              "ERR|Invalid \\uXXXX escape at line 1, column 6\n");
    EXPECT_EQ(ReadOne("\"\\uZZZZ\""),
              "ERR|Invalid characters in \\uXXXX escape at line 1, column 8\n");
    EXPECT_EQ(ReadOne("\"\\q\""), "ERR|Invalid escape at line 1, column 4\n");
    EXPECT_EQ(ReadOne("\"\\q\\u12\""),
              "ERR|Invalid escape at line 1, column 8\n");
    // A raw control byte is a problem the moment it is seen, and the
    // string reports its first problem when it closes.
    EXPECT_EQ(ReadOne("\"\x01\\q\""),
              "ERR|Invalid string: control characters from U+0000 through "
              "U+001F must be escaped at line 1, column 5\n");
    // A high surrogate waits for its low half; the first byte after it
    // that is not a '\\' settles the question, the closing quote included.
    EXPECT_EQ(ReadOne("\"\\ud800A\""),
              "ERR|Invalid \\uXXXX\\uXXXX surrogate pair escape at line 1, column 9\n");
    EXPECT_EQ(ReadOne("\"\\ud800\\ud800\""),
              "ERR|Invalid \\uXXXX\\uXXXX surrogate pair escape at line 1, column 14\n");
    EXPECT_EQ(ReadOne("\"\\ud800\\\\x\""),
              "ERR|Invalid \\uXXXX\\uXXXX surrogate pair escape at line 1, column 11\n");
    EXPECT_EQ(ReadOne("\"\\ud800\\\"x\""),
              "ERR|Invalid \\uXXXX\\uXXXX surrogate pair escape at line 1, column 11\n");
    // A lone low surrogate reads as the replacement character.
    EXPECT_EQ(ReadOne("\"\\udc00\""), "\"\xef\xbf\xbd\"\n");
}

TEST(JqJsonReader, LiteralTokens) {
    EXPECT_EQ(ReadOne("nan"), "null\n");
    EXPECT_EQ(ReadOne("NaN"), "null\n");
    EXPECT_EQ(ReadOne("Infinity"), "1.7976931348623157e+308\n");
    EXPECT_EQ(ReadOne("-Infinity"), "-1.7976931348623157e+308\n");
    EXPECT_EQ(ReadOne("+inf"), "1.7976931348623157e+308\n");
    EXPECT_EQ(ReadOne("true"), "true\n");
    EXPECT_EQ(ReadOne("false"), "false\n");
    EXPECT_EQ(ReadOne("null"), "null\n");
    EXPECT_EQ(ReadOne("01"), "1\n");
    EXPECT_EQ(ReadOne(".5"), "0.5\n");
    EXPECT_EQ(ReadOne("1."), "1\n");
    EXPECT_EQ(ReadOne("+.5e-1"), "0.05\n");
}

TEST(JqJsonReader, InvalidUtf8Repaired) {
    // Every ill-formed sequence becomes one replacement character, the
    // bytes around it kept; a sequence cut short by the closing quote too.
    EXPECT_EQ(ReadOne("\"\xff" "a\xc3\""),
              "\"\xef\xbf\xbd" "a\xef\xbf\xbd\"\n");
    EXPECT_EQ(ReadOne("\"\xe2\x82" "a\xf0\x9f\x98\""),
              "\"\xef\xbf\xbd" "a\xef\xbf\xbd\"\n");
    // A well-formed sequence is kept, a surrogate encoded as UTF-8 is not.
    EXPECT_EQ(ReadOne("\"\xc3\xa9\""), "\"\xc3\xa9\"\n");
    EXPECT_EQ(ReadOne("\"\xed\xa0\x80\""), "\"\xef\xbf\xbd\"\n");
    EXPECT_EQ(ReadOne("\"\xf0\x9f\x98\x80\""), "\"\xf0\x9f\x98\x80\"\n");
}

TEST(JqJsonReader, LineCountsAcrossFeeds) {
    EXPECT_EQ(ReadOne("{\"a\":1\n\"b\":2}"),
              "ERR|Expected separator between values at line 2, column 3\n");
    EXPECT_EQ(ReadOne("{\n\"a\":1}\n{"),
              "{\"a\":1}\nERR|Unfinished JSON term at EOF at line 3, column 1\n");
}

TEST(JqJsonReader, DepthLimit) {
    // 256 levels parse; the 257th is refused.
    const std::string deep256 = std::string(256, '[') + "1" + std::string(256, ']');
    std::vector<Value> values;
    std::string error;
    ASSERT_TRUE(Read(deep256, values, error)) << error;
    ASSERT_EQ(values.size(), 1u);
    EXPECT_EQ(Dump(values[0]).substr(0, 10), "[[[[[[[[[[");
    EXPECT_EQ(ReadOne(std::string(257, '[')),
              "ERR|Exceeds depth limit for parsing at line 1, column 257\n");
    EXPECT_EQ(ReadOne(std::string(257, '[') + "1" + std::string(257, ']')),
              "ERR|Exceeds depth limit for parsing at line 1, column 257\n");
}

TEST(JqJsonReader, ParseSingle) {
    Value out;
    std::string error;
    ASSERT_TRUE(ParseSingleJson("[1,2]", out, error)) << error;
    EXPECT_EQ(Dump(out), "[1,2]");
    ASSERT_TRUE(ParseSingleJson(" 1 ", out, error)) << error;
    EXPECT_EQ(Dump(out), "1");
    EXPECT_FALSE(ParseSingleJson("", out, error));
    EXPECT_EQ(error, "Expected JSON value");
    EXPECT_FALSE(ParseSingleJson("1 2", out, error));
    EXPECT_EQ(error, "Unexpected extra JSON values");
    // The reader's message passes through.
    EXPECT_FALSE(ParseSingleJson("x", out, error));
    EXPECT_EQ(error, "Invalid numeric literal at EOF at line 1, column 1");
    EXPECT_FALSE(ParseSingleJson("{\"a\":1", out, error));
    EXPECT_EQ(error, "Unfinished JSON term at EOF at line 1, column 6");
}

} // namespace