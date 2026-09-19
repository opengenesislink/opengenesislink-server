#include "opengenesis/core/identity_store.hpp"
#include "opengenesis/platform/filesystem.hpp"
#include "opengenesis/security/crypto.hpp"
#include "opengenesis/storage/database.hpp"

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
        const auto end = line.find('	', start);
        fields.push_back(
            line.substr(
                start,
                end == std::string::npos
                    ? std::string::npos
                    : end - start));
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return fields;
}

std::optional<std::string> cell(
    const storage::DatabaseRow& row,
    const std::string& name) {
    const auto it = row.find(name);
    if (it == row.end()) return std::nullopt;
    return it->second;
}

} // namespace

IdentityStore::IdentityStore(std::string path)
    : path_(std::move(path)) {
    load();
}

IdentityStore::IdentityStore(
    std::shared_ptr<storage::DatabasePool> database)
    : database_(std::move(database)) {
    if (!database_) {
        throw std::invalid_argument("identity database is required");
    }
}

std::string IdentityStore::normalize_username(
    const std::string_view username) {
    std::string normalized;
    normalized.reserve(username.size());
    for (const unsigned char c : username) {
        normalized.push_back(
            static_cast<char>(std::tolower(c)));
    }
    return normalized;
}

bool IdentityStore::valid_username(
    const std::string_view username) {
    if (username.size() < 3 || username.size() > 32) return false;
    return std::all_of(
        username.begin(), username.end(),
        [](const unsigned char c) {
            return std::isalnum(c) != 0 ||
                   c == '.' || c == '_' || c == '-';
        });
}

bool IdentityStore::valid_display_name(
    const std::string_view display_name) {
    if (display_name.empty() || display_name.size() > 64) return false;
    return std::none_of(
        display_name.begin(), display_name.end(),
        [](const unsigned char c) {
            return c < 0x20 || c == 0x7f || c == '	';
        });
}

