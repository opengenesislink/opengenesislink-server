#pragma once
#include <cstdint>
#include <mutex>
#include <unordered_map>
namespace opengenesis::physics {
struct Vec3 { double x{0},y{0},z{0}; Vec3& operator+=(const Vec3& o){x+=o.x;y+=o.y;z+=o.z;return *this;} };
struct Body { std::uint64_t id{0}; Vec3 position{},velocity{}; double mass{1.0}, restitution{0.15}; bool dynamic{true}; };
class PhysicsWorld final {
public:
    std::uint64_t add_body(Body body); bool remove_body(std::uint64_t id); void step(double dt);
    [[nodiscard]] std::size_t body_count() const; [[nodiscard]] Body body(std::uint64_t id) const;
    void set_gravity(Vec3 gravity); void set_ground_height(double height);
private: mutable std::mutex mutex_; std::unordered_map<std::uint64_t,Body> bodies_; std::uint64_t next_id_{1}; Vec3 gravity_{0,0,-9.81}; double ground_height_{0.0};
};
}
