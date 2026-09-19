#include "opengenesis/core/object_crossing_store.hpp"
#include "opengenesis/world/region_runtime.hpp"
#include "opengenesis/world/region_persistence.hpp"

#include <chrono>
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

std::filesystem::path temp_root() {
    const auto stamp =
        std::chrono::steady_clock::now().time_since_epoch().count();
    const auto path = std::filesystem::temp_directory_path() /
                      ("ogl-object-crossing-" + std::to_string(stamp));
    std::filesystem::create_directories(path);
    return path;
}

} // namespace

int main() {
    try {
        const auto root = temp_root();
        std::string reason;

        opengenesis::world::RegionRuntime source("region-a", 30.0, 21.0);
        opengenesis::world::RegionRuntime destination("region-b", 30.0, 21.0);

        opengenesis::world::Transform transform;
        transform.position = {250.0, 128.0, 30.0};
        transform.rotation = {0.0, 0.0, 45.0};
        transform.scale = {2.0, 3.0, 4.0};

        const auto source_id = source.spawn_object(
            "Crossing Crate", transform, true, "user-1", "group-1",
            opengenesis::core::perm_copy | opengenesis::core::perm_modify,
            opengenesis::core::perm_copy);
        require(source_id != 0, "source object created");
        require(source.set_velocity(source_id, {3.5, 0.5, 1.25}),
                "source velocity set");

        const auto snapshot = source.export_object(source_id);
        require(snapshot && snapshot->owner_user_id == "user-1" &&
                    snapshot->group_id == "group-1" &&
                    snapshot->transform.rotation.z == 45.0 &&
                    snapshot->velocity.x == 3.5 &&
                    snapshot->physical,
                "object export captures transform permissions and velocity");

        const auto store_path = (root / "object-crossings.db").string();
        std::string crossing_id;
        std::uint64_t destination_id = 0;
        {
            opengenesis::core::ObjectCrossingStore crossings(store_path, 2);
            const auto prepared = crossings.prepare(
                "user-1", "region-a", "region-b", source_id,
                {.x = 1.0, .y = 128.0, .z = 0.0},
                unix_now() + 60, reason);
            require(prepared.has_value(), "object crossing prepared");
            crossing_id = prepared->id;
            destination_id = prepared->destination_entity_id;
            require((destination_id & 0x8000000000000000ULL) != 0,
                    "destination id uses transfer namespace");

            const auto export_command =
                crossings.command_for_region("region-a");
            require(export_command &&
                        export_command->type ==
                            opengenesis::core::ObjectCrossingCommandType::
                                export_source &&
                        export_command->crossing.id == crossing_id,
                    "source receives export command");

            require(crossings.record_export(
                        crossing_id, "region-a", "snapshot-v1", reason),
                    "source export ACK stored");
            require(source.entity(source_id).has_value(),
                    "source object remains before destination import ACK");

            const auto import_command =
                crossings.command_for_region("region-b");
            require(import_command &&
                        import_command->type ==
                            opengenesis::core::ObjectCrossingCommandType::
                                import_destination,
                    "destination receives import command");

            require(destination.import_object(
                        *snapshot, destination_id,
                        {1.0, 128.0, 0.0}, reason),
                    "destination imports object");
            require(destination.import_object(
                        *snapshot, destination_id,
                        {1.0, 128.0, 0.0}, reason),
                    "destination import retry is idempotent");

            const auto imported = destination.entity(destination_id);
            require(imported &&
                        imported->owner_user_id == "user-1" &&
                        imported->group_id == "group-1" &&
                        imported->transform.position.x == 1.0 &&
                        imported->transform.scale.z == 4.0,
                    "destination object preserves ownership and transform");
            const auto imported_snapshot =
                destination.export_object(destination_id);
            require(imported_snapshot &&
                        imported_snapshot->velocity.x == 3.5,
                    "destination restores physical velocity");

            require(crossings.record_import(
                        crossing_id, "region-b", destination_id, reason),
                    "destination import ACK stored");
            require(source.entity(source_id).has_value(),
                    "source still exists until destination ACK transition");

            const auto remove_command =
                crossings.command_for_region("region-a");
            require(remove_command &&
                        remove_command->type ==
                            opengenesis::core::ObjectCrossingCommandType::
                                remove_source,
                    "source removal only commanded after import ACK");
            require(source.remove_entity(source_id),
                    "source object removed after import ACK");
            require(crossings.record_remove(
                        crossing_id, "region-a", reason),
                    "source removal ACK completes crossing");
            const auto completed = crossings.find(crossing_id);
            require(completed &&
                        completed->state ==
                            opengenesis::core::ObjectCrossingState::completed &&
                        completed->completed_unix > 0,
                    "object crossing completed");
        }

        {
            opengenesis::core::ObjectCrossingStore crossings(store_path, 2);
            const auto completed = crossings.find(crossing_id);
            require(completed &&
                        completed->state ==
                            opengenesis::core::ObjectCrossingState::completed,
                    "completed object crossing survives restart");
        }

        {
            const auto persistence_path = root / "destination-persistence";
            opengenesis::world::RegionPersistence persistence(
                persistence_path);
            persistence.save(destination, true);

            opengenesis::world::RegionRuntime restored_destination(
                "region-b", 30.0, 21.0);
            opengenesis::world::RegionPersistence restored_persistence(
                persistence_path);
            restored_persistence.load(restored_destination);
            const auto restored =
                restored_destination.export_object(destination_id);
            require(restored &&
                        restored->owner_user_id == "user-1" &&
                        restored->group_id == "group-1" &&
                        restored->transform.rotation.z == 45.0 &&
                        restored->velocity.x == 3.5 &&
                        restored->velocity.y == 0.5 &&
                        restored->velocity.z == 1.25,
                    "scene persistence v4 preserves crossed object velocity");
        }

        const auto rollback_source_id = source.spawn_object(
            "Rollback Crate", transform, false, "user-1");
        const auto rollback_snapshot =
            source.export_object(rollback_source_id);
        require(rollback_snapshot.has_value(),
                "rollback source snapshot available");

        {
            opengenesis::core::ObjectCrossingStore crossings(store_path, 2);
            const auto prepared = crossings.prepare(
                "user-1", "region-a", "region-b", rollback_source_id,
                {.x = 1.0, .y = 64.0, .z = 30.0},
                unix_now() + 60, reason);
            require(prepared.has_value(), "rollback crossing prepared");
            require(crossings.record_export(
                        prepared->id, "region-a", "rollback-snapshot", reason),
                    "rollback export recorded");
            require(destination.import_object(
                        *rollback_snapshot, prepared->destination_entity_id,
                        {1.0, 64.0, 30.0}, reason),
                    "rollback destination imported");
            require(crossings.record_import(
                        prepared->id, "region-b",
                        prepared->destination_entity_id, reason),
                    "rollback import ACK recorded");

            const auto rolled = crossings.rollback(
                prepared->id, "user-1",
                "operator-cancelled", reason);
            require(rolled &&
                        rolled->state ==
                            opengenesis::core::ObjectCrossingState::
                                cleanup_pending,
                    "rollback after import requires destination cleanup");
            require(source.entity(rollback_source_id).has_value(),
                    "rollback leaves source object intact");

            const auto cleanup =
                crossings.command_for_region("region-b");
            require(cleanup &&
                        cleanup->type ==
                            opengenesis::core::ObjectCrossingCommandType::
                                cleanup_destination,
                    "destination receives cleanup command");
            require(destination.remove_entity(
                        prepared->destination_entity_id),
                    "destination imported object cleaned");
            require(crossings.record_cleanup(
                        prepared->id, "region-b", reason),
                    "cleanup ACK records rollback");
            const auto terminal = crossings.find(prepared->id);
            require(terminal &&
                        terminal->state ==
                            opengenesis::core::ObjectCrossingState::
                                rolled_back &&
                        terminal->rollback_reason ==
                            "operator-cancelled",
                    "rollback terminal state persisted");
        }

        {
            opengenesis::core::ObjectCrossingStore crossings(store_path, 2);
            const auto prepared = crossings.prepare(
                "user-1", "region-a", "region-b", 999,
                {.x = 1.0, .y = 32.0, .z = 30.0},
                unix_now() + 60, reason);
            require(prepared.has_value(), "failure crossing prepared");
            require(crossings.reject_command(
                        prepared->id,
                        opengenesis::core::ObjectCrossingCommandType::
                            export_source,
                        "source-object-not-found", reason),
                    "first export rejection recorded");
            require(crossings.reject_command(
                        prepared->id,
                        opengenesis::core::ObjectCrossingCommandType::
                            export_source,
                        "source-object-not-found", reason),
                    "second export rejection recorded");
            const auto failed = crossings.find(prepared->id);
            require(failed &&
                        failed->state ==
                            opengenesis::core::ObjectCrossingState::
                                rolled_back &&
                        failed->export_attempts == 2,
                    "bounded export retries roll back safely");
        }

        std::filesystem::remove_all(root);
        std::cout << "OpenGenesisLINK 7.5 Object Crossing tests: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "OpenGenesisLINK 7.5 Object Crossing test failure: "
                  << error.what() << '\n';
        return 1;
    }
}
