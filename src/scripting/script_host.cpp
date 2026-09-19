#include "opengenesis/scripting/script_host.hpp"

#include <stdexcept>
#include <string>
#include <utility>

namespace opengenesis::scripting {

ScriptHost::ScriptHost(std::shared_ptr<core::IdentityStore> identities,
                       std::shared_ptr<core::FriendsStore> friends,
                       std::shared_ptr<core::MessageStore> messages,
                       std::shared_ptr<core::NotificationStore> notifications)
    : identities_(std::move(identities)),
      friends_(std::move(friends)),
      messages_(std::move(messages)),
      notifications_(std::move(notifications)) {
    if (!identities_ || !friends_ || !messages_ || !notifications_) {
        throw std::invalid_argument("ScriptHost dependencies required");
    }
}

ScriptHostResult ScriptHost::apply(
    const std::string_view owner_user_id,
    const std::string_view script_id,
    const std::vector<ScriptAction>& actions) const {
    ScriptHostResult result;

    for (const auto& action : actions) {
        if (action.type == ScriptActionType::notify_owner) {
            if (action.value.empty()) {
                result.errors.push_back("notify-empty");
                continue;
            }
            notifications_->push(std::string{owner_user_id}, "script",
                                 "Script notification", action.value,
                                 std::string{script_id});
            ++result.applied;
            continue;
        }

        if (action.type == ScriptActionType::direct_message) {
            const auto split = action.value.find('\n');
            if (split == std::string::npos) {
                result.errors.push_back("message-invalid");
                continue;
            }
            const auto target = action.value.substr(0, split);
            const auto text = action.value.substr(split + 1U);
            if (!identities_->find_by_id(target)) {
                result.errors.push_back("message-recipient-not-found");
                continue;
            }
            if (!friends_->are_friends(owner_user_id, target)) {
                result.errors.push_back("message-recipient-not-friend");
                continue;
            }

            std::string reason;
            const auto message = messages_->send(
                std::string{owner_user_id}, target, text, reason);
            if (!message) {
                result.errors.push_back("message-" + reason);
                continue;
            }
            notifications_->push(target, "direct_message",
                                 "New Script message", text,
                                 message->id);
            ++result.applied;
        }
    }

    return result;
}

} // namespace opengenesis::scripting
