#include "opengenesis/scripting/ogl_engine.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <string>
#include <utility>

namespace opengenesis::scripting {
namespace {

std::string trim(std::string value) {
    const auto nonspace = [](const char c) {
        return std::isspace(static_cast<unsigned char>(c)) == 0;
    };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), nonspace));
    value.erase(std::find_if(value.rbegin(), value.rend(), nonspace).base(), value.end());
    return value;
}

bool atom(std::string_view value, const std::size_t max_size = 128U) {
    if (value.empty() || value.size() > max_size) return false;
    return std::all_of(value.begin(), value.end(), [](const char c) {
        const auto u = static_cast<unsigned char>(c);
        return std::isalnum(u) != 0 || c == '_' || c == '-' || c == '.';
    });
}

std::string command_tail(const std::string& line, const std::string_view command) {
    if (line.size() <= command.size()) return {};
    return trim(line.substr(command.size()));
}

std::optional<std::string> translate_command(
    const std::string& line, std::string& reason) {
    struct Mapping {
        std::string_view ogl;
        std::string_view legacy;
    };
    static constexpr Mapping mappings[] = {
        {"emit", "emit"},
        {"timer.every", "timer"},
        {"chat.listen", "listen"},
        {"owner.notify", "notify"},
        {"user.message", "message"},
        {"world.move", "move"},
        {"world.rotate", "rotate"},
        {"world.scale", "scale"},
        {"world.velocity", "velocity"},
        {"world.angular_velocity", "angular_velocity"},
        {"world.physics", "physics"},
        {"world.text", "text"},
        {"world.say", "say"},
        {"world.whisper", "whisper"},
        {"world.shout", "shout"},
        {"world.object", "object_info"},
        {"world.region", "region_info"},
        {"world.terrain", "terrain_height"},
        {"world.water", "water_level"},
        {"world.time", "world_time"},
        {"world.nearby", "nearby_avatars"},
        {"stop", "stop"}
    };

    if (line.starts_with("let ")) {
        const auto assignment = line.find('=');
        if (assignment == std::string::npos) {
            reason = "ogl-invalid-let";
            return std::nullopt;
        }
        const auto name = trim(line.substr(4U, assignment - 4U));
        const auto value = trim(line.substr(assignment + 1U));
        if (!atom(name) || value.size() > 2048U) {
            reason = "ogl-invalid-let";
            return std::nullopt;
        }
        return "set " + name + " " + value;
    }

    if (line.starts_with("inc ")) {
        const auto by = line.find(" by ", 4U);
        if (by == std::string::npos) {
            reason = "ogl-invalid-inc";
            return std::nullopt;
        }
        const auto name = trim(line.substr(4U, by - 4U));
        const auto value = trim(line.substr(by + 4U));
        if (!atom(name) || value.empty()) {
            reason = "ogl-invalid-inc";
            return std::nullopt;
        }
        return "add " + name + " " + value;
    }

    if (line.starts_with("goto ")) {
        const auto state = trim(line.substr(5U));
        if (!atom(state)) {
            reason = "ogl-invalid-goto";
            return std::nullopt;
        }
        return "state " + state;
    }

    for (const auto& mapping : mappings) {
        if (line == mapping.ogl) return std::string{mapping.legacy};
        const auto prefix = std::string{mapping.ogl} + " ";
        if (line.starts_with(prefix)) {
            return std::string{mapping.legacy} + " " +
                   command_tail(line, mapping.ogl);
        }
    }

    reason = "ogl-unknown-command";
    return std::nullopt;
}

} // namespace

