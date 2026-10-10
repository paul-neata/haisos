#include <gtest/gtest.h>
#include <string>
#include "commands/jq/JqJsonWriter.h"
#include "commands/jq/JqValue.h"

namespace {

using Haisos::Jq::DumpTruncated;
using Haisos::Jq::FormatNumber;
using Haisos::Jq::QuoteJsonString;
using Haisos::Jq::Value;
using Haisos::Jq::WriteJson;
using Haisos::Jq::WriteOptions;

Value Num(double d) { return Value::Number(d); }

std::string Write(const Value& v, WriteOptions options) {
    std::string out;
    WriteJson(v, options, out);
    return out;
}

TEST(JqJsonWriter, ComputedNumbers) {
    // jq prints a computed number as its shortest round-trip form, plain
    // while the exponent stays in range, scientific beyond.
    EXPECT_EQ(FormatNumber(Num(1e15)), "1000000000000000");
    EXPECT_EQ(FormatNumber(Num(1e16)), "1e+16");
    EXPECT_EQ(FormatNumber(Num(12e15)), "12000000000000000");
    EXPECT_EQ(FormatNumber(Num(12e16)), "1.2e+17");
    EXPECT_EQ(FormatNumber(Num(0.0001)), "0.0001");
    EXPECT_EQ(FormatNumber(Num(0.001)), "0.001");
    EXPECT_EQ(FormatNumber(Num(0.00001)), "1e-05");
    EXPECT_EQ(FormatNumber(Num(2e-7)), "2e-07");
    EXPECT_EQ(FormatNumber(Num(1.0 / 3.0)), "0.3333333333333333");
    EXPECT_EQ(FormatNumber(Num(0.1 + 0.2)), "0.30000000000000004");
    EXPECT_EQ(FormatNumber(Num(0.0 * -1)), "-0");
    EXPECT_EQ(FormatNumber(Num(3.0)), "3");
    EXPECT_EQ(FormatNumber(Num(1000000000000000.5)), "1000000000000000.5");
    EXPECT_EQ(FormatNumber(Num(5e-324)), "5e-324");
    // jq prints the infinities as the largest double it formats, and NaN
    // as null.
    EXPECT_EQ(FormatNumber(Num(1e300 * 1e300)), "1.7976931348623157e+308");
    EXPECT_EQ(FormatNumber(Num(0.0 / 0.0)), "null");
    // A literal keeps its own canonical text.
    std::string canonical;
    double parsed = 0.0;
    ASSERT_TRUE(Haisos::Jq::CanonicalNumberLiteral("1.0", canonical, parsed));
    EXPECT_EQ(FormatNumber(Value::NumberLiteral(parsed, canonical)), "1.0");
}

TEST(JqJsonWriter, PrettyAndCompact) {
    Value doc = Value::Array({
        Num(1),
        Value::Array({Num(2),
            Value::Object({{"a", Value::Array({})}, {"b", Value::Object({})}})}),
        Value::Object({}),
        Value::String("x"),
    });
    EXPECT_EQ(Write(doc, WriteOptions()),
              "[\n  1,\n  [\n    2,\n    {\n      \"a\": [],\n      \"b\": {}\n"
              "    }\n  ],\n  {},\n  \"x\"\n]");
    WriteOptions one;
    one.indent = 1;
    EXPECT_EQ(Write(Value::Array({Num(1), Value::Object({{"a", Value::Array({})}})}), one),
              "[\n 1,\n {\n  \"a\": []\n }\n]");
    WriteOptions three;
    three.indent = 3;
    EXPECT_EQ(Write(Value::Array({Num(1), Value::Object({{"a", Value::Array({})}})}), three),
              "[\n   1,\n   {\n      \"a\": []\n   }\n]");
    WriteOptions tab;
    tab.tab = true;
    EXPECT_EQ(Write(Value::Object({{"a", Value::Array({Num(1)})}}), tab),
              "{\n\t\"a\": [\n\t\t1\n\t]\n}");
    WriteOptions compact;
    compact.indent = 0;
    EXPECT_EQ(Write(Value::Object({{"a", Num(1)}, {"b", Value::Array({Num(2)})}}), compact),
              "{\"a\":1,\"b\":[2]}");
    // Empty containers stay inline even when pretty.
    EXPECT_EQ(Write(Value::Array({Value::Object({})}), WriteOptions()),
              "[\n  {}\n]");
}

TEST(JqJsonWriter, SortKeys) {
    WriteOptions sorted;
    sorted.sortKeys = true;
    sorted.indent = 0;
    Value nested = Value::Object({
        {"b", Value::Object({{"d", Num(1)}, {"c", Num(2)}})},
        {"a", Value::Array({})},
    });
    EXPECT_EQ(Write(nested, sorted), "{\"a\":[],\"b\":{\"c\":2,\"d\":1}}");
    // Sorted byte by byte, so the ASCII names come before the accented one.
    Value unicode = Value::Object({
        {"\xc3\xa9", Num(1)}, {"z", Num(2)}, {"Z", Num(3)}, {"aa", Num(4)},
        {"a", Num(5)}, {"", Num(6)},
        {"b", Value::Array({Value::Object({{"y", Num(1)}, {"x", Num(2)}})})},
    });
    EXPECT_EQ(Write(unicode, sorted),
              "{\"\":6,\"Z\":3,\"a\":5,\"aa\":4,\"b\":[{\"x\":2,\"y\":1}],"
              "\"z\":2,\"\xc3\xa9\":1}");
    // Unsorted keeps the insertion order.
    EXPECT_EQ(Write(nested, WriteOptions()),
              "{\"b\":{\"d\":1,\"c\":2},\"a\":[]}");
}

TEST(JqJsonWriter, StringEscapes) {
    EXPECT_EQ(QuoteJsonString("\b\f\n\r\t\"\\", false),
              "\"\\b\\f\\n\\r\\t\\\"\\\\\"");
    EXPECT_EQ(QuoteJsonString("\x01\x1f\x7f", false),
              "\"\\u0001\\u001f\\u007f\"");
    // Not ASCII mode: raw UTF-8 kept as it is.
    EXPECT_EQ(QuoteJsonString("\xc2\x80\xc2\x9f\xe2\x80\xa8", false),
              "\"\xc2\x80\xc2\x9f\xe2\x80\xa8\"");
    // ASCII mode: every code point escaped, a pair above the BMP as its
    // surrogate pair.
    EXPECT_EQ(QuoteJsonString("\xc3\xa9\xf0\x9f\x98\x80", true),
              "\"\\u00e9\\ud83d\\ude00\"");
    // A slash is never escaped.
    EXPECT_EQ(QuoteJsonString("a/b", false), "\"a/b\"");
}

TEST(JqJsonWriter, Colours) {
    WriteOptions colour;
    colour.indent = 0;
    colour.color = true;
    EXPECT_EQ(Write(Value::Object({{"a", Value::Null()}}), colour),
              "\x1b[1;39m{\x1b[0m\x1b[1;34m\"a\"\x1b[0m\x1b[1;39m:\x1b[0m"
              "\x1b[0;90mnull\x1b[0m\x1b[1;39m\x1b[1;39m}\x1b[0m");
    EXPECT_EQ(Write(Value::Array({Num(1), Num(2)}), colour),
              "\x1b[1;39m[\x1b[0;39m1\x1b[0m\x1b[1;39m,\x1b[0;39m2\x1b[0m"
              "\x1b[1;39m\x1b[1;39m]\x1b[0m");
    WriteOptions colourPretty;
    colourPretty.color = true;
    EXPECT_EQ(Write(Value::Array({Value::Array({})}), colourPretty),
              "\x1b[1;39m[\n  \x1b[1;39m[]\x1b[0m\x1b[1;39m\n\x1b[1;39m]\x1b[0m");
    Value mixed = Value::Object({
        {"a", Value::Array({Num(1), Value::String("x"), Value::Null(),
                            Value::Boolean(true), Value::Boolean(false),
                            Value::Object({})})},
        {"b", Value::Array({})},
    });
    EXPECT_EQ(Write(mixed, colourPretty),
              "\x1b[1;39m{\n  \x1b[0m\x1b[1;34m\"a\"\x1b[0m\x1b[1;39m: \x1b[0m"
              "\x1b[1;39m[\n    \x1b[0;39m1\x1b[0m\x1b[1;39m,\n    "
              "\x1b[0;32m\"x\"\x1b[0m\x1b[1;39m,\n    \x1b[0;90mnull\x1b[0m\x1b[1;39m,\n    "
              "\x1b[0;39mtrue\x1b[0m\x1b[1;39m,\n    \x1b[0;39mfalse\x1b[0m\x1b[1;39m,\n    "
              "\x1b[1;39m{}\x1b[0m\x1b[1;39m\n  \x1b[1;39m]\x1b[0m\x1b[1;39m,\n  "
              "\x1b[0m\x1b[1;34m\"b\"\x1b[0m\x1b[1;39m: \x1b[0m\x1b[1;39m[]\x1b[0m"
              "\x1b[1;39m\n\x1b[1;39m}\x1b[0m");
}

TEST(JqJsonWriter, DumpTruncated) {
    EXPECT_EQ(DumpTruncated(Value::String("abcdefghijkl"), 15),
              "\"abcdefghijkl\"");
    // A cut lands before the "..." and never inside a UTF-8 sequence: the
    // bytes before it are repaired first.
    EXPECT_EQ(DumpTruncated(Value::String("abcdefghijklm"), 15),
              "\"abcdefghij...");
    EXPECT_EQ(DumpTruncated(Value::String("a\xc3\xa9\xe2\x82\xac\xf0\x9f\x98\x80""abcde"), 15),
              "\"a\xc3\xa9\xe2\x82\xac\xf0\x9f\x98\x80...");
    EXPECT_EQ(DumpTruncated(Value::String("a\xc3\xa9\xe2\x82\xac\xe2\x82\xac\xe2\x82\xac""zz"), 15),
              "\"a\xc3\xa9\xe2\x82\xac\xe2\x82\xac\xef\xbf\xbd...");
    EXPECT_EQ(DumpTruncated(Value::Object({{"file", Value::String("<top-level>")},
                                          {"line", Num(1)}}), 30),
              "{\"file\":\"<top-level>\",\"lin...");
}

} // namespace