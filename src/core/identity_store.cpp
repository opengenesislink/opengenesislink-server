#include "opengenesis/core/identity_store.hpp"

#include "opengenesis/security/crypto.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

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

IdentityStore::IdentityStore(std::string path) : path_(std::move(path)) { load(); }

std::string IdentityStore::normalize_username(const std::string_view username) {
    std::string normalized;
    normalized.reserve(username.size());
    for (const unsigned char c : username) normalized.push_back(static_cast<char>(std::tolower(c)));
    return normalized;
}

bool IdentityStore::valid_username(const std::string_view username) {
    if (username.size() < 3 || username.size() > 32) return false;
    return std::all_of(username.begin(), username.end(), [](const unsigned char c) {
        return std::isalnum(c) != 0 || c == '.' || c == '_' || c == '-';
    });
}

bool IdentityStore::valid_display_name(const std::string_view display_name) {
    if (display_name.empty() || display_name.size() > 64) return false;
    return std::none_of(display_name.begin(), display_name.end(), [](const unsigned char c) {
        return c < 0x20 || c == 0x7f || c == '\t';
    });
}

std::optional<UserInfo> IdentityStore::register_user(std::string username, std::string display_name,
                                                      const std::string_view password,
                                                      std::string& reason) {
    if (!valid_username(username)) {
        reason = "invalid-username";
        return std::nullopt;
    }
    if (!valid_display_name(display_name)) {
        reason = "invalid-display-name";
        return std::nullopt;
    }
    if (password.size() < 8 || password.size() > 1024) {
        reason = "invalid-password";
        return std::nullopt;
    }

    const auto normalized = normalize_username(username);
    const auto password_hash = security::hash_password(password);
    StoredUser stored{.public_info = {.id = security::random_hex(16),
                                      .username = normalized,
                                      .display_name = std::move(display_name),
                                      .created_unix = unix_now()},
                      .password_hash = password_hash};

    std::scoped_lock lock(mutex_);
    if (by_username_.contains(normalized)) {
        reason = "username-exists";
        return std::nullopt;
    }
    username_by_id_[stored.public_info.id] = normalized;
    by_username_[normalized] = stored;
    persist_locked();
    reason.clear();
    return stored.public_info;
}

std::optional<UserInfo> IdentityStore::authenticate(const std::string_view username,
                                                     const std::string_view password) const {
    const auto normalized = normalize_username(username);
    StoredUser stored;
    {
        std::scoped_lock lock(mutex_);
        const auto it = by_username_.find(normalized);
        if (it == by_username_.end()) return std::nullopt;
        stored = it->second;
    }
    if (!security::verify_password(password, stored.password_hash)) return std::nullopt;
    return stored.public_info;
}

std::optional<UserInfo> IdentityStore::find_by_id(const std::string_view id) const {
    std::scoped_lock lock(mutex_);
    const auto id_it = username_by_id_.find(std::string{id});
    if (id_it == username_by_id_.end()) return std::nullopt;
    const auto user_it = by_username_.find(id_it->second);
    return user_it == by_username_.end() ? std::nullopt : std::optional<UserInfo>{user_it->second.public_info};
}

std::vector<UserInfo> IdentityStore::list() const {
    std::scoped_lock lock(mutex_);
    std::vector<UserInfo> users;
    users.reserve(by_username_.size());
    for (const auto& [_, stored] : by_username_) users.push_back(stored.public_info);
    std::sort(users.begin(), users.end(), [](const UserInfo& a, const UserInfo& b) {
        return a.created_unix < b.created_unix;
    });
    return users;
}

std::size_t IdentityStore::count() const {
    std::scoped_lock lock(mutex_);
    return by_username_.size();
}

void IdentityStore::load() {
    std::scoped_lock lock(mutex_);
    by_username_.clear();
    username_by_id_.clear();
    std::ifstream input(path_);
    if (!input) return;
    std::string line;
    while (std::getline(input, line)) {
        if (line.empty() || line[0] == '#') continue;
        const auto fields = split_tab(line);
        if (fields.size() != 5) continue;
        try {
            StoredUser stored{.public_info = {.id = fields[0],
                                              .username = normalize_username(fields[1]),
                                              .display_name = fields[2],
                                              .created_unix = std::stoll(fields[4])},
                              .password_hash = fields[3]};
            if (!valid_username(stored.public_info.username) ||
                !valid_display_name(stored.public_info.display_name) || stored.public_info.id.empty()) {
                continue;
            }
            username_by_id_[stored.public_info.id] = stored.public_info.username;
            by_username_[stored.public_info.username] = std::move(stored);
        } catch (...) {
        }
    }
}

void IdentityStore::persist_locked() const {
    const std::filesystem::path path(path_);
    if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path());
    const auto temporary = path.string() + ".tmp";
    std::ofstream output(temporary, std::ios::trunc);
    if (!output) throw std::runtime_error("cannot write identity store");
    output << "# OpenGenesisLINK identity store v1\n";
    std::vector<StoredUser> users;
    users.reserve(by_username_.size());
    for (const auto& [_, stored] : by_username_) users.push_back(stored);
    std::sort(users.begin(), users.end(), [](const StoredUser& a, const StoredUser& b) {
        return a.public_info.created_unix < b.public_info.created_unix;
    });
    for (const auto& stored : users) {
        const auto& user = stored.public_info;
        output << user.id << '\t' << user.username << '\t' << user.display_name << '\t'
               << stored.password_hash << '\t' << user.created_unix << '\n';
    }
    output.close();
    if (!output) throw std::runtime_error("cannot flush identity store");
    std::filesystem::rename(temporary, path);
}

} // namespace opengenesis::core
