#include "opengenesis/core/audit_store.hpp"
#include "opengenesis/core/admin_role_store.hpp"
#include "opengenesis/core/identity_store.hpp"
#include "opengenesis/core/moderation_store.hpp"
#include "opengenesis/core/region_registry.hpp"
#include "opengenesis/core/session_store.hpp"
#include "opengenesis/core/world_registry.hpp"
#include "opengenesis/storage/database.hpp"
#include "opengenesis/storage/migrations.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void require(const bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

opengenesis::storage::DatabaseConfig config_from_environment(
    const std::string& backend) {
    using opengenesis::storage::DatabaseBackend;
    opengenesis::storage::DatabaseConfig config;
    if (backend == "sqlite") {
        config.backend = DatabaseBackend::sqlite;
        config.sqlite_path =
            (std::filesystem::temp_directory_path() /
             "ogl-production-foundation.sqlite")
                .string();
        std::filesystem::remove(config.sqlite_path);
        config.pool_size = 2;
        return config;
    }

    const auto env = [](const char* name, const char* fallback) {
#ifdef _WIN32
        char* value = nullptr;
        std::size_t value_size = 0U;
        if (_dupenv_s(&value, &value_size, name) == 0 &&
            value != nullptr) {
            const std::string result{
                *value != '\0' ? value : fallback};
            std::free(value);
            return result;
        }
        return std::string{fallback};
#else
        const auto* value = std::getenv(name);
        return std::string{
            value && *value != '\0' ? value : fallback};
#endif
    };
    config.backend =
        backend == "postgresql"
            ? DatabaseBackend::postgresql
            : DatabaseBackend::mariadb;
    config.host = env("OGL_TEST_DB_HOST", "127.0.0.1");
    config.port = static_cast<std::uint16_t>(
        std::stoi(env(
            "OGL_TEST_DB_PORT",
            backend == "postgresql" ? "5432" : "3306")));
    config.database =
        env("OGL_TEST_DB_NAME", "opengenesislink");
    config.user =
        env("OGL_TEST_DB_USER", "opengenesislink");
    config.password =
        env("OGL_TEST_DB_PASSWORD", "opengenesislink-test");
    config.ssl_mode = "disable";
    config.pool_size = 3;
    return config;
}

