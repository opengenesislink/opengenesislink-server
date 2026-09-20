#include "opengenesis/federation/service_grant_store.hpp"

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

bool safe_text(
    const std::string_view value,
    const std::size_t maximum = 2048U) {
    return !value.empty() && value.size() <= maximum &&
           value.find('\t') == std::string_view::npos &&
           value.find('\r') == std::string_view::npos &&
           value.find('\n') == std::string_view::npos;
}

bool valid_capabilities(const std::string_view capabilities) {
    if (!safe_text(capabilities, 1024U)) return false;
    std::size_t start = 0;
    while (start <= capabilities.size()) {
        const auto end = capabilities.find(',', start);
        const auto token = capabilities.substr(
            start,
            end == std::string_view::npos
                ? std::string_view::npos
                : end - start);
        if (token.empty() || token.size() > 64U ||
            !std::all_of(
                token.begin(), token.end(),
                [](const unsigned char c) {
                    return (c >= 'a' && c <= 'z') ||
                           (c >= '0' && c <= '9') ||
                           c == '.' || c == '-' || c == '_';
                })) {
            return false;
        }
        if (end == std::string_view::npos) break;
        start = end + 1U;
    }
    return true;
}

std::vector<std::string> split_tab(const std::string& line) {
    std::vector<std::string> fields;
    std::size_t start = 0;
    while (true) {
        const auto end = line.find('\t', start);
        fields.push_back(
            line.substr(
                start,
                end == std::string::npos
                    ? std::string::npos
                    : end - start));
        if (end == std::string::npos) break;
        start = end + 1U;
    }
    return fields;
}

} // namespace

bool federation_service_capability(
    const std::string_view capabilities,
    const std::string_view capability) {
    if (capability.empty()) return false;
    std::size_t start = 0;
    while (start <= capabilities.size()) {
        const auto end = capabilities.find(',', start);
        const auto token = capabilities.substr(
            start,
            end == std::string_view::npos
                ? std::string_view::npos
                : end - start);
        if (token == capability) return true;
        if (end == std::string_view::npos) break;
        start = end + 1U;
    }
    return false;
}

FederationServiceGrantStore::FederationServiceGrantStore(
    std::string path)
    : path_(std::move(path)) {
    load();
}

IssuedFederationServiceGrant FederationServiceGrantStore::issue(
    std::string audience_grid,
    std::string subject_user,
    std::chrono::seconds lifetime,
    std::string capabilities) {
    if (!safe_text(audience_grid, 256U) ||
        !safe_text(subject_user, 256U) ||
        !valid_capabilities(capabilities)) {
        throw std::invalid_argument(
            "invalid federation service grant");
    }

    lifetime = std::clamp(
        lifetime,
        std::chrono::seconds{30},
        std::chrono::seconds{900});
    const auto now = unix_now();
    const auto token = security::random_hex(32);
    FederationServiceGrant grant{
        .id = security::random_hex(16),
        .audience_grid = std::move(audience_grid),
        .subject_user = std::move(subject_user),
        .token_hash = security::sha256_hex(token),
        .capabilities = std::move(capabilities),
        .created_unix = now,
        .expires_unix = now + lifetime.count(),
        .revoked = false,
        .revoked_unix = 0};

    std::scoped_lock lock(mutex_);
    grants_[grant.id] = grant;
    persist_locked();
    return {.grant = std::move(grant), .token = token};
}

bool FederationServiceGrantStore::authorize(
    const std::string_view grant_id,
    const std::string_view token,
    const std::string_view audience_grid,
    const std::string_view subject_user,
    const std::string_view capability) const {
    if (grant_id.empty() || token.empty() ||
        audience_grid.empty() || subject_user.empty() ||
        capability.empty()) {
        return false;
    }

    std::scoped_lock lock(mutex_);
    const auto it = grants_.find(std::string{grant_id});
    if (it == grants_.end()) return false;

    const auto& grant = it->second;
    const auto now = unix_now();
    return !grant.revoked &&
           grant.expires_unix > now &&
           grant.audience_grid == audience_grid &&
           grant.subject_user == subject_user &&
           federation_service_capability(
               grant.capabilities, capability) &&
           security::secure_equals(
               grant.token_hash,
               security::sha256_hex(token));
}

bool FederationServiceGrantStore::revoke(
    const std::string_view grant_id) {
    std::scoped_lock lock(mutex_);
    const auto it = grants_.find(std::string{grant_id});
    if (it == grants_.end() || it->second.revoked) return false;
    it->second.revoked = true;
    it->second.revoked_unix = unix_now();
    persist_locked();
    return true;
}

