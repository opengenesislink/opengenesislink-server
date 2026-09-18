#pragma once

#include "opengenesis/compat/hypergrid/session_store.hpp"
#include "opengenesis/compat/hypergrid/xmlrpc.hpp"
#include "opengenesis/core/identity_store.hpp"
#include "opengenesis/core/message_store.hpp"
#include "opengenesis/core/notification_store.hpp"

#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

namespace opengenesis::compat::hypergrid {

class HypergridInstantMessageAdapter final {
public:
    HypergridInstantMessageAdapter(std::shared_ptr<core::IdentityStore> identities,
                                   std::shared_ptr<core::MessageStore> messages,
                                   std::shared_ptr<core::NotificationStore> notifications,
                                   std::shared_ptr<HypergridSessionStore> sessions);

    [[nodiscard]] std::unordered_map<std::string, std::string> handle_incoming(
        const XmlRpcCall& call) const;

    [[nodiscard]] bool send_remote(std::string_view sender_native_id,
                                   std::string_view sender_name,
                                   std::string_view target_agent_id,
                                   std::string_view text,
                                   std::string& reason) const;

private:
    [[nodiscard]] std::optional<std::string> native_user_for_legacy(
        std::string_view legacy_uuid) const;

    std::shared_ptr<core::IdentityStore> identities_;
    std::shared_ptr<core::MessageStore> messages_;
    std::shared_ptr<core::NotificationStore> notifications_;
    std::shared_ptr<HypergridSessionStore> sessions_;
};

} // namespace opengenesis::compat::hypergrid
