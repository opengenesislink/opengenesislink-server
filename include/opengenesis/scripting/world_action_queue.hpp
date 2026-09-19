#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>

namespace opengenesis::scripting {

enum class ScriptWorldActionType {
    move,
    rotate,
    scale,
    physics,
    chat_say,
    chat_whisper,
    chat_shout
};

struct ScriptWorldAction {
    std::string id;
    std::string region_id;
    std::uint64_t entity_id{0};
    std::string owner_user_id;
    std::string script_id;
    ScriptWorldActionType type{ScriptWorldActionType::move};
    std::string payload;
};

class ScriptWorldActionQueue final {
public:
    explicit ScriptWorldActionQueue(std::size_t max_pending = 4096);

    [[nodiscard]] bool enqueue(ScriptWorldAction action);
    [[nodiscard]] std::optional<ScriptWorldAction> take(std::string_view region_id);
    [[nodiscard]] std::size_t size() const;

private:
    std::size_t max_pending_;
    mutable std::mutex mutex_;
    std::deque<ScriptWorldAction> actions_;
};

[[nodiscard]] const char* script_world_action_name(ScriptWorldActionType type) noexcept;

} // namespace opengenesis::scripting
