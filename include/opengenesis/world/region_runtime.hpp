#pragma once

#include "opengenesis/physics/physics_world.hpp"
#include "opengenesis/world/terrain.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace opengenesis::world {

enum class EntityKind { object, avatar };

struct Transform {
    physics::Vec3 position{};
    physics::Vec3 rotation{};
    physics::Vec3 scale{1.0, 1.0, 1.0};
};

struct Entity {
    std::uint64_t id{0};
    std::string name;
    EntityKind kind{EntityKind::object};
    Transform transform{};
    std::uint64_t physics_body{0};
};

struct SceneEvent {
    std::uint64_t sequence{0};
    std::string type;
    std::uint64_t entity_id{0};
    Transform transform{};
    std::string text;
};

struct RuntimeMetrics {
    std::uint64_t ticks{0};
    std::uint64_t entities{0};
    std::uint64_t avatars{0};
    std::uint64_t physics_bodies{0};
    std::uint64_t scene_events{0};
    std::uint64_t terrain_revision{0};
    double sim_fps{0.0};
};

class RegionRuntime final {
public:
    RegionRuntime(std::string id, double target_hz, double terrain_base_height = 21.0);
    ~RegionRuntime();
    RegionRuntime(const RegionRuntime&) = delete;
    RegionRuntime& operator=(const RegionRuntime&) = delete;

    void start();
    void stop();

    std::uint64_t spawn_object(std::string name, Transform transform = {}, bool physical = true);
    std::uint64_t spawn_avatar(std::string name, Transform transform = {});
    bool remove_entity(std::uint64_t id);
    bool update_transform(std::uint64_t id, Transform transform);
    bool set_velocity(std::uint64_t id, physics::Vec3 velocity);

    [[nodiscard]] std::optional<Entity> entity(std::uint64_t id) const;
    [[nodiscard]] std::vector<Entity> snapshot_entities() const;
    [[nodiscard]] std::vector<SceneEvent> events_since(std::uint64_t sequence,
                                                        std::size_t max_events = 256) const;
    std::uint64_t chat(std::uint64_t sender_entity, std::string text);

    [[nodiscard]] RuntimeMetrics metrics() const;
    [[nodiscard]] std::uint64_t latest_sequence() const;
    [[nodiscard]] const std::string& id() const { return id_; }
    [[nodiscard]] Terrain& terrain() { return terrain_; }
    [[nodiscard]] const Terrain& terrain() const { return terrain_; }

private:
    void loop();
    std::uint64_t spawn_entity(std::string name, EntityKind kind, Transform transform, bool physical);
    std::uint64_t append_event_locked(std::string type, std::uint64_t entity_id,
                                      const Transform& transform, std::string text = {});

    std::string id_;
    double target_hz_;
    Terrain terrain_;
    std::atomic_bool running_{false};
    std::thread thread_;
    mutable std::mutex mutex_;
    std::unordered_map<std::uint64_t, Entity> entities_;
    std::deque<SceneEvent> events_;
    std::uint64_t next_entity_{1};
    std::uint64_t next_event_{1};
    std::atomic<std::uint64_t> ticks_{0};
    std::atomic<double> sim_fps_{0};
    physics::PhysicsWorld physics_;
};

[[nodiscard]] const char* entity_kind_name(EntityKind kind);

} // namespace opengenesis::world
