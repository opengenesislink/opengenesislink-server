#include "opengenesis/core/group_store.hpp"

#include "opengenesis/platform/filesystem.hpp"
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

std::string hex(std::string_view value) {
    constexpr char digits[] = "0123456789abcdef";
    std::string output(value.size() * 2U, '0');
    for (std::size_t i = 0; i < value.size(); ++i) {
        const auto c = static_cast<unsigned char>(value[i]);
        output[i * 2U] = digits[c >> 4U];
        output[i * 2U + 1U] = digits[c & 15U];
    }
    return output;
}

unsigned char nibble(const char c) {
    if (c >= '0' && c <= '9') {
        return static_cast<unsigned char>(c - '0');
    }
    if (c >= 'a' && c <= 'f') {
        return static_cast<unsigned char>(c - 'a' + 10);
    }
    if (c >= 'A' && c <= 'F') {
        return static_cast<unsigned char>(c - 'A' + 10);
    }
    throw std::runtime_error("invalid hex");
}

std::string unhex(const std::string_view value) {
    if ((value.size() % 2U) != 0U) {
        throw std::runtime_error("invalid hex");
    }
    std::string output(value.size() / 2U, '\0');
    for (std::size_t i = 0; i < output.size(); ++i) {
        output[i] = static_cast<char>(
            (nibble(value[i * 2U]) << 4U) |
            nibble(value[i * 2U + 1U]));
    }
    return output;
}

std::vector<std::string> split_tab(const std::string& line) {
    std::vector<std::string> fields;
    std::size_t start = 0;
    while (true) {
        const auto end = line.find('\t', start);
        fields.push_back(
            line.substr(
                start,
                end == std::string::npos
                    ? std::string::npos
                    : end - start));
        if (end == std::string::npos) break;
        start = end + 1U;
    }
    return fields;
}

bool valid_name(const std::string_view value) {
    return !value.empty() && value.size() <= 96U &&
           std::none_of(
               value.begin(), value.end(),
               [](const unsigned char c) {
                   return c < 0x20U || c == 0x7fU;
               });
}

bool valid_role(const std::string_view role) {
    return role == "member" || role == "officer";
}

GroupInviteState parse_invite_state(
    const std::string_view state) {
    if (state == "accepted") {
        return GroupInviteState::accepted;
    }
    if (state == "revoked") {
        return GroupInviteState::revoked;
    }
    if (state == "expired") {
        return GroupInviteState::expired;
    }
    return GroupInviteState::pending;
}

} // namespace

std::string_view group_invite_state_name(
    const GroupInviteState state) noexcept {
    switch (state) {
        case GroupInviteState::pending: return "pending";
        case GroupInviteState::accepted: return "accepted";
        case GroupInviteState::revoked: return "revoked";
        case GroupInviteState::expired: return "expired";
    }
    return "expired";
}

GroupStore::GroupStore(std::string path)
    : path_(std::move(path)) {
    load();
}

std::string GroupStore::member_key(
    const std::string_view group_id,
    const std::string_view user_id) {
    return std::string{group_id} + "|" + std::string{user_id};
}

std::uint32_t GroupStore::role_powers(
    const std::string_view role) {
    if (role == "owner") return group_power_all;
    if (role == "officer") {
        return static_cast<std::uint32_t>(GroupPower::invite) |
               static_cast<std::uint32_t>(GroupPower::eject) |
               static_cast<std::uint32_t>(GroupPower::land) |
               static_cast<std::uint32_t>(GroupPower::objects) |
               static_cast<std::uint32_t>(GroupPower::chat) |
               static_cast<std::uint32_t>(GroupPower::notice);
    }
    return static_cast<std::uint32_t>(GroupPower::chat);
}

std::optional<GroupInfo> GroupStore::create(
    std::string founder,
    std::string name,
    std::string& reason) {
    if (founder.empty() || !valid_name(name)) {
        reason = "invalid-group";
        return std::nullopt;
    }

    std::scoped_lock lock(mutex_);
    for (const auto& [_, group] : groups_) {
        if (group.name == name) {
            reason = "group-name-exists";
            return std::nullopt;
        }
    }

    GroupInfo group{
        .id = security::random_hex(16),
        .name = std::move(name),
        .founder_user_id = founder,
        .created_unix = unix_now()};
    groups_[group.id] = group;
    members_[member_key(group.id, founder)] = {
        .group_id = group.id,
        .user_id = std::move(founder),
        .role = "owner",
        .powers = group_power_all,
        .joined_unix = group.created_unix};
    persist_locked();
    reason.clear();
    return group;
}

