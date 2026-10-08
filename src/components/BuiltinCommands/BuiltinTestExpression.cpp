// The test/[ expression evaluator, shared by hsh (its test and [ builtins,
// dash's dialect) and the test and [ builtin commands (GNU coreutils 9.4's
// src/test.c, the dialect the real test/[ programs speak). The dash
// evaluator was hsh's own until the programs arrived; it moved here
// unchanged, and the file primaries both dialects spell alike are one
// function over the process's IFileIO.
#include "BuiltinTestExpression.h"

#include <cstdint>
#include <optional>
#include <string>

#include "BuiltinText.h"
#include "commands/hsh/HshNumber.h"
#include "interfaces/IFileIO.h"
#include "interfaces/IFileSystemService.h"

namespace Haisos {

namespace {

// A syntax error found while evaluating: the message has been reported
// already; the command returns 2.
struct TestSyntaxError {};

// One unary file test by the operator's own letter ('r' for -r, 'O' for
// -O -- case-sensitive, as the operator is), with Haisos's semantics. There
// are no permissions or users yet: -r, -w, -x, -O and -G only test that the
// file is there (a documented exception). There are no symbolic links,
// block devices, fifos, sockets or set-id bits either: -h, -L, -b, -p, -S,
// -u, -g and -k never match. -N (whether the file was modified after it was
// last read) needs both timestamps, as GNU's does.
bool EvaluateFileUnary(char letter, const std::string& operand, IFileIO& io) {
    switch (letter) {
    case 'r':
    case 'w':
    case 'x':
    case 'O':
    case 'G': {
        FileStatus status;
        return io.Stat(operand, status) == 0;
    }
    case 'e':
    case 'f':
    case 'd':
    case 'c':
    case 's':
    case 'N': {
        FileStatus status;
        if (io.Stat(operand, status) != 0) {
            return false;
        }
        switch (letter) {
        case 'e': return true;
        case 'f': return status.type == DirectoryEntryType::File;
        case 'd': return status.type == DirectoryEntryType::Dir;
        case 'c': return status.type == DirectoryEntryType::CharDevice;
        case 's': return status.size > 0;
        default:  return status.modificationTime > status.accessTime;  // 'N'
        }
    }
    default:
        return false;  // h -L -b -p -S -u -g -k
    }
}

// --- The Dash dialect: hsh's test / [ (dash's bltin/test.c) ---

// dash's test / [ (bltin/test.c), ported over tokens looked up in an operand
// window |begin|..|end| of the arguments (dash walks a NULL-terminated argv).
enum class TestToken {
    End, FileRead, FileWrite, FileExec, FileExists, FileRegular, FileDirectory,
    FileCharDevice, FileBlockDevice, FileFifo, FileSocket, FileSymlink,
    FileNonEmpty, FileTerminal, FileSetUid, FileSetGid, FileSticky,
    FileNewer, FileOlder, FileEqual, FileOwnerUid, FileOwnerGid,
    StringEmpty, StringNotEmpty, StringEqual, StringNotEqual, StringLess,
    StringGreater, IntEqual, IntNotEqual, IntGreaterEqual, IntGreater,
    IntLessEqual, IntLess, Not, And, Or, OpenParen, CloseParen, Operand
};

enum class TestOpType { Unary, Binary, UnaryNot, BinaryLogical, Paren };

struct TestOp {
    const char* text;
    TestToken token;
    TestOpType type;
};

// dash's ops table, in its own order.
constexpr TestOp kTestOps[] = {
    {"-r", TestToken::FileRead, TestOpType::Unary},
    {"-w", TestToken::FileWrite, TestOpType::Unary},
    {"-x", TestToken::FileExec, TestOpType::Unary},
    {"-e", TestToken::FileExists, TestOpType::Unary},
    {"-f", TestToken::FileRegular, TestOpType::Unary},
    {"-d", TestToken::FileDirectory, TestOpType::Unary},
    {"-c", TestToken::FileCharDevice, TestOpType::Unary},
    {"-b", TestToken::FileBlockDevice, TestOpType::Unary},
    {"-p", TestToken::FileFifo, TestOpType::Unary},
    {"-u", TestToken::FileSetUid, TestOpType::Unary},
    {"-g", TestToken::FileSetGid, TestOpType::Unary},
    {"-k", TestToken::FileSticky, TestOpType::Unary},
    {"-s", TestToken::FileNonEmpty, TestOpType::Unary},
    {"-t", TestToken::FileTerminal, TestOpType::Unary},
    {"-z", TestToken::StringEmpty, TestOpType::Unary},
    {"-n", TestToken::StringNotEmpty, TestOpType::Unary},
    {"-h", TestToken::FileSymlink, TestOpType::Unary},  // for backwards compat, as dash
    {"-O", TestToken::FileOwnerUid, TestOpType::Unary},
    {"-G", TestToken::FileOwnerGid, TestOpType::Unary},
    {"-L", TestToken::FileSymlink, TestOpType::Unary},
    {"-S", TestToken::FileSocket, TestOpType::Unary},
    {"=", TestToken::StringEqual, TestOpType::Binary},
    {"!=", TestToken::StringNotEqual, TestOpType::Binary},
    {"<", TestToken::StringLess, TestOpType::Binary},
    {">", TestToken::StringGreater, TestOpType::Binary},
    {"-eq", TestToken::IntEqual, TestOpType::Binary},
    {"-ne", TestToken::IntNotEqual, TestOpType::Binary},
    {"-ge", TestToken::IntGreaterEqual, TestOpType::Binary},
    {"-gt", TestToken::IntGreater, TestOpType::Binary},
    {"-le", TestToken::IntLessEqual, TestOpType::Binary},
    {"-lt", TestToken::IntLess, TestOpType::Binary},
    {"-nt", TestToken::FileNewer, TestOpType::Binary},
    {"-ot", TestToken::FileOlder, TestOpType::Binary},
    {"-ef", TestToken::FileEqual, TestOpType::Binary},
    {"!", TestToken::Not, TestOpType::UnaryNot},
    {"-a", TestToken::And, TestOpType::BinaryLogical},
    {"-o", TestToken::Or, TestOpType::BinaryLogical},
    {"(", TestToken::OpenParen, TestOpType::Paren},
    {")", TestToken::CloseParen, TestOpType::Paren},
};

char TestTokenLetter(TestToken token) {
    switch (token) {
    case TestToken::FileRead: return 'r';
    case TestToken::FileWrite: return 'w';
    case TestToken::FileExec: return 'x';
    case TestToken::FileExists: return 'e';
    case TestToken::FileRegular: return 'f';
    case TestToken::FileDirectory: return 'd';
    case TestToken::FileCharDevice: return 'c';
    case TestToken::FileBlockDevice: return 'b';
    case TestToken::FileFifo: return 'p';
    case TestToken::FileSocket: return 'S';
    case TestToken::FileSymlink: return 'h';
    case TestToken::FileNonEmpty: return 's';
    case TestToken::FileSetUid: return 'u';
    case TestToken::FileSetGid: return 'g';
    case TestToken::FileSticky: return 'k';
    case TestToken::FileOwnerUid: return 'O';
    case TestToken::FileOwnerGid: return 'G';
    default: return 0;  // no file test
    }
}

class TestEvaluation {
public:
    TestEvaluation(IFileIO& io, const std::function<void(const std::string&)>& report,
                   const std::vector<std::string>& args, size_t begin, size_t end)
        : m_io(io), m_report(report), m_args(args), m_begin(begin), m_end(end), m_pos(begin) {}

