#pragma once

#include "opengenesis/scripting/script_engine.hpp"

#include <optional>
#include <string>
#include <string_view>

namespace opengenesis::scripting {

[[nodiscard]] std::optional<CompiledScript> compile_lsl_script(
    std::string_view source,
    std::string& reason);

} // namespace opengenesis::scripting
