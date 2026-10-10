#include <gtest/gtest.h>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <vector>
#include "BuiltinCommand.h"
#include "BuiltinCommandList.h"
#include "ProcessFileIO.h"
#include "commands/awk/AwkError.h"
#include "commands/awk/AwkFields.h"
#include "commands/awk/AwkInput.h"
#include "commands/awk/AwkValue.h"
#include "interfaces/IProcess.h"
#include "tests/mocks/MockFileDescriptor.h"

using namespace Haisos;

namespace {

// The conversion format awk defaults to.
const char* kConvFmt = "%.6g";

// A process that exists only to hold a descriptor table, so a BuiltinContext
// can be driven directly (as BuiltinCommandsTest.cpp's BuiltinContextTest
// tests do).
class FakeProcess : public ICurrentProcess {
public:
    explicit FakeProcess(std::shared_ptr<ProcessFileIO> io) : m_io(std::move(io)) {}

    uint64_t GetPid() const override { return 1; }
    uint64_t GetParentPid() const override { return 0; }
    std::string Path() const override { return "/fake"; }
    std::string StartingAgentName() const override { return std::string(); }
    std::shared_ptr<IEnvironment> GetEnvironment() const override { return nullptr; }
    void TriggerStop() override {}
    bool WaitToFinish(uint64_t) override { return true; }
    std::optional<int> ExitCode() const override { return 0; }
    void StopForBrokenPipe() override {}

    std::shared_ptr<IFileIO> IO() const override { return m_io; }
    std::shared_ptr<IAgent> AsAgent() override { return nullptr; }
    std::shared_ptr<IHaisosOS> OS() const override { return nullptr; }

private:
    std::shared_ptr<ProcessFileIO> m_io;
};

// The command object of that name from the standard set, or null.
std::shared_ptr<IBuiltinCommand> FindStandardCommand(const std::string& name) {
    for (auto& command : CreateStandardBuiltinCommands()) {
        if (command->Name() == name) {
            return command;
        }
    }
    return nullptr;
}

// A FieldStore whose splitter is SplitAwkFields, as the interpreter's will
// be until awk--records' regexes arrive.
Awk::FieldStore MakeStore() {
    return Awk::FieldStore([](std::string_view record, const std::string& fs, bool /*paragraphMode*/,
                              std::vector<std::string>& fields) {
        if (!Awk::SplitAwkFields(record, fs, fields)) {
            throw Awk::AwkFatal("field separator is not a single byte");
        }
    });
}

// A RecordReader over a MockFileDescriptor fed by the test, with its
// BuiltinContext and its stop flag.
class ReaderHarness {
public:
    ReaderHarness() {
        auto out = std::make_shared<Mocks::MockFileDescriptor>();
        auto err = std::make_shared<Mocks::MockFileDescriptor>();
        m_io->InstallStandardStreams(m_input, out, err);
        m_process = std::make_unique<FakeProcess>(m_io);
        m_awk = FindStandardCommand("awk");
        m_context = std::make_unique<BuiltinContext>(*m_process, *m_awk, m_args, m_stop);
        m_reader = std::make_unique<Awk::RecordReader>(*m_context, m_input);
    }

