#include "commands/hsh/HshExpansion.h"

#include <cstdlib>

#include "commands/hsh/HshArithmetic.h"
#include "commands/hsh/HshError.h"
#include "commands/hsh/HshPattern.h"

namespace Haisos::Hsh {

namespace {

// One character of a field under construction, or a quote mark: an element
// with no character recording that a quoted (possibly empty) string stood
// here. |quoted|: the character came from a quoted context -- no splitting,
// literal for globbing. |splittable|: it came from an unquoted expansion --
// subject to IFS splitting.
struct Element {
    char ch = 0;
    bool quoted = false;
    bool splittable = false;
    bool quoteMark = false;
};

// A field under construction: a sequence of elements.
struct PendingField {
    std::vector<Element> elements;
    bool HasContent() const { return !elements.empty(); }
    std::string Text() const {
        std::string text;
        for (const Element& element : elements) {
            if (!element.quoteMark) {
                text += element.ch;
            }
        }
        return text;
    }
};

// The fields a word walks into. A hard break (between the values of $@)
// ends the current field and starts a new one.
struct PendingFields {
    std::vector<PendingField> fields{PendingField{}};
    PendingField& Current() { return fields.back(); }
};

enum class ExpansionKind {
    Fields,     // command words: field splitting and globbing at the end
    String,     // one string: a redirection target, a here-string, the case subject
    Assignment, // an assignment's value: as String, with tilde after ':' too
    Pattern,    // a case pattern: as String, quoted characters escaped
    HereDoc,    // a heredoc body: as String, never a tilde
};

// The variables $((...)) reads and assigns: the shell's variables, exported
// too when allexport is on.
class ShellArithmeticVariables : public IArithmeticVariables {
public:
    explicit ShellArithmeticVariables(ShellState& state) : m_state(state) {}
    std::optional<std::string> Get(const std::string& name) override {
        return m_state.variables.Get(name);
    }
    bool Set(const std::string& name, const std::string& value) override {
        if (!m_state.variables.Set(name, value)) {
            return false;
        }
        if (m_state.options.allexport) {
            m_state.variables.Export(name);
        }
        return true;
    }

private:
    ShellState& m_state;
};

// The recursive walk over a word's parts, shared by every Expander method.
class ExpansionEngine {
public:
    ExpansionEngine(ShellState& state, IExpansionHost& host, ExpansionKind kind)
        : m_state(state), m_host(host), m_kind(kind) {}

    PendingFields WalkWord(const std::vector<WordPart>& parts) {
        PendingFields out;
        WalkWordInto(out, parts, TildeAllowed());
        return out;
    }

    // Every final field of |fields|, split by IFS.
    std::vector<std::string> FinishFields(const PendingFields& fields) {
        std::vector<std::string> result;
        for (const PendingField& field : fields.fields) {
            for (std::vector<Element>& split : SplitField(field)) {
                if (!m_state.options.noglob) {
                    std::string pattern = GlobPatternOf(split);
                    if (HasPatternCharacters(pattern)) {
                        std::vector<std::string> matches = ExpandPathname(pattern, m_host);
                        if (!matches.empty()) {
                            for (std::string& match : matches) {
                                result.push_back(std::move(match));
                            }
                            continue;
                        }
                    }
                }
                result.push_back(TextOf(split));
            }
        }
        return result;
    }

    // One string: hard breaks become the first character of IFS (space when
    // it is unset, nothing when it is empty), quote marks are dropped.
    std::string FinishString(const PendingFields& fields) {
        std::string result;
        char separator = IfsSeparator();
        bool first = true;
        for (const PendingField& field : fields.fields) {
            if (!first && separator != '\0') {
                result += separator;
            }
            result += field.Text();
            first = false;
        }
        return result;
    }

    // One pattern: as FinishString, but each quoted character escaped so
    // MatchPattern takes it literally.
    std::string FinishPattern(const PendingFields& fields) {
        std::string result;
        char separator = IfsSeparator();
        bool first = true;
        for (const PendingField& field : fields.fields) {
            if (!first && separator != '\0') {
                result += separator;
            }
            for (const Element& element : field.elements) {
                if (element.quoteMark) {
                    continue;
                }
                result += element.quoted ? EscapeForPattern(std::string_view(&element.ch, 1))
                                         : std::string(1, element.ch);
            }
            first = false;
        }
        return result;
    }

private:
    bool TildeAllowed() const {
        return m_kind != ExpansionKind::HereDoc && !m_inDoubleQuotes;
    }

