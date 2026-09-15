#include "opengenesis/core/session_store.hpp"

#include "opengenesis/security/crypto.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <vector>

namespace opengenesis::core {
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
        fields.push_back(line.substr(start, end == std::string::npos ? std::string::npos : end - start));
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return fields;
}

} // namespace

SessionStore::SessionStore(std::string path, const std::chrono::seconds lifetime)
    : path_(std::move(path)), lifetime_(std::max(lifetime, std::chrono::seconds{300})) {
    load();
}

CreatedSession SessionStore::create(std::string user_id) {
    const auto token = security::random_hex(32);
    const auto token_hash = security::sha256_hex(token);
    const auto now = unix_now();
    StoredSession stored{.public_info = {.id = security::random_hex(12),
                                         .user_id = std::move(user_id),
                                         .issued_unix = now,
                                         .expires_unix = now + lifetime_.count()},
                         .token_hash = token_hash};
    std::scoped_lock lock(mutex_);
    purge_expired_locked(now);
    by_hash_[token_hash] = stored;
    persist_locked();
    return {.token = token, .session = stored.public_info};
}

std::optional<AuthSession> SessionStore::find(const std::string_view token) {
    if (token.empty()) return std::nullopt;
    const auto hash = security::sha256_hex(token);
    const auto now = unix_now();
    std::scoped_lock lock(mutex_);
    purge_expired_locked(now);
    const auto it = by_hash_.find(hash);
    return it == by_hash_.end() ? std::nullopt : std::optional<AuthSession>{it->second.public_info};
}

bool SessionStore::revoke(const std::string_view token) {
    if (token.empty()) return false;
    const auto hash = security::sha256_hex(token);
    std::scoped_lock lock(mutex_);
    const auto erased = by_hash_.erase(hash) != 0;
    if (erased) persist_locked();
    return erased;
}

std::size_t SessionStore::purge_expired() {
    const auto now = unix_now();
    std::scoped_lock lock(mutex_);
    const auto before = by_hash_.size();
    purge_expired_locked(now);
    if (before != by_hash_.size()) persist_locked();
    return before - by_hash_.size();
}

std::size_t SessionStore::active_count() {
    const auto now = unix_now();
    std::scoped_lock lock(mutex_);
    purge_expired_locked(now);
    return by_hash_.size();
}

void SessionStore::purge_expired_locked(const std::int64_t now) {
    for (auto it = by_hash_.begin(); it != by_hash_.end();) {
        if (it->second.public_info.expires_unix <= now) it = by_hash_.erase(it);
        else ++it;
    }
}

void SessionStore::load() {
    std::scoped_lock lock(mutex_);
    by_hash_.clear();
    std::ifstream input(path_);
    if (!input) return;
    std::string line;
    while (std::getline(input, line)) {
        if (line.empty() || line[0] == '#') continue;
        const auto fields = split_tab(line);
        if (fields.size() != 5) continue;
        try {
            StoredSession stored{.public_info = {.id = fields[1],
                                                 .user_id = fields[2],
                                                 .issued_unix = std::stoll(fields[3]),
                                                 .expires_unix = std::stoll(fields[4])},
                                 .token_hash = fields[0]};
            if (!stored.token_hash.empty() && !stored.public_info.user_id.empty()) {
                by_hash_[stored.token_hash] = std::move(stored);
            }
        } catch (...) {
        }
    }
    purge_expired_locked(unix_now());
}

void SessionStore::persist_locked() const {
    const std::filesystem::path path(path_);
    if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path());
    const auto temporary = path.string() + ".tmp";
    std::ofstream output(temporary, std::ios::trunc);
    if (!output) throw std::runtime_error("cannot write session store");
    output << "# OpenGenesisLINK session store v1 (token hashes only)\n";
    for (const auto& [hash, stored] : by_hash_) {
        const auto& session = stored.public_info;
        output << hash << '\t' << session.id << '\t' << session.user_id << '\t'
               << session.issued_unix << '\t' << session.expires_unix << '\n';
    }
    output.close();
    if (!output) throw std::runtime_error("cannot flush session store");
    std::filesystem::rename(temporary, path);
}

} // namespace opengenesis::core