    // dash's testcmd, from "recheck:" on: returns the command's status.
    int Run() {
        int result = 1;
        for (;;) {
            if (m_end <= m_begin) {
                return result;  // no operands: false, or true after a "!"
            }
            const size_t count = m_end - m_begin;
            // POSIX's prescriptions: a three-operand binary comparison is
            // evaluated as one; "( oexpr )" and "! oexpr" of three/four
            // operands are reduced first.
            bool operandToken = false;
            if (count == 3) {
                if (const TestOp* op = LookupOp(m_args[m_begin + 1]); op && op->type == TestOpType::Binary) {
                    operandToken = true;
                }
            }
            if ((count == 3 || count == 4) && !operandToken) {
                if (m_args[m_begin] == "(" && m_args[m_end - 1] == ")") {
                    --m_end;
                    ++m_begin;
                } else if (m_args[m_begin] == "!") {
                    result = 0;
                    ++m_begin;
                    continue;
                }
            }
            const TestToken token = operandToken ? TokenOperand() : Lex(m_begin);
            m_pos = m_begin;
            result ^= OrExpression(token) ? 1 : 0;
            if (m_pos < m_end && m_pos + 1 < m_end) {
                Syntax(m_args[m_pos], "unexpected operator");
            }
            return result;
        }
    }

private:
    TestToken TokenOperand() {
        m_currentOp = nullptr;
        return TestToken::Operand;
    }

