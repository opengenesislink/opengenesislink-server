#include "opengenesis/scripting/script_runtime.hpp"
#include "opengenesis/platform/filesystem.hpp"
#include "opengenesis/security/crypto.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
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
        script.source.size() > 64U * 1024U || script.vm_state.size() > 64U * 1024U ||
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

bool ScriptRuntime::set_program(const std::string_view script_id,
                                std::string source,
                                std::string& reason) {
    if (source.empty() || source.size() > 64U * 1024U) {
        reason = "invalid-script-size";
        return false;
    }

    std::scoped_lock lock(mutex_);
    const auto it = scripts_.find(std::string{script_id});
    if (it == scripts_.end()) {
        reason = "script-not-found";
        return false;
    }
    const auto compiled =
        compile_script_source(it->second.language, source, reason);
    if (!compiled) return false;
    it->second.source_hash = security::sha256_hex(source);
    it->second.source = std::move(source);
    ScriptVmState initial;
    initial.state = it->second.state;
    it->second.vm_state = serialize_vm_state(initial);
    persist_locked();
    reason.clear();
    return true;
}

bool ScriptRuntime::set_language(
    const std::string_view script_id,
    const ScriptLanguage language,
    std::string& reason) {
    std::scoped_lock lock(mutex_);
    const auto it = scripts_.find(std::string{script_id});
    if (it == scripts_.end()) {
        reason = "script-not-found";
        return false;
    }
    if (!it->second.source.empty()) {
        const auto compiled =
            compile_script_source(language, it->second.source, reason);
        if (!compiled) return false;
    }
    it->second.language = language;
    persist_locked();
    reason.clear();
    return true;
}

std::optional<ScriptVmResult> ScriptRuntime::execute_event(
    const std::string_view script_id,
    const std::string_view event,
    const std::int64_t now_unix_ms,
    std::string& reason,
    const ScriptVmLimits& limits,
    const std::string_view payload) {
    std::scoped_lock lock(mutex_);
    const auto it = scripts_.find(std::string{script_id});
    if (it == scripts_.end()) {
        reason = "script-not-found";
        return std::nullopt;
    }
    auto& script = it->second;
    if (!script.enabled) {
        reason = "script-disabled";
        return std::nullopt;
    }
    if (script.source.empty()) {
        reason = "script-program-missing";
        return std::nullopt;
    }

    const auto program =
        compile_script_source(script.language, script.source, reason);
    if (!program) return std::nullopt;

    ScriptVmState state;
    if (script.vm_state.empty()) {
        state.state = script.state;
    } else {
        const auto decoded = deserialize_vm_state(script.vm_state, reason);
        if (!decoded) return std::nullopt;
        state = *decoded;
    }

    auto result = execute_script_event(
        *program, event, state, limits, payload);
    if (!result.ok) {
        reason = result.error;
        return result;
    }

    script.state = result.state.state;
    script.vm_state = serialize_vm_state(result.state);
    ++script.event_count;
    for (const auto& action : result.actions) {
        if (action.type == ScriptActionType::set_timer) {
            script.timer_interval_ms = action.number;
            script.next_timer_unix_ms = action.number <= 0 ? 0 : now_unix_ms + action.number;
        } else if (action.type == ScriptActionType::listen) {
            script.chat_enabled = true;
            script.chat_channel = static_cast<std::int32_t>(action.number);
        }
    }
    persist_locked();
    reason.clear();
    return result;
}

bool ScriptRuntime::apply_world_result(const std::string_view script_id,
                                       const std::string_view prefix,
                                       const std::string_view result,
                                       std::string& reason) {
    if (!safe_field(prefix, 64)) {
        reason = "invalid-world-result-prefix";
        return false;
    }

    std::scoped_lock lock(mutex_);
    const auto it = scripts_.find(std::string{script_id});
    if (it == scripts_.end()) {
        reason = "script-not-found";
        return false;
    }

    ScriptVmState state;
    if (it->second.vm_state.empty()) {
        state.state = it->second.state;
    } else {
        const auto decoded = deserialize_vm_state(it->second.vm_state, reason);
        if (!decoded) return false;
        state = *decoded;
    }

    std::istringstream input(std::string{result});
    std::string line;
    std::size_t count = 0;
    while (std::getline(input, line)) {
        if (++count > 32U) {
            reason = "world-result-too-large";
            return false;
        }
        const auto split = line.find('=');
        if (split == std::string::npos || split == 0 || split > 64U) continue;
        const auto key = line.substr(0, split);
        const auto value = line.substr(split + 1U);
        if (!safe_field(key, 64) || value.size() > 2048U) {
            reason = "invalid-world-result-field";
            return false;
        }
        state.variables[std::string{prefix} + "." + key] = value;
    }
    state.variables[std::string{prefix} + ".ready"] = "1";
    if (state.variables.size() > 64U) {
        reason = "world-result-variable-budget-exceeded";
        return false;
    }

    const auto encoded = serialize_vm_state(state);
    if (encoded.size() > 64U * 1024U) {
        reason = "world-result-state-too-large";
        return false;
    }
    it->second.vm_state = encoded;
    persist_locked();
    reason.clear();
    return true;
}


