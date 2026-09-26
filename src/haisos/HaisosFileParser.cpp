#include "HaisosFileParser.h"
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace Haisos {

namespace {

std::string Trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) {
        return "";
    }
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

bool IsBlank(char c) {
    return c == ' ' || c == '\t';
}

bool IsQuote(char c) {
    return c == '\'' || c == '"';
}

// Strips a trailing "# ..." comment: a '#' that starts the (trimmed) line, or
// is preceded by whitespace, starts a comment running to the end of the line
// -- unless it is inside a quoted token (see SplitTokens), which runs from a
// quote starting a token to the next quote of the same kind.
std::string StripComment(const std::string& line) {
    for (size_t i = 0; i < line.size(); ++i) {
        const bool startsToken = (i == 0 || IsBlank(line[i - 1]));
        if (startsToken && IsQuote(line[i])) {
            const size_t close = line.find(line[i], i + 1);
            if (close == std::string::npos) {
                return line;
            }
            i = close;
        } else if (startsToken && line[i] == '#') {
            return line.substr(0, i);
        }
    }
    return line;
}

// Substitutes every "${name}" in text against values. If a reference doesn't
// resolve against a known ARG/VAR name, sets *error (if not already set) and
// returns whatever partial result had been built so far -- callers must check
// *error after calling this and bail out (mirroring the other parse-error
// checks in ParseHaisosFile), rather than silently substituting empty string.
std::string Substitute(const std::string& text, const std::unordered_map<std::string, std::string>& values, std::string* error) {
    std::string result;
    result.reserve(text.size());
    for (size_t i = 0; i < text.size(); ) {
        if (text[i] == '$' && i + 1 < text.size() && text[i + 1] == '{') {
            size_t close = text.find('}', i + 2);
            if (close == std::string::npos) {
                result += text.substr(i);
                break;
            }
            std::string name = text.substr(i + 2, close - (i + 2));
            auto it = values.find(name);
            if (it != values.end()) {
                result += it->second;
            } else if (error != nullptr && error->empty()) {
                *error = "Unknown variable reference: ${" + name + "}\n";
            }
            i = close + 1;
        } else {
            result += text[i];
            ++i;
        }
    }
    return result;
}

// Substitutes text into *out, and on an unresolved reference writes this
// file's standard "Error: line <N>: ..." message into *error and returns
// false, so every call site can just bail out on a false return.
bool SubstituteOrFail(
    const std::string& text,
    const std::unordered_map<std::string, std::string>& values,
    int lineNumber,
    std::string* out,
    std::string* error)
{
    std::string subError;
    *out = Substitute(text, values, &subError);
    if (!subError.empty()) {
        *error = "Error: line " + std::to_string(lineNumber) + ": " + subError;
        return false;
    }
    return true;
}

std::string LineError(int lineNumber, const std::string& message) {
    return "Error: line " + std::to_string(lineNumber) + ": " + message + "\n";
}

// Splits text into tokens at whitespace. A token that starts with a quote --
// ' or " -- runs to the next quote of the same kind, which must end it, and is
// taken without them: "C:\Program Files\x" is one token, spaces and all.
// Nothing inside is special, '\' least of all, so a quoted token may hold the
// other kind of quote but not its own; "" is an empty token. A quote anywhere
// else in a token is part of it (it's). Returns false, with the error in
// outError, if a quote is never closed or the closing one is followed by more
// of the token.
bool SplitTokens(const std::string& text, int lineNumber, std::vector<std::string>& tokens, std::string& outError) {
    tokens.clear();
    size_t i = 0;
    while (true) {
        while (i < text.size() && IsBlank(text[i])) {
            ++i;
        }
        if (i >= text.size()) {
            return true;
        }
        if (IsQuote(text[i])) {
            const char quote = text[i];
            const size_t close = text.find(quote, i + 1);
            if (close == std::string::npos) {
                outError = LineError(lineNumber, std::string("unterminated ") + (quote == '"' ? "double" : "single") +
                    "-quoted text: " + text.substr(i));
                return false;
            }
            if (close + 1 < text.size() && !IsBlank(text[close + 1])) {
                outError = LineError(lineNumber, "unexpected text right after the closing quote: " + text.substr(i));
                return false;
            }
            tokens.push_back(text.substr(i + 1, close - i - 1));
            i = close + 1;
        } else {
            const size_t start = i;
            while (i < text.size() && !IsBlank(text[i])) {
                ++i;
            }
            tokens.push_back(text.substr(start, i - start));
        }
    }
}

// A line read on Windows, or from a file written there, still carries its '\r';
// it is part of the line ending, not of the line.
std::string StripCarriageReturn(std::string line) {
    if (!line.empty() && line.back() == '\r') {
        line.pop_back();
    }
    return line;
}

// Paths inside the Haisos OS are always given from its root.
bool IsAbsoluteOsPath(const std::string& path) {
    return !path.empty() && path[0] == '/';
}

// BUILTIN counts as one: it places a file on a filesystem, so it is bound by
// the same ordering -- after every FS/MOUNT/ROOT, before the first RUN.
bool IsFileDirective(const std::string& key) {
    return key == "CREATE" || key == "APPEND" || key == "CREATE_DIR" || key == "COPY" || key == "DELETE" ||
        key == "OUTCOPY" || key == "BUILTIN";
}

// Reads the content of a CREATE/APPEND directive from spec -- the raw text
// after its path, comments included, since a '#' may well be content. One of:
//   'text' or "text"   up to the next quote of the same kind; only whitespace
//                      or a comment may follow it
//   text:<anything>    the rest of the line, exactly as written
//   multiline <marker> the lines that follow, up to one that is only <marker>
//                      (whitespace around it ignored), joined by '\n'. The
//                      newline before the marker is not part of the text, so a
//                      trailing newline takes an empty line before the marker.
// Consumes the multiline block's lines from stream, advancing lineNumber.
bool ParseFileContent(
    const std::string& key,
    const std::string& spec,
    std::istream& stream,
    int& lineNumber,
    std::string& outContent,
    std::string& outError)
{
    const int directiveLine = lineNumber;
    if (!spec.empty() && (spec[0] == '\'' || spec[0] == '"')) {
        const char quote = spec[0];
        size_t close = spec.find(quote, 1);
        if (close == std::string::npos) {
            outError = LineError(directiveLine, key + ": unterminated " + (quote == '"' ? "double" : "single") + "-quoted text");
            return false;
        }
        std::string trailing = Trim(spec.substr(close + 1));
        if (!trailing.empty() && trailing[0] != '#') {
            outError = LineError(directiveLine, key + ": unexpected text after the closing quote: " + trailing);
            return false;
        }
        outContent = spec.substr(1, close - 1);
        return true;
    }

    static const std::string kTextPrefix = "text:";
    if (spec.compare(0, kTextPrefix.size(), kTextPrefix) == 0) {
        outContent = spec.substr(kTextPrefix.size());
        return true;
    }

    static const std::string kMultilinePrefix = "multiline";
    if (spec.compare(0, kMultilinePrefix.size(), kMultilinePrefix) == 0 &&
        (spec.size() == kMultilinePrefix.size() || spec[kMultilinePrefix.size()] == ' ' || spec[kMultilinePrefix.size()] == '\t')) {
        std::string marker = Trim(spec.substr(kMultilinePrefix.size()));
        if (marker.empty()) {
            outError = LineError(directiveLine, key + ": multiline requires an end marker (e.g. `multiline EOF`)");
            return false;
        }
        std::string content;
        bool first = true;
        std::string rawLine;
        while (std::getline(stream, rawLine)) {
            ++lineNumber;
            std::string line = StripCarriageReturn(rawLine);
            if (Trim(line) == marker) {
                outContent = std::move(content);
                return true;
            }
            if (!first) {
                content += '\n';
            }
            content += line;
            first = false;
        }
        outError = LineError(directiveLine, key + ": multiline text is never closed by a line containing only '" + marker + "'");
        return false;
    }

    outError = LineError(directiveLine, key + " requires content after the path: 'text', \"text\", text:<rest of line>, or multiline <marker>");
    return false;
}

} // namespace

