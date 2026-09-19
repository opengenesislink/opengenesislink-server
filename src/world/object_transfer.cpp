#include "opengenesis/world/object_transfer.hpp"

#include <cmath>
#include <utility>

namespace opengenesis::world {
namespace {

bool finite(const physics::Vec3 value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

bool valid_package(const ObjectTransferPackage& package) {
    return !package.source_region_id.empty() &&
           package.source_region_id.size() <= 256U &&
           package.source_entity_id != 0 &&
           package.name.size() <= 256U &&
           package.owner_user_id.size() <= 256U &&
           package.group_id.size() <= 256U &&
           finite(package.transform.position) &&
           finite(package.transform.rotation) &&
           finite(package.transform.scale) &&
           finite(package.velocity) &&
           finite(package.angular_velocity) &&
           package.transform.scale.x >= 0.01 &&
           package.transform.scale.y >= 0.01 &&
           package.transform.scale.z >= 0.01 &&
           package.transform.scale.x <= 256.0 &&
           package.transform.scale.y <= 256.0 &&
           package.transform.scale.z <= 256.0 &&
           package.linkset_state.size() <= 64U * 1024U &&
           package.script_state.size() <= 64U * 1024U;
}

} // namespace

std::optional<ObjectTransferPackage> export_object_transfer(
    const RegionRuntime& source,
    const std::uint64_t entity_id,
    const physics::Vec3 angular_velocity,
    std::string linkset_state,
    std::string script_state,
    std::string& reason) {
    const auto entity = source.entity(entity_id);
    if (!entity || entity->kind != EntityKind::object) {
        reason = "object-not-found";
        return std::nullopt;
    }
    const auto velocity = source.velocity(entity_id);
    if (!velocity) {
        reason = "object-velocity-unavailable";
        return std::nullopt;
    }

    ObjectTransferPackage package{
        .source_region_id = source.id(),
        .source_entity_id = entity->id,
        .name = entity->name,
        .owner_user_id = entity->owner_user_id,
        .group_id = entity->group_id,
        .owner_permissions = entity->owner_permissions,
        .group_permissions = entity->group_permissions,
        .everyone_permissions = entity->everyone_permissions,
        .transform = entity->transform,
        .physical = entity->physics_body != 0,
        .velocity = *velocity,
        .angular_velocity = angular_velocity,
        .linkset_state = std::move(linkset_state),
        .script_state = std::move(script_state)};

    if (!valid_package(package)) {
        reason = "invalid-object-transfer";
        return std::nullopt;
    }
    reason.clear();
    return package;
}

std::optional<std::uint64_t> import_object_transfer(
    RegionRuntime& destination,
    const ObjectTransferPackage& package,
    const std::optional<physics::Vec3> destination_position,
    std::string& reason) {
    if (!valid_package(package) ||
        (destination_position && !finite(*destination_position))) {
        reason = "invalid-object-transfer";
        return std::nullopt;
    }

    auto transform = package.transform;
    if (destination_position) transform.position = *destination_position;

    const auto entity_id = destination.spawn_object(
        package.name, transform, package.physical,
        package.owner_user_id, package.group_id,
        package.group_permissions, package.everyone_permissions);
    if (entity_id == 0) {
        reason = "destination-object-create-failed";
        return std::nullopt;
    }

    if (!destination.set_object_owner_permissions(
            entity_id, package.owner_permissions)) {
        (void)destination.remove_entity(entity_id);
        reason = "destination-permission-restore-failed";
        return std::nullopt;
    }

    if (package.physical &&
        !destination.set_velocity(entity_id, package.velocity)) {
        (void)destination.remove_entity(entity_id);
        reason = "destination-velocity-restore-failed";
        return std::nullopt;
    }

    reason.clear();
    return entity_id;
}

std::optional<std::uint64_t> transfer_object(
    RegionRuntime& source,
    RegionRuntime& destination,
    const std::uint64_t entity_id,
    const std::optional<physics::Vec3> destination_position,
    const physics::Vec3 angular_velocity,
    std::string linkset_state,
    std::string script_state,
    std::string& reason) {
    const auto package = export_object_transfer(
        source, entity_id, angular_velocity,
        std::move(linkset_state), std::move(script_state), reason);
    if (!package) return std::nullopt;

    const auto imported =
        import_object_transfer(destination, *package, destination_position, reason);
    if (!imported) return std::nullopt;

    if (!source.remove_entity(entity_id)) {
        (void)destination.remove_entity(*imported);
        reason = "source-object-commit-failed";
        return std::nullopt;
    }

    reason.clear();
    return imported;
}

} // namespace opengenesis::world
