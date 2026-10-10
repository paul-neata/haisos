#include <gtest/gtest.h>
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>
#include "commands/jq/JqJsonWriter.h"
#include "commands/jq/JqValue.h"

namespace {

using Haisos::Jq::CanonicalNumberLiteral;
using Haisos::Jq::Compare;
using Haisos::Jq::CompareDecimalLiterals;
using Haisos::Jq::Equal;
using Haisos::Jq::Kind;
using Haisos::Jq::Value;

Value Num(double d) { return Value::Number(d); }
Value Lit(double d, const char* text) {
    std::string canonical;
    double parsed = 0.0;
    if (!CanonicalNumberLiteral(text, canonical, parsed))
        return Value::NumberLiteral(d, text);
    return Value::NumberLiteral(parsed, canonical);
}

std::string Dump(const Value& v) {
    // The compact rendering, only for the assertions below.
    Haisos::Jq::WriteOptions compact;
    compact.indent = 0;
    std::string out;
    Haisos::Jq::WriteJson(v, compact, out);
    return out;
}

TEST(JqValueTest, ObjectsKeepInsertionOrder) {
    Value o = Value::Object({
        {"b", Num(1)},
        {"a", Num(2)},
        {"c", Num(3)},
    });
    // A repeated key keeps its first place and takes the last value.
    o = o.WithMember("a", Num(9));
    o = o.WithMember("d", Num(4));
    std::string keys;
    for (const auto& entry : o.AsObject())
        keys += entry.first;
    EXPECT_EQ(keys, "bacd");
    EXPECT_EQ(o.Find("a")->AsNumber(), 9.0);
    // Removing then adding appends.
    o = o.WithoutMember("a");
    o = o.WithMember("a", Num(5));
    keys.clear();
    for (const auto& entry : o.AsObject())
        keys += entry.first;
    EXPECT_EQ(keys, "bcda");
}

TEST(JqValueTest, CompareFollowsJqOrder) {
    std::vector<Value> values = {
        Value::Null(),
        Value::Boolean(true),
        Value::Boolean(false),
        Num(0),
        Num(-1),
        Value::String("a"),
        Value::String("B"),
        Value::Array({Num(1)}),
        Value::Array({Num(0), Num(5)}),
        Value::Object({{"b", Num(1)}}),
        Value::Object({{"a", Num(2)}}),
        Value::Object({{"a", Num(1)}, {"b", Num(0)}}),
    };
    std::sort(values.begin(), values.end(),
                  [](const Value& a, const Value& b) { return Compare(a, b) < 0; });
    std::string joined;
    for (const Value& v : values) {
        joined += Dump(v);
        joined += ";";
    }
    EXPECT_EQ(joined,
              "null;false;true;-1;0;\"B\";\"a\";[0,5];[1];"
              "{\"a\":2};{\"a\":1,\"b\":0};{\"b\":1};");
}

TEST(JqValueTest, CompareObjectsSortedKeysFirst) {
    std::vector<Value> values = {
        Value::Object({{"a", Value::Array({Num(1)})}}),
        Value::Object({{"a", Value::Array({Num(0), Num(5)})}}),
        Value::Object({{"a", Num(1)}, {"b", Num(0)}}),
        Value::Object({{"b", Num(1)}}),
        Value::Object({{"a", Num(2)}}),
    };
    std::sort(values.begin(), values.end(),
                  [](const Value& a, const Value& b) { return Compare(a, b) < 0; });
    // The sorted key list decides first ("a" < "a,b" < "b"), then the
    // values taken in sorted key order.
    ASSERT_EQ(values.size(), 5u);
    EXPECT_EQ(values[0].Find("a")->AsNumber(), 2.0);
    EXPECT_EQ(values[1].Find("a")->AsArray().size(), 2u);
    EXPECT_EQ(values[2].Find("a")->AsArray().size(), 1u);
    EXPECT_TRUE(values[3].Find("a") != nullptr);
    EXPECT_TRUE(values[4].Find("b") != nullptr);
}

TEST(JqValueTest, CompareNumbers) {
    const double nan = std::nan("");
    EXPECT_EQ(Compare(Value::Number(nan), Value::Number(nan)), -1);
    // NaN is a number in jq's order, before every other number (jq's
    // `[null, nan, false, 0] | sort` is null, false, NaN, 0).
    EXPECT_EQ(Compare(Value::Number(nan), Value::Null()), 1);
    EXPECT_EQ(Compare(Value::Number(nan), Num(0)), -1);
    EXPECT_TRUE(Equal(Lit(1.0, "1.0"), Lit(1.0, "1")));
    EXPECT_TRUE(Equal(Lit(1.1, "1.10"), Lit(1.1, "1.1")));
    // Two big literals compare exactly as decimals; the double is the
    // same, so a computed number equal to them.
    const Value big1 = Lit(1e20, "100000000000000000001");
    const Value big2 = Lit(1e20, "100000000000000000000");
    EXPECT_EQ(Compare(big1, big2), 1);
    EXPECT_EQ(Compare(big2, big1), -1);
    EXPECT_TRUE(Equal(big1, Num(1e20)));
    EXPECT_TRUE(Equal(big2, Num(1e20)));
}

TEST(JqValueTest, CompareDecimalLiteralsTable) {
    EXPECT_EQ(CompareDecimalLiterals("1.0", "1"), 0);
    EXPECT_EQ(CompareDecimalLiterals("1.10", "1.1"), 0);
    EXPECT_EQ(CompareDecimalLiterals("-0", "0"), 0);
    EXPECT_EQ(CompareDecimalLiterals("1", "1.0000000001"), -1);
    EXPECT_EQ(CompareDecimalLiterals("1E+2", "100"), 0);
    EXPECT_EQ(CompareDecimalLiterals("1.5E+3", "1500"), 0);
    EXPECT_EQ(CompareDecimalLiterals("-1", "1"), -1);
    EXPECT_EQ(CompareDecimalLiterals("0", "-1"), 1);
}

void CheckCanonical(const char* text, const char* want, double wantValue) {
    std::string canonical;
    double value = 0.0;
    ASSERT_TRUE(CanonicalNumberLiteral(text, canonical, value))
        << text;
    EXPECT_EQ(canonical, want) << text;
    EXPECT_DOUBLE_EQ(value, wantValue) << text;
}

TEST(JqValueTest, CanonicalLiterals) {
    CheckCanonical("1", "1", 1.0);
    CheckCanonical("1.0", "1.0", 1.0);
    CheckCanonical("1e2", "1E+2", 100.0);
    CheckCanonical("1E2", "1E+2", 100.0);
    CheckCanonical("1.5e-3", "0.0015", 0.0015);
    CheckCanonical(".5", "0.5", 0.5);
    CheckCanonical("01", "1", 1.0);
    CheckCanonical("-0", "-0", 0.0);
    CheckCanonical("-1.50", "-1.50", -1.5);
    CheckCanonical("00.", "0", 0.0);
    CheckCanonical("1.2e10", "1.2E+10", 1.2e10);
    CheckCanonical("1e-7", "1E-7", 1e-7);
    CheckCanonical("100000000000000000001", "100000000000000000001", 1e20);
    // An adjusted exponent above 999999999 keeps no literal.
    {
        std::string canonical;
        double value = 0.0;
        ASSERT_TRUE(CanonicalNumberLiteral("1e1000000000", canonical, value));
        EXPECT_TRUE(canonical.empty());
    }
    // Not numbers.
    std::string canonical;
    double value = 0.0;
    EXPECT_FALSE(CanonicalNumberLiteral("1e", canonical, value));
    EXPECT_FALSE(CanonicalNumberLiteral("--1", canonical, value));
    EXPECT_FALSE(CanonicalNumberLiteral(".", canonical, value));
    EXPECT_FALSE(CanonicalNumberLiteral("0x10", canonical, value));
    EXPECT_FALSE(CanonicalNumberLiteral("1.2.3", canonical, value));
    EXPECT_FALSE(CanonicalNumberLiteral("-", canonical, value));
    EXPECT_FALSE(CanonicalNumberLiteral("+", canonical, value));
    EXPECT_FALSE(CanonicalNumberLiteral("1.e", canonical, value));
    EXPECT_FALSE(CanonicalNumberLiteral(".e1", canonical, value));
    EXPECT_FALSE(CanonicalNumberLiteral("", canonical, value));
}

// The rest of the plan's literal pairs, checked by their canonical text
// alone (the double is not part of what jq prints for a literal).
void CheckCanonicalText(const char* text, const std::string& want) {
    std::string canonical;
    double value = 0.0;
    ASSERT_TRUE(CanonicalNumberLiteral(text, canonical, value)) << text;
    EXPECT_EQ(canonical, want) << text;
}

TEST(JqValueTest, CanonicalLiteralTexts) {
    CheckCanonicalText("1.000", "1.000");
    CheckCanonicalText("1.50", "1.50");
    CheckCanonicalText("1E01", "1E+1");
    CheckCanonicalText("1.e1", "1E+1");
    CheckCanonicalText("100000000000000000000", "100000000000000000000");
    CheckCanonicalText("1E1000", "1E+1000");
    CheckCanonicalText("0.00001", "0.00001");
    CheckCanonicalText("3.141592653589793238", "3.141592653589793238");
    CheckCanonicalText("-00", "-0");
    CheckCanonicalText("-0.0", "-0.0");
    CheckCanonicalText("0.0e0", "0.0");
    CheckCanonicalText("1E-6", "0.000001");
    CheckCanonicalText("0.1e-6", "1E-7");
    CheckCanonicalText("12.3400", "12.3400");
    CheckCanonicalText("00", "0");
    CheckCanonicalText("1.", "1");
    CheckCanonicalText("-.5", "-0.5");
    CheckCanonicalText("+.5e-1", "0.05");
    CheckCanonicalText("+1", "1");
    CheckCanonicalText("1.5e3", "1.5E+3");
    CheckCanonicalText("0E5", "0E+5");
    // A 1000-digit integer is printed whole.
    const std::string thousand = "1" + std::string(999, '0');
    CheckCanonicalText(thousand.c_str(), thousand);
}

TEST(JqValueTest, CopiesShareTheirPayload) {
    const Value a = Value::Array({Num(1), Num(2)});
    const Value b = a;
    EXPECT_EQ(&a.AsArray(), &b.AsArray());
    const Value o = Value::Object({{"k", Value::String("v")}});
    const Value p = o;
    EXPECT_EQ(&o.AsObject(), &p.AsObject());
    const Value s = Value::String("text");
    const Value t = s;
    EXPECT_EQ(&s.AsString(), &t.AsString());
    // A copy with one change keeps the rest shared: its untouched element
    // is a new Value sharing the same payload.
    const Value arr = Value::Array({Num(1), Value::String("v")});
    const Value c = arr.WithElement(0, Num(9));
    EXPECT_EQ(&c.AsArray()[1].AsString(), &arr.AsArray()[1].AsString());
}

TEST(JqValueTest, KindAndTruthy) {
    EXPECT_EQ(Value::Null().GetKind(), Kind::Null);
    EXPECT_EQ(Value::Boolean(false).GetKind(), Kind::False);
    EXPECT_EQ(Value::Boolean(true).GetKind(), Kind::True);
    EXPECT_EQ(Num(0).GetKind(), Kind::Number);
    EXPECT_EQ(Value::String("").GetKind(), Kind::String);
    EXPECT_EQ(Value::Array({}).GetKind(), Kind::Array);
    EXPECT_EQ(Value::Object({}).GetKind(), Kind::Object);
    EXPECT_FALSE(Value::Null().IsTruthy());
    EXPECT_FALSE(Value::Boolean(false).IsTruthy());
    EXPECT_TRUE(Value::Boolean(true).IsTruthy());
    EXPECT_TRUE(Num(0).IsTruthy());
    EXPECT_TRUE(Value::String("").IsTruthy());
    EXPECT_TRUE(Value::Array({}).IsTruthy());
    EXPECT_TRUE(Value::Object({}).IsTruthy());
}

} // namespace