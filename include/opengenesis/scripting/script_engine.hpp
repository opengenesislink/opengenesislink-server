#pragma once

#include "opengenesis/scripting/script_vm.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace opengenesis::scripting {

enum class ScriptLanguage {
    legacy,
    lsl,
    ogl
};

enum class ScriptFeatureStatus {
    implemented,
    partial,
    recognized,
    unsupported
};

struct ScriptFeature {
    std::string name;
    std::string category;
    ScriptFeatureStatus status{ScriptFeatureStatus::unsupported};
};

[[nodiscard]] std::string_view script_language_name(ScriptLanguage language) noexcept;
[[nodiscard]] std::optional<ScriptLanguage> parse_script_language(std::string_view value) noexcept;
[[nodiscard]] std::string_view script_feature_status_name(ScriptFeatureStatus status) noexcept;

[[nodiscard]] std::optional<CompiledScript> compile_script_source(
    ScriptLanguage language,
    std::string_view source,
    std::string& reason);

[[nodiscard]] const std::vector<ScriptFeature>& ogl_feature_catalog();
[[nodiscard]] const std::vector<ScriptFeature>& lsl_function_catalog();
[[nodiscard]] const std::vector<ScriptFeature>& lsl_event_catalog();

} // namespace opengenesis::scripting
