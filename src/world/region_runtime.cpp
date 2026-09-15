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

std::uint64_t RegionRuntime::spawn_object(std::string name, Transform transform, const bool physical) {
    return spawn_entity(std::move(name), EntityKind::object, transform, physical);
}

std::uint64_t RegionRuntime::spawn_avatar(std::string name, Transform transform) {
    return spawn_entity(std::move(name), EntityKind::avatar, transform, true);
}

std::uint64_t RegionRuntime::spawn_entity(std::string name, const EntityKind kind, Transform transform,
                                          const bool physical) {
    if (transform.position.z == 0.0) {
        transform.position.x = 128.0;
        transform.position.y = 128.0;
        transform.position.z = terrain_.sample(transform.position.x, transform.position.y) + 1.0;
    }

    std::scoped_lock lock(mutex_);
    const auto id = next_entity_++;
    Entity entity{.id = id, .name = std::move(name), .kind = kind, .transform = transform};
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

std::uint64_t RegionRuntime::chat(const std::uint64_t sender_entity, std::string text) {
    if (text.size() > 512) text.resize(512);
    std::scoped_lock lock(mutex_);
    if (sender_entity != 0 && !entities_.contains(sender_entity)) return 0;
    return append_event_locked("chat", sender_entity, {}, std::move(text));
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