    // IFS's value, or " \t\n" when it is unset.
    std::string Ifs() const {
        std::optional<std::string> ifs = m_state.variables.Get("IFS");
        return ifs.has_value() ? *ifs : std::string(" \t\n");
    }

    // The character hard breaks join with: the first of IFS, space when IFS
    // is unset; '\0' (nothing) when it is empty.
    char IfsSeparator() const {
        std::string ifs = Ifs();
        return ifs.empty() ? '\0' : ifs[0];
    }

    // -- emission ----------------------------------------------------------

    static void EmitChar(PendingFields& out, char c, bool quoted, bool splittable) {
        out.Current().elements.push_back(Element{c, quoted, splittable, false});
    }

    void EmitChars(PendingFields& out, std::string_view text, bool quoted, bool splittable) {
        for (char c : text) {
            EmitChar(out, c, quoted, splittable);
        }
    }

    static void EmitQuoteMark(PendingFields& out) {
        out.Current().elements.push_back(Element{0, false, false, true});
    }

    static void HardBreak(PendingFields& out) {
        out.fields.push_back(PendingField{});
    }

    // A value from an expansion: quoted (preceded by a quote mark) inside
    // double quotes, splittable otherwise.
    void EmitValue(PendingFields& out, const std::string& value) {
        if (m_inDoubleQuotes) {
            EmitQuoteMark(out);
            EmitChars(out, value, true, false);
        } else {
            EmitChars(out, value, false, true);
        }
    }

    // -- the walk ----------------------------------------------------------

    // |tildeHere|: a '~' at the start of these parts opens a tilde-prefix.
    void WalkWordInto(PendingFields& out, const std::vector<WordPart>& parts, bool tildeHere) {
        bool atStart = tildeHere;
        for (size_t i = 0; i < parts.size(); ++i) {
            const WordPart& part = parts[i];
            bool lastPart = i + 1 == parts.size();
            switch (part.kind) {
                case WordPartKind::Literal:
                    EmitLiteral(out, part.text, atStart, lastPart);
                    break;
                case WordPartKind::Quoted:
                    EmitQuoteMark(out);
                    EmitChars(out, part.text, true, false);
                    break;
                case WordPartKind::DoubleQuoted:
                    WalkDoubleQuoted(out, part);
                    break;
                case WordPartKind::Parameter:
                    EmitParameter(out, part);
                    break;
                case WordPartKind::CommandSubstitution: {
                    CommandSubstitutionResult result = m_host.RunCommandSubstitution(part.text, part.line);
                    m_state.lastExitStatus = result.exitStatus;
                    std::string output = result.output;
                    while (!output.empty() && output.back() == '\n') {
                        output.pop_back();
                    }
                    EmitValue(out, output);
                    break;
                }
                case WordPartKind::Arithmetic: {
                    // The expression's own expansions first (never a tilde),
                    // then the arithmetic itself.
                    PendingFields expression;
                    WalkWordInto(expression, part.parts, false);
                    ShellArithmeticVariables variables(m_state);
                    intmax_t value = EvaluateArithmetic(FinishString(expression), variables);
                    EmitValue(out, std::to_string(value));
                    break;
                }
            }
            atStart = false;
        }
    }

    // Emits |text| as literal characters (never quoted; splittable inside an
    // operand). A tilde-prefix expands at the start when |tildeAtStart| and
    // this literal begins the word, and in an assignment's value after every
    // unquoted ':'.
    void EmitLiteral(PendingFields& out, const std::string& text, bool tildeAtStart, bool lastPart) {
        bool tildePossible = tildeAtStart;
        size_t i = 0;
        while (i < text.size()) {
            if (tildePossible && text[i] == '~') {
                size_t end = TildePrefixEnd(text, i, lastPart);
                if (end != std::string::npos && end == i + 1) {
                    // "~" alone: HOME. Unset leaves the text as it is; empty
                    // expands to nothing (no quote mark: the field vanishes).
                    std::optional<std::string> home = m_state.variables.Get("HOME");
                    if (home.has_value()) {
                        EmitChars(out, *home, true, false);
                        i = end;
                        tildePossible = false;
                        continue;
                    }
                }
                // "~<user>" (~user is not supported) or an unset HOME: left as it is.
            }
            EmitChar(out, text[i], false, m_inOperand);
            tildePossible = m_kind == ExpansionKind::Assignment && text[i] == ':';
            ++i;
        }
    }

