#pragma once

#include "opengenesis/compat/hypergrid/service.hpp"
#include "opengenesis/compat/hypergrid/session_store.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace opengenesis::compat::hypergrid {

struct RemoteHypergridRegion {
    std::string region_id;
    std::string name;
    std::string server_uri;
    std::string host;
    std::uint16_t http_port{0};
    std::uint16_t internal_port{0};
    std::int32_t x{0};
    std::int32_t y{0};
    std::uint16_t size_x{256};
    std::uint16_t size_y{256};
};

struct OutboundAgentRequest {
    std::string agent_id;
    std::string session_id;
    std::string secure_session_id;
    std::string service_session_id;
    std::string first_name;
    std::string last_name;
    std::string client_ip;
    std::string caps_path;
    std::uint32_t circuit_code{0};
    double start_x{128.0};
    double start_y{128.0};
    double start_z{25.0};
    std::uint32_t teleport_flags{0};
};

struct OutboundAgentResult {
    bool success{false};
    std::string reason;
    std::string caller_ip;
};

class HypergridGatekeeperClient final {
public:
    [[nodiscard]] std::optional<RemoteHypergridRegion> link_region(
        std::string_view gatekeeper_uri,
        std::string_view region_name,
        std::string& reason) const;

    [[nodiscard]] std::optional<RemoteHypergridRegion> get_region(
        std::string_view gatekeeper_uri,
        std::string_view region_uuid,
        std::string_view agent_id,
        std::string_view home_uri,
        std::string& reason) const;

    [[nodiscard]] OutboundAgentResult create_agent(
        const RemoteHypergridRegion& destination,
        const HypergridConfig& home,
        const OutboundAgentRequest& request) const;

    [[nodiscard]] static std::string build_agent_payload(
        const RemoteHypergridRegion& destination,
        const HypergridConfig& home,
        const OutboundAgentRequest& request);
};

[[nodiscard]] std::uint32_t legacy_circuit_code(std::string_view session_id);

} // namespace opengenesis::compat::hypergrid
