#include "opengenesis/common/log.hpp"
#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <mutex>

namespace opengenesis::common {
namespace { std::mutex g_log_mutex; }
void log(const LogLevel level, const std::string_view component, const std::string_view message) {
    const char* label = "INFO";
    if (level == LogLevel::debug) label = "DEBUG";
    else if (level == LogLevel::warning) label = "WARN";
    else if (level == LogLevel::error) label = "ERROR";
    const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm tm{};
    localtime_r(&now, &tm);
    std::scoped_lock lock(g_log_mutex);
    std::clog << std::put_time(&tm, "%Y-%m-%d %H:%M:%S") << " [" << label << "] [" << component << "] " << message << '\n';
}
}
