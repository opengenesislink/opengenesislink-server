#include "opengenesis/core/region_registry.hpp"
#include "opengenesis/platform/filesystem.hpp"
#include "opengenesis/storage/database.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace opengenesis::core {
namespace {

std::optional<std::string> cell(
    const storage::DatabaseRow& row,
    const std::string& name) {
    const auto it = row.find(name);
    if (it == row.end()) return std::nullopt;
    return it->second;
}

RegionInfo row_to_region(const storage::DatabaseRow& row) {
    RegionInfo region;
    region.id = cell(row, "id").value_or("");
    region.name = cell(row, "name").value_or("");
    region.node_id = cell(row, "node_id").value_or("");
    region.state = cell(row, "state").value_or("offline");
    region.grid_x = static_cast<std::int32_t>(
        std::stoll(cell(row, "grid_x").value_or("0")));
    region.grid_y = static_cast<std::int32_t>(
        std::stoll(cell(row, "grid_y").value_or("0")));
    region.node_generation = static_cast<std::uint64_t>(
        std::stoull(
            cell(row, "node_generation").value_or("0")));
    region.ticks = static_cast<std::uint64_t>(
        std::stoull(cell(row, "ticks").value_or("0")));
    region.entities = static_cast<std::uint64_t>(
        std::stoull(cell(row, "entities").value_or("0")));
    region.avatars = static_cast<std::uint64_t>(
        std::stoull(cell(row, "avatars").value_or("0")));
    region.physics_bodies = static_cast<std::uint64_t>(
        std::stoull(
            cell(row, "physics_bodies").value_or("0")));
    region.scene_events = static_cast<std::uint64_t>(
        std::stoull(
            cell(row, "scene_events").value_or("0")));
    region.terrain_revision = static_cast<std::uint64_t>(
        std::stoull(
            cell(row, "terrain_revision").value_or("0")));
    region.sim_fps =
        std::stod(cell(row, "sim_fps").value_or("0"));
    return region;
}

} // namespace

RegionRegistry::RegionRegistry(std::string path)
    : storage_path_(std::move(path)) {
    load();
}

RegionRegistry::RegionRegistry(
    std::shared_ptr<storage::DatabasePool> database)
    : database_(std::move(database)) {
    if (!database_) {
        throw std::invalid_argument("region registry database is required");
    }
}

bool RegionRegistry::register_region(
    RegionInfo region,
    std::string& reason) {
    if (region.id.empty() ||
        region.name.empty() ||
        region.node_id.empty()) {
        reason = "invalid-region";
        return false;
    }

    if (database_) {
        const auto coordinate = database_->query(
            "SELECT id FROM ogl_regions "
            "WHERE grid_x=? AND grid_y=? AND id<>?",
            {std::to_string(region.grid_x),
             std::to_string(region.grid_y),
             region.id});
        if (!coordinate.empty()) {
            reason = "grid-coordinate-in-use";
            return false;
        }

        const auto existing = database_->query(
            "SELECT node_id FROM ogl_regions WHERE id=?",
            {region.id});
        if (!existing.empty()) {
            const auto owner =
                cell(existing.front(), "node_id").value_or("");
            if (owner != region.node_id) {
                reason = "region-owned-by-other-node";
                return false;
            }
            database_->execute(
                "UPDATE ogl_regions SET "
                "name=?,node_id=?,state=?,grid_x=?,grid_y=?,"
                "node_generation=?,ticks=?,entities=?,avatars=?,"
                "physics_bodies=?,scene_events=?,terrain_revision=?,"
                "sim_fps=? WHERE id=?",
                {region.name, region.node_id,
                 std::string{"registered"},
                 std::to_string(region.grid_x),
                 std::to_string(region.grid_y),
                 std::to_string(region.node_generation),
                 std::to_string(region.ticks),
                 std::to_string(region.entities),
                 std::to_string(region.avatars),
                 std::to_string(region.physics_bodies),
                 std::to_string(region.scene_events),
                 std::to_string(region.terrain_revision),
                 std::to_string(region.sim_fps),
                 region.id});
        } else {
            database_->execute(
                "INSERT INTO ogl_regions("
                "id,name,node_id,state,grid_x,grid_y,"
                "node_generation,ticks,entities,avatars,"
                "physics_bodies,scene_events,terrain_revision,sim_fps"
                ") VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?)",
                {region.id, region.name, region.node_id,
                 std::string{"registered"},
                 std::to_string(region.grid_x),
                 std::to_string(region.grid_y),
                 std::to_string(region.node_generation),
                 std::to_string(region.ticks),
                 std::to_string(region.entities),
                 std::to_string(region.avatars),
                 std::to_string(region.physics_bodies),
                 std::to_string(region.scene_events),
                 std::to_string(region.terrain_revision),
                 std::to_string(region.sim_fps)});
        }
        reason.clear();
        return true;
    }

    std::scoped_lock lock(mutex_);
    for (const auto& [id, other] : regions_) {
        if (id != region.id &&
            other.grid_x == region.grid_x &&
            other.grid_y == region.grid_y) {
            reason = "grid-coordinate-in-use";
            return false;
        }
    }
    if (const auto it = regions_.find(region.id);
        it != regions_.end() &&
        it->second.node_id != region.node_id) {
        reason = "region-owned-by-other-node";
        return false;
    }
    region.state = "registered";
    regions_[region.id] = std::move(region);
    persist_locked();
    reason.clear();
    return true;
}

