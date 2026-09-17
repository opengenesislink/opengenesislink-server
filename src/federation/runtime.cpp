#include "opengenesis/federation/runtime.hpp"

#include "opengenesis/security/crypto.hpp"

#include <algorithm>

namespace opengenesis::federation {

FederationRuntime::FederationRuntime(std::shared_ptr<GridIdentityStore> identity,
                                     std::shared_ptr<FederationTrustStore> trust,
                                     std::shared_ptr<FederationSessionStore> sessions)
    : identity_(std::move(identity)),
      trust_(std::move(trust)),
      sessions_(std::move(sessions)) {
    if (!identity_ || !trust_ || !sessions_) {
        throw std::invalid_argument("federation runtime dependencies required");
    }
}

FederationInfo FederationRuntime::info() const {
    const auto local = identity_->identity();
    return {.protocol = std::string{kFederationProtocol},
            .grid_id = local.grid_id,
            .base_url = local.base_url,
            .public_key_hex = local.keys.public_key_hex};
}

std::optional<IssuedTravelToken> FederationRuntime::issue_travel(
    const OutboundTravelRequest& request,
    std::string& reason) const {
    if (request.subject_user.empty() || request.display_name.empty() ||
        request.audience_grid.empty() || request.destination_region.empty()) {
        reason = "invalid-travel-request";
        return std::nullopt;
    }

    const auto peer = trust_->find(request.audience_grid);
    if (!peer || !peer->trusted || peer->revoked) {
        reason = "destination-grid-not-trusted";
        return std::nullopt;
    }

    const auto local = identity_->identity();
    TravelTokenClaims claims{
        .issuer_grid = local.grid_id,
        .audience_grid = request.audience_grid,
        .subject_user = request.subject_user,
        .display_name = request.display_name,
        .origin_region = request.origin_region,
        .destination_region = request.destination_region,
        .session_id = security::random_hex(16),
        .nonce = {},
        .issued_unix = 0,
        .expires_unix = 0};

    try {
        reason.clear();
        return issue_travel_token(local.keys.private_key_hex, std::move(claims),
                                  request.lifetime);
    } catch (...) {
        reason = "travel-token-issue-failed";
        return std::nullopt;
    }
}

std::optional<AcceptedTravel> FederationRuntime::accept_travel(
    const std::string_view issuer_grid,
    const std::string_view token,
    std::string& reason) {
    const auto peer = trust_->find(issuer_grid);
    if (!peer || !peer->trusted || peer->revoked) {
        reason = "issuer-grid-not-trusted";
        return std::nullopt;
    }

    const auto local = identity_->identity();
    const auto claims = verify_travel_token(peer->public_key_hex, token, local.grid_id, issuer_grid);
    if (!claims) {
        reason = "invalid-travel-token";
        return std::nullopt;
    }

    if (!replay_.consume(claims->nonce, claims->expires_unix)) {
        reason = "travel-token-replayed";
        return std::nullopt;
    }

    try {
        auto session = sessions_->create(*claims);
        reason.clear();
        return AcceptedTravel{.claims = *claims, .session = std::move(session)};
    } catch (...) {
        reason = "foreign-session-create-failed";
        return std::nullopt;
    }
}

bool FederationRuntime::trust_peer(FederationPeer peer, std::string& reason) {
    const auto local = identity_->identity();
    if (peer.grid_id == local.grid_id) {
        reason = "cannot-trust-self";
        return false;
    }
    return trust_->trust(std::move(peer), reason);
}

bool FederationRuntime::revoke_peer(const std::string_view grid_id) {
    return trust_->revoke(grid_id);
}

std::vector<FederationPeer> FederationRuntime::peers() const {
    return trust_->list();
}

std::vector<ForeignSession> FederationRuntime::sessions() const {
    return sessions_->list();
}

bool FederationRuntime::logout_session(const std::string_view session_id) {
    return sessions_->logout(session_id);
}

std::size_t FederationRuntime::maintenance(const std::int64_t now_unix) {
    replay_.purge_expired(now_unix);
    return sessions_->purge_expired(now_unix);
}

} // namespace opengenesis::federation
