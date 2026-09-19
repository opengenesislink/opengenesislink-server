#include "opengenesis/scripting/world_action_queue.hpp"

#include "opengenesis/security/crypto.hpp"

#include <algorithm>
#include <utility>

namespace opengenesis::scripting {

const char* script_world_action_name(const ScriptWorldActionType type) noexcept {
    switch (type) {
        case ScriptWorldActionType::move: return "move";
        case ScriptWorldActionType::rotate: return "rotate";
        case ScriptWorldActionType::scale: return "scale";
        case ScriptWorldActionType::physics: return "physics";
        case ScriptWorldActionType::chat_say: return "say";
        case ScriptWorldActionType::chat_whisper: return "whisper";
        case ScriptWorldActionType::chat_shout: return "shout";
    }
    return "move";
}

ScriptWorldActionQueue::ScriptWorldActionQueue(const std::size_t max_pending)
    : max_pending_(std::clamp<std::size_t>(max_pending, 1U, 65536U)) {}

bool ScriptWorldActionQueue::enqueue(ScriptWorldAction action) {
    if (action.region_id.empty() || action.region_id.size() > 256U ||
        action.entity_id == 0 || action.owner_user_id.empty() ||
        action.owner_user_id.size() > 256U || action.script_id.empty() ||
        action.script_id.size() > 256U || action.payload.size() > 4096U) {
        return false;
    }

    std::scoped_lock lock(mutex_);
    if (actions_.size() >= max_pending_) return false;
    if (action.id.empty()) action.id = security::random_hex(16);
    actions_.push_back(std::move(action));
    return true;
}

std::optional<ScriptWorldAction> ScriptWorldActionQueue::take(
    const std::string_view region_id) {
    std::scoped_lock lock(mutex_);
    const auto it = std::find_if(actions_.begin(), actions_.end(),
        [&](const ScriptWorldAction& action) { return action.region_id == region_id; });
    if (it == actions_.end()) return std::nullopt;
    auto action = std::move(*it);
    actions_.erase(it);
    return action;
}

std::size_t ScriptWorldActionQueue::size() const {
    std::scoped_lock lock(mutex_);
    return actions_.size();
}

} // namespace opengenesis::scripting
