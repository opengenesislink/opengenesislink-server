#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace opengenesis::security {

struct SceneTicketClaims {
    std::string user_id;
    std::string display_name;
    std::string region_id;
    std::string nonce;
    std::int64_t issued_unix{0};
    std::int64_t expires_unix{0};
};

struct IssuedSceneTicket {
    std::string token;
    SceneTicketClaims claims;
};

[[nodiscard]] IssuedSceneTicket issue_scene_ticket(std::string_view secret,
                                                    std::string user_id,
                                                    std::string display_name,
                                                    std::string region_id,
                                                    std::chrono::seconds lifetime);
[[nodiscard]] std::optional<SceneTicketClaims> verify_scene_ticket(std::string_view secret,
                                                                   std::string_view token,
                                                                   std::string_view expected_region = {});

} // namespace opengenesis::security
