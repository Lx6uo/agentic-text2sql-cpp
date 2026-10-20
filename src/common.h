#pragma once
#include <csignal>
#include <json.hpp>
#include <chrono>
#include <string>

namespace t2s {
using Json = nlohmann::json;
inline volatile std::sig_atomic_t cancelled = 0;
using Clock = std::chrono::steady_clock;
inline double elapsed_ms(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}
inline std::string trim(std::string s) {
    auto first = s.find_first_not_of(" \t\r\n");
    return first == std::string::npos ? "" : s.substr(first, s.find_last_not_of(" \t\r\n") - first + 1);
}
}
