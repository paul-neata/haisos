#include "commands/awk/AwkError.h"

namespace Haisos::Awk {

std::string AwkLocationPrefix(const std::string& sourceName, int line) {
    return std::string(kAwkName) + ": " + sourceName + ":" + std::to_string(line) + ": ";
}

AwkFatal::AwkFatal(const std::string& message, bool withLocation)
    : std::runtime_error(message)
    , m_withLocation(withLocation) {
}

AwkSyntaxError::AwkSyntaxError(const std::string& message, std::string sourceName, int line,
                               std::string lineText, size_t column)
    : std::runtime_error(message)
    , m_sourceName(std::move(sourceName))
    , m_line(line)
    , m_lineText(std::move(lineText))
    , m_column(column) {
}

std::string FormatAwkSyntaxError(const AwkSyntaxError& error) {
    const std::string prefix = AwkLocationPrefix(error.SourceName(), error.Line());
    std::string text = prefix + error.LineText() + "\n";
    text += prefix;
    const size_t caret = error.Column() < error.LineText().size() ? error.Column() : error.LineText().size();
    for (size_t i = 0; i < caret; ++i) {
        text += error.LineText()[i] == '\t' ? '\t' : ' ';
    }
    text += "^ " + std::string(error.what()) + "\n";
    return text;
}

std::string FormatAwkWarning(const AwkWarning& warning) {
    return AwkLocationPrefix(warning.sourceName, warning.line) + "warning: " + warning.message + "\n";
}

std::string FormatAwkError(const std::string& sourceName, int line, const std::string& message) {
    return AwkLocationPrefix(sourceName, line) + "error: " + message + "\n";
}

} // namespace Haisos::Awk