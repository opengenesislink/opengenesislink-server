#include "opengenesis/compat/hypergrid/gatekeeper_client.hpp"
#include "opengenesis/compat/hypergrid/session_store.hpp"

#include <iostream>
#include <stdexcept>
#include <string>

namespace {
void require(bool value, const std::string& message) {
    if (!value) throw std::runtime_error(message);
}
}

int main() {
    try {
        using namespace opengenesis::compat::hypergrid;
        RemoteHypergridRegion region{
            .region_id = "11111111-1111-4111-8111-111111111111",
            .name = "Remote Welcome",
            .server_uri = "https://remote.example:8002",
            .host = "remote.example",
            .http_port = 9000,
            .internal_port = 9000,
            .x = 1000,
            .y = 1001,
            .size_x = 256,
            .size_y = 256};
        HypergridConfig home{
            .enabled = true,
            .external_name = "https://ogl.example:8002",
            .home_uri = "https://ogl.example:8002",
            .asset_uri = "https://ogl.example:8002",
            .inventory_uri = "https://ogl.example:8002",
            .avatar_uri = "https://ogl.example:8002",
            .friends_uri = "https://ogl.example:8002",
            .im_uri = "https://ogl.example:8002"};
        OutboundAgentRequest request{
            .agent_id = "22222222-2222-4222-8222-222222222222",
            .session_id = "33333333-3333-4333-8333-333333333333",
            .secure_session_id = "44444444-4444-4444-8444-444444444444",
            .service_session_id = "https://remote.example:8002;55555555-5555-4555-8555-555555555555",
            .first_name = "OGL",
            .last_name = "Resident",
            .client_ip = "192.0.2.40",
            .caps_path = "CAPS/test/",
            .circuit_code = 123456U,
            .start_x = 128.0,
            .start_y = 129.0,
            .start_z = 25.0,
            .teleport_flags = 8U};

        const auto body = HypergridGatekeeperClient::build_agent_payload(region, home, request);
        require(body.find("\"agent_id\":\"22222222-2222-4222-8222-222222222222\"") != std::string::npos,
                "agent id packed");
        require(body.find("\"HomeURI\":\"https://ogl.example:8002\"") != std::string::npos,
                "home uri packed");
        require(body.find("\"destination_uuid\":\"11111111-1111-4111-8111-111111111111\"") != std::string::npos,
                "destination packed");
        require(body.find("\"start_pos\":\"<128, 129, 25>\"") != std::string::npos,
                "start position packed");
        require(legacy_circuit_code(request.session_id) != 0U,
                "deterministic non-zero circuit code");

        std::cout << "OpenGenesisLINK Hypergrid outbound tests: PASS\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "OpenGenesisLINK Hypergrid outbound test failure: " << e.what() << '\n';
        return 1;
    }
}
