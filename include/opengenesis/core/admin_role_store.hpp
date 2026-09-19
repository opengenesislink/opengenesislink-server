#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace opengenesis::storage { class DatabasePool; }

namespace opengenesis::core {

struct AdminRoleRecord {
    std::string user_id;
    std::string role_name;
    std::string granted_by;
    std::int64_t granted_unix{0};
};

class AdminRoleStore final {
public:
    explicit AdminRoleStore(std::string path);
    explicit AdminRoleStore(std::shared_ptr<storage::DatabasePool> database);

    [[nodiscard]] bool grant(std::string user_id,
                             std::string role_name,
                             std::string granted_by,
                             std::string& reason);
    bool revoke(std::string_view user_id);
    [[nodiscard]] std::optional<AdminRoleRecord> find(
        std::string_view user_id) const;
    [[nodiscard]] std::vector<AdminRoleRecord> list() const;
    [[nodiscard]] bool has_role(std::string_view user_id,
                                std::string_view minimum_role) const;

    [[nodiscard]] static bool valid_role(std::string_view role) noexcept;

private:
    void load();
    void persist_locked() const;
    [[nodiscard]] static int role_level(std::string_view role) noexcept;

    std::string path_;
    std::shared_ptr<storage::DatabasePool> database_;
    mutable std::mutex mutex_;
    std::unordered_map<std::string, AdminRoleRecord> roles_;
};

} // namespace opengenesis::core
