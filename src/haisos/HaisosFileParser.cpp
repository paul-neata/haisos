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

// Strips a trailing "# ..." comment: a '#' that starts the (trimmed) line, or
// is preceded by whitespace, starts a comment running to the end of the line.
std::string StripComment(const std::string& line) {
    for (size_t i = 0; i < line.size(); ++i) {
        if (line[i] == '#' && (i == 0 || line[i - 1] == ' ' || line[i - 1] == '\t')) {
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

std::vector<std::string> SplitWhitespace(const std::string& text) {
    std::vector<std::string> tokens;
    std::istringstream iss(text);
    std::string token;
    while (iss >> token) {
        tokens.push_back(token);
    }
    return tokens;
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
            if (!SubstituteOrFail(rest, values, lineNumber, &result.config.rootPath, &result.error)) {
                return result;
            }
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
            auto tokens = SplitWhitespace(substituted);
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
            } else if (decl.type == "SUB") {
                expectedArgs = 2;
            } else if (decl.type == "COMPOSED") {
                expectedArgs = 3;
            } else {
                result.error = "Error: line " + std::to_string(lineNumber) + ": unknown FS type '" + decl.type + "' (expected PHYSICAL, RO, MEM, SUB, or COMPOSED)\n";
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
            auto tokens = SplitWhitespace(substituted);
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
            // Substitution can leave nothing behind (e.g. "RUN ${empty}"), so
            // re-check before indexing into the token vector.
            auto tokens = SplitWhitespace(substituted);
            if (tokens.empty()) {
                result.error = "Error: line " + std::to_string(lineNumber) + ": RUN requires a program path\n";
                return result;
            }
            HaisosFileRunEntry entry;
            entry.programPath = tokens.front();
            entry.args.assign(tokens.begin() + 1, tokens.end());
            result.config.runEntries.push_back(std::move(entry));
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

std::string GetHaisosFileTemplate() {
    return
        "# haisosfile - a small manifest that boots a Haisos OS.\n"
        "# Comments start with '#' (full-line or trailing).\n"
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
        "#   FS <name> PHYSICAL <folder>       a real disk directory (relative to\n"
        "#                                     this file, or absolute; may use . and ..)\n"
        "#   FS <name> RO <other_fs>           a read-only wrapper over another declared FS\n"
        "#   FS <name> MEM                     an empty, in-memory read/write FS\n"
        "#   FS <name> SUB <other_fs> <folder> a filesystem confined to a sub-path of another FS\n"
        "FS workspace PHYSICAL .\n"
        "\n"
        "# MOUNT overlays one filesystem inside another at a path, overriding\n"
        "# anything already there: MOUNT <main_fs> <path> <fs_to_mount>\n"
        "# FS scratch MEM\n"
        "# MOUNT workspace /scratch scratch\n"
        "\n"
        "# ROOT selects which declared filesystem (by name) becomes this OS's\n"
        "# root. If omitted, the last FS declared above is used; if no FS is\n"
        "# declared at all, ROOT may instead be a plain directory path.\n"
        "ROOT workspace\n"
        "\n"
        "# RUN starts an initial process: a .md agent or a .lua script. May\n"
        "# repeat; Haisos exits once every RUN process has finished.\n"
        "RUN agent.md\n";
}

} // namespace Haisos
