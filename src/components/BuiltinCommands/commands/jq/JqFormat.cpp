#include "commands/jq/JqFormat.h"

#include "commands/jq/JqJsonWriter.h"
#include "commands/jq/JqRuntime.h"

namespace Haisos::Jq {

Value ApplyFormat(const std::string& name, const Value& value) {
    if (name == "text") {
        // tostring: a string as it is, anything else its compact JSON.
        if (value.GetKind() == Kind::String)
            return value;
        std::string out;
        WriteOptions options;
        options.indent = 0;
        WriteJson(value, options, out);
        return Value::String(std::move(out));
    }
    if (name == "json") {
        std::string out;
        WriteOptions options;
        options.indent = 0;
        WriteJson(value, options, out);
        return Value::String(std::move(out));
    }
    throw JqError{Value::String(name + " is not a valid format")};
}

} // namespace Haisos::Jq