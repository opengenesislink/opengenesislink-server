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
    velocity,
    angular_velocity,
    force,
    impulse,
    angular_impulse,
    torque,
    buoyancy,
    material,
    physics,
    text,
    chat_say,
    chat_whisper,
    chat_shout,
    query_object,
    query_region,
    query_terrain,
    query_water,
    query_time,
    query_nearby
};

struct ScriptWorldAction {
    std::string id;
    std::string region_id;
    std::uint64_t entity_id{0};
    std::string owner_user_id;
    std::string script_id;
    ScriptWorldActionType type{ScriptWorldActionType::move};
    std::string payload;
    std::int64_t created_unix_ms{0};
    std::int64_t expires_unix_ms{0};
    std::int64_t lease_until_unix_ms{0};
    std::uint32_t attempts{0};
    std::string last_error;
};

class ScriptWorldActionQueue final {
public:
    explicit ScriptWorldActionQueue(std::string path = {},
                                    std::size_t max_pending = 4096,
                                    std::uint32_t max_attempts = 5,
                                    std::int64_t lease_ms = 2000,
                                    std::int64_t ttl_ms = 60000);

    [[nodiscard]] bool enqueue(ScriptWorldAction action);
    [[nodiscard]] std::optional<ScriptWorldAction> lease(std::string_view region_id,
                                                         std::int64_t now_unix_ms);
    [[nodiscard]] bool ack(std::string_view action_id);
    [[nodiscard]] bool nack(std::string_view action_id,
                            std::string error,
                            std::int64_t retry_at_unix_ms);
    [[nodiscard]] std::optional<ScriptWorldAction> find(std::string_view action_id) const;
    [[nodiscard]] std::size_t size() const;
    std::size_t purge_expired(std::int64_t now_unix_ms);

private:
    void load();
    void persist_locked() const;

    std::string path_;
    std::size_t max_pending_;
    std::uint32_t max_attempts_;
    std::int64_t lease_ms_;
    std::int64_t ttl_ms_;
    mutable std::mutex mutex_;
    std::deque<ScriptWorldAction> actions_;
};

[[nodiscard]] const char* script_world_action_name(ScriptWorldActionType type) noexcept;
[[nodiscard]] bool script_world_action_is_query(ScriptWorldActionType type) noexcept;
[[nodiscard]] std::string script_world_action_result_prefix(const ScriptWorldAction& action);

} // namespace opengenesis::scripting
