#include "opengenesis/compat/hypergrid/gatekeeper_client.hpp"

#include "opengenesis/compat/hypergrid/http_client.hpp"
#include "opengenesis/compat/hypergrid/xmlrpc.hpp"
#include "opengenesis/security/crypto.hpp"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <limits>
#include <sstream>
#include <string>
#include <unordered_map>

namespace opengenesis::compat::hypergrid {
namespace {

std::string trim_slash(std::string value) {
    while (!value.empty() && value.back() == '/') value.pop_back();
    return value;
}

std::string json_escape(std::string_view value) {
    std::ostringstream out;
    for (const unsigned char c : value) {
        switch (c) {
            case '"': out << "\\\""; break;
            case '\\': out << "\\\\"; break;
            case '\n': out << "\\n"; break;
            case '\r': out << "\\r"; break;
            case '\t': out << "\\t"; break;
            default:
                if (c < 0x20U) {
                    constexpr char hex[] = "0123456789abcdef";
                    out << "\\u00" << hex[(c >> 4U) & 0x0fU] << hex[c & 0x0fU];
                } else {
                    out << static_cast<char>(c);
                }
        }
    }
    return out.str();
}

bool truthy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value == "true" || value == "1";
}

std::uint16_t port_value(const std::unordered_map<std::string, std::string>& fields,
                         std::string_view key) {
    const auto it = fields.find(std::string{key});
    if (it == fields.end()) return 0;
    try {
        const auto value = std::stoul(it->second);
        return value > 65535U ? 0 : static_cast<std::uint16_t>(value);
    } catch (...) {
        return 0;
    }
}

std::int32_t i32_value(const std::unordered_map<std::string, std::string>& fields,
                       std::string_view key) {
    const auto it = fields.find(std::string{key});
    if (it == fields.end()) return 0;
    try {
        const auto value = std::stoll(it->second);
        if (value < std::numeric_limits<std::int32_t>::min() ||
            value > std::numeric_limits<std::int32_t>::max()) return 0;
        return static_cast<std::int32_t>(value);
    } catch (...) {
        return 0;
    }
}

std::optional<RemoteHypergridRegion> region_from_fields(
    const std::unordered_map<std::string, std::string>& fields,
    std::string& reason) {
    const auto result = fields.find("result");
    if (result == fields.end() || !truthy(result->second)) {
        const auto message = fields.find("message");
        reason = message == fields.end() ? "remote-region-unavailable" : message->second;
        return std::nullopt;
    }
    const auto uuid = fields.find("uuid");
    if (uuid == fields.end() || uuid->second.size() != 36U) {
        reason = "remote-region-response-invalid";
        return std::nullopt;
    }

    RemoteHypergridRegion region;
    region.region_id = uuid->second;
    if (const auto it = fields.find("region_name"); it != fields.end()) region.name = it->second;
    if (const auto it = fields.find("server_uri"); it != fields.end()) region.server_uri = it->second;
    if (const auto it = fields.find("external_name");
        it != fields.end() && region.server_uri.empty()) region.server_uri = it->second;
    if (const auto it = fields.find("hostname"); it != fields.end()) region.host = it->second;
    region.http_port = port_value(fields, "http_port");
    region.internal_port = port_value(fields, "internal_port");
    region.x = i32_value(fields, "x");
    region.y = i32_value(fields, "y");
    const auto sx = port_value(fields, "size_x");
    const auto sy = port_value(fields, "size_y");
    if (sx != 0) region.size_x = sx;
    if (sy != 0) region.size_y = sy;

    if (region.server_uri.empty()) {
        reason = "remote-region-server-uri-missing";
        return std::nullopt;
    }
    reason.clear();
    return region;
}

std::optional<std::unordered_map<std::string, std::string>> xmlrpc_request(
    std::string_view endpoint,
    std::string_view method,
    const std::unordered_map<std::string, std::string>& fields,
    std::string& reason) {
    const auto body = xmlrpc_struct_call(method, fields);
    const auto response = http_request(endpoint, "POST", "text/xml", body, {}, reason);
    if (!response) return std::nullopt;
    if (response->status != 200) {
        reason = "remote-http-" + std::to_string(response->status);
        return std::nullopt;
    }
    const auto parsed = parse_xmlrpc_struct_response(response->body);
    if (!parsed) {
        reason = "remote-xmlrpc-response-invalid";
        return std::nullopt;
    }
    reason.clear();
    return parsed;
}

std::optional<std::string> json_string(std::string_view json, std::string_view key) {
    const std::string needle = "\"" + std::string{key} + "\"";
    auto pos = json.find(needle);
    if (pos == std::string_view::npos) return std::nullopt;
    pos = json.find(':', pos + needle.size());
    if (pos == std::string_view::npos) return std::nullopt;
    ++pos;
    while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos]))) ++pos;
    if (pos >= json.size() || json[pos] != '"') return std::nullopt;
    ++pos;
    std::string out;
    while (pos < json.size()) {
        const char c = json[pos++];
        if (c == '"') return out;
        if (c == '\\' && pos < json.size()) {
            const char escaped = json[pos++];
            if (escaped == '"' || escaped == '\\' || escaped == '/') out.push_back(escaped);
            else if (escaped == 'n') out.push_back('\n');
            else if (escaped == 'r') out.push_back('\r');
            else if (escaped == 't') out.push_back('\t');
            else return std::nullopt;
        } else {
            out.push_back(c);
        }
    }
    return std::nullopt;
}