bool ScriptRuntime::rebind_objects(
    const std::string_view source_region,
    const std::string_view destination_region,
    const std::vector<std::pair<std::uint64_t, std::uint64_t>>& entity_map,
    const std::string_view owner_user_id,
    std::string& reason) {
    if (!safe_field(source_region, 256) ||
        !safe_field(destination_region, 256) ||
        source_region == destination_region ||
        owner_user_id.empty() || owner_user_id.size() > 256U ||
        entity_map.empty() || entity_map.size() > 64U) {
        reason = "invalid-script-rebind";
        return false;
    }

    std::vector<std::pair<std::string, std::string>> bindings;
    bindings.reserve(entity_map.size());
    for (const auto& [source_entity, destination_entity] : entity_map) {
        if (source_entity == 0 || destination_entity == 0) {
            reason = "invalid-script-rebind-entity";
            return false;
        }
        const auto source =
            std::string{source_region} + "/" + std::to_string(source_entity);
        const auto destination =
            std::string{destination_region} + "/" +
            std::to_string(destination_entity);
        if (std::find_if(
                bindings.begin(), bindings.end(),
                [&](const auto& item) {
                    return item.first == source ||
                           item.second == destination;
                }) != bindings.end()) {
            reason = "duplicate-script-rebind-entity";
            return false;
        }
        bindings.emplace_back(source, destination);
    }

    std::scoped_lock lock(mutex_);
    for (const auto& [_, script] : scripts_) {
        if (script.owner_user_id != owner_user_id) continue;
        const auto source_match = std::find_if(
            bindings.begin(), bindings.end(),
            [&](const auto& item) {
                return script.object_id == item.first;
            });
        const auto destination_match = std::find_if(
            bindings.begin(), bindings.end(),
            [&](const auto& item) {
                return script.object_id == item.second;
            });
        if (source_match == bindings.end() &&
            destination_match == bindings.end()) {
            continue;
        }
        if (source_match != bindings.end() &&
            destination_match != bindings.end() &&
            source_match != destination_match) {
            reason = "ambiguous-script-rebind";
            return false;
        }
    }

    for (auto& [_, script] : scripts_) {
        if (script.owner_user_id != owner_user_id) continue;
        const auto binding = std::find_if(
            bindings.begin(), bindings.end(),
            [&](const auto& item) {
                return script.object_id == item.first;
            });
        if (binding == bindings.end()) continue;
        script.object_id = binding->second;
    }
    // Persist even on an idempotent retry. A previous persist attempt may
    // have failed after the in-memory bindings were already updated.
    persist_locked();
    reason.clear();
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
    for (auto& [_, script] : scripts_) {
        if (!script.enabled || !script.chat_enabled || script.chat_channel != channel) continue;
        const auto payload =
            script.language == ScriptLanguage::lsl
                ? std::to_string(channel) + "\n" +
                      std::string{speaker} + "\n\n" +
                      std::string{text}
                : std::string{speaker} + "\n" + std::string{text};
        events.push_back({
            .script_id = script.id,
            .type = script.language == ScriptLanguage::lsl ? "listen" : "chat",
            .payload = payload});
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

std::vector<ScriptRecord> ScriptRuntime::list_for_object(
    const std::string_view object_id) const {
    std::scoped_lock lock(mutex_);
    std::vector<ScriptRecord> result;
    for (const auto& [_, script] : scripts_) {
        if (script.enabled && script.object_id == object_id) {
            result.push_back(script);
        }
    }
    std::sort(
        result.begin(), result.end(),
        [](const ScriptRecord& left, const ScriptRecord& right) {
            return left.id < right.id;
        });
    return result;
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
        if (fields.size() != 11 && fields.size() != 13 &&
            fields.size() != 14) continue;
        try {
            ScriptRecord script{
                .id = fields[0],
                .object_id = fields[1],
                .owner_user_id = fields[2],
                .source_hash = fields[3],
                .language = fields.size() == 14
                                ? parse_script_language(fields[11]).value_or(
                                      ScriptLanguage::legacy)
                                : ScriptLanguage::legacy,
                .source = fields.size() == 14
                              ? security::base64_decode(fields[12], 64U * 1024U)
                              : (fields.size() == 13
                                     ? security::base64_decode(fields[11], 64U * 1024U)
                                     : std::string{}),
                .vm_state = fields.size() == 14
                                ? security::base64_decode(fields[13], 64U * 1024U)
                                : (fields.size() == 13
                                       ? security::base64_decode(fields[12], 64U * 1024U)
                                       : std::string{}),
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
    output << "# OpenGenesisLINK script runtime v3\n";

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
               << script.event_count << '\t'
               << script_language_name(script.language) << '\t'
               << security::base64_encode(script.source) << '\t'
               << security::base64_encode(script.vm_state) << '\n';
    }

    output.close();
    if (!output) throw std::runtime_error("cannot flush script runtime store");

    opengenesis::platform::replace_file(temp, path);
}

} // namespace opengenesis::scripting
