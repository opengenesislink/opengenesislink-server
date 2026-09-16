#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace opengenesis::core {

struct FriendRelation {
    std::string id;
    std::string user_a;
    std::string user_b;
    std::string requested_by;
    std::string status{"pending"};
    std::int64_t created_unix{0};
    std::int64_t updated_unix{0};
};

class FriendsStore final {
public:
    explicit FriendsStore(std::string path);

    [[nodiscard]] std::optional<FriendRelation> request(std::string from_user,
                                                        std::string to_user,
                                                        std::string& reason);
    [[nodiscard]] std::optional<FriendRelation> accept(std::string user,
                                                       std::string other_user,
                                                       std::string& reason);
    bool remove(std::string_view user, std::string_view other_user);
    [[nodiscard]] bool are_friends(std::string_view user, std::string_view other_user) const;
    [[nodiscard]] std::vector<FriendRelation> list_for_user(std::string_view user) const;
    [[nodiscard]] std::size_t accepted_count() const;
    [[nodiscard]] std::size_t pending_count() const;

private:
    static std::string pair_key(std::string_view a, std::string_view b);
    void load();
    void persist_locked() const;

    std::string path_;
    mutable std::mutex mutex_;
    std::unordered_map<std::string, FriendRelation> by_pair_;
};

} // namespace opengenesis::core