    [[noreturn]] void Syntax(const std::string& opText, const std::string& message) {
        m_report(opText.empty() ? message : opText + ": " + message);
        throw TestSyntaxError{};
    }

    [[noreturn]] void IllegalNumber(const std::string& text) {
        m_report("Illegal number: " + text);
        throw TestSyntaxError{};
    }

    static const TestOp* LookupOp(const std::string& text) {
        for (const TestOp& op : kTestOps) {
            if (text == op.text) {
                return &op;
            }
        }
        return nullptr;
    }

    // dash's t_lex: the token at |index|, End past the last operand. A unary
    // operator that is followed by only an operand reads as an operand (the
    // "test -n" case), and a "(" last reads as an operand.
    TestToken Lex(size_t index) {
        if (index >= m_end) {
            m_currentOp = nullptr;
            return TestToken::End;
        }
        const TestOp* op = LookupOp(m_args[index]);
        if (op && !(op->type == TestOpType::Unary && IsOperand(index)) &&
            !(op->token == TestToken::OpenParen && index + 1 >= m_end)) {
            m_currentOp = op;
            return op->token;
        }
        m_currentOp = nullptr;
        return TestToken::Operand;
    }

    // dash's isoperand: whether |index|'s word must read as an operand.
    bool IsOperand(size_t index) const {
        if (index + 1 >= m_end) {
            return true;
        }
        if (index + 2 >= m_end) {
            return false;
        }
        const TestOp* op = LookupOp(m_args[index + 1]);
        return op && op->type == TestOpType::Binary;
    }

    // dash's oexpr: aexpr joined by -o.
    bool OrExpression(TestToken token) {
        bool result = false;
        for (;;) {
            result |= AndExpression(token);
            token = Lex(m_pos + 1);
            if (token != TestToken::Or) {
                break;
            }
            m_pos += 2;
            token = Lex(m_pos);
        }
        return result;
    }

    // dash's aexpr: nexpr joined by -a.
    bool AndExpression(TestToken token) {
        bool result = true;
        for (;;) {
            if (!NotExpression(token)) {
                result = false;
            }
            token = Lex(m_pos + 1);
            if (token != TestToken::And) {
                break;
            }
            m_pos += 2;
            token = Lex(m_pos);
        }
        return result;
    }

    // dash's nexpr: "!" primaries.
    bool NotExpression(TestToken token) {
        if (token != TestToken::Not) {
            return Primary(token);
        }
        token = Lex(m_pos + 1);
        if (token != TestToken::End) {
            ++m_pos;
        }
        return !NotExpression(token);
    }

    // dash's primary.
    bool Primary(TestToken token) {
        if (token == TestToken::End) {
            return false;  // a missing expression
        }
        if (token == TestToken::OpenParen) {
            ++m_pos;
            const TestToken inner = Lex(m_pos);
            if (inner == TestToken::CloseParen) {
                return false;  // "()" is empty: false
            }
            const bool value = OrExpression(inner);
            ++m_pos;
            if (Lex(m_pos) != TestToken::CloseParen) {
                Syntax("", "closing paren expected");
            }
            return value;
        }
        if (m_currentOp && m_currentOp->type == TestOpType::Unary) {
            ++m_pos;
            if (m_pos >= m_end) {
                Syntax(m_currentOp->text, "argument expected");
            }
            return UnaryTest(m_currentOp->token, m_args[m_pos]);
        }
        Lex(m_pos + 1);  // sets m_currentOp for the middle token
        if (m_currentOp && m_currentOp->type == TestOpType::Binary) {
            return BinaryTest();
        }
        return !m_args[m_pos].empty();  // one operand: its non-emptiness
    }

