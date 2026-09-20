#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace opengenesis::core {

struct SocialPolicy {
    std::string owner_user_id;
    std::string target_user_id;
    bool blocked{false};
    bool muted{false};
    std::int64_t updated_unix{0};
};

class SocialPolicyStore final {
public:
    explicit SocialPolicyStore(std::string path);

    [[nodiscard]] bool set_blocked(
        std::string owner_user_id,
        std::string target_user_id,
        bool blocked,
        std::string& reason);

    [[nodiscard]] bool set_muted(
        std::string owner_user_id,
        std::string target_user_id,
        bool muted,
        std::string& reason);

    [[nodiscard]] bool blocks(
        std::string_view owner_user_id,
        std::string_view target_user_id) const;

    [[nodiscard]] bool muted(
        std::string_view owner_user_id,
        std::string_view target_user_id) const;

    [[nodiscard]] std::vector<SocialPolicy> list_for_user(
        std::string_view owner_user_id) const;

    [[nodiscard]] std::size_t count() const;

private:
    void load();
    void persist_locked() const;
    static std::string key(
        std::string_view owner_user_id,
        std::string_view target_user_id);

    std::string path_;
    mutable std::mutex mutex_;
    std::unordered_map<std::string, SocialPolicy> policies_;
};

} // namespace opengenesis::core
