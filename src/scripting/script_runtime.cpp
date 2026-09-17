#include "opengenesis/scripting/script_runtime.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace opengenesis::scripting {
namespace {

bool safe_field(const std::string_view value, const std::size_t max_size = 1024) {
    return !value.empty() && value.size() <= max_size &&
           value.find('\t') == std::string_view::npos &&
           value.find('\r') == std::string_view::npos &&
           value.find('\n') == std::string_view::npos;
}

std::vector<std::string> split_tab(const std::string& line) {
    std::vector<std::string> fields;
    std::size_t start = 0;
    while (true) {
        const auto end = line.find('\t', start);
        fields.push_back(line.substr(start, end == std::string::npos
                                               ? std::string::npos
                                               : end - start));
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return fields;
}

} // namespace

ScriptRuntime::ScriptRuntime(std::string path) : path_(std::move(path)) {
    load();
}

bool ScriptRuntime::upsert(ScriptRecord script, std::string& reason) {
    if (!safe_field(script.id, 256) || !safe_field(script.object_id, 256) ||
        !safe_field(script.owner_user_id, 256) || !safe_field(script.source_hash, 256) ||
        !safe_field(script.state, 128) || script.timer_interval_ms < 0 ||
        script.timer_interval_ms > 24LL * 60LL * 60LL * 1000LL) {
        reason = "invalid-script-record";
        return false;
    }

    if (script.timer_interval_ms == 0) script.next_timer_unix_ms = 0;

    std::scoped_lock lock(mutex_);
    scripts_[script.id] = std::move(script);
    persist_locked();
    reason.clear();
    return true;
}

bool ScriptRuntime::remove(const std::string_view script_id) {
    std::scoped_lock lock(mutex_);
    if (scripts_.erase(std::string{script_id}) == 0) return false;
    persist_locked();
    return true;
}

bool ScriptRuntime::set_state(const std::string_view script_id, std::string state) {
    if (!safe_field(state, 128)) return false;
    std::scoped_lock lock(mutex_);
    const auto it = scripts_.find(std::string{script_id});
    if (it == scripts_.end()) return false;
    it->second.state = std::move(state);
    persist_locked();
    return true;
}

bool ScriptRuntime::set_timer(const std::string_view script_id,
                              const std::int64_t interval_ms,
                              const std::int64_t now_unix_ms) {
    if (interval_ms < 0 || interval_ms > 24LL * 60LL * 60LL * 1000LL) return false;
    std::scoped_lock lock(mutex_);
    const auto it = scripts_.find(std::string{script_id});
    if (it == scripts_.end()) return false;
    it->second.timer_interval_ms = interval_ms;
    it->second.next_timer_unix_ms = interval_ms == 0 ? 0 : now_unix_ms + interval_ms;
    persist_locked();
    return true;
}

bool ScriptRuntime::set_chat(const std::string_view script_id,
                             const bool enabled,
                             const std::int32_t channel) {
    std::scoped_lock lock(mutex_);
    const auto it = scripts_.find(std::string{script_id});
    if (it == scripts_.end()) return false;
    it->second.chat_enabled = enabled;
    it->second.chat_channel = channel;
    persist_locked();
    return true;
}

std::vector<ScriptEvent> ScriptRuntime::due_timers(const std::int64_t now_unix_ms) {
    std::scoped_lock lock(mutex_);
    std::vector<ScriptEvent> events;
    for (auto& [_, script] : scripts_) {
        if (!script.enabled || script.timer_interval_ms <= 0 ||
            script.next_timer_unix_ms <= 0 || script.next_timer_unix_ms > now_unix_ms) {
            continue;
        }

        events.push_back({.script_id = script.id, .type = "timer", .payload = {}});
        ++script.event_count;

        const auto elapsed = now_unix_ms - script.next_timer_unix_ms;
        const auto skipped = elapsed / script.timer_interval_ms;
        script.next_timer_unix_ms += (skipped + 1) * script.timer_interval_ms;
    }
    if (!events.empty()) persist_locked();
    return events;
}

std::vector<ScriptEvent> ScriptRuntime::dispatch_chat(const std::int32_t channel,
                                                       const std::string_view speaker,
                                                       const std::string_view text) {
    std::scoped_lock lock(mutex_);
    std::vector<ScriptEvent> events;
    const auto payload = std::string{speaker} + "\n" + std::string{text};

    for (auto& [_, script] : scripts_) {
        if (!script.enabled || !script.chat_enabled || script.chat_channel != channel) continue;
        events.push_back({.script_id = script.id, .type = "chat", .payload = payload});
        ++script.event_count;
    }
    if (!events.empty()) persist_locked();
    return events;
}

std::optional<ScriptRecord> ScriptRuntime::find(const std::string_view script_id) const {
    std::scoped_lock lock(mutex_);
    const auto it = scripts_.find(std::string{script_id});
    return it == scripts_.end() ? std::nullopt : std::optional<ScriptRecord>{it->second};
}

std::vector<ScriptRecord> ScriptRuntime::list() const {
    std::scoped_lock lock(mutex_);
    std::vector<ScriptRecord> output;
    output.reserve(scripts_.size());
    for (const auto& [_, script] : scripts_) output.push_back(script);
    std::sort(output.begin(), output.end(), [](const ScriptRecord& a, const ScriptRecord& b) {
        return a.id < b.id;
    });
    return output;
}

void ScriptRuntime::load() {
    std::scoped_lock lock(mutex_);
    scripts_.clear();

    std::ifstream input(path_);
    if (!input) return;

    std::string line;
    while (std::getline(input, line)) {
        if (line.empty() || line[0] == '#') continue;
        const auto fields = split_tab(line);
        if (fields.size() != 11) continue;
        try {
            ScriptRecord script{
                .id = fields[0],
                .object_id = fields[1],
                .owner_user_id = fields[2],
                .source_hash = fields[3],
                .state = fields[4],
                .enabled = fields[5] == "1",
                .timer_interval_ms = std::stoll(fields[6]),
                .next_timer_unix_ms = std::stoll(fields[7]),
                .chat_channel = static_cast<std::int32_t>(std::stol(fields[8])),
                .chat_enabled = fields[9] == "1",
                .event_count = static_cast<std::uint64_t>(std::stoull(fields[10]))};
            if (safe_field(script.id, 256) && safe_field(script.object_id, 256) &&
                safe_field(script.owner_user_id, 256) && safe_field(script.source_hash, 256) &&
                safe_field(script.state, 128)) {
                scripts_[script.id] = std::move(script);
            }
        } catch (...) {
        }
    }
}

void ScriptRuntime::persist_locked() const {
    const std::filesystem::path path(path_);
    if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path());
    const auto temp = path.string() + ".tmp";

