#include "opengenesis/config/toml_config.hpp"
#include "opengenesis/core/asset_store.hpp"
#include "opengenesis/core/audit_store.hpp"
#include "opengenesis/core/admin_role_store.hpp"
#include "opengenesis/core/group_store.hpp"
#include "opengenesis/core/group_channel_store.hpp"
#include "opengenesis/core/notification_store.hpp"
#include "opengenesis/core/landmark_store.hpp"
#include "opengenesis/core/estate_store.hpp"
#include "opengenesis/core/moderation_store.hpp"
#include "opengenesis/core/parcel_store.hpp"
#include "opengenesis/core/permissions.hpp"
#include "opengenesis/core/identity_store.hpp"
#include "opengenesis/core/friends_store.hpp"
#include "opengenesis/core/message_store.hpp"
#include "opengenesis/core/presence_store.hpp"
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
    expect(regions.register_region({.id = "r2",
                                    .name = "East",
                                    .node_id = "w1",
                                    .grid_x = 2,
                                    .grid_y = 1,
                                    .node_generation = 2},
                                   reason),
           "neighbor region register");
    const auto neighbors = regions.neighbors("r1");
    expect(neighbors.size() == 1 && neighbors[0].id == "r2", "region adjacency");
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
    const auto roles_path = (dir / "admin-roles.db").string();
    opengenesis::core::AdminRoleStore roles(roles_path);
    expect(
        roles.grant(user->id, "moderator", "bootstrap", reason),
        "admin role grant");
    expect(
        roles.has_role(user->id, "moderator") &&
            !roles.has_role(user->id, "operator"),
        "admin role hierarchy");
    opengenesis::core::AdminRoleStore reloaded_roles(roles_path);
    expect(
        reloaded_roles.has_role(user->id, "moderator"),
        "admin role file persistence");
    expect(reloaded_roles.revoke(user->id), "admin role revoke");
}


void scene_ticket_test() {
    const std::string secret = "unit-test-scene-ticket-secret-0123456789abcdef";
    const auto issued = opengenesis::security::issue_scene_ticket(
        secret, "user-1", "Test Avatar", "region-1", std::chrono::seconds{60},
        "scene.join,scene.read,scene.move", "region-0", "group-a,group-b", 12.0, 34.0, 22.0);
    expect(issued.token.starts_with("ogst1."), "scene ticket prefix");
    const auto claims = opengenesis::security::verify_scene_ticket(secret, issued.token, "region-1");
    expect(claims.has_value(), "scene ticket verify");
    expect(claims->user_id == "user-1" && claims->display_name == "Test Avatar", "scene ticket claims");
    expect(claims->handoff_from_region == "region-0", "scene handoff claim");
    expect(opengenesis::security::scene_ticket_has_group(*claims, "group-b"), "scene group claim");
    expect(std::abs(claims->spawn_x - 12.0) < 0.001 && std::abs(claims->spawn_y - 34.0) < 0.001, "scene spawn claim");
    expect(opengenesis::security::has_scene_capability(*claims, "scene.move"), "scene capability present");
    expect(!opengenesis::security::has_scene_capability(*claims, "scene.chat"), "scene capability absent");
    expect(!opengenesis::security::verify_scene_ticket(secret, issued.token, "region-2"), "scene ticket region binding");
    auto tampered = issued.token; tampered.back() = tampered.back() == '0' ? '1' : '0';
    expect(!opengenesis::security::verify_scene_ticket(secret, tampered, "region-1"), "scene ticket signature");
}

