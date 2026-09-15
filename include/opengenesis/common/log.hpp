#pragma once

#include <mutex>
#include <string_view>

namespace opengenesis::common {

enum class LogLevel { debug, info, warning, error };

class Logger final {
public:
    static Logger& instance();
    void set_level(LogLevel level) noexcept;
    [[nodiscard]] LogLevel level() const noexcept;
    void write(LogLevel level, std::string_view component, std::string_view message);

private:
    Logger() = default;
    mutable std::mutex mutex_;
    LogLevel level_{LogLevel::info};
};

void log(LogLevel level, std::string_view component, std::string_view message);

} // namespace opengenesis::common
