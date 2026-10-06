#include "commands/hsh/HshBuiltins.h"

#include <optional>
#include <string>
#include <vector>

#include "commands/hsh/HshShell.h"
#include "commands/hsh/HshWord.h"
#include "interfaces/IFileIO.h"

namespace Haisos::Hsh {

// read [-r] [-p prompt] name ..., as dash's readcmd. Regular: option and
// operand errors are reported with status 2 ("read: Illegal option -x",
// "read: arg count", "read: <name>: bad variable name"), not fatal.
//
// Input is read from the shell's slot 0 ONE BYTE AT A TIME: a read must never
// take more of a pipe than its line, so the next command reading the same
// stdin starts right after it (`{ read a; cat; } < f` prints the rest of f).
// A backslash before a newline joins the lines (both dropped); before any
// other byte it makes that byte literal, kept and protected from field
// splitting; at the end of the input a lone backslash is dropped. -r leaves
// backslashes plain. The status is 0 when a newline ended the line, 1 at the
// end of the input -- the variables are set either way.
//
// Splitting is dash's IFS split for read: leading IFS white space is skipped,
// each name but the last takes one field (IFS white-space runs collapse; one
// non-white IFS character, with the white space around it, delimits once) and
// the last name takes the rest of the line with its leading and trailing
// (unprotected) IFS white space removed, inner delimiters kept. Names left
// over are set empty. IFS unset is " \t\n"; IFS empty splits nothing: the
// whole line goes to the first name.
int BuiltinRead(Shell& shell, const std::vector<std::string>& args) {
    bool raw = false;
    std::optional<std::string> prompt;
    size_t i = 1;
    for (; i < args.size(); ++i) {
        const std::string& arg = args[i];
        if (arg == "--") {
            ++i;
            break;
        }
        if (arg.size() < 2 || arg[0] != '-') {
            break;  // "-" alone and everything else is the first name
        }
        for (size_t j = 1; j < arg.size(); ++j) {
            if (arg[j] == 'r') {
                raw = true;
            } else if (arg[j] == 'p') {
                if (i + 1 >= args.size()) {
                    shell.Report("read: arg count");
                    return 2;
                }
                prompt = args[++i];
                break;  // -p takes the next argument; the cluster ends
            } else {
                shell.Report("read: Illegal option -" + std::string(1, arg[j]));
                return 2;
            }
        }
    }
    const std::vector<std::string> names(args.begin() + static_cast<ptrdiff_t>(i), args.end());
    if (names.empty()) {
        shell.Report("read: arg count");
        return 2;
    }
    for (const std::string& name : names) {
        if (!IsValidShellName(name)) {
            shell.Report("read: " + name + ": bad variable name");
            return 2;
        }
    }

    const std::shared_ptr<IFileDescriptor> in = shell.IO().GetDescriptor(IFileIO::kStdIn);
    // The prompt goes to stderr only when the input is a terminal, as dash.
    if (prompt && in && in->IsTerminal()) {
        shell.WriteErr(*prompt);
    }

    std::string text;          // the line's bytes (backslashes processed)
    std::vector<bool> split;   // per byte: an unprotected IFS character may split here
    bool newline = false;
    for (;;) {
        char c = 0;
        const ssize_t count = in ? in->Read(&c, 1) : 0;
        if (count == kIOInterrupted) {
            throw ShellStopped{};
        }
        if (count <= 0) {
            break;  // the end of the input, an empty slot, or a read error
        }
        if (c == '\n') {
            newline = true;
            break;
        }
        bool protectedByte = false;
        if (!raw && c == '\\') {
            char next = 0;
            const ssize_t more = in->Read(&next, 1);
            if (more == kIOInterrupted) {
                throw ShellStopped{};
            }
            if (more <= 0) {
                break;  // a trailing lone backslash is dropped
            }
            if (next == '\n') {
                continue;  // a joined line: both bytes are dropped
            }
            c = next;
            protectedByte = true;
        }
        text += c;
        split.push_back(!protectedByte);
    }

    const std::string ifs = shell.State().variables.Get("IFS").value_or(" \t\n");
    const auto isIfsWhite = [&ifs](char c) {
        return (c == ' ' || c == '\t' || c == '\n') && ifs.find(c) != std::string::npos;
    };
    const auto unprotectedIfsWhite = [&](size_t at) {
        return at < text.size() && split[at] && isIfsWhite(text[at]);
    };
    const auto unprotectedIfs = [&](size_t at) {
        return at < text.size() && split[at] && ifs.find(text[at]) != std::string::npos;
    };

    std::vector<std::string> values(names.size());
    size_t at = 0;
    while (unprotectedIfsWhite(at)) {
        ++at;  // leading IFS white space
    }
    for (size_t name = 0; name + 1 < names.size(); ++name) {
        std::string& field = values[name];
        while (at < text.size() && !unprotectedIfs(at)) {
            field += text[at++];
        }
        if (at < text.size()) {
            if (unprotectedIfsWhite(at)) {
                while (unprotectedIfsWhite(at)) {
                    ++at;  // IFS white-space runs collapse
                }
            } else {
                ++at;  // one non-white IFS character ...
                while (unprotectedIfsWhite(at)) {
                    ++at;  // ... with the white space after it, delimits once
                }
            }
        }
    }
    {
        // The last name: the rest of the line, trailing (unprotected) IFS
        // white space removed -- its leading was skipped above or by the
        // delimiter of the name before.
        std::string rest;
        std::vector<bool> restSplit;
        while (at < text.size()) {
            rest += text[at];
            restSplit.push_back(split[at]);
            ++at;
        }
        while (!rest.empty() && restSplit.back() && isIfsWhite(rest.back())) {
            rest.pop_back();
            restSplit.pop_back();
        }
        values.back() = rest;
    }
    for (size_t name = 0; name < names.size(); ++name) {
        shell.AssignVariable(names[name], values[name]);
    }
    return newline ? 0 : 1;
}

} // namespace Haisos::Hsh
