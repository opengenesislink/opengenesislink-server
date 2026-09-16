#include "opengenesis/core/presence_store.hpp"

#include <algorithm>
#include <chrono>
#include <unordered_set>
#include <utility>

namespace opengenesis::core {
namespace {
std::int64_t unix_now() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}
} // namespace

void PresenceStore::replace_region_snapshot(std::string region_id, std::string node_id,
                                            const std::uint64_t generation,
                                            std::vector<PresenceInfo> presences) {
    const auto now = unix_now();
    std::unordered_set<std::string> incoming;
    incoming.reserve(presences.size());

    std::scoped_lock lock(mutex_);
    for (auto it = by_user_.begin(); it != by_user_.end();) {
        if (it->second.region_id == region_id && it->second.node_id == node_id &&
            it->second.node_generation == generation) {
            it = by_user_.erase(it);
        } else {
            ++it;
        }
    }

    for (auto& presence : presences) {
        if (presence.user_id.empty()) continue;
        presence.region_id = region_id;
        presence.node_id = node_id;
        presence.node_generation = generation;
        presence.updated_unix = now;
        incoming.insert(presence.user_id);
        by_user_[presence.user_id] = std::move(presence);
    }
}

void PresenceStore::mark_node_offline(const std::string_view node_id, const std::uint64_t generation) {
    std::scoped_lock lock(mutex_);
    for (auto it = by_user_.begin(); it != by_user_.end();) {
        if (it->second.node_id == node_id && it->second.node_generation == generation) {
            it = by_user_.erase(it);
        } else {
            ++it;
        }
    }
}

void PresenceStore::mark_region_offline(const std::string_view region_id) {
    std::scoped_lock lock(mutex_);
    for (auto it = by_user_.begin(); it != by_user_.end();) {
        if (it->second.region_id == region_id) it = by_user_.erase(it);
        else ++it;
    }
}

std::optional<PresenceInfo> PresenceStore::find_user(const std::string_view user_id) const {
    std::scoped_lock lock(mutex_);
    const auto it = by_user_.find(std::string{user_id});
    return it == by_user_.end() ? std::nullopt : std::optional<PresenceInfo>{it->second};
}

std::vector<PresenceInfo> PresenceStore::list() const {
    std::scoped_lock lock(mutex_);
    std::vector<PresenceInfo> result;
    result.reserve(by_user_.size());
    for (const auto& [_, presence] : by_user_) result.push_back(presence);
    std::sort(result.begin(), result.end(), [](const PresenceInfo& a, const PresenceInfo& b) {
        if (a.region_id != b.region_id) return a.region_id < b.region_id;
        return a.display_name < b.display_name;
    });
    return result;
}

std::size_t PresenceStore::count() const {
    std::scoped_lock lock(mutex_);
    return by_user_.size();
}

std::size_t PresenceStore::count_region(const std::string_view region_id) const {
    std::scoped_lock lock(mutex_);
    return static_cast<std::size_t>(std::count_if(by_user_.begin(), by_user_.end(), [&](const auto& entry) {
        return entry.second.region_id == region_id;
    }));
}

} // namespace opengenesis::core
