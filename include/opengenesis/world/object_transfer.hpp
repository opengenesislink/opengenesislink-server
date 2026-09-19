#pragma once

#include "opengenesis/world/region_runtime.hpp"

#include <cstdint>
#include <optional>
#include <string>

namespace opengenesis::world {

struct ObjectTransferPackage {
    std::string source_region_id;
    std::uint64_t source_entity_id{0};
    std::string name;
    std::string owner_user_id;
    std::string group_id;
    core::PermissionMask owner_permissions{core::perm_all};
    core::PermissionMask group_permissions{0};
    core::PermissionMask everyone_permissions{0};
    Transform transform{};
    bool physical{false};
    physics::Vec3 velocity{};
    physics::Vec3 angular_velocity{};
    std::string linkset_state;
    std::string script_state;
};

[[nodiscard]] std::optional<ObjectTransferPackage> export_object_transfer(
    const RegionRuntime& source,
    std::uint64_t entity_id,
    physics::Vec3 angular_velocity,
    std::string linkset_state,
    std::string script_state,
    std::string& reason);

[[nodiscard]] std::optional<std::uint64_t> import_object_transfer(
    RegionRuntime& destination,
    const ObjectTransferPackage& package,
    std::optional<physics::Vec3> destination_position,
    std::string& reason);

[[nodiscard]] std::optional<std::uint64_t> transfer_object(
    RegionRuntime& source,
    RegionRuntime& destination,
    std::uint64_t entity_id,
    std::optional<physics::Vec3> destination_position,
    physics::Vec3 angular_velocity,
    std::string linkset_state,
    std::string script_state,
    std::string& reason);

} // namespace opengenesis::world
