#include "opengenesis/world/region_runtime.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace opengenesis::world {
namespace {
constexpr std::size_t kMaxSceneEvents = 4096;
constexpr std::uint64_t kMovementEventStride = 5;

double delta_squared(const physics::Vec3& a, const physics::Vec3& b) {
    const double dx = a.x - b.x;
    const double dy = a.y - b.y;
    const double dz = a.z - b.z;
    return dx * dx + dy * dy + dz * dz;
}
} // namespace

const char* entity_kind_name(const EntityKind kind) {
    return kind == EntityKind::avatar ? "avatar" : "object";
}

RegionRuntime::RegionRuntime(std::string id, const double hz, const double terrain_base_height)
    : id_(std::move(id)), target_hz_(std::clamp(hz, 1.0, 240.0)),
      terrain_(256, 256, 1.0, terrain_base_height) {
    physics_.set_ground_sampler([this](const double x, const double y) { return terrain_.sample(x, y); });
}

RegionRuntime::~RegionRuntime() { stop(); }

void RegionRuntime::start() {
    if (running_.exchange(true)) return;
    thread_ = std::thread(&RegionRuntime::loop, this);
}

void RegionRuntime::stop() {
    if (!running_.exchange(false)) return;
    if (thread_.joinable()) thread_.join();
}

std::uint64_t RegionRuntime::spawn_object(std::string name, Transform transform, const bool physical,
                                          std::string owner_user_id, std::string group_id,
                                          const core::PermissionMask group_permissions,
                                          const core::PermissionMask everyone_permissions) {
    return spawn_entity(std::move(name), std::move(owner_user_id), std::move(group_id), EntityKind::object, transform, physical, group_permissions, everyone_permissions);
}

std::uint64_t RegionRuntime::spawn_avatar(std::string user_id, std::string name, Transform transform) {
    return spawn_entity(std::move(name), std::move(user_id), {}, EntityKind::avatar, transform, true);
}

bool RegionRuntime::restore_object(const std::uint64_t id, std::string name, Transform transform,
                                   const bool physical, std::string owner_user_id, std::string group_id,
                                   const core::PermissionMask owner_permissions,
                                   const core::PermissionMask group_permissions,
                                   const core::PermissionMask everyone_permissions) {
    if (id == 0) return false;
    std::scoped_lock lock(mutex_);
    if (entities_.contains(id)) return false;
    Entity entity{.id = id, .name = std::move(name), .owner_user_id = std::move(owner_user_id), .group_id = std::move(group_id), .owner_permissions = owner_permissions, .group_permissions = group_permissions, .everyone_permissions = everyone_permissions, .kind = EntityKind::object, .transform = transform};
    if (physical) {
        const double radius = std::max(0.1, transform.scale.z * 0.5);
        entity.physics_body = physics_.add_body({.position = transform.position, .radius = radius});
    }
    entities_[id] = entity;
    next_entity_ = std::max(next_entity_, id + 1);
    append_event_locked("entity_restored", id, transform, entity.name);
    return true;
}

std::uint64_t RegionRuntime::spawn_entity(std::string name, std::string owner_user_id, std::string group_id,
                                          const EntityKind kind, Transform transform, const bool physical,
                                          const core::PermissionMask group_permissions,
                                          const core::PermissionMask everyone_permissions) {
    if (transform.position.z == 0.0) {
        transform.position.x = 128.0;
        transform.position.y = 128.0;
        transform.position.z = terrain_.sample(transform.position.x, transform.position.y) + 1.0;
    }

    std::scoped_lock lock(mutex_);
    const auto id = next_entity_++;
    Entity entity{.id = id, .name = std::move(name), .owner_user_id = std::move(owner_user_id), .group_id = std::move(group_id), .owner_permissions = core::perm_all, .group_permissions = group_permissions, .everyone_permissions = everyone_permissions, .kind = kind, .transform = transform};
    if (physical) {
        const double radius = kind == EntityKind::avatar ? 0.45 : std::max(0.1, transform.scale.z * 0.5);
        entity.physics_body = physics_.add_body({.position = transform.position, .radius = radius});
    }
    entities_[id] = entity;
    append_event_locked("entity_created", id, transform, entity.name);
    return id;
}

