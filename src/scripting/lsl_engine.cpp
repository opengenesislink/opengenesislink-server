#include "opengenesis/scripting/lsl_engine.hpp"
#include "opengenesis/scripting/lsl_builtins.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <sstream>
#include <string>
#include <unordered_set>
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

bool identifier(std::string_view value) {
    if (value.empty()) return false;
    const auto first = static_cast<unsigned char>(value.front());
    if (std::isalpha(first) == 0 && value.front() != '_') return false;
    return std::all_of(value.begin() + 1, value.end(), [](const char c) {
        const auto u = static_cast<unsigned char>(c);
        return std::isalnum(u) != 0 || c == '_';
    });
}

std::string strip_comments(std::string_view source) {
    std::string out;
    out.reserve(source.size());
    bool quote = false;
    bool escape = false;
    for (std::size_t i = 0; i < source.size(); ++i) {
        const char c = source[i];
        if (escape) {
            out.push_back(c);
            escape = false;
            continue;
        }
        if (quote && c == '\\') {
            out.push_back(c);
            escape = true;
            continue;
        }
        if (c == '"') {
            quote = !quote;
            out.push_back(c);
            continue;
        }
        if (!quote && c == '/' && i + 1U < source.size() &&
            source[i + 1U] == '/') {
            while (i < source.size() && source[i] != '\n') ++i;
            if (i < source.size()) out.push_back('\n');
            continue;
        }
        if (!quote && c == '/' && i + 1U < source.size() &&
            source[i + 1U] == '*') {
            i += 2U;
            while (i + 1U < source.size() &&
                   !(source[i] == '*' && source[i + 1U] == '/')) {
                if (source[i] == '\n') out.push_back('\n');
                ++i;
            }
            if (i + 1U < source.size()) ++i;
            continue;
        }
        out.push_back(c);
    }
    return out;
}

std::size_t matching_brace(const std::string_view source, const std::size_t open) {
    if (open >= source.size() || source[open] != '{') return std::string_view::npos;
    std::size_t depth = 0U;
    bool quote = false;
    bool escape = false;
    for (std::size_t i = open; i < source.size(); ++i) {
        const char c = source[i];
        if (escape) {
            escape = false;
            continue;
        }
        if (quote && c == '\\') {
            escape = true;
            continue;
        }
        if (c == '"') {
            quote = !quote;
            continue;
        }
        if (quote) continue;
        if (c == '{') ++depth;
        else if (c == '}') {
            if (--depth == 0U) return i;
        }
    }
    return std::string_view::npos;
}

std::vector<std::string> split_args(std::string_view text) {
    std::vector<std::string> args;
    std::size_t start = 0U;
    int angle = 0;
    int square = 0;
    int round = 0;
    bool quote = false;
    bool escape = false;
    for (std::size_t i = 0; i <= text.size(); ++i) {
        const char c = i < text.size() ? text[i] : ',';
        if (escape) {
            escape = false;
            continue;
        }
        if (quote && c == '\\') {
            escape = true;
            continue;
        }
        if (c == '"') {
            quote = !quote;
            continue;
        }
        if (!quote) {
            if (c == '<') ++angle;
            else if (c == '>') --angle;
            else if (c == '[') ++square;
            else if (c == ']') --square;
            else if (c == '(') ++round;
            else if (c == ')') --round;
        }
        if (c == ',' && !quote && angle == 0 && square == 0 && round == 0) {
            args.push_back(trim(std::string{text.substr(start, i - start)}));
            start = i + 1U;
        }
    }
    if (args.size() == 1U && args.front().empty()) args.clear();
    return args;
}

std::string unquote(std::string value) {
    value = trim(std::move(value));
    if (value.size() >= 2U && value.front() == '"' && value.back() == '"') {
        value = value.substr(1U, value.size() - 2U);
    }
    return value;
}

std::string value_expression(std::string value) {
    value = trim(std::move(value));
    if (value.size() >= 2U && value.front() == '"' && value.back() == '"') {
        return value.substr(1U, value.size() - 2U);
    }
    if (identifier(value)) return "$" + value;
    return value;
}