    // dash's binop: |m_pos| at the first operand; ends at the second.
    bool BinaryTest() {
        const std::string& operand1 = m_args[m_pos];
        ++m_pos;
        Lex(m_pos);
        const TestOp* op = m_currentOp;
        ++m_pos;
        if (m_pos >= m_end) {
            Syntax(op->text, "argument expected");
        }
        const std::string& operand2 = m_args[m_pos];
        switch (op->token) {
        case TestToken::StringEqual:    return operand1 == operand2;
        case TestToken::StringNotEqual: return operand1 != operand2;
        case TestToken::StringLess:     return operand1 < operand2;
        case TestToken::StringGreater:  return operand1 > operand2;
        case TestToken::IntEqual:         return NumberOf(operand1) == NumberOf(operand2);
        case TestToken::IntNotEqual:     return NumberOf(operand1) != NumberOf(operand2);
        case TestToken::IntGreaterEqual: return NumberOf(operand1) >= NumberOf(operand2);
        case TestToken::IntGreater:      return NumberOf(operand1) > NumberOf(operand2);
        case TestToken::IntLessEqual:    return NumberOf(operand1) <= NumberOf(operand2);
        case TestToken::IntLess:         return NumberOf(operand1) < NumberOf(operand2);
        case TestToken::FileNewer: { FileStatus a, b; return StatOf(operand1, a) && StatOf(operand2, b)
            && a.modificationTime > b.modificationTime; }
        case TestToken::FileOlder: { FileStatus a, b; return StatOf(operand1, a) && StatOf(operand2, b)
            && a.modificationTime < b.modificationTime; }
        case TestToken::FileEqual: { FileStatus a, b; return StatOf(operand1, a) && StatOf(operand2, b)
            && m_io.ResolvePath(operand1) == m_io.ResolvePath(operand2); }
        default: return false;  // unreachable: only Binary ops reach here
        }
    }

    intmax_t NumberOf(const std::string& text) {
        const std::optional<intmax_t> number = Hsh::Atomax10(text);
        if (!number) {
            IllegalNumber(text);
        }
        return *number;
    }

    bool StatOf(const std::string& path, FileStatus& out) {
        return m_io.Stat(path, out) == 0;
    }

    // dash's filstat through the process's IFileIO: the shared file
    // primaries above, with dash's own string and fd tests around them.
    bool UnaryTest(TestToken token, const std::string& operand) {
        switch (token) {
        case TestToken::StringEmpty:
            return operand.empty();
        case TestToken::StringNotEmpty:
            return !operand.empty();
        case TestToken::FileTerminal: {
            const std::optional<intmax_t> fd = Hsh::Atomax10(operand);
            if (!fd) {
                IllegalNumber(operand);
            }
            if (*fd < 0 || *fd >= IFileIO::kMaxDescriptors) {
                return false;
            }
            const auto descriptor = m_io.GetDescriptor(static_cast<int>(*fd));
            return descriptor && descriptor->IsTerminal();
        }
        default:
            return EvaluateFileUnary(TestTokenLetter(token), operand, m_io);
        }
    }

    IFileIO& m_io;
    const std::function<void(const std::string&)>& m_report;
    const std::vector<std::string>& m_args;
    size_t m_begin;
    size_t m_end;
    size_t m_pos;                        // dash's t_wp, as an index
    const TestOp* m_currentOp = nullptr; // dash's t_wp_op
};

// --- The Gnu dialect: coreutils 9.4's src/test.c ---

// GNU's binary operators, the ones its isbinop looks for: = != == -eq -ne
// -lt -le -gt -ge -nt -ot -ef ("<" and ">" are string comparisons POSIX
// leaves to test's shell dialect; GNU leaves them out here too).
bool IsGnuBinop(const std::string& text) {
    return text == "=" || text == "!=" || text == "==" || text == "-eq" || text == "-ne"
        || text == "-lt" || text == "-le" || text == "-gt" || text == "-ge"
        || text == "-nt" || text == "-ot" || text == "-ef";
}

// Whether |left| < |right| as GNU compares its integers: any length, so as
// canonical decimal strings -- sign, then digit count, then digits. Both
// are canonical (no leading zeros, a lone zero never negative).
bool LessThanInteger(const std::string& left, const std::string& right) {
    const bool leftNegative = left[0] == '-';
    const bool rightNegative = right[0] == '-';
    if (leftNegative != rightNegative) {
        return leftNegative;
    }
    const std::string& leftDigits = leftNegative ? left.substr(1) : left;
    const std::string& rightDigits = rightNegative ? right.substr(1) : right;
    if (leftDigits.size() != rightDigits.size()) {
        return (leftDigits.size() < rightDigits.size()) != leftNegative;
    }
    const int compare = leftDigits.compare(rightDigits);
    return (compare < 0) != leftNegative;
}

class GnuTestEvaluation {
public:
    GnuTestEvaluation(IFileIO& io, const std::function<void(const std::string&)>& report,
                      const std::vector<std::string>& args, size_t begin, size_t end)
        : m_io(io), m_report(report), m_args(args), m_end(end) {
        m_pos = begin;
        m_argc = end;
    }

