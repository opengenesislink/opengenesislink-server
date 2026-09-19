#pragma once

#include "opengenesis/core/friends_store.hpp"
#include "opengenesis/core/identity_store.hpp"
#include "opengenesis/core/message_store.hpp"
#include "opengenesis/core/notification_store.hpp"
#include "opengenesis/scripting/script_vm.hpp"
#include "opengenesis/scripting/world_action_queue.hpp"

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace opengenesis::scripting {

struct ScriptHostResult {
    std::size_t applied{0};
    std::vector<std::string> errors;
};

class ScriptHost final {
public:
    ScriptHost(std::shared_ptr<core::IdentityStore> identities,
               std::shared_ptr<core::FriendsStore> friends,
               std::shared_ptr<core::MessageStore> messages,
               std::shared_ptr<core::NotificationStore> notifications,
               std::shared_ptr<ScriptWorldActionQueue> world_actions = {});

    [[nodiscard]] ScriptHostResult apply(std::string_view owner_user_id,
                                         std::string_view script_id,
                                         const std::vector<ScriptAction>& actions,
                                         std::string_view object_id = {}) const;

private:
    std::shared_ptr<core::IdentityStore> identities_;
    std::shared_ptr<core::FriendsStore> friends_;
    std::shared_ptr<core::MessageStore> messages_;
    std::shared_ptr<core::NotificationStore> notifications_;
    std::shared_ptr<ScriptWorldActionQueue> world_actions_;
};

} // namespace opengenesis::scripting
