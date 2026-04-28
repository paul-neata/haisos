#pragma once
#include <string>
#include <functional>

namespace Haisos {

struct JsonSendReceiveCallbacks {
    std::function<void(const std::string& json)> on_send;
    std::function<void(const std::string& json)> on_received;
};

}