    // GNU's main, from posixtest on: the status of the expression.
    int Run() {
        const size_t count = m_argc - m_pos;
        if (count == 0) {
            return 1;  // no operands: false, as GNU's test with no arguments
        }
        const bool value = PosixTest(count);
        if (m_pos != m_argc) {
            Syntax("extra argument " + GnuQuote(m_args[m_pos]));
        }
        return value ? 0 : 1;
    }

private:
    // GNU's pos... the window end, shrunk inside a parenthesized expression.
    size_t m_pos;

    [[noreturn]] void Syntax(const std::string& message) {
        m_report(message);
        throw TestSyntaxError{};
    }

    // GNU's beyond: the operand that ran out is named by the command's last
    // one, wherever the window ended.
    [[noreturn]] void Beyond() {
        Syntax("missing argument after " + GnuQuote(m_args[m_end - 1]));
    }

    // GNU's find_int: |word| as a canonical decimal integer, reported
    // "invalid integer 'word'" when it is not one (GNU's integers are any
    // length, so they are kept as strings). Blanks are spaces and tabs.
    std::string FindInt(const std::string& word) {
        const size_t n = word.size();
        size_t i = 0;
        while (i < n && (word[i] == ' ' || word[i] == '\t')) {
            ++i;
        }
        const bool negative = i < n && word[i] == '-';
        if (i < n && (word[i] == '+' || word[i] == '-')) {
            ++i;
        }
        const size_t firstDigit = i;
        while (i < n && word[i] >= '0' && word[i] <= '9') {
            ++i;
        }
        const size_t lastDigit = i;
        while (i < n && (word[i] == ' ' || word[i] == '\t')) {
            ++i;
        }
        if (firstDigit == lastDigit || i != n) {
            Syntax("invalid integer " + GnuQuote(word));
        }
        const size_t first = word.find_first_not_of('0', firstDigit);
        if (first == std::string::npos || first >= lastDigit) {
            return "0";  // all zeros: never negative
        }
        return negative ? "-" + word.substr(first, lastDigit - first)
                        : word.substr(first, lastDigit - first);
    }

    // GNU's posixtest: the POSIX reductions over short windows, the -o/-a
    // expression over the long and the parenthesized ones.
    bool PosixTest(size_t nargs) {
        switch (nargs) {
        case 1:  return OneArgument();
        case 2:  return TwoArguments();
        case 3:  return ThreeArguments();
        case 4:  return FourArguments();
        default: return Expr();
        }
    }

    // GNU's one_arg: a single operand is its non-emptiness.
    bool OneArgument() {
        const bool value = !m_args[m_pos].empty();
        ++m_pos;
        return value;
    }

    // GNU's two_arguments.
    bool TwoArguments() {
        if (m_args[m_pos] == "!") {
            ++m_pos;
            return !OneArgument();
        }
        if (m_args[m_pos].size() == 2 && m_args[m_pos][0] == '-') {
            return UnaryOperator();
        }
        Beyond();
    }

    // GNU's three_arguments.
    bool ThreeArguments() {
        if (IsGnuBinop(m_args[m_pos + 1])) {
            return BinaryOperator(false);
        }
        if (m_args[m_pos] == "!") {
            ++m_pos;
            return !TwoArguments();
        }
        if (m_args[m_pos] == "(" && m_args[m_pos + 2] == ")") {
            ++m_pos;
            const bool value = OneArgument();
            ++m_pos;
            return value;
        }
        if (m_args[m_pos + 1] == "-a" || m_args[m_pos + 1] == "-o") {
            return Expr();
        }
        Syntax(GnuQuote(m_args[m_pos + 1]) + ": binary operator expected");
    }

