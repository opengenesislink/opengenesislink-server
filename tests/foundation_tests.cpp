#include "opengenesis/config/toml_config.hpp"
#include "opengenesis/core/asset_store.hpp"
#include "opengenesis/core/identity_store.hpp"
#include "opengenesis/core/inventory_store.hpp"
#include "opengenesis/core/region_registry.hpp"
#include "opengenesis/core/session_store.hpp"
#include "opengenesis/core/world_registry.hpp"
#include "opengenesis/physics/physics_world.hpp"
#include "opengenesis/security/crypto.hpp"
#include "opengenesis/security/scene_ticket.hpp"
#include "opengenesis/protocol/frame.hpp"
#include "opengenesis/world/region_persistence.hpp"
#include "opengenesis/world/region_runtime.hpp"
#include "opengenesis/world/terrain.hpp"

#include <chrono>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
void expect(const bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void config_test() {
    const auto config = opengenesis::config::TomlConfig::parse("[a]\nx=42\ny=2.5\nz=true\n");
    expect(config.get_int("a.x") == 42, "config int");
    expect(config.get_double("a.y") == 2.5, "config double");
    expect(config.get_bool("a.z"), "config bool");
}

void frame_test() {
    constexpr std::uint32_t request_id = 0x01020304U;
    opengenesis::protocol::Frame frame{opengenesis::protocol::MessageType::scene_join, request_id,
                                       opengenesis::protocol::payload_from_string("region=r1\n")};
    const auto encoded = opengenesis::protocol::encode(frame);
    expect(encoded.size() == opengenesis::protocol::kHeaderSize + 10, "frame encoded size");
    expect(std::to_integer<unsigned>(encoded[8]) == 0x01U &&
               std::to_integer<unsigned>(encoded[9]) == 0x02U &&
               std::to_integer<unsigned>(encoded[10]) == 0x03U &&
               std::to_integer<unsigned>(encoded[11]) == 0x04U,
           "request id header offset");
    expect(std::to_integer<unsigned>(encoded[12]) == 0x00U &&
               std::to_integer<unsigned>(encoded[13]) == 0x00U &&
               std::to_integer<unsigned>(encoded[14]) == 0x00U &&
               std::to_integer<unsigned>(encoded[15]) == 0x0AU,
           "payload length header offset");
    const auto decoded = opengenesis::protocol::decode(encoded);
    expect(decoded.type == frame.type && decoded.request_id == request_id, "frame roundtrip");
}

void registry_test() {
    const auto dir = std::filesystem::temp_directory_path() / "ogl-tests-030";
    std::filesystem::create_directories(dir);
    const auto worlds_path = (dir / "worlds.db").string();
    const auto regions_path = (dir / "regions.db").string();
    std::filesystem::remove(worlds_path);
    std::filesystem::remove(regions_path);

    opengenesis::core::WorldRegistry worlds(worlds_path);
    const auto first = worlds.register_or_reconnect("w1", "World", "127.0.0.1:1");
    expect(first.generation == 1, "generation 1");
    const auto second = worlds.register_or_reconnect("w1", "World", "127.0.0.1:1");
    expect(second.generation == 2, "generation 2");

    opengenesis::core::RegionRegistry regions(regions_path);
    std::string reason;
    expect(regions.register_region({.id = "r1",
                                    .name = "Region",
                                    .node_id = "w1",
                                    .grid_x = 1,
                                    .grid_y = 1,
                                    .node_generation = 2},
                                   reason),
           "region register");
    expect(regions.update_state("r1", "w1", 2, "starting", reason), "region starting");
    expect(regions.update_state("r1", "w1", 2, "online", reason), "region online");
    expect(!regions.register_region({.id = "r2",
                                     .name = "Other",
                                     .node_id = "w1",
                                     .grid_x = 1,
                                     .grid_y = 1,
                                     .node_generation = 2},
                                    reason),
           "collision reject");
}

void identity_test() {
    const auto dir = std::filesystem::temp_directory_path() / "ogl-tests-040-identity";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    const auto users_path = (dir / "users.db").string();
    const auto sessions_path = (dir / "sessions.db").string();

    std::string reason;
    opengenesis::core::IdentityStore identities(users_path);
    const auto user = identities.register_user("Test.User", "Test User", "correct horse battery", reason);
    expect(user.has_value(), "identity register");
    expect(user->username == "test.user", "identity normalized username");
    expect(!identities.register_user("test.user", "Duplicate", "another password", reason),
           "identity duplicate reject");
    expect(identities.authenticate("TEST.USER", "correct horse battery").has_value(),
           "identity authenticate");
    expect(!identities.authenticate("test.user", "wrong password").has_value(),
           "identity wrong password reject");

    opengenesis::core::IdentityStore reloaded(users_path);
    expect(reloaded.count() == 1 && reloaded.find_by_id(user->id).has_value(),
           "identity persistence");

    opengenesis::core::SessionStore sessions(sessions_path, std::chrono::seconds{600});
    const auto created = sessions.create(user->id);
    expect(created.token.size() == 64, "session token length");
    expect(sessions.find(created.token).has_value(), "session lookup");
    opengenesis::core::SessionStore reloaded_sessions(sessions_path, std::chrono::seconds{600});
    expect(reloaded_sessions.find(created.token).has_value(), "session persistence");
    expect(reloaded_sessions.revoke(created.token), "session revoke");
    expect(!reloaded_sessions.find(created.token).has_value(), "session revoked lookup");
}


void scene_ticket_test() {
    const std::string secret = "unit-test-scene-ticket-secret-0123456789abcdef";
    const auto issued = opengenesis::security::issue_scene_ticket(
        secret, "user-1", "Test Avatar", "region-1", std::chrono::seconds{60});
    expect(issued.token.starts_with("ogst1."), "scene ticket prefix");
    const auto claims = opengenesis::security::verify_scene_ticket(secret, issued.token, "region-1");
    expect(claims.has_value(), "scene ticket verify");
    expect(claims->user_id == "user-1" && claims->display_name == "Test Avatar", "scene ticket claims");
    expect(!opengenesis::security::verify_scene_ticket(secret, issued.token, "region-2"), "scene ticket region binding");
    auto tampered = issued.token; tampered.back() = tampered.back() == '0' ? '1' : '0';
    expect(!opengenesis::security::verify_scene_ticket(secret, tampered, "region-1"), "scene ticket signature");
}

void content_test() {
    const auto dir = std::filesystem::temp_directory_path() / "ogl-tests-050-content";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    opengenesis::core::AssetStore assets((dir / "assets.db").string(), dir / "blobs", 1024 * 1024);
    std::string reason;
    const auto asset = assets.create("user-1", "hello.txt", "text/plain", "hello OpenGenesis", reason);
    expect(asset.has_value(), "asset create");
    expect(asset->size == 17 && assets.read(asset->id, "user-1").value_or("") == "hello OpenGenesis", "asset read");
    expect(!assets.read(asset->id, "user-2"), "asset ownership");
    opengenesis::core::AssetStore reloaded_assets((dir / "assets.db").string(), dir / "blobs", 1024 * 1024);
    expect(reloaded_assets.find(asset->id).has_value(), "asset metadata persistence");

    opengenesis::core::InventoryStore inventory((dir / "inventory.db").string());
    const auto root_folder = inventory.ensure_root("user-1");
    const auto folder = inventory.create_folder("user-1", root_folder.id, "Objects", reason);
    expect(folder.has_value(), "inventory folder create");
    const auto item = inventory.create_item("user-1", folder->id, asset->id, "Hello Asset", reason);
    expect(item.has_value(), "inventory item create");
    opengenesis::core::InventoryStore reloaded_inventory((dir / "inventory.db").string());
    const auto listed = reloaded_inventory.list("user-1");
    expect(listed.folders.size() == 1 && listed.items.size() == 1, "inventory persistence");
    expect(listed.items[0].asset_id == asset->id, "inventory asset reference");
}

void persistence_test() {
    const auto dir = std::filesystem::temp_directory_path() / "ogl-tests-040-region";
    std::filesystem::remove_all(dir);
    opengenesis::world::RegionRuntime source("persisted", 30.0, 21.0);
    opengenesis::world::Transform transform;
    transform.position = {42.0, 43.0, 44.0};
    transform.rotation = {0.1, 0.2, 0.3};
    transform.scale = {2.0, 3.0, 4.0};
    const auto id = source.spawn_object("Persistent Cube", transform, false, "user-owner");
    expect(source.set_terrain_height(5, 6, 27.5), "terrain persistent edit");
    opengenesis::world::RegionPersistence store(dir);
    store.save(source, true);

    opengenesis::world::RegionRuntime restored("persisted", 30.0, 21.0);
    opengenesis::world::RegionPersistence loader(dir);
    loader.load(restored);
    const auto object = restored.entity(id);
    expect(object.has_value() && object->name == "Persistent Cube", "scene object restore");
    expect(object->owner_user_id == "user-owner", "scene owner restore");
    expect(std::abs(object->transform.position.z - 44.0) < 0.001, "scene transform restore");
    expect(std::abs(restored.terrain().height_at(5, 6) - 27.5) < 0.001, "terrain restore");
    expect(restored.terrain().revision() >= 2, "terrain revision restore");
}

void terrain_test() {
    opengenesis::world::Terrain terrain(8, 8, 1.0, 21.0);
    expect(std::abs(terrain.sample(3.5, 3.5) - 21.0) < 0.001, "terrain base height");
    const auto revision = terrain.revision();
    expect(terrain.set_height(3, 3, 25.0), "terrain set height");
    expect(terrain.revision() == revision + 1, "terrain revision");
    expect(terrain.sample(3.0, 3.0) == 25.0, "terrain sample edited height");
}

void physics_test() {
    opengenesis::physics::PhysicsWorld physics;
    physics.set_ground_sampler([](double, double) { return 5.0; });
    const auto id = physics.add_body({.position = {0, 0, 10}, .velocity = {0, 0, 0}, .mass = 1,
                                      .radius = 0.5});
    physics.step(0.1);
    expect(physics.body(id).position.z < 10.0, "gravity integration");
    for (int i = 0; i < 200; ++i) physics.step(0.05);
    expect(physics.body(id).position.z >= 5.5, "terrain ground contact");
    expect(physics.set_body_position(id, {2, 3, 12}), "set body position");
    expect(physics.set_body_velocity(id, {1, 0, 0}), "set body velocity");
}

void runtime_test() {
    opengenesis::world::RegionRuntime runtime("test", 60.0, 21.0);
    opengenesis::world::Transform avatar_transform;
    avatar_transform.position = {128, 128, 23};
    const auto avatar = runtime.spawn_avatar("user-1", "avatar", avatar_transform);
    const auto object = runtime.spawn_object("cube", {}, true, "user-1");
    runtime.start();
    std::this_thread::sleep_for(std::chrono::milliseconds{180});

    const auto before = runtime.latest_sequence();
    auto object_entity = runtime.entity(object);
    expect(object_entity.has_value(), "object lookup");
    object_entity->transform.position = {10, 20, 30};
    expect(runtime.update_transform(object, object_entity->transform), "transform update");
    expect(runtime.set_velocity(object, {1, 0, 0}), "velocity update");
    const auto chat_sequence = runtime.chat(avatar, "hello scene");
    expect(chat_sequence > before, "chat event");

    std::this_thread::sleep_for(std::chrono::milliseconds{120});
    runtime.stop();
    const auto metrics = runtime.metrics();
    expect(metrics.ticks > 5, "runtime ticks");
    expect(metrics.entities == 2 && metrics.avatars == 1 && metrics.physics_bodies == 2,
           "runtime metrics");
    expect(metrics.scene_events >= 4, "scene event metrics");
    expect(metrics.terrain_revision == 1, "terrain revision metrics");
    const auto events = runtime.events_since(before);
    expect(!events.empty(), "scene events available");
    expect(runtime.remove_entity(object), "object removal");
}
} // namespace

int main() {
    try {
        config_test();
        frame_test();
        registry_test();
        identity_test();
        scene_ticket_test();
        content_test();
        persistence_test();
        terrain_test();
        physics_test();
        runtime_test();
        std::cout << "OpenGenesisLINK 0.5.0 foundation tests: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
