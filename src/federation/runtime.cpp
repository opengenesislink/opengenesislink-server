#include "opengenesis/federation/runtime.hpp"

#include "opengenesis/security/crypto.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace opengenesis::federation {

FederationRuntime::FederationRuntime(
    std::shared_ptr<GridIdentityStore> identity,
    std::shared_ptr<FederationTrustStore> trust,
    std::shared_ptr<FederationSessionStore> sessions,
    std::shared_ptr<FederationServiceGrantStore> grants)
    : identity_(std::move(identity)),
      trust_(std::move(trust)),
      sessions_(std::move(sessions)),
      grants_(std::move(grants)) {
    if (!identity_ || !trust_ || !sessions_ || !grants_) {
        throw std::invalid_argument(
            "federation runtime dependencies required");
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
        request.audience_grid.empty() ||
        request.destination_region.empty() ||
        request.service_capabilities.empty()) {
        reason = "invalid-travel-request";
        return std::nullopt;
    }

    const auto peer = trust_->find(request.audience_grid);
    if (!peer || !peer->trusted || peer->revoked) {
        reason = "destination-grid-not-trusted";
        return std::nullopt;
    }

    try {
        const auto local = identity_->identity();
        const auto grant = grants_->issue(
            request.audience_grid,
            request.subject_user,
            request.lifetime,
            request.service_capabilities);

        TravelTokenClaims claims{
            .issuer_grid = local.grid_id,
            .audience_grid = request.audience_grid,
            .subject_user = request.subject_user,
            .display_name = request.display_name,
            .origin_region = request.origin_region,
            .destination_region = request.destination_region,
            .session_id = security::random_hex(16),
            .home_url = local.base_url,
            .service_grant_id = grant.grant.id,
            .service_token = grant.token,
            .service_capabilities = grant.grant.capabilities,
            .nonce = {},
            .issued_unix = 0,
            .expires_unix = 0};

        try {
            auto issued = issue_travel_token(
                local.keys.private_key_hex,
                std::move(claims),
                request.lifetime);
            reason.clear();
            return issued;
        } catch (...) {
            (void)grants_->revoke(grant.grant.id);
            throw;
        }
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
    const auto claims = verify_travel_token(
        peer->public_key_hex, token,
        local.grid_id, issuer_grid);
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
        return AcceptedTravel{
            .claims = *claims,
            .session = std::move(session)};
    } catch (...) {
        reason = "foreign-session-create-failed";
        return std::nullopt;
    }
}

bool FederationRuntime::trust_peer(
    FederationPeer peer,
    std::string& reason) {
    const auto local = identity_->identity();
    if (peer.grid_id == local.grid_id) {
        reason = "cannot-trust-self";
        return false;
    }
    return trust_->trust(std::move(peer), reason);
}

bool FederationRuntime::revoke_peer(
    const std::string_view grid_id) {
    const bool revoked = trust_->revoke(grid_id);
    if (revoked) {
        (void)grants_->revoke_audience(grid_id);
    }
    return revoked;
}

std::vector<FederationPeer> FederationRuntime::peers() const {
    return trust_->list();
}

std::vector<ForeignSession> FederationRuntime::sessions() const {
    return sessions_->list();
}

bool FederationRuntime::logout_session(
    const std::string_view session_id) {
    return sessions_->logout(session_id);
}

bool FederationRuntime::authorize_remote_service(
    const std::string_view grant_id,
    const std::string_view service_token,
    const std::string_view audience_grid,
    const std::string_view subject_user,
    const std::string_view capability) const {
    const auto peer = trust_->find(audience_grid);
    if (!peer || !peer->trusted || peer->revoked) {
        return false;
    }
    return grants_->authorize(
        grant_id, service_token, audience_grid,
        subject_user, capability);
}

std::vector<FederationServiceGrant>
FederationRuntime::service_grants() const {
    return grants_->list();
}

bool FederationRuntime::revoke_service_grant(
    const std::string_view grant_id) {
    return grants_->revoke(grant_id);
}

std::size_t FederationRuntime::maintenance(
    const std::int64_t now_unix) {
    replay_.purge_expired(now_unix);
    const auto sessions =
        sessions_->purge_expired(now_unix);
    const auto grants =
        grants_->purge_expired(now_unix);
    return sessions + grants;
}

} // namespace opengenesis::federation