std::optional<bool> json_bool(std::string_view json, std::string_view key) {
    const std::string needle = "\"" + std::string{key} + "\"";
    auto pos = json.find(needle);
    if (pos == std::string_view::npos) return std::nullopt;
    pos = json.find(':', pos + needle.size());
    if (pos == std::string_view::npos) return std::nullopt;
    ++pos;
    while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos]))) ++pos;
    if (json.substr(pos, 4) == "true") return true;
    if (json.substr(pos, 5) == "false") return false;
    return std::nullopt;
}

} // namespace

std::uint32_t legacy_circuit_code(const std::string_view session_id) {
    const auto hash = security::sha256_hex(
        "OpenGenesisLINK-HG-circuit:" + std::string{session_id});
    std::uint32_t value = 0;
    const auto [end, ec] = std::from_chars(
        hash.data(), hash.data() + 8, value, 16);
    if (ec != std::errc{} || end != hash.data() + 8 || value == 0U) {
        return 1U;
    }
    return value;
}

std::optional<RemoteHypergridRegion> HypergridGatekeeperClient::link_region(
    const std::string_view gatekeeper_uri,
    const std::string_view region_name,
    std::string& reason) const {
    const auto fields = xmlrpc_request(
        gatekeeper_uri, "link_region",
        {{"region_name", std::string{region_name}}}, reason);
    if (!fields) return std::nullopt;

    auto region = region_from_fields(*fields, reason);
    if (!region) return std::nullopt;

    // link_region commonly omits coordinates/host. Resolve the returned UUID
    // through get_region before handing it to the caller.
    auto detailed = get_region(gatekeeper_uri, region->region_id, "", "", reason);
    if (detailed) return detailed;

    // Some older grids expose only the link response. Keep it usable when it
    // at least supplied a remote server URI.
    if (!region->server_uri.empty()) {
        reason.clear();
        return region;
    }
    return std::nullopt;
}

std::optional<RemoteHypergridRegion> HypergridGatekeeperClient::get_region(
    const std::string_view gatekeeper_uri,
    const std::string_view region_uuid,
    const std::string_view agent_id,
    const std::string_view home_uri,
    std::string& reason) const {
    std::unordered_map<std::string, std::string> args{
        {"region_uuid", std::string{region_uuid}}};
    if (!agent_id.empty()) args["agent_id"] = std::string{agent_id};
    if (!home_uri.empty()) args["agent_home_uri"] = std::string{home_uri};

    const auto fields = xmlrpc_request(gatekeeper_uri, "get_region", args, reason);
    if (!fields) return std::nullopt;
    return region_from_fields(*fields, reason);
}

