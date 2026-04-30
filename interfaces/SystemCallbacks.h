#pragma once
#include <string>
#include <functional>

namespace Haisos {

struct SystemCallbacks {
    std::function<void(const std::string& json)> on_send;
    std::function<void(const std::string& json)> on_received;
    std::function<void(const std::string& agentName, const std::string& json)> on_send_with_name;
    std::function<void(const std::string& agentName, const std::string& json)> on_received_with_name;
};

}
