#include <cmath>
#include <limits>
#include <string>
#include <vector>

#include "commands/awk/AwkInterpreter.h"

namespace Haisos::Awk {
namespace {

// The `&' and `\' of a replacement, expanded for one match: `&' the matched
// text, `\&' a literal `&', `\\' one `\'; a `\' before any other byte (and
// a final lone one) kept as written.
std::string ExpandReplacement(std::string_view replacement, std::string_view matched) {
    std::string out;
    for (size_t i = 0; i < replacement.size(); ++i) {
        const char c = replacement[i];
        if (c == '&') {
            out.append(matched);
        } else if (c == '\\' && i + 1 < replacement.size()) {
            const char d = replacement[i + 1];
            if (d == '\\' || d == '&') {
                out += d;
                ++i;
            } else {
                out += '\\';
            }
        } else {
            out += c;
        }
    }
    return out;
}

// One substitution pass over |text|, its result and substitution count in
// |result| and |count|. sub replaces the first match only; gsub goes on: an
// empty match exactly where the previous match ended is not a substitution
// (the byte there is copied and the search moves past it), any other match
// copies the text before it, the replacement, and -- for an empty match --
// the byte at its position, continuing one past it.
void Substitute(const std::string& text, const Regex& regex, const std::string& replacement,
                bool global, std::string& result, int& count) {
    if (!global) {
        RegexMatch match;
        if (!regex.Search(text, 0, match)) {
            result = text;
            return;
        }
        const size_t begin = static_cast<size_t>(match.groups[0].first);
        const size_t end = static_cast<size_t>(match.groups[0].second);
        ++count;
        result = text.substr(0, begin)
               + ExpandReplacement(replacement,
                                   std::string_view(text).substr(begin, end - begin))
               + text.substr(end);
        return;
    }
    size_t pos = 0;
    ptrdiff_t lastEnd = -1;   // where the previous match ended
    while (pos <= text.size()) {
        RegexMatch match;
        if (!regex.Search(text, pos, match)) {
            break;
        }
        const ptrdiff_t begin = match.groups[0].first;
        const ptrdiff_t end = match.groups[0].second;
        if (begin == end && begin == lastEnd) {
            if (static_cast<size_t>(begin) < text.size()) {
                result += text[static_cast<size_t>(begin)];
            }
            pos = static_cast<size_t>(begin) + 1;
            continue;
        }
        ++count;
        result.append(text, pos, static_cast<size_t>(begin) - pos);
        result += ExpandReplacement(replacement,
                                    std::string_view(text).substr(
                                        static_cast<size_t>(begin),
                                        static_cast<size_t>(end - begin)));
        if (begin == end) {
            if (static_cast<size_t>(begin) < text.size()) {
                result += text[static_cast<size_t>(begin)];
            }
            pos = static_cast<size_t>(begin) + 1;
        } else {
            pos = static_cast<size_t>(end);
        }
        lastEnd = end;
    }
    // An empty match at the very end leaves pos one past size: nothing
    // of the text is left to copy.
    if (pos <= text.size()) {
        result.append(text, pos, text.size() - pos);
    }
}

} // namespace

// --- the built-in string functions ---

Value Interpreter::CallBuiltin(const Expr& call) {
    const std::vector<ExprPtr>& args = call.operands;
    const std::string& convfmt = SpecialString(kSlotCONVFMT);

    if (call.text == "length") {
        std::string text;
        if (args.empty()) {
            text = m_fields.Record(convfmt);   // length() is length($0)
        } else if (args[0]->kind == ExprKind::Variable && !args[0]->parenthesized
                   && args[0]->slot != kSlotNF) {
            // A variable argument is its variable: an array is refused, an
            // untyped one taken as (and made) a scalar.
            Variable& variable = VariableOf(*args[0]);
            if (variable.kind == Variable::Kind::Array) {
                throw AwkFatal("length: received array argument");
            }
            text = ScalarRefOf(args[0]->slot, args[0]->localSlot, args[0]->text)
                       .ToString(convfmt);
        } else {
            text = ValueOf(*args[0]).ToString(convfmt);
        }
        return Value::FromNumber(static_cast<double>(text.size()));
    }

    if (call.text == "substr") {
        const std::string text = ValueOf(*args[0]).ToString(convfmt);
        // The length: to the end without a third argument; NaN, 0 and a
        // negative number are none at all.
        double length = std::numeric_limits<double>::infinity();
        if (args.size() == 3) {
            length = std::trunc(ValueOf(*args[2]).ToNumber());
            if (std::isnan(length) || length <= 0) {
                return Value::FromString("");
            }
        }
        // The start: truncated toward zero; below 1 it is 1, without
        // shortening the length; NaN is 1; +inf is past the end.
        double start = ValueOf(*args[1]).ToNumber();
        if (std::isnan(start)) {
            start = 1;
        } else {
            start = std::trunc(start);
            if (std::isinf(start)) {
                if (start > 0) {
                    return Value::FromString("");
                }
                start = 1;
            } else if (start < 1) {
                start = 1;
            }
        }
        const double size = static_cast<double>(text.size());
        const double first = start - 1;
        if (first >= size) {
            return Value::FromString("");
        }
        double count = size - first;
        if (length < count) {
            count = length;
        }
        return Value::FromString(text.substr(static_cast<size_t>(first),
                                             static_cast<size_t>(count)));
    }

    if (call.text == "index") {
        const std::string s = ValueOf(*args[0]).ToString(convfmt);
        const std::string t = ValueOf(*args[1]).ToString(convfmt);
        const size_t at = s.find(t);   // an empty t is found at 1
        return Value::FromNumber(at == std::string::npos ? 0 : static_cast<double>(at) + 1);
    }

    if (call.text == "split") {
        if (args.size() == 4) {
            throw AwkFatal("split: fourth argument is a gawk extension");
        }
        const std::string text = ValueOf(*args[0]).ToString(convfmt);
        // The array: a plain variable used as an array -- a constant, an
        // element, a special variable or a scalar is not one.
        const Expr& arrayExpr = *args[1];
        if (arrayExpr.kind != ExprKind::Variable || arrayExpr.parenthesized
            || arrayExpr.slot == kSlotNF) {
            throw AwkFatal("split: second argument is not an array");
        }
        Variable& variable = VariableOf(arrayExpr);
        if (variable.kind == Variable::Kind::Scalar) {
            throw AwkFatal("split: second argument is not an array");
        }
        AwkArray& array = arrayExpr.localSlot >= 0
                              ? ArrayRef(variable, arrayExpr.text)
                              : ArrayRef(arrayExpr.slot, arrayExpr.text);
        array.Clear();   // first of all, even for an empty s
        std::vector<std::string> pieces;
        if (args.size() == 3) {
            const Expr& separator = *args[2];
            if (separator.kind == ExprKind::Regex && !separator.parenthesized) {
                SplitByRegex(text, *separator.compiledRegex, pieces);
            } else {
                const std::string fs = ValueOf(separator).ToString(convfmt);
                if (fs == " ") {
                    SplitAwkFields(text, fs, pieces);   // runs of blanks
                } else if (fs.empty()) {
                    // One piece per byte, unlike FS "".
                    pieces.reserve(text.size());
                    for (size_t i = 0; i < text.size(); ++i) {
                        pieces.push_back(text.substr(i, 1));
                    }
                } else if (fs.size() == 1) {
                    SplitAwkFields(text, fs, pieces);   // the byte literally
                } else {
                    std::shared_ptr<const Regex> regex = RegexOperand(separator);
                    SplitByRegex(text, *regex, pieces);
                }
            }
        } else {
            // No separator: the current FS, with the record rules.
            SplitRecord(text, SpecialString(kSlotFS), /*paragraphMode=*/false, pieces);
        }
        for (size_t i = 0; i < pieces.size(); ++i) {
            array.GetOrCreate(std::to_string(i + 1)) = Value::FromInput(pieces[i]);
        }
        return Value::FromNumber(static_cast<double>(pieces.size()));
    }

    if (call.text == "sub" || call.text == "gsub") {
        const bool global = call.text == "gsub";
        const std::shared_ptr<const Regex> regex = RegexOperand(*args[0]);
        const std::string replacement = ValueOf(*args[1]).ToString(convfmt);
        // The target: $0 by default; an lvalue assigned the result (so $0
        // is re-split and a field rebuilds $0), anything else a temporary
        // worked on and dropped.
        const Expr* target = args.size() >= 3 ? args[2].get() : nullptr;
        Place place;
        if (target != nullptr && IsLvalue(*target)) {
            place = PlaceOf(*target);
        }
        std::string text;
        if (target == nullptr) {
            text = m_fields.Record(convfmt);
        } else if (place.lvalue != nullptr) {
            text = ReadPlace(place).ToString(convfmt);
        } else {
            text = ValueOf(*target).ToString(convfmt);
        }
        std::string result;
        int count = 0;
        Substitute(text, *regex, replacement, global, result, count);
        if (count > 0) {
            // Nothing assigned without a match: the target keeps its value
            // and its type.
            if (target == nullptr) {
                m_fields.SetRecord(result, SpecialString(kSlotFS),
                                   SpecialString(kSlotRS).empty());
            } else if (place.lvalue != nullptr) {
                WritePlace(place, Value::FromString(result));
            }
        }
        return Value::FromNumber(static_cast<double>(count));
    }

    if (call.text == "match") {
        const std::string text = ValueOf(*args[0]).ToString(convfmt);
        const std::shared_ptr<const Regex> regex = RegexOperand(*args[1]);
        double rstart = 0;
        double rlength = -1;
        RegexMatch match;
        if (regex->Search(text, 0, match)) {
            rstart = static_cast<double>(match.groups[0].first) + 1;
            rlength = static_cast<double>(match.groups[0].second - match.groups[0].first);
        }
        ScalarRef(kSlotRSTART, "RSTART") = Value::FromNumber(rstart);
        ScalarRef(kSlotRLENGTH, "RLENGTH") = Value::FromNumber(rlength);
        return Value::FromNumber(rstart);
    }

    if (call.text == "tolower" || call.text == "toupper") {
        const bool lower = call.text == "tolower";
        std::string text = ValueOf(*args[0]).ToString(convfmt);
        for (char& c : text) {
            if (lower && c >= 'A' && c <= 'Z') {
                c = static_cast<char>(c - 'A' + 'a');
            } else if (!lower && c >= 'a' && c <= 'z') {
                c = static_cast<char>(c - 'a' + 'A');
            }
        }
        return Value::FromString(std::move(text));
    }

    // sprintf, the math functions, close, fflush and system: later tasks.
    throw AwkFatal("function `" + call.text + "' is not implemented yet");
}

} // namespace Haisos::Awk