bool RegionRuntime::remove_entity(const std::uint64_t id) {
    std::scoped_lock lock(mutex_);
    const auto it = entities_.find(id);
    if (it == entities_.end()) return false;
    if (it->second.physics_body) physics_.remove_body(it->second.physics_body);
    append_event_locked("entity_deleted", id, it->second.transform, it->second.name);
    entities_.erase(it);
    return true;
}

bool RegionRuntime::update_transform(const std::uint64_t id, Transform transform) {
    std::scoped_lock lock(mutex_);
    const auto it = entities_.find(id);
    if (it == entities_.end()) return false;
    it->second.transform = transform;
    if (it->second.physics_body) physics_.set_body_position(it->second.physics_body, transform.position);
    append_event_locked("entity_updated", id, transform);
    return true;
}

bool RegionRuntime::set_velocity(const std::uint64_t id, const physics::Vec3 velocity) {
    std::scoped_lock lock(mutex_);
    const auto it = entities_.find(id);
    if (it == entities_.end() || !it->second.physics_body) return false;
    return physics_.set_body_velocity(it->second.physics_body, velocity);
}

bool RegionRuntime::set_physical(const std::uint64_t id, const bool enabled) {
    std::scoped_lock lock(mutex_);
    const auto it = entities_.find(id);
    if (it == entities_.end()) return false;
    auto& entity = it->second;
    if (enabled && entity.physics_body == 0) {
        const double radius = entity.kind == EntityKind::avatar
                                  ? 0.45
                                  : std::max(0.1, entity.transform.scale.z * 0.5);
        entity.physics_body =
            physics_.add_body({.position = entity.transform.position, .radius = radius});
    } else if (!enabled && entity.physics_body != 0) {
        physics_.remove_body(entity.physics_body);
        entity.physics_body = 0;
    }
    append_event_locked("physics_updated", id, entity.transform, enabled ? "1" : "0");
    return true;
}


bool RegionRuntime::set_object_permissions(const std::uint64_t id, std::string group_id,
                                           const core::PermissionMask group_permissions,
                                           const core::PermissionMask everyone_permissions) {
    std::scoped_lock lock(mutex_);
    const auto it = entities_.find(id);
    if (it == entities_.end() || it->second.kind != EntityKind::object) return false;
    it->second.group_id = std::move(group_id);
    it->second.group_permissions = group_permissions & core::perm_all;
    it->second.everyone_permissions = everyone_permissions & core::perm_all;
    append_event_locked("permissions_updated", id, it->second.transform);
    return true;
}

bool RegionRuntime::move_avatar(const std::uint64_t id, Transform transform,
                                const physics::Vec3 velocity, std::string& boundary) {
    boundary.clear();
    const double max_x = static_cast<double>(terrain_.width()) * terrain_.cell_size();
    const double max_y = static_cast<double>(terrain_.height()) * terrain_.cell_size();
    if (transform.position.x < 0.0) boundary = "west";
    else if (transform.position.x >= max_x) boundary = "east";
    else if (transform.position.y < 0.0) boundary = "south";
    else if (transform.position.y >= max_y) boundary = "north";

    transform.position.x = std::clamp(transform.position.x, 0.25, max_x - 0.25);
    transform.position.y = std::clamp(transform.position.y, 0.25, max_y - 0.25);
    const auto ground = terrain_.sample(transform.position.x, transform.position.y);
    transform.position.z = std::max(transform.position.z, ground + 0.45);

    std::scoped_lock lock(mutex_);
    const auto it = entities_.find(id);
    if (it == entities_.end() || it->second.kind != EntityKind::avatar || !it->second.physics_body) {
        return false;
    }
    it->second.transform = transform;
    (void)physics_.set_body_position(it->second.physics_body, transform.position);
    (void)physics_.set_body_velocity(it->second.physics_body, velocity);
    append_event_locked("avatar_move", id, transform);
    if (!boundary.empty()) append_event_locked("region_boundary", id, transform, boundary);
    return true;
}

