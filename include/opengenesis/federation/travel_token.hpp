#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace opengenesis::federation {

inline constexpr std::string_view kFederationProtocol = "OGL-FED/1";

struct TravelTokenClaims {
    std::string issuer_grid;
    std::string audience_grid;
    std::string subject_user;
    std::string display_name;
    std::string origin_region;
    std::string destination_region;
    std::string session_id;
    std::string nonce;
    std::int64_t issued_unix{0};
    std::int64_t expires_unix{0};
};

struct IssuedTravelToken {
    std::string token;
    TravelTokenClaims claims;
};

[[nodiscard]] IssuedTravelToken issue_travel_token(
    std::string_view private_key_hex,
    TravelTokenClaims claims,
    std::chrono::seconds lifetime = std::chrono::seconds{120});

[[nodiscard]] std::optional<TravelTokenClaims> verify_travel_token(
    std::string_view public_key_hex,
    std::string_view token,
    std::string_view expected_audience,
    std::string_view expected_issuer = {});

} // namespace opengenesis::federation
