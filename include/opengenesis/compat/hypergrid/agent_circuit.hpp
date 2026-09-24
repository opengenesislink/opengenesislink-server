#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace opengenesis::compat::hypergrid {

struct ForeignAgentCircuit {
    std::string agent_id;
    std::string session_id;
    std::string secure_session_id;
    std::string service_session_id;
    std::string caps_path;
    std::string base_folder;
    std::string inventory_folder;
    std::string first_name;
    std::string last_name;
    std::string home_uri;
    std::string asset_uri;
    std::string inventory_uri;
    std::string avatar_uri;
    std::string im_uri;
    std::string client_ip;
    std::string viewer;
    std::string channel;
    std::string mac;
    std::string id0;
    std::string start_pos;
    bool child{false};
    std::uint32_t circuit_code{0};
    std::string destination_uuid;
    std::string destination_name;
    std::int32_t destination_x{0};
    std::int32_t destination_y{0};
    std::uint32_t teleport_flags{0};
};

[[nodiscard]] std::optional<ForeignAgentCircuit> parse_foreign_agent_circuit(
    std::string_view json,
    std::string& reason);

[[nodiscard]] bool service_token_targets(std::string_view service_token,
                                         std::string_view gatekeeper_uri);
[[nodiscard]] std::string service_token_destination(std::string_view service_token);

} // namespace opengenesis::compat::hypergrid
