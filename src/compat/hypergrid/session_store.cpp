#include "opengenesis/compat/hypergrid/session_store.hpp"

#include "opengenesis/platform/filesystem.hpp"
#include "opengenesis/security/crypto.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace opengenesis::compat::hypergrid {
namespace {

std::int64_t unix_now() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

std::vector<std::string> split_tab(const std::string& line) {
    std::vector<std::string> result;
    std::size_t start = 0;
    while (true) {
        const auto end = line.find('\t', start);
        result.push_back(line.substr(start, end == std::string::npos
                                               ? std::string::npos
                                               : end - start));
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return result;
}

bool safe_field(const std::string_view value, const std::size_t max_size = 2048) {
    return !value.empty() && value.size() <= max_size &&
           value.find('\t') == std::string_view::npos &&
           value.find('\r') == std::string_view::npos &&
           value.find('\n') == std::string_view::npos;
}

TravelState parse_state(const std::string_view value) {
    if (value == "returning_home") return TravelState::returning_home;
    if (value == "logged_out") return TravelState::logged_out;
    if (value == "expired") return TravelState::expired;
    return TravelState::active;
}

char hex_digit(const unsigned value) {
    constexpr char digits[] = "0123456789abcdef";
    return digits[value & 0x0fU];
}

} // namespace

std::string_view travel_state_name(const TravelState state) noexcept {
    switch (state) {
        case TravelState::active: return "active";
        case TravelState::returning_home: return "returning_home";
        case TravelState::logged_out: return "logged_out";
        case TravelState::expired: return "expired";
    }
    return "expired";
}

std::string legacy_uuid_from_seed(const std::string_view seed) {
    auto hex = security::sha256_hex("OpenGenesisLINK-HG-UUID:" + std::string{seed});
    hex.resize(32);
    hex[12] = '5';
    unsigned variant = 0;
    const char c = hex[16];
    if (c >= '0' && c <= '9') variant = static_cast<unsigned>(c - '0');
    else if (c >= 'a' && c <= 'f') variant = static_cast<unsigned>(10 + c - 'a');
    else variant = static_cast<unsigned>(10 + c - 'A');
    hex[16] = hex_digit((variant & 0x3U) | 0x8U);
    return hex.substr(0, 8) + "-" + hex.substr(8, 4) + "-" + hex.substr(12, 4) + "-" +
           hex.substr(16, 4) + "-" + hex.substr(20, 12);
}

HypergridSessionStore::HypergridSessionStore(std::string path) : path_(std::move(path)) {
    load();
}

HomeTravelSession HypergridSessionStore::issue_home_travel(
    std::string native_user_id,
    std::string user_id,
    std::string destination_gatekeeper,
    std::string client_ip,
    std::chrono::seconds lifetime) {
    if (!safe_field(native_user_id, 256) || !safe_field(user_id, 64) ||
        !safe_field(destination_gatekeeper) || !safe_field(client_ip, 128)) {
        throw std::invalid_argument("invalid Hypergrid travel fields");
    }
    lifetime = std::clamp(lifetime, std::chrono::seconds{60}, std::chrono::seconds{86400});
    const auto now = unix_now();
    const auto session_id = legacy_uuid_from_seed(security::random_hex(32));
    HomeTravelSession session{
        .session_id = session_id,
        .user_id = std::move(user_id),
        .native_user_id = std::move(native_user_id),
        .destination_gatekeeper = std::move(destination_gatekeeper),
        .service_token = {},
        .client_ip = std::move(client_ip),
        .state = TravelState::active,
        .created_unix = now,
        .expires_unix = now + lifetime.count(),
        .ended_unix = 0};
    session.service_token = session.destination_gatekeeper + ";" +
                            legacy_uuid_from_seed(security::random_hex(32));

    std::scoped_lock lock(mutex_);
    home_[session.session_id] = session;
    persist_locked();
    return session;
}

bool HypergridSessionStore::verify_agent(const std::string_view session_id,
                                         const std::string_view service_token) const {
    std::scoped_lock lock(mutex_);
    const auto it = home_.find(std::string{session_id});
    return it != home_.end() &&
           (it->second.state == TravelState::active ||
            it->second.state == TravelState::returning_home) &&
           it->second.expires_unix > unix_now() &&
           it->second.service_token == service_token;
}

bool HypergridSessionStore::verify_client(const std::string_view session_id,
                                          const std::string_view reported_ip) const {
    std::scoped_lock lock(mutex_);
    const auto it = home_.find(std::string{session_id});
    return it != home_.end() && it->second.state == TravelState::active &&
           it->second.expires_unix > unix_now() && !reported_ip.empty() &&
           it->second.client_ip == reported_ip;
}

bool HypergridSessionStore::is_agent_coming_home(
    const std::string_view session_id,
    const std::string_view grid_external_name) const {
    std::scoped_lock lock(mutex_);
    const auto it = home_.find(std::string{session_id});
    return it != home_.end() &&
           (it->second.state == TravelState::returning_home ||
            it->second.state == TravelState::active) &&
           it->second.destination_gatekeeper == grid_external_name;
}

std::optional<HomeTravelSession> HypergridSessionStore::home(
    const std::string_view session_id) const {
    std::scoped_lock lock(mutex_);
    const auto it = home_.find(std::string{session_id});
    return it == home_.end() ? std::nullopt : std::optional<HomeTravelSession>{it->second};
}

bool HypergridSessionStore::request_return_home(
    const std::string_view native_user_id,
    const std::string_view session_id,
    const std::string_view home_grid_uri) {
    if (home_grid_uri.empty()) return false;
    std::scoped_lock lock(mutex_);
    const auto it = home_.find(std::string{session_id});
    if (it == home_.end() || it->second.native_user_id != native_user_id ||
        (it->second.state != TravelState::active &&
         it->second.state != TravelState::returning_home)) {
        return false;
    }
    it->second.destination_gatekeeper = std::string{home_grid_uri};
    it->second.state = TravelState::returning_home;
    persist_locked();
    return true;
}

bool HypergridSessionStore::logout_home(const std::string_view user_id,
                                        const std::string_view session_id) {
    std::scoped_lock lock(mutex_);
    const auto it = home_.find(std::string{session_id});
    if (it == home_.end() || it->second.user_id != user_id ||
        (it->second.state != TravelState::active &&
         it->second.state != TravelState::returning_home)) {
        return false;
    }
    it->second.state = TravelState::logged_out;
    it->second.ended_unix = unix_now();
    persist_locked();
    return true;
}

bool HypergridSessionStore::upsert_foreign(ForeignVisitorSession session,
                                           std::string& reason) {
    const auto now = unix_now();
    const auto optional_safe = [](const std::string& value) {
        return value.empty() || safe_field(value);
    };
    if (!safe_field(session.session_id, 64) || !safe_field(session.agent_id, 64) ||
        !safe_field(session.home_uri) || !optional_safe(session.asset_uri) ||
        !optional_safe(session.inventory_uri) || !optional_safe(session.avatar_uri) ||
        !optional_safe(session.im_uri) || !safe_field(session.service_token) ||
        !safe_field(session.destination_region, 256) ||
        session.expires_unix <= now || session.expires_unix > now + 86400) {
        reason = "invalid-foreign-session";
        return false;
    }
    std::scoped_lock lock(mutex_);
    foreign_[session.session_id] = std::move(session);
    persist_locked();
    reason.clear();
    return true;
}

std::optional<ForeignVisitorSession> HypergridSessionStore::foreign(
    const std::string_view session_id) const {
    std::scoped_lock lock(mutex_);
    const auto it = foreign_.find(std::string{session_id});
    return it == foreign_.end() ? std::nullopt : std::optional<ForeignVisitorSession>{it->second};
}

std::optional<ForeignVisitorSession> HypergridSessionStore::foreign_by_agent(
    const std::string_view agent_id) const {
    std::scoped_lock lock(mutex_);
    for (const auto& [_, session] : foreign_) {
        if (session.agent_id == agent_id) return session;
    }
    return std::nullopt;
}

bool HypergridSessionStore::logout_foreign(const std::string_view session_id) {
    std::scoped_lock lock(mutex_);
    if (foreign_.erase(std::string{session_id}) == 0) return false;
    persist_locked();
    return true;
}

std::vector<HomeTravelSession> HypergridSessionStore::home_sessions() const {
    std::scoped_lock lock(mutex_);
    std::vector<HomeTravelSession> result;
    result.reserve(home_.size());
    for (const auto& [_, session] : home_) result.push_back(session);
    return result;
}

std::vector<ForeignVisitorSession> HypergridSessionStore::foreign_sessions() const {
    std::scoped_lock lock(mutex_);
    std::vector<ForeignVisitorSession> result;
    result.reserve(foreign_.size());
    for (const auto& [_, session] : foreign_) result.push_back(session);
    return result;
}

std::size_t HypergridSessionStore::purge_expired(const std::int64_t now_unix) {
    std::scoped_lock lock(mutex_);
    std::size_t changed = 0;
    for (auto& [_, session] : home_) {
        if (session.state == TravelState::active && session.expires_unix <= now_unix) {
            session.state = TravelState::expired;
            session.ended_unix = now_unix;
            ++changed;
        }
    }
    for (auto it = foreign_.begin(); it != foreign_.end();) {
        if (it->second.expires_unix <= now_unix) {
            it = foreign_.erase(it);
            ++changed;
        } else {
            ++it;
        }
    }
    if (changed != 0) persist_locked();
    return changed;
}

void HypergridSessionStore::load() {
    std::scoped_lock lock(mutex_);
    std::ifstream input(path_);
    if (!input) return;
    std::string line;
    while (std::getline(input, line)) {
        if (line.empty() || line[0] == '#') continue;
        const auto fields = split_tab(line);
        try {
            if (fields[0] == "H" && fields.size() == 12) {
                HomeTravelSession session{
                    .session_id = fields[1],
                    .user_id = fields[2],
                    .native_user_id = fields[3],
                    .destination_gatekeeper = fields[4],
                    .service_token = fields[5],
                    .client_ip = fields[6],
                    .state = parse_state(fields[7]),
                    .created_unix = std::stoll(fields[8]),
                    .expires_unix = std::stoll(fields[9]),
                    .ended_unix = std::stoll(fields[10])};
                home_[session.session_id] = std::move(session);
            } else if (fields[0] == "V" && (fields.size() == 12 || fields.size() == 16)) {
                const bool v2 = fields.size() == 16;
                ForeignVisitorSession session{
                    .session_id = fields[1],
                    .agent_id = fields[2],
                    .home_uri = fields[3],
                    .asset_uri = v2 ? fields[4] : std::string{},
                    .inventory_uri = v2 ? fields[5] : std::string{},
                    .avatar_uri = v2 ? fields[6] : std::string{},
                    .im_uri = v2 ? fields[7] : std::string{},
                    .service_token = fields[v2 ? 8 : 4],
                    .destination_region = fields[v2 ? 9 : 5],
                    .first_name = fields[v2 ? 10 : 6],
                    .last_name = fields[v2 ? 11 : 7],
                    .client_ip = fields[v2 ? 12 : 8],
                    .verified = fields[v2 ? 13 : 9] == "1",
                    .created_unix = std::stoll(fields[v2 ? 14 : 10]),
                    .expires_unix = std::stoll(fields[v2 ? 15 : 11])};
                foreign_[session.session_id] = std::move(session);
            }
        } catch (...) {
        }
    }
}

void HypergridSessionStore::persist_locked() const {
    const std::filesystem::path path(path_);
    if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path());
    const auto temporary = path.string() + ".tmp";
    std::ofstream output(temporary, std::ios::trunc);
    if (!output) throw std::runtime_error("cannot write Hypergrid session store");
    output << "# OpenGenesisLINK Hypergrid sessions v2\n";
    for (const auto& [_, session] : home_) {
        output << "H\t" << session.session_id << '\t' << session.user_id << '\t'
               << session.native_user_id << '\t' << session.destination_gatekeeper << '\t'
               << session.service_token << '\t' << session.client_ip << '\t'
               << travel_state_name(session.state) << '\t' << session.created_unix << '\t'
               << session.expires_unix << '\t' << session.ended_unix << "\t0\n";
    }
    for (const auto& [_, session] : foreign_) {
        output << "V\t" << session.session_id << '\t' << session.agent_id << '\t'
               << session.home_uri << '\t' << session.asset_uri << '\t'
               << session.inventory_uri << '\t' << session.avatar_uri << '\t'
               << session.im_uri << '\t' << session.service_token << '\t'
               << session.destination_region << '\t' << session.first_name << '\t'
               << session.last_name << '\t' << session.client_ip << '\t'
               << (session.verified ? '1' : '0') << '\t' << session.created_unix << '\t'
               << session.expires_unix << '\n';
    }
    output.close();
    if (!output) throw std::runtime_error("cannot flush Hypergrid session store");
    platform::replace_file(temporary, path);
}

} // namespace opengenesis::compat::hypergrid
