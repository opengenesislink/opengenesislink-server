#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace opengenesis::meshing {

struct Vec3 {
    double x{0.0};
    double y{0.0};
    double z{0.0};
};

struct Vertex {
    Vec3 position{};
    Vec3 normal{};
    double u{0.0};
    double v{0.0};
};

struct Bounds {
    Vec3 minimum{};
    Vec3 maximum{};
};

enum class PrimitiveKind {
    box,
    sphere,
    cylinder,
    capsule
};

enum class CollisionRepresentation {
    primitive_proxy,
    convex_hull,
    triangle_mesh
};

struct PrimitiveDescriptor {
    PrimitiveKind kind{PrimitiveKind::box};
    Vec3 size{1.0, 1.0, 1.0};
};

struct MeshingOptions {
    std::uint32_t radial_segments{24};
    std::uint32_t vertical_segments{12};
    std::size_t max_triangles{8192};
    bool generate_uvs{true};
};

struct TriangleMesh {
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;
    Bounds bounds{};
    std::string cache_key;

    [[nodiscard]] std::size_t triangle_count() const noexcept {
        return indices.size() / 3U;
    }

    [[nodiscard]] bool empty() const noexcept {
        return vertices.empty() || indices.empty();
    }
};

struct MeshBuildResult {
    TriangleMesh mesh;
    std::string error;

    [[nodiscard]] bool ok() const noexcept {
        return error.empty();
    }
};

class GenesisMesher final {
public:
    [[nodiscard]] MeshBuildResult mesh_primitive(
        const PrimitiveDescriptor& descriptor,
        MeshingOptions options = {}) const;

    [[nodiscard]] bool validate(const TriangleMesh& mesh,
                                std::string* error = nullptr) const;

    [[nodiscard]] std::string cache_key(
        const PrimitiveDescriptor& descriptor,
        const MeshingOptions& options) const;

    [[nodiscard]] CollisionRepresentation
    recommended_collision_representation(
        const PrimitiveDescriptor& descriptor,
        const TriangleMesh& mesh,
        bool dynamic_body) const noexcept;
};

[[nodiscard]] const char* primitive_kind_name(PrimitiveKind kind) noexcept;
[[nodiscard]] const char* collision_representation_name(
    CollisionRepresentation representation) noexcept;

} // namespace opengenesis::meshing