std::optional<std::string> vector3(std::string value) {
    value = trim(std::move(value));
    if (value.size() < 5U || value.front() != '<' || value.back() != '>') {
        return std::nullopt;
    }
    const auto args = split_args(
        std::string_view{value}.substr(1U, value.size() - 2U));
    if (args.size() != 3U) return std::nullopt;
    return args[0] + " " + args[1] + " " + args[2];
}

bool zero_channel(const std::string& value) {
    return trim(value) == "0" || trim(value) == "PUBLIC_CHANNEL";
}

std::optional<std::string> translate_call(
    const std::string& name,
    const std::vector<std::string>& args,
    std::string& reason) {
    auto text_arg = [&](const std::size_t index) -> std::optional<std::string> {
        if (index >= args.size()) return std::nullopt;
        auto value = value_expression(args[index]);
        if (value.size() > 2048U) return std::nullopt;
        return value;
    };

    if (name == "llSay" || name == "llWhisper" || name == "llShout") {
        if (args.size() != 2U || !zero_channel(args[0])) {
            reason = "lsl-channel-chat-only-public-supported";
            return std::nullopt;
        }
        const auto text = text_arg(1U);
        if (!text) {
            reason = "lsl-invalid-chat";
            return std::nullopt;
        }
        const auto command = name == "llSay"
                                 ? "say "
                                 : (name == "llWhisper" ? "whisper " : "shout ");
        return command + *text;
    }
    if (name == "llOwnerSay") {
        if (args.size() != 1U) {
            reason = "lsl-invalid-owner-say";
            return std::nullopt;
        }
        const auto text = text_arg(0U);
        return text ? std::optional<std::string>{"notify " + *text}
                    : std::nullopt;
    }
    if (name == "llInstantMessage") {
        if (args.size() != 2U) {
            reason = "lsl-invalid-instant-message";
            return std::nullopt;
        }
        return "message " + trim(args[0]) + " " + unquote(args[1]);
    }
    if (name == "llSetTimerEvent") {
        if (args.size() != 1U) {
            reason = "lsl-invalid-timer";
            return std::nullopt;
        }
        try {
            const double seconds = std::stod(trim(args[0]));
            if (!std::isfinite(seconds) || seconds < 0.0 || seconds > 86400.0) {
                reason = "lsl-invalid-timer";
                return std::nullopt;
            }
            const auto milliseconds =
                static_cast<long long>(std::llround(seconds * 1000.0));
            return "timer " + std::to_string(milliseconds);
        } catch (...) {
            reason = "lsl-timer-constant-required";
            return std::nullopt;
        }
    }
    if (name == "llListen") {
        if (args.size() != 4U) {
            reason = "lsl-invalid-listen";
            return std::nullopt;
        }
        return "listen " + trim(args[0]);
    }
    if (name == "llSetPos" || name == "llSetRegionPos" ||
        name == "llSetScale" || name == "llSetVelocity" ||
        name == "llSetAngularVelocity") {
        if (args.empty()) {
            reason = "lsl-invalid-vector-call";
            return std::nullopt;
        }
        const auto value = vector3(args[0]);
        if (!value) {
            reason = "lsl-vector-constant-required";
            return std::nullopt;
        }
        const auto command =
            name == "llSetScale" ? "scale " :
            (name == "llSetVelocity" ? "velocity " :
             (name == "llSetAngularVelocity" ? "angular_velocity " : "move "));
        return command + *value;
    }
    if (name == "llSetText") {
        if (args.size() != 3U) {
            reason = "lsl-invalid-set-text";
            return std::nullopt;
        }
        return "text " + unquote(args[0]);
    }
    if (name == "llSetStatus") {
        if (args.size() != 2U || trim(args[0]) != "STATUS_PHYSICS") {
            reason = "lsl-only-status-physics-supported";
            return std::nullopt;
        }
        const auto enabled = trim(args[1]);
        if (enabled == "TRUE" || enabled == "1") return "physics 1";
        if (enabled == "FALSE" || enabled == "0") return "physics 0";
        reason = "lsl-status-constant-required";
        return std::nullopt;
    }
    if (name == "llResetScript") return "state default";

    reason = "lsl-function-not-executable";
    return std::nullopt;
}