    // One past the tilde-prefix that opens at |pos| (a '~' has been seen):
    // up to the first '/' (or ':' in an assignment's value), or the whole
    // text when it has neither and is the word's last part; npos when the
    // '~' opens no tilde-prefix (more of the word follows it).
    size_t TildePrefixEnd(const std::string& text, size_t pos, bool lastPart) const {
        size_t end = text.find_first_of(m_kind == ExpansionKind::Assignment ? "/:" : "/", pos);
        if (end != std::string::npos) {
            return end;
        }
        return lastPart ? text.size() : std::string::npos;
    }

    void WalkDoubleQuoted(PendingFields& out, const WordPart& part) {
        // A "$@" alone keeps no quote mark: with no positional parameters it
        // gives no field at all.
        bool onlyPlainAt = !part.parts.empty();
        for (const WordPart& inner : part.parts) {
            if (inner.kind != WordPartKind::Parameter || inner.text != "@" ||
                inner.op != ParameterOp::None) {
                onlyPlainAt = false;
                break;
            }
        }
        if (!onlyPlainAt) {
            EmitQuoteMark(out);
        }
        bool savedQuotes = m_inDoubleQuotes;
        m_inDoubleQuotes = true;
        WalkWordInto(out, part.parts, false);
        m_inDoubleQuotes = savedQuotes;
    }

    // The operand of a ${...}, walked where it stands (its literals splittable
    // when the ${...} is not in double quotes). |tilde| is whether a tilde at
    // the operand's very start expands (outside double quotes and heredocs,
    // for every op), with a full assignment's after-':' rule.
    void WalkOperandInPlace(PendingFields& out, const std::vector<WordPart>& parts, bool tilde) {
        bool savedOperand = m_inOperand;
        if (!m_inDoubleQuotes) {
            m_inOperand = true;
        }
        WalkWordInto(out, parts, tilde);
        m_inOperand = savedOperand;
    }

    std::string WalkOperandToString(const std::vector<WordPart>& parts, bool tilde) {
        PendingFields sub;
        WalkWordInto(sub, parts, tilde);
        return FinishString(sub);
    }

    // A #/##/%/%% operand has its own quoting even inside double quotes (the
    // lexer reads it as an unquoted word): "${x#$y}" with y='*' is a pattern,
    // and a tilde at its start expands.
    std::string WalkOperandToPattern(const std::vector<WordPart>& parts) {
        bool savedQuotes = m_inDoubleQuotes;
        m_inDoubleQuotes = false;
        PendingFields sub;
        WalkWordInto(sub, parts, TildeAllowed());
        m_inDoubleQuotes = savedQuotes;
        return FinishPattern(sub);
    }

    // -- parameters --------------------------------------------------------

    // The value of a name: the positional parameters as a list for @ and *
    // (always set), the one-character special parameters, $n, or a variable.
    struct Resolved {
        bool set = false;
        std::vector<std::string> values;  // one element; each parameter for @ and *
    };

    Resolved Resolve(const std::string& name) const {
        if (name == "@" || name == "*") {
            return {true, m_state.positional};
        }
        if (name == "#") {
            return {true, {std::to_string(m_state.positional.size())}};
        }
        if (name == "?") {
            return {true, {std::to_string(m_state.lastExitStatus)}};
        }
        if (name == "$") {
            return {true, {std::to_string(m_state.shellPid)}};
        }
        if (name == "-") {
            return {true, {OptionLetters(m_state.options)}};
        }
        if (name == "!") {
            if (!m_state.lastBackgroundPid.has_value()) {
                return {false, {}};
            }
            return {true, {std::to_string(*m_state.lastBackgroundPid)}};
        }
        if (!name.empty() && name.find_first_not_of("0123456789") == std::string::npos) {
            // A positional parameter ($0 and ${0} are the shell's name).
            unsigned long n = std::strtoul(name.c_str(), nullptr, 10);
            if (n == 0) {
                return {true, {m_state.arg0}};
            }
            if (n - 1 < m_state.positional.size()) {
                return {true, {m_state.positional[n - 1]}};
            }
            return {false, {}};
        }
        std::optional<std::string> value = m_state.variables.Get(name);
        if (!value.has_value()) {
            return {false, {}};
        }
        return {true, {*value}};
    }