    std::ofstream output(temp, std::ios::trunc);
    if (!output) throw std::runtime_error("cannot write script runtime store");
    output << "# OpenGenesisLINK script runtime v1\n";

    std::vector<ScriptRecord> rows;
    rows.reserve(scripts_.size());
    for (const auto& [_, script] : scripts_) rows.push_back(script);
    std::sort(rows.begin(), rows.end(), [](const ScriptRecord& a, const ScriptRecord& b) {
        return a.id < b.id;
    });

    for (const auto& script : rows) {
        output << script.id << '\t' << script.object_id << '\t' << script.owner_user_id << '\t'
               << script.source_hash << '\t' << script.state << '\t'
               << (script.enabled ? '1' : '0') << '\t'
               << script.timer_interval_ms << '\t' << script.next_timer_unix_ms << '\t'
               << script.chat_channel << '\t' << (script.chat_enabled ? '1' : '0') << '\t'
               << script.event_count << '\n';
    }

    output.close();
    if (!output) throw std::runtime_error("cannot flush script runtime store");

    std::error_code error;
    std::filesystem::rename(temp, path, error);
    if (error) {
        std::filesystem::remove(path, error);
        error.clear();
        std::filesystem::rename(temp, path, error);
    }
    if (error) throw std::runtime_error("cannot replace script runtime store");
}

} // namespace opengenesis::scripting