void exercise_backend(const std::string& backend) {
    using namespace opengenesis;

    auto database = storage::DatabasePool::connect(
        config_from_environment(backend));
    storage::MigrationRunner migrations(database);
    migrations.migrate();
    migrations.migrate();

    require(database->ping(), backend + " ping");
    require(
        migrations.current_version() ==
            storage::MigrationRunner::latest_version(),
        backend + " migration version");
    require(migrations.applied().size() == 2U,
            backend + " migration history");

    database->execute(
        "DELETE FROM ogl_storage_metadata WHERE name=?",
        {std::string{"prepared-test"}});
    const std::string hostile =
        "quote ' question ? semicolon ; comment -- utf8 ä";
    database->execute(
        "INSERT INTO ogl_storage_metadata"
        "(name,value_text,updated_unix) VALUES(?,?,?)",
        {std::string{"prepared-test"}, hostile,
         std::string{"1"}});
    const auto prepared = database->scalar(
        "SELECT value_text FROM ogl_storage_metadata WHERE name=?",
        {std::string{"prepared-test"}});
    require(prepared && *prepared == hostile,
            backend + " prepared parameter fidelity");

    try {
        database->transaction({
            {.sql =
                 "INSERT INTO ogl_storage_metadata"
                 "(name,value_text,updated_unix) VALUES(?,?,?)",
             .parameters = {
                 std::string{"rollback-test"},
                 std::string{"should disappear"},
                 std::string{"1"}}},
            {.sql =
                 "INSERT INTO table_that_does_not_exist(x) VALUES(?)",
             .parameters = {std::string{"x"}}}});
        throw std::runtime_error(
            backend + " transaction unexpectedly succeeded");
    } catch (const std::exception&) {
    }
    require(
        database->query(
            "SELECT name FROM ogl_storage_metadata WHERE name=?",
            {std::string{"rollback-test"}})
            .empty(),
        backend + " transaction rollback");

    database->execute("DELETE FROM ogl_auth_sessions");
    database->execute("DELETE FROM ogl_users");
    database->execute("DELETE FROM ogl_regions");
    database->execute("DELETE FROM ogl_world_nodes");
    database->execute("DELETE FROM ogl_audit_events");
    database->execute("DELETE FROM ogl_moderation_bans");
    database->execute("DELETE FROM ogl_admin_roles");

    core::IdentityStore identities(database);
    std::string reason;
    const auto user = identities.register_user(
        "Foundation.User", "Foundation User",
        "correct horse battery staple", reason);
    require(user.has_value(), backend + " identity create");
    require(
        identities.authenticate(
            "foundation.user",
            "correct horse battery staple")
            .has_value(),
        backend + " identity authenticate");
    require(identities.count() == 1U,
            backend + " identity count");

    core::SessionStore sessions(
        database, std::chrono::seconds{600});
    const auto session = sessions.create(user->id);
    require(
        sessions.find(session.token).has_value(),
        backend + " session lookup");
    require(sessions.active_count() == 1U,
            backend + " session count");

    core::WorldRegistry worlds(database);
    const auto world = worlds.register_or_reconnect(
        "world-a", "World A", "127.0.0.1:19100");
    require(world.generation == 1U,
            backend + " world registration");
    require(worlds.touch("world-a", world.generation),
            backend + " world lease");

    core::RegionRegistry regions(database);
    core::RegionInfo region{
        .id = "region-a",
        .name = "Region A",
        .node_id = world.id,
        .state = "registered",
        .grid_x = 1000,
        .grid_y = 1000,
        .node_generation = world.generation};
    require(regions.register_region(region, reason),
            backend + " region registration");
    require(
        regions.update_state(
            region.id, world.id, world.generation,
            "starting", reason),
        backend + " region starting");
    require(
        regions.update_state(
            region.id, world.id, world.generation,
            "online", reason),
        backend + " region online");
    auto metrics = region;
    metrics.ticks = 42;
    metrics.entities = 7;
    metrics.avatars = 2;
    metrics.physics_bodies = 3;
    metrics.scene_events = 12;
    metrics.terrain_revision = 4;
    metrics.sim_fps = 44.5;
    require(regions.update_metrics(metrics, reason),
            backend + " region metrics");
    const auto raw_region_rows = database->query(
        "SELECT entities,sim_fps FROM ogl_regions WHERE id=?",
        {region.id});
    require(
        raw_region_rows.size() == 1U,
        backend + " raw region row");
    const auto raw_entities =
        raw_region_rows.front().find("entities");
    const auto raw_sim_fps =
        raw_region_rows.front().find("sim_fps");
    require(
        raw_entities != raw_region_rows.front().end() &&
            raw_entities->second &&
            std::stoull(*raw_entities->second) == 7U,
        backend + " raw region entities");
    require(
        raw_sim_fps != raw_region_rows.front().end() &&
            raw_sim_fps->second &&
            std::stod(*raw_sim_fps->second) > 44.0,
        backend + " raw region sim_fps");

    const auto loaded_region = regions.find(region.id);
    require(
        loaded_region.has_value(),
        backend + " region readback");
    require(
        loaded_region->entities == 7U,
        backend + " region entities readback");
    require(
        loaded_region->sim_fps > 44.0,
        backend + " region sim_fps readback");

    core::ModerationStore moderation(database);
    const auto ban = moderation.ban(
        "admin", user->id, "region", region.id,
        "foundation test", 0, reason);
    require(
        ban && moderation.is_banned(user->id, region.id),
        backend + " moderation persistence");

    core::AuditStore audit(database);
    audit.append(
        "admin", "foundation.test",
        user->id, backend);
    require(audit.count() == 1U,
            backend + " audit persistence");
    const auto recent = audit.recent(10U);
    require(
        recent.size() == 1U &&
            recent.front().detail == backend,
        backend + " audit readback");
    core::AdminRoleStore admin_roles(database);
    require(
        admin_roles.grant(
            user->id, "moderator", "bootstrap", reason),
        backend + " admin role grant");
    require(
        admin_roles.has_role(user->id, "moderator") &&
            !admin_roles.has_role(user->id, "operator"),
        backend + " admin role hierarchy");
    require(
        admin_roles.grant(
            user->id, "administrator", "bootstrap", reason),
        backend + " admin role elevate");
    require(
        admin_roles.has_role(user->id, "operator") &&
            admin_roles.has_role(user->id, "administrator"),
        backend + " admin role inheritance");

    core::IdentityStore identities_reloaded(database);
    core::SessionStore sessions_reloaded(
        database, std::chrono::seconds{600});
    core::WorldRegistry worlds_reloaded(database);
    core::RegionRegistry regions_reloaded(database);
    core::ModerationStore moderation_reloaded(database);
    core::AuditStore audit_reloaded(database);
    core::AdminRoleStore admin_roles_reloaded(database);

    require(
        identities_reloaded.find_by_id(user->id).has_value(),
        backend + " identity restart view");
    require(
        sessions_reloaded.find(session.token).has_value(),
        backend + " session restart view");
    require(
        worlds_reloaded.find(world.id).has_value(),
        backend + " world restart view");
    require(
        regions_reloaded.find(region.id).has_value(),
        backend + " region restart view");
    require(
        moderation_reloaded.is_banned(user->id, region.id),
        backend + " moderation restart view");
    require(audit_reloaded.count() == 1U,
            backend + " audit restart view");
    require(
        admin_roles_reloaded.has_role(
            user->id, "administrator"),
        backend + " admin role restart view");

    require(sessions.revoke(session.token),
            backend + " session revoke");
    require(moderation.unban(ban->id),
            backend + " moderation remove");
    require(
        admin_roles.revoke(user->id),
        backend + " admin role revoke");

    const auto health = database->health();
    require(
        health.ready &&
            health.pool_size >= 1U &&
            health.successful_operations > 0U,
        backend + " health metrics");
}

} // namespace

int main(int argc, char** argv) {
    try {
        const std::string backend =
            argc > 1 ? argv[1] : "sqlite";
        if (backend != "sqlite" &&
            backend != "postgresql" &&
            backend != "mariadb") {
            throw std::runtime_error(
                "usage: ogl-database-foundation-tests "
                "[sqlite|postgresql|mariadb]");
        }
        exercise_backend(backend);
        std::cout
            << "OpenGenesisLINK 10.0 database foundation "
            << backend << ": PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
