#pragma once

#include "opengenesis/federation/grid_identity_store.hpp"
#include "opengenesis/federation/replay_cache.hpp"
#include "opengenesis/federation/session_store.hpp"
#include "opengenesis/federation/service_grant_store.hpp"
#include "opengenesis/federation/trust_store.hpp"
#include "opengenesis/federation/travel_token.hpp"

#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace opengenesis::federation {

struct FederationInfo {
    std::string protocol;
    std::string grid_id;
    std::string base_url;
    std::string public_key_hex;
};

struct OutboundTravelRequest {
    std::string subject_user;
    std::string display_name;
    std::string audience_grid;
    std::string origin_region;
    std::string destination_region;
    std::chrono::seconds lifetime{120};
    std::string service_capabilities{
        kDefaultFederationServiceCapabilities};
};

struct AcceptedTravel {
    TravelTokenClaims claims;
    ForeignSession session;
};

class FederationRuntime final {
public:
    FederationRuntime(std::shared_ptr<GridIdentityStore> identity,
                      std::shared_ptr<FederationTrustStore> trust,
                      std::shared_ptr<FederationSessionStore> sessions,
                      std::shared_ptr<FederationServiceGrantStore> grants);

    [[nodiscard]] FederationInfo info() const;
    [[nodiscard]] std::optional<IssuedTravelToken> issue_travel(
        const OutboundTravelRequest& request,
        std::string& reason) const;
    [[nodiscard]] std::optional<AcceptedTravel> accept_travel(
        std::string_view issuer_grid,
        std::string_view token,
        std::string& reason);

    [[nodiscard]] bool trust_peer(FederationPeer peer, std::string& reason);
    [[nodiscard]] bool revoke_peer(std::string_view grid_id);
    [[nodiscard]] std::vector<FederationPeer> peers() const;
    [[nodiscard]] std::vector<ForeignSession> sessions() const;
    [[nodiscard]] bool logout_session(std::string_view session_id);

    [[nodiscard]] bool authorize_remote_service(
        std::string_view grant_id,
        std::string_view service_token,
        std::string_view audience_grid,
        std::string_view subject_user,
        std::string_view capability) const;
    [[nodiscard]] std::vector<FederationServiceGrant>
    service_grants() const;
    [[nodiscard]] bool revoke_service_grant(
        std::string_view grant_id);

    std::size_t maintenance(std::int64_t now_unix);

private:
    std::shared_ptr<GridIdentityStore> identity_;
    std::shared_ptr<FederationTrustStore> trust_;
    std::shared_ptr<FederationSessionStore> sessions_;
    std::shared_ptr<FederationServiceGrantStore> grants_;
    TravelReplayCache replay_;
};

} // namespace opengenesis::federation
