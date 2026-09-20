#pragma once

#include "opengenesis/federation/travel_token.hpp"

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace opengenesis::federation {

enum class ForeignSessionState {
    active,
    logged_out,
    expired
};

struct ForeignSession {
    std::string id;
    std::string issuer_grid;
    std::string subject_user;
    std::string display_name;
    std::string origin_region;
    std::string destination_region;
    std::string remote_session_id;
    std::string home_url;
    std::string service_grant_id;
    std::string service_token;
    std::string service_capabilities;
    ForeignSessionState state{ForeignSessionState::active};
    std::int64_t created_unix{0};
    std::int64_t expires_unix{0};
    std::int64_t ended_unix{0};
};

class FederationSessionStore final {
public:
    explicit FederationSessionStore(std::string path);

    [[nodiscard]] ForeignSession create(const TravelTokenClaims& claims);
    [[nodiscard]] std::optional<ForeignSession> find(std::string_view id) const;
    [[nodiscard]] std::vector<ForeignSession> list() const;
    [[nodiscard]] bool logout(std::string_view id);
    std::size_t logout_issuer(std::string_view issuer_grid);
    std::size_t purge_expired(std::int64_t now_unix);
    [[nodiscard]] std::size_t active_count() const;

private:
    void load();
    void persist_locked() const;

    std::string path_;
    mutable std::mutex mutex_;
    std::unordered_map<std::string, ForeignSession> sessions_;
};

[[nodiscard]] std::string_view foreign_session_state_name(ForeignSessionState state) noexcept;

} // namespace opengenesis::federation
