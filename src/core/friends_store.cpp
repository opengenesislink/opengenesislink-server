#include "opengenesis/core/friends_store.hpp"

#include "opengenesis/security/crypto.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <utility>
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

FriendsStore::FriendsStore(std::string path) : path_(std::move(path)) { load(); }

std::string FriendsStore::pair_key(const std::string_view a, const std::string_view b) {
    return a < b ? std::string{a} + "|" + std::string{b} : std::string{b} + "|" + std::string{a};
}

std::optional<FriendRelation> FriendsStore::request(std::string from_user, std::string to_user,
                                                     std::string& reason) {
    if (from_user.empty() || to_user.empty() || from_user == to_user) {
        reason = "invalid-friend-target";
        return std::nullopt;
    }
    const auto key = pair_key(from_user, to_user);
    std::scoped_lock lock(mutex_);
    const auto existing = by_pair_.find(key);
    if (existing != by_pair_.end()) {
        reason = existing->second.status == "accepted" ? "already-friends" : "request-exists";
        return std::nullopt;
    }
    const auto now = unix_now();
    FriendRelation relation{.id = security::random_hex(16),
                            .user_a = std::min(from_user, to_user),
                            .user_b = std::max(from_user, to_user),
                            .requested_by = std::move(from_user),
                            .status = "pending",
                            .created_unix = now,
                            .updated_unix = now};
    by_pair_[key] = relation;
    persist_locked();
    reason.clear();
    return relation;
}

std::optional<FriendRelation> FriendsStore::accept(std::string user, std::string other_user,
                                                    std::string& reason) {
    const auto key = pair_key(user, other_user);
    std::scoped_lock lock(mutex_);
    const auto it = by_pair_.find(key);
    if (it == by_pair_.end() || it->second.status != "pending") {
        reason = "friend-request-not-found";
        return std::nullopt;
    }
    if (it->second.requested_by == user) {
        reason = "cannot-accept-own-request";
        return std::nullopt;
    }
    it->second.status = "accepted";
    it->second.updated_unix = unix_now();
    persist_locked();
    reason.clear();
    return it->second;
}

bool FriendsStore::remove(const std::string_view user, const std::string_view other_user) {
    const auto key = pair_key(user, other_user);
    std::scoped_lock lock(mutex_);
    const auto it = by_pair_.find(key);
    if (it == by_pair_.end()) return false;
    by_pair_.erase(it);
    persist_locked();
    return true;
}

bool FriendsStore::are_friends(const std::string_view user, const std::string_view other_user) const {
    const auto key = pair_key(user, other_user);
    std::scoped_lock lock(mutex_);
    const auto it = by_pair_.find(key);
    return it != by_pair_.end() && it->second.status == "accepted";
}

std::vector<FriendRelation> FriendsStore::list_for_user(const std::string_view user) const {
    std::scoped_lock lock(mutex_);
    std::vector<FriendRelation> result;
    for (const auto& [_, relation] : by_pair_) {
        if (relation.user_a == user || relation.user_b == user) result.push_back(relation);
    }
    std::sort(result.begin(), result.end(), [](const FriendRelation& a, const FriendRelation& b) {
        return a.updated_unix > b.updated_unix;
    });
    return result;
}

std::size_t FriendsStore::accepted_count() const {
    std::scoped_lock lock(mutex_);
    return static_cast<std::size_t>(std::count_if(by_pair_.begin(), by_pair_.end(), [](const auto& entry) {
        return entry.second.status == "accepted";
    }));
}

std::size_t FriendsStore::pending_count() const {
    std::scoped_lock lock(mutex_);
    return static_cast<std::size_t>(std::count_if(by_pair_.begin(), by_pair_.end(), [](const auto& entry) {
        return entry.second.status == "pending";
    }));
}

void FriendsStore::load() {
    std::scoped_lock lock(mutex_);
    by_pair_.clear();
    std::ifstream input(path_);
    if (!input) return;
    std::string line;
    while (std::getline(input, line)) {
        if (line.empty() || line[0] == '#') continue;
        const auto fields = split_tab(line);
        if (fields.size() != 7) continue;
        try {
            FriendRelation relation{.id = fields[0],
                                    .user_a = fields[1],
                                    .user_b = fields[2],
                                    .requested_by = fields[3],
                                    .status = fields[4],
                                    .created_unix = std::stoll(fields[5]),
                                    .updated_unix = std::stoll(fields[6])};
            if (relation.user_a.empty() || relation.user_b.empty() ||
                (relation.status != "pending" && relation.status != "accepted")) continue;
            by_pair_[pair_key(relation.user_a, relation.user_b)] = std::move(relation);
        } catch (...) {
        }
    }
}

void FriendsStore::persist_locked() const {
    const std::filesystem::path path(path_);
    if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path());
    const auto temp = path.string() + ".tmp";
    std::ofstream output(temp, std::ios::trunc);
    if (!output) throw std::runtime_error("cannot write friends store");
    output << "# OpenGenesisLINK friends store v1\n";
    std::vector<FriendRelation> rows;
    rows.reserve(by_pair_.size());
    for (const auto& [_, relation] : by_pair_) rows.push_back(relation);
    std::sort(rows.begin(), rows.end(), [](const FriendRelation& a, const FriendRelation& b) {
        return a.created_unix < b.created_unix;
    });
    for (const auto& relation : rows) {
        output << relation.id << '\t' << relation.user_a << '\t' << relation.user_b << '\t'
               << relation.requested_by << '\t' << relation.status << '\t' << relation.created_unix
               << '\t' << relation.updated_unix << '\n';
    }
    output.close();
    if (!output) throw std::runtime_error("cannot flush friends store");
    std::filesystem::rename(temp, path);
}

} // namespace opengenesis::core
