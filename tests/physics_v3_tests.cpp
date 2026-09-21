#include "opengenesis/physics/physics_world.hpp"
#include "opengenesis/world/region_runtime.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <chrono>

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


void region_runtime_v3_test() {
    using opengenesis::physics::CollisionShape;
    using opengenesis::world::RegionRuntime;
    using opengenesis::world::Transform;

    RegionRuntime runtime("physics-v3-test", 60.0, 0.0, -10.0);

    Transform object_transform;
    object_transform.position = {20.0, 20.0, 10.0};
    object_transform.scale = {2.0, 4.0, 6.0};
    const auto object = runtime.spawn_object(
        "Physics v3 box", object_transform, true, "owner-v3");
    const auto object_body = runtime.physics_body_state(object);
    expect(object_body &&
               object_body->shape == CollisionShape::box,
           "runtime object defaults to scale-aware box");
    expect(std::abs(object_body->half_extents.x - 1.0) < 1e-9 &&
               std::abs(object_body->half_extents.y - 2.0) < 1e-9 &&
               std::abs(object_body->half_extents.z - 3.0) < 1e-9,
           "runtime object box extents follow scale");

    expect(runtime.set_physics_shape(
               object, CollisionShape::capsule),
           "runtime changes object collision shape");
    const auto capsule = runtime.physics_body_state(object);
    expect(capsule && capsule->shape == CollisionShape::capsule,
           "runtime exposes capsule shape");

    const auto hit = runtime.raycast(
        {20.0, 20.0, 20.0}, {0.0, 0.0, -1.0}, 30.0);
    expect(hit && hit->entity_id == object,
           "runtime raycast maps body back to entity");

    Transform avatar_transform;
    avatar_transform.position = {30.0, 30.0, 0.9};
    const auto avatar = runtime.spawn_avatar(
        "avatar-v3", "Avatar V3", avatar_transform);
    const auto avatar_body = runtime.physics_body_state(avatar);
    expect(avatar_body &&
               avatar_body->shape == CollisionShape::capsule &&
               avatar_body->character,
           "runtime avatar uses capsule character body");
    expect(runtime.configure_character(
               avatar, 45.0, 0.4, 6.5),
           "runtime character config");
    runtime.start();
    for (int i = 0; i < 20; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    runtime.stop();
    expect(runtime.avatar_jump(avatar),
           "runtime grounded avatar jump");

    Transform second_transform;
    second_transform.position = {24.0, 20.0, 10.0};
    const auto second = runtime.spawn_object(
        "Spring target", second_transform, true, "owner-v3");
    std::string reason;
    const auto spring = runtime.constrain_spring(
        object, second, 2.0, 20.0, 2.0, reason);
    expect(spring != 0U && reason.empty(),
           "runtime spring constraint");
    expect(runtime.metrics().physics_constraints >= 1U,
           "runtime reports spring constraint");
    expect(runtime.remove_constraint(spring),
           "runtime removes spring");
}

} // namespace

int main() {
    try {
        shape_and_raycast_test();
        character_controller_test();
        spring_constraint_test();
        shape_parser_test();
        region_runtime_v3_test();
        std::cout << "OpenGenesisLINK Physics v3 tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "OpenGenesisLINK Physics v3 test failure: "
                  << error.what() << '\n';
        return 1;
    }
}