void social_test() {
    const auto dir = std::filesystem::temp_directory_path() / "ogl-tests-100-social";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);

    std::string reason;
    opengenesis::core::FriendsStore friends((dir / "friends.db").string());
    const auto pending = friends.request("user-a", "user-b", reason);
    expect(pending.has_value() && pending->status == "pending", "friend request");
    expect(!friends.are_friends("user-a", "user-b"), "pending is not accepted");
    const auto accepted = friends.accept("user-b", "user-a", reason);
    expect(accepted.has_value() && friends.are_friends("user-a", "user-b"), "friend accept");
    opengenesis::core::FriendsStore reloaded_friends((dir / "friends.db").string());
    expect(reloaded_friends.are_friends("user-a", "user-b"), "friend persistence");

    opengenesis::core::MessageStore messages((dir / "messages.db").string());
    const auto message = messages.send("user-a", "user-b", "hello social world", reason);
    expect(message.has_value(), "direct message send");
    expect(messages.unread_count("user-b") == 1, "unread message count");
    expect(messages.mark_read("user-b", message->id), "mark message read");
    opengenesis::core::MessageStore reloaded_messages((dir / "messages.db").string());
    expect(reloaded_messages.list_for_user("user-a").size() == 1, "message persistence");
    expect(reloaded_messages.unread_count("user-b") == 0, "read state persistence");

    opengenesis::core::PresenceStore presence;
    presence.replace_region_snapshot("r1", "w1", 7,
        {{.user_id = "user-a", .display_name = "User A", .entity_id = 42, .x = 10, .y = 11, .z = 22}});
    expect(presence.count() == 1 && presence.find_user("user-a").has_value(), "presence snapshot");
    presence.mark_node_offline("w1", 7);
    expect(presence.count() == 0, "presence node offline cleanup");
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


void governance_test() {
    const auto dir = std::filesystem::temp_directory_path() / "ogl-tests-150-governance";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    std::string reason;

    opengenesis::core::GroupStore groups((dir / "groups.db").string());
    const auto group = groups.create("owner", "Builders Guild", reason);
    expect(group.has_value(), "group create");
    expect(groups.has_power(group->id, "owner", opengenesis::core::GroupPower::land), "group owner land power");
    const auto member = groups.add_member("owner", group->id, "member", "officer", reason);
    expect(member.has_value() && groups.is_member(group->id, "member"), "group member add");
    expect(groups.has_power(group->id, "member", opengenesis::core::GroupPower::objects), "officer object power");
    expect(groups.set_role("owner", group->id, "member", "member", reason), "group role update");
    opengenesis::core::GroupStore groups_reloaded((dir / "groups.db").string());
    expect(groups_reloaded.is_member(group->id, "member"), "group persistence");

    opengenesis::core::ParcelStore parcels((dir / "parcels.db").string());
    const auto parcel = parcels.create("owner", "region-1", "Workshop", 0, 0, 127, 127, reason);
    expect(parcel.has_value(), "parcel create");
    expect(parcels.update_policy("owner", parcel->id, group->id, false, false, true, true, reason), "parcel policy");
    expect(!parcels.can_enter("region-1", 10, 10, "stranger", {}), "parcel private entry");
    expect(parcels.can_enter("region-1", 10, 10, "member", {group->id}), "parcel group entry");
    expect(parcels.can_build("region-1", 10, 10, "member", {group->id}), "parcel group build");
    expect(parcels.can_terraform("region-1", 10, 10, "member", {group->id}), "parcel group terraform");
    opengenesis::core::ParcelStore parcels_reloaded((dir / "parcels.db").string());
    expect(parcels_reloaded.find(parcel->id).has_value(), "parcel persistence");

    opengenesis::core::AssetStore assets((dir / "assets.db").string(), dir / "blobs", 1024 * 1024);
    const auto asset = assets.create("owner", "transfer.txt", "text/plain", "payload", reason,
                                     opengenesis::core::perm_copy | opengenesis::core::perm_transfer,
                                     opengenesis::core::perm_transfer);
    expect(asset.has_value(), "permission asset create");
    const auto transferred = assets.transfer(asset->id, "owner", "member", true, reason);
    expect(transferred.has_value() && transferred->owner_user_id == "member", "asset copy transfer");
    expect(transferred->permissions == opengenesis::core::perm_transfer, "next owner permission reduction");
    expect(assets.find(asset->id).has_value(), "source asset retained");

    opengenesis::core::ModerationStore moderation((dir / "moderation.db").string());
    const auto ban = moderation.ban("admin", "bad-user", "region", "region-1", "abuse", 0, reason);
    expect(ban.has_value() && moderation.is_banned("bad-user", "region-1"), "region ban");
    expect(!moderation.is_banned("bad-user", "region-2"), "region ban scope");
    opengenesis::core::ModerationStore moderation_reloaded((dir / "moderation.db").string());
    expect(moderation_reloaded.is_banned("bad-user", "region-1"), "ban persistence");
    expect(moderation_reloaded.unban(ban->id), "ban remove");

    opengenesis::core::AuditStore audit((dir / "audit.log").string());
    audit.append("owner", "parcel.create", parcel->id, "region-1");
    audit.append("admin", "moderation.ban", "bad-user", "region-1");
    expect(audit.count() == 2 && audit.recent(1).front().action == "moderation.ban", "audit append");
    opengenesis::core::AuditStore audit_reloaded((dir / "audit.log").string());
    expect(audit_reloaded.count() == 2, "audit persistence");
}


