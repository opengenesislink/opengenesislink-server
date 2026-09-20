#include "opengenesis/scripting/ogl_engine.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

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

bool integer_literal(const std::string& value) {
    try {
        std::size_t used = 0U;
        (void)std::stoll(value, &used, 10);
        return used == value.size();
    } catch (...) {
        return false;
    }
}

bool float_literal(const std::string& value) {
    try {
        std::size_t used = 0U;
        const auto parsed = std::stod(value, &used);
        return used == value.size() && std::isfinite(parsed);
    } catch (...) {
        return false;
    }
}

std::string unquote(std::string value) {
    value = trim(std::move(value));
    if (value.size() >= 2U && value.front() == '"' && value.back() == '"') {
        return value.substr(1U, value.size() - 2U);
    }
    return value;
}

std::string command_tail(const std::string& line, const std::string_view command) {
    if (line.size() <= command.size()) return {};
    return trim(line.substr(command.size()));
}

std::optional<std::string> typed_value(
    const std::string& type,
    std::string value,
    std::string& reason) {
    value = trim(std::move(value));
    if (value.empty() || value.size() > 2048U) {
        reason = "ogl-invalid-typed-value";
        return std::nullopt;
    }
    if (value.front() == '$') return value;

    if (type == "integer") {
        if (!integer_literal(value)) {
            reason = "ogl-integer-value-required";
            return std::nullopt;
        }
        return value;
    }
    if (type == "float") {
        if (!float_literal(value)) {
            reason = "ogl-float-value-required";
            return std::nullopt;
        }
        return value;
    }
    if (type == "bool") {
        if (value == "true" || value == "TRUE" || value == "1") return "1";
        if (value == "false" || value == "FALSE" || value == "0") return "0";
        reason = "ogl-bool-value-required";
        return std::nullopt;
    }
    if (type == "string") return unquote(std::move(value));
    if (type == "key") {
        const auto normalized = unquote(std::move(value));
        if (normalized.empty() || normalized.size() > 256U) {
            reason = "ogl-key-value-required";
            return std::nullopt;
        }
        return normalized;
    }
    if (type == "vector") {
        if (value.size() < 5U || value.front() != '<' || value.back() != '>') {
            reason = "ogl-vector-value-required";
            return std::nullopt;
        }
        return value;
    }
    if (type == "rotation") {
        if (value.size() < 7U || value.front() != '<' || value.back() != '>') {
            reason = "ogl-rotation-value-required";
            return std::nullopt;
        }
        return value;
    }
    if (type == "list") {
        if (value.size() < 2U || value.front() != '[' || value.back() != ']') {
            reason = "ogl-list-value-required";
            return std::nullopt;
        }
        return value;
    }
    reason = "ogl-unknown-type";
    return std::nullopt;
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
        {"world.force", "force"},
        {"world.impulse", "impulse"},
        {"world.angular_impulse", "angular_impulse"},
        {"world.torque", "torque"},
        {"world.buoyancy", "buoyancy"},
        {"world.material", "material"},
        {"world.physics", "physics"},
        {"world.shape", "shape"},
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
        {"world.raycast", "raycast"},
        {"stop", "stop"}
    };

    if (line.starts_with("let ")) {
        const auto assignment = line.find('=');
        if (assignment == std::string::npos) {
            reason = "ogl-invalid-let";
            return std::nullopt;
        }
        auto declaration = trim(line.substr(4U, assignment - 4U));
        auto value = trim(line.substr(assignment + 1U));
        std::string type;
        static constexpr std::string_view types[] = {
            "integer", "float", "bool", "string", "key",
            "vector", "rotation", "list"
        };
        for (const auto candidate : types) {
            const auto prefix = std::string{candidate} + " ";
            if (declaration.starts_with(prefix)) {
                type = std::string{candidate};
                declaration = trim(declaration.substr(prefix.size()));
                break;
            }
        }
        if (!atom(declaration)) {
            reason = "ogl-invalid-let";
            return std::nullopt;
        }
        if (!type.empty()) {
            const auto normalized = typed_value(type, std::move(value), reason);
            if (!normalized) return std::nullopt;
            value = *normalized;
        } else {
            value = unquote(std::move(value));
            if (value.size() > 2048U) {
                reason = "ogl-invalid-let";
                return std::nullopt;
            }
        }
        return "set " + declaration + " " + value;
    }

    if (line.starts_with("inc ")) {
        const auto by = line.find(" by ", 4U);
        if (by == std::string::npos) {
            reason = "ogl-invalid-inc";
            return std::nullopt;
        }
        const auto name = trim(line.substr(4U, by - 4U));
        const auto value = trim(line.substr(by + 4U));
        if (!atom(name) || !integer_literal(value)) {
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

struct Condition {
    std::string left;
    std::string op;
    std::string right;
};

std::string condition_operand(std::string value) {
    value = trim(std::move(value));
    if (value.size() >= 2U && value.front() == '"' && value.back() == '"') {
        return value.substr(1U, value.size() - 2U);
    }
    if (value == "true" || value == "TRUE") return "1";
    if (value == "false" || value == "FALSE") return "0";
    if (value.starts_with("$") || integer_literal(value) || float_literal(value)) {
        return value;
    }
    if (atom(value)) return "$" + value;
    return value;
}

std::optional<Condition> parse_condition(
    const std::string& text, std::string& reason) {
    std::istringstream input(text);
    std::string left;
    std::string op;
    std::string right;
    std::string extra;
    input >> left >> op >> right >> extra;
    const bool valid_op =
        op == "==" || op == "!=" || op == "<" ||
        op == "<=" || op == ">" || op == ">=";
    if (left.empty() || right.empty() || !valid_op || !extra.empty()) {
        reason = "ogl-invalid-condition";
        return std::nullopt;
    }
    return Condition{
        .left = condition_operand(std::move(left)),
        .op = std::move(op),
        .right = condition_operand(std::move(right))};
}

bool patch_target(
    std::vector<std::string>& commands,
    const std::size_t index,
    const std::size_t target) {
    if (index >= commands.size()) return false;
    const auto last_space = commands[index].rfind(' ');
    if (last_space == std::string::npos) return false;
    commands[index].replace(
        last_space + 1U,
        std::string::npos,
        std::to_string(target));
    return true;
}

enum class BlockKind {
    if_block,
    while_block,
    for_block
};

struct Block {
    BlockKind kind{BlockKind::if_block};
    std::size_t condition_index{0U};
    std::optional<std::size_t> exit_jump;
    std::string variable;
    std::string step;
};

std::optional<std::vector<std::string>> compile_commands(
    const std::vector<std::string>& raw,
    std::string& reason) {
    std::vector<std::string> commands;
    std::vector<Block> blocks;

    for (const auto& line : raw) {
        if (line.starts_with("if ")) {
            const auto condition = parse_condition(line.substr(3U), reason);
            if (!condition) return std::nullopt;
            const auto index = commands.size();
            commands.push_back(
                "jfalse " + condition->left + " " + condition->op + " " +
                condition->right + " 0");
            blocks.push_back({
                .kind = BlockKind::if_block,
                .condition_index = index,
                .exit_jump = std::nullopt,
                .variable = {},
                .step = {}});
            continue;
        }

        if (line == "else") {
            if (blocks.empty() ||
                blocks.back().kind != BlockKind::if_block ||
                blocks.back().exit_jump.has_value()) {
                reason = "ogl-unexpected-else";
                return std::nullopt;
            }
            const auto jump_index = commands.size();
            commands.push_back("jump 0");
            if (!patch_target(
                    commands,
                    blocks.back().condition_index,
                    commands.size())) {
                reason = "ogl-control-flow-patch-failed";
                return std::nullopt;
            }
            blocks.back().exit_jump = jump_index;
            continue;
        }

        if (line == "endif") {
            if (blocks.empty() || blocks.back().kind != BlockKind::if_block) {
                reason = "ogl-unexpected-endif";
                return std::nullopt;
            }
            const auto block = blocks.back();
            blocks.pop_back();
            const auto patch_index =
                block.exit_jump.value_or(block.condition_index);
            if (!patch_target(commands, patch_index, commands.size())) {
                reason = "ogl-control-flow-patch-failed";
                return std::nullopt;
            }
            continue;
        }

        if (line.starts_with("while ")) {
            const auto condition = parse_condition(line.substr(6U), reason);
            if (!condition) return std::nullopt;
            const auto index = commands.size();
            commands.push_back(
                "jfalse " + condition->left + " " + condition->op + " " +
                condition->right + " 0");
            blocks.push_back({
                .kind = BlockKind::while_block,
                .condition_index = index,
                .exit_jump = std::nullopt,
                .variable = {},
                .step = {}});
            continue;
        }

        if (line == "endwhile") {
            if (blocks.empty() || blocks.back().kind != BlockKind::while_block) {
                reason = "ogl-unexpected-endwhile";
                return std::nullopt;
            }
            const auto block = blocks.back();
            blocks.pop_back();
            commands.push_back("jump " + std::to_string(block.condition_index));
            if (!patch_target(
                    commands, block.condition_index, commands.size())) {
                reason = "ogl-control-flow-patch-failed";
                return std::nullopt;
            }
            continue;
        }

        if (line.starts_with("for ")) {
            const auto body = trim(line.substr(4U));
            const auto from = body.find(" from ");
            const auto to = body.find(" to ", from == std::string::npos ? 0U : from + 6U);
            if (from == std::string::npos || to == std::string::npos) {
                reason = "ogl-invalid-for";
                return std::nullopt;
            }
            const auto variable = trim(body.substr(0U, from));
            const auto start = trim(body.substr(from + 6U, to - from - 6U));
            auto tail = trim(body.substr(to + 4U));
            std::string finish = tail;
            std::string step = "1";
            const auto step_pos = tail.find(" step ");
            if (step_pos != std::string::npos) {
                finish = trim(tail.substr(0U, step_pos));
                step = trim(tail.substr(step_pos + 6U));
            }
            if (!atom(variable) || !integer_literal(start) ||
                !integer_literal(step)) {
                reason = "ogl-invalid-for";
                return std::nullopt;
            }
            const auto step_value = std::stoll(step);
            if (step_value == 0) {
                reason = "ogl-for-zero-step";
                return std::nullopt;
            }
            commands.push_back("set " + variable + " " + start);
            const auto condition_index = commands.size();
            const auto op = step_value > 0 ? "<=" : ">=";
            commands.push_back(
                "jfalse $" + variable + " " + op + " " +
                condition_operand(finish) + " 0");
            blocks.push_back({
                .kind = BlockKind::for_block,
                .condition_index = condition_index,
                .exit_jump = std::nullopt,
                .variable = variable,
                .step = step});
            continue;
        }

        if (line == "endfor") {
            if (blocks.empty() || blocks.back().kind != BlockKind::for_block) {
                reason = "ogl-unexpected-endfor";
                return std::nullopt;
            }
            const auto block = blocks.back();
            blocks.pop_back();
            commands.push_back("add " + block.variable + " " + block.step);
            commands.push_back("jump " + std::to_string(block.condition_index));
            if (!patch_target(
                    commands, block.condition_index, commands.size())) {
                reason = "ogl-control-flow-patch-failed";
                return std::nullopt;
            }
            continue;
        }

        auto translated = translate_command(line, reason);
        if (!translated) return std::nullopt;
        commands.push_back(std::move(*translated));
        if (commands.size() > 1024U) {
            reason = "handler-too-large";
            return std::nullopt;
        }
    }

    if (!blocks.empty()) {
        reason = "ogl-unclosed-control-flow";
        return std::nullopt;
    }
    return commands;
}

using FunctionMap =
    std::unordered_map<std::string, std::vector<std::string>>;

std::optional<std::vector<std::string>> expand_calls(
    const std::vector<std::string>& input,
    const FunctionMap& functions,
    const std::size_t depth,
    std::string& reason) {
    if (depth > 8U) {
        reason = "ogl-function-recursion-limit";
        return std::nullopt;
    }
    std::vector<std::string> output;
    for (const auto& line : input) {
        if (!line.starts_with("call ")) {
            output.push_back(line);
            continue;
        }
        const auto name = trim(line.substr(5U));
        const auto it = functions.find(name);
        if (!atom(name) || it == functions.end()) {
            reason = "ogl-unknown-function";
            return std::nullopt;
        }
        const auto expanded =
            expand_calls(it->second, functions, depth + 1U, reason);
        if (!expanded) return std::nullopt;
        output.insert(output.end(), expanded->begin(), expanded->end());
        if (output.size() > 4096U) {
            reason = "ogl-expanded-program-too-large";
            return std::nullopt;
        }
    }
    return output;
}

std::vector<std::string> normalized_lines(std::string_view source) {
    std::vector<std::string> lines;
    std::istringstream input(std::string{source});
    std::string line;
    while (std::getline(input, line)) {
        const auto comment = line.find("//");
        if (comment != std::string::npos) line.resize(comment);
        line = trim(std::move(line));
        if (line.empty() || line.starts_with("#")) continue;
        if (!line.empty() && line.back() == ';') {
            line.pop_back();
            line = trim(std::move(line));
        }
        if (!line.empty()) lines.push_back(std::move(line));
    }
    return lines;
}

} // namespace

std::optional<CompiledScript> compile_ogl_script(
    const std::string_view source, std::string& reason) {
    if (source.empty() || source.size() > 64U * 1024U) {
        reason = "invalid-script-size";
        return std::nullopt;
    }

    const auto lines = normalized_lines(source);
    if (lines.size() > 4096U) {
        reason = "ogl-too-many-lines";
        return std::nullopt;
    }

    FunctionMap functions;
    std::vector<std::string> program_lines;
    for (std::size_t index = 0U; index < lines.size(); ++index) {
        const auto& line = lines[index];
        if (!line.starts_with("function ")) {
            program_lines.push_back(line);
            continue;
        }
        const auto name = trim(line.substr(9U));
        if (!atom(name) || functions.contains(name)) {
            reason = "ogl-invalid-function";
            return std::nullopt;
        }
        std::vector<std::string> body;
        bool closed = false;
        for (++index; index < lines.size(); ++index) {
            if (lines[index] == "endfunction") {
                closed = true;
                break;
            }
            if (lines[index].starts_with("function ")) {
                reason = "ogl-nested-function";
                return std::nullopt;
            }
            body.push_back(lines[index]);
        }
        if (!closed) {
            reason = "ogl-unclosed-function";
            return std::nullopt;
        }
        functions.emplace(name, std::move(body));
    }

    const auto expanded =
        expand_calls(program_lines, functions, 0U, reason);
    if (!expanded) return std::nullopt;

    CompiledScript output;
    std::string logical_state = "default";
    std::string event;
    std::vector<std::string> raw_commands;

    auto flush = [&]() -> bool {
        if (event.empty()) return true;
        const auto commands = compile_commands(raw_commands, reason);
        if (!commands) return false;
        std::ostringstream legacy;
        legacy << "event " << event << '\n';
        for (const auto& command : *commands) legacy << command << '\n';
        legacy << "end\n";
        auto compiled = compile_script(legacy.str(), reason);
        if (!compiled || compiled->handlers.size() != 1U) return false;
        auto handler = std::move(compiled->handlers.front());
        handler.state = logical_state;
        output.handlers.push_back(std::move(handler));
        event.clear();
        raw_commands.clear();
        return true;
    };

    for (const auto& line : *expanded) {
        if (line == "@ogl 1" || line == "@ogl 2" ||
            line == "engine ogl 1" || line == "engine ogl 2") {
            continue;
        }

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
        raw_commands.push_back(line);
        if (raw_commands.size() > 4096U) {
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
        {"world.force", "physics", ScriptFeatureStatus::implemented},
        {"world.impulse", "physics", ScriptFeatureStatus::implemented},
        {"world.angular_impulse", "physics", ScriptFeatureStatus::implemented},
        {"world.torque", "physics", ScriptFeatureStatus::implemented},
        {"world.buoyancy", "physics", ScriptFeatureStatus::implemented},
        {"world.material", "physics", ScriptFeatureStatus::implemented},
        {"world.physics", "world", ScriptFeatureStatus::implemented},
        {"world.shape", "physics", ScriptFeatureStatus::implemented},
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
        {"world.raycast", "physics", ScriptFeatureStatus::implemented},
        {"stop", "runtime", ScriptFeatureStatus::implemented},
        {"if/else", "language", ScriptFeatureStatus::implemented},
        {"while/for", "language", ScriptFeatureStatus::implemented},
        {"functions", "language", ScriptFeatureStatus::implemented},
        {"typed values", "language", ScriptFeatureStatus::implemented}
    };
    return features;
}

} // namespace opengenesis::scripting
