std::string federation_peer_json(const federation::FederationPeer& peer) {
    return "{\"grid_id\":\"" + json_escape(peer.grid_id) +
           "\",\"base_url\":\"" + json_escape(peer.base_url) +
           "\",\"public_key\":\"" + json_escape(peer.public_key_hex) +
           "\",\"trusted\":" + (peer.trusted ? "true" : "false") +
           ",\"revoked\":" + (peer.revoked ? "true" : "false") +
           ",\"updated_unix\":" + std::to_string(peer.updated_unix) + "}";
}

std::string federation_peers_json(const std::vector<federation::FederationPeer>& peers) {
    std::ostringstream out;
    out << '[';
    for (std::size_t i = 0; i < peers.size(); ++i) {
        if (i) out << ',';
        out << federation_peer_json(peers[i]);
    }
    out << ']';
    return out.str();
}

std::string federation_session_json(const federation::ForeignSession& session) {
    return "{\"id\":\"" + json_escape(session.id) +
           "\",\"issuer_grid\":\"" + json_escape(session.issuer_grid) +
           "\",\"subject_user\":\"" + json_escape(session.subject_user) +
           "\",\"display_name\":\"" + json_escape(session.display_name) +
           "\",\"origin_region\":\"" + json_escape(session.origin_region) +
           "\",\"destination_region\":\"" + json_escape(session.destination_region) +
           "\",\"remote_session_id\":\"" + json_escape(session.remote_session_id) +
           "\",\"home_url\":\"" + json_escape(session.home_url) +
           "\",\"service_grant_id\":\"" + json_escape(session.service_grant_id) +
           "\",\"service_capabilities\":\"" + json_escape(session.service_capabilities) +
           "\",\"state\":\"" + std::string(federation::foreign_session_state_name(session.state)) +
           "\",\"created_unix\":" + std::to_string(session.created_unix) +
           ",\"expires_unix\":" + std::to_string(session.expires_unix) +
           ",\"ended_unix\":" + std::to_string(session.ended_unix) + "}";
}

std::string federation_sessions_json(const std::vector<federation::ForeignSession>& sessions) {
    std::ostringstream out;
    out << '[';
    for (std::size_t i = 0; i < sessions.size(); ++i) {
        if (i) out << ',';
        out << federation_session_json(sessions[i]);
    }
    out << ']';
    return out.str();
}


std::string federation_service_grant_json(
    const federation::FederationServiceGrant& grant) {
    return "{\"id\":\"" + json_escape(grant.id) +
           "\",\"audience_grid\":\"" + json_escape(grant.audience_grid) +
           "\",\"subject_user\":\"" + json_escape(grant.subject_user) +
           "\",\"capabilities\":\"" + json_escape(grant.capabilities) +
           "\",\"created_unix\":" + std::to_string(grant.created_unix) +
           ",\"expires_unix\":" + std::to_string(grant.expires_unix) +
           ",\"revoked\":" + (grant.revoked ? "true" : "false") +
           ",\"revoked_unix\":" + std::to_string(grant.revoked_unix) + "}";
}

std::string federation_service_grants_json(
    const std::vector<federation::FederationServiceGrant>& grants) {
    std::ostringstream out;
    out << '[';
    for (std::size_t i = 0; i < grants.size(); ++i) {
        if (i) out << ',';
        out << federation_service_grant_json(grants[i]);
    }
    out << ']';
    return out.str();
}