void operations_test() {
    const auto dir = std::filesystem::temp_directory_path() / "ogl-tests-200-operations";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    std::string reason;

    opengenesis::core::EstateStore estates((dir / "estates.db").string());
    const auto estate = estates.create("owner", "Genesis Estate", reason);
    expect(estate.has_value(), "estate create");
    expect(estates.add_manager("owner", estate->id, "manager", reason), "estate manager add");
    expect(estates.can_manage(estate->id, "manager"), "estate manager permission");
    expect(estates.attach_region("manager", estate->id, "region-1", reason), "estate attach region");
    opengenesis::core::RegionEstatePolicy policy{.region_id="region-1",.estate_id=estate->id,
        .public_access=false,.allow_fly=false,.allow_scripts=true,.allow_voice=false,
        .max_agents=42,.maturity=1,.landing_x=64.0,.landing_y=65.0,.landing_z=22.0};
    expect(estates.update_region_policy("manager", policy, reason), "estate policy update");
    opengenesis::core::EstateStore estates_reloaded((dir / "estates.db").string());
    const auto loaded_policy = estates_reloaded.policy_for_region("region-1");
    expect(loaded_policy.has_value() && !loaded_policy->public_access && loaded_policy->max_agents==42,
           "estate policy persistence");

    opengenesis::core::LandmarkStore landmarks((dir / "landmarks.db").string());
    const auto landmark=landmarks.create("owner","Home","region-1",12.0,34.0,56.0,reason);
    expect(landmark.has_value(), "landmark create");
    opengenesis::core::LandmarkStore landmarks_reloaded((dir / "landmarks.db").string());
    expect(landmarks_reloaded.list_for_user("owner").size()==1, "landmark persistence");
    expect(landmarks_reloaded.remove("owner", landmark->id), "landmark remove");

    opengenesis::core::NotificationStore notifications((dir / "notifications.db").string());
    const auto notification=notifications.push("owner","direct_message","Message","hello","msg-1");
    expect(notifications.unread_count("owner")==1, "notification unread");
    expect(notifications.mark_read("owner",notification.id), "notification read");
    opengenesis::core::NotificationStore notifications_reloaded((dir / "notifications.db").string());
    expect(notifications_reloaded.unread_count("owner")==0 && notifications_reloaded.count()==1,
           "notification persistence");

    opengenesis::core::GroupChannelStore channels((dir / "group-channels.db").string());
    const auto chat=channels.send("group-1","owner","chat","","hello group",reason);
    const auto notice=channels.send("group-1","owner","notice","Maintenance","Tonight",reason);
    expect(chat.has_value() && notice.has_value(), "group posts create");
    opengenesis::core::GroupChannelStore channels_reloaded((dir / "group-channels.db").string());
    expect(channels_reloaded.list("group-1").size()==2, "group posts persistence");
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

    opengenesis::physics::PhysicsWorld solver;
    solver.set_gravity({0, 0, 0});
    solver.set_ground_height(-100.0);
    const auto left = solver.add_body({
        .position = {0, 0, 10},
        .velocity = {1, 0, 0},
        .mass = 2.0,
        .restitution = 1.0,
        .friction = 0.0,
        .radius = 1.0});
    const auto right = solver.add_body({
        .position = {1.5, 0, 10},
        .velocity = {-1, 0, 0},
        .mass = 2.0,
        .restitution = 1.0,
        .friction = 0.0,
        .radius = 1.0});
    solver.step(0.01);
    expect(!solver.collisions().empty(), "body collision detected");
    expect(solver.body(left).velocity.x < 0.0 &&
               solver.body(right).velocity.x > 0.0,
           "collision impulse resolves closing velocity");

    expect(solver.set_body_material(left, 4.0, 0.25, 0.8),
           "physics material update");
    expect(solver.set_body_buoyancy(left, 0.5), "body buoyancy update");
    expect(solver.apply_impulse(left, {4.0, 0.0, 0.0}),
           "linear impulse applied");
    expect(solver.apply_angular_impulse(left, {0.0, 0.0, 8.0}),
           "angular impulse applied");
    expect(solver.apply_force(left, {8.0, 0.0, 0.0}),
           "force accumulator applied");
    expect(solver.apply_torque(left, {0.0, 0.0, 4.0}),
           "torque accumulator applied");
    const auto before = solver.body(left);
    solver.step(0.05);
    const auto after = solver.body(left);
    expect(after.angular_velocity.z > before.angular_velocity.z,
           "torque integrates angular velocity");
    expect(after.mass == 4.0 && after.restitution == 0.25 &&
               after.friction == 0.8 && after.buoyancy == 0.5,
           "physics material state retained");

    expect(solver.set_body_position(left, {10.0, 0.0, 10.0}),
           "constraint left position");
    expect(solver.set_body_position(right, {15.0, 0.0, 10.0}),
           "constraint right position");
    expect(solver.set_body_velocity(left, {}) &&
               solver.set_body_velocity(right, {}),
           "constraint bodies stopped");
    const auto constraint =
        solver.add_distance_constraint(left, right, 2.0, 1.0);
    expect(constraint != 0 && solver.constraints().size() == 1,
           "distance constraint created");
    solver.step(0.02);
    const auto constrained_left = solver.body(left);
    const auto constrained_right = solver.body(right);
    expect(std::abs(
               (constrained_right.position.x -
                constrained_left.position.x) -
               2.0) < 0.05,
           "distance constraint solved");
    expect(solver.remove_constraint(constraint),
           "distance constraint removed");
}