bool RegionRuntime::set_terrain_height(const std::size_t x, const std::size_t y, const double value) {
    if (!terrain_.set_height(x, y, value)) return false;
    std::scoped_lock lock(mutex_);
    Transform transform;
    transform.position = {static_cast<double>(x) * terrain_.cell_size(),
                          static_cast<double>(y) * terrain_.cell_size(), value};
    append_event_locked("terrain_updated", 0, transform, std::to_string(terrain_.revision()));
    return true;
}

std::optional<ObjectTransferSnapshot> RegionRuntime::export_object(
    const std::uint64_t id) const {
    std::scoped_lock lock(mutex_);
    const auto it = entities_.find(id);
    if (it == entities_.end() || it->second.kind != EntityKind::object) {
        return std::nullopt;
    }

    ObjectTransferSnapshot snapshot{
        .source_entity_id = it->second.id,
        .name = it->second.name,
        .owner_user_id = it->second.owner_user_id,
        .group_id = it->second.group_id,
        .owner_permissions = it->second.owner_permissions,
        .group_permissions = it->second.group_permissions,
        .everyone_permissions = it->second.everyone_permissions,
        .transform = it->second.transform,
        .velocity = {},
        .physical = it->second.physics_body != 0};

    if (it->second.physics_body != 0) {
        snapshot.velocity = physics_.body(it->second.physics_body).velocity;
    }
    return snapshot;
}

bool RegionRuntime::import_object(
    const ObjectTransferSnapshot& snapshot,
    const std::uint64_t destination_entity_id,
    physics::Vec3 destination_position,
    std::string& reason) {
    if (destination_entity_id == 0 || snapshot.source_entity_id == 0 ||
        snapshot.name.size() > 256U || snapshot.owner_user_id.empty() ||
        snapshot.owner_user_id.size() > 256U || snapshot.group_id.size() > 256U ||
        !std::isfinite(destination_position.x) ||
        !std::isfinite(destination_position.y) ||
        !std::isfinite(destination_position.z) ||
        !std::isfinite(snapshot.velocity.x) ||
        !std::isfinite(snapshot.velocity.y) ||
        !std::isfinite(snapshot.velocity.z)) {
        reason = "invalid-object-transfer-snapshot";
        return false;
    }

    const double max_x =
        static_cast<double>(terrain_.width()) * terrain_.cell_size();
    const double max_y =
        static_cast<double>(terrain_.height()) * terrain_.cell_size();
    destination_position.x =
        std::clamp(destination_position.x, 0.25, max_x - 0.25);
    destination_position.y =
        std::clamp(destination_position.y, 0.25, max_y - 0.25);
    if (destination_position.z <= 0.0) {
        destination_position.z = snapshot.transform.position.z;
    }
    const auto floor = terrain_.sample(
        destination_position.x, destination_position.y);
    destination_position.z =
        std::max(destination_position.z,
                 floor + std::max(0.1, snapshot.transform.scale.z * 0.5));

    {
        std::scoped_lock lock(mutex_);
        const auto existing = entities_.find(destination_entity_id);
        if (existing != entities_.end()) {
            const auto& entity = existing->second;
            if (entity.kind == EntityKind::object &&
                entity.owner_user_id == snapshot.owner_user_id &&
                entity.name == snapshot.name &&
                entity.group_id == snapshot.group_id) {
                reason.clear();
                return true;
            }
            reason = "destination-entity-id-collision";
            return false;
        }
    }

    auto transform = snapshot.transform;
    transform.position = destination_position;
    if (!restore_object(destination_entity_id, snapshot.name, transform,
                        snapshot.physical, snapshot.owner_user_id,
                        snapshot.group_id, snapshot.owner_permissions,
                        snapshot.group_permissions,
                        snapshot.everyone_permissions)) {
        reason = "destination-object-restore-failed";
        return false;
    }
    if (snapshot.physical &&
        !set_velocity(destination_entity_id, snapshot.velocity)) {
        (void)remove_entity(destination_entity_id);
        reason = "destination-object-velocity-restore-failed";
        return false;
    }
    reason.clear();
    return true;
}

