#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace opengenesis::scripting {

[[nodiscard]] bool lsl_builtin_implemented(std::string_view name) noexcept;

[[nodiscard]] std::optional<std::string> evaluate_lsl_builtin(
    std::string_view name,
    const std::vector<std::string>& arguments,
    std::string& reason);

} // namespace opengenesis::scripting