HaisosFileParseResult ParseHaisosFile(
    const std::string& content,
    const std::vector<std::pair<std::string, std::string>>& argOverrides)
{
    HaisosFileParseResult result;
    std::unordered_map<std::string, std::string> values;
    std::unordered_set<std::string> declaredArgs;
    bool sawRoot = false;
    // Where the first RUN / file directive was, for the ordering rules: the
    // root filesystem cannot change once files have been written to it, and
    // the files are set up before any process starts.
    int firstRunLine = 0;
    int firstFileDirectiveLine = 0;

    std::istringstream stream(content);
    std::string rawLine;
    int lineNumber = 0;

    while (std::getline(stream, rawLine)) {
        ++lineNumber;
        std::string line = Trim(StripComment(rawLine));
        if (line.empty()) {
            continue;
        }

        size_t sep = line.find_first_of(" \t");
        std::string key = (sep == std::string::npos) ? line : line.substr(0, sep);
        std::string rest = (sep == std::string::npos) ? "" : Trim(line.substr(sep + 1));

        if ((key == "ROOT" || key == "FS" || key == "MOUNT") && firstFileDirectiveLine != 0) {
            result.error = LineError(lineNumber, key + " cannot come after the file directive on line " +
                std::to_string(firstFileDirectiveLine) + ": the root filesystem is fixed once files are written to it");
            return result;
        }
        if (IsFileDirective(key)) {
            if (key != "OUTCOPY" && firstRunLine != 0) {
                result.error = LineError(lineNumber, key + " cannot come after the RUN on line " +
                    std::to_string(firstRunLine) + ": files are set up before any process starts");
                return result;
            }
            if (firstFileDirectiveLine == 0) {
                firstFileDirectiveLine = lineNumber;
            }
        }

        if (key == "ARG") {
            auto eq = rest.find('=');
            std::string name = (eq == std::string::npos) ? rest : rest.substr(0, eq);
            const bool hasDefault = (eq != std::string::npos);
            std::string defaultValue;
            if (hasDefault && !SubstituteOrFail(rest.substr(eq + 1), values, lineNumber, &defaultValue, &result.error)) {
                return result;
            }
            name = Trim(name);
            if (name.empty()) {
                result.error = "Error: line " + std::to_string(lineNumber) + ": ARG requires a name\n";
                return result;
            }
            declaredArgs.insert(name);

            const std::string* overrideValue = nullptr;
            for (const auto& override : argOverrides) {
                if (override.first == name) {
                    overrideValue = &override.second;
                    break;
                }
            }
            // A bare "ARG name" (no '=') declares an argument with no default,
            // so it must be supplied via `-- name=value`; "ARG name=" declares
            // one that deliberately defaults to the empty string.
            if (overrideValue == nullptr && !hasDefault) {
                result.error = "Error: line " + std::to_string(lineNumber) + ": ARG '" + name + "' has no default value and was not provided (pass `-- " + name + "=value`)\n";
                return result;
            }
            values[name] = (overrideValue != nullptr) ? *overrideValue : defaultValue;
        } else if (key == "VAR") {
            auto eq = rest.find('=');
            if (eq == std::string::npos) {
                result.error = "Error: line " + std::to_string(lineNumber) + ": VAR requires name=value\n";
                return result;
            }
            std::string name = Trim(rest.substr(0, eq));
            if (name.empty()) {
                result.error = "Error: line " + std::to_string(lineNumber) + ": VAR requires a name\n";
                return result;
            }
            std::string value;
            if (!SubstituteOrFail(rest.substr(eq + 1), values, lineNumber, &value, &result.error)) {
                return result;
            }
            values[name] = value;
        } else if (key == "ENV") {
            if (rest.empty()) {
                result.error = "Error: line " + std::to_string(lineNumber) + ": ENV requires a name\n";
                return result;
            }
            HaisosFileEnvEntry entry;
            auto eq = rest.find('=');
            if (eq == std::string::npos) {
                // `ENV NAME`: import NAME from the host environment.
                entry.name = Trim(rest);
                entry.importFromHost = true;
            } else {
                entry.name = Trim(rest.substr(0, eq));
                if (!SubstituteOrFail(rest.substr(eq + 1), values, lineNumber, &entry.value, &result.error)) {
                    return result;
                }
            }
            if (entry.name.empty()) {
                result.error = "Error: line " + std::to_string(lineNumber) + ": ENV requires a name\n";
                return result;
            }
            result.config.envEntries.push_back(std::move(entry));
        } else if (key == "ROOT") {
            if (sawRoot) {
                result.error = "Error: line " + std::to_string(lineNumber) + ": only one ROOT directive is allowed\n";
                return result;
            }
            if (rest.empty()) {
                result.error = "Error: line " + std::to_string(lineNumber) + ": ROOT requires a directory\n";
                return result;
            }
            std::string substituted;
            if (!SubstituteOrFail(rest, values, lineNumber, &substituted, &result.error)) {
                return result;
            }
            std::vector<std::string> tokens;
            if (!SplitTokens(substituted, lineNumber, tokens, result.error)) {
                return result;
            }
            if (tokens.size() != 1 || tokens[0].empty()) {
                result.error = LineError(lineNumber, "ROOT takes one filesystem name, or one directory (quote a path holding spaces)");
                return result;
            }
            result.config.rootPath = tokens[0];
            sawRoot = true;
        } else if (key == "FS") {
            if (rest.empty()) {
                result.error = "Error: line " + std::to_string(lineNumber) + ": FS requires a name and a filesystem type\n";
                return result;
            }
            std::string substituted;
            if (!SubstituteOrFail(rest, values, lineNumber, &substituted, &result.error)) {
                return result;
            }
            std::vector<std::string> tokens;
            if (!SplitTokens(substituted, lineNumber, tokens, result.error)) {
                return result;
            }
            if (tokens.size() < 2) {
                result.error = "Error: line " + std::to_string(lineNumber) + ": FS requires a name and a filesystem type\n";
                return result;
            }

            HaisosFileFilesystemDecl decl;
            decl.name = tokens[0];
            decl.type = tokens[1];
            decl.args.assign(tokens.begin() + 2, tokens.end());

            size_t expectedArgs = 0;
            if (decl.type == "PHYSICAL") {
                expectedArgs = 1;
            } else if (decl.type == "RO") {
                expectedArgs = 1;
            } else if (decl.type == "MEM") {
                expectedArgs = 0;
            } else if (decl.type == "DEV") {
                expectedArgs = 0;
            } else if (decl.type == "SUB") {
                expectedArgs = 2;
            } else if (decl.type == "COMPOSED") {
                expectedArgs = 3;
            } else {
                result.error = "Error: line " + std::to_string(lineNumber) + ": unknown FS type '" + decl.type + "' (expected PHYSICAL, RO, MEM, DEV, SUB, or COMPOSED)\n";
                return result;
            }
            if (decl.args.size() != expectedArgs) {
                result.error = "Error: line " + std::to_string(lineNumber) + ": FS " + decl.type + " expects " + std::to_string(expectedArgs) + " argument(s), got " + std::to_string(decl.args.size()) + "\n";
                return result;
            }

            HaisosFileFsStep step;
            step.isMount = false;
            step.declare = std::move(decl);
            result.config.fsSteps.push_back(std::move(step));
        } else if (key == "MOUNT") {
            std::string substituted;
            if (!SubstituteOrFail(rest, values, lineNumber, &substituted, &result.error)) {
                return result;
            }
            std::vector<std::string> tokens;
            if (!SplitTokens(substituted, lineNumber, tokens, result.error)) {
                return result;
            }
            if (tokens.size() != 3) {
                result.error = "Error: line " + std::to_string(lineNumber) + ": MOUNT requires <main_fs> <path> <fs_to_mount>\n";
                return result;
            }

            HaisosFileFsStep step;
            step.isMount = true;
            step.mount.mainFs = tokens[0];
            step.mount.path = tokens[1];
            step.mount.toBeMountedFs = tokens[2];
            result.config.fsSteps.push_back(std::move(step));
        } else if (key == "RUN") {
            if (rest.empty()) {
                result.error = "Error: line " + std::to_string(lineNumber) + ": RUN requires a program path\n";
                return result;
            }
            std::string substituted;
            if (!SubstituteOrFail(rest, values, lineNumber, &substituted, &result.error)) {
                return result;
            }
            std::vector<std::string> tokens;
            if (!SplitTokens(substituted, lineNumber, tokens, result.error)) {
                return result;
            }
            HaisosFileRunEntry entry;
            // "-i" is only an option in front of the program, never after it:
            // everything after the program is the program's own arguments.
            if (!tokens.empty() && tokens.front() == "-i") {
                entry.interactive = true;
                tokens.erase(tokens.begin());
            }
            // Substitution can leave nothing behind (e.g. "RUN ${empty}"), so
            // re-check before indexing into the token vector.
            if (tokens.empty()) {
                result.error = "Error: line " + std::to_string(lineNumber) + ": RUN requires a program path\n";
                return result;
            }
            if (!IsAbsoluteOsPath(tokens.front())) {
                result.error = LineError(lineNumber, "RUN requires an absolute program path (starting with '/'), got '" + tokens.front() + "'");
                return result;
            }
            entry.programPath = tokens.front();
            entry.args.assign(tokens.begin() + 1, tokens.end());
            if (firstRunLine == 0) {
                firstRunLine = lineNumber;
            }
            result.config.runEntries.push_back(std::move(entry));
        } else if (key == "CREATE" || key == "APPEND") {
            // Parsed from the raw line rather than the comment-stripped one: a
            // '#' in the content is content.
            std::string raw = StripCarriageReturn(rawLine);
            size_t keyStart = raw.find_first_not_of(" \t");
            size_t pathStart = raw.find_first_not_of(" \t", keyStart + key.size());
            if (pathStart == std::string::npos) {
                result.error = LineError(lineNumber, key + " requires a file path and its content");
                return result;
            }
            // The path is a token like any other: quoted, it may hold spaces.
            size_t pathEnd = std::string::npos;
            std::string rawPath;
            if (IsQuote(raw[pathStart])) {
                const size_t close = raw.find(raw[pathStart], pathStart + 1);
                if (close == std::string::npos || (close + 1 < raw.size() && !IsBlank(raw[close + 1]))) {
                    result.error = LineError(lineNumber, key + ": the quoted path is " +
                        (close == std::string::npos ? "never closed" : "followed by more of it right after its closing quote"));
                    return result;
                }
                rawPath = raw.substr(pathStart + 1, close - pathStart - 1);
                pathEnd = (close + 1 < raw.size()) ? close + 1 : std::string::npos;
            } else {
                pathEnd = raw.find_first_of(" \t", pathStart);
                rawPath = raw.substr(pathStart, pathEnd == std::string::npos ? std::string::npos : pathEnd - pathStart);
            }
            size_t specStart = (pathEnd == std::string::npos) ? std::string::npos : raw.find_first_not_of(" \t", pathEnd);
            std::string spec = (specStart == std::string::npos) ? "" : raw.substr(specStart);

            HaisosFileOperation operation;
            operation.type = (key == "CREATE") ? HaisosFileOperationType::Create : HaisosFileOperationType::Append;
            operation.lineNumber = lineNumber;
            if (!SubstituteOrFail(rawPath, values, lineNumber, &operation.path, &result.error)) {
                return result;
            }
            if (!IsAbsoluteOsPath(operation.path)) {
                result.error = LineError(lineNumber, key + " requires an absolute file path (starting with '/'), got '" + operation.path + "'");
                return result;
            }
            // The content is taken literally: no ${name} substitution, since
            // it is often code or markup where "${" means something else.
            if (!ParseFileContent(key, spec, stream, lineNumber, operation.content, result.error)) {
                return result;
            }
            result.config.setupOperations.push_back(std::move(operation));
        } else if (key == "BUILTIN") {
            std::string substituted;
            if (!SubstituteOrFail(rest, values, lineNumber, &substituted, &result.error)) {
                return result;
            }
            std::vector<std::string> tokens;
            if (!SplitTokens(substituted, lineNumber, tokens, result.error)) {
                return result;
            }
            if (tokens.size() < 3) {
                result.error = LineError(lineNumber, "BUILTIN requires <fs_name> <builtin_name> <absolute_path>...");
                return result;
            }
            for (size_t i = 2; i < tokens.size(); ++i) {
                if (!IsAbsoluteOsPath(tokens[i])) {
                    result.error = LineError(lineNumber, "BUILTIN requires absolute paths (starting with '/'), got '" + tokens[i] + "'");
                    return result;
                }
                HaisosFileOperation operation;
                operation.type = HaisosFileOperationType::Builtin;
                operation.lineNumber = lineNumber;
                operation.fsName = tokens[0];
                operation.builtinName = tokens[1];
                operation.path = tokens[i];
                result.config.setupOperations.push_back(std::move(operation));
            }
        } else if (key == "COPY" || key == "OUTCOPY" || key == "DELETE" || key == "CREATE_DIR") {
            std::string substituted;
            if (!SubstituteOrFail(rest, values, lineNumber, &substituted, &result.error)) {
                return result;
            }
            std::vector<std::string> tokens;
            if (!SplitTokens(substituted, lineNumber, tokens, result.error)) {
                return result;
            }
            HaisosFileOperation operation;
            operation.lineNumber = lineNumber;
            if (key == "DELETE" || key == "CREATE_DIR") {
                if (tokens.size() != 1) {
                    result.error = LineError(lineNumber, key + " requires exactly one path");
                    return result;
                }
                operation.type = (key == "DELETE") ? HaisosFileOperationType::Delete : HaisosFileOperationType::CreateDir;
                operation.path = tokens[0];
            } else if (key == "COPY") {
                if (tokens.size() != 2) {
                    result.error = LineError(lineNumber, "COPY requires <host_path> <absolute_path>");
                    return result;
                }
                operation.type = HaisosFileOperationType::Copy;
                operation.hostPath = tokens[0];
                operation.path = tokens[1];
            } else {
                if (tokens.size() != 2) {
                    result.error = LineError(lineNumber, "OUTCOPY requires <absolute_path> <host_path>");
                    return result;
                }
                operation.type = HaisosFileOperationType::OutCopy;
                operation.path = tokens[0];
                operation.hostPath = tokens[1];
            }
            if (!IsAbsoluteOsPath(operation.path)) {
                result.error = LineError(lineNumber, key + " requires an absolute path inside the OS (starting with '/'), got '" + operation.path + "'");
                return result;
            }
            if (operation.type == HaisosFileOperationType::OutCopy) {
                result.config.outCopyOperations.push_back(std::move(operation));
            } else {
                result.config.setupOperations.push_back(std::move(operation));
            }
        } else {
            result.error = "Error: line " + std::to_string(lineNumber) + ": unknown directive '" + key + "'\n";
            return result;
        }
    }

    // An override naming an ARG the haisosfile never declares would otherwise
    // be silently discarded, hiding a typo in `-- name=value`.
    for (const auto& override : argOverrides) {
        if (declaredArgs.find(override.first) == declaredArgs.end()) {
            result.error = "Error: unknown argument override '" + override.first + "' (no ARG with that name is declared in the haisosfile)\n";
            return result;
        }
    }

    if (result.config.runEntries.empty()) {
        result.error = "Error: haisosfile must have at least one RUN directive\n";
        return result;
    }

    return result;
}

