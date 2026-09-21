#pragma once

#include "opengenesis/meshing/genesis_mesher.hpp"
#include "opengenesis/physics/physics_world.hpp"

#include <optional>
#include <string>

namespace opengenesis::physics {

struct PhysicsShapePlan {
    meshing::CollisionRepresentation representation{
        meshing::CollisionRepresentation::primitive_proxy};
    std::optional<CollisionShape> primitive_shape;
    Vec3 half_extents{0.5, 0.5, 0.5};
    double radius{0.5};
    double capsule_half_height{0.0};
    meshing::TriangleMesh source_mesh;
    bool solver_ready{false};
    std::string reason;
};

class PhysicsShapeBuilder final {
public:
    explicit PhysicsShapeBuilder(
        const meshing::GenesisMesher* mesher = nullptr);

    [[nodiscard]] PhysicsShapePlan plan_primitive(
        const meshing::PrimitiveDescriptor& descriptor,
        const meshing::MeshingOptions& options,
        bool dynamic_body) const;

private:
    meshing::GenesisMesher owned_mesher_;
    const meshing::GenesisMesher* mesher_{nullptr};
};

} // namespace opengenesis::physics
