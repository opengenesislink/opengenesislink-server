#include "opengenesis/compat/hypergrid/legacy_simulator_gateway.hpp"
#include "opengenesis/core/region_registry.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void require(bool value, const std::string& message) {
    if (!value) throw std::runtime_error(message);
}

std::filesystem::path temp_path(const char* stem) {
    const auto stamp =
        std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
           (std::string{stem} + "-" + std::to_string(stamp) + ".db");
}

void append_u32_le(std::vector<std::uint8_t>& out, std::uint32_t value) {
    out.push_back(static_cast<std::uint8_t>(value & 0xffU));
    out.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xffU));
    out.push_back(static_cast<std::uint8_t>((value >> 16U) & 0xffU));
    out.push_back(static_cast<std::uint8_t>((value >> 24U) & 0xffU));
}

void append_uuid(std::vector<std::uint8_t>& out, std::string_view uuid) {
    const auto bytes =
        opengenesis::compat::hypergrid::lludp::uuid_to_network_bytes(uuid);
    require(bytes.has_value(), "UUID encodes");
    out.insert(out.end(), bytes->begin(), bytes->end());
}

std::vector<std::uint8_t> use_circuit(
    std::uint32_t sequence,
    std::uint32_t circuit,
    std::string_view session,
    std::string_view agent) {
    std::vector<std::uint8_t> packet{
        0x40U,
        static_cast<std::uint8_t>((sequence >> 24U) & 0xffU),
        static_cast<std::uint8_t>((sequence >> 16U) & 0xffU),
        static_cast<std::uint8_t>((sequence >> 8U) & 0xffU),
        static_cast<std::uint8_t>(sequence & 0xffU),
        0x00U, 0xffU, 0xffU, 0x00U, 0x03U};
    append_u32_le(packet, circuit);
    append_uuid(packet, session);
    append_uuid(packet, agent);
    return packet;
}

std::vector<std::uint8_t> complete_movement(
    std::uint32_t sequence,
    std::uint32_t circuit,
    std::string_view session,
    std::string_view agent) {
    std::vector<std::uint8_t> packet{
        0x40U,
        static_cast<std::uint8_t>((sequence >> 24U) & 0xffU),
        static_cast<std::uint8_t>((sequence >> 16U) & 0xffU),
        static_cast<std::uint8_t>((sequence >> 8U) & 0xffU),
        static_cast<std::uint8_t>(sequence & 0xffU),
        0x00U, 0xffU, 0xffU, 0x00U, 0xf9U};
    append_uuid(packet, agent);
    append_uuid(packet, session);
    append_u32_le(packet, circuit);
    return packet;
}
}

int main() {
    try {
        using namespace opengenesis::compat::hypergrid;

        const auto region_path = temp_path("ogl-hg-legacy-region");
        const auto session_path = temp_path("ogl-hg-legacy-session");
        auto regions =
            std::make_shared<opengenesis::core::RegionRegistry>(
                region_path.string());
        std::string reason;
        require(regions->register_region(
                    {.id = "native-region",
                     .name = "Native Region",
                     .node_id = "node-1",
                     .state = "online",
                     .grid_x = 1000,
                     .grid_y = 1000,
                     .node_generation = 1},
                    reason),
                "region registered");

        HypergridConfig config{
            .enabled = true,
            .external_name = "https://ogl.example:8002",
            .home_uri = "https://ogl.example:8002",
            .region_host = "ogl.example",
            .http_port = 19100,
            .internal_port = 19100};
        auto service =
            std::make_shared<HypergridService>(config, regions);
        auto sessions =
            std::make_shared<HypergridSessionStore>(
                session_path.string());

        constexpr std::string_view session =
            "22222222-2222-4222-8222-222222222222";
        constexpr std::string_view agent =
            "11111111-1111-4111-8111-111111111111";
        constexpr std::uint32_t circuit = 0x01020304U;
        const auto now =
            std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch())
                .count();
        require(sessions->upsert_foreign(
                    {.session_id = std::string{session},
                     .agent_id = std::string{agent},
                     .home_uri = "https://home.example:8002",
                     .service_token = "token",
                     .destination_region = "native-region",
                     .first_name = "Remote",
                     .last_name = "Resident",
                     .client_ip = "192.0.2.30",
                     .circuit_code = circuit,
                     .verified = true,
                     .created_unix = now,
                     .expires_unix = now + 600},
                    reason),
                "foreign session prepared");

        LegacyCircuitRouter router{service, sessions};
        auto use = use_circuit(42U, circuit, session, agent);
        auto result = router.handle(use, "192.0.2.30", 50000U);
        require(result.kind == LegacyDatagramKind::use_circuit &&
                    result.session_id == session &&
                    result.replies.size() == 1U,
                "UseCircuitCode binds verified circuit and ACKs");

        auto bindings = router.bindings();
        require(bindings.size() == 1U &&
                    bindings[0].state ==
                        LegacyCircuitState::circuit_bound,
                "circuit binding recorded");

        auto complete =
            complete_movement(43U, circuit, session, agent);
        result = router.handle(
            complete, "192.0.2.30", 50000U);
        require(result.kind ==
                    LegacyDatagramKind::complete_movement &&
                    result.replies.size() == 1U,
                "CompleteAgentMovement validates and ACKs");
        bindings = router.bindings();
        require(bindings[0].state ==
                    LegacyCircuitState::movement_completed,
                "movement completion state recorded");

        std::vector<std::uint8_t> ping{
            0x00U, 0U, 0U, 0U, 44U, 0U, 1U, 7U, 0U, 0U, 0U, 0U};
        result = router.handle(
            ping, "192.0.2.30", 50000U);
        require(result.kind == LegacyDatagramKind::ping &&
                    result.replies.size() == 1U,
                "bound circuit answers ping");

        auto bad = use_circuit(
            45U, circuit + 1U, session, agent);
        result = router.handle(
            bad, "192.0.2.31", 50001U);
        require(result.kind == LegacyDatagramKind::rejected &&
                    result.reason ==
                        "legacy-circuit-not-authorized",
                "wrong circuit code rejected");

        router.remove_session(session);
        require(router.bindings().empty(),
                "circuit cleanup removes endpoint binding");

        std::filesystem::remove(region_path);
        std::filesystem::remove(session_path);
        std::cout
            << "OpenGenesisLINK legacy simulator gateway tests: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr
            << "OpenGenesisLINK legacy simulator gateway failure: "
            << error.what() << '\n';
        return 1;
    }
}
