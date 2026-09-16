#pragma once
#include <cstdint>
namespace opengenesis::core {
using PermissionMask = std::uint32_t;
inline constexpr PermissionMask perm_copy = 1U << 0U;
inline constexpr PermissionMask perm_modify = 1U << 1U;
inline constexpr PermissionMask perm_transfer = 1U << 2U;
inline constexpr PermissionMask perm_export = 1U << 3U;
inline constexpr PermissionMask perm_all = perm_copy | perm_modify | perm_transfer | perm_export;
[[nodiscard]] inline bool has_permission(PermissionMask mask, PermissionMask required) { return (mask & required) == required; }
}
