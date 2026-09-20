#pragma once

#include "opengenesis/core/permissions.hpp"
#include "opengenesis/physics/physics_world.hpp"
#include "opengenesis/world/terrain.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
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
    std::string owner_user_id;
    std::string group_id;
    core::PermissionMask owner_permissions{core::perm_all};
    core::PermissionMask group_permissions{0};
    core::PermissionMask everyone_permissions{0};
    EntityKind kind{EntityKind::object};
    Transform transform{};
    std::uint64_t physics_body{0};
    std::uint64_t parent_entity_id{0};
    std::uint32_t link_number{1};
    std::string floating_text;
};

struct ObjectTransferSnapshot {
    std::uint64_t source_entity_id{0};
    std::string name;
    std::string owner_user_id;
    std::string group_id;
    core::PermissionMask owner_permissions{core::perm_all};
    core::PermissionMask group_permissions{0};
    core::PermissionMask everyone_permissions{0};
    Transform transform{};
    physics::Vec3 velocity{};
    physics::Vec3 angular_velocity{};
    bool physical{false};
    std::uint64_t parent_source_entity_id{0};
    std::uint32_t link_number{1};
    std::string floating_text;
};

struct ObjectLinksetTransferSnapshot {
    std::uint64_t source_root_entity_id{0};
    std::vector<ObjectTransferSnapshot> members;
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
    std::uint64_t collision_contacts{0};
    std::uint64_t physics_constraints{0};
    double sim_fps{0.0};
};

class RegionRuntime final {
public:
    RegionRuntime(std::string id, double target_hz,
                  double terrain_base_height = 21.0,
                  double water_height = 20.0);
    ~RegionRuntime();
    RegionRuntime(const RegionRuntime&) = delete;
    RegionRuntime& operator=(const RegionRuntime&) = delete;

    void start();
    void stop();

    std::uint64_t spawn_object(
        std::string name, Transform transform = {}, bool physical = true,
        std::string owner_user_id = {}, std::string group_id = {},
        core::PermissionMask group_permissions = 0,
        core::PermissionMask everyone_permissions = 0);
    std::uint64_t spawn_avatar(
        std::string user_id, std::string name, Transform transform = {});
    bool restore_object(
        std::uint64_t id, std::string name, Transform transform, bool physical,
        std::string owner_user_id = {}, std::string group_id = {},
        core::PermissionMask owner_permissions = core::perm_all,
        core::PermissionMask group_permissions = 0,
        core::PermissionMask everyone_permissions = 0,
        std::uint64_t parent_entity_id = 0,
        std::uint32_t link_number = 1,
        std::string floating_text = {},
        physics::Vec3 velocity = {},
        physics::Vec3 angular_velocity = {});

    bool remove_entity(std::uint64_t id);
    bool remove_linkset(std::uint64_t entity_id);
    bool update_transform(std::uint64_t id, Transform transform);
    bool set_velocity(std::uint64_t id, physics::Vec3 velocity);
    bool set_angular_velocity(std::uint64_t id,
                              physics::Vec3 angular_velocity);
    bool apply_force(std::uint64_t id, physics::Vec3 force);
    bool apply_impulse(std::uint64_t id, physics::Vec3 impulse);
    bool apply_torque(std::uint64_t id, physics::Vec3 torque);
    bool set_physics_material(std::uint64_t id, double mass,
                              double restitution, double friction);
    bool set_buoyancy(std::uint64_t id, double buoyancy);
    bool set_physical(std::uint64_t id, bool enabled);
    std::uint64_t constrain_distance(std::uint64_t entity_a,
                                     std::uint64_t entity_b,
                                     double rest_length,
                                     double stiffness,
                                     std::string& reason);
    bool remove_constraint(std::uint64_t constraint_id);
    bool set_floating_text(std::uint64_t id, std::string text);
    bool link_objects(std::uint64_t root_id, std::uint64_t child_id,
                      std::string& reason);
    bool unlink_object(std::uint64_t child_id, std::string& reason);
    bool set_object_permissions(
        std::uint64_t id, std::string group_id,
        core::PermissionMask group_permissions,
        core::PermissionMask everyone_permissions);
    bool move_avatar(std::uint64_t id, Transform transform,
                     physics::Vec3 velocity, std::string& boundary);
    bool set_terrain_height(std::size_t x, std::size_t y, double value);

    [[nodiscard]] std::optional<ObjectTransferSnapshot> export_object(
        std::uint64_t id) const;
    [[nodiscard]] std::optional<ObjectLinksetTransferSnapshot> export_linkset(
        std::uint64_t entity_id) const;
    bool import_object(const ObjectTransferSnapshot& snapshot,
                       std::uint64_t destination_entity_id,
                       physics::Vec3 destination_position,
                       std::string& reason);
    bool import_linkset(
        const ObjectLinksetTransferSnapshot& snapshot,
        std::uint64_t destination_root_entity_id,
        physics::Vec3 destination_position,
        std::vector<std::pair<std::uint64_t, std::uint64_t>>& entity_map,
        std::string& reason,
        bool preserve_source_ids = false);

    [[nodiscard]] std::optional<Entity> entity(std::uint64_t id) const;
    [[nodiscard]] std::optional<physics::Body> physics_body_state(
        std::uint64_t id) const;
    [[nodiscard]] std::vector<Entity> linkset_members(
        std::uint64_t entity_id) const;
    [[nodiscard]] std::vector<Entity> snapshot_entities() const;
    [[nodiscard]] std::vector<SceneEvent> events_since(
        std::uint64_t sequence, std::size_t max_events = 256) const;
    std::uint64_t chat(std::uint64_t sender_entity, std::string text,
                       std::string event_type = "chat");

    [[nodiscard]] RuntimeMetrics metrics() const;
    [[nodiscard]] std::uint64_t latest_sequence() const;
    [[nodiscard]] const std::string& id() const { return id_; }
    [[nodiscard]] double water_height() const { return water_height_; }
    [[nodiscard]] Terrain& terrain() { return terrain_; }
    [[nodiscard]] const Terrain& terrain() const { return terrain_; }

private:
    void loop();
    std::uint64_t spawn_entity(
        std::string name, std::string owner_user_id, std::string group_id,
        EntityKind kind, Transform transform, bool physical,
        core::PermissionMask group_permissions = 0,
        core::PermissionMask everyone_permissions = 0);
    std::uint64_t append_event_locked(
        std::string type, std::uint64_t entity_id,
        const Transform& transform, std::string text = {});

    std::string id_;
    double target_hz_;
    double water_height_;
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
    std::atomic<std::uint64_t> collision_contacts_{0};
    physics::PhysicsWorld physics_;
    std::set<std::pair<std::uint64_t, std::uint64_t>> active_collisions_;
    std::set<std::uint64_t> active_ground_contacts_;
};

[[nodiscard]] const char* entity_kind_name(EntityKind kind);

} // namespace opengenesis::world
