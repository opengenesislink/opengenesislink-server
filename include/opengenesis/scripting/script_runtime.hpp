#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace opengenesis::scripting {

struct ScriptRecord {
    std::string id;
    std::string object_id;
    std::string owner_user_id;
    std::string source_hash;
    std::string state{"default"};
    bool enabled{true};
    std::int64_t timer_interval_ms{0};
    std::int64_t next_timer_unix_ms{0};
    std::int32_t chat_channel{0};
    bool chat_enabled{false};
    std::uint64_t event_count{0};
};

struct ScriptEvent {
    std::string script_id;
    std::string type;
    std::string payload;
};

class ScriptRuntime final {
public:
    explicit ScriptRuntime(std::string path);

    [[nodiscard]] bool upsert(ScriptRecord script, std::string& reason);
    [[nodiscard]] bool remove(std::string_view script_id);
    [[nodiscard]] bool set_state(std::string_view script_id, std::string state);
    [[nodiscard]] bool set_timer(std::string_view script_id,
                                 std::int64_t interval_ms,
                                 std::int64_t now_unix_ms);
    [[nodiscard]] bool set_chat(std::string_view script_id,
                                bool enabled,
                                std::int32_t channel);

    [[nodiscard]] std::vector<ScriptEvent> due_timers(std::int64_t now_unix_ms);
    [[nodiscard]] std::vector<ScriptEvent> dispatch_chat(std::int32_t channel,
                                                         std::string_view speaker,
                                                         std::string_view text);

    [[nodiscard]] std::optional<ScriptRecord> find(std::string_view script_id) const;
    [[nodiscard]] std::vector<ScriptRecord> list() const;

private:
    void load();
    void persist_locked() const;

    std::string path_;
    mutable std::mutex mutex_;
    std::unordered_map<std::string, ScriptRecord> scripts_;
};

} // namespace opengenesis::scripting
