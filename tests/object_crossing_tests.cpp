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
        require(source.set_angular_velocity(source_id, {0.0, 0.0, 12.5}),
                "source angular velocity set");
        require(source.set_floating_text(source_id, "Crossing Runtime v2"),
                "source floating text set");

        const auto snapshot = source.export_object(source_id);
        require(snapshot && snapshot->owner_user_id == "user-1" &&
                    snapshot->group_id == "group-1" &&
                    snapshot->transform.rotation.z == 45.0 &&
                    snapshot->velocity.x == 3.5 &&
                    snapshot->angular_velocity.z == 12.5 &&
                    snapshot->floating_text == "Crossing Runtime v2" &&
                    snapshot->physical,
                "object export captures rich runtime state");

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
                        imported_snapshot->velocity.x == 3.5 &&
                        imported_snapshot->angular_velocity.z == 12.5 &&
                        imported_snapshot->floating_text ==
                            "Crossing Runtime v2",
                    "destination restores linear/angular motion and text");

            require(crossings.record_import(
                        crossing_id, "region-b", destination_id,
                        std::to_string(source_id) + ":" +
                            std::to_string(destination_id),
                        reason),
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

        std::uint64_t linkset_destination_root = 0;
        std::uint64_t linkset_destination_child = 0;
        {
            opengenesis::world::Transform root_transform;
            root_transform.position = {245.0, 64.0, 28.0};
            root_transform.rotation = {0.0, 0.0, 15.0};
            root_transform.scale = {2.0, 2.0, 2.0};
            opengenesis::world::Transform child_transform = root_transform;
            child_transform.position.x += 3.0;
            child_transform.scale = {1.0, 1.0, 1.0};

            const auto link_root = source.spawn_object(
                "Vehicle Root", root_transform, true, "user-1");
            const auto link_child = source.spawn_object(
                "Vehicle Child", child_transform, false, "user-1");
            require(link_root != 0 && link_child != 0,
                    "linkset members created");
            require(source.set_velocity(link_root, {2.0, 0.25, 0.0}),
                    "linkset root linear velocity set");
            require(source.set_angular_velocity(link_root, {0.0, 0.0, 25.0}),
                    "linkset root angular velocity set");
            require(source.set_floating_text(link_root, "Vehicle"),
                    "linkset root text set");
            require(source.set_floating_text(link_child, "Seat"),
                    "linkset child text set");
            require(source.link_objects(link_root, link_child, reason),
                    "native linkset created");

            const auto linkset = source.export_linkset(link_child);
            require(linkset &&
                        linkset->source_root_entity_id == link_root &&
                        linkset->members.size() == 2,
                    "linkset export resolves root from child");

            linkset_destination_root = 0x8000000000005001ULL;
            std::vector<std::pair<std::uint64_t, std::uint64_t>> entity_map;
            require(destination.import_linkset(
                        *linkset, linkset_destination_root,
                        {4.0, 64.0, 28.0}, entity_map, reason),
                    "destination imports complete linkset");
            require(entity_map.size() == 2,
                    "linkset import returns source destination map");
            for (const auto& [source_entity, destination_entity] : entity_map) {
                if (source_entity == link_child) {
                    linkset_destination_child = destination_entity;
                }
            }
            require(linkset_destination_child != 0,
                    "child destination mapping returned");

            const auto imported_root =
                destination.export_object(linkset_destination_root);
            const auto imported_child =
                destination.entity(linkset_destination_child);
            require(imported_root &&
                        imported_root->velocity.x == 2.0 &&
                        imported_root->angular_velocity.z == 25.0 &&
                        imported_root->floating_text == "Vehicle",
                    "linkset root preserves motion and text");
            require(imported_child &&
                        imported_child->parent_entity_id ==
                            linkset_destination_root &&
                        imported_child->link_number >= 2 &&
                        imported_child->floating_text == "Seat" &&
                        imported_child->physics_body == 0,
                    "linkset child preserves topology and non-root physics");

            std::vector<std::pair<std::uint64_t, std::uint64_t>> retry_map;
            require(destination.import_linkset(
                        *linkset, linkset_destination_root,
                        {4.0, 64.0, 28.0}, retry_map, reason),
                    "linkset import retry is idempotent");
            require(retry_map == entity_map,
                    "linkset retry returns stable entity mapping");

            const auto mapping_store_path =
                (root / "linkset-crossings.db").string();
            std::string mapping_crossing_id;
            {
                opengenesis::core::ObjectCrossingStore crossings(
                    mapping_store_path, 2);
                const auto prepared = crossings.prepare(
                    "user-1", "region-a", "region-b", link_root,
                    {.x = 4.0, .y = 64.0, .z = 28.0},
                    unix_now() + 60, reason);
                require(prepared.has_value(),
                        "linkset crossing transaction prepared");
                mapping_crossing_id = prepared->id;
                require(crossings.record_export(
                            prepared->id, "region-a",
                            "linkset-v2-snapshot", reason),
                        "linkset snapshot persisted");
                std::string serialized_map;
                for (std::size_t index = 0; index < entity_map.size(); ++index) {
                    if (index != 0) serialized_map += ",";
                    serialized_map +=
                        std::to_string(entity_map[index].first) + ":" +
                        std::to_string(entity_map[index].second);
                }
                require(crossings.record_import(
                            prepared->id, "region-b",
                            prepared->destination_entity_id,
                            serialized_map, reason),
                        "linkset entity map persisted with import ACK");
                const auto imported_tx = crossings.find(prepared->id);
                require(imported_tx &&
                            imported_tx->entity_map == serialized_map,
                        "linkset entity map visible in transaction");
            }
            {
                opengenesis::core::ObjectCrossingStore crossings(
                    mapping_store_path, 2);
                const auto restored_tx =
                    crossings.find(mapping_crossing_id);
                require(restored_tx && !restored_tx->entity_map.empty(),
                        "linkset entity map survives Core restart");
            }

            const auto expiry_store_path =
                (root / "expired-linkset-crossings.db").string();
            opengenesis::core::ObjectCrossingStore expiring(
                expiry_store_path, 2);
            const auto expiring_tx = expiring.prepare(
                "user-1", "region-a", "region-b", link_root,
                {.x = 4.0, .y = 64.0, .z = 28.0},
                unix_now() + 30, reason);
            require(expiring_tx.has_value(),
                    "expiring linkset transaction prepared");
            require(expiring.record_export(
                        expiring_tx->id, "region-a",
                        "linkset-v2-snapshot", reason),
                    "expiring linkset export persisted");
            require(expiring.maintenance(unix_now() + 31) == 1,
                    "expired exported crossing enters reconciliation");
            const auto expired = expiring.find(expiring_tx->id);
            require(expired &&
                        expired->state ==
                            opengenesis::core::ObjectCrossingState::
                                cleanup_pending,
                    "lost destination import ACK triggers cleanup instead of blind rollback");
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
                        restored->velocity.z == 1.25 &&
                        restored->angular_velocity.z == 12.5 &&
                        restored->floating_text == "Crossing Runtime v2",
                    "scene persistence v5 preserves rich crossed object state");
            const auto restored_child =
                restored_destination.entity(linkset_destination_child);
            require(restored_child &&
                        restored_child->parent_entity_id ==
                            linkset_destination_root &&
                        restored_child->floating_text == "Seat",
                    "scene persistence v5 preserves linkset topology");
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
                        prepared->destination_entity_id,
                        std::to_string(rollback_source_id) + ":" +
                            std::to_string(prepared->destination_entity_id),
                        reason),
                    "rollback import ACK recorded");

            require(source.remove_entity(rollback_source_id),
                    "simulate source removal with lost ACK");

            const auto rolled = crossings.rollback(
                prepared->id, "user-1",
                "operator-cancelled", reason);
            require(rolled &&
                        rolled->state ==
                            opengenesis::core::ObjectCrossingState::
                                cleanup_pending,
                    "rollback after import requires destination cleanup");
            require(!source.entity(rollback_source_id).has_value(),
                    "lost remove ACK simulation leaves source absent");

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
                    "cleanup ACK advances rollback to source restore");

            const auto restore =
                crossings.command_for_region("region-a");
            require(restore &&
                        restore->type ==
                            opengenesis::core::ObjectCrossingCommandType::
                                restore_source,
                    "source receives restore command after destination cleanup");
            const auto repeated = crossings.rollback(
                prepared->id, "user-1",
                "ignored-retry", reason);
            require(repeated &&
                        repeated->state ==
                            opengenesis::core::ObjectCrossingState::
                                restore_pending,
                    "repeated rollback cannot skip source restoration");

            require(source.import_object(
                        *rollback_snapshot, rollback_source_id,
                        rollback_snapshot->transform.position, reason),
                    "source restored from preserved transfer snapshot");
            require(crossings.record_restore(
                        prepared->id, "region-a", reason),
                    "source restoration ACK records rollback");

            const auto restored_source =
                source.export_object(rollback_source_id);
            require(restored_source &&
                        restored_source->owner_user_id == "user-1" &&
                        restored_source->transform.position.x ==
                            rollback_snapshot->transform.position.x,
                    "source object reconstructed after lost remove ACK");

            const auto terminal = crossings.find(prepared->id);
            require(terminal &&
                        terminal->state ==
                            opengenesis::core::ObjectCrossingState::
                                rolled_back &&
                        terminal->rollback_reason ==
                            "operator-cancelled" &&
                        terminal->rolled_back_unix > 0,
                    "rollback terminal state follows source restoration");
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
        std::cout << "OpenGenesisLINK 8.0 Object Runtime/Crossing tests: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "OpenGenesisLINK 8.0 Object Runtime/Crossing test failure: "
                  << error.what() << '\n';
        return 1;
    }
}
