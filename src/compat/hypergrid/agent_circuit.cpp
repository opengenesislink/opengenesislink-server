#include "opengenesis/compat/hypergrid/agent_circuit.hpp"

#include <charconv>
#include <cctype>
#include <optional>
#include <string>

namespace opengenesis::compat::hypergrid {
namespace {

std::optional<std::string> json_string(const std::string_view json,
                                       const std::string_view key,
                                       const std::size_t start = 0) {
    const std::string needle = "\"" + std::string{key} + "\"";
    auto position = json.find(needle, start);
    if (position == std::string_view::npos) return std::nullopt;
    position = json.find(':', position + needle.size());
    if (position == std::string_view::npos) return std::nullopt;
    ++position;
    while (position < json.size() &&
           std::isspace(static_cast<unsigned char>(json[position])) != 0) {
        ++position;
    }
    if (position >= json.size() || json[position] != '"') return std::nullopt;
    ++position;
    std::string output;
    while (position < json.size()) {
        const char c = json[position++];
        if (c == '"') return output;
        if (c != '\\') {
            output.push_back(c);
            continue;
        }
        if (position >= json.size()) return std::nullopt;
        const char escaped = json[position++];
        switch (escaped) {
            case '"': output.push_back('"'); break;
            case '\\': output.push_back('\\'); break;
            case '/': output.push_back('/'); break;
            case 'b': output.push_back('\b'); break;
            case 'f': output.push_back('\f'); break;
            case 'n': output.push_back('\n'); break;
            case 'r': output.push_back('\r'); break;
            case 't': output.push_back('\t'); break;
            default: return std::nullopt;
        }
    }
    return std::nullopt;
}

std::optional<std::string_view> json_object(const std::string_view json,
                                            const std::string_view key) {
    const std::string needle = "\"" + std::string{key} + "\"";
    auto position = json.find(needle);
    if (position == std::string_view::npos) return std::nullopt;
    position = json.find(':', position + needle.size());
    if (position == std::string_view::npos) return std::nullopt;
    position = json.find('{', position + 1);
    if (position == std::string_view::npos) return std::nullopt;

    const auto begin = position;
    int depth = 0;
    bool quoted = false;
    bool escaped = false;
    for (; position < json.size(); ++position) {
        const char c = json[position];
        if (quoted) {
            if (escaped) escaped = false;
            else if (c == '\\') escaped = true;
            else if (c == '"') quoted = false;
            continue;
        }
        if (c == '"') quoted = true;
        else if (c == '{') ++depth;
        else if (c == '}') {
            --depth;
            if (depth == 0) return json.substr(begin, position - begin + 1);
        }
    }
    return std::nullopt;
}

std::optional<bool> json_bool(const std::string_view json,
                              const std::string_view key) {
    const std::string needle = "\"" + std::string{key} + "\"";
    auto position = json.find(needle);
    if (position == std::string_view::npos) return std::nullopt;
    position = json.find(':', position + needle.size());
    if (position == std::string_view::npos) return std::nullopt;
    ++position;
    while (position < json.size() &&
           std::isspace(static_cast<unsigned char>(json[position])) != 0) {
        ++position;
    }
    if (json.substr(position, 4) == "true") return true;
    if (json.substr(position, 5) == "false") return false;
    return std::nullopt;
}

template <typename T>
std::optional<T> json_integer(const std::string_view json, const std::string_view key) {
    const std::string needle = "\"" + std::string{key} + "\"";
    auto position = json.find(needle);
    if (position == std::string_view::npos) return std::nullopt;
    position = json.find(':', position + needle.size());
    if (position == std::string_view::npos) return std::nullopt;
    ++position;
    while (position < json.size() &&
           std::isspace(static_cast<unsigned char>(json[position])) != 0) {
        ++position;
    }
    if (position < json.size() && json[position] == '"') ++position;
    const auto begin = position;
    while (position < json.size() &&
           (std::isdigit(static_cast<unsigned char>(json[position])) != 0 ||
            json[position] == '-')) {
        ++position;
    }
    if (position == begin) return std::nullopt;
    T output{};
    const auto [end, error] = std::from_chars(
        json.data() + begin, json.data() + position, output);
    if (error != std::errc{} || end != json.data() + position) return std::nullopt;
    return output;
}

std::string normalize_uri(std::string value) {
    while (!value.empty() && value.back() == '/') value.pop_back();
    return value;
}

bool plausible_uuid(const std::string_view value) {
    return value.size() == 36 && value[8] == '-' && value[13] == '-' &&
           value[18] == '-' && value[23] == '-';
}

} // namespace

std::optional<ForeignAgentCircuit> parse_foreign_agent_circuit(
    const std::string_view json,
    std::string& reason) {
    ForeignAgentCircuit circuit;
    circuit.agent_id = json_string(json, "agent_id").value_or("");
    circuit.session_id = json_string(json, "session_id").value_or("");
    circuit.secure_session_id = json_string(json, "secure_session_id").value_or("");
    circuit.service_session_id = json_string(json, "service_session_id").value_or("");
    circuit.caps_path = json_string(json, "caps_path").value_or("");
    circuit.base_folder = json_string(json, "base_folder").value_or("");
    circuit.inventory_folder = json_string(json, "inventory_folder").value_or("");
    circuit.first_name = json_string(json, "first_name").value_or("");
    circuit.last_name = json_string(json, "last_name").value_or("");
    circuit.client_ip = json_string(json, "client_ip").value_or("");
    circuit.viewer = json_string(json, "viewer").value_or("");
    circuit.channel = json_string(json, "channel").value_or("");
    circuit.mac = json_string(json, "mac").value_or("");
    circuit.id0 = json_string(json, "id0").value_or("");
    circuit.start_pos = json_string(json, "start_pos").value_or("<128, 128, 25>");
    circuit.child = json_bool(json, "child").value_or(false);
    circuit.circuit_code = json_integer<std::uint32_t>(json, "circuit_code").value_or(0U);
    circuit.destination_uuid = json_string(json, "destination_uuid").value_or("");
    circuit.destination_name = json_string(json, "destination_name").value_or("");
    circuit.destination_x = json_integer<std::int32_t>(json, "destination_x").value_or(0);
    circuit.destination_y = json_integer<std::int32_t>(json, "destination_y").value_or(0);
    circuit.teleport_flags = json_integer<std::uint32_t>(json, "teleport_flags").value_or(0);

    if (const auto urls = json_object(json, "serviceurls")) {
        circuit.home_uri = json_string(*urls, "HomeURI").value_or("");
        circuit.asset_uri = json_string(*urls, "AssetServerURI").value_or("");
        circuit.inventory_uri = json_string(*urls, "InventoryServerURI").value_or("");
        circuit.avatar_uri = json_string(*urls, "AvatarServerURI").value_or("");
        circuit.im_uri = json_string(*urls, "IMServerURI").value_or("");
    }

    if (!plausible_uuid(circuit.agent_id) || !plausible_uuid(circuit.session_id) ||
        circuit.service_session_id.empty() || circuit.home_uri.empty() ||
        circuit.destination_uuid.empty()) {
        reason = "missing-or-invalid-agent-circuit-fields";
        return std::nullopt;
    }
    if (!circuit.home_uri.starts_with("http://") &&
        !circuit.home_uri.starts_with("https://")) {
        reason = "invalid-home-uri";
        return std::nullopt;
    }

    reason.clear();
    return circuit;
}

std::string service_token_destination(const std::string_view service_token) {
    const auto delimiter = service_token.find(';');
    if (delimiter == std::string_view::npos || delimiter == 0) return {};
    return normalize_uri(std::string{service_token.substr(0, delimiter)});
}

bool service_token_targets(const std::string_view service_token,
                           const std::string_view gatekeeper_uri) {
    const auto destination = service_token_destination(service_token);
    return !destination.empty() &&
           destination == normalize_uri(std::string{gatekeeper_uri});
}

} // namespace opengenesis::compat::hypergrid
