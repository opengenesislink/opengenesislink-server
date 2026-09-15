#include "opengenesis/common/log.hpp"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

namespace opengenesis::common {
namespace {

std::string_view level_name(const LogLevel level) {
    switch (level) {
        case LogLevel::debug: return "DEBUG";
        case LogLevel::info: return "INFO";
        case LogLevel::warning: return "WARN";
        case LogLevel::error: return "ERROR";
    }
    return "UNKNOWN";
}

std::string timestamp_utc() {
    const auto now = std::chrono::system_clock::now();
    const auto time = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &time);
#else
    gmtime_r(&time, &tm);
#endif
    std::ostringstream out;
    out << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    return out.str();
}

} // namespace

Logger& Logger::instance() {
    static Logger logger;
    return logger;
}

void Logger::set_level(const LogLevel level) noexcept {
    std::scoped_lock lock(mutex_);
    level_ = level;
}

LogLevel Logger::level() const noexcept {
    std::scoped_lock lock(mutex_);
    return level_;
}

void Logger::write(const LogLevel level, const std::string_view component, const std::string_view message) {
    std::scoped_lock lock(mutex_);
    if (static_cast<int>(level) < static_cast<int>(level_)) {
        return;
    }
    std::clog << timestamp_utc() << " [" << level_name(level) << "] [" << component << "] " << message << '\n';
}

void log(const LogLevel level, const std::string_view component, const std::string_view message) {
    Logger::instance().write(level, component, message);
}

} // namespace opengenesis::common