std::optional<CompiledScript> compile_ogl_script(
    const std::string_view source, std::string& reason) {
    if (source.empty() || source.size() > 64U * 1024U) {
        reason = "invalid-script-size";
        return std::nullopt;
    }

    CompiledScript output;
    std::string logical_state = "default";
    std::string event;
    std::vector<std::string> commands;
    std::istringstream input(std::string{source});
    std::string line;
    std::size_t line_count = 0U;

    auto flush = [&]() -> bool {
        if (event.empty()) return true;
        std::ostringstream legacy;
        legacy << "event " << event << '\n';
        for (const auto& command : commands) legacy << command << '\n';
        legacy << "end\n";
        auto compiled = compile_script(legacy.str(), reason);
        if (!compiled || compiled->handlers.size() != 1U) return false;
        auto handler = std::move(compiled->handlers.front());
        handler.state = logical_state;
        output.handlers.push_back(std::move(handler));
        event.clear();
        commands.clear();
        return true;
    };

    while (std::getline(input, line)) {
        if (++line_count > 4096U) {
            reason = "ogl-too-many-lines";
            return std::nullopt;
        }
        const auto comment = line.find("//");
        if (comment != std::string::npos) line.resize(comment);
        line = trim(std::move(line));
        if (line.empty() || line.starts_with("#")) continue;
        if (!line.empty() && line.back() == ';') {
            line.pop_back();
            line = trim(std::move(line));
        }

        if (line == "@ogl 1" || line == "engine ogl 1") continue;

        if (line.starts_with("state ")) {
            if (!event.empty() && !flush()) return std::nullopt;
            logical_state = trim(line.substr(6U));
            if (!atom(logical_state)) {
                reason = "ogl-invalid-state";
                return std::nullopt;
            }
            continue;
        }

        if (line.starts_with("on ")) {
            if (!event.empty() && !flush()) return std::nullopt;
            event = trim(line.substr(3U));
            if (!atom(event)) {
                reason = "ogl-invalid-event";
                return std::nullopt;
            }
            continue;
        }

        if (line == "end") {
            if (event.empty()) {
                reason = "ogl-unexpected-end";
                return std::nullopt;
            }
            if (!flush()) return std::nullopt;
            continue;
        }

        if (event.empty()) {
            reason = "ogl-command-outside-event";
            return std::nullopt;
        }
        auto translated = translate_command(line, reason);
        if (!translated) return std::nullopt;
        commands.push_back(std::move(*translated));
        if (commands.size() > 1024U) {
            reason = "handler-too-large";
            return std::nullopt;
        }
    }

    if (!event.empty() && !flush()) return std::nullopt;
    if (output.handlers.empty()) {
        reason = "no-event-handlers";
        return std::nullopt;
    }

    reason.clear();
    return output;
}

const std::vector<ScriptFeature>& ogl_feature_catalog() {
    static const std::vector<ScriptFeature> features = {
        {"state", "language", ScriptFeatureStatus::implemented},
        {"on", "language", ScriptFeatureStatus::implemented},
        {"let", "language", ScriptFeatureStatus::implemented},
        {"inc", "language", ScriptFeatureStatus::implemented},
        {"goto", "language", ScriptFeatureStatus::implemented},
        {"emit", "runtime", ScriptFeatureStatus::implemented},
        {"timer.every", "runtime", ScriptFeatureStatus::implemented},
        {"chat.listen", "runtime", ScriptFeatureStatus::implemented},
        {"owner.notify", "social", ScriptFeatureStatus::implemented},
        {"user.message", "social", ScriptFeatureStatus::implemented},
        {"world.move", "world", ScriptFeatureStatus::implemented},
        {"world.rotate", "world", ScriptFeatureStatus::implemented},
        {"world.scale", "world", ScriptFeatureStatus::implemented},
        {"world.velocity", "world", ScriptFeatureStatus::implemented},
        {"world.angular_velocity", "world", ScriptFeatureStatus::implemented},
        {"world.physics", "world", ScriptFeatureStatus::implemented},
        {"world.text", "world", ScriptFeatureStatus::implemented},
        {"world.say", "world", ScriptFeatureStatus::implemented},
        {"world.whisper", "world", ScriptFeatureStatus::implemented},
        {"world.shout", "world", ScriptFeatureStatus::implemented},
        {"world.object", "query", ScriptFeatureStatus::implemented},
        {"world.region", "query", ScriptFeatureStatus::implemented},
        {"world.terrain", "query", ScriptFeatureStatus::implemented},
        {"world.water", "query", ScriptFeatureStatus::implemented},
        {"world.time", "query", ScriptFeatureStatus::implemented},
        {"world.nearby", "query", ScriptFeatureStatus::implemented},
        {"stop", "runtime", ScriptFeatureStatus::implemented},
        {"if/else", "language", ScriptFeatureStatus::unsupported},
        {"while/for", "language", ScriptFeatureStatus::unsupported},
        {"functions", "language", ScriptFeatureStatus::unsupported},
        {"typed values", "language", ScriptFeatureStatus::unsupported}
    };
    return features;
}

} // namespace opengenesis::scripting
