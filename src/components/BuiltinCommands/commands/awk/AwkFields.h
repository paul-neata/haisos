#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>
#include "commands/awk/AwkValue.h"

namespace Haisos::Awk {

// Splits |text| as awk splits a record by |fs|: " " -- runs of blanks
// (space, tab, newline), leading and trailing ones ignored; "" -- no
// splitting (one field, none for empty text: gawk --posix); one other
// byte -- every occurrence of that byte, literally (also "\t", "|", "."),
// empty fields kept; an empty text has no fields. Returns false, |fields|
// untouched, for an fs of two or more bytes (a regex: awk--records).
bool SplitAwkFields(std::string_view text, const std::string& fs,
                    std::vector<std::string>& fields);

// The most fields an assignment may make (NF = n, $n = v). gawk has no such
// limit: it fails on the allocation with a message naming its own source,
// which is not copied -- a documented difference.
inline constexpr intmax_t kAwkMaxFields = 1000000;

// The fields of the current record: $0, $1..., NF -- split lazily, rebuilt
// on assignment, as awk's field machinery works.
class FieldStore {
public:
    // How a record is split when its fields are first needed: given the
    // record and the FS saved with it (and whether RS was "" then). The
    // interpreter supplies it -- SplitAwkFields, then regexes and paragraph
    // mode in awk--records. Throws AwkFatal on its own errors.
    using Splitter = std::function<void(std::string_view record, const std::string& fs,
                                        bool paragraphMode, std::vector<std::string>& fields)>;

    explicit FieldStore(Splitter splitter);

    // $0 = record: fields are split lazily, with |fs| as it is now (a later
    // change of FS does not affect this record). $0 reads as input (a
    // strnum) until a field or NF assignment rebuilds it; a rebuilt $0 is a
    // plain string (gawk: $1 = 12 makes $0 < 9 a string comparison).
    void SetRecord(std::string record, const std::string& fs, bool paragraphMode = false);
    // $0 (rebuilt first if a field or NF was assigned since -- with the
    // CONVFMT in force at this call; the rebuild happens but once).
    const std::string& Record(const std::string& convfmt);
    // $index: 0 is $0 (FromInput for a record that came from input,
    // FromString for a rebuilt one); 1..NF the fields (FromInput); beyond NF
    // an Uninitialized value. Negative: AwkFatal
    // "attempt to access field -1".
    Value Field(intmax_t index, const std::string& convfmt);
    // $index = value: 0 sets the record (re-split with the FS saved with the
    // current record, |convfmt| converting a Number value); n > NF first
    // extends with empty fields; then $0 is rebuilt -- the fields joined by
    // |ofs| (the assignment's), each a Number converted at rebuild time
    // (the CONVFMT of the first Record read after it, not this one). An
    // index above kAwkMaxFields: AwkFatal. Negative: the "attempt to access
    // field" AwkFatal.
    void SetField(intmax_t index, const Value& value, const std::string& ofs, const std::string& convfmt);
    size_t NF();
    // NF = n: truncates or extends with empty fields, rebuilds $0 with |ofs|.
    // Negative, or above kAwkMaxFields: AwkFatal.
    void SetNF(intmax_t nf, const std::string& ofs);

private:
    // Splits the record if it has not been split yet (a record is split at
    // most once: its fields, once made, are kept).
    void EnsureSplit();
    // Joins the fields into $0: each field ToString(convfmt), m_ofs between.
    void Rebuild(const std::string& convfmt);

    Splitter m_splitter;
    std::string m_record;
    std::string m_fs = " ";
    bool m_paragraph = false;
    // The FS and paragraph flag are saved with the record, the OFS with the
    // last assignment that changed the fields. The CONVFMT is not saved: a
    // rebuild is lazy, with the CONVFMT in force when $0 is first read after
    // the assignment.
    std::string m_ofs = " ";
    bool m_split = true;       // nothing to split until a record arrives
    bool m_recordDirty = false;   // a field or NF was assigned since the record
    bool m_recordFromInput = true;  // the record came from input, not a rebuild
    std::vector<Value> m_fields;  // $1..$NF
};

} // namespace Haisos::Awk