    // GNU's four_arguments.
    bool FourArguments() {
        if (m_args[m_pos] == "!") {
            ++m_pos;
            return !ThreeArguments();
        }
        if (m_args[m_pos] == "(" && m_args[m_pos + 3] == ")") {
            ++m_pos;
            const bool value = TwoArguments();
            ++m_pos;
            return value;
        }
        return Expr();
    }

    // GNU's expr: an -o expression of -a expressions of terms.
    bool Expr() {
        if (m_pos >= m_argc) {
            Beyond();
        }
        return Or();
    }

    // GNU's or: -o joins ands. Both sides always evaluate (no short-circuit
    // of errors): a failing right side is reported even when the left is
    // already true.
    bool Or() {
        bool value = And();
        while (m_pos < m_argc && m_args[m_pos] == "-o") {
            ++m_pos;
            value = And() || value;
        }
        return value;
    }

    // GNU's and: -a joins terms, both sides always evaluated.
    bool And() {
        bool value = Term();
        while (m_pos < m_argc && m_args[m_pos] == "-a") {
            ++m_pos;
            value = Term() && value;
        }
        return value;
    }

    // GNU's term: a primary, parenthesized, negated, or a bare operand.
    bool Term() {
        if (m_pos >= m_argc) {
            Beyond();
        }
        const std::string& word = m_args[m_pos];
        if (word == "!") {
            ++m_pos;
            return !Term();
        }
        if (word == "(") {
            ++m_pos;
            // The ")" that closes: the first from the word after "(" on,
            // within the four a POSIX test holds -- GNU's scan starts one
            // word past "(", so a ")" right after it is an operand, not
            // the closing one. With none in reach, the rest of the window
            // is the expression, and the ")" is checked after it.
            size_t i = 1;
            while (m_pos + i < m_argc && i <= 4 && m_args[m_pos + i] != ")") {
                ++i;
            }
            bool value = false;
            const size_t saveArgc = m_argc;
            if (m_pos + i < m_argc && i <= 4) {
                m_argc = m_pos + i;
                value = PosixTest(i);
                m_argc = saveArgc;
            } else {
                value = PosixTest(saveArgc - m_pos);
            }
            // The closing ")": GNU reads the word at |m_pos| even one past
            // the window's end -- for "[", the "]" its caller dropped still
            // sits in the arguments, as it does in GNU's own argv, and is
            // what the check names. With nothing there, the plain form.
            if (m_pos < m_args.size() && m_args[m_pos] == ")") {
                ++m_pos;
                return value;
            }
            if (m_pos < m_args.size()) {
                Syntax("')' expected, found " + GnuQuote(m_args[m_pos]));
            }
            Syntax("')' expected");
        }
        // "-l word": the word's length, as an operand -- only with the
        // operator's right operand there too (a "-l" without one is an
        // operator itself, as GNU's).
        if (word == "-l" && m_pos + 3 < m_argc && IsGnuBinop(m_args[m_pos + 2])) {
            ++m_pos;
            return BinaryOperator(true);
        }
        if (m_pos + 2 < m_argc && IsGnuBinop(m_args[m_pos + 1])) {
            return BinaryOperator(false);
        }
        if (word.size() == 2 && word[0] == '-') {
            return UnaryOperator();
        }
        ++m_pos;
        return !word.empty();
    }