bool RegionRegistry::valid_transition(
    const std::string& from,
    const std::string& to) {
    if (from == to) return true;
    if (to == "error" || to == "offline") return true;
    return (from == "registered" && to == "starting") ||
           (from == "offline" && to == "starting") ||
           (from == "starting" && to == "online") ||
           (from == "online" && to == "stopping") ||
           (from == "stopping" && to == "offline");
}

bool RegionRegistry::update_state(
    const std::string& id,
    const std::string& node_id,
    const std::uint64_t generation,
    const std::string& state,
    std::string& reason) {
    if (database_) {
        const auto rows = database_->query(
            "SELECT node_id,node_generation,state "
            "FROM ogl_regions WHERE id=?",
            {id});
        if (rows.empty()) {
            reason = "unknown-region";
            return false;
        }
        const auto owner =
            cell(rows.front(), "node_id").value_or("");
        const auto current_generation = static_cast<std::uint64_t>(
            std::stoull(
                cell(rows.front(), "node_generation").value_or("0")));
        const auto current_state =
            cell(rows.front(), "state").value_or("offline");
        if (owner != node_id ||
            current_generation != generation) {
            reason = "stale-or-foreign-session";
            return false;
        }
        if (!valid_transition(current_state, state)) {
            reason = "invalid-transition";
            return false;
        }
        database_->execute(
            "UPDATE ogl_regions SET state=? WHERE id=?",
            {state, id});
        reason.clear();
        return true;
    }

    std::scoped_lock lock(mutex_);
    const auto it = regions_.find(id);
    if (it == regions_.end()) {
        reason = "unknown-region";
        return false;
    }
    if (it->second.node_id != node_id ||
        it->second.node_generation != generation) {
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

bool RegionRegistry::update_metrics(
    const RegionInfo& metrics,
    std::string& reason) {
    if (database_) {
        const auto rows = database_->query(
            "SELECT node_id,node_generation "
            "FROM ogl_regions WHERE id=?",
            {metrics.id});
        if (rows.empty()) {
            reason = "unknown-region";
            return false;
        }
        const auto owner =
            cell(rows.front(), "node_id").value_or("");
        const auto generation = static_cast<std::uint64_t>(
            std::stoull(
                cell(rows.front(), "node_generation").value_or("0")));
        if (owner != metrics.node_id ||
            generation != metrics.node_generation) {
            reason = "stale-or-foreign-session";
            return false;
        }
        database_->execute(
            "UPDATE ogl_regions SET "
            "ticks=?,entities=?,avatars=?,physics_bodies=?,"
            "scene_events=?,terrain_revision=?,sim_fps=? "
            "WHERE id=?",
            {std::to_string(metrics.ticks),
             std::to_string(metrics.entities),
             std::to_string(metrics.avatars),
             std::to_string(metrics.physics_bodies),
             std::to_string(metrics.scene_events),
             std::to_string(metrics.terrain_revision),
             std::to_string(metrics.sim_fps),
             metrics.id});
        reason.clear();
        return true;
    }

    std::scoped_lock lock(mutex_);
    const auto it = regions_.find(metrics.id);
    if (it == regions_.end()) {
        reason = "unknown-region";
        return false;
    }
    if (it->second.node_id != metrics.node_id ||
        it->second.node_generation != metrics.node_generation) {
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

void RegionRegistry::mark_node_offline(
    const std::string& node_id,
    const std::uint64_t generation) {
    if (database_) {
        database_->execute(
            "UPDATE ogl_regions SET state=? "
            "WHERE node_id=? AND node_generation=? AND state<>?",
            {std::string{"offline"}, node_id,
             std::to_string(generation),
             std::string{"offline"}});
        return;
    }

    std::scoped_lock lock(mutex_);
    bool changed = false;
    for (auto& [_, region] : regions_) {
        if (region.node_id == node_id &&
            region.node_generation == generation &&
            region.state != "offline") {
            region.state = "offline";
            changed = true;
        }
    }
    if (changed) persist_locked();
}

std::optional<RegionInfo> RegionRegistry::find(
    const std::string& id) const {
    if (database_) {
        const auto rows = database_->query(
            "SELECT id,name,node_id,state,grid_x,grid_y,"
            "node_generation,ticks,entities,avatars,"
            "physics_bodies,scene_events,terrain_revision,sim_fps "
            "FROM ogl_regions WHERE id=?",
            {id});
        if (rows.empty()) return std::nullopt;
        return row_to_region(rows.front());
    }

    std::scoped_lock lock(mutex_);
    const auto it = regions_.find(id);
    return it == regions_.end()
               ? std::nullopt
               : std::optional<RegionInfo>{it->second};
}

std::vector<RegionInfo> RegionRegistry::list() const {
    if (database_) {
        const auto rows = database_->query(
            "SELECT id,name,node_id,state,grid_x,grid_y,"
            "node_generation,ticks,entities,avatars,"
            "physics_bodies,scene_events,terrain_revision,sim_fps "
            "FROM ogl_regions ORDER BY grid_y,grid_x,id");
        std::vector<RegionInfo> result;
        result.reserve(rows.size());
        for (const auto& row : rows) {
            result.push_back(row_to_region(row));
        }
        return result;
    }

    std::scoped_lock lock(mutex_);
    std::vector<RegionInfo> result;
    result.reserve(regions_.size());
    for (const auto& [_, region] : regions_) {
        result.push_back(region);
    }
    std::sort(
        result.begin(), result.end(),
        [](const RegionInfo& a, const RegionInfo& b) {
            if (a.grid_y != b.grid_y) {
                return a.grid_y < b.grid_y;
            }
            return a.grid_x < b.grid_x;
        });
    return result;
}

std::vector<RegionInfo> RegionRegistry::neighbors(
    const std::string& id) const {
    if (database_) {
        const auto source = find(id);
        if (!source) return {};
        const auto rows = database_->query(
            "SELECT id,name,node_id,state,grid_x,grid_y,"
            "node_generation,ticks,entities,avatars,"
            "physics_bodies,scene_events,terrain_revision,sim_fps "
            "FROM ogl_regions WHERE id<>? AND "
            "((grid_x=? AND (grid_y=? OR grid_y=?)) OR "
            "(grid_y=? AND (grid_x=? OR grid_x=?))) "
            "ORDER BY grid_y,grid_x,id",
            {id,
             std::to_string(source->grid_x),
             std::to_string(source->grid_y - 1),
             std::to_string(source->grid_y + 1),
             std::to_string(source->grid_y),
             std::to_string(source->grid_x - 1),
             std::to_string(source->grid_x + 1)});
        std::vector<RegionInfo> result;
        result.reserve(rows.size());
        for (const auto& row : rows) {
            result.push_back(row_to_region(row));
        }
        return result;
    }

    std::scoped_lock lock(mutex_);
    const auto source = regions_.find(id);
    if (source == regions_.end()) return {};
    std::vector<RegionInfo> result;
    for (const auto& [other_id, region] : regions_) {
        if (other_id == id) continue;
        const auto dx =
            std::abs(
                region.grid_x - source->second.grid_x);
        const auto dy =
            std::abs(
                region.grid_y - source->second.grid_y);
        if ((dx == 1 && dy == 0) ||
            (dx == 0 && dy == 1)) {
            result.push_back(region);
        }
    }
    std::sort(
        result.begin(), result.end(),
        [](const RegionInfo& a, const RegionInfo& b) {
            if (a.grid_y != b.grid_y) {
                return a.grid_y < b.grid_y;
            }
            return a.grid_x < b.grid_x;
        });
    return result;
}

void RegionRegistry::load() {
    if (database_ || storage_path_.empty()) return;
    std::ifstream input(storage_path_);
    std::string line;
    while (std::getline(input, line)) {
        std::istringstream stream(line);
        RegionInfo region;
        stream >> std::quoted(region.id)
               >> std::quoted(region.name)
               >> std::quoted(region.node_id)
               >> std::quoted(region.state)
               >> region.grid_x
               >> region.grid_y
               >> region.node_generation
               >> region.ticks
               >> region.entities
               >> region.avatars
               >> region.physics_bodies
               >> region.sim_fps;
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
    if (database_ || storage_path_.empty()) return;
    const std::filesystem::path path(storage_path_);
    if (path.has_parent_path()) {
        std::filesystem::create_directories(
            path.parent_path());
    }
    const auto temporary = storage_path_ + ".tmp";
    std::ofstream output(
        temporary, std::ios::trunc);
    if (!output) {
        throw std::runtime_error(
            "cannot write region registry");
    }
    for (const auto& [_, region] : regions_) {
        output << std::quoted(region.id) << ' '
               << std::quoted(region.name) << ' '
               << std::quoted(region.node_id) << ' '
               << std::quoted(region.state) << ' '
               << region.grid_x << ' '
               << region.grid_y << ' '
               << region.node_generation << ' '
               << region.ticks << ' '
               << region.entities << ' '
               << region.avatars << ' '
               << region.physics_bodies << ' '
               << region.sim_fps << ' '
               << region.scene_events << ' '
               << region.terrain_revision << '\n';
    }
    output.close();
    if (!output) {
        throw std::runtime_error(
            "cannot flush region registry");
    }
    opengenesis::platform::replace_file(
        temporary, storage_path_);
}

} // namespace opengenesis::core