std::optional<Entity> RegionRuntime::entity(const std::uint64_t id) const {
    std::scoped_lock lock(mutex_);
    const auto it = entities_.find(id);
    if (it == entities_.end()) return std::nullopt;
    return it->second;
}

std::vector<Entity> RegionRuntime::snapshot_entities() const {
    std::scoped_lock lock(mutex_);
    std::vector<Entity> result;
    result.reserve(entities_.size());
    for (const auto& [_, entity] : entities_) result.push_back(entity);
    std::sort(result.begin(), result.end(), [](const Entity& a, const Entity& b) { return a.id < b.id; });
    return result;
}

std::vector<SceneEvent> RegionRuntime::events_since(const std::uint64_t sequence,
                                                     const std::size_t max_events) const {
    std::scoped_lock lock(mutex_);
    std::vector<SceneEvent> result;
    result.reserve(std::min(max_events, events_.size()));
    for (const auto& event : events_) {
        if (event.sequence <= sequence) continue;
        result.push_back(event);
        if (result.size() >= max_events) break;
    }
    return result;
}

std::uint64_t RegionRuntime::chat(const std::uint64_t sender_entity, std::string text,
                                  std::string event_type) {
    if (text.size() > 512) text.resize(512);
    if (event_type != "chat" && event_type != "chat_whisper" &&
        event_type != "chat_shout") {
        event_type = "chat";
    }
    std::scoped_lock lock(mutex_);
    if (sender_entity != 0 && !entities_.contains(sender_entity)) return 0;
    return append_event_locked(std::move(event_type), sender_entity, {}, std::move(text));
}

RuntimeMetrics RegionRuntime::metrics() const {
    std::scoped_lock lock(mutex_);
    std::uint64_t avatars = 0;
    for (const auto& [_, entity] : entities_) {
        if (entity.kind == EntityKind::avatar) ++avatars;
    }
    return {.ticks = ticks_.load(),
            .entities = entities_.size(),
            .avatars = avatars,
            .physics_bodies = physics_.body_count(),
            .scene_events = next_event_ - 1,
            .terrain_revision = terrain_.revision(),
            .sim_fps = sim_fps_.load()};
}

std::uint64_t RegionRuntime::latest_sequence() const {
    std::scoped_lock lock(mutex_);
    return next_event_ - 1;
}

std::uint64_t RegionRuntime::append_event_locked(std::string type, const std::uint64_t entity_id,
                                                 const Transform& transform, std::string text) {
    const auto sequence = next_event_++;
    events_.push_back({.sequence = sequence,
                       .type = std::move(type),
                       .entity_id = entity_id,
                       .transform = transform,
                       .text = std::move(text)});
    while (events_.size() > kMaxSceneEvents) events_.pop_front();
    return sequence;
}

void RegionRuntime::loop() {
    using clock = std::chrono::steady_clock;
    const auto step = std::chrono::duration<double>(1.0 / target_hz_);
    auto next = clock::now();
    auto fps_start = next;
    std::uint64_t fps_ticks = 0;

    while (running_) {
        next += std::chrono::duration_cast<clock::duration>(step);
        physics_.step(step.count());
        const auto tick = ticks_.fetch_add(1) + 1;
        ++fps_ticks;

        if (tick % kMovementEventStride == 0) {
            std::scoped_lock lock(mutex_);
            for (auto& [id, entity] : entities_) {
                if (!entity.physics_body) continue;
                const auto body = physics_.body(entity.physics_body);
                if (delta_squared(body.position, entity.transform.position) > 0.000001) {
                    entity.transform.position = body.position;
                    append_event_locked("entity_updated", id, entity.transform);
                }
            }
        }

        const auto now = clock::now();
        if (now - fps_start >= std::chrono::seconds(1)) {
            const auto seconds = std::chrono::duration<double>(now - fps_start).count();
            sim_fps_.store(static_cast<double>(fps_ticks) / seconds);
            fps_ticks = 0;
            fps_start = now;
        }
        std::this_thread::sleep_until(next);
    }
}

} // namespace opengenesis::world
