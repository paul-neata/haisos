#include "HaisosFileParser.h"
#include <sstream>
#include <unordered_map>

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

std::string Substitute(const std::string& text, const std::unordered_map<std::string, std::string>& values) {
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
            }
            i = close + 1;
        } else {
            result += text[i];
            ++i;
        }
    }
    return result;
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
            std::string defaultValue = (eq == std::string::npos) ? "" : Substitute(rest.substr(eq + 1), values);
            name = Trim(name);
            if (name.empty()) {
                result.error = "Error: line " + std::to_string(lineNumber) + ": ARG requires a name\n";
                return result;
            }

            std::string value = defaultValue;
            for (const auto& override : argOverrides) {
                if (override.first == name) {
                    value = override.second;
                    break;
                }
            }
            values[name] = value;
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
            values[name] = Substitute(rest.substr(eq + 1), values);
        } else if (key == "ROOT") {
            if (sawRoot) {
                result.error = "Error: line " + std::to_string(lineNumber) + ": only one ROOT directive is allowed\n";
                return result;
            }
            if (rest.empty()) {
                result.error = "Error: line " + std::to_string(lineNumber) + ": ROOT requires a directory\n";
                return result;
            }
            result.config.rootPath = Substitute(rest, values);
            sawRoot = true;
        } else if (key == "RUN") {
            if (rest.empty()) {
                result.error = "Error: line " + std::to_string(lineNumber) + ": RUN requires a program path\n";
                return result;
            }
            auto tokens = SplitWhitespace(Substitute(rest, values));
            HaisosFileRunEntry entry;
            entry.programPath = tokens.front();
            entry.args.assign(tokens.begin() + 1, tokens.end());
            result.config.runEntries.push_back(std::move(entry));
        } else {
            result.error = "Error: line " + std::to_string(lineNumber) + ": unknown directive '" + key + "'\n";
            return result;
        }
    }

    if (result.config.runEntries.empty()) {
        result.error = "Error: haisosfile must have at least one RUN directive\n";
        return result;
    }

    return result;
}

} // namespace Haisos