    // Null: set and empty. For @ and *: null when joined with spaces (at
    // least one separator, or one empty sole parameter, or none) they are empty.
    static bool IsNull(const Resolved& resolved) {
        if (!resolved.set) {
            return true;
        }
        if (resolved.values.size() > 1) {
            return false;
        }
        return resolved.values.empty() || resolved.values[0].empty();
    }

    static std::string JoinedWithSpaces(const std::vector<std::string>& values) {
        std::string joined;
        bool first = true;
        for (const std::string& value : values) {
            if (!first) {
                joined += ' ';
            }
            joined += value;
            first = false;
        }
        return joined;
    }

    static bool IsRemoveOp(ParameterOp op) {
        return op == ParameterOp::RemoveSmallestSuffix || op == ParameterOp::RemoveLargestSuffix ||
               op == ParameterOp::RemoveSmallestPrefix || op == ParameterOp::RemoveLargestPrefix;
    }

    static PatternRemoval RemovalFor(ParameterOp op) {
        switch (op) {
            case ParameterOp::RemoveSmallestSuffix: return PatternRemoval::SmallestSuffix;
            case ParameterOp::RemoveLargestSuffix:  return PatternRemoval::LargestSuffix;
            case ParameterOp::RemoveSmallestPrefix: return PatternRemoval::SmallestPrefix;
            default:                                return PatternRemoval::LargestPrefix;
        }
    }

    void EmitParameter(PendingFields& out, const WordPart& part) {
        if (part.op == ParameterOp::Bad) {
            throw ShellError("Bad substitution");
        }
        const std::string& name = part.text;
        Resolved resolved = Resolve(name);

        switch (part.op) {
            case ParameterOp::None:
            case ParameterOp::Length: {
                CheckNounset(name, resolved);
                if (part.op == ParameterOp::Length) {
                    size_t length = name == "@" || name == "*"
                        ? JoinedWithSpaces(resolved.values).size()
                        : (resolved.values.empty() ? 0 : resolved.values[0].size());
                    EmitValue(out, std::to_string(length));
                    return;
                }
                EmitPositionalStyle(out, name, resolved.values);
                return;
            }
            case ParameterOp::RemoveSmallestSuffix:
            case ParameterOp::RemoveLargestSuffix:
            case ParameterOp::RemoveSmallestPrefix:
            case ParameterOp::RemoveLargestPrefix: {
                CheckNounset(name, resolved);
                std::string pattern = WalkOperandToPattern(part.parts);
                PatternRemoval which = RemovalFor(part.op);
                std::vector<std::string> values;
                for (const std::string& value : resolved.values) {
                    values.push_back(RemovePattern(value, pattern, which));
                }
                EmitPositionalStyle(out, name, values);
                return;
            }
            case ParameterOp::UseDefault:
            case ParameterOp::UseDefaultIfUnset: {
                bool fallback = !resolved.set ||
                    (part.op == ParameterOp::UseDefault && IsNull(resolved));
                if (fallback) {
                    WalkOperandInPlace(out, part.parts, TildeAllowed());
                } else {
                    // "@"/"*" keep their per-parameter shape: ${@:-x} with a b
                    // is <a><b>, "${*:-x}" is joined by IFS's first character.
                    EmitPositionalStyle(out, name, resolved.values);
                }
                return;
            }
            case ParameterOp::AssignDefault:
            case ParameterOp::AssignDefaultIfUnset: {
                bool assign = !resolved.set ||
                    (part.op == ParameterOp::AssignDefault && IsNull(resolved));
                if (assign) {
                    if (!IsValidShellName(name)) {
                        throw ShellError(name + ": bad variable name");
                    }
                    // dash: a tilde at the very start of a :=/= operand
                    // expands (not inside double quotes or a heredoc), but
                    // never after a ':' in it, even in an assignment.
                    bool tilde = TildeAllowed();
                    ExpansionKind savedKind = m_kind;
                    m_kind = ExpansionKind::String;
                    std::string value = WalkOperandToString(part.parts, tilde);
                    m_kind = savedKind;
                    if (!m_state.variables.Set(name, value)) {
                        throw ShellError(name + ": is read only");
                    }
                    if (m_state.options.allexport) {
                        m_state.variables.Export(name);
                    }
                    EmitValue(out, value);
                } else {
                    EmitPositionalStyle(out, name, resolved.values);
                }
                return;
            }
            case ParameterOp::ErrorIfNull:
            case ParameterOp::ErrorIfUnset: {
                bool fails = !resolved.set ||
                    (part.op == ParameterOp::ErrorIfNull && IsNull(resolved));
                if (fails) {
                    std::string message = name + ": ";
                    if (part.parts.empty()) {
                        message += part.op == ParameterOp::ErrorIfNull
                            ? "parameter not set or null"
                            : "parameter not set";
                    } else {
                        message += WalkOperandToString(part.parts, TildeAllowed());
                    }
                    throw ShellError(message);
                }
                EmitPositionalStyle(out, name, resolved.values);
                return;
            }
            case ParameterOp::UseAlternative:
            case ParameterOp::UseAlternativeIfSet: {
                bool alternative = resolved.set &&
                    (part.op == ParameterOp::UseAlternativeIfSet || !IsNull(resolved));
                if (alternative) {
                    WalkOperandInPlace(out, part.parts, TildeAllowed());
                }
                return;
            }
            default:
                return;
        }
    }

