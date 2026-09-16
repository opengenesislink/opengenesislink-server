#include "opengenesis/core/region_registry.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <utility>

namespace opengenesis::core {

RegionRegistry::RegionRegistry(std::string path) : storage_path_(std::move(path)) { load(); }

bool RegionRegistry::register_region(RegionInfo region, std::string& reason) {
    if (region.id.empty() || region.name.empty() || region.node_id.empty()) {
        reason = "invalid-region";
        return false;
    }
    std::scoped_lock lock(mutex_);
    for (const auto& [id, other] : regions_) {
        if (id != region.id && other.grid_x == region.grid_x && other.grid_y == region.grid_y) {
            reason = "grid-coordinate-in-use";
            return false;
        }
    }
    if (const auto it = regions_.find(region.id);
        it != regions_.end() && it->second.node_id != region.node_id) {
        reason = "region-owned-by-other-node";
        return false;
    }
    region.state = "registered";
    regions_[region.id] = std::move(region);
    persist_locked();
    reason.clear();
    return true;
}

bool RegionRegistry::valid_transition(const std::string& from, const std::string& to) {
    if (from == to) return true;
    if (to == "error" || to == "offline") return true;
    return (from == "registered" && to == "starting") ||
           (from == "offline" && to == "starting") ||
           (from == "starting" && to == "online") ||
           (from == "online" && to == "stopping") ||
           (from == "stopping" && to == "offline");
}

bool RegionRegistry::update_state(const std::string& id, const std::string& node_id,
                                  const std::uint64_t generation, const std::string& state,
                                  std::string& reason) {
    std::scoped_lock lock(mutex_);
    const auto it = regions_.find(id);
    if (it == regions_.end()) {
        reason = "unknown-region";
        return false;
    }
    if (it->second.node_id != node_id || it->second.node_generation != generation) {
        reason = "stale-or-foreign-session";
        return false;
    }
    if (!valid_transition(it->second.state, state)) {
        reason = "invalid-transition";
        return false;
    }
    it->second.state = state;
    persist_locked();
    reason.clear();
    return true;
}

bool RegionRegistry::update_metrics(const RegionInfo& metrics, std::string& reason) {
    std::scoped_lock lock(mutex_);
    const auto it = regions_.find(metrics.id);
    if (it == regions_.end()) {
        reason = "unknown-region";
        return false;
    }
    if (it->second.node_id != metrics.node_id || it->second.node_generation != metrics.node_generation) {
        reason = "stale-or-foreign-session";
        return false;
    }
    it->second.ticks = metrics.ticks;
    it->second.entities = metrics.entities;
    it->second.avatars = metrics.avatars;
    it->second.physics_bodies = metrics.physics_bodies;
    it->second.scene_events = metrics.scene_events;
    it->second.terrain_revision = metrics.terrain_revision;
    it->second.sim_fps = metrics.sim_fps;
    persist_locked();
    reason.clear();
    return true;
}

void RegionRegistry::mark_node_offline(const std::string& node_id, const std::uint64_t generation) {
    std::scoped_lock lock(mutex_);
    bool changed = false;
    for (auto& [_, region] : regions_) {
        if (region.node_id == node_id && region.node_generation == generation && region.state != "offline") {
            region.state = "offline";
            changed = true;
        }
    }
    if (changed) persist_locked();
}

std::optional<RegionInfo> RegionRegistry::find(const std::string& id) const {
    std::scoped_lock lock(mutex_);
    const auto it = regions_.find(id);
    return it == regions_.end() ? std::nullopt : std::optional<RegionInfo>{it->second};
}

std::vector<RegionInfo> RegionRegistry::list() const {
    std::scoped_lock lock(mutex_);
    std::vector<RegionInfo> result;
    result.reserve(regions_.size());
    for (const auto& [_, region] : regions_) result.push_back(region);
    std::sort(result.begin(), result.end(), [](const RegionInfo& a, const RegionInfo& b) {
        if (a.grid_y != b.grid_y) return a.grid_y < b.grid_y;
        return a.grid_x < b.grid_x;
    });
    return result;
}

std::vector<RegionInfo> RegionRegistry::neighbors(const std::string& id) const {
    std::scoped_lock lock(mutex_);
    const auto source = regions_.find(id);
    if (source == regions_.end()) return {};
    std::vector<RegionInfo> result;
    for (const auto& [other_id, region] : regions_) {
        if (other_id == id) continue;
        const auto dx = std::abs(region.grid_x - source->second.grid_x);
        const auto dy = std::abs(region.grid_y - source->second.grid_y);
        if ((dx == 1 && dy == 0) || (dx == 0 && dy == 1)) result.push_back(region);
    }
    std::sort(result.begin(), result.end(), [](const RegionInfo& a, const RegionInfo& b) {
        if (a.grid_y != b.grid_y) return a.grid_y < b.grid_y;
        return a.grid_x < b.grid_x;
    });
    return result;
}

void RegionRegistry::load() {
    if (storage_path_.empty()) return;
    std::ifstream input(storage_path_);
    std::string line;
    while (std::getline(input, line)) {
        std::istringstream stream(line);
        RegionInfo region;
        stream >> std::quoted(region.id) >> std::quoted(region.name) >> std::quoted(region.node_id) >>
            std::quoted(region.state) >> region.grid_x >> region.grid_y >> region.node_generation >>
            region.ticks >> region.entities >> region.avatars >> region.physics_bodies >> region.sim_fps;
        if (stream.good()) {
            std::uint64_t events = 0;
            std::uint64_t terrain = 0;
            if (stream >> events >> terrain) {
                region.scene_events = events;
                region.terrain_revision = terrain;
            }
        }
        if (!region.id.empty()) {
            region.state = "offline";
            regions_[region.id] = std::move(region);
        }
    }
}

void RegionRegistry::persist_locked() const {
    if (storage_path_.empty()) return;
    const std::filesystem::path path(storage_path_);
    if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path());
    const auto temp = storage_path_ + ".tmp";
    std::ofstream output(temp, std::ios::trunc);
    if (!output) throw std::runtime_error("cannot write region registry");
    for (const auto& [_, region] : regions_) {
        output << std::quoted(region.id) << ' ' << std::quoted(region.name) << ' '
               << std::quoted(region.node_id) << ' ' << std::quoted(region.state) << ' '
               << region.grid_x << ' ' << region.grid_y << ' ' << region.node_generation << ' '
               << region.ticks << ' ' << region.entities << ' ' << region.avatars << ' '
               << region.physics_bodies << ' ' << region.sim_fps << ' ' << region.scene_events << ' '
               << region.terrain_revision << '\n';
    }
    output.close();
    if (!output) throw std::runtime_error("cannot flush region registry");
    std::filesystem::rename(temp, storage_path_);
}

} // namespace opengenesis::core
