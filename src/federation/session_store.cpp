#include "opengenesis/federation/session_store.hpp"

#include "opengenesis/platform/filesystem.hpp"
#include "opengenesis/security/crypto.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace opengenesis::federation {
namespace {

std::int64_t unix_now() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

std::vector<std::string> split_tab(const std::string& line) {
    std::vector<std::string> fields;
    std::size_t start = 0;
    while (true) {
        const auto end = line.find('\t', start);
        fields.push_back(line.substr(start, end == std::string::npos
                                               ? std::string::npos
                                               : end - start));
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return fields;
}

ForeignSessionState parse_state(const std::string_view state) {
    if (state == "logged_out") return ForeignSessionState::logged_out;
    if (state == "expired") return ForeignSessionState::expired;
    return ForeignSessionState::active;
}

} // namespace

std::string_view foreign_session_state_name(const ForeignSessionState state) noexcept {
    switch (state) {
        case ForeignSessionState::active: return "active";
        case ForeignSessionState::logged_out: return "logged_out";
        case ForeignSessionState::expired: return "expired";
    }
    return "expired";
}

FederationSessionStore::FederationSessionStore(std::string path) : path_(std::move(path)) {
    load();
}

ForeignSession FederationSessionStore::create(const TravelTokenClaims& claims) {
    const auto now = unix_now();
    if (claims.issuer_grid.empty() || claims.subject_user.empty() ||
        claims.destination_region.empty() || claims.session_id.empty() ||
        claims.expires_unix <= now) {
        throw std::invalid_argument("invalid foreign session claims");
    }

    ForeignSession session{
        .id = security::random_hex(16),
        .issuer_grid = claims.issuer_grid,
        .subject_user = claims.subject_user,
        .display_name = claims.display_name,
        .origin_region = claims.origin_region,
        .destination_region = claims.destination_region,
        .remote_session_id = claims.session_id,
        .home_url = claims.home_url,
        .service_grant_id = claims.service_grant_id,
        .service_token = claims.service_token,
        .service_capabilities = claims.service_capabilities,
        .state = ForeignSessionState::active,
        .created_unix = now,
        .expires_unix = claims.expires_unix,
        .ended_unix = 0};

    std::scoped_lock lock(mutex_);
    for (auto& [_, existing] : sessions_) {
        if (existing.state == ForeignSessionState::active &&
            existing.issuer_grid == session.issuer_grid &&
            existing.remote_session_id == session.remote_session_id) {
            existing.state = ForeignSessionState::logged_out;
            existing.ended_unix = now;
        }
    }
    sessions_[session.id] = session;
    persist_locked();
    return session;
}

std::optional<ForeignSession> FederationSessionStore::find(const std::string_view id) const {
    std::scoped_lock lock(mutex_);
    const auto it = sessions_.find(std::string{id});
    return it == sessions_.end() ? std::nullopt : std::optional<ForeignSession>{it->second};
}

std::vector<ForeignSession> FederationSessionStore::list() const {
    std::scoped_lock lock(mutex_);
    std::vector<ForeignSession> output;
    output.reserve(sessions_.size());
    for (const auto& [_, session] : sessions_) output.push_back(session);
    std::sort(output.begin(), output.end(), [](const ForeignSession& a, const ForeignSession& b) {
        return a.created_unix > b.created_unix;
    });
    return output;
}

bool FederationSessionStore::logout(const std::string_view id) {
    std::scoped_lock lock(mutex_);
    const auto it = sessions_.find(std::string{id});
    if (it == sessions_.end() || it->second.state != ForeignSessionState::active) return false;
    it->second.state = ForeignSessionState::logged_out;
    it->second.ended_unix = unix_now();
    persist_locked();
    return true;
}

std::size_t FederationSessionStore::logout_issuer(
    const std::string_view issuer_grid) {
    std::scoped_lock lock(mutex_);
    const auto now = unix_now();
    std::size_t changed = 0;
    for (auto& [_, session] : sessions_) {
        if (session.state == ForeignSessionState::active &&
            session.issuer_grid == issuer_grid) {
            session.state = ForeignSessionState::logged_out;
            session.ended_unix = now;
            ++changed;
        }
    }
    if (changed != 0U) persist_locked();
    return changed;
}

std::size_t FederationSessionStore::purge_expired(const std::int64_t now_unix) {
    std::scoped_lock lock(mutex_);
    std::size_t changed = 0;
    for (auto& [_, session] : sessions_) {
        if (session.state == ForeignSessionState::active && session.expires_unix <= now_unix) {
            session.state = ForeignSessionState::expired;
            session.ended_unix = now_unix;
            ++changed;
        }
    }
    if (changed != 0) persist_locked();
    return changed;
}

std::size_t FederationSessionStore::active_count() const {
    std::scoped_lock lock(mutex_);
    return static_cast<std::size_t>(std::count_if(
        sessions_.begin(), sessions_.end(), [](const auto& entry) {
            return entry.second.state == ForeignSessionState::active;
        }));
}

void FederationSessionStore::load() {
    std::scoped_lock lock(mutex_);
    sessions_.clear();
    std::ifstream input(path_);
    if (!input) return;

    std::string line;
    while (std::getline(input, line)) {
        if (line.empty() || line[0] == '#') continue;
        const auto fields = split_tab(line);
        if (fields.size() != 11U && fields.size() != 15U) continue;
        try {
            ForeignSession session{
                .id = fields[0],
                .issuer_grid = fields[1],
                .subject_user = fields[2],
                .display_name = fields[3],
                .origin_region = fields[4],
                .destination_region = fields[5],
                .remote_session_id = fields[6],
                .home_url = fields.size() == 15U ? fields[7] : std::string{},
                .service_grant_id =
                    fields.size() == 15U ? fields[8] : std::string{},
                .service_token =
                    fields.size() == 15U ? fields[9] : std::string{},
                .service_capabilities =
                    fields.size() == 15U ? fields[10] : std::string{},
                .state = parse_state(
                    fields[fields.size() == 15U ? 11U : 7U]),
                .created_unix = std::stoll(
                    fields[fields.size() == 15U ? 12U : 8U]),
                .expires_unix = std::stoll(
                    fields[fields.size() == 15U ? 13U : 9U]),
                .ended_unix = std::stoll(
                    fields[fields.size() == 15U ? 14U : 10U])};
            if (!session.id.empty() && !session.issuer_grid.empty() &&
                !session.subject_user.empty()) {
                sessions_[session.id] = std::move(session);
            }
        } catch (...) {
        }
    }
}

void FederationSessionStore::persist_locked() const {
    const std::filesystem::path path(path_);
    if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path());
    const auto temp = path.string() + ".tmp";

    std::ofstream output(temp, std::ios::trunc);
    if (!output) throw std::runtime_error("cannot write federation session store");
    output << "# OpenGenesisLINK federation sessions v2\n";

    std::vector<ForeignSession> rows;
    rows.reserve(sessions_.size());
    for (const auto& [_, session] : sessions_) rows.push_back(session);
    std::sort(rows.begin(), rows.end(), [](const ForeignSession& a, const ForeignSession& b) {
        return a.created_unix < b.created_unix;
    });

    for (const auto& session : rows) {
        output << session.id << '\t' << session.issuer_grid << '\t' << session.subject_user << '\t'
               << session.display_name << '\t' << session.origin_region << '\t'
               << session.destination_region << '\t' << session.remote_session_id << '\t'
               << session.home_url << '\t' << session.service_grant_id << '\t'
               << session.service_token << '\t' << session.service_capabilities << '\t'
               << foreign_session_state_name(session.state) << '\t'
               << session.created_unix << '\t' << session.expires_unix << '\t'
               << session.ended_unix << '\n';
    }

    output.close();
    if (!output) throw std::runtime_error("cannot flush federation session store");
    platform::replace_file(temp, path);
}

} // namespace opengenesis::federation
