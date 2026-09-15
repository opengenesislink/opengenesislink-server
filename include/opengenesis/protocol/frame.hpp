#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>
namespace opengenesis::protocol {
inline constexpr std::array<std::byte, 4> kMagic{std::byte{'O'},std::byte{'G'},std::byte{'L'},std::byte{'1'}};
inline constexpr std::uint16_t kProtocolVersion = 1;
inline constexpr std::size_t kHeaderSize = 16;
inline constexpr std::uint32_t kMaxPayloadSize = 1024U * 1024U;
enum class MessageType : std::uint16_t {
    hello=1, hello_ack=2,
    world_register=10, world_register_ack=11, world_lease=12, world_lease_ack=13,
    region_register=30, region_register_ack=31, region_state_update=32, region_state_ack=33,
    region_metrics=34, region_metrics_ack=35,
    ping=40, pong=41, goodbye=42, error=255
};
struct Frame { MessageType type{MessageType::error}; std::uint32_t request_id{0}; std::vector<std::byte> payload; };
[[nodiscard]] std::vector<std::byte> encode(const Frame& frame);
[[nodiscard]] Frame decode(std::span<const std::byte> bytes);
[[nodiscard]] std::string payload_as_string(const Frame& frame);
[[nodiscard]] std::vector<std::byte> payload_from_string(std::string_view value);
}