void runtime_test() {
    opengenesis::world::RegionRuntime runtime("test", 60.0, 21.0);
    opengenesis::world::Transform avatar_transform;
    avatar_transform.position = {128, 128, 23};
    const auto avatar = runtime.spawn_avatar("user-1", "avatar", avatar_transform);
    const auto object = runtime.spawn_object("cube", {}, true, "user-1", "group-a",
                                             opengenesis::core::perm_modify, 0);
    expect(runtime.set_object_permissions(object, "group-a", opengenesis::core::perm_modify,
                                          opengenesis::core::perm_copy), "object permissions update");
    const auto permission_object = runtime.entity(object);
    expect(permission_object.has_value() && permission_object->group_id == "group-a" &&
               permission_object->group_permissions == opengenesis::core::perm_modify,
           "object permissions state");
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
    auto avatar_entity = runtime.entity(avatar);
    expect(avatar_entity.has_value(), "avatar lookup");
    avatar_entity->transform.position = {300.0, 128.0, 23.0};
    std::string boundary;
    expect(runtime.move_avatar(avatar, avatar_entity->transform, {2.0, 0.0, 0.0}, boundary),
           "avatar movement");
    expect(boundary == "east", "avatar boundary detection");

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

void runtime_physics_v2_test() {
    opengenesis::world::RegionRuntime runtime(
        "physics-v2", 90.0, 0.0, -10.0);
    std::string reason;

    opengenesis::world::Transform root_transform;
    root_transform.position = {20.0, 20.0, 10.0};
    const auto root = runtime.spawn_object(
        "Rigid Root", root_transform, true, "user-physics");
    auto child_transform = root_transform;
    child_transform.position.x = 22.0;
    const auto child = runtime.spawn_object(
        "Rigid Child", child_transform, false, "user-physics");
    expect(runtime.link_objects(root, child, reason),
           "physics v2 linkset created");

    auto rotated = root_transform;
    rotated.rotation.z = 90.0;
    expect(runtime.update_transform(root, rotated),
           "linkset root rotation update");
    const auto child_after_rotation = runtime.entity(child);
    expect(child_after_rotation.has_value() &&
               std::abs(child_after_rotation->transform.position.x - 20.0) <
                   0.01 &&
               std::abs(child_after_rotation->transform.position.y - 22.0) <
                   0.01,
           "root rotation propagates rigid child transform");

    opengenesis::world::Transform left_transform;
    left_transform.position = {40.0, 40.0, 10.0};
    opengenesis::world::Transform right_transform = left_transform;
    right_transform.position.x = 40.6;
    const auto left = runtime.spawn_object(
        "Collider A", left_transform, true, "user-physics");
    const auto right = runtime.spawn_object(
        "Collider B", right_transform, true, "user-physics");

    expect(runtime.set_physics_material(left, 3.0, 0.4, 0.7),
           "runtime physics material");
    expect(runtime.set_buoyancy(left, 0.25),
           "runtime buoyancy");
    expect(runtime.apply_impulse(left, {2.0, 0.0, 0.0}),
           "runtime linear impulse");
    expect(runtime.apply_angular_impulse(left, {0.0, 0.0, 3.0}),
           "runtime angular impulse");
    expect(runtime.apply_force(left, {1.0, 0.0, 0.0}),
           "runtime force");
    expect(runtime.apply_torque(left, {0.0, 0.0, 1.0}),
           "runtime torque");

    const auto body = runtime.physics_body_state(left);
    expect(body.has_value() && body->mass == 3.0 &&
               body->restitution == 0.4 &&
               body->friction == 0.7 &&
               body->buoyancy == 0.25,
           "runtime exposes physics body state");

    const auto before = runtime.latest_sequence();
    runtime.start();
    std::this_thread::sleep_for(std::chrono::milliseconds{120});

    const auto collision_events = runtime.events_since(before);
    expect(std::any_of(
               collision_events.begin(), collision_events.end(),
               [&](const auto& event) {
                   return event.type == "collision_start" &&
                          (event.entity_id == left ||
                           event.entity_id == right);
               }),
           "runtime emits collision_start events");

    auto left_entity = runtime.entity(left);
    auto right_entity = runtime.entity(right);
    expect(left_entity && right_entity, "constraint entities available");
    auto left_moved = left_entity->transform;
    auto right_moved = right_entity->transform;
    left_moved.position = {60.0, 60.0, 10.0};
    right_moved.position = {65.0, 60.0, 10.0};
    expect(runtime.update_transform(left, left_moved) &&
               runtime.update_transform(right, right_moved),
           "constraint entities repositioned");
    expect(runtime.set_velocity(left, {}) &&
               runtime.set_velocity(right, {}),
           "constraint entities stopped");
    const auto constraint = runtime.constrain_distance(
        left, right, 2.0, 1.0, reason);
    expect(constraint != 0, "runtime distance constraint created");

    std::this_thread::sleep_for(std::chrono::milliseconds{80});
    runtime.stop();
    const auto constrained_left = runtime.entity(left);
    const auto constrained_right = runtime.entity(right);
    expect(constrained_left && constrained_right &&
               std::abs(
                   (constrained_right->transform.position.x -
                    constrained_left->transform.position.x) -
                   2.0) < 0.15,
           "runtime distance constraint affects scene transforms");
    expect(runtime.metrics().physics_constraints == 1,
           "constraint metric exposed");
    expect(runtime.remove_constraint(constraint),
           "runtime distance constraint removed");
}
} // namespace

int main() {
    try {
        config_test();
        frame_test();
        registry_test();
        identity_test();
        scene_ticket_test();
        social_test();
        governance_test();
        operations_test();
        content_test();
        persistence_test();
        terrain_test();
        physics_test();
        runtime_test();
        runtime_physics_v2_test();
        std::cout << "OpenGenesisLINK 12.0.0 foundation tests: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
