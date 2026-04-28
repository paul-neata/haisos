#include "GetCurrentDateTime.h"
#include <chrono>
#include <iomanip>
#include <sstream>
#include <ctime>

namespace Haisos::Tools {

const std::string GetCurrentDateTime::ToolName = "get_current_date_time";
const std::string GetCurrentDateTime::ToolDefaultDescription = "Returns the current date and time in ISO 8601 format (YYYY-MM-DD HH:MM:SS)";

nlohmann::json GetCurrentDateTime::GetDefaultParametersSchema() {
    return nlohmann::json{
        {"type", "object"},
        {"properties", nlohmann::json::object()}
    };
}

std::string GetCurrentDateTime::Call(std::shared_ptr<IAgent> /*callerAgent*/, const nlohmann::json& /*args*/) {
    auto now = std::chrono::system_clock::now();
    std::time_t time = std::chrono::system_clock::to_time_t(now);

    std::tm tm_buf;
#ifdef _WIN32
    localtime_s(&tm_buf, &time);
#else
    localtime_r(&time, &tm_buf);
#endif

    std::ostringstream oss;
    oss << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S");

    return oss.str();
}

}
