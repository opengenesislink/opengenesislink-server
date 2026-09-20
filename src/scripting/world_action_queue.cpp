#include "opengenesis/scripting/world_action_queue.hpp"

#include "opengenesis/platform/filesystem.hpp"
#include "opengenesis/security/crypto.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace opengenesis::scripting {
namespace {

std::int64_t now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

std::vector<std::string> split_tab(const std::string& line) {
    std::vector<std::string> fields;
    std::size_t start = 0;
    while (true) {
        const auto end = line.find('\t', start);
        fields.push_back(line.substr(start, end == std::string::npos ? std::string::npos
                                                                    : end - start));
        if (end == std::string::npos) break;
        start = end + 1U;
    }
    return fields;
}

std::optional<ScriptWorldActionType> parse_type(const std::string_view value) {
    if (value == "move") return ScriptWorldActionType::move;
    if (value == "rotate") return ScriptWorldActionType::rotate;
    if (value == "scale") return ScriptWorldActionType::scale;
    if (value == "velocity") return ScriptWorldActionType::velocity;
    if (value == "angular_velocity") return ScriptWorldActionType::angular_velocity;
    if (value == "force") return ScriptWorldActionType::force;
    if (value == "impulse") return ScriptWorldActionType::impulse;
    if (value == "angular_impulse") return ScriptWorldActionType::angular_impulse;
    if (value == "torque") return ScriptWorldActionType::torque;
    if (value == "buoyancy") return ScriptWorldActionType::buoyancy;
    if (value == "material") return ScriptWorldActionType::material;
    if (value == "physics") return ScriptWorldActionType::physics;
    if (value == "text") return ScriptWorldActionType::text;
    if (value == "say") return ScriptWorldActionType::chat_say;
    if (value == "whisper") return ScriptWorldActionType::chat_whisper;
    if (value == "shout") return ScriptWorldActionType::chat_shout;
    if (value == "query_object") return ScriptWorldActionType::query_object;
    if (value == "query_region") return ScriptWorldActionType::query_region;
    if (value == "query_terrain") return ScriptWorldActionType::query_terrain;
    if (value == "query_water") return ScriptWorldActionType::query_water;
    if (value == "query_time") return ScriptWorldActionType::query_time;
    if (value == "query_nearby") return ScriptWorldActionType::query_nearby;
    return std::nullopt;
}

} // namespace

const char* script_world_action_name(const ScriptWorldActionType type) noexcept {
    switch (type) {
        case ScriptWorldActionType::move: return "move";
        case ScriptWorldActionType::rotate: return "rotate";
        case ScriptWorldActionType::scale: return "scale";
        case ScriptWorldActionType::velocity: return "velocity";
        case ScriptWorldActionType::angular_velocity: return "angular_velocity";
        case ScriptWorldActionType::force: return "force";
        case ScriptWorldActionType::impulse: return "impulse";
        case ScriptWorldActionType::angular_impulse: return "angular_impulse";
        case ScriptWorldActionType::torque: return "torque";
        case ScriptWorldActionType::buoyancy: return "buoyancy";
        case ScriptWorldActionType::material: return "material";
        case ScriptWorldActionType::physics: return "physics";
        case ScriptWorldActionType::text: return "text";
        case ScriptWorldActionType::chat_say: return "say";
        case ScriptWorldActionType::chat_whisper: return "whisper";
        case ScriptWorldActionType::chat_shout: return "shout";
        case ScriptWorldActionType::query_object: return "query_object";
        case ScriptWorldActionType::query_region: return "query_region";
        case ScriptWorldActionType::query_terrain: return "query_terrain";
        case ScriptWorldActionType::query_water: return "query_water";
        case ScriptWorldActionType::query_time: return "query_time";
        case ScriptWorldActionType::query_nearby: return "query_nearby";
    }
    return "move";
}

bool script_world_action_is_query(const ScriptWorldActionType type) noexcept {
    return type == ScriptWorldActionType::query_object ||
           type == ScriptWorldActionType::query_region ||
           type == ScriptWorldActionType::query_terrain ||
           type == ScriptWorldActionType::query_water ||
           type == ScriptWorldActionType::query_time ||
           type == ScriptWorldActionType::query_nearby;
}

std::string script_world_action_result_prefix(const ScriptWorldAction& action) {
    if (!script_world_action_is_query(action.type)) return {};
    const auto split = action.payload.find('|');
    return action.payload.substr(0, split);
}

ScriptWorldActionQueue::ScriptWorldActionQueue(std::string path,
                                               const std::size_t max_pending,
                                               const std::uint32_t max_attempts,
                                               const std::int64_t lease_ms,
                                               const std::int64_t ttl_ms)
    : path_(std::move(path)),
      max_pending_(std::clamp<std::size_t>(max_pending, 1U, 65536U)),
      max_attempts_(std::clamp<std::uint32_t>(max_attempts, 1U, 32U)),
      lease_ms_(std::clamp<std::int64_t>(lease_ms, 100, 60000)),
      ttl_ms_(std::clamp<std::int64_t>(ttl_ms, 1000, 3600000)) {
    load();
}

bool ScriptWorldActionQueue::enqueue(ScriptWorldAction action) {
    if (action.region_id.empty() || action.region_id.size() > 256U ||
        action.entity_id == 0 || action.owner_user_id.empty() ||
        action.owner_user_id.size() > 256U || action.script_id.empty() ||
        action.script_id.size() > 256U || action.payload.size() > 4096U) {
        return false;
    }

    const auto current = now_ms();
    if (action.id.empty()) action.id = security::random_hex(16);
    if (action.created_unix_ms <= 0) action.created_unix_ms = current;
    if (action.expires_unix_ms <= action.created_unix_ms) {
        action.expires_unix_ms = action.created_unix_ms + ttl_ms_;
    }
    action.lease_until_unix_ms = 0;
    action.attempts = 0;
    action.last_error.clear();

    std::scoped_lock lock(mutex_);
    if (actions_.size() >= max_pending_) return false;
    actions_.push_back(std::move(action));
    persist_locked();
    return true;
}