    void CheckNounset(const std::string& name, const Resolved& resolved) const {
        if (!resolved.set && m_state.options.nounset && name != "@" && name != "*") {
            throw ShellError(name + ": parameter not set");
        }
    }

    // How @ and *'s value is emitted (op None; after a Remove op; or when a
    // :-/=/? op falls through to the value): in double
    // quotes $@ is each parameter quoted, preceded by a quote mark, with a
    // hard break between consecutive ones; "$*" is the parameters joined by
    // the first character of IFS as one quoted value; unquoted, either is
    // each parameter splittable with a hard break between them.
    void EmitPositionalStyle(PendingFields& out, const std::string& name,
                             const std::vector<std::string>& values) {
        bool isAt = name == "@";
        if (!isAt && name != "*") {
            EmitValue(out, values.empty() ? "" : values[0]);
            return;
        }
        if (m_inDoubleQuotes) {
            if (isAt) {
                bool first = true;
                for (const std::string& value : values) {
                    if (!first) {
                        HardBreak(out);
                    }
                    EmitQuoteMark(out);
                    EmitChars(out, value, true, false);
                    first = false;
                }
                return;
            }
            EmitQuoteMark(out);
            char separator = IfsSeparator();
            std::string joined;
            bool first = true;
            for (const std::string& value : values) {
                if (!first && separator != '\0') {
                    joined += separator;
                }
                joined += value;
                first = false;
            }
            EmitChars(out, joined, true, false);
            return;
        }
        bool first = true;
        for (const std::string& value : values) {
            if (!first) {
                HardBreak(out);
            }
            EmitChars(out, value, false, true);
            first = false;
        }
    }

    // -- finishing ---------------------------------------------------------

