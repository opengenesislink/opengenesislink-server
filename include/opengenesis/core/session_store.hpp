#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace opengenesis::core {

struct AuthSession {
    std::string id;
    std::string user_id;
    std::int64_t issued_unix{0};
    std::int64_t expires_unix{0};
};

struct CreatedSession {
    std::string token;
    AuthSession session;
};

class SessionStore final {
public:
    explicit SessionStore(std::string path,
                          std::chrono::seconds lifetime = std::chrono::hours{24});

    [[nodiscard]] CreatedSession create(std::string user_id);
    [[nodiscard]] std::optional<AuthSession> find(std::string_view token);
    bool revoke(std::string_view token);
    std::size_t purge_expired();
    [[nodiscard]] std::size_t active_count();

private:
    struct StoredSession {
        AuthSession public_info;
        std::string token_hash;
    };

    void load();
    void persist_locked() const;
    void purge_expired_locked(std::int64_t now);

    std::string path_;
    std::chrono::seconds lifetime_;
    mutable std::mutex mutex_;
    std::unordered_map<std::string, StoredSession> by_hash_;
};

} // namespace opengenesis::core