std::optional<GroupMember> GroupStore::add_member(
    const std::string_view actor,
    const std::string_view group_id,
    std::string user_id,
    std::string role,
    std::string& reason) {
    if (user_id.empty() || !valid_role(role)) {
        reason = "invalid-member";
        return std::nullopt;
    }

    std::scoped_lock lock(mutex_);
    if (!groups_.contains(std::string{group_id})) {
        reason = "group-not-found";
        return std::nullopt;
    }
    const auto actor_it =
        members_.find(member_key(group_id, actor));
    if (actor_it == members_.end() ||
        (actor_it->second.powers &
         static_cast<std::uint32_t>(GroupPower::invite)) == 0U) {
        reason = "permission-denied";
        return std::nullopt;
    }

    const auto key = member_key(group_id, user_id);
    if (members_.contains(key)) {
        reason = "already-member";
        return std::nullopt;
    }

    GroupMember member{
        .group_id = std::string{group_id},
        .user_id = std::move(user_id),
        .role = std::move(role),
        .powers = 0,
        .joined_unix = unix_now()};
    member.powers = role_powers(member.role);
    members_[key] = member;
    persist_locked();
    reason.clear();
    return member;
}

bool GroupStore::remove_member(
    const std::string_view actor,
    const std::string_view group_id,
    const std::string_view user_id,
    std::string& reason) {
    std::scoped_lock lock(mutex_);
    const auto target =
        members_.find(member_key(group_id, user_id));
    if (target == members_.end()) {
        reason = "member-not-found";
        return false;
    }
    if (target->second.role == "owner") {
        reason = "cannot-remove-owner";
        return false;
    }
    const auto actor_it =
        members_.find(member_key(group_id, actor));
    if (actor != user_id &&
        (actor_it == members_.end() ||
         (actor_it->second.powers &
          static_cast<std::uint32_t>(GroupPower::eject)) == 0U)) {
        reason = "permission-denied";
        return false;
    }

    members_.erase(target);
    persist_locked();
    reason.clear();
    return true;
}

bool GroupStore::set_role(
    const std::string_view actor,
    const std::string_view group_id,
    const std::string_view user_id,
    std::string role,
    std::string& reason) {
    if (!valid_role(role)) {
        reason = "invalid-role";
        return false;
    }

    std::scoped_lock lock(mutex_);
    const auto actor_it =
        members_.find(member_key(group_id, actor));
    const auto member_it =
        members_.find(member_key(group_id, user_id));
    if (actor_it == members_.end() ||
        member_it == members_.end()) {
        reason = "member-not-found";
        return false;
    }
    if ((actor_it->second.powers &
         static_cast<std::uint32_t>(GroupPower::roles)) == 0U ||
        member_it->second.role == "owner") {
        reason = "permission-denied";
        return false;
    }

    member_it->second.role = std::move(role);
    member_it->second.powers =
        role_powers(member_it->second.role);
    persist_locked();
    reason.clear();
    return true;
}

std::optional<GroupInvite> GroupStore::invite(
    const std::string_view actor,
    const std::string_view group_id,
    std::string invited_user_id,
    std::string role,
    std::chrono::seconds lifetime,
    std::string& reason) {
    if (invited_user_id.empty() ||
        !valid_role(role)) {
        reason = "invalid-group-invite";
        return std::nullopt;
    }

    lifetime = std::clamp(
        lifetime,
        std::chrono::seconds{60},
        std::chrono::hours{24 * 30});

    std::scoped_lock lock(mutex_);
    if (!groups_.contains(std::string{group_id})) {
        reason = "group-not-found";
        return std::nullopt;
    }
    const auto actor_it =
        members_.find(member_key(group_id, actor));
    if (actor_it == members_.end() ||
        (actor_it->second.powers &
         static_cast<std::uint32_t>(GroupPower::invite)) == 0U) {
        reason = "permission-denied";
        return std::nullopt;
    }
    if (members_.contains(
            member_key(group_id, invited_user_id))) {
        reason = "already-member";
        return std::nullopt;
    }

    const auto now = unix_now();
    for (auto& [_, invite] : invites_) {
        if (invite.group_id == group_id &&
            invite.invited_user_id == invited_user_id &&
            invite.state == GroupInviteState::pending &&
            invite.expires_unix > now) {
            reason = "invite-already-pending";
            return std::nullopt;
        }
    }

    GroupInvite invite{
        .id = security::random_hex(16),
        .group_id = std::string{group_id},
        .invited_user_id = std::move(invited_user_id),
        .invited_by_user_id = std::string{actor},
        .role = std::move(role),
        .state = GroupInviteState::pending,
        .created_unix = now,
        .expires_unix = now + lifetime.count(),
        .resolved_unix = 0};
    invites_[invite.id] = invite;
    persist_locked();
    reason.clear();
    return invite;
}

