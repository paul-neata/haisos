#pragma once
#include <string>

namespace Haisos {

inline std::string SanitizeUserInput(const std::string& input) {
    constexpr size_t MAX_USER_INPUT_SIZE = 64 * 1024;
    std::string result;
    result.reserve(input.size());

    size_t lineStart = 0;
    while (lineStart < input.size()) {
        size_t lineEnd = input.find('\n', lineStart);
        if (lineEnd == std::string::npos) {
            lineEnd = input.size();
        }
        std::string line = input.substr(lineStart, lineEnd - lineStart);

        std::string lowerLine = line;
        for (char& c : lowerLine) {
            if (c >= 'A' && c <= 'Z') {
                c = static_cast<char>(c + ('a' - 'A'));
            }
        }

        bool strip = false;
        if (lowerLine.find("ignore previous") != std::string::npos ||
            lowerLine.find("ignore all previous") != std::string::npos ||
            lowerLine.find("disregard previous") != std::string::npos ||
            lowerLine.find("forget previous") != std::string::npos ||
            lowerLine.find("system:") != std::string::npos ||
            lowerLine.find("you are now") != std::string::npos) {
            strip = true;
        }

        if (!strip) {
            bool inTag = false;
            for (char c : line) {
                if (c == '<') {
                    inTag = true;
                } else if (c == '>') {
                    inTag = false;
                } else if (!inTag) {
                    result.push_back(c);
                }
            }
        }

        if (lineEnd < input.size()) {
            result.push_back('\n');
        }
        lineStart = lineEnd + 1;
    }

    if (result.size() > MAX_USER_INPUT_SIZE) {
        result.resize(MAX_USER_INPUT_SIZE);
    }

    return result;
}

}
