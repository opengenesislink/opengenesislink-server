#include "opengenesis/meshing/genesis_mesher.hpp"

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void expect(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void primitive_meshing_test() {
    using namespace opengenesis::meshing;

    GenesisMesher mesher;

    const auto box = mesher.mesh_primitive({
        .kind = PrimitiveKind::box,
        .size = {2.0, 4.0, 6.0}});
    expect(box.ok(), "box mesh builds");
    expect(box.mesh.triangle_count() == 12U,
           "box has 12 triangles");
    expect(std::abs(box.mesh.bounds.minimum.x + 1.0) < 1e-9,
           "box min x bound");
    expect(std::abs(box.mesh.bounds.maximum.z - 3.0) < 1e-9,
           "box max z bound");

    MeshingOptions budget;
    budget.radial_segments = 64;
    budget.vertical_segments = 32;
    budget.max_triangles = 600;

    const auto sphere = mesher.mesh_primitive(
        {.kind = PrimitiveKind::sphere,
         .size = {2.0, 2.0, 2.0}},
        budget);
    expect(sphere.ok(), "sphere mesh builds");
    expect(sphere.mesh.triangle_count() <= budget.max_triangles,
           "sphere obeys triangle budget");

    const auto cylinder = mesher.mesh_primitive(
        {.kind = PrimitiveKind::cylinder,
         .size = {2.0, 4.0, 5.0}});
    expect(cylinder.ok(), "cylinder mesh builds");
    expect(cylinder.mesh.triangle_count() > 12U,
           "cylinder has generated surface");

    const auto capsule = mesher.mesh_primitive(
        {.kind = PrimitiveKind::capsule,
         .size = {1.0, 1.0, 3.0}});
    expect(capsule.ok(), "capsule mesh builds");
    expect(capsule.mesh.bounds.maximum.z > 1.4,
           "capsule extends across requested height");

    const auto invalid = mesher.mesh_primitive(
        {.kind = PrimitiveKind::box,
         .size = {0.0, 1.0, 1.0}});
    expect(!invalid.ok(), "invalid dimensions rejected");
}

void deterministic_contract_test() {
    using namespace opengenesis::meshing;

    GenesisMesher mesher;
    PrimitiveDescriptor descriptor{
        .kind = PrimitiveKind::sphere,
        .size = {1.25, 2.5, 3.75}};
    MeshingOptions options;
    options.radial_segments = 18;
    options.vertical_segments = 9;
    options.max_triangles = 1024;

    const auto first = mesher.mesh_primitive(descriptor, options);
    const auto second = mesher.mesh_primitive(descriptor, options);
    expect(first.ok() && second.ok(),
           "deterministic meshes build");
    expect(first.mesh.cache_key == second.mesh.cache_key,
           "cache key deterministic");
    expect(first.mesh.vertices.size() ==
               second.mesh.vertices.size() &&
           first.mesh.indices == second.mesh.indices,
           "topology deterministic");
}

void collision_policy_test() {
    using namespace opengenesis::meshing;

    GenesisMesher mesher;
    const PrimitiveDescriptor box_descriptor{
        .kind = PrimitiveKind::box,
        .size = {1.0, 1.0, 1.0}};
    const auto box = mesher.mesh_primitive(box_descriptor);
    expect(box.ok(), "collision-policy box builds");
    expect(
        mesher.recommended_collision_representation(
            box_descriptor, box.mesh, true) ==
            CollisionRepresentation::primitive_proxy,
        "native primitive keeps primitive collision proxy");

    const PrimitiveDescriptor cylinder_descriptor{
        .kind = PrimitiveKind::cylinder,
        .size = {1.0, 1.0, 2.0}};
    const auto cylinder =
        mesher.mesh_primitive(cylinder_descriptor);
    expect(cylinder.ok(), "collision-policy cylinder builds");
    expect(
        mesher.recommended_collision_representation(
            cylinder_descriptor, cylinder.mesh, true) ==
            CollisionRepresentation::convex_hull,
        "dynamic complex shape prefers convex hull");
    expect(
        mesher.recommended_collision_representation(
            cylinder_descriptor, cylinder.mesh, false) ==
            CollisionRepresentation::triangle_mesh,
        "static complex shape may use triangle mesh");
}

} // namespace

int main() {
    try {
        primitive_meshing_test();
        deterministic_contract_test();
        collision_policy_test();
        std::cout << "OpenGenesisLINK GenesisMesher tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "OpenGenesisLINK GenesisMesher test failure: "
                  << error.what() << '\n';
        return 1;
    }
}
