#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include <memory>

namespace opengenesis::storage { class DatabasePool; }

namespace opengenesis::core {

struct UserInfo {
    std::string id;
    std::string username;
    std::string display_name;
    std::int64_t created_unix{0};
};

class IdentityStore final {
public:
    explicit IdentityStore(std::string path);
    explicit IdentityStore(std::shared_ptr<storage::DatabasePool> database);

    [[nodiscard]] std::optional<UserInfo> register_user(std::string username,
                                                        std::string display_name,
                                                        std::string_view password,
                                                        std::string& reason);
    [[nodiscard]] std::optional<UserInfo> authenticate(std::string_view username,
                                                       std::string_view password) const;
    [[nodiscard]] std::optional<UserInfo> find_by_id(std::string_view id) const;
    [[nodiscard]] std::vector<UserInfo> list() const;
    [[nodiscard]] std::size_t count() const;

private:
    struct StoredUser {
        UserInfo public_info;
        std::string password_hash;
    };

    void load();
    void persist_locked() const;
    [[nodiscard]] static std::string normalize_username(std::string_view username);
    [[nodiscard]] static bool valid_username(std::string_view username);
    [[nodiscard]] static bool valid_display_name(std::string_view display_name);

    std::string path_;
    std::shared_ptr<storage::DatabasePool> database_;
    mutable std::mutex mutex_;
    std::unordered_map<std::string, StoredUser> by_username_;
    std::unordered_map<std::string, std::string> username_by_id_;
};

} // namespace opengenesis::core
