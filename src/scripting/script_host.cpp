#include "opengenesis/scripting/script_host.hpp"

#include <charconv>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

namespace opengenesis::scripting {
namespace {

std::optional<std::pair<std::string, std::uint64_t>> object_binding(
    const std::string_view object_id) {
    const auto split = object_id.rfind('/');
    if (split == std::string_view::npos || split == 0 || split + 1U >= object_id.size()) {
        return std::nullopt;
    }
    const auto region = object_id.substr(0, split);
    const auto entity = object_id.substr(split + 1U);
    if (region.size() > 256U || entity.size() > 32U) return std::nullopt;
    std::uint64_t value = 0;
    const auto [end, ec] = std::from_chars(entity.data(), entity.data() + entity.size(), value);
    if (ec != std::errc{} || end != entity.data() + entity.size() || value == 0) {
        return std::nullopt;
    }
    return std::pair<std::string, std::uint64_t>{std::string{region}, value};
}

std::optional<ScriptWorldActionType> world_action_type(const ScriptActionType type) {
    switch (type) {
        case ScriptActionType::world_move: return ScriptWorldActionType::move;
        case ScriptActionType::world_rotate: return ScriptWorldActionType::rotate;
        case ScriptActionType::world_scale: return ScriptWorldActionType::scale;
        case ScriptActionType::world_physics: return ScriptWorldActionType::physics;
        case ScriptActionType::world_chat_say: return ScriptWorldActionType::chat_say;
        case ScriptActionType::world_chat_whisper: return ScriptWorldActionType::chat_whisper;
        case ScriptActionType::world_chat_shout: return ScriptWorldActionType::chat_shout;
        case ScriptActionType::world_query_object: return ScriptWorldActionType::query_object;
        case ScriptActionType::world_query_region: return ScriptWorldActionType::query_region;
        case ScriptActionType::world_query_terrain: return ScriptWorldActionType::query_terrain;
        case ScriptActionType::world_query_nearby: return ScriptWorldActionType::query_nearby;
        default: return std::nullopt;
    }
}

} // namespace

ScriptHost::ScriptHost(std::shared_ptr<core::IdentityStore> identities,
                       std::shared_ptr<core::FriendsStore> friends,
                       std::shared_ptr<core::MessageStore> messages,
                       std::shared_ptr<core::NotificationStore> notifications,
                       std::shared_ptr<ScriptWorldActionQueue> world_actions)
    : identities_(std::move(identities)),
      friends_(std::move(friends)),
      messages_(std::move(messages)),
      notifications_(std::move(notifications)),
      world_actions_(world_actions ? std::move(world_actions)
                                   : std::make_shared<ScriptWorldActionQueue>()) {
    if (!identities_ || !friends_ || !messages_ || !notifications_) {
        throw std::invalid_argument("ScriptHost dependencies required");
    }
}

ScriptHostResult ScriptHost::apply(
    const std::string_view owner_user_id,
    const std::string_view script_id,
    const std::vector<ScriptAction>& actions,
    const std::string_view object_id) const {
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
            continue;
        }

        const auto world_type = world_action_type(action.type);
        if (!world_type) continue;
        const auto binding = object_binding(object_id);
        if (!binding) {
            result.errors.push_back("world-binding-invalid");
            continue;
        }
        if (!world_actions_->enqueue(
                {.id = {},
                 .region_id = binding->first,
                 .entity_id = binding->second,
                 .owner_user_id = std::string{owner_user_id},
                 .script_id = std::string{script_id},
                 .type = *world_type,
                 .payload = action.value})) {
            result.errors.push_back("world-action-queue-rejected");
            continue;
        }
        ++result.applied;
    }

    return result;
}

} // namespace opengenesis::scripting