    Awk::RecordReadResult Next(const std::string& rs, std::string& record) {
        return m_reader->Next(rs, record);
    }
    void Feed(const std::string& bytes) { m_input->Feed(bytes); }
    void EndInput() { m_input->EndInput(); }
    void RequestStop() { m_stop.store(true); }

private:
    std::atomic<bool> m_stop{false};
    std::vector<std::string> m_args;
    std::shared_ptr<ProcessFileIO> m_io = ProcessFileIO::Create({}, "/");
    std::shared_ptr<Mocks::MockFileDescriptor> m_input =
        std::make_shared<Mocks::MockFileDescriptor>();
    std::unique_ptr<FakeProcess> m_process;
    std::shared_ptr<IBuiltinCommand> m_awk;
    std::unique_ptr<BuiltinContext> m_context;
    std::unique_ptr<Awk::RecordReader> m_reader;
};

double NanWithSign(bool negative) {
    const uint64_t bits = negative ? 0xFFF8000000000000ull : 0x7FF8000000000000ull;
    double value = 0.0;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

} // namespace

// --- StringToNumber / LooksNumeric ---

TEST(AwkValueTest, StringToNumber) {
    EXPECT_EQ(Awk::StringToNumber("3x"), 3.0);
    EXPECT_EQ(Awk::StringToNumber(" 12 "), 12.0);
    EXPECT_EQ(Awk::StringToNumber("0x1A"), 26.0);
    EXPECT_EQ(Awk::StringToNumber("1e3x"), 1000.0);
    EXPECT_EQ(Awk::StringToNumber(".e1"), 0.0);
    EXPECT_EQ(Awk::StringToNumber("+"), 0.0);
    EXPECT_EQ(Awk::StringToNumber(""), 0.0);
    EXPECT_TRUE(std::isnan(Awk::StringToNumber("nancy")));
    EXPECT_TRUE(std::isinf(Awk::StringToNumber("info")));
    EXPECT_GT(Awk::StringToNumber("info"), 0.0);
    EXPECT_TRUE(std::isinf(Awk::StringToNumber("-inf")));
    EXPECT_LT(Awk::StringToNumber("-inf"), 0.0);
}

TEST(AwkValueTest, LooksNumeric) {
    double number = 0.0;
    EXPECT_TRUE(Awk::LooksNumeric("0x11", number));
    EXPECT_EQ(number, 17.0);
    EXPECT_TRUE(Awk::LooksNumeric("+inf", number));
    EXPECT_TRUE(std::isinf(number));
    EXPECT_TRUE(Awk::LooksNumeric(" 12 ", number));
    EXPECT_EQ(number, 12.0);
    EXPECT_TRUE(Awk::LooksNumeric("nan", number));
    EXPECT_TRUE(std::isnan(number));
    EXPECT_TRUE(Awk::LooksNumeric("1e400", number));
    EXPECT_TRUE(std::isinf(number));
    EXPECT_GT(number, 0.0);

    EXPECT_FALSE(Awk::LooksNumeric(" -3.5e2x", number));
    EXPECT_FALSE(Awk::LooksNumeric(".5.", number));
    EXPECT_FALSE(Awk::LooksNumeric("1e", number));
    EXPECT_FALSE(Awk::LooksNumeric("", number));
    EXPECT_FALSE(Awk::LooksNumeric("a", number));
}

// --- AwkNumberToString / FormatAwkNumber ---

TEST(AwkValueTest, NumberToString) {
    EXPECT_EQ(Awk::AwkNumberToString(1e30, kConvFmt), "1000000000000000019884624838656");
    EXPECT_EQ(Awk::AwkNumberToString(-1e30, kConvFmt), "-1000000000000000019884624838656");
    EXPECT_EQ(Awk::AwkNumberToString(18446744073709551616.0, kConvFmt), "18446744073709551616");
    EXPECT_EQ(Awk::AwkNumberToString(0.1 + 0.2, kConvFmt), "0.3");
    EXPECT_EQ(Awk::AwkNumberToString(1e-5, kConvFmt), "1e-05");
    EXPECT_EQ(Awk::AwkNumberToString(123456.7, kConvFmt), "123457");
    EXPECT_EQ(Awk::AwkNumberToString(1234567.8, kConvFmt), "1.23457e+06");
    EXPECT_EQ(Awk::AwkNumberToString(-0.0, kConvFmt), "0");
    EXPECT_EQ(Awk::AwkNumberToString(2147483648.0, kConvFmt), "2147483648");
    EXPECT_EQ(Awk::AwkNumberToString(-9223372036854775808.0, kConvFmt),
              "-9223372036854775808");
    EXPECT_EQ(Awk::AwkNumberToString(1e6 * 1e6, kConvFmt), "1000000000000");
    EXPECT_EQ(Awk::AwkNumberToString(100.0 / 3, kConvFmt), "33.3333");
    EXPECT_EQ(Awk::AwkNumberToString(HUGE_VAL, kConvFmt), "+inf");
    EXPECT_EQ(Awk::AwkNumberToString(-HUGE_VAL, kConvFmt), "-inf");
    EXPECT_EQ(Awk::AwkNumberToString(NanWithSign(false), kConvFmt), "+nan");
    EXPECT_EQ(Awk::AwkNumberToString(NanWithSign(true), kConvFmt), "-nan");
    EXPECT_EQ(Awk::AwkNumberToString(3.14159, "%.2f"), "3.14");
    EXPECT_EQ(Awk::AwkNumberToString(2.5, "%d"), "2");
    EXPECT_EQ(Awk::AwkNumberToString(255.5, "%x"), "ff");
    EXPECT_EQ(Awk::AwkNumberToString(3.14159, "x=%.1f%%"), "x=3.1%");
}

TEST(AwkValueTest, FormatAwkNumberCopiesWhatItDoesNotApply) {
    // A conversion not applied to a number is copied as written; so is a
    // format that ends inside a specification.
    EXPECT_EQ(Awk::FormatAwkNumber("%q", 3.5), "%q");
    EXPECT_EQ(Awk::FormatAwkNumber("abc", 3.5), "abc");
    EXPECT_EQ(Awk::FormatAwkNumber("%5", 3.5), "%5");
    EXPECT_EQ(Awk::FormatAwkNumber("a%%b", 3.5), "a%b");
}
// --- CompareValues ---

TEST(AwkValueTest, Comparisons) {
    using Awk::Value;
    const Value number1 = Value::FromNumber(1);
    const Value number10 = Value::FromNumber(10);
    const Value number2 = Value::FromNumber(2);
    const Value numberNaN = Value::FromNumber(NanWithSign(false));
    const Value string10 = Value::FromString("10");
    const Value string9 = Value::FromString("9");
    const Value strnum10 = Value::FromInput("10");
    const Value strnum9 = Value::FromInput("9");
    const Value strnumNaN = Value::FromInput("nan");
    const Value number5 = Value::FromNumber(5);
    const Value nothing;
    const Value empty = Value::FromString("");

    EXPECT_EQ(Awk::CompareValues(number1, Value::FromNumber(1.0), kConvFmt), 0);
    EXPECT_LT(Awk::CompareValues(string10, string9, kConvFmt), 0);
    EXPECT_GT(Awk::CompareValues(strnum10, strnum9, kConvFmt), 0);
    // A strnum against a string is a string comparison.
    EXPECT_LT(Awk::CompareValues(strnum10, string9, kConvFmt), 0);
    EXPECT_EQ(Awk::CompareValues(nothing, Value::FromNumber(0), kConvFmt), 0);
    EXPECT_EQ(Awk::CompareValues(nothing, empty, kConvFmt), 0);
    EXPECT_LT(Awk::CompareValues(number2, number10, kConvFmt), 0);
    // A NaN on either side: the two are unordered.
    EXPECT_EQ(Awk::CompareValues(numberNaN, numberNaN, kConvFmt), Awk::kAwkUnordered);
    EXPECT_EQ(Awk::CompareValues(numberNaN, number1, kConvFmt), Awk::kAwkUnordered);
    EXPECT_EQ(Awk::CompareValues(strnumNaN, number5, kConvFmt), Awk::kAwkUnordered);
}

// --- Value::ToBoolean ---

TEST(AwkValueTest, Truth) {
    EXPECT_FALSE(Awk::Value::FromNumber(0).ToBoolean());
    EXPECT_TRUE(Awk::Value::FromString("0").ToBoolean());
    EXPECT_FALSE(Awk::Value::FromInput("0").ToBoolean());
    EXPECT_FALSE(Awk::Value::FromString("").ToBoolean());
    EXPECT_FALSE(Awk::Value().ToBoolean());
    EXPECT_TRUE(Awk::Value::FromInput(" 1 ").ToBoolean());
}

// --- AwkArray ---

TEST(AwkValueTest, ArrayKeepsInsertionOrder) {
    Awk::AwkArray array;
    array.GetOrCreate("z") = Awk::Value::FromNumber(1);
    array.GetOrCreate("a") = Awk::Value::FromNumber(2);
    array.GetOrCreate("m") = Awk::Value::FromNumber(3);
    EXPECT_EQ(array.Keys(), (std::vector<std::string>{"z", "a", "m"}));

    array.Remove("a");
    array.GetOrCreate("a") = Awk::Value::FromNumber(4);
    EXPECT_EQ(array.Keys(), (std::vector<std::string>{"z", "m", "a"}));
    EXPECT_EQ(array.Find("a")->ToNumber(), 4.0);

    EXPECT_TRUE(array.Contains("z"));
    EXPECT_FALSE(array.Contains("nope"));
    EXPECT_EQ(array.Size(), 3u);
    EXPECT_EQ(array.Find("nope"), nullptr);

    // A reference stays valid whatever is inserted after it.
    Awk::Value& held = array.GetOrCreate("held");
    held = Awk::Value::FromNumber(42);
    for (int i = 0; i < 1000; ++i) {
        array.GetOrCreate("k" + std::to_string(i));
    }
    EXPECT_EQ(held.ToString(kConvFmt), "42");
    held = Awk::Value::FromNumber(43);
    EXPECT_EQ(array.Find("held")->ToNumber(), 43.0);
    EXPECT_EQ(array.Size(), 1004u);

    array.Clear();
    EXPECT_EQ(array.Size(), 0u);
    EXPECT_TRUE(array.Keys().empty());
    EXPECT_FALSE(array.Contains("z"));
}

// --- SplitAwkFields ---

TEST(AwkValueTest, SplitFields) {
    std::vector<std::string> fields;
    EXPECT_TRUE(Awk::SplitAwkFields(" a  b\tc\n", " ", fields));
    EXPECT_EQ(fields, (std::vector<std::string>{"a", "b", "c"}));
    EXPECT_TRUE(Awk::SplitAwkFields("a:b::c", ":", fields));
    EXPECT_EQ(fields, (std::vector<std::string>{"a", "b", "", "c"}));
    EXPECT_TRUE(Awk::SplitAwkFields("a|b.c", "|", fields));
    EXPECT_EQ(fields, (std::vector<std::string>{"a", "b.c"}));
    EXPECT_TRUE(Awk::SplitAwkFields("abc", "", fields));
    EXPECT_EQ(fields, (std::vector<std::string>{"abc"}));
    EXPECT_TRUE(Awk::SplitAwkFields("", ":", fields));
    EXPECT_TRUE(fields.empty());
    fields = {"keep"};
    EXPECT_FALSE(Awk::SplitAwkFields("x", "ab", fields));
    EXPECT_EQ(fields, (std::vector<std::string>{"keep"}));
}

// --- FieldStore ---

TEST(AwkValueTest, FieldStoreAssignmentsRebuildRecord) {
    Awk::FieldStore store = MakeStore();

    // record ` a b `, $3 = "c" -> $0 `a b c`, NF 3.
    store.SetRecord(" a b ", " ");
    store.SetField(3, Awk::Value::FromString("c"), " ", kConvFmt);
    EXPECT_EQ(store.Record(kConvFmt), "a b c");
    EXPECT_EQ(store.NF(), 3u);
    // NF = 2 -> `a b`.
    store.SetNF(2, " ");
    EXPECT_EQ(store.Record(kConvFmt), "a b");
    EXPECT_EQ(store.NF(), 2u);
    // $5 = "e" -> `a b   e`, NF 5.
    store.SetField(5, Awk::Value::FromString("e"), " ", kConvFmt);
    EXPECT_EQ(store.Record(kConvFmt), "a b   e");
    EXPECT_EQ(store.NF(), 5u);

    // record `a b c`, OFS `:`, $7 = "z" -> `a:b:c::::z`, NF 7.
    store.SetRecord("a b c", " ");
    store.SetField(7, Awk::Value::FromString("z"), ":", kConvFmt);
    EXPECT_EQ(store.Record(kConvFmt), "a:b:c::::z");
    EXPECT_EQ(store.NF(), 7u);

    // record `a b c`, NF = 5 -> `a b c  `; then $2 = "" -> `a  c  `.
    store.SetRecord("a b c", " ");
    store.SetNF(5, " ");
    EXPECT_EQ(store.Record(kConvFmt), "a b c  ");
    EXPECT_EQ(store.NF(), 5u);
    store.SetField(2, Awk::Value::FromString(""), " ", kConvFmt);
    EXPECT_EQ(store.Record(kConvFmt), "a  c  ");

    // empty record, NF = 2 -> $0 is one space; then $2 = "b" -> ` b`.
    store.SetRecord("", " ");
    store.SetNF(2, " ");
    EXPECT_EQ(store.Record(kConvFmt), " ");
    store.SetField(2, Awk::Value::FromString("b"), " ", kConvFmt);
    EXPECT_EQ(store.Record(kConvFmt), " b");

    // $0 = "  x  " -> NF 1, $1 `x`.
    store.SetRecord("  x  ", " ");
    EXPECT_EQ(store.NF(), 1u);
    EXPECT_EQ(store.Field(1, kConvFmt).ToString(kConvFmt), "x");
    EXPECT_EQ(store.Field(0, kConvFmt).ToString(kConvFmt), "  x  ");
}

TEST(AwkValueTest, FieldStoreSplitsWithTheFsSavedWithTheRecord) {
    Awk::FieldStore store = MakeStore();
    store.SetRecord("a:b", " ");
    // The store splits with the FS given to SetRecord; it never sees the
    // caller's FS, so a record that came while FS was " " stays one field.
    EXPECT_EQ(store.NF(), 1u);
    EXPECT_EQ(store.Field(1, kConvFmt).ToString(kConvFmt), "a:b");
    // $0 = value re-splits with that same FS.
    store.SetField(0, Awk::Value::FromString("x:y"), " ", kConvFmt);
    EXPECT_EQ(store.NF(), 1u);
    EXPECT_EQ(store.Field(1, kConvFmt).ToString(kConvFmt), "x:y");
    EXPECT_EQ(store.Record(kConvFmt), "x:y");
    // Fields come from the record as input: strnum when they look numeric.
    store.SetRecord(" 12 x", " ");
    EXPECT_EQ(store.Field(1, kConvFmt).GetType(), Awk::Value::Type::StrNum);
    EXPECT_EQ(store.Field(2, kConvFmt).GetType(), Awk::Value::Type::String);
}

TEST(AwkValueTest, FieldStoreKeepsAssignedTypesAndConvertsNumbers) {
    Awk::FieldStore store = MakeStore();
    store.SetRecord("a b", " ");
    store.SetField(2, Awk::Value::FromNumber(3.5), " ", kConvFmt);
    // The field reads back as the number it was assigned.
    EXPECT_EQ(store.Field(2, kConvFmt).GetType(), Awk::Value::Type::Number);
    EXPECT_EQ(store.Field(2, kConvFmt).ToNumber(), 3.5);
    // $0 is rebuilt with the conversion format.
    EXPECT_EQ(store.Record(kConvFmt), "a 3.5");
    // Beyond NF the field is Uninitialized; the rebuilt $0 is a plain
    // string: only a record set by SetRecord is strnum, and a field
    // assignment made this one.
    EXPECT_EQ(store.Field(4, kConvFmt).GetType(), Awk::Value::Type::Uninitialized);
    EXPECT_EQ(store.Field(0, kConvFmt).GetType(), Awk::Value::Type::String);
    // A record that came from input and looks numeric: $0 is a strnum.
    store.SetRecord(" 12 ", " ");
    EXPECT_EQ(store.Field(0, kConvFmt).GetType(), Awk::Value::Type::StrNum);
}

TEST(AwkValueTest, FieldStoreRefusesNegativeIndexAndNF) {
    Awk::FieldStore store = MakeStore();
    store.SetRecord("a b", " ");
    try {
        (void)store.Field(-1, kConvFmt);
        FAIL() << "expected AwkFatal";
    } catch (const Awk::AwkFatal& error) {
        EXPECT_STREQ(error.what(), "attempt to access field -1");
        EXPECT_TRUE(error.WithLocation());
    }
    try {
        store.SetField(-2, Awk::Value::FromNumber(1), " ", kConvFmt);
        FAIL() << "expected AwkFatal";
    } catch (const Awk::AwkFatal& error) {
        EXPECT_STREQ(error.what(), "attempt to access field -2");
    }
    try {
        store.SetNF(-1, " ");
        FAIL() << "expected AwkFatal";
    } catch (const Awk::AwkFatal& error) {
        EXPECT_STREQ(error.what(), "NF set to negative value");
    }
}

TEST(AwkValueTest, FieldStoreRebuildsWithTheConvfmtOfTheRebuild) {
    Awk::FieldStore store = MakeStore();
    store.SetRecord("a b", " ");
    store.SetField(2, Awk::Value::FromNumber(3.5), " ", kConvFmt);
    // The CONVFMT of the first read after the assignment is what the rebuild
    // uses, and the rebuild happens once: a later change changes nothing.
    EXPECT_EQ(store.Record("%.2f"), "a 3.50");
    EXPECT_EQ(store.Record(kConvFmt), "a 3.50");
    // The rebuilt $0 is a plain string; a record set again is input once more.
    EXPECT_EQ(store.Field(0, kConvFmt).GetType(), Awk::Value::Type::String);
    store.SetRecord(" 12 ", " ");
    EXPECT_EQ(store.Field(0, kConvFmt).GetType(), Awk::Value::Type::StrNum);
    // $0 = <number> goes through SetRecord: the new record is input.
    store.SetField(0, Awk::Value::FromNumber(12), " ", kConvFmt);
    EXPECT_EQ(store.Field(0, kConvFmt).GetType(), Awk::Value::Type::StrNum);
}

TEST(AwkValueTest, FieldStoreRefusesTooManyFields) {
    Awk::FieldStore store = MakeStore();
    store.SetRecord("a b", " ");
    try {
        store.SetNF(1000001, " ");
        FAIL() << "expected AwkFatal";
    } catch (const Awk::AwkFatal& error) {
        EXPECT_STREQ(error.what(), "NF set to 1000001: more than 1000000 fields");
    }
    try {
        store.SetField(1000001, Awk::Value::FromString("x"), " ", kConvFmt);
        FAIL() << "expected AwkFatal";
    } catch (const Awk::AwkFatal& error) {
        EXPECT_STREQ(error.what(), "attempt to assign field 1000001: more than 1000000 fields");
    }
    // The limit itself is allowed; reading far beyond NF stays quiet.
    store.SetNF(1000000, " ");
    EXPECT_EQ(store.NF(), 1000000u);
    EXPECT_EQ(store.Field(1000002, kConvFmt).GetType(), Awk::Value::Type::Uninitialized);
}

// --- AwkIntegerOf ---

TEST(AwkValueTest, AwkIntegerOf) {
    const intmax_t kMin = std::numeric_limits<intmax_t>::min();
    EXPECT_EQ(Awk::AwkIntegerOf(3.9), 3);
    EXPECT_EQ(Awk::AwkIntegerOf(-3.9), -3);
    EXPECT_EQ(Awk::AwkIntegerOf(0.5), 0);
    EXPECT_EQ(Awk::AwkIntegerOf(1e30), kMin);
    EXPECT_EQ(Awk::AwkIntegerOf(-1e30), kMin);
    EXPECT_EQ(Awk::AwkIntegerOf(std::nan("")), kMin);
    // 2^63 itself is out of range; -2^63 is intmax_t's own minimum.
    EXPECT_EQ(Awk::AwkIntegerOf(9223372036854775808.0), kMin);
    EXPECT_EQ(Awk::AwkIntegerOf(-9223372036854775808.0), kMin);
    EXPECT_EQ(Awk::AwkIntegerOf(4611686018427387904.0), 4611686018427387904);
}

// --- RecordReader ---

TEST(AwkValueTest, RecordReaderReadsLines) {
    ReaderHarness harness;
    harness.Feed("a\nb");
    harness.EndInput();
    std::string record;
    EXPECT_EQ(harness.Next("\n", record), Awk::RecordReadResult::Record);
    EXPECT_EQ(record, "a");
    EXPECT_EQ(harness.Next("\n", record), Awk::RecordReadResult::Record);
    EXPECT_EQ(record, "b");
    EXPECT_EQ(harness.Next("\n", record), Awk::RecordReadResult::End);
}

TEST(AwkValueTest, RecordReaderGivesTheEmptyRecordAfterTheLastSeparator) {
    ReaderHarness harness;
    harness.Feed("a\n\n");
    harness.EndInput();
    std::string record;
    EXPECT_EQ(harness.Next("\n", record), Awk::RecordReadResult::Record);
    EXPECT_EQ(record, "a");
    EXPECT_EQ(harness.Next("\n", record), Awk::RecordReadResult::Record);
    EXPECT_EQ(record, "");
    EXPECT_EQ(harness.Next("\n", record), Awk::RecordReadResult::End);
}

TEST(AwkValueTest, RecordReaderUsesOnlyTheFirstByteOfRs) {
    ReaderHarness harness;
    harness.Feed("aXbXc");
    harness.EndInput();
    std::string record;
    EXPECT_EQ(harness.Next("X", record), Awk::RecordReadResult::Record);
    EXPECT_EQ(record, "a");
    EXPECT_EQ(harness.Next("X", record), Awk::RecordReadResult::Record);
    EXPECT_EQ(record, "b");
    EXPECT_EQ(harness.Next("X", record), Awk::RecordReadResult::Record);
    EXPECT_EQ(record, "c");
    EXPECT_EQ(harness.Next("X", record), Awk::RecordReadResult::End);

    ReaderHarness two;
    two.Feed("xaby\nzabw");
    two.EndInput();
    EXPECT_EQ(two.Next("ab", record), Awk::RecordReadResult::Record);
    EXPECT_EQ(record, "x");
    EXPECT_EQ(two.Next("ab", record), Awk::RecordReadResult::Record);
    EXPECT_EQ(record, "by\nz");
    EXPECT_EQ(two.Next("ab", record), Awk::RecordReadResult::Record);
    EXPECT_EQ(record, "bw");
    EXPECT_EQ(two.Next("ab", record), Awk::RecordReadResult::End);
}

TEST(AwkValueTest, RecordReaderAppliesARsChangeFromTheNextRecordOn) {
    ReaderHarness harness;
    harness.Feed("aXbYc");
    harness.EndInput();
    std::string record;
    EXPECT_EQ(harness.Next("X", record), Awk::RecordReadResult::Record);
    EXPECT_EQ(record, "a");
    EXPECT_EQ(harness.Next("Y", record), Awk::RecordReadResult::Record);
    EXPECT_EQ(record, "b");
    EXPECT_EQ(harness.Next("Y", record), Awk::RecordReadResult::Record);
    EXPECT_EQ(record, "c");
    EXPECT_EQ(harness.Next("Y", record), Awk::RecordReadResult::End);
}

TEST(AwkValueTest, RecordReaderReadsALongLineWhole) {
    ReaderHarness harness;
    const std::string line = std::string(200 * 1024, 'x') + "\n";
    harness.Feed(line);
    harness.EndInput();
    std::string record;
    EXPECT_EQ(harness.Next("\n", record), Awk::RecordReadResult::Record);
    EXPECT_EQ(record, std::string(200 * 1024, 'x'));
    EXPECT_EQ(harness.Next("\n", record), Awk::RecordReadResult::End);
}

TEST(AwkValueTest, RecordReaderReadsManyShortRecords) {
    // 300000 records `x<n>\n', each 7 bytes or fewer: the buffer's front is
    // consumed per record, so this stays linear where erasing every returned
    // record would not. Spot-check the records, not all of them.
    const auto makeRecords = [] {
        std::string bytes;
        for (int i = 0; i < 300000; ++i) {
            bytes += "x" + std::to_string(i) + "\n";
        }
        return bytes;
    };
    const std::string bytes = makeRecords();
    const auto checkAll = [&](ReaderHarness& harness) {
        std::string record;
        for (int i = 0; i < 300000; ++i) {
            ASSERT_EQ(harness.Next("\n", record), Awk::RecordReadResult::Record);
            if (i == 0 || i == 65535 || i == 65536 || i == 299999) {
                EXPECT_EQ(record, "x" + std::to_string(i));
            }
        }
        EXPECT_EQ(harness.Next("\n", record), Awk::RecordReadResult::End);
    };
    ReaderHarness whole;
    whole.Feed(bytes);
    whole.EndInput();
    checkAll(whole);
    // The same records again, fed 7 bytes at a time: every Read then takes
    // what has accumulated, across many block boundaries.
    ReaderHarness inSevens;
    for (size_t pos = 0; pos < bytes.size(); pos += 7) {
        inSevens.Feed(bytes.substr(pos, 7));
    }
    inSevens.EndInput();
    checkAll(inSevens);
}

TEST(AwkValueTest, RecordReaderStopsPromptly) {
    // A stop set before the first read: Stopped, nothing read.
    ReaderHarness before;
    before.Feed("a\nb");
    before.EndInput();
    before.RequestStop();
    std::string record;
    EXPECT_EQ(before.Next("\n", record), Awk::RecordReadResult::Stopped);
    // A stop set mid-input: the record already in the buffer still comes,
    // then Stopped.
    ReaderHarness mid;
    mid.Feed("a\nb");
    mid.EndInput();
    EXPECT_EQ(mid.Next("\n", record), Awk::RecordReadResult::Record);
    EXPECT_EQ(record, "a");
    mid.RequestStop();
    EXPECT_EQ(mid.Next("\n", record), Awk::RecordReadResult::Stopped);
}
