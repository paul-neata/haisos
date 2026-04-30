#pragma once
#include <cstdlib>
#include <string>
#include <vector>
#include <memory>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <random>
#include <atomic>
#include <nlohmann/json.hpp>
#include "interfaces/IAgent.h"
#include "interfaces/IFactory.h"
#include "src/components/Logger/Logger.h"

namespace Haisos::Tools {

inline std::string GenerateAgentName() {
    const char chars[] = "abcdefghijklmnopqrstuvwxyz0123456789";
    static std::mt19937 gen([]() {
        std::random_device rd;
        return rd();
    }());
    static std::atomic<uint64_t> s_counter{0};
    std::uniform_int_distribution<> dist(0, static_cast<int>(sizeof(chars) - 2));

    std::string name;
    uint64_t count = s_counter.fetch_add(1);
    // First 4 chars from counter (base-36), next 4 chars random
    for (int i = 0; i < 4; ++i) {
        name += chars[count % 36];
        count /= 36;
    }
    for (int i = 0; i < 4; ++i) {
        name += chars[dist(gen)];
    }
    return name;
}

inline std::string GetCurrentTimestamp() {
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

inline std::string GetCurrentTimestampISO8601() {
    auto now = std::chrono::system_clock::now();
    std::time_t time = std::chrono::system_clock::to_time_t(now);

    std::tm tm_buf;
#ifdef _WIN32
    gmtime_s(&tm_buf, &time);
#else
    gmtime_r(&time, &tm_buf);
#endif

    std::ostringstream oss;
    oss << std::put_time(&tm_buf, "%Y-%m-%dT%H:%M:%SZ");
    return oss.str();
}

inline std::shared_ptr<IAgent> FindChildByName(std::shared_ptr<IAgent> parent, const std::string& name) {
    if (!parent) return nullptr;
    for (const auto& child : parent->GetChildren()) {
        if (child->Name() == name) {
            LogVerboseDebug("FindChildByName: found child '%s' under parent '%s'", name.c_str(), parent->Name().c_str());
            return child;
        }
    }
    LogVerboseDebug("FindChildByName: child '%s' not found under parent '%s'", name.c_str(), parent->Name().c_str());
    return nullptr;
}

} // namespace Haisos::Tools
