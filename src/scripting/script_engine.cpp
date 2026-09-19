#include "opengenesis/scripting/script_engine.hpp"

#include "opengenesis/scripting/lsl_engine.hpp"
#include "opengenesis/scripting/ogl_engine.hpp"

#include <algorithm>
#include <cctype>
#include <string>

namespace opengenesis::scripting {
namespace {
std::string lower(std::string_view value) {
    std::string out(value);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return out;
}
} // namespace

std::string_view script_language_name(const ScriptLanguage language) noexcept {
    switch (language) {
        case ScriptLanguage::legacy: return "legacy";
        case ScriptLanguage::lsl: return "lsl";
        case ScriptLanguage::ogl: return "ogl";
    }
    return "legacy";
}

std::optional<ScriptLanguage> parse_script_language(
    const std::string_view value) noexcept {
    const auto normalized = lower(value);
    if (normalized == "legacy" || normalized == "ogl-ir") {
        return ScriptLanguage::legacy;
    }
    if (normalized == "lsl") return ScriptLanguage::lsl;
    if (normalized == "ogl") return ScriptLanguage::ogl;
    return std::nullopt;
}

std::string_view script_feature_status_name(
    const ScriptFeatureStatus status) noexcept {
    switch (status) {
        case ScriptFeatureStatus::implemented: return "implemented";
        case ScriptFeatureStatus::partial: return "partial";
        case ScriptFeatureStatus::recognized: return "recognized";
        case ScriptFeatureStatus::unsupported: return "unsupported";
    }
    return "unsupported";
}

std::optional<CompiledScript> compile_script_source(
    const ScriptLanguage language,
    const std::string_view source,
    std::string& reason) {
    switch (language) {
        case ScriptLanguage::legacy:
            return compile_script(source, reason);
        case ScriptLanguage::lsl:
            return compile_lsl_script(source, reason);
        case ScriptLanguage::ogl:
            return compile_ogl_script(source, reason);
    }
    reason = "unsupported-script-language";
    return std::nullopt;
}

} // namespace opengenesis::scripting
