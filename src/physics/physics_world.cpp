#include "opengenesis/physics/physics_world.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace opengenesis::physics {
namespace {

double normalized_degrees(double value) {
    if (!std::isfinite(value)) return 0.0;
    value = std::fmod(value, 360.0);
    if (value < 0.0) value += 360.0;
    return value;
}

} // namespace

std::uint64_t PhysicsWorld::add_body(Body body) {
    std::scoped_lock lock(mutex_);
    if (body.id == 0) body.id = next_id_++;
    else next_id_ = std::max(next_id_, body.id + 1);
    body.mass = std::max(0.001, body.mass);
    body.radius = std::max(0.01, body.radius);
    body.rotation = {normalized_degrees(body.rotation.x),
                     normalized_degrees(body.rotation.y),
                     normalized_degrees(body.rotation.z)};
    bodies_[body.id] = body;
    return body.id;
}

bool PhysicsWorld::remove_body(const std::uint64_t id) {
    std::scoped_lock lock(mutex_);
    return bodies_.erase(id) > 0;
}

bool PhysicsWorld::set_body_position(const std::uint64_t id, const Vec3 position) {
    std::scoped_lock lock(mutex_);
    const auto it = bodies_.find(id);
    if (it == bodies_.end()) return false;
    it->second.position = position;
    return true;
}

bool PhysicsWorld::set_body_velocity(const std::uint64_t id, const Vec3 velocity) {
    std::scoped_lock lock(mutex_);
    const auto it = bodies_.find(id);
    if (it == bodies_.end()) return false;
    it->second.velocity = velocity;
    return true;
}

bool PhysicsWorld::set_body_rotation(const std::uint64_t id, const Vec3 rotation) {
    std::scoped_lock lock(mutex_);
    const auto it = bodies_.find(id);
    if (it == bodies_.end()) return false;
    it->second.rotation = {normalized_degrees(rotation.x),
                           normalized_degrees(rotation.y),
                           normalized_degrees(rotation.z)};
    return true;
}

bool PhysicsWorld::set_body_angular_velocity(
    const std::uint64_t id, const Vec3 angular_velocity) {
    std::scoped_lock lock(mutex_);
    const auto it = bodies_.find(id);
    if (it == bodies_.end()) return false;
    it->second.angular_velocity = angular_velocity;
    return true;
}

void PhysicsWorld::step(const double dt) {
    if (dt <= 0.0 || !std::isfinite(dt)) return;
    std::scoped_lock lock(mutex_);
    for (auto& [_, body] : bodies_) {
        if (!body.dynamic) continue;
        body.velocity.x += gravity_.x * dt;
        body.velocity.y += gravity_.y * dt;
        body.velocity.z += gravity_.z * dt;
        body.position.x += body.velocity.x * dt;
        body.position.y += body.velocity.y * dt;
        body.position.z += body.velocity.z * dt;

        body.rotation.x = normalized_degrees(
            body.rotation.x + body.angular_velocity.x * dt);
        body.rotation.y = normalized_degrees(
            body.rotation.y + body.angular_velocity.y * dt);
        body.rotation.z = normalized_degrees(
            body.rotation.z + body.angular_velocity.z * dt);

        const double ground = ground_sampler_
                                  ? ground_sampler_(body.position.x, body.position.y)
                                  : ground_height_;
        const double floor_z = ground + body.radius;
        if (body.position.z < floor_z) {
            body.position.z = floor_z;
            if (body.velocity.z < 0.0) {
                body.velocity.z =
                    -body.velocity.z *
                    std::clamp(body.restitution, 0.0, 1.0);
            }
            if (std::abs(body.velocity.z) < 0.01) body.velocity.z = 0.0;
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
    if (it == bodies_.end()) throw std::runtime_error("unknown physics body");
    return it->second;
}

void PhysicsWorld::set_gravity(const Vec3 gravity) {
    std::scoped_lock lock(mutex_);
    gravity_ = gravity;
}

void PhysicsWorld::set_ground_height(const double height) {
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
