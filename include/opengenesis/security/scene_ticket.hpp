#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace opengenesis::security {

inline constexpr std::string_view kDefaultSceneCapabilities =
    "scene.join,scene.read,scene.move,scene.chat,scene.object.create,"
    "scene.object.modify.own,scene.object.permissions,scene.object.link,"
    "scene.terrain.sample,scene.terrain.modify,scene.sync,"
    "scene.avatar.reconcile,scene.region.metadata,scene.parcel.read";

struct SceneTicketClaims {
    std::string user_id;
    std::string display_name;
    std::string region_id;
    std::string nonce;
    std::string capabilities;
    std::string handoff_from_region;
    std::string crossing_id;
    std::string group_ids_csv;
    double spawn_x{128.0};
    double spawn_y{128.0};
    double spawn_z{0.0};
    std::int64_t issued_unix{0};
    std::int64_t expires_unix{0};
};

struct IssuedSceneTicket {
    std::string token;
    SceneTicketClaims claims;
};

[[nodiscard]] IssuedSceneTicket issue_scene_ticket(
    std::string_view secret,
    std::string user_id,
    std::string display_name,
    std::string region_id,
    std::chrono::seconds lifetime,
    std::string capabilities = std::string{kDefaultSceneCapabilities},
    std::string handoff_from_region = {},
    std::string group_ids_csv = {},
    double spawn_x = 128.0,
    double spawn_y = 128.0,
    double spawn_z = 0.0,
    std::string crossing_id = {});

[[nodiscard]] std::optional<SceneTicketClaims> verify_scene_ticket(
    std::string_view secret,
    std::string_view token,
    std::string_view expected_region = {});

[[nodiscard]] bool has_scene_capability(const SceneTicketClaims& claims,
                                        std::string_view capability);

[[nodiscard]] bool scene_ticket_has_group(const SceneTicketClaims& claims, std::string_view group_id);

} // namespace opengenesis::security
