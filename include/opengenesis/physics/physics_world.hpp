#pragma once

#include <cstdint>
#include <functional>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace opengenesis::physics {

struct Vec3 {
    double x{0}, y{0}, z{0};
    Vec3& operator+=(const Vec3& other) {
        x += other.x;
        y += other.y;
        z += other.z;
        return *this;
    }
};

struct Body {
    std::uint64_t id{0};
    Vec3 position{}, velocity{};
    Vec3 rotation{}, angular_velocity{};
    Vec3 force{}, torque{};
    double mass{1.0};
    double restitution{0.15};
    double friction{0.6};
    double radius{0.5};
    double linear_damping{0.04};
    double angular_damping{0.04};
    double gravity_scale{1.0};
    double buoyancy{0.0};
    bool dynamic{true};
    bool character{false};
};

struct CollisionContact {
    std::uint64_t body_a{0};
    std::uint64_t body_b{0};
    Vec3 point{};
    Vec3 normal{};
    double impulse{0.0};
    bool ground{false};
};

struct DistanceConstraint {
    std::uint64_t id{0};
    std::uint64_t body_a{0};
    std::uint64_t body_b{0};
    double rest_length{0.0};
    double stiffness{1.0};
};

class PhysicsWorld final {
public:
    std::uint64_t add_body(Body body);
    bool remove_body(std::uint64_t id);
    bool set_body_position(std::uint64_t id, Vec3 position);
    bool set_body_velocity(std::uint64_t id, Vec3 velocity);
    bool set_body_rotation(std::uint64_t id, Vec3 rotation);
    bool set_body_angular_velocity(std::uint64_t id, Vec3 angular_velocity);
    bool set_body_material(std::uint64_t id, double mass,
                           double restitution, double friction);
    bool set_body_damping(std::uint64_t id, double linear_damping,
                          double angular_damping);
    bool set_body_gravity_scale(std::uint64_t id, double gravity_scale);
    bool set_body_buoyancy(std::uint64_t id, double buoyancy);
    bool set_body_radius(std::uint64_t id, double radius);
    bool apply_force(std::uint64_t id, Vec3 force);
    bool apply_impulse(std::uint64_t id, Vec3 impulse);
    bool apply_angular_impulse(std::uint64_t id, Vec3 impulse);
    bool apply_torque(std::uint64_t id, Vec3 torque);

    std::uint64_t add_distance_constraint(std::uint64_t body_a,
                                          std::uint64_t body_b,
                                          double rest_length,
                                          double stiffness = 1.0);
    bool remove_constraint(std::uint64_t constraint_id);

    void step(double dt);

    [[nodiscard]] std::size_t body_count() const;
    [[nodiscard]] Body body(std::uint64_t id) const;
    [[nodiscard]] std::vector<CollisionContact> collisions() const;
    [[nodiscard]] std::vector<DistanceConstraint> constraints() const;

    void set_gravity(Vec3 gravity);
    void set_ground_height(double height);
    void set_ground_sampler(std::function<double(double, double)> sampler);

private:
    mutable std::mutex mutex_;
    std::unordered_map<std::uint64_t, Body> bodies_;
    std::unordered_map<std::uint64_t, DistanceConstraint> constraints_;
    std::vector<CollisionContact> collisions_;
    std::uint64_t next_id_{1};
    std::uint64_t next_constraint_id_{1};
    Vec3 gravity_{0, 0, -9.81};
    double ground_height_{0.0};
    std::function<double(double, double)> ground_sampler_;
};

} // namespace opengenesis::physics
