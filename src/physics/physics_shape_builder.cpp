#include "opengenesis/physics/physics_shape_builder.hpp"

#include <algorithm>
#include <cmath>

namespace opengenesis::physics {
namespace {

Vec3 half_extents(
    const meshing::PrimitiveDescriptor& descriptor) {
    return {
        descriptor.size.x * 0.5,
        descriptor.size.y * 0.5,
        descriptor.size.z * 0.5};
}

} // namespace

PhysicsShapeBuilder::PhysicsShapeBuilder(
    const meshing::GenesisMesher* mesher)
    : mesher_(mesher ? mesher : &owned_mesher_) {}

PhysicsShapePlan PhysicsShapeBuilder::plan_primitive(
    const meshing::PrimitiveDescriptor& descriptor,
    const meshing::MeshingOptions& options,
    const bool dynamic_body) const {
    PhysicsShapePlan plan;
    const auto built =
        mesher_->mesh_primitive(descriptor, options);
    if (!built.ok()) {
        plan.reason = built.error;
        return plan;
    }

    plan.source_mesh = built.mesh;
    plan.representation =
        mesher_->recommended_collision_representation(
            descriptor, plan.source_mesh, dynamic_body);
    plan.half_extents = half_extents(descriptor);

    switch (descriptor.kind) {
        case meshing::PrimitiveKind::box:
            plan.primitive_shape = CollisionShape::box;
            plan.radius = std::max(
                {0.01, plan.half_extents.x,
                 plan.half_extents.y, plan.half_extents.z});
            plan.solver_ready = true;
            break;
        case meshing::PrimitiveKind::sphere:
            plan.primitive_shape = CollisionShape::sphere;
            plan.radius = std::max(
                {0.01, plan.half_extents.x,
                 plan.half_extents.y, plan.half_extents.z});
            plan.solver_ready = true;
            break;
        case meshing::PrimitiveKind::capsule: {
            plan.primitive_shape = CollisionShape::capsule;
            plan.radius = std::max(
                0.01,
                std::max(
                    descriptor.size.x,
                    descriptor.size.y) * 0.5);
            plan.capsule_half_height =
                std::max(
                    0.0,
                    descriptor.size.z * 0.5 - plan.radius);
            plan.solver_ready = true;
            break;
        }
        case meshing::PrimitiveKind::cylinder:
            plan.primitive_shape.reset();
            plan.radius = std::max(
                {0.01, plan.half_extents.x,
                 plan.half_extents.y, plan.half_extents.z});
            plan.solver_ready = false;
            plan.reason =
                dynamic_body
                    ? "convex-hull-builder-pending"
                    : "triangle-mesh-narrowphase-pending";
            break;
    }

    return plan;
}

} // namespace opengenesis::physics
