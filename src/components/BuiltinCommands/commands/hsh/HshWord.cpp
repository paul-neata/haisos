#include "commands/hsh/HshWord.h"

namespace Haisos::Hsh {

namespace {

bool IsNameStart(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
}

bool IsNameChar(char c) {
    return IsNameStart(c) || (c >= '0' && c <= '9');
}

const char* ParameterOpText(ParameterOp op) {
    switch (op) {
        case ParameterOp::UseDefault:           return ":-";
        case ParameterOp::UseDefaultIfUnset:    return "-";
        case ParameterOp::AssignDefault:        return ":=";
        case ParameterOp::AssignDefaultIfUnset: return "=";
        case ParameterOp::ErrorIfNull:          return ":?";
        case ParameterOp::ErrorIfUnset:         return "?";
        case ParameterOp::UseAlternative:       return ":+";
        case ParameterOp::UseAlternativeIfSet:  return "+";
        case ParameterOp::RemoveSmallestSuffix: return "%";
        case ParameterOp::RemoveLargestSuffix:  return "%%";
        case ParameterOp::RemoveSmallestPrefix: return "#";
        case ParameterOp::RemoveLargestPrefix:  return "##";
        default:                                return "";
    }
}

std::string DescribePart(const WordPart& part);

std::string DescribeParts(const std::vector<WordPart>& parts) {
    std::string out;
    bool first = true;
    for (const WordPart& part : parts) {
        if (!first) {
            out += ' ';
        }
        out += DescribePart(part);
        first = false;
    }
    return out;
}

std::string DescribePart(const WordPart& part) {
    switch (part.kind) {
        case WordPartKind::Literal:
            return "L'" + part.text + "'";
        case WordPartKind::Quoted:
            return "Q'" + part.text + "'";
        case WordPartKind::DoubleQuoted:
            return "D[" + DescribeParts(part.parts) + "]";
        case WordPartKind::Parameter:
            switch (part.op) {
                case ParameterOp::None:
                    return "P(" + part.text + ")";
                case ParameterOp::Length:
                    return "P(#" + part.text + ")";
                case ParameterOp::Bad:
                    return "P(<bad>)";
                default:
                    return "P(" + part.text + ParameterOpText(part.op) +
                           "[" + DescribeParts(part.parts) + "])";
            }
        case WordPartKind::CommandSubstitution:
            return std::string(part.backquoted ? "B'" : "C'") + part.text + "'";
        case WordPartKind::Arithmetic:
            return "A[" + DescribeParts(part.parts) + "]";
    }
    return "";
}

} // namespace

bool IsValidShellName(std::string_view name) {
    if (name.empty() || !IsNameStart(name.front())) {
        return false;
    }
    for (char c : name) {
        if (!IsNameChar(c)) {
            return false;
        }
    }
    return true;
}

std::optional<std::string> LiteralText(const Word& word) {
    std::string text;
    for (const WordPart& part : word.parts) {
        if (part.kind != WordPartKind::Literal) {
            return std::nullopt;
        }
        text += part.text;
    }
    return text;
}

std::string DescribeWord(const Word& word) {
    return DescribeParts(word.parts);
}

} // namespace Haisos::Hsh
