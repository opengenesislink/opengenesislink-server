#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>
#include <memory>

namespace opengenesis::storage { class DatabasePool; }

namespace opengenesis::core {

struct RegionInfo {
    std::string id, name, node_id, state{"offline"};
    std::int32_t grid_x{0}, grid_y{0};
    std::uint64_t node_generation{0};
    std::uint64_t ticks{0};
    std::uint64_t entities{0};
    std::uint64_t avatars{0};
    std::uint64_t physics_bodies{0};
    std::uint64_t scene_events{0};
    std::uint64_t terrain_revision{0};
    double sim_fps{0.0};
};

class RegionRegistry final {
public:
    explicit RegionRegistry(std::string storage_path = {});
    explicit RegionRegistry(std::shared_ptr<storage::DatabasePool> database);
    bool register_region(RegionInfo region, std::string& reason);
    bool update_state(const std::string& id, const std::string& node_id, std::uint64_t generation,
                      const std::string& state, std::string& reason);
    bool update_metrics(const RegionInfo& metrics, std::string& reason);
    void mark_node_offline(const std::string& node_id, std::uint64_t generation);
    [[nodiscard]] std::optional<RegionInfo> find(const std::string& id) const;
    [[nodiscard]] std::vector<RegionInfo> list() const;
    [[nodiscard]] std::vector<RegionInfo> neighbors(const std::string& id) const;

private:
    static bool valid_transition(const std::string& from, const std::string& to);
    void load();
    void persist_locked() const;

    std::string storage_path_;
    std::shared_ptr<storage::DatabasePool> database_;
    mutable std::mutex mutex_;
    std::unordered_map<std::string, RegionInfo> regions_;
};

} // namespace opengenesis::core
