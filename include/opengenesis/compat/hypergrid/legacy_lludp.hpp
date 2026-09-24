#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace opengenesis::compat::hypergrid::lludp {

enum class Frequency : std::uint8_t {
    high,
    medium,
    low
};

struct PacketHeader {
    bool appended_acks{false};
    bool reliable{false};
    bool resent{false};
    bool zerocoded{false};
    std::uint32_t sequence{0};
    Frequency frequency{Frequency::high};
    std::uint16_t id{0};
    std::size_t body_offset{0};
    std::size_t body_end{0};
};

struct CircuitHandshake {
    std::uint32_t circuit_code{0};
    std::string session_id;
    std::string agent_id;
};

[[nodiscard]] std::optional<PacketHeader> parse_header(
    std::span<const std::uint8_t> packet,
    std::string& reason);

[[nodiscard]] std::optional<CircuitHandshake> parse_use_circuit_code(
    std::span<const std::uint8_t> packet,
    std::string& reason);

[[nodiscard]] std::optional<CircuitHandshake> parse_complete_agent_movement(
    std::span<const std::uint8_t> packet,
    std::string& reason);

[[nodiscard]] std::vector<std::uint8_t> build_packet_ack(
    std::uint32_t sequence);

[[nodiscard]] std::vector<std::uint8_t> build_complete_ping_check(
    std::uint32_t sequence,
    std::uint8_t ping_id);

[[nodiscard]] std::vector<std::uint8_t> zero_encode(
    std::span<const std::uint8_t> packet);

[[nodiscard]] std::optional<std::string> uuid_from_network_bytes(
    std::span<const std::uint8_t> bytes);

[[nodiscard]] std::optional<std::array<std::uint8_t, 16>> uuid_to_network_bytes(
    std::string_view uuid);

} // namespace opengenesis::compat::hypergrid::lludp
