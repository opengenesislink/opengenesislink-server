#include "opengenesis/physics/physics_world.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>
#include <vector>

namespace opengenesis::physics {
namespace {

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

Vec3 normalized(const Vec3& value, const Vec3 fallback = {1.0, 0.0, 0.0}) {
    const auto magnitude = length(value);
    if (magnitude <= 1e-12) return fallback;
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

} // namespace

std::uint64_t PhysicsWorld::add_body(Body body) {
    std::scoped_lock lock(mutex_);
    if (body.id == 0) {
        body.id = next_id_++;
    } else {
        next_id_ = std::max(next_id_, body.id + 1U);
    }

    body.mass = std::max(0.001, body.mass);
    body.radius = std::max(0.01, body.radius);
    body.restitution = std::clamp(body.restitution, 0.0, 1.0);
    body.friction = std::clamp(body.friction, 0.0, 2.0);
    body.linear_damping = std::clamp(body.linear_damping, 0.0, 10.0);
    body.angular_damping = std::clamp(body.angular_damping, 0.0, 10.0);
    body.gravity_scale = std::clamp(body.gravity_scale, -4.0, 4.0);
    body.buoyancy = std::clamp(body.buoyancy, -1.0, 1.0);
    body.rotation = {
        normalized_degrees(body.rotation.x),
        normalized_degrees(body.rotation.y),
        normalized_degrees(body.rotation.z)};
    if (!finite_vec(body.position)) body.position = {};
    if (!finite_vec(body.velocity)) body.velocity = {};
    if (!finite_vec(body.angular_velocity)) body.angular_velocity = {};
    if (!finite_vec(body.force)) body.force = {};
    if (!finite_vec(body.torque)) body.torque = {};

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
    it->second.linear_damping = std::clamp(linear_damping, 0.0, 10.0);
    it->second.angular_damping = std::clamp(angular_damping, 0.0, 10.0);
    return true;
}

bool PhysicsWorld::set_body_gravity_scale(
    const std::uint64_t id, const double gravity_scale) {
    if (!std::isfinite(gravity_scale)) return false;
    std::scoped_lock lock(mutex_);
    const auto it = bodies_.find(id);
    if (it == bodies_.end()) return false;
    it->second.gravity_scale = std::clamp(gravity_scale, -4.0, 4.0);
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
    add_scaled(it->second.velocity, impulse, inverse_mass(it->second));
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

bool PhysicsWorld::remove_constraint(
    const std::uint64_t constraint_id) {
    std::scoped_lock lock(mutex_);
    return constraints_.erase(constraint_id) > 0U;
}

void PhysicsWorld::step(const double dt) {
    if (dt <= 0.0 || !std::isfinite(dt)) return;
    const auto bounded_dt = std::min(dt, 0.25);

    std::scoped_lock lock(mutex_);
    collisions_.clear();

    std::vector<std::uint64_t> ids;
    ids.reserve(bodies_.size());
    for (const auto& [id, _] : bodies_) ids.push_back(id);
    std::sort(ids.begin(), ids.end());

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
            (gravity_.x * gravity_factor + body.force.x * inv_mass) *
            bounded_dt;
        body.velocity.y +=
            (gravity_.y * gravity_factor + body.force.y * inv_mass) *
            bounded_dt;
        body.velocity.z +=
            (gravity_.z * gravity_factor + body.force.z * inv_mass) *
            bounded_dt;

        body.angular_velocity.x += body.torque.x * inv_mass * bounded_dt;
        body.angular_velocity.y += body.torque.y * inv_mass * bounded_dt;
        body.angular_velocity.z += body.torque.z * inv_mass * bounded_dt;

        const auto linear_decay =
            std::max(0.0, 1.0 - body.linear_damping * bounded_dt);
        const auto angular_decay =
            std::max(0.0, 1.0 - body.angular_damping * bounded_dt);
        body.velocity = scale(body.velocity, linear_decay);
        body.angular_velocity =
            scale(body.angular_velocity, angular_decay);

        add_scaled(body.position, body.velocity, bounded_dt);
        body.rotation.x = normalized_degrees(
            body.rotation.x + body.angular_velocity.x * bounded_dt);
        body.rotation.y = normalized_degrees(
            body.rotation.y + body.angular_velocity.y * bounded_dt);
        body.rotation.z = normalized_degrees(
            body.rotation.z + body.angular_velocity.z * bounded_dt);

        body.force = {};
        body.torque = {};
    }

    for (int iteration = 0; iteration < 4; ++iteration) {
        for (const auto& [_, constraint] : constraints_) {
            auto a = bodies_.find(constraint.body_a);
            auto b = bodies_.find(constraint.body_b);
            if (a == bodies_.end() || b == bodies_.end()) continue;

            const auto delta =
                subtract(b->second.position, a->second.position);
            const auto distance = length(delta);
            if (distance <= 1e-12) continue;
            const auto error =
                distance - constraint.rest_length;
            if (std::abs(error) <= 1e-7) continue;

            const auto inv_a = inverse_mass(a->second);
            const auto inv_b = inverse_mass(b->second);
            const auto inv_total = inv_a + inv_b;
            if (inv_total <= 0.0) continue;

            const auto correction =
                scale(delta, (error / distance) *
                                 constraint.stiffness / inv_total);
            if (a->second.dynamic) {
                add_scaled(a->second.position, correction, inv_a);
            }
            if (b->second.dynamic) {
                add_scaled(b->second.position, correction, -inv_b);
            }
        }
    }

    for (const auto id : ids) {
        auto& body = bodies_.at(id);
        const double ground =
            ground_sampler_
                ? ground_sampler_(body.position.x, body.position.y)
                : ground_height_;
        const double floor_z = ground + body.radius;
        if (body.position.z >= floor_z) continue;

        const auto impact_velocity = body.velocity.z;
        body.position.z = floor_z;
        double impulse = 0.0;
        if (body.velocity.z < 0.0) {
            const auto previous = body.velocity.z;
            body.velocity.z =
                -body.velocity.z * body.restitution;
            impulse =
                std::abs(body.velocity.z - previous) * body.mass;
        }

        const auto ground_drag =
            std::clamp(body.friction * bounded_dt * 5.0, 0.0, 1.0);
        body.velocity.x *= 1.0 - ground_drag;
        body.velocity.y *= 1.0 - ground_drag;
        if (std::abs(body.velocity.z) < 0.01) body.velocity.z = 0.0;

        collisions_.push_back({
            .body_a = id,
            .body_b = 0,
            .point = {body.position.x, body.position.y, ground},
            .normal = {0.0, 0.0, 1.0},
            .impulse = std::max(
                impulse,
                std::abs(impact_velocity) * body.mass),
            .ground = true});
    }

    for (std::size_t left_index = 0;
         left_index < ids.size(); ++left_index) {
        for (std::size_t right_index = left_index + 1U;
             right_index < ids.size(); ++right_index) {
            auto& a = bodies_.at(ids[left_index]);
            auto& b = bodies_.at(ids[right_index]);
            if (!a.dynamic && !b.dynamic) continue;

            const auto delta = subtract(b.position, a.position);
            const auto radius = a.radius + b.radius;
            const auto distance_sq = length_squared(delta);
            if (distance_sq >= radius * radius) continue;

            const auto distance = std::sqrt(std::max(0.0, distance_sq));
            const auto normal =
                distance > 1e-12
                    ? scale(delta, 1.0 / distance)
                    : Vec3{1.0, 0.0, 0.0};
            const auto inv_a = inverse_mass(a);
            const auto inv_b = inverse_mass(b);
            const auto inv_total = inv_a + inv_b;
            if (inv_total <= 0.0) continue;

            const auto penetration = radius - distance;
            const auto correction =
                std::max(0.0, penetration - 0.0001) *
                0.8 / inv_total;
            if (a.dynamic) {
                add_scaled(a.position, normal, -correction * inv_a);
            }
            if (b.dynamic) {
                add_scaled(b.position, normal, correction * inv_b);
            }

            auto relative = subtract(b.velocity, a.velocity);
            const auto normal_velocity = dot(relative, normal);
            double normal_impulse = 0.0;
            if (normal_velocity < 0.0) {
                const auto restitution =
                    std::min(a.restitution, b.restitution);
                normal_impulse =
                    -(1.0 + restitution) * normal_velocity /
                    inv_total;
                if (a.dynamic) {
                    add_scaled(
                        a.velocity, normal,
                        -normal_impulse * inv_a);
                }
                if (b.dynamic) {
                    add_scaled(
                        b.velocity, normal,
                        normal_impulse * inv_b);
                }

                relative = subtract(b.velocity, a.velocity);
                const auto tangent_raw =
                    subtract(
                        relative,
                        scale(normal, dot(relative, normal)));
                const auto tangent_length = length(tangent_raw);
                if (tangent_length > 1e-12) {
                    const auto tangent =
                        scale(tangent_raw, 1.0 / tangent_length);
                    auto friction_impulse =
                        -dot(relative, tangent) / inv_total;
                    const auto friction =
                        std::sqrt(a.friction * b.friction);
                    const auto maximum =
                        std::abs(normal_impulse) * friction;
                    friction_impulse =
                        std::clamp(
                            friction_impulse, -maximum, maximum);
                    if (a.dynamic) {
                        add_scaled(
                            a.velocity, tangent,
                            -friction_impulse * inv_a);
                    }
                    if (b.dynamic) {
                        add_scaled(
                            b.velocity, tangent,
                            friction_impulse * inv_b);
                    }
                }
            }

            collisions_.push_back({
                .body_a = a.id,
                .body_b = b.id,
                .point = add(
                    a.position,
                    scale(normal, a.radius)),
                .normal = normal,
                .impulse = std::abs(normal_impulse),
                .ground = false});
        }
    }
}

std::size_t PhysicsWorld::body_count() const {
    std::scoped_lock lock(mutex_);
    return bodies_.size();
}

Body PhysicsWorld::body(const std::uint64_t id) const {
    std::scoped_lock lock(mutex_);
    const auto it = bodies_.find(id);
    if (it == bodies_.end()) {
        throw std::runtime_error("unknown physics body");
    }
    return it->second;
}

std::vector<CollisionContact> PhysicsWorld::collisions() const {
    std::scoped_lock lock(mutex_);
    return collisions_;
}

std::vector<DistanceConstraint> PhysicsWorld::constraints() const {
    std::scoped_lock lock(mutex_);
    std::vector<DistanceConstraint> result;
    result.reserve(constraints_.size());
    for (const auto& [_, constraint] : constraints_) {
        result.push_back(constraint);
    }
    std::sort(
        result.begin(), result.end(),
        [](const DistanceConstraint& left,
           const DistanceConstraint& right) {
            return left.id < right.id;
        });
    return result;
}

void PhysicsWorld::set_gravity(const Vec3 gravity) {
    if (!finite_vec(gravity)) return;
    std::scoped_lock lock(mutex_);
    gravity_ = gravity;
}

void PhysicsWorld::set_ground_height(const double height) {
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
