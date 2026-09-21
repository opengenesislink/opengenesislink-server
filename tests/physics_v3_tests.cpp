#include "opengenesis/physics/physics_world.hpp"

#include <algorithm>
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

void shape_and_raycast_test() {
    using namespace opengenesis::physics;

    PhysicsWorld world;
    world.set_gravity({0.0, 0.0, 0.0});
    world.set_ground_height(-100.0);

    const auto static_box = world.add_body(
        {.position = {5.0, 0.0, 5.0},
         .half_extents = {1.0, 1.0, 1.0},
         .shape = CollisionShape::box,
         .dynamic = false});
    const auto moving_box = world.add_body(
        {.position = {6.5, 0.0, 5.0},
         .half_extents = {1.0, 1.0, 1.0},
         .shape = CollisionShape::box,
         .dynamic = true});

    world.step(1.0 / 60.0);
    const auto contacts = world.collisions();
    expect(
        std::any_of(
            contacts.begin(), contacts.end(),
            [&](const auto& contact) {
                return !contact.ground &&
                       ((contact.body_a == static_box &&
                         contact.body_b == moving_box) ||
                        (contact.body_a == moving_box &&
                         contact.body_b == static_box));
            }),
        "box collision");

    const auto hit = world.raycast(
        {0.0, 0.0, 5.0},
        {1.0, 0.0, 0.0},
        20.0);
    expect(hit && hit->body_id == static_box,
           "box raycast hits static box");
    expect(std::abs(hit->distance - 4.0) < 0.05,
           "box raycast distance");
}

void character_controller_test() {
    using namespace opengenesis::physics;

    PhysicsWorld world;
    world.set_ground_height(0.0);

    const auto character = world.add_body(
        {.position = {0.0, 0.0, 0.9},
         .radius = 0.45,
         .half_extents = {0.45, 0.45, 0.9},
         .capsule_half_height = 0.45,
         .shape = CollisionShape::capsule,
         .character = true});

    expect(
        world.set_character_controller(
            character, 50.0, 0.45, 6.0),
        "character controller config");
    world.step(1.0 / 30.0);
    expect(world.body(character).grounded,
           "capsule character grounded");
    expect(world.character_jump(character),
           "grounded character jumps");
    expect(world.body(character).velocity.z >= 5.9,
           "jump speed applied");
}

void spring_constraint_test() {
    using namespace opengenesis::physics;

    PhysicsWorld world;
    world.set_gravity({0.0, 0.0, 0.0});
    world.set_ground_height(-100.0);

    const auto anchor = world.add_body(
        {.position = {0.0, 0.0, 5.0},
         .shape = CollisionShape::sphere,
         .dynamic = false});
    const auto bob = world.add_body(
        {.position = {6.0, 0.0, 5.0},
         .shape = CollisionShape::sphere,
         .dynamic = true});

    const auto spring = world.add_spring_constraint(
        anchor, bob, 2.0, 25.0, 2.0);
    expect(spring != 0U, "spring created");
    world.step(0.05);
    expect(world.body(bob).velocity.x < 0.0,
           "spring pulls toward rest length");
    expect(world.spring_constraints().size() == 1U,
           "spring introspection");
    expect(world.remove_constraint(spring),
           "spring removed");
}

void shape_parser_test() {
    using namespace opengenesis::physics;

    expect(parse_collision_shape("sphere") ==
               CollisionShape::sphere,
           "parse sphere");
    expect(parse_collision_shape("box") ==
               CollisionShape::box,
           "parse box");
    expect(parse_collision_shape("capsule") ==
               CollisionShape::capsule,
           "parse capsule");
    expect(!parse_collision_shape("triangle_mesh"),
           "mesh narrowphase is not falsely advertised");
}

} // namespace

int main() {
    try {
        shape_and_raycast_test();
        character_controller_test();
        spring_constraint_test();
        shape_parser_test();
        std::cout << "OpenGenesisLINK Physics v3 tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "OpenGenesisLINK Physics v3 test failure: "
                  << error.what() << '\n';
        return 1;
    }
}
