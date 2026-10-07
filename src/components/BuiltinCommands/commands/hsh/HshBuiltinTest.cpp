#include "commands/hsh/HshBuiltins.h"

#include <cstdint>
#include <optional>
#include <string>

#include "commands/hsh/HshNumber.h"
#include "commands/hsh/HshShell.h"

namespace Haisos::Hsh {

namespace {

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

// A syntax error found while evaluating: the message has been reported
// already; the command returns 2, as dash's error() does for test.
struct TestSyntaxError {};

class TestEvaluation {
public:
    TestEvaluation(Shell& shell, const std::string& command,
                   const std::vector<std::string>& args, size_t begin, size_t end)
        : m_shell(shell), m_command(command), m_args(args), m_begin(begin), m_end(end), m_pos(begin) {}

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
        m_shell.Report(opText.empty() ? m_command + ": " + message
                                      : m_command + ": " + opText + ": " + message);
        throw TestSyntaxError{};
    }

    [[noreturn]] void IllegalNumber(const std::string& text) {
        m_shell.Report(m_command + ": Illegal number: " + text);
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
        case TestToken::FileEqual: return m_shell.Exists(operand1) && m_shell.Exists(operand2)
            && m_shell.IO().ResolvePath(operand1) == m_shell.IO().ResolvePath(operand2);
        default: return false;  // unreachable: only Binary ops reach here
        }
    }

    intmax_t NumberOf(const std::string& text) {
        const std::optional<intmax_t> number = Atomax10(text);
        if (!number) {
            IllegalNumber(text);
        }
        return *number;
    }

    bool StatOf(const std::string& path, FileStatus& out) {
        return m_shell.IO().Stat(path, out) == 0;
    }

    // dash's filstat through the process's IFileIO. There are no permissions,
    // users, fifos, sockets or block devices in Haisos yet: -r, -w, -x, -O
    // and -G are "it is there" (a documented exception), and -h, -L, -b, -p,
    // -S, -u, -g and -k never match.
    bool UnaryTest(TestToken token, const std::string& operand) {
        switch (token) {
        case TestToken::StringEmpty:
            return operand.empty();
        case TestToken::StringNotEmpty:
            return !operand.empty();
        case TestToken::FileTerminal: {
            const std::optional<intmax_t> fd = Atomax10(operand);
            if (!fd) {
                IllegalNumber(operand);
            }
            if (*fd < 0 || *fd >= IFileIO::kMaxDescriptors) {
                return false;
            }
            const auto descriptor = m_shell.IO().GetDescriptor(static_cast<int>(*fd));
            return descriptor && descriptor->IsTerminal();
        }
        case TestToken::FileSymlink:
            return false;  // lstat's S_ISLNK: there are no symlinks here
        case TestToken::FileRead:
        case TestToken::FileWrite:
        case TestToken::FileExec:
        case TestToken::FileOwnerUid:
        case TestToken::FileOwnerGid:
            return m_shell.Exists(operand);
        default: {
            FileStatus status;
            if (m_shell.IO().Stat(operand, status) != 0) {
                return false;
            }
            switch (token) {
            case TestToken::FileExists:     return true;
            case TestToken::FileRegular:    return status.type == DirectoryEntryType::File;
            case TestToken::FileDirectory:  return status.type == DirectoryEntryType::Dir;
            case TestToken::FileCharDevice: return status.type == DirectoryEntryType::CharDevice;
            case TestToken::FileNonEmpty:   return status.size > 0;
            default:                        return false;  // -b -p -S -u -g -k
            }
        }
        }
    }

    Shell& m_shell;
    const std::string& m_command;  // "test" or "["
    const std::vector<std::string>& m_args;
    size_t m_begin;
    size_t m_end;
    size_t m_pos;                        // dash's t_wp, as an index
    const TestOp* m_currentOp = nullptr; // dash's t_wp_op
};

} // namespace

// test [expression] / [ expression ]: dash's testcmd. "[" requires its last
// argument to begin with ']' ("[: missing ]" when not). Errors are reported
// with status 2; both are regular builtins, so the shell goes on.
int BuiltinTest(Shell& shell, const std::vector<std::string>& args) {
    const std::string& command = args[0];
    size_t end = args.size();
    if (command == "[") {
        if (end == 1 || args[end - 1][0] != ']') {
            shell.Report("[: missing ]");
            return 2;
        }
        --end;
    }
    try {
        return TestEvaluation(shell, command, args, 1, end).Run();
    } catch (const TestSyntaxError&) {
        return 2;
    }
}

} // namespace Haisos::Hsh