std::size_t FederationServiceGrantStore::revoke_audience(
    const std::string_view audience_grid) {
    std::scoped_lock lock(mutex_);
    const auto now = unix_now();
    std::size_t changed = 0;
    for (auto& [_, grant] : grants_) {
        if (!grant.revoked &&
            grant.audience_grid == audience_grid) {
            grant.revoked = true;
            grant.revoked_unix = now;
            ++changed;
        }
    }
    if (changed != 0U) persist_locked();
    return changed;
}

std::size_t FederationServiceGrantStore::purge_expired(
    const std::int64_t now_unix) {
    std::scoped_lock lock(mutex_);
    std::size_t removed = 0;
    for (auto it = grants_.begin(); it != grants_.end();) {
        const bool old_revoked =
            it->second.revoked &&
            it->second.revoked_unix > 0 &&
            it->second.revoked_unix + 86400 <= now_unix;
        const bool expired =
            it->second.expires_unix <= now_unix;
        if (expired || old_revoked) {
            it = grants_.erase(it);
            ++removed;
        } else {
            ++it;
        }
    }
    if (removed != 0U) persist_locked();
    return removed;
}

std::optional<FederationServiceGrant>
FederationServiceGrantStore::find(
    const std::string_view grant_id) const {
    std::scoped_lock lock(mutex_);
    const auto it = grants_.find(std::string{grant_id});
    return it == grants_.end()
               ? std::nullopt
               : std::optional<FederationServiceGrant>{
                     it->second};
}

std::vector<FederationServiceGrant>
FederationServiceGrantStore::list() const {
    std::scoped_lock lock(mutex_);
    std::vector<FederationServiceGrant> result;
    result.reserve(grants_.size());
    for (const auto& [_, grant] : grants_) {
        result.push_back(grant);
    }
    std::sort(
        result.begin(), result.end(),
        [](const FederationServiceGrant& left,
           const FederationServiceGrant& right) {
            return left.created_unix > right.created_unix;
        });
    return result;
}

void FederationServiceGrantStore::load() {
    std::scoped_lock lock(mutex_);
    grants_.clear();

    std::ifstream input(path_);
    if (!input) return;

    std::string line;
    while (std::getline(input, line)) {
        if (line.empty() || line[0] == '#') continue;
        const auto fields = split_tab(line);
        if (fields.size() != 9U) continue;
        try {
            FederationServiceGrant grant{
                .id = fields[0],
                .audience_grid = fields[1],
                .subject_user = fields[2],
                .token_hash = fields[3],
                .capabilities = fields[4],
                .created_unix = std::stoll(fields[5]),
                .expires_unix = std::stoll(fields[6]),
                .revoked = fields[7] == "1",
                .revoked_unix = std::stoll(fields[8])};
            if (!grant.id.empty() &&
                safe_text(grant.audience_grid, 256U) &&
                safe_text(grant.subject_user, 256U) &&
                valid_capabilities(grant.capabilities)) {
                grants_[grant.id] = std::move(grant);
            }
        } catch (...) {
        }
    }
}

void FederationServiceGrantStore::persist_locked() const {
    const std::filesystem::path path(path_);
    if (path.has_parent_path()) {
        std::filesystem::create_directories(
            path.parent_path());
    }
    const auto temporary = path.string() + ".tmp";
    std::ofstream output(temporary, std::ios::trunc);
    if (!output) {
        throw std::runtime_error(
            "cannot write federation service grants");
    }

    output << "# OpenGenesisLINK federation service grants v1\n";
    std::vector<FederationServiceGrant> rows;
    rows.reserve(grants_.size());
    for (const auto& [_, grant] : grants_) rows.push_back(grant);
    std::sort(
        rows.begin(), rows.end(),
        [](const FederationServiceGrant& left,
           const FederationServiceGrant& right) {
            return left.created_unix < right.created_unix;
        });

    for (const auto& grant : rows) {
        output << grant.id << '\t'
               << grant.audience_grid << '\t'
               << grant.subject_user << '\t'
               << grant.token_hash << '\t'
               << grant.capabilities << '\t'
               << grant.created_unix << '\t'
               << grant.expires_unix << '\t'
               << (grant.revoked ? 1 : 0) << '\t'
               << grant.revoked_unix << '\n';
    }

    output.close();
    if (!output) {
        throw std::runtime_error(
            "cannot flush federation service grants");
    }
    platform::replace_file(temporary, path);
}

} // namespace opengenesis::federation
