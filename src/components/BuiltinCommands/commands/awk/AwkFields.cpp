#include "commands/awk/AwkFields.h"
#include "commands/awk/AwkError.h"

namespace Haisos::Awk {

namespace {

// The blanks FS = " " splits on: space, tab, newline.
bool IsFieldBlank(char c) {
    return c == ' ' || c == '\t' || c == '\n';
}

// AwkFatal for an assignment making more fields than kAwkMaxFields.
[[noreturn]] void FailTooManyFields(const char* what, intmax_t number) {
    throw AwkFatal(std::string(what) + " " + std::to_string(number) + ": more than " +
                   std::to_string(kAwkMaxFields) + " fields");
}

} // namespace

bool SplitAwkFields(std::string_view text, const std::string& fs,
                    std::vector<std::string>& fields) {
    if (fs.size() >= 2) {
        return false;   // a regex field separator: awk--records
    }
    fields.clear();
    if (text.empty()) {
        return true;   // an empty record has no fields
    }
    if (fs.empty()) {
        // No splitting at all: the record is the one field.
        fields.emplace_back(text);
        return true;
    }
    const char separator = fs[0];
    if (separator == ' ') {
        // Runs of blanks, leading and trailing ones ignored.
        size_t pos = 0;
        while (pos < text.size()) {
            while (pos < text.size() && IsFieldBlank(text[pos])) {
                ++pos;
            }
            if (pos >= text.size()) {
                break;
            }
            const size_t start = pos;
            while (pos < text.size() && !IsFieldBlank(text[pos])) {
                ++pos;
            }
            fields.emplace_back(text.substr(start, pos - start));
        }
        return true;
    }
    // Every occurrence of the one byte, literally; empty fields kept.
    size_t start = 0;
    while (true) {
        const size_t separatorPos = text.find(separator, start);
        if (separatorPos == std::string_view::npos) {
            fields.emplace_back(text.substr(start));
            return true;
        }
        fields.emplace_back(text.substr(start, separatorPos - start));
        start = separatorPos + 1;
    }
}

FieldStore::FieldStore(Splitter splitter)
    : m_splitter(std::move(splitter)) {
}

void FieldStore::EnsureSplit() {
    if (m_split) {
        return;
    }
    std::vector<std::string> fields;
    m_splitter(m_record, m_fs, m_paragraph, fields);
    m_fields.clear();
    m_fields.reserve(fields.size());
    for (std::string& field : fields) {
        m_fields.push_back(Value::FromInput(std::move(field)));
    }
    m_split = true;
}

void FieldStore::Rebuild(const std::string& convfmt) {
    EnsureSplit();
    std::string record;
    for (size_t i = 0; i < m_fields.size(); ++i) {
        if (i > 0) {
            record += m_ofs;
        }
        record += m_fields[i].ToString(convfmt);
    }
    m_record = std::move(record);
    m_recordDirty = false;
}

void FieldStore::SetRecord(std::string record, const std::string& fs, bool paragraphMode) {
    m_record = std::move(record);
    m_fs = fs;
    m_paragraph = paragraphMode;
    m_fields.clear();
    m_split = false;
    m_recordDirty = false;
    m_recordFromInput = true;
}

const std::string& FieldStore::Record(const std::string& convfmt) {
    if (m_recordDirty) {
        Rebuild(convfmt);
    }
    return m_record;
}

Value FieldStore::Field(intmax_t index, const std::string& convfmt) {
    if (index < 0) {
        throw AwkFatal("attempt to access field " + std::to_string(index));
    }
    if (index == 0) {
        // $0 is input (a strnum) only for a record no assignment rebuilt.
        const std::string& record = Record(convfmt);
        return m_recordFromInput ? Value::FromInput(record) : Value::FromString(record);
    }
    EnsureSplit();
    const size_t field = static_cast<size_t>(index);
    if (field > m_fields.size()) {
        return Value();
    }
    return m_fields[field - 1];
}

void FieldStore::SetField(intmax_t index, const Value& value, const std::string& ofs,
                          const std::string& convfmt) {
    if (index < 0) {
        throw AwkFatal("attempt to access field " + std::to_string(index));
    }
    if (index == 0) {
        // $0 = value: a new record, re-split with the FS saved with the
        // current record. |convfmt| converts a Number value.
        SetRecord(value.ToString(convfmt), m_fs, m_paragraph);
        return;
    }
    if (index > kAwkMaxFields) {
        FailTooManyFields("attempt to assign field", index);
    }
    EnsureSplit();
    const size_t field = static_cast<size_t>(index);
    if (field > m_fields.size()) {
        // Beyond NF: NF rises, the fields in between empty.
        m_fields.resize(field, Value::FromInput(std::string()));
    }
    m_fields[field - 1] = value;
    m_ofs = ofs;
    m_recordDirty = true;
    m_recordFromInput = false;
}

size_t FieldStore::NF() {
    EnsureSplit();
    return m_fields.size();
}

void FieldStore::SetNF(intmax_t nf, const std::string& ofs) {
    if (nf < 0) {
        throw AwkFatal("NF set to negative value");
    }
    if (nf > kAwkMaxFields) {
        FailTooManyFields("NF set to", nf);
    }
    EnsureSplit();
    const size_t count = static_cast<size_t>(nf);
    if (count < m_fields.size()) {
        m_fields.resize(count);
    } else {
        m_fields.resize(count, Value::FromInput(std::string()));
    }
    m_ofs = ofs;
    m_recordDirty = true;
    m_recordFromInput = false;
}

} // namespace Haisos::Awk