    // GNU's binary_operator: |m_pos| at the left operand, or, with
    // |leftIsLength|, after a "-l" that lengthened it. Ends after the right
    // one -- one word further when the right operand is "-l word".
    bool BinaryOperator(bool leftIsLength) {
        const std::string& op = m_args[m_pos + 1];
        const bool rightIsLength = m_args[m_pos + 2] == "-l" && m_pos + 3 < m_argc;
        if (op == "=" || op == "==" || op == "!=") {
            // A string comparison, the operands as they stand, "-l"
            // included: a "-l" only moves the window -- and GNU's own
            // quirk, when the right operand is one both operands shift
            // past it, so the operator's word is what gets compared with
            // the word after "-l".
            const size_t shift = rightIsLength ? 1 : 0;
            const bool equal = m_args[m_pos + shift] == m_args[m_pos + 2 + shift];
            m_pos += rightIsLength ? 4 : 3;
            return op != "!=" ? equal : !equal;
        }
        if (op == "-nt" || op == "-ot" || op == "-ef") {
            if (leftIsLength || rightIsLength) {
                Syntax(op + " does not accept -l");
            }
            const std::string& leftPath = m_args[m_pos];
            const std::string& rightPath = m_args[m_pos + 2];
            FileStatus left, right;
            const bool leftExists = m_io.Stat(leftPath, left) == 0;
            const bool rightExists = m_io.Stat(rightPath, right) == 0;
            m_pos += 3;
            if (op == "-ef") {
                // The same file: GNU compares device and inode numbers;
                // Haisos has neither, so what ResolvePath gives (a
                // documented exception).
                return leftExists && rightExists
                    && m_io.ResolvePath(leftPath) == m_io.ResolvePath(rightPath);
            }
            if (op == "-nt") {
                return leftExists && (!rightExists || left.modificationTime > right.modificationTime);
            }
            return rightExists && (!leftExists || left.modificationTime < right.modificationTime);
        }
        // The integer comparisons. A "-l word" counts as its word's length.
        const std::string left = leftIsLength ? std::to_string(m_args[m_pos].size())
                                              : FindInt(m_args[m_pos]);
        const std::string right = rightIsLength ? std::to_string(m_args[m_pos + 3].size())
                                                : FindInt(m_args[m_pos + 2]);
        m_pos += rightIsLength ? 4 : 3;
        if (op == "-eq") return left == right;
        if (op == "-ne") return left != right;
        if (op == "-lt") return LessThanInteger(left, right);
        if (op == "-le") return !LessThanInteger(right, left);
        if (op == "-gt") return LessThanInteger(right, left);
        return !LessThanInteger(left, right);  // -ge
    }

    // GNU's unary_operator: |m_pos| at the operator; ends after the operand.
    // An operator that is not one of GNU's is an error, its operand there
    // or not; one of them with the operand missing runs out of arguments.
    bool UnaryOperator() {
        const std::string& word = m_args[m_pos];
        const char letter = word[1];
        if (letter != 't' && letter != 'n' && letter != 'z' && !EvaluateFileUnaryKnown(letter)) {
            Syntax(GnuQuote(word) + ": unary operator expected");
        }
        if (m_pos + 1 >= m_argc) {
            Beyond();
        }
        const std::string& operand = m_args[m_pos + 1];
        m_pos += 2;
        switch (letter) {
        case 'n':
            return !operand.empty();
        case 'z':
            return operand.empty();
        case 't': {
            // GNU's isatty: the number first, then the descriptor; a number
            // too large for a descriptor is just false.
            const std::string number = FindInt(operand);
            long long fd = 0;
            for (const char digit : number) {
                if (digit == '-') {
                    fd = -1;
                    break;
                }
                fd = fd * 10 + (digit - '0');
                if (fd >= IFileIO::kMaxDescriptors) {
                    break;
                }
            }
            if (fd < 0 || fd >= IFileIO::kMaxDescriptors) {
                return false;
            }
            const auto descriptor = m_io.GetDescriptor(static_cast<int>(fd));
            return descriptor && descriptor->IsTerminal();
        }
        default:
            return EvaluateFileUnary(letter, operand, m_io);
        }
    }

    // Whether |letter| is one of GNU's file tests (e r w x O G N f d s h L
    // b p S u g k), all of them shared with dash's evaluator above.
    static bool EvaluateFileUnaryKnown(char letter) {
        switch (letter) {
        case 'e': case 'r': case 'w': case 'x': case 'O': case 'G': case 'N':
        case 'f': case 'd': case 's': case 'h': case 'L': case 'b': case 'p':
        case 'S': case 'u': case 'g': case 'k': case 'c':
            return true;
        default:
            return false;
        }
    }

    IFileIO& m_io;
    const std::function<void(const std::string&)>& m_report;
    const std::vector<std::string>& m_args;
    size_t m_argc;  // the window end
    const size_t m_end;
};

} // namespace

int EvaluateTestExpression(TestDialect dialect, const std::string& command,
    const std::vector<std::string>& args, size_t begin, size_t end,
    IFileIO& io, const std::function<void(const std::string&)>& report) {
    try {
        if (dialect == TestDialect::Dash) {
            return TestEvaluation(io, report, args, begin, end).Run();
        }
        return GnuTestEvaluation(io, report, args, begin, end).Run();
    } catch (const TestSyntaxError&) {
        return 2;
    }
}

} // namespace Haisos