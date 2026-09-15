#pragma once
#include <string_view>
namespace opengenesis::common {
enum class LogLevel { debug, info, warning, error };
void log(LogLevel level, std::string_view component, std::string_view message);
}