    // Splits one field by IFS; quote marks are dropped here, the quoted flag
    // of characters is kept for globbing.
    std::vector<std::vector<Element>> SplitField(const PendingField& field) {
        std::vector<std::vector<Element>> result;
        std::string ifs = Ifs();
        if (ifs.empty()) {
            // No splitting happens: only hard breaks separate fields.
            if (!field.HasContent()) {
                return result;
            }
            std::vector<Element> chars;
            for (const Element& element : field.elements) {
                if (!element.quoteMark) {
                    chars.push_back(element);
                }
            }
            result.push_back(std::move(chars));
            return result;
        }
        auto isIfs = [&](char c) { return ifs.find(c) != std::string::npos; };
        auto isIfsWhiteSpace = [&](char c) {
            return (c == ' ' || c == '\t' || c == '\n') && isIfs(c);
        };
        const std::vector<Element>& elements = field.elements;
        size_t i = 0;
        // Skip leading splittable IFS white space.
        while (i < elements.size() && !elements[i].quoteMark && elements[i].splittable &&
               isIfsWhiteSpace(elements[i].ch)) {
            ++i;
        }
        std::vector<Element> current;
        bool content = false;
        auto emit = [&](bool always) {
            if (always || content) {
                result.push_back(current);
            }
            current.clear();
            content = false;
        };
        auto skipWhiteSpace = [&]() {
            while (i < elements.size() && !elements[i].quoteMark && elements[i].splittable &&
                   isIfsWhiteSpace(elements[i].ch)) {
                ++i;
            }
        };
        auto atNonWhiteDelimiter = [&]() {
            return i < elements.size() && !elements[i].quoteMark && elements[i].splittable &&
                   isIfs(elements[i].ch) && !isIfsWhiteSpace(elements[i].ch);
        };
        while (i < elements.size()) {
            const Element& element = elements[i];
            if (element.quoteMark) {
                content = true;
                ++i;
                continue;
            }
            if (element.splittable && isIfs(element.ch)) {
                if (isIfsWhiteSpace(element.ch)) {
                    // White space: emit the field when it has content; a
                    // non-white IFS character right after (with its white
                    // space) is part of the same delimiter.
                    emit(false);
                    skipWhiteSpace();
                    if (atNonWhiteDelimiter()) {
                        ++i;
                        skipWhiteSpace();
                    }
                } else {
                    // Non-white: a delimiter of its own, the field ends even
                    // when empty.
                    emit(true);
                    ++i;
                    skipWhiteSpace();
                }
                continue;
            }
            current.push_back(element);
            content = true;
            ++i;
        }
        emit(false);
        return result;
    }

    static std::string TextOf(const std::vector<Element>& elements) {
        std::string text;
        for (const Element& element : elements) {
            text += element.ch;
        }
        return text;
    }

    // The pattern a field globs against: a quoted character as '\' +
    // character (except '/', kept as it is), an unquoted '\' as "\\", any
    // other unquoted character as it is.
    static std::string GlobPatternOf(const std::vector<Element>& elements) {
        std::string pattern;
        for (const Element& element : elements) {
            if (element.quoted) {
                if (element.ch != '/') {
                    pattern += '\\';
                }
                pattern += element.ch;
            } else if (element.ch == '\\') {
                pattern += "\\\\";
            } else {
                pattern += element.ch;
            }
        }
        return pattern;
    }

    ShellState& m_state;
    IExpansionHost& m_host;
    ExpansionKind m_kind;
    bool m_inDoubleQuotes = false;
    bool m_inOperand = false;
};

} // namespace

Expander::Expander(ShellState& state, IExpansionHost& host) : m_state(state), m_host(host) {}

std::vector<std::string> Expander::ExpandWords(const std::vector<Word>& words) {
    std::vector<std::string> result;
    for (const Word& word : words) {
        std::vector<std::string> fields = ExpandWord(word);
        for (std::string& field : fields) {
            result.push_back(std::move(field));
        }
    }
    return result;
}

std::vector<std::string> Expander::ExpandWord(const Word& word) {
    ExpansionEngine engine(m_state, m_host, ExpansionKind::Fields);
    return engine.FinishFields(engine.WalkWord(word.parts));
}

std::string Expander::ExpandAssignmentValue(const Word& value) {
    ExpansionEngine engine(m_state, m_host, ExpansionKind::Assignment);
    return engine.FinishString(engine.WalkWord(value.parts));
}

std::string Expander::ExpandToString(const Word& word) {
    ExpansionEngine engine(m_state, m_host, ExpansionKind::String);
    return engine.FinishString(engine.WalkWord(word.parts));
}

std::string Expander::ExpandPattern(const Word& word) {
    ExpansionEngine engine(m_state, m_host, ExpansionKind::Pattern);
    return engine.FinishPattern(engine.WalkWord(word.parts));
}

std::string Expander::ExpandHereDocument(const HereDocument& hereDoc) {
    if (hereDoc.quoted) {
        return hereDoc.rawBody;
    }
    ExpansionEngine engine(m_state, m_host, ExpansionKind::HereDoc);
    return engine.FinishString(engine.WalkWord(hereDoc.body.parts));
}

} // namespace Haisos::Hsh