std::string HypergridGatekeeperClient::build_agent_payload(
    const RemoteHypergridRegion& destination,
    const HypergridConfig& home,
    const OutboundAgentRequest& request) {
    const std::string zero = "00000000-0000-0000-0000-000000000000";
    std::ostringstream out;
    out << "{"
        << "\"agent_id\":\"" << json_escape(request.agent_id) << "\","
        << "\"base_folder\":\"" << zero << "\","
        << "\"caps_path\":\"" << json_escape(request.caps_path) << "\","
        << "\"child\":false,"
        << "\"circuit_code\":\"" << request.circuit_code << "\","
        << "\"first_name\":\"" << json_escape(request.first_name) << "\","
        << "\"last_name\":\"" << json_escape(request.last_name) << "\","
        << "\"inventory_folder\":\"" << zero << "\","
        << "\"secure_session_id\":\"" << json_escape(request.secure_session_id) << "\","
        << "\"session_id\":\"" << json_escape(request.session_id) << "\","
        << "\"service_session_id\":\"" << json_escape(request.service_session_id) << "\","
        << "\"start_pos\":\"<" << request.start_x << ", " << request.start_y << ", "
        << request.start_z << ">\","
        << "\"client_ip\":\"" << json_escape(request.client_ip) << "\","
        << "\"viewer\":\"OpenGenesisLINK Viewer\","
        << "\"channel\":\"OpenGenesisLINK\","
        << "\"mac\":\"\",\"id0\":\"\","
        << "\"destination_x\":\"" << destination.x << "\","
        << "\"destination_y\":\"" << destination.y << "\","
        << "\"destination_name\":\"" << json_escape(destination.name) << "\","
        << "\"destination_uuid\":\"" << json_escape(destination.region_id) << "\","
        << "\"teleport_flags\":\"" << request.teleport_flags << "\","
        << "\"context\":{},"
        << "\"serviceurls\":{"
        << "\"HomeURI\":\"" << json_escape(home.home_uri) << "\","
        << "\"AssetServerURI\":\"" << json_escape(home.asset_uri) << "\","
        << "\"InventoryServerURI\":\"" << json_escape(home.inventory_uri) << "\","
        << "\"AvatarServerURI\":\"" << json_escape(home.avatar_uri) << "\","
        << "\"FriendsServerURI\":\"" << json_escape(home.friends_uri) << "\","
        << "\"IMServerURI\":\"" << json_escape(home.im_uri) << "\""
        << "}}";
    return out.str();
}

OutboundAgentResult HypergridGatekeeperClient::create_agent(
    const RemoteHypergridRegion& destination,
    const HypergridConfig& home,
    const OutboundAgentRequest& request) const {
    OutboundAgentResult result;
    if (destination.server_uri.empty()) {
        result.reason = "remote-region-server-uri-missing";
        return result;
    }
    const auto endpoint = trim_slash(destination.server_uri) +
                          "/foreignagent/" + request.agent_id + "/";
    std::string reason;
    const auto response = http_request(
        endpoint, "POST", "application/json",
        build_agent_payload(destination, home, request), {}, reason);
    if (!response) {
        result.reason = reason;
        return result;
    }
    if (response->status != 200) {
        result.reason = "remote-http-" + std::to_string(response->status);
        return result;
    }
    result.success = json_bool(response->body, "success").value_or(false);
    result.reason = json_string(response->body, "reason").value_or(
        result.success ? "" : "remote-agent-rejected");
    result.caller_ip = json_string(response->body, "your_ip").value_or("");
    return result;
}

} // namespace opengenesis::compat::hypergrid
