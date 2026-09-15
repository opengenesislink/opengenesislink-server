#pragma once
#include "opengenesis/physics/physics_world.hpp"
#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
namespace opengenesis::world {
struct Entity { std::uint64_t id{0}; std::string name; bool avatar{false}; std::uint64_t physics_body{0}; };
struct RuntimeMetrics { std::uint64_t ticks{0},entities{0},avatars{0},physics_bodies{0}; double sim_fps{0.0}; };
class RegionRuntime final {
public:
    RegionRuntime(std::string id,double target_hz); ~RegionRuntime(); RegionRuntime(const RegionRuntime&)=delete; RegionRuntime& operator=(const RegionRuntime&)=delete;
    void start(); void stop(); std::uint64_t spawn_entity(std::string name,bool avatar,bool physical=true); bool remove_entity(std::uint64_t id);
    [[nodiscard]] RuntimeMetrics metrics() const; [[nodiscard]] const std::string& id() const { return id_; }
private: void loop(); std::string id_; double target_hz_; std::atomic_bool running_{false}; std::thread thread_; mutable std::mutex mutex_; std::unordered_map<std::uint64_t,Entity> entities_; std::uint64_t next_entity_{1}; std::atomic<std::uint64_t> ticks_{0}; std::atomic<double> sim_fps_{0}; physics::PhysicsWorld physics_;
};
}