std::string GetHaisosFileTemplate(const std::vector<std::string>& builtinNames) {
    std::string builtinExamples;
    for (const auto& name : builtinNames) {
        builtinExamples += "# BUILTIN rootfs " + name + " /bin/" + name + "\n";
    }

    return
        "# haisosfile - a small manifest that boots a Haisos OS.\n"
        "# Comments start with '#' (full-line, or trailing after whitespace).\n"
        "# Any token may be quoted, '...' or \"...\", to hold spaces or a '#'; nothing\n"
        "# inside the quotes is special, so \"C:\\Program Files\\x\" is taken as written.\n"
        "\n"
        "# ARG declares an argument, overridable from the command line via\n"
        "# `haisos -- name=value`. The value here is the default; written\n"
        "# without a '=' (just `ARG name`) the argument has no default and\n"
        "# must be supplied on the command line.\n"
        "ARG name=World\n"
        "\n"
        "# VAR declares a variable; its value may reference ${ARG} or ${VAR} names\n"
        "# declared above it.\n"
        "VAR greeting=Hello-${name}\n"
        "\n"
        "# ENV sets a variable in this OS's environment, which every process and\n"
        "# sub-OS inherits. `ENV NAME=value` sets it outright; `ENV NAME` alone\n"
        "# imports NAME from the host OS -- the only way a host variable gets in.\n"
        "#\n"
        "# The LLM configuration is read from here and has no built-in default:\n"
        "# whatever these say is what agents will talk to. Swap either line for\n"
        "# the bare `ENV HAISOS_ENDPOINT` form to take the host's value instead.\n"
        "ENV HAISOS_ENDPOINT=http://localhost:11434/api/chat\n"
        "ENV HAISOS_MODEL=llama3\n"
        "# ENV HAISOS_API_KEY          # import the host's key, if one is needed\n"
        "# ENV GREETING=${greeting}\n"
        "\n"
        "# FS declares a named filesystem: FS <name> <type> <args...>\n"
        "# An FS may only refer to filesystems declared above it. Uncomment an\n"
        "# example by deleting its leading '#'; the note after it stays a comment.\n"
        "FS rootfs PHYSICAL .                   # this haisosfile's own directory\n"
        "# FS data PHYSICAL ./data              # a real disk directory (relative to this file, or absolute; may use . and ..)\n"
        "# FS docs PHYSICAL \"/c/My Documents\"  # on Windows /c/x, c:\\x and c:/x are all C:\\x, and / is every drive\n"
        "# FS scratch MEM                       # an empty, in-memory read/write filesystem\n"
        "# FS devices DEV                       # device files, as Linux's /dev: null and zero (see below)\n"
        "# FS readonly RO rootfs                # a read-only wrapper over another declared FS\n"
        "# FS tools SUB rootfs tools            # confined to a sub-path (here tools/) of another declared FS\n"
        "# FS combined COMPOSED rootfs /scratch scratch   # rootfs with scratch overlaid at /scratch, both left untouched\n"
        "\n"
        "# MOUNT overlays one filesystem inside another at a path, in place,\n"
        "# overriding anything already there: MOUNT <main_fs> <path> <fs_to_mount>\n"
        "# MOUNT rootfs /scratch scratch\n"
        "\n"
        "# /dev, as on Linux: uncomment both lines to give every process\n"
        "# /dev/null (writes vanish, reads end at once) and /dev/zero (writes\n"
        "# vanish, reads give endless zero bytes). Nothing can be created or\n"
        "# deleted in it. Shell commands redirect to /dev/null all the time.\n"
        "# FS devfs DEV\n"
        "# MOUNT rootfs /dev devfs\n"
        "\n"
        "# ROOT selects which declared filesystem (by name) becomes this OS's\n"
        "# root. If omitted, the last FS declared above is used; if no FS is\n"
        "# declared at all, ROOT may instead be a plain directory path.\n"
        "ROOT rootfs\n"
        "\n"
        "# The directives below work on files of the root filesystem (BUILTIN on\n"
        "# those of any declared FS), so the filesystems are fixed from the first\n"
        "# of them on: no ROOT, FS or MOUNT may follow one. Paths inside the OS are\n"
        "# absolute (they start at the root, '/'); paths on the host are relative\n"
        "# to this file, or absolute. All but OUTCOPY run before any process\n"
        "# starts, so they must come before the first RUN.\n"
        "#\n"
        "# CREATE_DIR <path> creates a directory, and any missing parents; it is\n"
        "# fine if it already exists.\n"
        "# CREATE_DIR /bin\n"
        "#\n"
        "# BUILTIN <fs_name> <builtin_name> <path>... places a builtin command -- one\n"
        "# compiled into Haisos -- on a declared FS at each path given; RUN (or\n"
        "# os_start_process) on that path then runs it. Its directory must already\n"
        "# exist. The file reads as a note naming the builtin, cannot be written,\n"
        "# and its directory cannot be deleted while it is there. Every builtin:\n"
        + builtinExamples +
        "#\n"
        "# CREATE <path> <content> writes a file, replacing it if it exists;\n"
        "# APPEND <path> <content> appends to one, creating it if it does not.\n"
        "# Missing parent directories are created. The content is taken\n"
        "# literally (no ${name} substitution) and is one of:\n"
        "#   'text' or \"text\"      up to the matching quote; a comment may follow\n"
        "#   text:<rest of line>   everything after 'text:', exactly as written\n"
        "#   multiline <marker>    the lines below, up to a line that is only <marker>;\n"
        "#                         the newline before the marker is dropped, so end\n"
        "#                         with an empty line to keep a final newline\n"
        "# CREATE /notes/hello.txt 'Hello, world'\n"
        "# APPEND /notes/hello.txt text: and # this is content, not a comment\n"
        "# CREATE /notes/poem.md multiline END\n"
        "# # A heading, kept as-is\n"
        "# Roses are red.\n"
        "#\n"
        "# END\n"
        "#\n"
        "# COPY <host_path> <path> copies a host file into the OS.\n"
        "# COPY ./input.txt /work/input.txt\n"
        "#\n"
        "# DELETE <path> removes a file, or a directory and everything in it.\n"
        "# DELETE /work/stale\n"
        "#\n"
        "# OUTCOPY <path> <host_path> is COPY's inverse: once every RUN process has\n"
        "# finished, it copies a file out of the OS onto the host.\n"
        "# OUTCOPY /work/result.txt ./out/result.txt\n"
        "\n"
        "# RUN starts an initial process: a .md agent, a .lua script or a builtin\n"
        "# (e.g. `RUN /bin/ls -l /`), given by its absolute path inside the OS,\n"
        "# followed by its arguments. It starts in the root directory, '/'. RUN\n"
        "# may repeat; Haisos exits once every RUN process has finished.\n"
        "#\n"
        "# `RUN -i <agent.md>` runs an agent interactively: after its program, each\n"
        "# line typed on the console is sent to it, until it closes itself (with\n"
        "# its self_close tool) or input ends.\n"
        "# RUN -i /chat.md\n"
        "RUN /agent.md\n";
}

} // namespace Haisos
