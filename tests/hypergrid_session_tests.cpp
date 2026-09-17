#include "opengenesis/compat/hypergrid/agent_circuit.hpp"
#include "opengenesis/compat/hypergrid/session_store.hpp"
#include "opengenesis/compat/hypergrid/xmlrpc.hpp"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void require(const bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

std::filesystem::path temp_path() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
           ("ogl-hg35-" + std::to_string(stamp) + ".db");
}

} // namespace

int main() {
    try {
        const auto path = temp_path();
        opengenesis::compat::hypergrid::HypergridSessionStore sessions(path.string());

        const auto legacy_user =
            opengenesis::compat::hypergrid::legacy_uuid_from_seed("native-user-1");
        require(legacy_user.size() == 36, "legacy user UUID generated");

        const auto travel = sessions.issue_home_travel(
            "native-user-1", legacy_user, "http://remote.example:8002",
            "192.0.2.10", std::chrono::seconds{600});
        require(travel.session_id.size() == 36, "HG session UUID generated");
        require(travel.service_token.starts_with("http://remote.example:8002;"),
                "service token bound to destination");
        require(sessions.verify_agent(travel.session_id, travel.service_token),
                "verify_agent accepts exact token");
        require(!sessions.verify_agent(travel.session_id, travel.service_token + "x"),
                "verify_agent rejects wrong token");
        require(sessions.verify_client(travel.session_id, "192.0.2.10"),
                "verify_client accepts original IP");
        require(!sessions.verify_client(travel.session_id, "192.0.2.11"),
                "verify_client rejects other IP");

        require(opengenesis::compat::hypergrid::service_token_targets(
                    travel.service_token, "http://remote.example:8002/"),
                "service token destination match");
        require(!opengenesis::compat::hypergrid::service_token_targets(
                    travel.service_token, "http://other.example:8002"),
                "service token destination mismatch");

        const std::string circuit_json =
            "{"
            "\"agent_id\":\"11111111-1111-4111-8111-111111111111\","
            "\"session_id\":\"22222222-2222-4222-8222-222222222222\","
            "\"service_session_id\":\"http://grid-b.example:8002;33333333-3333-4333-8333-333333333333\","
            "\"first_name\":\"Alice\",\"last_name\":\"Visitor\","
            "\"client_ip\":\"192.0.2.20\",\"viewer\":\"TestViewer\","
            "\"channel\":\"OpenGenesis\",\"mac\":\"mac\",\"id0\":\"id0\","
            "\"destination_uuid\":\"44444444-4444-4444-8444-444444444444\","
            "\"destination_name\":\"Welcome\","
            "\"destination_x\":1000,\"destination_y\":1001,\"teleport_flags\":8,"
            "\"serviceurls\":{\"HomeURI\":\"http://grid-a.example:8002\"}"
            "}";
        std::string reason;
        const auto circuit =
            opengenesis::compat::hypergrid::parse_foreign_agent_circuit(circuit_json, reason);
        require(circuit.has_value(), "foreign agent circuit parsed");
        require(circuit->home_uri == "http://grid-a.example:8002", "HomeURI parsed");
        require(circuit->destination_x == 1000 && circuit->destination_y == 1001,
                "destination coordinates parsed");

        const auto now = std::chrono::duration_cast<std::chrono::seconds>(
                             std::chrono::system_clock::now().time_since_epoch())
                             .count();
        require(sessions.upsert_foreign(
                    {.session_id = circuit->session_id,
                     .agent_id = circuit->agent_id,
                     .home_uri = circuit->home_uri,
                     .service_token = circuit->service_session_id,
                     .destination_region = "native-region",
                     .first_name = circuit->first_name,
                     .last_name = circuit->last_name,
                     .client_ip = circuit->client_ip,
                     .verified = true,
                     .created_unix = now,
                     .expires_unix = now + 600},
                    reason),
                "verified foreign visitor persisted");

        {
            opengenesis::compat::hypergrid::HypergridSessionStore restored(path.string());
            require(restored.verify_agent(travel.session_id, travel.service_token),
                    "home travel persists");
            const auto foreign = restored.foreign(circuit->session_id);
            require(foreign && foreign->verified &&
                        foreign->destination_region == "native-region",
                    "foreign visitor persists");
        }

        const auto call = opengenesis::compat::hypergrid::xmlrpc_struct_call(
            "verify_agent", {{"sessionID", travel.session_id},
                             {"token", travel.service_token}});
        const auto parsed = opengenesis::compat::hypergrid::parse_xmlrpc_call(call);
        require(parsed && parsed->method == "verify_agent", "verify_agent XML-RPC roundtrip");

        const auto response = opengenesis::compat::hypergrid::xmlrpc_struct_response(
            {{"result", "True"}});
        const auto response_fields =
            opengenesis::compat::hypergrid::parse_xmlrpc_struct_response(response);
        require(response_fields && response_fields->at("result") == "True",
                "XML-RPC response roundtrip");

        require(sessions.logout_home(legacy_user, travel.session_id),
                "home travel logout");
        require(!sessions.verify_agent(travel.session_id, travel.service_token),
                "logged-out travel no longer verifies");

        std::filesystem::remove(path);
        std::cout << "OpenGenesisLINK 3.5 Hypergrid session tests: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "OpenGenesisLINK 3.5 Hypergrid test failure: "
                  << error.what() << '\n';
        return 1;
    }
}
