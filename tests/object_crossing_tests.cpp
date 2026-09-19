#include "opengenesis/core/crossing_store.hpp"
#include "opengenesis/core/permissions.hpp"
#include "opengenesis/world/object_transfer.hpp"
#include "opengenesis/world/region_runtime.hpp"

#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void require(const bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

std::int64_t unix_now() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

bool near(const double left, const double right) {
    return std::abs(left - right) < 0.000001;
}

} // namespace

int main() {
    try {
        namespace core = opengenesis::core;
        namespace physics = opengenesis::physics;
        namespace world = opengenesis::world;

        const auto root = std::filesystem::temp_directory_path() /
                          "opengenesis-object-crossing-v3-tests";
        std::filesystem::remove_all(root);
        std::filesystem::create_directories(root);

        world::RegionRuntime source("source-region", 30.0, 21.0);
        world::RegionRuntime destination("destination-region", 30.0, 21.0);

        world::Transform transform;
        transform.position = {12.0, 13.0, 30.0};
        transform.rotation = {5.0, 10.0, 15.0};
        transform.scale = {2.0, 3.0, 4.0};

        const auto object_id = source.spawn_object(
            "Transfer Cube", transform, true, "owner-1", "builders",
            core::perm_copy | core::perm_modify, core::perm_copy);
        require(object_id != 0, "source object created");
        require(source.set_object_owner_permissions(
                    object_id,
                    core::perm_copy | core::perm_modify | core::perm_transfer),
                "source owner permission mask configured");
        require(source.set_velocity(object_id, {4.0, 5.0, 6.0}),
                "source object velocity configured");

        std::string reason;
        const auto package = world::export_object_transfer(
            source, object_id, {0.1, 0.2, 0.3},
            "{\"root\":1,\"parts\":[]}",
            "[{\"script\":\"runtime-state\"}]", reason);
        require(package.has_value() && reason.empty(),
                "object transfer package exported");
        require(package->source_region_id == "source-region" &&
                    package->source_entity_id == object_id &&
                    package->name == "Transfer Cube" &&
                    package->owner_user_id == "owner-1" &&
                    package->physical,
                "object identity and physics state captured");
        require(near(package->velocity.x, 4.0) &&
                    near(package->velocity.y, 5.0) &&
                    near(package->velocity.z, 6.0) &&
                    near(package->angular_velocity.z, 0.3),
                "linear and angular velocity captured");
        require(package->linkset_state.find("parts") != std::string::npos &&
                    package->script_state.find("runtime-state") != std::string::npos,
                "linkset and Script carriers captured");

        const auto imported = world::import_object_transfer(
            destination, *package, physics::Vec3{100.0, 101.0, 40.0}, reason);
        require(imported.has_value() && reason.empty(),
                "object transfer package imported");
        const auto imported_entity = destination.entity(*imported);
        const auto imported_velocity = destination.velocity(*imported);
        require(imported_entity &&
                    imported_entity->name == "Transfer Cube" &&
                    imported_entity->owner_user_id == "owner-1" &&
                    imported_entity->group_id == "builders" &&
                    imported_entity->owner_permissions ==
                        (core::perm_copy | core::perm_modify | core::perm_transfer) &&
                    imported_entity->group_permissions ==
                        (core::perm_copy | core::perm_modify) &&
                    imported_entity->everyone_permissions == core::perm_copy &&
                    near(imported_entity->transform.position.x, 100.0) &&
                    near(imported_entity->transform.position.y, 101.0) &&
                    near(imported_entity->transform.position.z, 40.0) &&
                    imported_entity->physics_body != 0 &&
                    imported_velocity &&
                    near(imported_velocity->x, 4.0) &&
                    near(imported_velocity->y, 5.0) &&
                    near(imported_velocity->z, 6.0),
                "destination restores transform, ownership, permissions and velocity");
        require(source.entity(object_id).has_value(),
                "export/import does not remove source before commit");
        require(destination.remove_entity(*imported),
                "test import cleaned up");

        const auto committed = world::transfer_object(
            source, destination, object_id,
            physics::Vec3{110.0, 111.0, 41.0},
            {0.1, 0.2, 0.3},
            "{\"root\":1}", "[{\"script\":\"runtime-state\"}]", reason);
        require(committed.has_value() && reason.empty(),
                "object transfer commit succeeds");
        require(!source.entity(object_id).has_value(),
                "source object removed only after destination import succeeds");
        require(destination.entity(*committed).has_value(),
                "committed destination object remains present");

        const auto crossing_path = (root / "crossings.db").string();
        std::string crossing_id;
        std::string reservation_token;
        {
            core::CrossingStore crossings(crossing_path);
            const auto crossing = crossings.prepare(
                "user-1", "source-region", "destination-region",
                {.x = 0.5, .y = 128.0, .z = 25.0},
                {.x = 8.0, .y = 1.0, .z = 0.5},
                unix_now() + 60, reason,
                "{\"attachments\":[]}", "[{\"script\":\"state\"}]",
                {.x = 5.0, .y = 10.0, .z = 15.0},
                {.x = 0.1, .y = 0.2, .z = 0.3},
                true, "{\"object\":\"state\"}",
                "{\"linkset\":\"state\"}");
            require(crossing.has_value() &&
                        crossing->state == core::CrossingState::prepared,
                    "Crossing v3 prepared");
            crossing_id = crossing->id;

            const auto reserved = crossings.reserve(
                crossing_id, "user-1", "destination-region", reason);
            require(reserved &&
                        reserved->state == core::CrossingState::reserved &&
                        !reserved->reservation_token.empty() &&
                        reserved->reserved_unix > 0,
                    "Crossing v3 destination reservation created");
            reservation_token = reserved->reservation_token;
        }

        {
            core::CrossingStore crossings(crossing_path);
            const auto restored = crossings.find(crossing_id);
            require(restored &&
                        restored->state == core::CrossingState::reserved &&
                        restored->reservation_token == reservation_token &&
                        restored->physical &&
                        near(restored->rotation.z, 15.0) &&
                        near(restored->angular_velocity.z, 0.3) &&
                        restored->object_state.find("object") != std::string::npos &&
                        restored->linkset_state.find("linkset") != std::string::npos,
                    "Crossing v3 reservation and extended state survive restart");

            require(!crossings.complete(
                        crossing_id, "user-1", "destination-region",
                        reason, "wrong-reservation-token"),
                    "wrong Crossing reservation token rejected");
            require(reason == "crossing-reservation-token-invalid",
                    "wrong reservation token reports explicit reason");

            const auto completed = crossings.complete(
                crossing_id, "user-1", "destination-region",
                reason, reservation_token);
            require(completed &&
                        completed->state == core::CrossingState::completed &&
                        completed->completed_unix > 0,
                    "reserved Crossing commits with matching token");
            require(!crossings.complete(
                        crossing_id, "user-1", "destination-region",
                        reason, reservation_token),
                    "completed Crossing replay rejected");
        }

        {
            core::CrossingStore crossings(crossing_path);
            const auto crossing = crossings.prepare(
                "user-2", "source-region", "destination-region",
                {.x = 255.5, .y = 128.0, .z = 25.0},
                {.x = -2.0, .y = 0.0, .z = 0.0},
                unix_now() + 60, reason);
            require(crossing.has_value(), "rollback Crossing prepared");
            const auto reserved = crossings.reserve(
                crossing->id, "user-2", "destination-region", reason);
            require(reserved.has_value(), "rollback Crossing reserved");
            const auto rolled_back = crossings.rollback(
                crossing->id, "user-2", "destination-import-failed", reason);
            require(rolled_back &&
                        rolled_back->state == core::CrossingState::rolled_back &&
                        rolled_back->rolled_back_unix > 0 &&
                        rolled_back->rollback_reason == "destination-import-failed",
                    "Crossing rollback persisted");
            require(!crossings.complete(
                        crossing->id, "user-2", "destination-region",
                        reason, reserved->reservation_token),
                    "rolled-back Crossing cannot commit");
        }

        {
            core::CrossingStore crossings(crossing_path);
            const auto legacy = crossings.prepare(
                "user-3", "source-region", "destination-region",
                {.x = 0.5, .y = 1.0, .z = 22.0},
                {.x = 1.0, .y = 0.0, .z = 0.0},
                unix_now() + 60, reason);
            require(legacy.has_value(), "legacy-compatible Crossing prepared");
            require(crossings.complete(
                        legacy->id, "user-3", "destination-region", reason)
                        .has_value(),
                    "unreserved legacy completion remains compatible");
        }

        std::filesystem::remove_all(root);
        std::cout << "OpenGenesisLINK 7.0 Object/Crossing transaction tests: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "OpenGenesisLINK 7.0 Object/Crossing test failure: "
                  << error.what() << '\n';
        return 1;
    }
}
