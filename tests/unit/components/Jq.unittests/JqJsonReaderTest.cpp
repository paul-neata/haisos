#include <gtest/gtest.h>
#include <cmath>
#include <string>
#include <vector>
#include "commands/jq/JqJsonReader.h"
#include "commands/jq/JqJsonWriter.h"
#include "commands/jq/JqUtf8.h"
#include "commands/jq/JqValue.h"

namespace {

using Haisos::Jq::JsonReader;
using Haisos::Jq::Kind;
using Haisos::Jq::ParseSingleJson;
using Haisos::Jq::RepairUtf8;
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

TEST(JqJsonReaderTest, ReadsSeveralValues) {
    // One value after another, the input fed in small pieces of every size.
    for (size_t pieceSize = 1; pieceSize <= 3; ++pieceSize) {
        std::vector<Value> values;
        std::string error;
        ASSERT_TRUE(Read("1 2 {\"a\":1}{\"a\":2}\n[3]", values, error, pieceSize))
            << error << " (piece size " << pieceSize << ")";
        ASSERT_EQ(values.size(), 5u);
        EXPECT_EQ(Dump(values[0]), "1");
        EXPECT_EQ(Dump(values[1]), "2");
        EXPECT_EQ(Dump(values[2]), "{\"a\":1}");
        EXPECT_EQ(Dump(values[3]), "{\"a\":2}");
        EXPECT_EQ(Dump(values[4]), "[3]");
    }
    // One byte at a time: a value lands in the output exactly when its
    // last byte is fed -- a top-level scalar only at its delimiter.
    const std::string text = "1 2 {\"a\":1}{\"a\":2}\n3";
    const std::vector<size_t> countAfterByte = {
        0, 1, 1, 2, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3, 4, 4, 4,
    };
    ASSERT_EQ(countAfterByte.size(), text.size());
    std::vector<Value> values;
    JsonReader reader(values);
    for (size_t i = 0; i < text.size(); ++i) {
        ASSERT_TRUE(reader.Feed(std::string_view(text).substr(i, 1)));
        ASSERT_EQ(values.size(), countAfterByte[i]) << "after byte " << i;
    }
    // The trailing scalar has no delimiter yet: only the end of the input
    // makes it jq's.
    ASSERT_TRUE(reader.Finish());
    ASSERT_EQ(values.size(), 5u);
    EXPECT_EQ(Dump(values[4]), "3");
}

TEST(JqJsonReaderTest, KeepsLiterals) {
    EXPECT_EQ(ReadOne("[1.0, 1e2, -0]"), "[1.0,1E+2,-0]\n");
    EXPECT_EQ(ReadOne("100000000000000000001"),
              "100000000000000000001\n");
    EXPECT_EQ(ReadOne("1e999999999"), "1E+999999999\n");
    EXPECT_EQ(ReadOne("1e-999999999"), "1E-999999999\n");
    // Too large an exponent for a literal: the double decides.
    EXPECT_EQ(ReadOne("1e1000000000"), "1.7976931348623157e+308\n");
}

TEST(JqJsonReaderTest, ObjectKeys) {
    // A repeated key keeps its first place and takes the last value.
    EXPECT_EQ(ReadOne("{\"a\":1,\"b\":2,\"a\":3}"), "{\"a\":3,\"b\":2}\n");
}

TEST(JqJsonReaderTest, LargeObjectIndexedLookup) {
    // 100000 distinct keys, one of them repeated at the end: the repeated
    // key keeps its first place and takes the last value, and every key
    // is found afterwards (the frame's key index makes this quick; a
    // scan of every member for each would take minutes).
    const int count = 100000;
    std::string json = "{";
    for (int i = 0; i < count; ++i) {
        if (i)
            json += ',';
        json += "\"k" + std::to_string(i) + "\":" + std::to_string(i);
    }
    json += ",\"k0\":999}";
    std::vector<Value> values;
    std::string error;
    ASSERT_TRUE(Read(json, values, error)) << error;
    ASSERT_EQ(values.size(), 1u);
    const Value& object = values[0];
    ASSERT_EQ(object.AsObject().size(), 100000u);
    EXPECT_EQ(object.AsObject()[0].second.AsNumber(), 999);
    EXPECT_EQ(object.AsObject().back().first, "k99999");
    for (int i = 1; i < count; i += 997) {
        const std::string key = "k" + std::to_string(i);
        const Value* found = object.Find(key);
        ASSERT_TRUE(found);
        EXPECT_EQ(found->AsNumber(), i);
    }
    EXPECT_FALSE(object.Find("k100000"));
}

TEST(JqJsonReaderTest, ErrorMessages) {
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
    // The rest of the plan's table, table-driven: the expected string is
    // ReadOne's whole output -- the values read before the error, each on
    // its own line, then the error line.
    const std::vector<std::pair<std::string, std::string>> table = {
        {"[1,2", "ERR|Unfinished JSON term at EOF at line 1, column 4\n"},
        {"[1,2\n", "ERR|Unfinished JSON term at EOF at line 2, column 0\n"},
        {"{\"a\" 1}", "ERR|Expected separator between values at line 1, column 7\n"},
        {"[1] x", "[1]\nERR|Invalid numeric literal at EOF at line 1, column 5\n"},
        {"{\"a\":1}}", "{\"a\":1}\nERR|Unmatched '}' at line 1, column 8\n"},
        {"[1],", "[1]\nERR|Expected value before ',' at line 1, column 4\n"},
        {"\"abc", "ERR|Unfinished string at EOF at line 1, column 4\n"},
        {"\"a\nb\"",
         "ERR|Invalid string: control characters from U+0000 through "
         "U+001F must be escaped at line 2, column 2\n"},
        {"\"\\ud800\"",
         "ERR|Invalid \\uXXXX\\uXXXX surrogate pair escape at line 1, column 8\n"},
        {"\"\\ud800x\"",
         "ERR|Invalid \\uXXXX\\uXXXX surrogate pair escape at line 1, column 9\n"},
        {"{\"a\",1}", "ERR|Objects must consist of key:value pairs at line 1, column 5\n"},
        {"[1:2]", "ERR|':' not as part of an object at line 1, column 3\n"},
        {"{\"a\":}", "ERR|Unmatched '}' at line 1, column 6\n"},
        {"[true}", "ERR|Objects must consist of key:value pairs at line 1, column 6\n"},
        {"[-]", "ERR|Invalid numeric literal at line 1, column 3\n"},
    };
    for (const auto& row : table)
        EXPECT_EQ(ReadOne(row.first), row.second) << row.first;
}

TEST(JqJsonReaderTest, StringErrors) {
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

TEST(JqJsonReaderTest, LiteralTokens) {
    EXPECT_EQ(ReadOne("true"), "true\n");
    EXPECT_EQ(ReadOne("false"), "false\n");
    EXPECT_EQ(ReadOne("null"), "null\n");
    EXPECT_EQ(ReadOne("01"), "1\n");
    EXPECT_EQ(ReadOne(".5"), "0.5\n");
    EXPECT_EQ(ReadOne("1."), "1\n");
    EXPECT_EQ(ReadOne("+.5e-1"), "0.05\n");
    // jq's nan and infinity, either sign, any case.
    const std::vector<std::pair<const char*, const char*>> accepted = {
        {"nan", "null"},
        {"NaN", "null"},
        {"nAn", "null"},
        {"naN", "null"},
        {"-nan", "null"},
        {"-NaN", "null"},
        {"+NaN", "null"},
        {"Infinity", "1.7976931348623157e+308"},
        {"+inf", "1.7976931348623157e+308"},
        {"Inf", "1.7976931348623157e+308"},
        {"INFINITY", "1.7976931348623157e+308"},
        {"+infinity", "1.7976931348623157e+308"},
        {"-Infinity", "-1.7976931348623157e+308"},
        {"-inf", "-1.7976931348623157e+308"},
    };
    for (const auto& row : accepted)
        EXPECT_EQ(ReadOne(row.first), std::string(row.second) + "\n") << row.first;
    // Both are numbers: a NaN (printed null) and a real infinity.
    for (const char* text : {"nan", "NaN", "-nan"}) {
        std::vector<Value> values;
        std::string error;
        ASSERT_TRUE(Read(text, values, error)) << text;
        ASSERT_EQ(values.size(), 1u) << text;
        EXPECT_EQ(values[0].GetKind(), Kind::Number) << text;
        EXPECT_TRUE(std::isnan(values[0].AsNumber())) << text;
        EXPECT_EQ(Dump(values[0]), "null") << text;
    }
    const std::pair<const char*, bool> infinities[] = {
        {"Infinity", true}, {"+inf", true}, {"-Infinity", false},
    };
    for (const auto& row : infinities) {
        std::vector<Value> values;
        std::string error;
        ASSERT_TRUE(Read(row.first, values, error)) << row.first;
        ASSERT_EQ(values.size(), 1u) << row.first;
        EXPECT_EQ(values[0].GetKind(), Kind::Number) << row.first;
        ASSERT_TRUE(std::isinf(values[0].AsNumber())) << row.first;
        EXPECT_EQ(values[0].AsNumber() > 0, row.second) << row.first;
        EXPECT_EQ(Dump(values[0]),
                  row.second ? "1.7976931348623157e+308"
                             : "-1.7976931348623157e+308")
            << row.first;
    }
    // Every invalid bare word: a word starting t/f/nu is an invalid
    // literal, anything else an invalid numeric literal.
    const std::vector<std::pair<const char*, const char*>> invalid = {
        {"t", "Invalid literal at EOF at line 1, column 1"},
        {"tru", "Invalid literal at EOF at line 1, column 3"},
        {"truex", "Invalid literal at EOF at line 1, column 5"},
        {"fals", "Invalid literal at EOF at line 1, column 4"},
        {"nu", "Invalid literal at EOF at line 1, column 2"},
        {"nul", "Invalid literal at EOF at line 1, column 3"},
        {"null2", "Invalid literal at EOF at line 1, column 5"},
        {"nuLL", "Invalid literal at EOF at line 1, column 4"},
        {"nul1", "Invalid literal at EOF at line 1, column 4"},
        {"n", "Invalid numeric literal at EOF at line 1, column 1"},
        {"na", "Invalid numeric literal at EOF at line 1, column 2"},
        {"nax", "Invalid numeric literal at EOF at line 1, column 3"},
        {"nil", "Invalid numeric literal at EOF at line 1, column 3"},
        {"nUll", "Invalid numeric literal at EOF at line 1, column 4"},
        {"Null", "Invalid numeric literal at EOF at line 1, column 4"},
        {"True", "Invalid numeric literal at EOF at line 1, column 4"},
        {"F", "Invalid numeric literal at EOF at line 1, column 1"},
        {"nanx", "Invalid numeric literal at EOF at line 1, column 4"},
        {"nan1", "Invalid numeric literal at EOF at line 1, column 4"},
        {"Infinite", "Invalid numeric literal at EOF at line 1, column 8"},
        {"infinityx", "Invalid numeric literal at EOF at line 1, column 9"},
        {"1e5x", "Invalid numeric literal at EOF at line 1, column 4"},
        {"1e", "Invalid numeric literal at EOF at line 1, column 2"},
        {"1e+", "Invalid numeric literal at EOF at line 1, column 3"},
        {"--1", "Invalid numeric literal at EOF at line 1, column 3"},
        {"0x10", "Invalid numeric literal at EOF at line 1, column 4"},
        {"1.2.3", "Invalid numeric literal at EOF at line 1, column 5"},
        {".", "Invalid numeric literal at EOF at line 1, column 1"},
        {"x", "Invalid numeric literal at EOF at line 1, column 1"},
        {"-", "Invalid numeric literal at EOF at line 1, column 1"},
        {"+", "Invalid numeric literal at EOF at line 1, column 1"},
        {"1-2", "Invalid numeric literal at EOF at line 1, column 3"},
    };
    for (const auto& row : invalid)
        EXPECT_EQ(ReadOne(row.first), "ERR|" + std::string(row.second) + "\n")
            << row.first;
}

TEST(JqJsonReaderTest, InvalidUtf8Repaired) {
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

TEST(JqJsonReaderTest, LineCountsAcrossFeeds) {
    EXPECT_EQ(ReadOne("{\"a\":1\n\"b\":2}"),
              "ERR|Expected separator between values at line 2, column 3\n");
    EXPECT_EQ(ReadOne("{\n\"a\":1}\n{"),
              "{\"a\":1}\nERR|Unfinished JSON term at EOF at line 3, column 1\n");
    // The line and column really carry from one Feed to the next.
    std::vector<Value> values;
    JsonReader reader(values);
    ASSERT_TRUE(reader.Feed("{\"a\":1}\n"));
    ASSERT_EQ(values.size(), 1u);
    ASSERT_TRUE(reader.Feed("{"));
    EXPECT_FALSE(reader.Finish());
    EXPECT_EQ(reader.Error(), "Unfinished JSON term at EOF at line 2, column 1");
}

// Every ill-formed sequence one U+FFFD, the bytes around it kept.
TEST(JqJsonReaderTest, RepairUtf8Sequences) {
    const std::string R = "\xef\xbf\xbd";  // U+FFFD
    const std::vector<std::pair<std::string, std::string>> cases = {
        {"a\xc0\xaf" "b", "a" + R + R + "b"},
        {"a\x80\x80" "b", "a" + R + R + "b"},
        {"a\xf5\x80\x80\x80" "b", "a" + R + R + R + R + "b"},
        {"a\xf8\x88\x80\x80\x80" "b", "a" + R + R + R + R + R + "b"},
        {"a\xfe" "b", "a" + R + "b"},
        {"a\xe0\x80\x80" "b", "a" + R + "b"},
        {"a\xed\xa0\x80" "b", "a" + R + "b"},
        {"a\xf4\x90\x80\x80" "b", "a" + R + "b"},
        {"a\xe2\x82" "b", "a" + R + "b"},
        {"a\xc3\xc3\xa9" "b", "a" + R + "\xc3\xa9" "b"},
        {"a\xe2\x82\xe2\x82\xac" "b", "a" + R + "\xe2\x82\xac" "b"},
        // A sequence cut short by the end of the input.
        {"a\xf0\x9f\x98", "a" + R},
        // Well-formed sequences are kept as they are.
        {"a\xc3\xa9\xe2\x82\xac\xf0\x9f\x98\x80" "b",
         "a\xc3\xa9\xe2\x82\xac\xf0\x9f\x98\x80" "b"},
    };
    for (const auto& row : cases)
        EXPECT_EQ(RepairUtf8(row.first), row.second);
}

TEST(JqJsonReaderTest, DepthLimit) {
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

TEST(JqJsonReaderTest, ParseSingle) {
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