std::vector<std::string> split_statements(std::string_view body) {
    std::vector<std::string> statements;
    std::size_t start = 0U;
    bool quote = false;
    bool escape = false;
    int nested = 0;
    for (std::size_t i = 0; i <= body.size(); ++i) {
        const char c = i < body.size() ? body[i] : ';';
        if (escape) {
            escape = false;
            continue;
        }
        if (quote && c == '\\') {
            escape = true;
            continue;
        }
        if (c == '"') {
            quote = !quote;
            continue;
        }
        if (!quote) {
            if (c == '(' || c == '[' || c == '<') ++nested;
            else if (c == ')' || c == ']' || c == '>') --nested;
        }
        if (c == ';' && !quote && nested == 0) {
            auto statement = trim(std::string{body.substr(start, i - start)});
            if (!statement.empty()) statements.push_back(std::move(statement));
            start = i + 1U;
        }
    }
    return statements;
}

bool compile_event_body(
    const std::string& logical_state,
    const std::string& event,
    std::string_view body,
    CompiledScript& output,
    std::string& reason) {
    std::ostringstream legacy;
    legacy << "event " << event << '\n';

    for (auto statement : split_statements(body)) {
        if (statement.starts_with("state ")) {
            const auto target = trim(statement.substr(6U));
            if (!identifier(target)) {
                reason = "lsl-invalid-state-change";
                return false;
            }
            legacy << "state " << target << '\n';
            continue;
        }

        static constexpr std::string_view declaration_types[] = {
            "integer ", "float ", "string ", "key ",
            "vector ", "rotation ", "list "
        };
        std::string assignment = statement;
        bool declaration = false;
        for (const auto type : declaration_types) {
            if (assignment.starts_with(type)) {
                assignment = trim(assignment.substr(type.size()));
                declaration = true;
                break;
            }
        }

        const auto equal = assignment.find('=');
        if (equal != std::string::npos) {
            const auto target = trim(assignment.substr(0U, equal));
            const auto expression = trim(assignment.substr(equal + 1U));
            if (!identifier(target) || expression.empty()) {
                reason = "lsl-invalid-assignment";
                return false;
            }

            const auto call_open = expression.find('(');
            const auto call_close = expression.rfind(')');
            if (call_open != std::string::npos &&
                call_close == expression.size() - 1U &&
                call_close > call_open) {
                const auto function =
                    trim(expression.substr(0U, call_open));
                if (!identifier(function) ||
                    !lsl_builtin_implemented(function)) {
                    reason = "lsl-builtin-not-implemented";
                    return false;
                }
                const auto args = split_args(
                    std::string_view{expression}.substr(
                        call_open + 1U,
                        call_close - call_open - 1U));
                std::string encoded_args;
                for (std::size_t index = 0; index < args.size(); ++index) {
                    if (index != 0U) encoded_args.push_back('\x1f');
                    encoded_args += value_expression(args[index]);
                }
                legacy << "builtin " << target << ' ' << function;
                if (!encoded_args.empty()) {
                    legacy << ' ' << encoded_args;
                }
                legacy << '\n';
                continue;
            }

            if (expression.find('(') != std::string::npos) {
                reason = "lsl-expression-not-supported";
                return false;
            }
            legacy << "set " << target << ' '
                   << value_expression(expression) << '\n';
            continue;
        }

        if (declaration) {
            reason = "lsl-declaration-requires-initializer";
            return false;
        }

        const auto open = statement.find('(');
        const auto close = statement.rfind(')');
        if (open == std::string::npos || close == std::string::npos ||
            close < open) {
            reason = "lsl-statement-not-supported";
            return false;
        }
        const auto function = trim(statement.substr(0, open));
        if (!identifier(function)) {
            reason = "lsl-invalid-function";
            return false;
        }
        const auto args = split_args(std::string_view{statement}.substr(
            open + 1U, close - open - 1U));
        auto command = translate_call(function, args, reason);
        if (!command) return false;
        legacy << *command << '\n';
    }
    legacy << "end\n";

    auto compiled = compile_script(legacy.str(), reason);
    if (!compiled || compiled->handlers.size() != 1U) return false;
    auto handler = std::move(compiled->handlers.front());
    handler.state = logical_state;
    output.handlers.push_back(std::move(handler));
    return true;
}