std::optional<GroupMember> GroupStore::accept_invite(
    const std::string_view invited_user_id,
    const std::string_view invite_id,
    std::string& reason) {
    std::scoped_lock lock(mutex_);
    const auto invite_it =
        invites_.find(std::string{invite_id});
    if (invite_it == invites_.end()) {
        reason = "invite-not-found";
        return std::nullopt;
    }

    auto& invite = invite_it->second;
    const auto now = unix_now();
    if (invite.state != GroupInviteState::pending) {
        reason = "invite-not-pending";
        return std::nullopt;
    }
    if (invite.expires_unix <= now) {
        invite.state = GroupInviteState::expired;
        invite.resolved_unix = now;
        persist_locked();
        reason = "invite-expired";
        return std::nullopt;
    }
    if (invite.invited_user_id != invited_user_id) {
        reason = "invite-user-mismatch";
        return std::nullopt;
    }
    if (!groups_.contains(invite.group_id)) {
        reason = "group-not-found";
        return std::nullopt;
    }

    const auto inviter_it =
        members_.find(
            member_key(
                invite.group_id,
                invite.invited_by_user_id));
    if (inviter_it == members_.end() ||
        (inviter_it->second.powers &
         static_cast<std::uint32_t>(GroupPower::invite)) == 0U) {
        reason = "inviter-no-longer-authorized";
        return std::nullopt;
    }

    const auto key =
        member_key(invite.group_id, invited_user_id);
    if (members_.contains(key)) {
        invite.state = GroupInviteState::accepted;
        invite.resolved_unix = now;
        persist_locked();
        reason = "already-member";
        return std::nullopt;
    }

    GroupMember member{
        .group_id = invite.group_id,
        .user_id = std::string{invited_user_id},
        .role = invite.role,
        .powers = role_powers(invite.role),
        .joined_unix = now};
    members_[key] = member;
    invite.state = GroupInviteState::accepted;
    invite.resolved_unix = now;
    persist_locked();
    reason.clear();
    return member;
}

bool GroupStore::revoke_invite(
    const std::string_view actor,
    const std::string_view invite_id,
    std::string& reason) {
    std::scoped_lock lock(mutex_);
    const auto invite_it =
        invites_.find(std::string{invite_id});
    if (invite_it == invites_.end()) {
        reason = "invite-not-found";
        return false;
    }

    auto& invite = invite_it->second;
    if (invite.state != GroupInviteState::pending) {
        reason = "invite-not-pending";
        return false;
    }
    const auto actor_it =
        members_.find(member_key(invite.group_id, actor));
    if (actor != invite.invited_by_user_id &&
        (actor_it == members_.end() ||
         (actor_it->second.powers &
          static_cast<std::uint32_t>(GroupPower::invite)) == 0U)) {
        reason = "permission-denied";
        return false;
    }

    invite.state = GroupInviteState::revoked;
    invite.resolved_unix = unix_now();
    persist_locked();
    reason.clear();
    return true;
}

std::size_t GroupStore::purge_expired_invites(
    const std::int64_t now_unix) {
    std::scoped_lock lock(mutex_);
    std::size_t changed = 0U;
    for (auto& [_, invite] : invites_) {
        if (invite.state == GroupInviteState::pending &&
            invite.expires_unix <= now_unix) {
            invite.state = GroupInviteState::expired;
            invite.resolved_unix = now_unix;
            ++changed;
        }
    }
    if (changed != 0U) persist_locked();
    return changed;
}

std::vector<GroupInvite> GroupStore::invites_for_user(
    const std::string_view invited_user_id) const {
    std::scoped_lock lock(mutex_);
    std::vector<GroupInvite> result;
    for (const auto& [_, invite] : invites_) {
        if (invite.invited_user_id == invited_user_id) {
            result.push_back(invite);
        }
    }
    std::sort(
        result.begin(), result.end(),
        [](const GroupInvite& left,
           const GroupInvite& right) {
            return left.created_unix > right.created_unix;
        });
    return result;
}

bool GroupStore::is_member(
    const std::string_view group_id,
    const std::string_view user_id) const {
    std::scoped_lock lock(mutex_);
    return members_.contains(
        member_key(group_id, user_id));
}

bool GroupStore::has_power(
    const std::string_view group_id,
    const std::string_view user_id,
    const GroupPower power) const {
    std::scoped_lock lock(mutex_);
    const auto it =
        members_.find(member_key(group_id, user_id));
    return it != members_.end() &&
           (it->second.powers &
            static_cast<std::uint32_t>(power)) != 0U;
}

std::optional<GroupInfo> GroupStore::find(
    const std::string_view id) const {
    std::scoped_lock lock(mutex_);
    const auto it = groups_.find(std::string{id});
    return it == groups_.end()
               ? std::nullopt
               : std::optional<GroupInfo>{it->second};
}

