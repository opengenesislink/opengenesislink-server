#pragma once

#include <chrono>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace opengenesis::federation {

inline constexpr std::string_view kDefaultFederationServiceCapabilities =
    "profile,appearance,inventory,assets,social,presence";

struct FederationServiceGrant {
    std::string id;
    std::string audience_grid;
    std::string subject_user;
    std::string token_hash;
    std::string capabilities;
    std::int64_t created_unix{0};
    std::int64_t expires_unix{0};
    bool revoked{false};
    std::int64_t revoked_unix{0};
};

struct IssuedFederationServiceGrant {
    FederationServiceGrant grant;
    std::string token;
};

class FederationServiceGrantStore final {
public:
    explicit FederationServiceGrantStore(std::string path);

    [[nodiscard]] IssuedFederationServiceGrant issue(
        std::string audience_grid,
        std::string subject_user,
        std::chrono::seconds lifetime,
        std::string capabilities =
            std::string{kDefaultFederationServiceCapabilities});

    [[nodiscard]] bool authorize(
        std::string_view grant_id,
        std::string_view token,
        std::string_view audience_grid,
        std::string_view subject_user,
        std::string_view capability) const;

    [[nodiscard]] bool revoke(std::string_view grant_id);
    std::size_t revoke_audience(std::string_view audience_grid);
    std::size_t purge_expired(std::int64_t now_unix);

    [[nodiscard]] std::optional<FederationServiceGrant> find(
        std::string_view grant_id) const;
    [[nodiscard]] std::vector<FederationServiceGrant> list() const;

private:
    void load();
    void persist_locked() const;

    std::string path_;
    mutable std::mutex mutex_;
    std::unordered_map<std::string, FederationServiceGrant> grants_;
};

[[nodiscard]] bool federation_service_capability(
    std::string_view capabilities,
    std::string_view capability);

} // namespace opengenesis::federation