std::optional<UserInfo> IdentityStore::register_user(
    std::string username,
    std::string display_name,
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
    UserInfo user{
        .id = security::random_hex(16),
        .username = normalized,
        .display_name = std::move(display_name),
        .created_unix = unix_now()};

    if (database_) {
        if (!database_->query(
                 "SELECT id FROM ogl_users WHERE username=?",
                 {normalized}).empty()) {
            reason = "username-exists";
            return std::nullopt;
        }
        try {
            database_->execute(
                "INSERT INTO ogl_users"
                "(id,username,display_name,password_hash,created_unix)"
                " VALUES(?,?,?,?,?)",
                {user.id, user.username, user.display_name,
                 password_hash,
                 std::to_string(user.created_unix)});
        } catch (...) {
            if (!database_->query(
                     "SELECT id FROM ogl_users WHERE username=?",
                     {normalized}).empty()) {
                reason = "username-exists";
                return std::nullopt;
            }
            throw;
        }
        reason.clear();
        return user;
    }

    StoredUser stored{
        .public_info = user,
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

std::optional<UserInfo> IdentityStore::authenticate(
    const std::string_view username,
    const std::string_view password) const {
    const auto normalized = normalize_username(username);

    if (database_) {
        const auto rows = database_->query(
            "SELECT id,username,display_name,password_hash,created_unix "
            "FROM ogl_users WHERE username=?",
            {normalized});
        if (rows.empty()) return std::nullopt;
        const auto id = cell(rows.front(), "id");
        const auto stored_username =
            cell(rows.front(), "username");
        const auto display =
            cell(rows.front(), "display_name");
        const auto hash =
            cell(rows.front(), "password_hash");
        const auto created =
            cell(rows.front(), "created_unix");
        if (!id || !stored_username || !display ||
            !hash || !created ||
            !security::verify_password(password, *hash)) {
            return std::nullopt;
        }
        return UserInfo{
            .id = *id,
            .username = *stored_username,
            .display_name = *display,
            .created_unix = std::stoll(*created)};
    }

    StoredUser stored;
    {
        std::scoped_lock lock(mutex_);
        const auto it = by_username_.find(normalized);
        if (it == by_username_.end()) return std::nullopt;
        stored = it->second;
    }
    if (!security::verify_password(
            password, stored.password_hash)) {
        return std::nullopt;
    }
    return stored.public_info;
}

std::optional<UserInfo> IdentityStore::find_by_id(
    const std::string_view id) const {
    if (database_) {
        const auto rows = database_->query(
            "SELECT id,username,display_name,created_unix "
            "FROM ogl_users WHERE id=?",
            {std::string{id}});
        if (rows.empty()) return std::nullopt;
        const auto user_id = cell(rows.front(), "id");
        const auto username = cell(rows.front(), "username");
        const auto display = cell(rows.front(), "display_name");
        const auto created = cell(rows.front(), "created_unix");
        if (!user_id || !username || !display || !created) {
            return std::nullopt;
        }
        return UserInfo{
            .id = *user_id,
            .username = *username,
            .display_name = *display,
            .created_unix = std::stoll(*created)};
    }

    std::scoped_lock lock(mutex_);
    const auto id_it =
        username_by_id_.find(std::string{id});
    if (id_it == username_by_id_.end()) return std::nullopt;
    const auto user_it = by_username_.find(id_it->second);
    return user_it == by_username_.end()
               ? std::nullopt
               : std::optional<UserInfo>{
                     user_it->second.public_info};
}

std::vector<UserInfo> IdentityStore::list() const {
    if (database_) {
        const auto rows = database_->query(
            "SELECT id,username,display_name,created_unix "
            "FROM ogl_users ORDER BY created_unix,id");
        std::vector<UserInfo> users;
        users.reserve(rows.size());
        for (const auto& row : rows) {
            const auto id = cell(row, "id");
            const auto username = cell(row, "username");
            const auto display = cell(row, "display_name");
            const auto created = cell(row, "created_unix");
            if (!id || !username || !display || !created) continue;
            users.push_back({
                .id = *id,
                .username = *username,
                .display_name = *display,
                .created_unix = std::stoll(*created)});
        }
        return users;
    }

    std::scoped_lock lock(mutex_);
    std::vector<UserInfo> users;
    users.reserve(by_username_.size());
    for (const auto& [_, stored] : by_username_) {
        users.push_back(stored.public_info);
    }
    std::sort(
        users.begin(), users.end(),
        [](const UserInfo& a, const UserInfo& b) {
            return a.created_unix < b.created_unix;
        });
    return users;
}

std::size_t IdentityStore::count() const {
    if (database_) {
        const auto value =
            database_->scalar("SELECT COUNT(*) AS count FROM ogl_users");
        return value
                   ? static_cast<std::size_t>(std::stoull(*value))
                   : 0U;
    }

    std::scoped_lock lock(mutex_);
    return by_username_.size();
}

void IdentityStore::load() {
    if (database_) return;
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
            StoredUser stored{
                .public_info = {
                    .id = fields[0],
                    .username =
                        normalize_username(fields[1]),
                    .display_name = fields[2],
                    .created_unix =
                        std::stoll(fields[4])},
                .password_hash = fields[3]};
            if (!valid_username(stored.public_info.username) ||
                !valid_display_name(
                    stored.public_info.display_name) ||
                stored.public_info.id.empty()) {
                continue;
            }
            username_by_id_[stored.public_info.id] =
                stored.public_info.username;
            by_username_[stored.public_info.username] =
                std::move(stored);
        } catch (...) {
        }
    }
}

void IdentityStore::persist_locked() const {
    if (database_) return;
    const std::filesystem::path path(path_);
    if (path.has_parent_path()) {
        std::filesystem::create_directories(
            path.parent_path());
    }
    const auto temporary = path.string() + ".tmp";
    std::ofstream output(
        temporary, std::ios::trunc);
    if (!output) {
        throw std::runtime_error(
            "cannot write identity store");
    }
    output << "# OpenGenesisLINK identity store v1
";
    std::vector<StoredUser> users;
    users.reserve(by_username_.size());
    for (const auto& [_, stored] : by_username_) {
        users.push_back(stored);
    }
    std::sort(
        users.begin(), users.end(),
        [](const StoredUser& a, const StoredUser& b) {
            return a.public_info.created_unix <
                   b.public_info.created_unix;
        });
    for (const auto& stored : users) {
        const auto& user = stored.public_info;
        output << user.id << '	'
               << user.username << '	'
               << user.display_name << '	'
               << stored.password_hash << '	'
               << user.created_unix << '
';
    }
    output.close();
    if (!output) {
        throw std::runtime_error(
            "cannot flush identity store");
    }
    opengenesis::platform::replace_file(
        temporary, path);
}

} // namespace opengenesis::core