bool parse_state_body(
    const std::string& state,
    std::string_view body,
    CompiledScript& output,
    std::string& reason) {
    std::size_t pos = 0U;
    while (pos < body.size()) {
        while (pos < body.size() &&
               std::isspace(static_cast<unsigned char>(body[pos])) != 0) {
            ++pos;
        }
        if (pos >= body.size()) break;

        const auto name_start = pos;
        while (pos < body.size() &&
               (std::isalnum(static_cast<unsigned char>(body[pos])) != 0 ||
                body[pos] == '_')) {
            ++pos;
        }
        const auto event = std::string{body.substr(name_start, pos - name_start)};
        if (!identifier(event)) {
            reason = "lsl-invalid-event";
            return false;
        }
        while (pos < body.size() &&
               std::isspace(static_cast<unsigned char>(body[pos])) != 0) {
            ++pos;
        }
        if (pos >= body.size() || body[pos] != '(') {
            reason = "lsl-event-signature-required";
            return false;
        }
        const auto parameter_open = pos;
        int depth = 1;
        ++pos;
        while (pos < body.size() && depth > 0) {
            if (body[pos] == '(') ++depth;
            else if (body[pos] == ')') --depth;
            ++pos;
        }
        if (depth != 0) {
            reason = "lsl-event-signature-unclosed";
            return false;
        }
        const auto parameter_text = body.substr(
            parameter_open + 1U, pos - parameter_open - 2U);
        std::vector<std::string> parameters;
        for (const auto& declaration : split_args(parameter_text)) {
            std::istringstream declaration_stream(declaration);
            std::string token;
            std::string name;
            while (declaration_stream >> token) name = token;
            if (!name.empty()) {
                if (!identifier(name)) {
                    reason = "lsl-invalid-event-parameter";
                    return false;
                }
                parameters.push_back(std::move(name));
            }
        }
        while (pos < body.size() &&
               std::isspace(static_cast<unsigned char>(body[pos])) != 0) {
            ++pos;
        }
        if (pos >= body.size() || body[pos] != '{') {
            reason = "lsl-event-body-required";
            return false;
        }
        const auto close = matching_brace(body, pos);
        if (close == std::string_view::npos) {
            reason = "lsl-event-body-unclosed";
            return false;
        }
        if (!compile_event_body(
                state, event, body.substr(pos + 1U, close - pos - 1U),
                output, reason)) {
            return false;
        }
        output.handlers.back().parameters = std::move(parameters);
        pos = close + 1U;
    }
    return true;
}

} // namespace

std::optional<CompiledScript> compile_lsl_script(
    const std::string_view source, std::string& reason) {
    if (source.empty() || source.size() > 64U * 1024U) {
        reason = "invalid-script-size";
        return std::nullopt;
    }
    const auto clean = strip_comments(source);
    CompiledScript output;
    std::size_t pos = 0U;

    while (pos < clean.size()) {
        while (pos < clean.size() &&
               std::isspace(static_cast<unsigned char>(clean[pos])) != 0) {
            ++pos;
        }
        if (pos >= clean.size()) break;

        std::string state;
        const auto state_start = pos;
        while (pos < clean.size() &&
               (std::isalnum(static_cast<unsigned char>(clean[pos])) != 0 ||
                clean[pos] == '_')) {
            ++pos;
        }
        state = clean.substr(state_start, pos - state_start);
        if (state == "state") {
            reason = "lsl-state-keyword-is-only-valid-for-state-change";
            return std::nullopt;
        }

        if (!identifier(state)) {
            reason = "lsl-invalid-state";
            return std::nullopt;
        }
        while (pos < clean.size() &&
               std::isspace(static_cast<unsigned char>(clean[pos])) != 0) {
            ++pos;
        }
        if (pos >= clean.size() || clean[pos] != '{') {
            reason = "lsl-state-body-required";
            return std::nullopt;
        }
        const auto close = matching_brace(clean, pos);
        if (close == std::string::npos) {
            reason = "lsl-state-body-unclosed";
            return std::nullopt;
        }
        if (!parse_state_body(
                state,
                std::string_view{clean}.substr(pos + 1U, close - pos - 1U),
                output, reason)) {
            return std::nullopt;
        }
        pos = close + 1U;
    }

    if (output.handlers.empty()) {
        reason = "no-event-handlers";
        return std::nullopt;
    }
    reason.clear();
    return output;
}

