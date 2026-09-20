#include "opengenesis/world/region_runtime.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace opengenesis::world {
namespace {
constexpr std::size_t kMaxSceneEvents = 4096;
constexpr std::uint64_t kMovementEventStride = 5;
constexpr std::size_t kMaxLinksetMembers = 64;

double delta_squared(const physics::Vec3& a, const physics::Vec3& b) {
    const double dx = a.x - b.x;
    const double dy = a.y - b.y;
    const double dz = a.z - b.z;
    return dx * dx + dy * dy + dz * dz;
}

bool finite_vec(const physics::Vec3& value) {
    return std::isfinite(value.x) && std::isfinite(value.y) &&
           std::isfinite(value.z);
}

double normalized_degrees(double value) {
    if (!std::isfinite(value)) return 0.0;
    value = std::fmod(value, 360.0);
    if (value < 0.0) value += 360.0;
    return value;
}

double radians(const double degrees) {
    return degrees * 3.14159265358979323846 / 180.0;
}

physics::Vec3 rotate_x(physics::Vec3 value, const double angle) {
    const auto cosine = std::cos(angle);
    const auto sine = std::sin(angle);
    return {
        value.x,
        value.y * cosine - value.z * sine,
        value.y * sine + value.z * cosine};
}

physics::Vec3 rotate_y(physics::Vec3 value, const double angle) {
    const auto cosine = std::cos(angle);
    const auto sine = std::sin(angle);
    return {
        value.x * cosine + value.z * sine,
        value.y,
        -value.x * sine + value.z * cosine};
}

physics::Vec3 rotate_z(physics::Vec3 value, const double angle) {
    const auto cosine = std::cos(angle);
    const auto sine = std::sin(angle);
    return {
        value.x * cosine - value.y * sine,
        value.x * sine + value.y * cosine,
        value.z};
}

physics::Vec3 rotate_euler(
    physics::Vec3 value, const physics::Vec3& rotation) {
    value = rotate_x(value, radians(rotation.x));
    value = rotate_y(value, radians(rotation.y));
    value = rotate_z(value, radians(rotation.z));
    return value;
}

physics::Vec3 inverse_rotate_euler(
    physics::Vec3 value, const physics::Vec3& rotation) {
    value = rotate_z(value, -radians(rotation.z));
    value = rotate_y(value, -radians(rotation.y));
    value = rotate_x(value, -radians(rotation.x));
    return value;
}

physics::Vec3 subtract(
    const physics::Vec3& left, const physics::Vec3& right) {
    return {
        left.x - right.x,
        left.y - right.y,
        left.z - right.z};
}

physics::Vec3 add(
    const physics::Vec3& left, const physics::Vec3& right) {
    return {
        left.x + right.x,
        left.y + right.y,
        left.z + right.z};
}

physics::Vec3 rotation_delta(
    const physics::Vec3& next, const physics::Vec3& previous) {
    auto delta_axis = [](double value) {
        value = std::fmod(value + 180.0, 360.0);
        if (value < 0.0) value += 360.0;
        return value - 180.0;
    };
    return {
        delta_axis(next.x - previous.x),
        delta_axis(next.y - previous.y),
        delta_axis(next.z - previous.z)};
}

std::uint64_t transfer_member_id(const std::uint64_t destination_root,
                                 const std::uint64_t source_entity,
                                 const std::uint32_t link_number) {
    std::uint64_t value =
        destination_root ^ (source_entity + 0x9e3779b97f4a7c15ULL +
                            (destination_root << 6U) +
                            (destination_root >> 2U));
    value ^= static_cast<std::uint64_t>(link_number) *
             0xbf58476d1ce4e5b9ULL;
    value ^= value >> 30U;
    value *= 0xbf58476d1ce4e5b9ULL;
    value ^= value >> 27U;
    value *= 0x94d049bb133111ebULL;
    value ^= value >> 31U;
    value |= 0x8000000000000000ULL;
    if (value == destination_root) value ^= 0x4000000000000000ULL;
    if (value == 0ULL) value = 0x8000000000000001ULL;
    return value;
}

} // namespace

const char* entity_kind_name(const EntityKind kind) {
    return kind == EntityKind::avatar ? "avatar" : "object";
}

