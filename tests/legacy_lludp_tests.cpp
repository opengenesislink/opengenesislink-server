#include "opengenesis/compat/hypergrid/legacy_lludp.hpp"

#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void require(bool value, const std::string& message) {
    if (!value) throw std::runtime_error(message);
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
    require(bytes.has_value(), "test UUID encodes");
    out.insert(out.end(), bytes->begin(), bytes->end());
}
}

int main() {
    try {
        using namespace opengenesis::compat::hypergrid::lludp;
        constexpr std::string_view session =
            "22222222-2222-4222-8222-222222222222";
        constexpr std::string_view agent =
            "11111111-1111-4111-8111-111111111111";

        std::vector<std::uint8_t> use{
            0x40U, 0x00U, 0x00U, 0x00U, 0x2aU, 0x00U,
            0xffU, 0xffU, 0x00U, 0x03U};
        append_u32_le(use, 0x01020304U);
        append_uuid(use, session);
        append_uuid(use, agent);

        std::string reason;
        const auto header = parse_header(use, reason);
        require(header && header->reliable &&
                    header->frequency == Frequency::low &&
                    header->id == 3U &&
                    header->sequence == 42U &&
                    header->body_offset == 10U,
                "UseCircuitCode header parsed");

        const auto circuit = parse_use_circuit_code(use, reason);
        require(circuit &&
                    circuit->circuit_code == 0x01020304U &&
                    circuit->session_id == session &&
                    circuit->agent_id == agent,
                "UseCircuitCode body parsed");

        std::vector<std::uint8_t> complete{
            0x40U, 0x00U, 0x00U, 0x00U, 0x2bU, 0x00U,
            0xffU, 0xffU, 0x00U, 0xf9U};
        append_uuid(complete, agent);
        append_uuid(complete, session);
        append_u32_le(complete, 0x01020304U);
        const auto movement =
            parse_complete_agent_movement(complete, reason);
        require(movement &&
                    movement->agent_id == agent &&
                    movement->session_id == session &&
                    movement->circuit_code == 0x01020304U,
                "CompleteAgentMovement parsed");

        const auto ack = build_packet_ack(42U);
        const auto ack_header = parse_header(ack, reason);
        require(ack_header &&
                    ack_header->frequency == Frequency::low &&
                    ack_header->id == 0xfffbU &&
                    ack.size() == 15U &&
                    ack[10] == 1U &&
                    ack[11] == 42U,
                "PacketAck generated");

        const auto ping = build_complete_ping_check(9U, 7U);
        const auto ping_header = parse_header(ping, reason);
        require(ping_header &&
                    ping_header->frequency == Frequency::high &&
                    ping_header->id == 2U &&
                    ping.back() == 7U,
                "CompletePingCheck generated");

        const auto handshake = build_region_handshake(
            10U,
            {.region_id = "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa",
             .region_name = "OGL Welcome",
             .region_handle = 0x0003e8000003e800ULL,
             .water_height = 20.0F,
             .sim_access = 21U});
        require(!handshake.empty() && (handshake[0] & 0x80U) != 0U,
                "RegionHandshake generated as zerocoded packet");
        const auto decoded_handshake = zero_decode(handshake, reason);
        require(decoded_handshake.has_value(),
                "RegionHandshake zero decoding succeeds");
        const auto handshake_header =
            parse_header(*decoded_handshake, reason);
        require(handshake_header &&
                    handshake_header->frequency == Frequency::low &&
                    handshake_header->id == 148U &&
                    handshake_header->reliable,
                "RegionHandshake packet identity retained");

        const auto movement_complete = build_agent_movement_complete(
            11U,
            {.agent_id = std::string{agent},
             .session_id = std::string{session},
             .x = 128.0F,
             .y = 129.0F,
             .z = 25.0F,
             .region_handle = 0x0003e8000003e800ULL,
             .timestamp = 1234U});
        const auto movement_complete_header =
            parse_header(movement_complete, reason);
        require(movement_complete_header &&
                    movement_complete_header->frequency ==
                        Frequency::low &&
                    movement_complete_header->id == 250U,
                "AgentMovementComplete generated");

        std::vector<std::uint8_t> zeros{
            0x80U, 0U, 0U, 0U, 1U, 0U, 4U, 0U, 0U, 0U, 5U};
        const auto encoded = zero_encode(zeros);
        require(encoded.size() < zeros.size() &&
                    encoded[6] == 4U &&
                    encoded[7] == 0U &&
                    encoded[8] == 3U &&
                    encoded[9] == 5U,
                "LLUDP zero encoding after six-byte header");

        std::cout << "OpenGenesisLINK legacy LLUDP codec tests: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "OpenGenesisLINK legacy LLUDP codec failure: "
                  << error.what() << '\n';
        return 1;
    }
}