const std::vector<ScriptFeature>& lsl_event_catalog() {
    static const std::vector<ScriptFeature> events = {
        {"attach", "event", ScriptFeatureStatus::recognized},
        {"at_rot_target", "event", ScriptFeatureStatus::recognized},
        {"at_target", "event", ScriptFeatureStatus::recognized},
        {"changed", "event", ScriptFeatureStatus::recognized},
        {"collision", "event", ScriptFeatureStatus::recognized},
        {"collision_end", "event", ScriptFeatureStatus::recognized},
        {"collision_start", "event", ScriptFeatureStatus::recognized},
        {"control", "event", ScriptFeatureStatus::recognized},
        {"dataserver", "event", ScriptFeatureStatus::recognized},
        {"email", "event", ScriptFeatureStatus::recognized},
        {"event_order", "documentation", ScriptFeatureStatus::unsupported},
        {"experience_permissions", "event", ScriptFeatureStatus::recognized},
        {"experience_permissions_denied", "event", ScriptFeatureStatus::recognized},
        {"final_damage", "event", ScriptFeatureStatus::recognized},
        {"game_control", "event", ScriptFeatureStatus::recognized},
        {"http_request", "event", ScriptFeatureStatus::recognized},
        {"http_response", "event", ScriptFeatureStatus::recognized},
        {"land_collision", "event", ScriptFeatureStatus::recognized},
        {"land_collision_end", "event", ScriptFeatureStatus::recognized},
        {"land_collision_start", "event", ScriptFeatureStatus::recognized},
        {"linkset_data", "event", ScriptFeatureStatus::recognized},
        {"link_message", "event", ScriptFeatureStatus::recognized},
        {"listen", "event", ScriptFeatureStatus::partial},
        {"money", "event", ScriptFeatureStatus::recognized},
        {"moving_end", "event", ScriptFeatureStatus::recognized},
        {"moving_start", "event", ScriptFeatureStatus::recognized},
        {"not_at_rot_target", "event", ScriptFeatureStatus::recognized},
        {"not_at_target", "event", ScriptFeatureStatus::recognized},
        {"no_sensor", "event", ScriptFeatureStatus::recognized},
        {"object_rez", "event", ScriptFeatureStatus::recognized},
        {"on_damage", "event", ScriptFeatureStatus::recognized},
        {"on_death", "event", ScriptFeatureStatus::recognized},
        {"on_rez", "event", ScriptFeatureStatus::recognized},
        {"path_update", "event", ScriptFeatureStatus::recognized},
        {"remote_data", "event", ScriptFeatureStatus::recognized},
        {"run_time_permissions", "event", ScriptFeatureStatus::recognized},
        {"sensor", "event", ScriptFeatureStatus::recognized},
        {"state_entry", "event", ScriptFeatureStatus::implemented},
        {"state_exit", "event", ScriptFeatureStatus::recognized},
        {"timer", "event", ScriptFeatureStatus::implemented},
        {"touch", "event", ScriptFeatureStatus::recognized},
        {"touch_end", "event", ScriptFeatureStatus::recognized},
        {"touch_start", "event", ScriptFeatureStatus::partial},
        {"transaction_result", "event", ScriptFeatureStatus::recognized}
    };
    return events;
}

} // namespace opengenesis::scripting