RegionRuntime::RegionRuntime(std::string id, const double hz,
                             const double terrain_base_height,
                             const double water_height)
    : id_(std::move(id)),
      target_hz_(std::clamp(hz, 1.0, 240.0)),
      water_height_(std::isfinite(water_height) ? water_height : 20.0),
      terrain_(256, 256, 1.0, terrain_base_height) {
    physics_.set_ground_sampler(
        [this](const double x, const double y) {
            return terrain_.sample(x, y);
        });
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

std::uint64_t RegionRuntime::spawn_object(
    std::string name, Transform transform, const bool physical,
    std::string owner_user_id, std::string group_id,
    const core::PermissionMask group_permissions,
    const core::PermissionMask everyone_permissions) {
    return spawn_entity(
        std::move(name), std::move(owner_user_id), std::move(group_id),
        EntityKind::object, transform, physical, group_permissions,
        everyone_permissions);
}

std::uint64_t RegionRuntime::spawn_avatar(
    std::string user_id, std::string name, Transform transform) {
    return spawn_entity(
        std::move(name), std::move(user_id), {}, EntityKind::avatar,
        transform, true);
}

bool RegionRuntime::restore_object(
    const std::uint64_t id, std::string name, Transform transform,
    const bool physical, std::string owner_user_id, std::string group_id,
    const core::PermissionMask owner_permissions,
    const core::PermissionMask group_permissions,
    const core::PermissionMask everyone_permissions,
    const std::uint64_t parent_entity_id,
    const std::uint32_t link_number,
    std::string floating_text,
    const physics::Vec3 velocity,
    const physics::Vec3 angular_velocity,
    const double mass,
    const double restitution,
    const double friction,
    const double linear_damping,
    const double angular_damping,
    const double gravity_scale,
    const double buoyancy,
    const physics::CollisionShape collision_shape,
    const physics::Vec3 half_extents,
    const double capsule_half_height) {
    if (id == 0 || name.size() > 256U || owner_user_id.size() > 256U ||
        group_id.size() > 256U || floating_text.size() > 512U ||
        !finite_vec(transform.position) || !finite_vec(transform.rotation) ||
        !finite_vec(transform.scale) || !finite_vec(velocity) ||
        !finite_vec(angular_velocity) ||
        !std::isfinite(mass) || !std::isfinite(restitution) ||
        !std::isfinite(friction) || !std::isfinite(linear_damping) ||
        !std::isfinite(angular_damping) || !std::isfinite(gravity_scale) ||
        !std::isfinite(buoyancy) || !finite_vec(half_extents) ||
        half_extents.x <= 0.0 || half_extents.y <= 0.0 ||
        half_extents.z <= 0.0 ||
        !std::isfinite(capsule_half_height) ||
        capsule_half_height < 0.0) {
        return false;
    }

    std::scoped_lock lock(mutex_);
    if (entities_.contains(id)) return false;

    Entity entity{
        .id = id,
        .name = std::move(name),
        .owner_user_id = std::move(owner_user_id),
        .group_id = std::move(group_id),
        .owner_permissions = owner_permissions & core::perm_all,
        .group_permissions = group_permissions & core::perm_all,
        .everyone_permissions = everyone_permissions & core::perm_all,
        .kind = EntityKind::object,
        .transform = transform,
        .physics_body = 0,
        .parent_entity_id = parent_entity_id,
        .link_number = parent_entity_id == 0
                           ? 1U
                           : std::max<std::uint32_t>(2U, link_number),
        .floating_text = std::move(floating_text)};

    if (physical && parent_entity_id == 0) {
        const double radius = std::max(0.1, transform.scale.z * 0.5);
        entity.physics_body = physics_.add_body(
            {.position = transform.position,
             .velocity = velocity,
             .rotation = transform.rotation,
             .angular_velocity = angular_velocity,
             .mass = mass,
             .restitution = restitution,
             .friction = friction,
             .radius = radius,
             .half_extents = half_extents,
             .capsule_half_height = capsule_half_height,
             .linear_damping = linear_damping,
             .angular_damping = angular_damping,
             .gravity_scale = gravity_scale,
             .buoyancy = buoyancy,
             .shape = collision_shape});
    }

    entities_[id] = entity;
    next_entity_ = std::max(next_entity_, id + 1);
    append_event_locked("entity_restored", id, transform, entity.name);
    return true;
}

std::uint64_t RegionRuntime::spawn_entity(
    std::string name, std::string owner_user_id, std::string group_id,
    const EntityKind kind, Transform transform, const bool physical,
    const core::PermissionMask group_permissions,
    const core::PermissionMask everyone_permissions) {
    if (transform.position.z == 0.0) {
        transform.position.x = 128.0;
        transform.position.y = 128.0;
        transform.position.z =
            terrain_.sample(transform.position.x, transform.position.y) + 1.0;
    }

    std::scoped_lock lock(mutex_);
    const auto id = next_entity_++;
    Entity entity{
        .id = id,
        .name = std::move(name),
        .owner_user_id = std::move(owner_user_id),
        .group_id = std::move(group_id),
        .owner_permissions = core::perm_all,
        .group_permissions = group_permissions,
        .everyone_permissions = everyone_permissions,
        .kind = kind,
        .transform = transform,
        .physics_body = 0,
        .parent_entity_id = 0,
        .link_number = 1,
        .floating_text = {}};

    if (physical) {
        const auto half_extents = physics::Vec3{
            std::max(0.05, transform.scale.x * 0.5),
            std::max(0.05, transform.scale.y * 0.5),
            std::max(0.05, transform.scale.z * 0.5)};
        if (kind == EntityKind::avatar) {
            entity.physics_body = physics_.add_body(
                {.position = transform.position,
                 .rotation = transform.rotation,
                 .radius = 0.45,
                 .half_extents = {0.45, 0.45, 0.9},
                 .capsule_half_height = 0.45,
                 .shape = physics::CollisionShape::capsule,
                 .character = true});
            (void)physics_.set_character_controller(
                entity.physics_body, 50.0, 0.45, 5.0);
        } else {
            entity.physics_body = physics_.add_body(
                {.position = transform.position,
                 .rotation = transform.rotation,
                 .radius = std::max(
                     {0.1, half_extents.x,
                      half_extents.y, half_extents.z}),
                 .half_extents = half_extents,
                 .shape = physics::CollisionShape::box});
        }
    }
    entities_[id] = entity;
    append_event_locked("entity_created", id, transform, entity.name);
    return id;
}

bool RegionRuntime::remove_entity(const std::uint64_t id) {
    std::scoped_lock lock(mutex_);
    const auto it = entities_.find(id);
    if (it == entities_.end()) return false;

    if (it->second.kind == EntityKind::object &&
        it->second.parent_entity_id == 0) {
        for (auto& [child_id, child] : entities_) {
            if (child.parent_entity_id != id) continue;
            child.parent_entity_id = 0;
            child.link_number = 1;
            append_event_locked(
                "entity_unlinked", child_id, child.transform,
                std::to_string(id));
        }
    }

    if (it->second.physics_body != 0) {
        (void)physics_.remove_body(it->second.physics_body);
    }
    append_event_locked(
        "entity_deleted", id, it->second.transform, it->second.name);
    entities_.erase(it);
    return true;
}

bool RegionRuntime::remove_linkset(const std::uint64_t entity_id) {
    std::scoped_lock lock(mutex_);
    const auto it = entities_.find(entity_id);
    if (it == entities_.end() || it->second.kind != EntityKind::object) {
        return false;
    }
    const auto root_id =
        it->second.parent_entity_id == 0
            ? it->second.id
            : it->second.parent_entity_id;

    std::vector<std::uint64_t> ids;
    for (const auto& [id, entity] : entities_) {
        if (id == root_id || entity.parent_entity_id == root_id) {
            ids.push_back(id);
        }
    }
    if (ids.empty()) return false;

    for (const auto id : ids) {
        const auto current = entities_.find(id);
        if (current == entities_.end()) continue;
        if (current->second.physics_body != 0) {
            (void)physics_.remove_body(current->second.physics_body);
        }
        append_event_locked(
            "entity_deleted", id, current->second.transform,
            current->second.name);
        entities_.erase(current);
    }
    return true;
}

bool RegionRuntime::update_transform(
    const std::uint64_t id, Transform transform) {
    if (!finite_vec(transform.position) || !finite_vec(transform.rotation) ||
        !finite_vec(transform.scale)) {
        return false;
    }

    std::scoped_lock lock(mutex_);
    const auto it = entities_.find(id);
    if (it == entities_.end()) return false;

    const auto previous = it->second.transform;
    it->second.transform = transform;
    if (it->second.physics_body != 0) {
        (void)physics_.set_body_position(
            it->second.physics_body, transform.position);
        (void)physics_.set_body_rotation(
            it->second.physics_body, transform.rotation);
        if (it->second.kind == EntityKind::object) {
            const auto body = physics_.body(it->second.physics_body);
            if (body.shape == physics::CollisionShape::box) {
                (void)physics_.set_body_shape(
                    it->second.physics_body,
                    physics::CollisionShape::box,
                    {std::max(0.05, transform.scale.x * 0.5),
                     std::max(0.05, transform.scale.y * 0.5),
                     std::max(0.05, transform.scale.z * 0.5)},
                    body.capsule_half_height);
            } else if (body.shape == physics::CollisionShape::sphere) {
                (void)physics_.set_body_radius(
                    it->second.physics_body,
                    std::max(
                        {0.05, transform.scale.x * 0.5,
                         transform.scale.y * 0.5,
                         transform.scale.z * 0.5}));
            } else {
                const auto radius =
                    std::max(0.05, std::max(
                        transform.scale.x,
                        transform.scale.y) * 0.5);
                (void)physics_.set_body_radius(
                    it->second.physics_body, radius);
                (void)physics_.set_body_shape(
                    it->second.physics_body,
                    physics::CollisionShape::capsule,
                    {radius, radius,
                     std::max(radius, transform.scale.z * 0.5)},
                    std::max(
                        0.0,
                        transform.scale.z * 0.5 - radius));
            }
        }
    }

    if (it->second.kind == EntityKind::object &&
        it->second.parent_entity_id == 0) {
        const bool position_changed =
            delta_squared(transform.position, previous.position) > 0.000001;
        const bool rotation_changed =
            delta_squared(transform.rotation, previous.rotation) > 0.000001;
        if (position_changed || rotation_changed) {
            const auto rotation_change =
                rotation_delta(transform.rotation, previous.rotation);
            for (auto& [child_id, child] : entities_) {
                if (child.parent_entity_id != id) continue;
                const auto previous_offset =
                    subtract(child.transform.position, previous.position);
                const auto local_offset =
                    inverse_rotate_euler(previous_offset, previous.rotation);
                child.transform.position =
                    add(transform.position,
                        rotate_euler(local_offset, transform.rotation));
                child.transform.rotation = {
                    normalized_degrees(
                        child.transform.rotation.x + rotation_change.x),
                    normalized_degrees(
                        child.transform.rotation.y + rotation_change.y),
                    normalized_degrees(
                        child.transform.rotation.z + rotation_change.z)};
                if (child.physics_body != 0) {
                    (void)physics_.set_body_position(
                        child.physics_body, child.transform.position);
                    (void)physics_.set_body_rotation(
                        child.physics_body, child.transform.rotation);
                }
                append_event_locked(
                    "entity_updated", child_id, child.transform);
            }
        }
    }

    append_event_locked("entity_updated", id, transform);
    return true;
}

bool RegionRuntime::set_velocity(
    const std::uint64_t id, const physics::Vec3 velocity) {
    if (!finite_vec(velocity)) return false;
    std::scoped_lock lock(mutex_);
    const auto it = entities_.find(id);
    if (it == entities_.end() || it->second.physics_body == 0) return false;
    return physics_.set_body_velocity(it->second.physics_body, velocity);
}

bool RegionRuntime::set_angular_velocity(
    const std::uint64_t id, const physics::Vec3 angular_velocity) {
    if (!finite_vec(angular_velocity)) return false;
    std::scoped_lock lock(mutex_);
    const auto it = entities_.find(id);
    if (it == entities_.end() || it->second.physics_body == 0) return false;
    return physics_.set_body_angular_velocity(
        it->second.physics_body, angular_velocity);
}

bool RegionRuntime::apply_force(
    const std::uint64_t id, const physics::Vec3 force) {
    if (!finite_vec(force)) return false;
    std::scoped_lock lock(mutex_);
    const auto it = entities_.find(id);
    if (it == entities_.end() || it->second.physics_body == 0) {
        return false;
    }
    const bool ok =
        physics_.apply_force(it->second.physics_body, force);
    if (ok) {
        append_event_locked(
            "physics_force", id, it->second.transform,
            std::to_string(force.x) + "," +
                std::to_string(force.y) + "," +
                std::to_string(force.z));
    }
    return ok;
}

bool RegionRuntime::apply_impulse(
    const std::uint64_t id, const physics::Vec3 impulse) {
    if (!finite_vec(impulse)) return false;
    std::scoped_lock lock(mutex_);
    const auto it = entities_.find(id);
    if (it == entities_.end() || it->second.physics_body == 0) {
        return false;
    }
    const bool ok =
        physics_.apply_impulse(it->second.physics_body, impulse);
    if (ok) {
        append_event_locked(
            "physics_impulse", id, it->second.transform,
            std::to_string(impulse.x) + "," +
                std::to_string(impulse.y) + "," +
                std::to_string(impulse.z));
    }
    return ok;
}

bool RegionRuntime::apply_angular_impulse(
    const std::uint64_t id, const physics::Vec3 impulse) {
    if (!finite_vec(impulse)) return false;
    std::scoped_lock lock(mutex_);
    const auto it = entities_.find(id);
    if (it == entities_.end() || it->second.physics_body == 0) {
        return false;
    }
    const bool ok =
        physics_.apply_angular_impulse(
            it->second.physics_body, impulse);
    if (ok) {
        append_event_locked(
            "physics_angular_impulse", id, it->second.transform,
            std::to_string(impulse.x) + "," +
                std::to_string(impulse.y) + "," +
                std::to_string(impulse.z));
    }
    return ok;
}

bool RegionRuntime::apply_torque(
    const std::uint64_t id, const physics::Vec3 torque) {
    if (!finite_vec(torque)) return false;
    std::scoped_lock lock(mutex_);
    const auto it = entities_.find(id);
    if (it == entities_.end() || it->second.physics_body == 0) {
        return false;
    }
    const bool ok =
        physics_.apply_torque(it->second.physics_body, torque);
    if (ok) {
        append_event_locked(
            "physics_torque", id, it->second.transform,
            std::to_string(torque.x) + "," +
                std::to_string(torque.y) + "," +
                std::to_string(torque.z));
    }
    return ok;
}

bool RegionRuntime::set_physics_material(
    const std::uint64_t id, const double mass,
    const double restitution, const double friction) {
    std::scoped_lock lock(mutex_);
    const auto it = entities_.find(id);
    if (it == entities_.end() || it->second.physics_body == 0) {
        return false;
    }
    const bool ok = physics_.set_body_material(
        it->second.physics_body, mass, restitution, friction);
    if (ok) {
        append_event_locked(
            "physics_material", id, it->second.transform);
    }
    return ok;
}

bool RegionRuntime::set_buoyancy(
    const std::uint64_t id, const double buoyancy) {
    std::scoped_lock lock(mutex_);
    const auto it = entities_.find(id);
    if (it == entities_.end() || it->second.physics_body == 0) {
        return false;
    }
    const bool ok =
        physics_.set_body_buoyancy(it->second.physics_body, buoyancy);
    if (ok) {
        append_event_locked(
            "physics_buoyancy", id, it->second.transform,
            std::to_string(buoyancy));
    }
    return ok;
}

bool RegionRuntime::set_physics_shape(
    const std::uint64_t id,
    const physics::CollisionShape shape) {
    std::scoped_lock lock(mutex_);
    const auto it = entities_.find(id);
    if (it == entities_.end() ||
        it->second.kind != EntityKind::object ||
        it->second.physics_body == 0) {
        return false;
    }

    const auto half_extents = physics::Vec3{
        std::max(0.05, it->second.transform.scale.x * 0.5),
        std::max(0.05, it->second.transform.scale.y * 0.5),
        std::max(0.05, it->second.transform.scale.z * 0.5)};
    auto radius = std::max(
        {0.05, half_extents.x, half_extents.y, half_extents.z});
    auto capsule_half_height = 0.0;
    if (shape == physics::CollisionShape::capsule) {
        radius = std::max(
            0.05,
            std::max(
                it->second.transform.scale.x,
                it->second.transform.scale.y) * 0.5);
        capsule_half_height = std::max(
            0.0,
            it->second.transform.scale.z * 0.5 - radius);
    }
    if (!physics_.set_body_radius(
            it->second.physics_body, radius) ||
        !physics_.set_body_shape(
            it->second.physics_body,
            shape,
            half_extents,
            capsule_half_height)) {
        return false;
    }
    append_event_locked(
        "physics_shape", id, it->second.transform,
        physics::collision_shape_name(shape));
    return true;
}

bool RegionRuntime::configure_character(
    const std::uint64_t id,
    const double max_slope_degrees,
    const double step_height,
    const double jump_speed) {
    std::scoped_lock lock(mutex_);
    const auto it = entities_.find(id);
    if (it == entities_.end() ||
        it->second.kind != EntityKind::avatar ||
        it->second.physics_body == 0) {
        return false;
    }
    const bool ok = physics_.set_character_controller(
        it->second.physics_body,
        max_slope_degrees,
        step_height,
        jump_speed);
    if (ok) {
        append_event_locked(
            "character_config", id, it->second.transform,
            std::to_string(max_slope_degrees) + "," +
                std::to_string(step_height) + "," +
                std::to_string(jump_speed));
    }
    return ok;
}

bool RegionRuntime::avatar_jump(const std::uint64_t id) {
    std::scoped_lock lock(mutex_);
    const auto it = entities_.find(id);
    if (it == entities_.end() ||
        it->second.kind != EntityKind::avatar ||
        it->second.physics_body == 0) {
        return false;
    }
    const bool ok =
        physics_.character_jump(it->second.physics_body);
    if (ok) {
        append_event_locked(
            "character_jump", id, it->second.transform);
    }
    return ok;
}

std::optional<RuntimeRaycastHit> RegionRuntime::raycast(
    const physics::Vec3 origin,
    const physics::Vec3 direction,
    const double max_distance,
    const std::uint64_t ignore_entity_id) const {
    std::scoped_lock lock(mutex_);

    std::uint64_t ignore_body = 0;
    if (ignore_entity_id != 0) {
        const auto ignored = entities_.find(ignore_entity_id);
        if (ignored != entities_.end()) {
            ignore_body = ignored->second.physics_body;
        }
    }

    const auto hit = physics_.raycast(
        origin, direction, max_distance, ignore_body);
    if (!hit) return std::nullopt;

    RuntimeRaycastHit result{
        .entity_id = 0,
        .point = hit->point,
        .normal = hit->normal,
        .distance = hit->distance,
        .ground = hit->ground};
    if (hit->body_id != 0) {
        const auto entity = std::find_if(
            entities_.begin(), entities_.end(),
            [&](const auto& item) {
                return item.second.physics_body == hit->body_id;
            });
        if (entity == entities_.end()) return std::nullopt;
        result.entity_id = entity->first;
    }
    return result;
}

std::uint64_t RegionRuntime::constrain_distance(
    const std::uint64_t entity_a,
    const std::uint64_t entity_b,
    const double rest_length,
    const double stiffness,
    std::string& reason) {
    std::scoped_lock lock(mutex_);
    const auto a = entities_.find(entity_a);
    const auto b = entities_.find(entity_b);
    if (a == entities_.end() || b == entities_.end() ||
        a->second.physics_body == 0 ||
        b->second.physics_body == 0) {
        reason = "constraint-physical-entity-required";
        return 0;
    }
    const auto constraint = physics_.add_distance_constraint(
        a->second.physics_body, b->second.physics_body,
        rest_length, stiffness);
    if (constraint == 0) {
        reason = "constraint-rejected";
        return 0;
    }
    append_event_locked(
        "physics_constraint", entity_a, a->second.transform,
        std::to_string(constraint) + "," +
            std::to_string(entity_b));
    reason.clear();
    return constraint;
}

std::uint64_t RegionRuntime::constrain_spring(
    const std::uint64_t entity_a,
    const std::uint64_t entity_b,
    const double rest_length,
    const double stiffness,
    const double damping,
    std::string& reason) {
    std::scoped_lock lock(mutex_);
    const auto a = entities_.find(entity_a);
    const auto b = entities_.find(entity_b);
    if (a == entities_.end() || b == entities_.end() ||
        a->second.physics_body == 0 ||
        b->second.physics_body == 0) {
        reason = "constraint-physical-entity-required";
        return 0;
    }
    const auto constraint = physics_.add_spring_constraint(
        a->second.physics_body,
        b->second.physics_body,
        rest_length,
        stiffness,
        damping);
    if (constraint == 0) {
        reason = "constraint-rejected";
        return 0;
    }
    append_event_locked(
        "physics_spring", entity_a, a->second.transform,
        std::to_string(constraint) + "," +
            std::to_string(entity_b));
    reason.clear();
    return constraint;
}

bool RegionRuntime::remove_constraint(
    const std::uint64_t constraint_id) {
    return physics_.remove_constraint(constraint_id);
}

bool RegionRuntime::set_physical(
    const std::uint64_t id, const bool enabled) {
    std::scoped_lock lock(mutex_);
    const auto it = entities_.find(id);
    if (it == entities_.end()) return false;
    auto& entity = it->second;

    if (enabled && entity.parent_entity_id != 0) return false;

    if (enabled && entity.physics_body == 0) {
        if (entity.kind == EntityKind::avatar) {
            entity.physics_body = physics_.add_body(
                {.position = entity.transform.position,
                 .rotation = entity.transform.rotation,
                 .radius = 0.45,
                 .half_extents = {0.45, 0.45, 0.9},
                 .capsule_half_height = 0.45,
                 .shape = physics::CollisionShape::capsule,
                 .character = true});
            (void)physics_.set_character_controller(
                entity.physics_body, 50.0, 0.45, 5.0);
        } else {
            const auto half_extents = physics::Vec3{
                std::max(0.05, entity.transform.scale.x * 0.5),
                std::max(0.05, entity.transform.scale.y * 0.5),
                std::max(0.05, entity.transform.scale.z * 0.5)};
            entity.physics_body = physics_.add_body(
                {.position = entity.transform.position,
                 .rotation = entity.transform.rotation,
                 .radius = std::max(
                     {0.1, half_extents.x,
                      half_extents.y, half_extents.z}),
                 .half_extents = half_extents,
                 .shape = physics::CollisionShape::box});
        }
    } else if (!enabled && entity.physics_body != 0) {
        (void)physics_.remove_body(entity.physics_body);
        entity.physics_body = 0;
    }
    append_event_locked(
        "physics_updated", id, entity.transform, enabled ? "1" : "0");
    return true;
}

bool RegionRuntime::set_floating_text(
    const std::uint64_t id, std::string text) {
    if (text.size() > 512U) return false;
    std::scoped_lock lock(mutex_);
    const auto it = entities_.find(id);
    if (it == entities_.end() || it->second.kind != EntityKind::object) {
        return false;
    }
    it->second.floating_text = std::move(text);
    append_event_locked(
        "object_text_updated", id, it->second.transform,
        it->second.floating_text);
    return true;
}

bool RegionRuntime::link_objects(
    const std::uint64_t root_id, const std::uint64_t child_id,
    std::string& reason) {
    if (root_id == 0 || child_id == 0 || root_id == child_id) {
        reason = "invalid-linkset-ids";
        return false;
    }

    std::scoped_lock lock(mutex_);
    const auto root = entities_.find(root_id);
    const auto child = entities_.find(child_id);
    if (root == entities_.end() || child == entities_.end() ||
        root->second.kind != EntityKind::object ||
        child->second.kind != EntityKind::object) {
        reason = "linkset-object-not-found";
        return false;
    }
    if (root->second.parent_entity_id != 0 ||
        child->second.parent_entity_id != 0) {
        reason = "linkset-member-already-linked";
        return false;
    }
    if (root->second.owner_user_id.empty() ||
        root->second.owner_user_id != child->second.owner_user_id) {
        reason = "linkset-owner-mismatch";
        return false;
    }
    for (const auto& [_, entity] : entities_) {
        if (entity.parent_entity_id == child_id) {
            reason = "linkset-nested-root-not-supported";
            return false;
        }
    }

    std::uint32_t next_link = 2;
    for (const auto& [_, entity] : entities_) {
        if (entity.parent_entity_id == root_id) {
            next_link = std::max(
                next_link,
                static_cast<std::uint32_t>(entity.link_number + 1U));
        }
    }

    child->second.parent_entity_id = root_id;
    child->second.link_number = next_link;
    if (child->second.physics_body != 0) {
        (void)physics_.remove_body(child->second.physics_body);
        child->second.physics_body = 0;
    }
    root->second.link_number = 1;
    append_event_locked(
        "entity_linked", child_id, child->second.transform,
        std::to_string(root_id));
    reason.clear();
    return true;
}

bool RegionRuntime::unlink_object(
    const std::uint64_t child_id, std::string& reason) {
    std::scoped_lock lock(mutex_);
    const auto child = entities_.find(child_id);
    if (child == entities_.end() ||
        child->second.kind != EntityKind::object) {
        reason = "linkset-object-not-found";
        return false;
    }
    if (child->second.parent_entity_id == 0) {
        reason.clear();
        return true;
    }
    const auto previous_parent = child->second.parent_entity_id;
    child->second.parent_entity_id = 0;
    child->second.link_number = 1;
    append_event_locked(
        "entity_unlinked", child_id, child->second.transform,
        std::to_string(previous_parent));
    reason.clear();
    return true;
}

bool RegionRuntime::set_object_permissions(
    const std::uint64_t id, std::string group_id,
    const core::PermissionMask group_permissions,
    const core::PermissionMask everyone_permissions) {
    std::scoped_lock lock(mutex_);
    const auto it = entities_.find(id);
    if (it == entities_.end() ||
        it->second.kind != EntityKind::object) {
        return false;
    }
    it->second.group_id = std::move(group_id);
    it->second.group_permissions = group_permissions & core::perm_all;
    it->second.everyone_permissions =
        everyone_permissions & core::perm_all;
    append_event_locked(
        "permissions_updated", id, it->second.transform);
    return true;
}

bool RegionRuntime::move_avatar(
    const std::uint64_t id, Transform transform,
    const physics::Vec3 velocity, std::string& boundary) {
    boundary.clear();
    const double max_x =
        static_cast<double>(terrain_.width()) * terrain_.cell_size();
    const double max_y =
        static_cast<double>(terrain_.height()) * terrain_.cell_size();
    if (transform.position.x < 0.0) boundary = "west";
    else if (transform.position.x >= max_x) boundary = "east";
    else if (transform.position.y < 0.0) boundary = "south";
    else if (transform.position.y >= max_y) boundary = "north";

    transform.position.x =
        std::clamp(transform.position.x, 0.25, max_x - 0.25);
    transform.position.y =
        std::clamp(transform.position.y, 0.25, max_y - 0.25);
    const auto ground =
        terrain_.sample(transform.position.x, transform.position.y);
    transform.position.z =
        std::max(transform.position.z, ground + 0.9);

    std::scoped_lock lock(mutex_);
    const auto it = entities_.find(id);
    if (it == entities_.end() ||
        it->second.kind != EntityKind::avatar ||
        it->second.physics_body == 0) {
        return false;
    }
    it->second.transform = transform;
    (void)physics_.set_body_position(
        it->second.physics_body, transform.position);
    (void)physics_.set_body_rotation(
        it->second.physics_body, transform.rotation);
    (void)physics_.set_body_velocity(
        it->second.physics_body, velocity);
    append_event_locked("avatar_move", id, transform);
    if (!boundary.empty()) {
        append_event_locked("region_boundary", id, transform, boundary);
    }
    return true;
}

bool RegionRuntime::set_terrain_height(
    const std::size_t x, const std::size_t y, const double value) {
    if (!terrain_.set_height(x, y, value)) return false;
    std::scoped_lock lock(mutex_);
    Transform transform;
    transform.position = {
        static_cast<double>(x) * terrain_.cell_size(),
        static_cast<double>(y) * terrain_.cell_size(),
        value};
    append_event_locked(
        "terrain_updated", 0, transform,
        std::to_string(terrain_.revision()));
    return true;
}

std::optional<ObjectTransferSnapshot> RegionRuntime::export_object(
    const std::uint64_t id) const {
    std::scoped_lock lock(mutex_);
    const auto it = entities_.find(id);
    if (it == entities_.end() ||
        it->second.kind != EntityKind::object) {
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
        .angular_velocity = {},
        .mass = 1.0,
        .restitution = 0.15,
        .friction = 0.6,
        .linear_damping = 0.04,
        .angular_damping = 0.04,
        .gravity_scale = 1.0,
        .buoyancy = 0.0,
        .collision_shape = physics::CollisionShape::sphere,
        .half_extents = {0.5, 0.5, 0.5},
        .capsule_half_height = 0.5,
        .physical = it->second.physics_body != 0,
        .parent_source_entity_id = it->second.parent_entity_id,
        .link_number = it->second.link_number,
        .floating_text = it->second.floating_text};

    if (it->second.physics_body != 0) {
        const auto body = physics_.body(it->second.physics_body);
        snapshot.velocity = body.velocity;
        snapshot.angular_velocity = body.angular_velocity;
        snapshot.mass = body.mass;
        snapshot.restitution = body.restitution;
        snapshot.friction = body.friction;
        snapshot.linear_damping = body.linear_damping;
        snapshot.angular_damping = body.angular_damping;
        snapshot.gravity_scale = body.gravity_scale;
        snapshot.buoyancy = body.buoyancy;
        snapshot.collision_shape = body.shape;
        snapshot.half_extents = body.half_extents;
        snapshot.capsule_half_height = body.capsule_half_height;
    }
    return snapshot;
}

std::optional<ObjectLinksetTransferSnapshot>
RegionRuntime::export_linkset(const std::uint64_t entity_id) const {
    std::scoped_lock lock(mutex_);
    const auto selected = entities_.find(entity_id);
    if (selected == entities_.end() ||
        selected->second.kind != EntityKind::object) {
        return std::nullopt;
    }

    const auto root_id =
        selected->second.parent_entity_id == 0
            ? selected->second.id
            : selected->second.parent_entity_id;
    const auto root = entities_.find(root_id);
    if (root == entities_.end() ||
        root->second.kind != EntityKind::object) {
        return std::nullopt;
    }

    std::vector<const Entity*> members;
    for (const auto& [id, entity] : entities_) {
        if (id == root_id || entity.parent_entity_id == root_id) {
            members.push_back(&entity);
        }
    }
    if (members.empty() || members.size() > kMaxLinksetMembers) {
        return std::nullopt;
    }
    std::sort(
        members.begin(), members.end(),
        [](const Entity* left, const Entity* right) {
            if (left->id == left->parent_entity_id) return true;
            if (left->link_number != right->link_number) {
                return left->link_number < right->link_number;
            }
            return left->id < right->id;
        });

    ObjectLinksetTransferSnapshot result;
    result.source_root_entity_id = root_id;
    result.members.reserve(members.size());
    for (const auto* entity : members) {
        if (entity->owner_user_id != root->second.owner_user_id) {
            return std::nullopt;
        }
        ObjectTransferSnapshot snapshot{
            .source_entity_id = entity->id,
            .name = entity->name,
            .owner_user_id = entity->owner_user_id,
            .group_id = entity->group_id,
            .owner_permissions = entity->owner_permissions,
            .group_permissions = entity->group_permissions,
            .everyone_permissions = entity->everyone_permissions,
            .transform = entity->transform,
            .velocity = {},
            .angular_velocity = {},
            .mass = 1.0,
            .restitution = 0.15,
            .friction = 0.6,
            .linear_damping = 0.04,
            .angular_damping = 0.04,
            .gravity_scale = 1.0,
            .buoyancy = 0.0,
            .collision_shape = physics::CollisionShape::sphere,
            .half_extents = {0.5, 0.5, 0.5},
            .capsule_half_height = 0.5,
            .physical = entity->physics_body != 0,
            .parent_source_entity_id = entity->parent_entity_id,
            .link_number = entity->link_number,
            .floating_text = entity->floating_text};
        if (entity->physics_body != 0) {
            const auto body = physics_.body(entity->physics_body);
            snapshot.velocity = body.velocity;
            snapshot.angular_velocity = body.angular_velocity;
            snapshot.mass = body.mass;
            snapshot.restitution = body.restitution;
            snapshot.friction = body.friction;
            snapshot.linear_damping = body.linear_damping;
            snapshot.angular_damping = body.angular_damping;
            snapshot.gravity_scale = body.gravity_scale;
            snapshot.buoyancy = body.buoyancy;
            snapshot.collision_shape = body.shape;
            snapshot.half_extents = body.half_extents;
            snapshot.capsule_half_height = body.capsule_half_height;
        }
        result.members.push_back(std::move(snapshot));
    }
    return result;
}

bool RegionRuntime::import_object(
    const ObjectTransferSnapshot& snapshot,
    const std::uint64_t destination_entity_id,
    physics::Vec3 destination_position,
    std::string& reason) {
    if (destination_entity_id == 0 || snapshot.source_entity_id == 0 ||
        snapshot.name.size() > 256U ||
        snapshot.owner_user_id.empty() ||
        snapshot.owner_user_id.size() > 256U ||
        snapshot.group_id.size() > 256U ||
        snapshot.floating_text.size() > 512U ||
        !finite_vec(destination_position) ||
        !finite_vec(snapshot.velocity) ||
        !finite_vec(snapshot.angular_velocity) ||
        !std::isfinite(snapshot.mass) ||
        !std::isfinite(snapshot.restitution) ||
        !std::isfinite(snapshot.friction) ||
        !std::isfinite(snapshot.linear_damping) ||
        !std::isfinite(snapshot.angular_damping) ||
        !std::isfinite(snapshot.gravity_scale) ||
        !std::isfinite(snapshot.buoyancy) ||
        !finite_vec(snapshot.half_extents) ||
        snapshot.half_extents.x <= 0.0 ||
        snapshot.half_extents.y <= 0.0 ||
        snapshot.half_extents.z <= 0.0 ||
        !std::isfinite(snapshot.capsule_half_height) ||
        snapshot.capsule_half_height < 0.0) {
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
    const auto floor =
        terrain_.sample(destination_position.x, destination_position.y);
    destination_position.z =
        std::max(
            destination_position.z,
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
    if (!restore_object(
            destination_entity_id, snapshot.name, transform,
            snapshot.physical, snapshot.owner_user_id, snapshot.group_id,
            snapshot.owner_permissions, snapshot.group_permissions,
            snapshot.everyone_permissions, 0, 1,
            snapshot.floating_text, snapshot.velocity,
            snapshot.angular_velocity,
            snapshot.mass, snapshot.restitution, snapshot.friction,
            snapshot.linear_damping, snapshot.angular_damping,
            snapshot.gravity_scale, snapshot.buoyancy,
            snapshot.collision_shape, snapshot.half_extents,
            snapshot.capsule_half_height)) {
        reason = "destination-object-restore-failed";
        return false;
    }
    reason.clear();
    return true;
}

bool RegionRuntime::import_linkset(
    const ObjectLinksetTransferSnapshot& snapshot,
    const std::uint64_t destination_root_entity_id,
    physics::Vec3 destination_position,
    std::vector<std::pair<std::uint64_t, std::uint64_t>>& entity_map,
    std::string& reason,
    const bool preserve_source_ids) {
    entity_map.clear();
    if (destination_root_entity_id == 0 ||
        snapshot.source_root_entity_id == 0 ||
        snapshot.members.empty() ||
        snapshot.members.size() > kMaxLinksetMembers ||
        !finite_vec(destination_position)) {
        reason = "invalid-linkset-transfer-snapshot";
        return false;
    }

    const auto root_it = std::find_if(
        snapshot.members.begin(), snapshot.members.end(),
        [&](const ObjectTransferSnapshot& member) {
            return member.source_entity_id ==
                   snapshot.source_root_entity_id;
        });
    if (root_it == snapshot.members.end() ||
        root_it->parent_source_entity_id != 0) {
        reason = "linkset-root-missing";
        return false;
    }

    const auto& root_snapshot = *root_it;
    if (root_snapshot.owner_user_id.empty()) {
        reason = "linkset-owner-missing";
        return false;
    }

    for (const auto& member : snapshot.members) {
        if (member.source_entity_id == 0 ||
            member.owner_user_id != root_snapshot.owner_user_id ||
            member.name.size() > 256U ||
            member.group_id.size() > 256U ||
            member.floating_text.size() > 512U ||
            !finite_vec(member.transform.position) ||
            !finite_vec(member.transform.rotation) ||
            !finite_vec(member.transform.scale) ||
            !finite_vec(member.velocity) ||
            !finite_vec(member.angular_velocity) ||
            !std::isfinite(member.mass) ||
            !std::isfinite(member.restitution) ||
            !std::isfinite(member.friction) ||
            !std::isfinite(member.linear_damping) ||
            !std::isfinite(member.angular_damping) ||
            !std::isfinite(member.gravity_scale) ||
            !std::isfinite(member.buoyancy) ||
            !finite_vec(member.half_extents) ||
            member.half_extents.x <= 0.0 ||
            member.half_extents.y <= 0.0 ||
            member.half_extents.z <= 0.0 ||
            !std::isfinite(member.capsule_half_height) ||
            member.capsule_half_height < 0.0) {
            reason = "invalid-linkset-member";
            return false;
        }
        if (member.source_entity_id != snapshot.source_root_entity_id &&
            member.parent_source_entity_id !=
                snapshot.source_root_entity_id) {
            reason = "nested-linkset-not-supported";
            return false;
        }
    }

    const double max_x =
        static_cast<double>(terrain_.width()) * terrain_.cell_size();
    const double max_y =
        static_cast<double>(terrain_.height()) * terrain_.cell_size();

    if (destination_position.z <= 0.0) {
        destination_position.z = root_snapshot.transform.position.z;
    }

    double min_dx = 0.0;
    double max_dx = 0.0;
    double min_dy = 0.0;
    double max_dy = 0.0;
    for (const auto& member : snapshot.members) {
        const auto dx =
            member.transform.position.x -
            root_snapshot.transform.position.x;
        const auto dy =
            member.transform.position.y -
            root_snapshot.transform.position.y;
        min_dx = std::min(min_dx, dx);
        max_dx = std::max(max_dx, dx);
        min_dy = std::min(min_dy, dy);
        max_dy = std::max(max_dy, dy);
    }

    const double low_x = 0.25 - min_dx;
    const double high_x = max_x - 0.25 - max_dx;
    const double low_y = 0.25 - min_dy;
    const double high_y = max_y - 0.25 - max_dy;
    if (low_x > high_x || low_y > high_y) {
        reason = "linkset-too-large-for-region";
        return false;
    }
    destination_position.x =
        std::clamp(destination_position.x, low_x, high_x);
    destination_position.y =
        std::clamp(destination_position.y, low_y, high_y);

    const auto floor =
        terrain_.sample(destination_position.x, destination_position.y);
    destination_position.z =
        std::max(
            destination_position.z,
            floor +
                std::max(0.1, root_snapshot.transform.scale.z * 0.5));

    const physics::Vec3 delta{
        destination_position.x - root_snapshot.transform.position.x,
        destination_position.y - root_snapshot.transform.position.y,
        destination_position.z - root_snapshot.transform.position.z};

    entity_map.reserve(snapshot.members.size());
    for (const auto& member : snapshot.members) {
        const auto destination_id =
            preserve_source_ids
                ? member.source_entity_id
                : (member.source_entity_id == snapshot.source_root_entity_id
                       ? destination_root_entity_id
                       : transfer_member_id(
                             destination_root_entity_id,
                             member.source_entity_id,
                             member.link_number));
        if (std::find_if(
                entity_map.begin(), entity_map.end(),
                [&](const auto& pair) {
                    return pair.second == destination_id;
                }) != entity_map.end()) {
            reason = "linkset-destination-id-collision";
            entity_map.clear();
            return false;
        }
        entity_map.emplace_back(
            member.source_entity_id, destination_id);
    }

    for (const auto& member : snapshot.members) {
        const auto map_it = std::find_if(
            entity_map.begin(), entity_map.end(),
            [&](const auto& pair) {
                return pair.first == member.source_entity_id;
            });
        const auto destination_id = map_it->second;
        const auto existing = entity(destination_id);
        if (!existing) continue;

        const auto expected_parent =
            member.source_entity_id == snapshot.source_root_entity_id
                ? 0ULL
                : destination_root_entity_id;
        if (existing->kind != EntityKind::object ||
            existing->owner_user_id != member.owner_user_id ||
            existing->name != member.name ||
            existing->group_id != member.group_id ||
            existing->parent_entity_id != expected_parent ||
            existing->link_number !=
                (expected_parent == 0 ? 1U : member.link_number)) {
            reason = "linkset-destination-entity-collision";
            entity_map.clear();
            return false;
        }
    }

    std::vector<std::uint64_t> created;
    std::vector<ObjectTransferSnapshot> ordered = snapshot.members;
    std::sort(
        ordered.begin(), ordered.end(),
        [&](const auto& left, const auto& right) {
            if (left.source_entity_id == snapshot.source_root_entity_id) {
                return true;
            }
            if (right.source_entity_id == snapshot.source_root_entity_id) {
                return false;
            }
            if (left.link_number != right.link_number) {
                return left.link_number < right.link_number;
            }
            return left.source_entity_id < right.source_entity_id;
        });

    for (const auto& member : ordered) {
        const auto map_it = std::find_if(
            entity_map.begin(), entity_map.end(),
            [&](const auto& pair) {
                return pair.first == member.source_entity_id;
            });
        const auto destination_id = map_it->second;
        if (entity(destination_id)) continue;

        auto transform = member.transform;
        transform.position += delta;
        const auto destination_parent =
            member.source_entity_id == snapshot.source_root_entity_id
                ? 0ULL
                : destination_root_entity_id;

        if (!restore_object(
                destination_id, member.name, transform,
                member.physical && destination_parent == 0,
                member.owner_user_id, member.group_id,
                member.owner_permissions, member.group_permissions,
                member.everyone_permissions, destination_parent,
                destination_parent == 0 ? 1U : member.link_number,
                member.floating_text, member.velocity,
                member.angular_velocity,
                member.mass, member.restitution, member.friction,
                member.linear_damping, member.angular_damping,
                member.gravity_scale, member.buoyancy,
                member.collision_shape, member.half_extents,
                member.capsule_half_height)) {
            for (auto it = created.rbegin(); it != created.rend(); ++it) {
                (void)remove_entity(*it);
            }
            entity_map.clear();
            reason = "linkset-destination-restore-failed";
            return false;
        }
        created.push_back(destination_id);
    }

    reason.clear();
    return true;
}

std::optional<Entity> RegionRuntime::entity(
    const std::uint64_t id) const {
    std::scoped_lock lock(mutex_);
    const auto it = entities_.find(id);
    if (it == entities_.end()) return std::nullopt;
    return it->second;
}

std::optional<physics::Body> RegionRuntime::physics_body_state(
    const std::uint64_t id) const {
    std::scoped_lock lock(mutex_);
    const auto it = entities_.find(id);
    if (it == entities_.end() || it->second.physics_body == 0) {
        return std::nullopt;
    }
    return physics_.body(it->second.physics_body);
}

std::vector<Entity> RegionRuntime::linkset_members(
    const std::uint64_t entity_id) const {
    std::scoped_lock lock(mutex_);
    const auto selected = entities_.find(entity_id);
    if (selected == entities_.end() ||
        selected->second.kind != EntityKind::object) {
        return {};
    }
    const auto root_id =
        selected->second.parent_entity_id == 0
            ? selected->second.id
            : selected->second.parent_entity_id;

    std::vector<Entity> result;
    for (const auto& [id, entity] : entities_) {
        if (id == root_id || entity.parent_entity_id == root_id) {
            result.push_back(entity);
        }
    }
    std::sort(
        result.begin(), result.end(),
        [root_id](const Entity& left, const Entity& right) {
            if (left.id == root_id) return right.id != root_id;
            if (right.id == root_id) return false;
            if (left.link_number != right.link_number) {
                return left.link_number < right.link_number;
            }
            return left.id < right.id;
        });
    return result;
}

std::vector<Entity> RegionRuntime::snapshot_entities() const {
    std::scoped_lock lock(mutex_);
    std::vector<Entity> result;
    result.reserve(entities_.size());
    for (const auto& [_, entity] : entities_) {
        result.push_back(entity);
    }
    std::sort(
        result.begin(), result.end(),
        [](const Entity& left, const Entity& right) {
            return left.id < right.id;
        });
    return result;
}

std::vector<SceneEvent> RegionRuntime::events_since(
    const std::uint64_t sequence,
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

std::uint64_t RegionRuntime::chat(
    const std::uint64_t sender_entity, std::string text,
    std::string event_type) {
    if (text.size() > 512U) text.resize(512U);
    if (event_type != "chat" &&
        event_type != "chat_whisper" &&
        event_type != "chat_shout") {
        event_type = "chat";
    }
    std::scoped_lock lock(mutex_);
    if (sender_entity != 0 &&
        !entities_.contains(sender_entity)) {
        return 0;
    }
    return append_event_locked(
        std::move(event_type), sender_entity, {}, std::move(text));
}

RuntimeMetrics RegionRuntime::metrics() const {
    std::scoped_lock lock(mutex_);
    std::uint64_t avatars = 0;
    for (const auto& [_, entity] : entities_) {
        if (entity.kind == EntityKind::avatar) ++avatars;
    }
    return {
        .ticks = ticks_.load(),
        .entities = entities_.size(),
        .avatars = avatars,
        .physics_bodies = physics_.body_count(),
        .scene_events = next_event_ - 1,
        .terrain_revision = terrain_.revision(),
        .collision_contacts = collision_contacts_.load(),
        .physics_constraints =
            physics_.constraints().size() +
            physics_.spring_constraints().size(),
        .sim_fps = sim_fps_.load()};
}

std::uint64_t RegionRuntime::latest_sequence() const {
    std::scoped_lock lock(mutex_);
    return next_event_ - 1;
}

std::uint64_t RegionRuntime::append_event_locked(
    std::string type, const std::uint64_t entity_id,
    const Transform& transform, std::string text) {
    const auto sequence = next_event_++;
    events_.push_back({
        .sequence = sequence,
        .type = std::move(type),
        .entity_id = entity_id,
        .transform = transform,
        .text = std::move(text)});
    while (events_.size() > kMaxSceneEvents) {
        events_.pop_front();
    }
    return sequence;
}

void RegionRuntime::loop() {
    using clock = std::chrono::steady_clock;
    const auto step =
        std::chrono::duration<double>(1.0 / target_hz_);
    auto next = clock::now();
    auto fps_start = next;
    std::uint64_t fps_ticks = 0;

    while (running_) {
        next += std::chrono::duration_cast<clock::duration>(step);
        physics_.step(step.count());
        const auto tick = ticks_.fetch_add(1) + 1;
        ++fps_ticks;

        const auto contacts = physics_.collisions();
        collision_contacts_.store(contacts.size());
        {
            std::scoped_lock lock(mutex_);
            std::unordered_map<std::uint64_t, std::uint64_t> body_entities;
            body_entities.reserve(entities_.size());
            for (const auto& [entity_id, entity] : entities_) {
                if (entity.physics_body != 0) {
                    body_entities.emplace(
                        entity.physics_body, entity_id);
                }
            }

            std::set<std::pair<std::uint64_t, std::uint64_t>>
                current_collisions;
            std::set<std::uint64_t> current_ground_contacts;

            for (const auto& contact : contacts) {
                const auto first_body =
                    body_entities.find(contact.body_a);
                if (first_body == body_entities.end()) continue;
                const auto first_entity = first_body->second;

                if (contact.ground || contact.body_b == 0) {
                    current_ground_contacts.insert(first_entity);
                    const auto first = !active_ground_contacts_.contains(
                        first_entity);
                    if (first || tick % kMovementEventStride == 0) {
                        const auto entity_it =
                            entities_.find(first_entity);
                        if (entity_it != entities_.end()) {
                            append_event_locked(
                                first ? "land_collision_start"
                                      : "land_collision",
                                first_entity,
                                entity_it->second.transform,
                                "impulse=" +
                                    std::to_string(contact.impulse));
                        }
                    }
                    continue;
                }

                const auto second_body =
                    body_entities.find(contact.body_b);
                if (second_body == body_entities.end()) continue;
                const auto second_entity = second_body->second;
                const std::pair<std::uint64_t, std::uint64_t> pair{
                    std::min(first_entity, second_entity),
                    std::max(first_entity, second_entity)};
                current_collisions.insert(pair);
                const auto first =
                    !active_collisions_.contains(pair);
                if (first || tick % kMovementEventStride == 0) {
                    const auto left = entities_.find(first_entity);
                    const auto right = entities_.find(second_entity);
                    if (left != entities_.end()) {
                        append_event_locked(
                            first ? "collision_start" : "collision",
                            first_entity, left->second.transform,
                            std::to_string(second_entity) +
                                ",impulse=" +
                                std::to_string(contact.impulse));
                    }
                    if (right != entities_.end()) {
                        append_event_locked(
                            first ? "collision_start" : "collision",
                            second_entity, right->second.transform,
                            std::to_string(first_entity) +
                                ",impulse=" +
                                std::to_string(contact.impulse));
                    }
                }
            }

            for (const auto entity_id : active_ground_contacts_) {
                if (current_ground_contacts.contains(entity_id)) continue;
                const auto entity_it = entities_.find(entity_id);
                if (entity_it != entities_.end()) {
                    append_event_locked(
                        "land_collision_end", entity_id,
                        entity_it->second.transform);
                }
            }
            for (const auto& pair : active_collisions_) {
                if (current_collisions.contains(pair)) continue;
                const auto left = entities_.find(pair.first);
                const auto right = entities_.find(pair.second);
                if (left != entities_.end()) {
                    append_event_locked(
                        "collision_end", pair.first,
                        left->second.transform,
                        std::to_string(pair.second));
                }
                if (right != entities_.end()) {
                    append_event_locked(
                        "collision_end", pair.second,
                        right->second.transform,
                        std::to_string(pair.first));
                }
            }
            active_collisions_ = std::move(current_collisions);
            active_ground_contacts_ =
                std::move(current_ground_contacts);
        }

        if (tick % kMovementEventStride == 0) {
            std::scoped_lock lock(mutex_);
            for (auto& [id, entity] : entities_) {
                if (entity.physics_body == 0) continue;
                const auto body =
                    physics_.body(entity.physics_body);
                const bool position_changed =
                    delta_squared(
                        body.position,
                        entity.transform.position) > 0.000001;
                const bool rotation_changed =
                    delta_squared(
                        body.rotation,
                        entity.transform.rotation) > 0.000001;
                if (!position_changed && !rotation_changed) continue;

                const auto previous_transform =
                    entity.transform;
                entity.transform.position = body.position;
                entity.transform.rotation = body.rotation;

                if (entity.kind == EntityKind::object &&
                    entity.parent_entity_id == 0 &&
                    (position_changed || rotation_changed)) {
                    const auto rotation_change =
                        rotation_delta(
                            entity.transform.rotation,
                            previous_transform.rotation);
                    for (auto& [child_id, child] : entities_) {
                        if (child.parent_entity_id != id) continue;
                        const auto previous_offset =
                            subtract(
                                child.transform.position,
                                previous_transform.position);
                        const auto local_offset =
                            inverse_rotate_euler(
                                previous_offset,
                                previous_transform.rotation);
                        child.transform.position =
                            add(
                                entity.transform.position,
                                rotate_euler(
                                    local_offset,
                                    entity.transform.rotation));
                        child.transform.rotation = {
                            normalized_degrees(
                                child.transform.rotation.x +
                                rotation_change.x),
                            normalized_degrees(
                                child.transform.rotation.y +
                                rotation_change.y),
                            normalized_degrees(
                                child.transform.rotation.z +
                                rotation_change.z)};
                        append_event_locked(
                            "entity_updated",
                            child_id,
                            child.transform);
                    }
                }
                append_event_locked(
                    "entity_updated", id, entity.transform);
            }
        }

        const auto now = clock::now();
        if (now - fps_start >= std::chrono::seconds(1)) {
            const auto seconds =
                std::chrono::duration<double>(
                    now - fps_start)
                    .count();
            sim_fps_.store(
                static_cast<double>(fps_ticks) / seconds);
            fps_ticks = 0;
            fps_start = now;
        }
        std::this_thread::sleep_until(next);
    }
}

} // namespace opengenesis::world
