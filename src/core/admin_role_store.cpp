#include "opengenesis/core/admin_role_store.hpp"
#include "opengenesis/platform/filesystem.hpp"
#include "opengenesis/storage/database.hpp"

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
        fields.push_back(line.substr(
            start,
            end == std::string::npos
                ? std::string::npos
                : end - start));
        if (end == std::string::npos) break;
        start = end + 1U;
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

AdminRoleRecord row_to_role(const storage::DatabaseRow& row) {
    return {
        .user_id = cell(row, "user_id").value_or(""),
        .role_name = cell(row, "role_name").value_or(""),
        .granted_by = cell(row, "granted_by").value_or(""),
        .granted_unix =
            std::stoll(cell(row, "granted_unix").value_or("0"))};
}

} // namespace

AdminRoleStore::AdminRoleStore(std::string path)
    : path_(std::move(path)) {
    load();
}

AdminRoleStore::AdminRoleStore(
    std::shared_ptr<storage::DatabasePool> database)
    : database_(std::move(database)) {
    if (!database_) {
        throw std::invalid_argument("admin role database is required");
    }
}

int AdminRoleStore::role_level(const std::string_view role) noexcept {
    if (role == "moderator") return 1;
    if (role == "operator") return 2;
    if (role == "administrator") return 3;
    return 0;
}

bool AdminRoleStore::valid_role(const std::string_view role) noexcept {
    return role_level(role) != 0;
}

bool AdminRoleStore::grant(
    std::string user_id,
    std::string role_name,
    std::string granted_by,
    std::string& reason) {
    if (user_id.empty() || granted_by.empty() ||
        !valid_role(role_name)) {
        reason = "invalid-admin-role";
        return false;
    }

    AdminRoleRecord record{
        .user_id = std::move(user_id),
        .role_name = std::move(role_name),
        .granted_by = std::move(granted_by),
        .granted_unix = unix_now()};

    if (database_) {
        const auto existing = database_->query(
            "SELECT user_id FROM ogl_admin_roles WHERE user_id=?",
            {record.user_id});
        if (existing.empty()) {
            database_->execute(
                "INSERT INTO ogl_admin_roles"
                "(user_id,role_name,granted_by,granted_unix)"
                " VALUES(?,?,?,?)",
                {record.user_id, record.role_name,
                 record.granted_by,
                 std::to_string(record.granted_unix)});
        } else {
            database_->execute(
                "UPDATE ogl_admin_roles SET "
                "role_name=?,granted_by=?,granted_unix=? "
                "WHERE user_id=?",
                {record.role_name, record.granted_by,
                 std::to_string(record.granted_unix),
                 record.user_id});
        }
        reason.clear();
        return true;
    }

    std::scoped_lock lock(mutex_);
    roles_[record.user_id] = record;
    persist_locked();
    reason.clear();
    return true;
}

bool AdminRoleStore::revoke(const std::string_view user_id) {
    if (user_id.empty()) return false;

    if (database_) {
        const auto rows = database_->query(
            "SELECT user_id FROM ogl_admin_roles WHERE user_id=?",
            {std::string{user_id}});
        if (rows.empty()) return false;
        database_->execute(
            "DELETE FROM ogl_admin_roles WHERE user_id=?",
            {std::string{user_id}});
        return true;
    }

    std::scoped_lock lock(mutex_);
    const auto erased = roles_.erase(std::string{user_id});
    if (erased != 0U) persist_locked();
    return erased != 0U;
}

std::optional<AdminRoleRecord> AdminRoleStore::find(
    const std::string_view user_id) const {
    if (database_) {
        const auto rows = database_->query(
            "SELECT user_id,role_name,granted_by,granted_unix "
            "FROM ogl_admin_roles WHERE user_id=?",
            {std::string{user_id}});
        if (rows.empty()) return std::nullopt;
        return row_to_role(rows.front());
    }

    std::scoped_lock lock(mutex_);
    const auto it = roles_.find(std::string{user_id});
    return it == roles_.end()
               ? std::nullopt
               : std::optional<AdminRoleRecord>{it->second};
}

std::vector<AdminRoleRecord> AdminRoleStore::list() const {
    if (database_) {
        const auto rows = database_->query(
            "SELECT user_id,role_name,granted_by,granted_unix "
            "FROM ogl_admin_roles ORDER BY role_name,user_id");
        std::vector<AdminRoleRecord> result;
        result.reserve(rows.size());
        for (const auto& row : rows) {
            result.push_back(row_to_role(row));
        }
        return result;
    }

    std::scoped_lock lock(mutex_);
    std::vector<AdminRoleRecord> result;
    result.reserve(roles_.size());
    for (const auto& [_, record] : roles_) {
        result.push_back(record);
    }
    std::sort(
        result.begin(), result.end(),
        [](const AdminRoleRecord& left,
           const AdminRoleRecord& right) {
            if (left.role_name != right.role_name) {
                return left.role_name < right.role_name;
            }
            return left.user_id < right.user_id;
        });
    return result;
}

bool AdminRoleStore::has_role(
    const std::string_view user_id,
    const std::string_view minimum_role) const {
    const auto minimum = role_level(minimum_role);
    if (minimum == 0 || user_id.empty()) return false;
    const auto role = find(user_id);
    return role && role_level(role->role_name) >= minimum;
}

void AdminRoleStore::load() {
    if (database_ || path_.empty()) return;
    std::scoped_lock lock(mutex_);
    roles_.clear();

    std::ifstream input(path_);
    if (!input) return;
    std::string line;
    while (std::getline(input, line)) {
        if (line.empty() || line.front() == '#') continue;
        const auto fields = split_tab(line);
        if (fields.size() != 4U) continue;
        try {
            AdminRoleRecord record{
                .user_id = fields[0],
                .role_name = fields[1],
                .granted_by = fields[2],
                .granted_unix = std::stoll(fields[3])};
            if (record.user_id.empty() ||
                record.granted_by.empty() ||
                !valid_role(record.role_name)) {
                continue;
            }
            roles_[record.user_id] = std::move(record);
        } catch (...) {
        }
    }
}

void AdminRoleStore::persist_locked() const {
    if (database_ || path_.empty()) return;
    const std::filesystem::path path(path_);
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path());
    }
    const auto temporary = path.string() + ".tmp";
    std::ofstream output(temporary, std::ios::trunc);
    if (!output) {
        throw std::runtime_error("cannot write admin role store");
    }
    output << "# OpenGenesisLINK admin roles v1\n";
    for (const auto& [_, record] : roles_) {
        output << record.user_id << '\t'
               << record.role_name << '\t'
               << record.granted_by << '\t'
               << record.granted_unix << '\n';
    }
    output.close();
    if (!output) {
        throw std::runtime_error("cannot flush admin role store");
    }
    opengenesis::platform::replace_file(temporary, path);
}

} // namespace opengenesis::core
