#pragma once

#include <chrono>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace opengenesis::core {

enum class GroupPower : std::uint32_t {
    invite = 1U << 0U,
    eject = 1U << 1U,
    roles = 1U << 2U,
    land = 1U << 3U,
    objects = 1U << 4U,
    chat = 1U << 5U,
    notice = 1U << 6U
};

inline constexpr std::uint32_t group_power_all = 0x7fU;

struct GroupInfo {
    std::string id;
    std::string name;
    std::string founder_user_id;
    std::int64_t created_unix{0};
};

struct GroupMember {
    std::string group_id;
    std::string user_id;
    std::string role;
    std::uint32_t powers{0};
    std::int64_t joined_unix{0};
};

enum class GroupInviteState {
    pending,
    accepted,
    revoked,
    expired
};

struct GroupInvite {
    std::string id;
    std::string group_id;
    std::string invited_user_id;
    std::string invited_by_user_id;
    std::string role{"member"};
    GroupInviteState state{GroupInviteState::pending};
    std::int64_t created_unix{0};
    std::int64_t expires_unix{0};
    std::int64_t resolved_unix{0};
};

class GroupStore final {
public:
    explicit GroupStore(std::string path);

    [[nodiscard]] std::optional<GroupInfo> create(
        std::string founder,
        std::string name,
        std::string& reason);

    [[nodiscard]] std::optional<GroupMember> add_member(
        std::string_view actor,
        std::string_view group_id,
        std::string user_id,
        std::string role,
        std::string& reason);

    bool remove_member(
        std::string_view actor,
        std::string_view group_id,
        std::string_view user_id,
        std::string& reason);

    bool set_role(
        std::string_view actor,
        std::string_view group_id,
        std::string_view user_id,
        std::string role,
        std::string& reason);

    [[nodiscard]] std::optional<GroupInvite> invite(
        std::string_view actor,
        std::string_view group_id,
        std::string invited_user_id,
        std::string role,
        std::chrono::seconds lifetime,
        std::string& reason);

    [[nodiscard]] std::optional<GroupMember> accept_invite(
        std::string_view invited_user_id,
        std::string_view invite_id,
        std::string& reason);

    bool revoke_invite(
        std::string_view actor,
        std::string_view invite_id,
        std::string& reason);

    std::size_t purge_expired_invites(std::int64_t now_unix);

    [[nodiscard]] std::vector<GroupInvite> invites_for_user(
        std::string_view invited_user_id) const;

    [[nodiscard]] bool is_member(
        std::string_view group_id,
        std::string_view user_id) const;

    [[nodiscard]] bool has_power(
        std::string_view group_id,
        std::string_view user_id,
        GroupPower power) const;

    [[nodiscard]] std::optional<GroupInfo> find(
        std::string_view id) const;
    [[nodiscard]] std::vector<GroupInfo> list_for_user(
        std::string_view user) const;
    [[nodiscard]] std::vector<GroupMember> members(
        std::string_view group_id) const;
    [[nodiscard]] std::vector<std::string> group_ids_for_user(
        std::string_view user) const;
    [[nodiscard]] std::size_t count() const;

private:
    void load();
    void persist_locked() const;
    static std::uint32_t role_powers(std::string_view role);
    static std::string member_key(
        std::string_view group_id,
        std::string_view user_id);

    std::string path_;
    mutable std::mutex mutex_;
    std::unordered_map<std::string, GroupInfo> groups_;
    std::unordered_map<std::string, GroupMember> members_;
    std::unordered_map<std::string, GroupInvite> invites_;
};

[[nodiscard]] std::string_view group_invite_state_name(
    GroupInviteState state) noexcept;

} // namespace opengenesis::core
