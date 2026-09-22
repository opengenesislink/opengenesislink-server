#include "opengenesis/physics/physics_world.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

namespace opengenesis::physics {
namespace {

constexpr double kEpsilon = 1e-9;
constexpr double kPi = 3.14159265358979323846;

double normalized_degrees(double value) {
    if (!std::isfinite(value)) return 0.0;
    value = std::fmod(value, 360.0);
    if (value < 0.0) value += 360.0;
    return value;
}

bool finite_vec(const Vec3& value) {
    return std::isfinite(value.x) &&
           std::isfinite(value.y) &&
           std::isfinite(value.z);
}

Vec3 add(const Vec3& left, const Vec3& right) {
    return {left.x + right.x, left.y + right.y, left.z + right.z};
}

Vec3 subtract(const Vec3& left, const Vec3& right) {
    return {left.x - right.x, left.y - right.y, left.z - right.z};
}

Vec3 scale(const Vec3& value, const double factor) {
    return {value.x * factor, value.y * factor, value.z * factor};
}

double dot(const Vec3& left, const Vec3& right) {
    return left.x * right.x + left.y * right.y + left.z * right.z;
}

double length_squared(const Vec3& value) {
    return dot(value, value);
}

double length(const Vec3& value) {
    return std::sqrt(length_squared(value));
}

Vec3 normalized(const Vec3& value) {
    const auto magnitude = length(value);
    if (magnitude <= kEpsilon) return {};
    return scale(value, 1.0 / magnitude);
}

void add_scaled(Vec3& target, const Vec3& value, const double factor) {
    target.x += value.x * factor;
    target.y += value.y * factor;
    target.z += value.z * factor;
}

double inverse_mass(const Body& body) {
    return body.dynamic ? 1.0 / std::max(0.001, body.mass) : 0.0;
}

Vec3 shape_extents(const Body& body) {
    switch (body.shape) {
        case CollisionShape::sphere:
            return {body.radius, body.radius, body.radius};
        case CollisionShape::box:
            return body.half_extents;
        case CollisionShape::capsule:
            return {
                body.radius,
                body.radius,
                body.radius + body.capsule_half_height};
    }
    return {body.radius, body.radius, body.radius};
}

double support_height(const Body& body) {
    return shape_extents(body).z;
}

Vec3 closest_aabb_point(
    const Vec3& point,
    const Vec3& center,
    const Vec3& extents) {
    return {
        std::clamp(point.x, center.x - extents.x, center.x + extents.x),
        std::clamp(point.y, center.y - extents.y, center.y + extents.y),
        std::clamp(point.z, center.z - extents.z, center.z + extents.z)};
}

struct ContactGeometry {
    Vec3 normal{};
    Vec3 point{};
    double penetration{0.0};
};

std::optional<ContactGeometry> sphere_sphere(
    const Body& a, const Body& b) {
    const auto delta = subtract(b.position, a.position);
    const auto radius = a.radius + b.radius;
    const auto distance_sq = length_squared(delta);
    if (distance_sq >= radius * radius) return std::nullopt;
    const auto distance = std::sqrt(std::max(0.0, distance_sq));
    const auto normal =
        distance > kEpsilon
            ? scale(delta, 1.0 / distance)
            : Vec3{1.0, 0.0, 0.0};
    return ContactGeometry{
        .normal = normal,
        .point = add(a.position, scale(normal, a.radius)),
        .penetration = radius - distance};
}

std::optional<ContactGeometry> box_box(
    const Body& a, const Body& b) {
    const auto ea = a.half_extents;
    const auto eb = b.half_extents;
    const auto dx = b.position.x - a.position.x;
    const auto dy = b.position.y - a.position.y;
    const auto dz = b.position.z - a.position.z;
    const auto ox = ea.x + eb.x - std::abs(dx);
    const auto oy = ea.y + eb.y - std::abs(dy);
    const auto oz = ea.z + eb.z - std::abs(dz);
    if (ox <= 0.0 || oy <= 0.0 || oz <= 0.0) {
        return std::nullopt;
    }

    Vec3 normal{};
    double penetration = ox;
    normal.x = dx >= 0.0 ? 1.0 : -1.0;
    if (oy < penetration) {
        penetration = oy;
        normal = {0.0, dy >= 0.0 ? 1.0 : -1.0, 0.0};
    }
    if (oz < penetration) {
        penetration = oz;
        normal = {0.0, 0.0, dz >= 0.0 ? 1.0 : -1.0};
    }

    return ContactGeometry{
        .normal = normal,
        .point = {
            (a.position.x + b.position.x) * 0.5,
            (a.position.y + b.position.y) * 0.5,
            (a.position.z + b.position.z) * 0.5},
        .penetration = penetration};
}

std::optional<ContactGeometry> sphere_box_geometry(
    const Vec3& sphere_center,
    const double sphere_radius,
    const Body& box) {
    const auto closest =
        closest_aabb_point(sphere_center, box.position, box.half_extents);
    auto delta = subtract(closest, sphere_center);
    const auto distance_sq = length_squared(delta);
    if (distance_sq > sphere_radius * sphere_radius) {
        return std::nullopt;
    }

    if (distance_sq > kEpsilon) {
        const auto distance = std::sqrt(distance_sq);
        return ContactGeometry{
            .normal = scale(delta, 1.0 / distance),
            .point = closest,
            .penetration = sphere_radius - distance};
    }

    const auto local = subtract(sphere_center, box.position);
    const auto face_x = box.half_extents.x - std::abs(local.x);
    const auto face_y = box.half_extents.y - std::abs(local.y);
    const auto face_z = box.half_extents.z - std::abs(local.z);
    Vec3 normal{local.x >= 0.0 ? 1.0 : -1.0, 0.0, 0.0};
    double face = face_x;
    if (face_y < face) {
        face = face_y;
        normal = {0.0, local.y >= 0.0 ? 1.0 : -1.0, 0.0};
    }
    if (face_z < face) {
        face = face_z;
        normal = {0.0, 0.0, local.z >= 0.0 ? 1.0 : -1.0};
    }
    return ContactGeometry{
        .normal = normal,
        .point = add(sphere_center, scale(normal, face)),
        .penetration = sphere_radius + std::max(0.0, face)};
}

std::optional<ContactGeometry> sphere_box(
    const Body& sphere, const Body& box) {
    return sphere_box_geometry(sphere.position, sphere.radius, box);
}

Vec3 capsule_segment_point(
    const Body& capsule, const double z) {
    return {
        capsule.position.x,
        capsule.position.y,
        std::clamp(
            z,
            capsule.position.z - capsule.capsule_half_height,
            capsule.position.z + capsule.capsule_half_height)};
}

std::optional<ContactGeometry> capsule_sphere(
    const Body& capsule, const Body& sphere) {
    const auto point =
        capsule_segment_point(capsule, sphere.position.z);
    const auto delta = subtract(sphere.position, point);
    const auto radius = capsule.radius + sphere.radius;
    const auto distance_sq = length_squared(delta);
    if (distance_sq >= radius * radius) return std::nullopt;
    const auto distance = std::sqrt(std::max(0.0, distance_sq));
    const auto normal =
        distance > kEpsilon
            ? scale(delta, 1.0 / distance)
            : Vec3{1.0, 0.0, 0.0};
    return ContactGeometry{
        .normal = normal,
        .point = add(point, scale(normal, capsule.radius)),
        .penetration = radius - distance};
}

std::optional<ContactGeometry> capsule_capsule(
    const Body& a, const Body& b) {
    auto pa = capsule_segment_point(a, b.position.z);
    auto pb = capsule_segment_point(b, pa.z);
    pa = capsule_segment_point(a, pb.z);
    const auto delta = subtract(pb, pa);
    const auto radius = a.radius + b.radius;
    const auto distance_sq = length_squared(delta);
    if (distance_sq >= radius * radius) return std::nullopt;
    const auto distance = std::sqrt(std::max(0.0, distance_sq));
    const auto normal =
        distance > kEpsilon
            ? scale(delta, 1.0 / distance)
            : Vec3{1.0, 0.0, 0.0};
    return ContactGeometry{
        .normal = normal,
        .point = add(pa, scale(normal, a.radius)),
        .penetration = radius - distance};
}

std::optional<ContactGeometry> capsule_box(
    const Body& capsule, const Body& box) {
    const auto box_low = box.position.z - box.half_extents.z;
    const auto box_high = box.position.z + box.half_extents.z;
    const auto cap_low =
        capsule.position.z - capsule.capsule_half_height;
    const auto cap_high =
        capsule.position.z + capsule.capsule_half_height;

    double z = capsule.position.z;
    if (cap_high < box_low) z = cap_high;
    else if (cap_low > box_high) z = cap_low;
    else z = std::clamp(box.position.z, cap_low, cap_high);

    const Vec3 sphere_center{
        capsule.position.x, capsule.position.y, z};
    return sphere_box_geometry(
        sphere_center, capsule.radius, box);
}

std::optional<ContactGeometry> collision_geometry(
    const Body& a, const Body& b) {
    if (a.shape == CollisionShape::sphere &&
        b.shape == CollisionShape::sphere) {
        return sphere_sphere(a, b);
    }
    if (a.shape == CollisionShape::box &&
        b.shape == CollisionShape::box) {
        return box_box(a, b);
    }
    if (a.shape == CollisionShape::sphere &&
        b.shape == CollisionShape::box) {
        return sphere_box(a, b);
    }
    if (a.shape == CollisionShape::box &&
        b.shape == CollisionShape::sphere) {
        auto contact = sphere_box(b, a);
        if (contact) contact->normal = scale(contact->normal, -1.0);
        return contact;
    }
    if (a.shape == CollisionShape::capsule &&
        b.shape == CollisionShape::sphere) {
        return capsule_sphere(a, b);
    }
    if (a.shape == CollisionShape::sphere &&
        b.shape == CollisionShape::capsule) {
        auto contact = capsule_sphere(b, a);
        if (contact) contact->normal = scale(contact->normal, -1.0);
        return contact;
    }
    if (a.shape == CollisionShape::capsule &&
        b.shape == CollisionShape::capsule) {
        return capsule_capsule(a, b);
    }
    if (a.shape == CollisionShape::capsule &&
        b.shape == CollisionShape::box) {
        return capsule_box(a, b);
    }
    if (a.shape == CollisionShape::box &&
        b.shape == CollisionShape::capsule) {
        auto contact = capsule_box(b, a);
        if (contact) contact->normal = scale(contact->normal, -1.0);
        return contact;
    }
    return std::nullopt;
}

std::optional<double> ray_sphere_distance(
    const Vec3& origin,
    const Vec3& direction,
    const Vec3& center,
    const double radius) {
    const auto oc = subtract(origin, center);
    const auto b = dot(oc, direction);
    const auto c = dot(oc, oc) - radius * radius;
    const auto discriminant = b * b - c;
    if (discriminant < 0.0) return std::nullopt;
    const auto root = std::sqrt(discriminant);
    const auto near_value = -b - root;
    const auto far_value = -b + root;
    if (near_value >= 0.0) return near_value;
    if (far_value >= 0.0) return far_value;
    return std::nullopt;
}

struct SlabHit {
    double distance{0.0};
    Vec3 normal{};
};

std::optional<SlabHit> ray_aabb(
    const Vec3& origin,
    const Vec3& direction,
    const Vec3& center,
    const Vec3& extents) {
    double t_min = 0.0;
    double t_max = std::numeric_limits<double>::infinity();
    Vec3 hit_normal{};

    const auto axis = [&](const double o, const double d,
                          const double low, const double high,
                          const Vec3& low_normal,
                          const Vec3& high_normal,
                          double& current_min,
                          double& current_max,
                          Vec3& current_normal) -> bool {
        if (std::abs(d) <= kEpsilon) {
            return o >= low && o <= high;
        }
        double t1 = (low - o) / d;
        double t2 = (high - o) / d;
        Vec3 n1 = low_normal;
        Vec3 n2 = high_normal;
        if (t1 > t2) {
            std::swap(t1, t2);
            std::swap(n1, n2);
        }
        if (t1 > current_min) {
            current_min = t1;
            current_normal = n1;
        }
        current_max = std::min(current_max, t2);
        return current_min <= current_max;
    };

    if (!axis(
            origin.x, direction.x,
            center.x - extents.x, center.x + extents.x,
            {-1.0, 0.0, 0.0}, {1.0, 0.0, 0.0},
            t_min, t_max, hit_normal) ||
        !axis(
            origin.y, direction.y,
            center.y - extents.y, center.y + extents.y,
            {0.0, -1.0, 0.0}, {0.0, 1.0, 0.0},
            t_min, t_max, hit_normal) ||
        !axis(
            origin.z, direction.z,
            center.z - extents.z, center.z + extents.z,
            {0.0, 0.0, -1.0}, {0.0, 0.0, 1.0},
            t_min, t_max, hit_normal)) {
        return std::nullopt;
    }

    if (t_max < 0.0) return std::nullopt;
    return SlabHit{
        .distance = std::max(0.0, t_min),
        .normal = hit_normal};
}

std::optional<RaycastHit> ray_body(
    const Vec3& origin,
    const Vec3& direction,
    const Body& body) {
    if (body.shape == CollisionShape::sphere) {
        const auto distance = ray_sphere_distance(
            origin, direction, body.position, body.radius);
        if (!distance) return std::nullopt;
        const auto point =
            add(origin, scale(direction, *distance));
        return RaycastHit{
            .body_id = body.id,
            .point = point,
            .normal = normalized(subtract(point, body.position)),
            .distance = *distance,
            .ground = false};
    }

    if (body.shape == CollisionShape::box) {
        const auto hit = ray_aabb(
            origin, direction, body.position, body.half_extents);
        if (!hit) return std::nullopt;
        return RaycastHit{
            .body_id = body.id,
            .point = add(origin, scale(direction, hit->distance)),
            .normal = hit->normal,
            .distance = hit->distance,
            .ground = false};
    }

    std::optional<RaycastHit> best;
    const Vec3 bottom{
        body.position.x,
        body.position.y,
        body.position.z - body.capsule_half_height};
    const Vec3 top{
        body.position.x,
        body.position.y,
        body.position.z + body.capsule_half_height};

    for (const auto& center : {bottom, top}) {
        const auto distance =
            ray_sphere_distance(origin, direction, center, body.radius);
        if (!distance) continue;
        const auto point =
            add(origin, scale(direction, *distance));
        RaycastHit candidate{
            .body_id = body.id,
            .point = point,
            .normal = normalized(subtract(point, center)),
            .distance = *distance,
            .ground = false};
        if (!best || candidate.distance < best->distance) {
            best = candidate;
        }
    }

    const auto ox = origin.x - body.position.x;
    const auto oy = origin.y - body.position.y;
    const auto a = direction.x * direction.x +
                   direction.y * direction.y;
    if (a > kEpsilon) {
        const auto b =
            2.0 * (ox * direction.x + oy * direction.y);
        const auto c =
            ox * ox + oy * oy - body.radius * body.radius;
        const auto discriminant = b * b - 4.0 * a * c;
        if (discriminant >= 0.0) {
            const auto root = std::sqrt(discriminant);
            for (const auto distance : {
                     (-b - root) / (2.0 * a),
                     (-b + root) / (2.0 * a)}) {
                if (distance < 0.0) continue;
                const auto z =
                    origin.z + direction.z * distance;
                if (z < bottom.z || z > top.z) continue;
                const auto point =
                    add(origin, scale(direction, distance));
                const auto normal = normalized(Vec3{
                    point.x - body.position.x,
                    point.y - body.position.y,
                    0.0});
                RaycastHit candidate{
                    .body_id = body.id,
                    .point = point,
                    .normal = normal,
                    .distance = distance,
                    .ground = false};
                if (!best || candidate.distance < best->distance) {
                    best = candidate;
                }
            }
        }
    }
    return best;
}

} // namespace

const char* collision_shape_name(
    const CollisionShape shape) noexcept {
    switch (shape) {
        case CollisionShape::sphere: return "sphere";
        case CollisionShape::box: return "box";
        case CollisionShape::capsule: return "capsule";
    }
    return "sphere";
}

std::optional<CollisionShape> parse_collision_shape(
    const char* value) noexcept {
    if (value == nullptr) return std::nullopt;
    const std::string_view text{value};
    if (text == "sphere") return CollisionShape::sphere;
    if (text == "box") return CollisionShape::box;
    if (text == "capsule") return CollisionShape::capsule;
    return std::nullopt;
}

std::uint64_t PhysicsWorld::add_body(Body body) {
    std::scoped_lock lock(mutex_);
    if (body.id == 0) {
        body.id = next_id_++;
    } else {
        next_id_ = std::max(next_id_, body.id + 1U);
    }

    body.mass = std::clamp(body.mass, 0.001, 100000.0);
    body.radius = std::clamp(body.radius, 0.01, 4096.0);
    body.half_extents = {
        std::clamp(body.half_extents.x, 0.01, 4096.0),
        std::clamp(body.half_extents.y, 0.01, 4096.0),
        std::clamp(body.half_extents.z, 0.01, 4096.0)};
    body.capsule_half_height =
        std::clamp(body.capsule_half_height, 0.0, 4096.0);
    body.restitution = std::clamp(body.restitution, 0.0, 1.0);
    body.friction = std::clamp(body.friction, 0.0, 2.0);
    body.linear_damping = std::clamp(body.linear_damping, 0.0, 10.0);
    body.angular_damping = std::clamp(body.angular_damping, 0.0, 10.0);
    body.gravity_scale = std::clamp(body.gravity_scale, -4.0, 4.0);
    body.buoyancy = std::clamp(body.buoyancy, -1.0, 1.0);
    body.max_slope_degrees =
        std::clamp(body.max_slope_degrees, 0.0, 89.0);
    body.step_height = std::clamp(body.step_height, 0.0, 4.0);
    body.jump_speed = std::clamp(body.jump_speed, 0.0, 100.0);
    body.rotation = {
        normalized_degrees(body.rotation.x),
        normalized_degrees(body.rotation.y),
        normalized_degrees(body.rotation.z)};
    if (!finite_vec(body.position)) body.position = {};
    if (!finite_vec(body.velocity)) body.velocity = {};
    if (!finite_vec(body.angular_velocity)) body.angular_velocity = {};
    if (!finite_vec(body.force)) body.force = {};
    if (!finite_vec(body.torque)) body.torque = {};
    if (body.character) {
        body.shape = CollisionShape::capsule;
        body.dynamic = true;
    }

    bodies_[body.id] = body;
    return body.id;
}

bool PhysicsWorld::remove_body(const std::uint64_t id) {
    std::scoped_lock lock(mutex_);
    const bool removed = bodies_.erase(id) > 0U;
    if (!removed) return false;

    for (auto it = constraints_.begin(); it != constraints_.end();) {
        if (it->second.body_a == id || it->second.body_b == id) {
            it = constraints_.erase(it);
        } else {
            ++it;
        }
    }
    for (auto it = spring_constraints_.begin();
         it != spring_constraints_.end();) {
        if (it->second.body_a == id || it->second.body_b == id) {
            it = spring_constraints_.erase(it);
        } else {
            ++it;
        }
    }
    collisions_.erase(
        std::remove_if(
            collisions_.begin(), collisions_.end(),
            [id](const CollisionContact& contact) {
                return contact.body_a == id || contact.body_b == id;
            }),
        collisions_.end());
    return true;
}

bool PhysicsWorld::set_body_position(
    const std::uint64_t id, const Vec3 position) {
    if (!finite_vec(position)) return false;
    std::scoped_lock lock(mutex_);
    const auto it = bodies_.find(id);
    if (it == bodies_.end()) return false;
    it->second.position = position;
    return true;
}

bool PhysicsWorld::set_body_velocity(
    const std::uint64_t id, const Vec3 velocity) {
    if (!finite_vec(velocity)) return false;
    std::scoped_lock lock(mutex_);
    const auto it = bodies_.find(id);
    if (it == bodies_.end()) return false;
    it->second.velocity = velocity;
    return true;
}

bool PhysicsWorld::set_body_rotation(
    const std::uint64_t id, const Vec3 rotation) {
    if (!finite_vec(rotation)) return false;
    std::scoped_lock lock(mutex_);
    const auto it = bodies_.find(id);
    if (it == bodies_.end()) return false;
    it->second.rotation = {
        normalized_degrees(rotation.x),
        normalized_degrees(rotation.y),
        normalized_degrees(rotation.z)};
    return true;
}

bool PhysicsWorld::set_body_angular_velocity(
    const std::uint64_t id, const Vec3 angular_velocity) {
    if (!finite_vec(angular_velocity)) return false;
    std::scoped_lock lock(mutex_);
    const auto it = bodies_.find(id);
    if (it == bodies_.end()) return false;
    it->second.angular_velocity = angular_velocity;
    return true;
}

bool PhysicsWorld::set_body_material(
    const std::uint64_t id, const double mass,
    const double restitution, const double friction) {
    if (!std::isfinite(mass) || !std::isfinite(restitution) ||
        !std::isfinite(friction) || mass <= 0.0) {
        return false;
    }
    std::scoped_lock lock(mutex_);
    const auto it = bodies_.find(id);
    if (it == bodies_.end()) return false;
    it->second.mass = std::clamp(mass, 0.001, 100000.0);
    it->second.restitution = std::clamp(restitution, 0.0, 1.0);
    it->second.friction = std::clamp(friction, 0.0, 2.0);
    return true;
}

bool PhysicsWorld::set_body_damping(
    const std::uint64_t id, const double linear_damping,
    const double angular_damping) {
    if (!std::isfinite(linear_damping) ||
        !std::isfinite(angular_damping)) {
        return false;
    }
    std::scoped_lock lock(mutex_);
    const auto it = bodies_.find(id);
    if (it == bodies_.end()) return false;
    it->second.linear_damping =
        std::clamp(linear_damping, 0.0, 10.0);
    it->second.angular_damping =
        std::clamp(angular_damping, 0.0, 10.0);
    return true;
}

bool PhysicsWorld::set_body_gravity_scale(
    const std::uint64_t id, const double gravity_scale) {
    if (!std::isfinite(gravity_scale)) return false;
    std::scoped_lock lock(mutex_);
    const auto it = bodies_.find(id);
    if (it == bodies_.end()) return false;
    it->second.gravity_scale =
        std::clamp(gravity_scale, -4.0, 4.0);
    return true;
}

bool PhysicsWorld::set_body_buoyancy(
    const std::uint64_t id, const double buoyancy) {
    if (!std::isfinite(buoyancy)) return false;
    std::scoped_lock lock(mutex_);
    const auto it = bodies_.find(id);
    if (it == bodies_.end()) return false;
    it->second.buoyancy = std::clamp(buoyancy, -1.0, 1.0);
    return true;
}

bool PhysicsWorld::set_body_radius(
    const std::uint64_t id, const double radius) {
    if (!std::isfinite(radius) || radius <= 0.0) return false;
    std::scoped_lock lock(mutex_);
    const auto it = bodies_.find(id);
    if (it == bodies_.end()) return false;
    it->second.radius = std::clamp(radius, 0.01, 4096.0);
    return true;
}

bool PhysicsWorld::set_body_shape(
    const std::uint64_t id,
    const CollisionShape shape,
    const Vec3 half_extents,
    const double capsule_half_height) {
    if (!finite_vec(half_extents) ||
        half_extents.x <= 0.0 ||
        half_extents.y <= 0.0 ||
        half_extents.z <= 0.0 ||
        !std::isfinite(capsule_half_height) ||
        capsule_half_height < 0.0) {
        return false;
    }
    std::scoped_lock lock(mutex_);
    const auto it = bodies_.find(id);
    if (it == bodies_.end()) return false;
    it->second.shape = shape;
    it->second.half_extents = {
        std::clamp(half_extents.x, 0.01, 4096.0),
        std::clamp(half_extents.y, 0.01, 4096.0),
        std::clamp(half_extents.z, 0.01, 4096.0)};
    it->second.capsule_half_height =
        std::clamp(capsule_half_height, 0.0, 4096.0);
    if (shape != CollisionShape::capsule) {
        it->second.character = false;
        it->second.grounded = false;
    }
    return true;
}

bool PhysicsWorld::set_character_controller(
    const std::uint64_t id,
    const double max_slope_degrees,
    const double step_height,
    const double jump_speed) {
    if (!std::isfinite(max_slope_degrees) ||
        !std::isfinite(step_height) ||
        !std::isfinite(jump_speed) ||
        max_slope_degrees < 0.0 ||
        step_height < 0.0 ||
        jump_speed < 0.0) {
        return false;
    }
    std::scoped_lock lock(mutex_);
    const auto it = bodies_.find(id);
    if (it == bodies_.end()) return false;
    it->second.character = true;
    it->second.dynamic = true;
    it->second.shape = CollisionShape::capsule;
    it->second.max_slope_degrees =
        std::clamp(max_slope_degrees, 0.0, 89.0);
    it->second.step_height =
        std::clamp(step_height, 0.0, 4.0);
    it->second.jump_speed =
        std::clamp(jump_speed, 0.0, 100.0);
    return true;
}

bool PhysicsWorld::character_jump(const std::uint64_t id) {
    std::scoped_lock lock(mutex_);
    const auto it = bodies_.find(id);
    if (it == bodies_.end() ||
        !it->second.character ||
        !it->second.grounded) {
        return false;
    }
    it->second.velocity.z =
        std::max(it->second.velocity.z, it->second.jump_speed);
    it->second.grounded = false;
    return true;
}

bool PhysicsWorld::apply_force(
    const std::uint64_t id, const Vec3 force) {
    if (!finite_vec(force)) return false;
    std::scoped_lock lock(mutex_);
    const auto it = bodies_.find(id);
    if (it == bodies_.end() || !it->second.dynamic) return false;
    it->second.force += force;
    return true;
}

bool PhysicsWorld::apply_impulse(
    const std::uint64_t id, const Vec3 impulse) {
    if (!finite_vec(impulse)) return false;
    std::scoped_lock lock(mutex_);
    const auto it = bodies_.find(id);
    if (it == bodies_.end() || !it->second.dynamic) return false;
    add_scaled(
        it->second.velocity,
        impulse,
        inverse_mass(it->second));
    return true;
}

bool PhysicsWorld::apply_angular_impulse(
    const std::uint64_t id, const Vec3 impulse) {
    if (!finite_vec(impulse)) return false;
    std::scoped_lock lock(mutex_);
    const auto it = bodies_.find(id);
    if (it == bodies_.end() || !it->second.dynamic) return false;
    add_scaled(
        it->second.angular_velocity,
        impulse,
        inverse_mass(it->second));
    return true;
}

bool PhysicsWorld::apply_torque(
    const std::uint64_t id, const Vec3 torque) {
    if (!finite_vec(torque)) return false;
    std::scoped_lock lock(mutex_);
    const auto it = bodies_.find(id);
    if (it == bodies_.end() || !it->second.dynamic) return false;
    it->second.torque += torque;
    return true;
}

std::uint64_t PhysicsWorld::add_distance_constraint(
    const std::uint64_t body_a,
    const std::uint64_t body_b,
    const double rest_length,
    const double stiffness) {
    if (body_a == 0U || body_b == 0U || body_a == body_b ||
        !std::isfinite(rest_length) || rest_length < 0.0 ||
        !std::isfinite(stiffness)) {
        return 0U;
    }

    std::scoped_lock lock(mutex_);
    if (!bodies_.contains(body_a) || !bodies_.contains(body_b)) {
        return 0U;
    }
    const auto id = next_constraint_id_++;
    constraints_[id] = {
        .id = id,
        .body_a = body_a,
        .body_b = body_b,
        .rest_length = rest_length,
        .stiffness = std::clamp(stiffness, 0.0, 1.0)};
    return id;
}

std::uint64_t PhysicsWorld::add_spring_constraint(
    const std::uint64_t body_a,
    const std::uint64_t body_b,
    const double rest_length,
    const double stiffness,
    const double damping) {
    if (body_a == 0U || body_b == 0U || body_a == body_b ||
        !std::isfinite(rest_length) || rest_length < 0.0 ||
        !std::isfinite(stiffness) || stiffness < 0.0 ||
        !std::isfinite(damping) || damping < 0.0) {
        return 0U;
    }

    std::scoped_lock lock(mutex_);
    if (!bodies_.contains(body_a) || !bodies_.contains(body_b)) {
        return 0U;
    }
    const auto id = next_constraint_id_++;
    spring_constraints_[id] = {
        .id = id,
        .body_a = body_a,
        .body_b = body_b,
        .rest_length = rest_length,
        .stiffness = std::clamp(stiffness, 0.0, 10000.0),
        .damping = std::clamp(damping, 0.0, 1000.0)};
    return id;
}

bool PhysicsWorld::remove_constraint(
    const std::uint64_t constraint_id) {
    std::scoped_lock lock(mutex_);
    const bool distance_removed =
        constraints_.erase(constraint_id) > 0U;
    const bool spring_removed =
        spring_constraints_.erase(constraint_id) > 0U;
    return distance_removed || spring_removed;
}

std::optional<RaycastHit> PhysicsWorld::raycast(
    const Vec3 origin,
    const Vec3 direction,
    const double max_distance,
    const std::uint64_t ignore_body) const {
    if (!finite_vec(origin) || !finite_vec(direction) ||
        !std::isfinite(max_distance) ||
        max_distance <= 0.0) {
        return std::nullopt;
    }
    const auto ray_direction = normalized(direction);
    if (length_squared(ray_direction) <= kEpsilon) {
        return std::nullopt;
    }

    std::scoped_lock lock(mutex_);
    std::optional<RaycastHit> best;
    for (const auto& [id, body] : bodies_) {
        if (id == ignore_body) continue;
        const auto hit = ray_body(origin, ray_direction, body);
        if (!hit || hit->distance > max_distance) continue;
        if (!best || hit->distance < best->distance) best = hit;
    }

    const auto ground_at = [&](const double x, const double y) {
        return ground_sampler_ ? ground_sampler_(x, y) : ground_height_;
    };
    const auto samples = std::clamp(
        static_cast<int>(std::ceil(max_distance * 2.0)),
        16, 512);
    double previous_t = 0.0;
    auto previous_point = origin;
    double previous_delta =
        previous_point.z -
        ground_at(previous_point.x, previous_point.y);
    for (int index = 1; index <= samples; ++index) {
        const auto t =
            max_distance *
            static_cast<double>(index) /
            static_cast<double>(samples);
        const auto point =
            add(origin, scale(ray_direction, t));
        const auto delta =
            point.z - ground_at(point.x, point.y);
        if (previous_delta > 0.0 && delta <= 0.0) {
            double low = previous_t;
            double high = t;
            for (int iteration = 0; iteration < 10; ++iteration) {
                const auto middle = (low + high) * 0.5;
                const auto middle_point =
                    add(origin, scale(ray_direction, middle));
                const auto middle_delta =
                    middle_point.z -
                    ground_at(middle_point.x, middle_point.y);
                if (middle_delta > 0.0) low = middle;
                else high = middle;
            }
            const auto distance = (low + high) * 0.5;
            if (distance <= max_distance &&
                (!best || distance < best->distance)) {
                const auto hit_point =
                    add(origin, scale(ray_direction, distance));
                constexpr double epsilon = 0.25;
                const auto hx0 = ground_at(
                    hit_point.x - epsilon, hit_point.y);
                const auto hx1 = ground_at(
                    hit_point.x + epsilon, hit_point.y);
                const auto hy0 = ground_at(
                    hit_point.x, hit_point.y - epsilon);
                const auto hy1 = ground_at(
                    hit_point.x, hit_point.y + epsilon);
                const auto normal = normalized(Vec3{
                    -(hx1 - hx0),
                    -(hy1 - hy0),
                    2.0 * epsilon});
                best = RaycastHit{
                    .body_id = 0,
                    .point = hit_point,
                    .normal = normal,
                    .distance = distance,
                    .ground = true};
            }
            break;
        }
        previous_t = t;
        previous_delta = delta;
    }
    return best;
}

void PhysicsWorld::step(const double dt) {
    if (dt <= 0.0 || !std::isfinite(dt)) return;
    const auto bounded_dt = std::min(dt, 0.25);
    const auto substeps = std::clamp(
        static_cast<int>(std::ceil(bounded_dt / (1.0 / 60.0))),
        1, 8);
    const auto sub_dt =
        bounded_dt / static_cast<double>(substeps);

    std::scoped_lock lock(mutex_);

    std::vector<std::uint64_t> ids;
    ids.reserve(bodies_.size());
    for (const auto& [id, _] : bodies_) ids.push_back(id);
    std::sort(ids.begin(), ids.end());

    for (int substep = 0; substep < substeps; ++substep) {
        collisions_.clear();

        for (auto& [_, body] : bodies_) {
            if (body.character) body.grounded = false;
        }

        for (const auto& [_, spring] : spring_constraints_) {
            auto a = bodies_.find(spring.body_a);
            auto b = bodies_.find(spring.body_b);
            if (a == bodies_.end() || b == bodies_.end()) continue;
            const auto delta =
                subtract(b->second.position, a->second.position);
            const auto distance = length(delta);
            if (distance <= kEpsilon) continue;
            const auto direction =
                scale(delta, 1.0 / distance);
            const auto relative =
                subtract(b->second.velocity, a->second.velocity);
            const auto relative_speed = dot(relative, direction);
            const auto force_magnitude =
                spring.stiffness *
                    (distance - spring.rest_length) +
                spring.damping * relative_speed;
            if (a->second.dynamic) {
                add_scaled(
                    a->second.force,
                    direction,
                    force_magnitude);
            }
            if (b->second.dynamic) {
                add_scaled(
                    b->second.force,
                    direction,
                    -force_magnitude);
            }
        }

        for (const auto id : ids) {
            auto& body = bodies_.at(id);
            if (!body.dynamic) {
                body.force = {};
                body.torque = {};
                continue;
            }

            const auto inv_mass = inverse_mass(body);
            const auto gravity_factor =
                body.gravity_scale * (1.0 - body.buoyancy);
            body.velocity.x +=
                (gravity_.x * gravity_factor +
                 body.force.x * inv_mass) *
                sub_dt;
            body.velocity.y +=
                (gravity_.y * gravity_factor +
                 body.force.y * inv_mass) *
                sub_dt;
            body.velocity.z +=
                (gravity_.z * gravity_factor +
                 body.force.z * inv_mass) *
                sub_dt;

            body.angular_velocity.x +=
                body.torque.x * inv_mass * sub_dt;
            body.angular_velocity.y +=
                body.torque.y * inv_mass * sub_dt;
            body.angular_velocity.z +=
                body.torque.z * inv_mass * sub_dt;

            const auto linear_decay =
                std::max(
                    0.0,
                    1.0 - body.linear_damping * sub_dt);
            const auto angular_decay =
                std::max(
                    0.0,
                    1.0 - body.angular_damping * sub_dt);
            body.velocity =
                scale(body.velocity, linear_decay);
            body.angular_velocity =
                scale(body.angular_velocity, angular_decay);

            add_scaled(body.position, body.velocity, sub_dt);
            body.rotation.x = normalized_degrees(
                body.rotation.x +
                body.angular_velocity.x * sub_dt);
            body.rotation.y = normalized_degrees(
                body.rotation.y +
                body.angular_velocity.y * sub_dt);
            body.rotation.z = normalized_degrees(
                body.rotation.z +
                body.angular_velocity.z * sub_dt);

            body.force = {};
            body.torque = {};
        }

        for (int iteration = 0; iteration < 4; ++iteration) {
            for (const auto& [_, constraint] : constraints_) {
                auto a = bodies_.find(constraint.body_a);
                auto b = bodies_.find(constraint.body_b);
                if (a == bodies_.end() ||
                    b == bodies_.end()) {
                    continue;
                }

                const auto delta =
                    subtract(
                        b->second.position,
                        a->second.position);
                const auto distance = length(delta);
                if (distance <= kEpsilon) continue;
                const auto error =
                    distance - constraint.rest_length;
                if (std::abs(error) <= 1e-7) continue;

                const auto inv_a =
                    inverse_mass(a->second);
                const auto inv_b =
                    inverse_mass(b->second);
                const auto inv_total = inv_a + inv_b;
                if (inv_total <= 0.0) continue;

                const auto correction =
                    scale(
                        delta,
                        (error / distance) *
                            constraint.stiffness /
                            inv_total);
                if (a->second.dynamic) {
                    add_scaled(
                        a->second.position,
                        correction,
                        inv_a);
                }
                if (b->second.dynamic) {
                    add_scaled(
                        b->second.position,
                        correction,
                        -inv_b);
                }
            }
        }

        const auto ground_at =
            [&](const double x, const double y) {
                return ground_sampler_
                           ? ground_sampler_(x, y)
                           : ground_height_;
            };
        for (const auto id : ids) {
            auto& body = bodies_.at(id);
            const auto ground =
                ground_at(
                    body.position.x,
                    body.position.y);
            const auto floor_z =
                ground + support_height(body);
            if (body.position.z >= floor_z) continue;

            const auto impact_velocity =
                body.velocity.z;
            body.position.z = floor_z;
            double impulse = 0.0;

            Vec3 ground_normal{0.0, 0.0, 1.0};
            if (ground_sampler_) {
                constexpr double epsilon = 0.25;
                const auto hx0 =
                    ground_at(
                        body.position.x - epsilon,
                        body.position.y);
                const auto hx1 =
                    ground_at(
                        body.position.x + epsilon,
                        body.position.y);
                const auto hy0 =
                    ground_at(
                        body.position.x,
                        body.position.y - epsilon);
                const auto hy1 =
                    ground_at(
                        body.position.x,
                        body.position.y + epsilon);
                ground_normal = normalized(Vec3{
                    -(hx1 - hx0),
                    -(hy1 - hy0),
                    2.0 * epsilon});
            }

            if (body.character) {
                const auto slope_limit =
                    std::cos(
                        body.max_slope_degrees *
                        kPi / 180.0);
                if (ground_normal.z >= slope_limit) {
                    body.grounded = true;
                    if (body.velocity.z < 0.0) {
                        impulse =
                            std::abs(body.velocity.z) *
                            body.mass;
                        body.velocity.z = 0.0;
                    }
                    const auto drag =
                        std::clamp(
                            body.friction *
                                sub_dt * 8.0,
                            0.0, 1.0);
                    body.velocity.x *= 1.0 - drag;
                    body.velocity.y *= 1.0 - drag;
                } else {
                    const auto gravity_normal =
                        dot(gravity_, ground_normal);
                    const auto slide =
                        subtract(
                            gravity_,
                            scale(
                                ground_normal,
                                gravity_normal));
                    add_scaled(
                        body.velocity,
                        slide,
                        sub_dt);
                    if (body.velocity.z < 0.0) {
                        body.velocity.z = 0.0;
                    }
                }
            } else {
                if (body.velocity.z < 0.0) {
                    const auto previous =
                        body.velocity.z;
                    body.velocity.z =
                        -body.velocity.z *
                        body.restitution;
                    impulse =
                        std::abs(
                            body.velocity.z -
                            previous) *
                        body.mass;
                }

                const auto ground_drag =
                    std::clamp(
                        body.friction *
                            sub_dt * 5.0,
                        0.0, 1.0);
                body.velocity.x *=
                    1.0 - ground_drag;
                body.velocity.y *=
                    1.0 - ground_drag;
                if (std::abs(body.velocity.z) < 0.01) {
                    body.velocity.z = 0.0;
                }
            }

            collisions_.push_back({
                .body_a = id,
                .body_b = 0,
                .point = {
                    body.position.x,
                    body.position.y,
                    ground},
                .normal = ground_normal,
                .impulse = std::max(
                    impulse,
                    std::abs(impact_velocity) *
                        body.mass),
                .ground = true});
        }

        for (std::size_t left_index = 0;
             left_index < ids.size(); ++left_index) {
            for (std::size_t right_index =
                     left_index + 1U;
                 right_index < ids.size();
                 ++right_index) {
                auto& a = bodies_.at(ids[left_index]);
                auto& b = bodies_.at(ids[right_index]);
                if (!a.dynamic && !b.dynamic) continue;

                const auto geometry =
                    collision_geometry(a, b);
                if (!geometry ||
                    geometry->penetration <= 0.0) {
                    continue;
                }

                const auto inv_a = inverse_mass(a);
                const auto inv_b = inverse_mass(b);
                const auto inv_total = inv_a + inv_b;
                if (inv_total <= 0.0) continue;

                const auto correction =
                    std::max(
                        0.0,
                        geometry->penetration - 0.0001) *
                    0.8 / inv_total;
                if (a.dynamic) {
                    add_scaled(
                        a.position,
                        geometry->normal,
                        -correction * inv_a);
                }
                if (b.dynamic) {
                    add_scaled(
                        b.position,
                        geometry->normal,
                        correction * inv_b);
                }

                auto relative =
                    subtract(b.velocity, a.velocity);
                const auto normal_velocity =
                    dot(relative, geometry->normal);
                double normal_impulse = 0.0;
                if (normal_velocity < 0.0) {
                    const auto restitution =
                        std::min(
                            a.restitution,
                            b.restitution);
                    normal_impulse =
                        -(1.0 + restitution) *
                        normal_velocity /
                        inv_total;
                    if (a.dynamic) {
                        add_scaled(
                            a.velocity,
                            geometry->normal,
                            -normal_impulse * inv_a);
                    }
                    if (b.dynamic) {
                        add_scaled(
                            b.velocity,
                            geometry->normal,
                            normal_impulse * inv_b);
                    }

                    relative =
                        subtract(b.velocity, a.velocity);
                    const auto tangent_raw =
                        subtract(
                            relative,
                            scale(
                                geometry->normal,
                                dot(
                                    relative,
                                    geometry->normal)));
                    const auto tangent_length =
                        length(tangent_raw);
                    if (tangent_length > kEpsilon) {
                        const auto tangent =
                            scale(
                                tangent_raw,
                                1.0 /
                                    tangent_length);
                        auto friction_impulse =
                            -dot(
                                relative,
                                tangent) /
                            inv_total;
                        const auto friction =
                            std::sqrt(
                                a.friction *
                                b.friction);
                        const auto maximum =
                            std::abs(normal_impulse) *
                            friction;
                        friction_impulse =
                            std::clamp(
                                friction_impulse,
                                -maximum,
                                maximum);
                        if (a.dynamic) {
                            add_scaled(
                                a.velocity,
                                tangent,
                                -friction_impulse *
                                    inv_a);
                        }
                        if (b.dynamic) {
                            add_scaled(
                                b.velocity,
                                tangent,
                                friction_impulse *
                                    inv_b);
                        }
                    }
                }

                collisions_.push_back({
                    .body_a = a.id,
                    .body_b = b.id,
                    .point = geometry->point,
                    .normal = geometry->normal,
                    .impulse =
                        std::abs(normal_impulse),
                    .ground = false});
            }
        }
    }
}

std::size_t PhysicsWorld::body_count() const {
    std::scoped_lock lock(mutex_);
    return bodies_.size();
}

Body PhysicsWorld::body(
    const std::uint64_t id) const {
    std::scoped_lock lock(mutex_);
    const auto it = bodies_.find(id);
    if (it == bodies_.end()) {
        throw std::runtime_error(
            "unknown physics body");
    }
    return it->second;
}

std::vector<CollisionContact>
PhysicsWorld::collisions() const {
    std::scoped_lock lock(mutex_);
    return collisions_;
}

std::vector<DistanceConstraint>
PhysicsWorld::constraints() const {
    std::scoped_lock lock(mutex_);
    std::vector<DistanceConstraint> result;
    result.reserve(constraints_.size());
    for (const auto& [_, constraint] : constraints_) {
        result.push_back(constraint);
    }
    std::sort(
        result.begin(),
        result.end(),
        [](const DistanceConstraint& left,
           const DistanceConstraint& right) {
            return left.id < right.id;
        });
    return result;
}

std::vector<SpringConstraint>
PhysicsWorld::spring_constraints() const {
    std::scoped_lock lock(mutex_);
    std::vector<SpringConstraint> result;
    result.reserve(spring_constraints_.size());
    for (const auto& [_, constraint] :
         spring_constraints_) {
        result.push_back(constraint);
    }
    std::sort(
        result.begin(),
        result.end(),
        [](const SpringConstraint& left,
           const SpringConstraint& right) {
            return left.id < right.id;
        });
    return result;
}

void PhysicsWorld::set_gravity(
    const Vec3 gravity) {
    if (!finite_vec(gravity)) return;
    std::scoped_lock lock(mutex_);
    gravity_ = gravity;
}

void PhysicsWorld::set_ground_height(
    const double height) {
    if (!std::isfinite(height)) return;
    std::scoped_lock lock(mutex_);
    ground_height_ = height;
    ground_sampler_ = {};
}

void PhysicsWorld::set_ground_sampler(
    std::function<double(double, double)> sampler) {
    std::scoped_lock lock(mutex_);
    ground_sampler_ = std::move(sampler);
}

} // namespace opengenesis::physics
