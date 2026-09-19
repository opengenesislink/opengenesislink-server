#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace opengenesis::protocol {

inline constexpr std::array<std::byte, 4> kMagic{std::byte{'O'}, std::byte{'G'}, std::byte{'L'},
                                                 std::byte{'1'}};
inline constexpr std::uint16_t kProtocolVersion = 1;
inline constexpr std::size_t kHeaderSize = 16;
inline constexpr std::uint32_t kMaxPayloadSize = 1024U * 1024U;

enum class MessageType : std::uint16_t {
    hello = 1,
    hello_ack = 2,

    world_register = 10,
    world_register_ack = 11,
    world_lease = 12,
    world_lease_ack = 13,

    region_register = 30,
    region_register_ack = 31,
    region_state_update = 32,
    region_state_ack = 33,
    region_metrics = 34,
    region_metrics_ack = 35,
    presence_snapshot = 36,
    presence_snapshot_ack = 37,
    script_action_poll = 38,
    script_action = 39,

    ping = 40,
    pong = 41,
    goodbye = 42,
    script_action_result = 43,
    script_action_result_ack = 44,

    scene_join = 100,
    scene_join_ack = 101,
    scene_snapshot_request = 102,
    scene_snapshot = 103,
    entity_create = 110,
    entity_create_ack = 111,
    entity_update = 112,
    entity_update_ack = 113,
    entity_delete = 114,
    entity_delete_ack = 115,
    entity_permissions = 116,
    entity_permissions_ack = 117,
    chat_send = 120,
    chat_event = 121,
    scene_events_request = 130,
    scene_events = 131,
    terrain_sample_request = 140,
    terrain_sample = 141,
    terrain_set_request = 142,
    terrain_set_ack = 143,
    avatar_move = 150,
    avatar_move_ack = 151,

    error = 255
};

struct Frame {
    MessageType type{MessageType::error};
    std::uint32_t request_id{0};
    std::vector<std::byte> payload;
};

[[nodiscard]] std::vector<std::byte> encode(const Frame& frame);
[[nodiscard]] Frame decode(std::span<const std::byte> bytes);
[[nodiscard]] std::string payload_as_string(const Frame& frame);
[[nodiscard]] std::vector<std::byte> payload_from_string(std::string_view value);

} // namespace opengenesis::protocol
