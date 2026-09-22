#include "opengenesis/physics/physics_shape_builder.hpp"

#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void expect(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

} // namespace

int main() {
    try {
        using namespace opengenesis;

        physics::PhysicsShapeBuilder builder;
        meshing::MeshingOptions options;

        const auto box = builder.plan_primitive(
            {.kind = meshing::PrimitiveKind::box,
             .size = {2.0, 4.0, 6.0}},
            options, true);
        expect(box.solver_ready, "box solver ready");
        expect(box.primitive_shape ==
                   physics::CollisionShape::box,
               "box maps to native collider");
        expect(box.source_mesh.triangle_count() == 12U,
               "box retains mesher output");

        const auto capsule = builder.plan_primitive(
            {.kind = meshing::PrimitiveKind::capsule,
             .size = {1.0, 1.0, 3.0}},
            options, true);
        expect(capsule.solver_ready, "capsule solver ready");
        expect(capsule.primitive_shape ==
                   physics::CollisionShape::capsule,
               "capsule maps to native collider");
        expect(capsule.capsule_half_height > 0.0,
               "capsule half-height derived");

        const auto dynamic_cylinder = builder.plan_primitive(
            {.kind = meshing::PrimitiveKind::cylinder,
             .size = {2.0, 2.0, 4.0}},
            options, true);
        expect(!dynamic_cylinder.solver_ready,
               "dynamic cylinder does not fake convex support");
        expect(dynamic_cylinder.representation ==
                   meshing::CollisionRepresentation::convex_hull,
               "dynamic cylinder requests convex path");

        const auto static_cylinder = builder.plan_primitive(
            {.kind = meshing::PrimitiveKind::cylinder,
             .size = {2.0, 2.0, 4.0}},
            options, false);
        expect(!static_cylinder.solver_ready,
               "static cylinder waits for mesh narrowphase");
        expect(static_cylinder.representation ==
                   meshing::CollisionRepresentation::triangle_mesh,
               "static cylinder requests triangle mesh path");

        std::cout
            << "OpenGenesisLINK PhysicsShapeBuilder tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr
            << "OpenGenesisLINK PhysicsShapeBuilder test failure: "
            << error.what() << '\n';
        return 1;
    }
}