std::vector<GroupInfo> GroupStore::list_for_user(
    const std::string_view user) const {
    std::scoped_lock lock(mutex_);
    std::vector<GroupInfo> result;
    for (const auto& [_, member] : members_) {
        if (member.user_id != user) continue;
        const auto group = groups_.find(member.group_id);
        if (group != groups_.end()) {
            result.push_back(group->second);
        }
    }
    std::sort(
        result.begin(), result.end(),
        [](const GroupInfo& left,
           const GroupInfo& right) {
            return left.name < right.name;
        });
    return result;
}

std::vector<GroupMember> GroupStore::members(
    const std::string_view group_id) const {
    std::scoped_lock lock(mutex_);
    std::vector<GroupMember> result;
    for (const auto& [_, member] : members_) {
        if (member.group_id == group_id) {
            result.push_back(member);
        }
    }
    std::sort(
        result.begin(), result.end(),
        [](const GroupMember& left,
           const GroupMember& right) {
            return left.joined_unix < right.joined_unix;
        });
    return result;
}

std::vector<std::string> GroupStore::group_ids_for_user(
    const std::string_view user) const {
    std::scoped_lock lock(mutex_);
    std::vector<std::string> result;
    for (const auto& [_, member] : members_) {
        if (member.user_id == user) {
            result.push_back(member.group_id);
        }
    }
    std::sort(result.begin(), result.end());
    return result;
}

std::size_t GroupStore::count() const {
    std::scoped_lock lock(mutex_);
    return groups_.size();
}

void GroupStore::load() {
    std::scoped_lock lock(mutex_);
    groups_.clear();
    members_.clear();
    invites_.clear();

    std::ifstream input(path_);
    if (!input) return;

    std::string line;
    while (std::getline(input, line)) {
        if (line.empty() || line[0] == '#') continue;
        const auto fields = split_tab(line);
        try {
            if (fields.size() == 5U &&
                fields[0] == "G") {
                GroupInfo group{
                    .id = fields[1],
                    .name = unhex(fields[2]),
                    .founder_user_id = fields[3],
                    .created_unix = std::stoll(fields[4])};
                groups_[group.id] = std::move(group);
            } else if (fields.size() == 6U &&
                       fields[0] == "M") {
                GroupMember member{
                    .group_id = fields[1],
                    .user_id = fields[2],
                    .role = fields[3],
                    .powers = role_powers(fields[3]),
                    .joined_unix = std::stoll(fields[5])};
                members_[member_key(
                    member.group_id,
                    member.user_id)] = std::move(member);
            } else if (fields.size() == 10U &&
                       fields[0] == "I") {
                GroupInvite invite{
                    .id = fields[1],
                    .group_id = fields[2],
                    .invited_user_id = fields[3],
                    .invited_by_user_id = fields[4],
                    .role = fields[5],
                    .state = parse_invite_state(fields[6]),
                    .created_unix = std::stoll(fields[7]),
                    .expires_unix = std::stoll(fields[8]),
                    .resolved_unix = std::stoll(fields[9])};
                invites_[invite.id] = std::move(invite);
            }
        } catch (...) {
        }
    }
}

void GroupStore::persist_locked() const {
    const std::filesystem::path path(path_);
    if (path.has_parent_path()) {
        std::filesystem::create_directories(
            path.parent_path());
    }
    const auto temporary = path.string() + ".tmp";
    std::ofstream output(temporary, std::ios::trunc);
    if (!output) {
        throw std::runtime_error("cannot write groups");
    }
    output << "# OpenGenesisLINK groups v2\n";

    for (const auto& [_, group] : groups_) {
        output << "G\t" << group.id << '\t'
               << hex(group.name) << '\t'
               << group.founder_user_id << '\t'
               << group.created_unix << '\n';
    }
    for (const auto& [_, member] : members_) {
        output << "M\t" << member.group_id << '\t'
               << member.user_id << '\t'
               << member.role << '\t'
               << member.powers << '\t'
               << member.joined_unix << '\n';
    }
    for (const auto& [_, invite] : invites_) {
        output << "I\t" << invite.id << '\t'
               << invite.group_id << '\t'
               << invite.invited_user_id << '\t'
               << invite.invited_by_user_id << '\t'
               << invite.role << '\t'
               << group_invite_state_name(invite.state) << '\t'
               << invite.created_unix << '\t'
               << invite.expires_unix << '\t'
               << invite.resolved_unix << '\n';
    }

    output.close();
    if (!output) {
        throw std::runtime_error("cannot flush groups");
    }
    platform::replace_file(temporary, path);
}

} // namespace opengenesis::core