std::optional<ScriptWorldAction> ScriptWorldActionQueue::lease(
    const std::string_view region_id,
    const std::int64_t now_unix_ms) {
    std::scoped_lock lock(mutex_);

    bool changed = false;
    for (auto it = actions_.begin(); it != actions_.end();) {
        const bool expired = it->expires_unix_ms <= now_unix_ms;
        const bool attempts_exhausted =
            it->attempts >= max_attempts_ &&
            it->lease_until_unix_ms <= now_unix_ms;
        if (expired || attempts_exhausted) {
            it = actions_.erase(it);
            changed = true;
        } else {
            ++it;
        }
    }

    for (auto& action : actions_) {
        if (action.region_id != region_id || action.lease_until_unix_ms > now_unix_ms ||
            action.attempts >= max_attempts_) {
            continue;
        }
        ++action.attempts;
        action.lease_until_unix_ms = now_unix_ms + lease_ms_;
        persist_locked();
        return action;
    }

    if (changed) persist_locked();
    return std::nullopt;
}

bool ScriptWorldActionQueue::ack(const std::string_view action_id) {
    std::scoped_lock lock(mutex_);
    const auto it = std::find_if(actions_.begin(), actions_.end(),
        [&](const ScriptWorldAction& action) { return action.id == action_id; });
    if (it == actions_.end()) return false;
    actions_.erase(it);
    persist_locked();
    return true;
}

bool ScriptWorldActionQueue::nack(const std::string_view action_id,
                                  std::string error,
                                  const std::int64_t retry_at_unix_ms) {
    if (error.size() > 512U) error.resize(512U);
    std::scoped_lock lock(mutex_);
    const auto it = std::find_if(actions_.begin(), actions_.end(),
        [&](const ScriptWorldAction& action) { return action.id == action_id; });
    if (it == actions_.end()) return false;

    if (it->attempts >= max_attempts_) {
        actions_.erase(it);
    } else {
        it->last_error = std::move(error);
        it->lease_until_unix_ms = std::max<std::int64_t>(0, retry_at_unix_ms);
    }
    persist_locked();
    return true;
}

std::optional<ScriptWorldAction> ScriptWorldActionQueue::find(
    const std::string_view action_id) const {
    std::scoped_lock lock(mutex_);
    const auto it = std::find_if(actions_.begin(), actions_.end(),
        [&](const ScriptWorldAction& action) { return action.id == action_id; });
    return it == actions_.end() ? std::nullopt : std::optional<ScriptWorldAction>{*it};
}

std::size_t ScriptWorldActionQueue::size() const {
    std::scoped_lock lock(mutex_);
    return actions_.size();
}

std::size_t ScriptWorldActionQueue::purge_expired(const std::int64_t now_unix_ms) {
    std::scoped_lock lock(mutex_);
    const auto before = actions_.size();
    std::erase_if(actions_, [&](const ScriptWorldAction& action) {
        return action.expires_unix_ms <= now_unix_ms;
    });
    const auto removed = before - actions_.size();
    if (removed != 0U) persist_locked();
    return removed;
}

void ScriptWorldActionQueue::load() {
    if (path_.empty()) return;
    std::scoped_lock lock(mutex_);
    actions_.clear();
    std::ifstream input(path_);
    if (!input) return;

    std::string line;
    while (std::getline(input, line)) {
        if (line.empty() || line[0] == '#') continue;
        const auto fields = split_tab(line);
        if (fields.size() != 12U) continue;
        try {
            const auto type = parse_type(fields[5]);
            if (!type) continue;
            ScriptWorldAction action{
                .id = fields[0],
                .region_id = fields[1],
                .entity_id = std::stoull(fields[2]),
                .owner_user_id = fields[3],
                .script_id = fields[4],
                .type = *type,
                .payload = security::base64_decode(fields[6], 4096U),
                .created_unix_ms = std::stoll(fields[7]),
                .expires_unix_ms = std::stoll(fields[8]),
                .lease_until_unix_ms = std::stoll(fields[9]),
                .attempts = static_cast<std::uint32_t>(std::stoul(fields[10])),
                .last_error = security::base64_decode(fields[11], 512U)};
            if (action.expires_unix_ms > now_ms() && actions_.size() < max_pending_) {
                actions_.push_back(std::move(action));
            }
        } catch (...) {
        }
    }
}

void ScriptWorldActionQueue::persist_locked() const {
    if (path_.empty()) return;
    const std::filesystem::path path(path_);
    if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path());
    const auto temp = path.string() + ".tmp";
    std::ofstream output(temp, std::ios::trunc);
    if (!output) throw std::runtime_error("cannot write Script World Action queue");
    output << "# OpenGenesisLINK Script World Action queue v2\n";
    for (const auto& action : actions_) {
        output << action.id << '\t'
               << action.region_id << '\t'
               << action.entity_id << '\t'
               << action.owner_user_id << '\t'
               << action.script_id << '\t'
               << script_world_action_name(action.type) << '\t'
               << security::base64_encode(action.payload) << '\t'
               << action.created_unix_ms << '\t'
               << action.expires_unix_ms << '\t'
               << action.lease_until_unix_ms << '\t'
               << action.attempts << '\t'
               << security::base64_encode(action.last_error) << '\n';
    }
    output.close();
    if (!output) throw std::runtime_error("cannot flush Script World Action queue");
    opengenesis::platform::replace_file(temp, path);
}

} // namespace opengenesis::scripting
