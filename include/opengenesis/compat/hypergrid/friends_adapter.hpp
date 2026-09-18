#pragma once

#include "opengenesis/compat/hypergrid/session_store.hpp"
#include "opengenesis/core/friends_store.hpp"
#include "opengenesis/core/identity_store.hpp"
#include "opengenesis/core/notification_store.hpp"
#include "opengenesis/core/presence_store.hpp"

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace opengenesis::compat::hypergrid {

class HypergridFriendsAdapter final {
public:
    HypergridFriendsAdapter(std::shared_ptr<core::IdentityStore> identities,
                            std::shared_ptr<core::FriendsStore> friends,
                            std::shared_ptr<core::PresenceStore> presences,
                            std::shared_ptr<core::NotificationStore> notifications,
                            std::shared_ptr<HypergridSessionStore> sessions);

    [[nodiscard]] std::string handle_form(std::string_view body) const;

private:
    [[nodiscard]] std::optional<std::string> native_user_for_legacy(
        std::string_view legacy_uuid) const;
    [[nodiscard]] static std::string remote_key(std::string_view value);
    [[nodiscard]] static std::string uui_secret(std::string_view value);
    [[nodiscard]] bool verified(const std::unordered_map<std::string, std::string>& fields) const;

    std::shared_ptr<core::IdentityStore> identities_;
    std::shared_ptr<core::FriendsStore> friends_;
    std::shared_ptr<core::PresenceStore> presences_;
    std::shared_ptr<core::NotificationStore> notifications_;
    std::shared_ptr<HypergridSessionStore> sessions_;
};

[[nodiscard]] std::unordered_map<std::string, std::string> parse_form_urlencoded(
    std::string_view body);

} // namespace opengenesis::compat::hypergrid
