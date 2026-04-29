#include "GetCurrentDateTime.h"
#include "src/tools/agent_tools_common/AgentToolsCommon.h"

namespace Haisos::Tools {

const std::string GetCurrentDateTime::ToolName = "get_current_date_time";
const std::string GetCurrentDateTime::ToolDefaultDescription = "Returns the current date and time. When get_gmt is true, returns GMT/UTC time in ISO 8601 format (YYYY-MM-DDTHH:MM:SSZ). Otherwise returns local time (YYYY-MM-DD HH:MM:SS).";

nlohmann::json GetCurrentDateTime::GetDefaultParametersSchema() {
    return nlohmann::json{
        {"type", "object"},
        {"properties", nlohmann::json{
            {"get_gmt", nlohmann::json{
                {"type", "boolean"},
                {"description", "If true, return GMT/UTC time in ISO 8601 format. If false or omitted, return local time."}
            }}
        }}
    };
}

ToolResult GetCurrentDateTime::Call(std::shared_ptr<IAgent> /*callerAgent*/, const nlohmann::json& args) {
    bool getGmt = false;
    if (args.contains("get_gmt") && args["get_gmt"].is_boolean()) {
        getGmt = args["get_gmt"].get<bool>();
    }

    std::string timestamp = getGmt ? GetCurrentTimestampISO8601() : GetCurrentTimestamp();

    return ToolResult{timestamp, false};
}

}
