#include "opengenesis/storage/migrations.hpp"

#include <algorithm>
#include <chrono>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace opengenesis::storage {
namespace {

struct MigrationDefinition {
    std::uint32_t version;
    const char* name;
    std::vector<std::string> statements;
};

std::int64_t unix_now() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

const std::vector<MigrationDefinition>& migrations() {
    static const std::vector<MigrationDefinition> definitions = {
        {
            1U,
            "production-foundation",
            {
                "CREATE TABLE IF NOT EXISTS ogl_users ("
                "id VARCHAR(64) PRIMARY KEY,"
                "username VARCHAR(64) NOT NULL UNIQUE,"
                "display_name VARCHAR(128) NOT NULL,"
                "password_hash VARCHAR(512) NOT NULL,"
                "created_unix BIGINT NOT NULL"
                ")",

                "CREATE TABLE IF NOT EXISTS ogl_auth_sessions ("
                "token_hash VARCHAR(128) PRIMARY KEY,"
                "id VARCHAR(64) NOT NULL UNIQUE,"
                "user_id VARCHAR(64) NOT NULL,"
                "issued_unix BIGINT NOT NULL,"
                "expires_unix BIGINT NOT NULL"
                ")",

                "CREATE INDEX IF NOT EXISTS ogl_auth_sessions_user_idx "
                "ON ogl_auth_sessions(user_id)",
                "CREATE INDEX IF NOT EXISTS ogl_auth_sessions_expiry_idx "
                "ON ogl_auth_sessions(expires_unix)",

                "CREATE TABLE IF NOT EXISTS ogl_world_nodes ("
                "id VARCHAR(128) PRIMARY KEY,"
                "name VARCHAR(256) NOT NULL,"
                "endpoint VARCHAR(512) NOT NULL,"
                "state VARCHAR(32) NOT NULL,"
                "generation BIGINT NOT NULL,"
                "registered_ms BIGINT NOT NULL,"
                "last_seen_ms BIGINT NOT NULL"
                ")",

                "CREATE TABLE IF NOT EXISTS ogl_regions ("
                "id VARCHAR(128) PRIMARY KEY,"
                "name VARCHAR(256) NOT NULL,"
                "node_id VARCHAR(128) NOT NULL,"
                "state VARCHAR(32) NOT NULL,"
                "grid_x BIGINT NOT NULL,"
                "grid_y BIGINT NOT NULL,"
                "node_generation BIGINT NOT NULL,"
                "ticks BIGINT NOT NULL,"
                "entities BIGINT NOT NULL,"
                "avatars BIGINT NOT NULL,"
                "physics_bodies BIGINT NOT NULL,"
                "scene_events BIGINT NOT NULL,"
                "terrain_revision BIGINT NOT NULL,"
                "sim_fps DOUBLE PRECISION NOT NULL,"
                "UNIQUE(grid_x, grid_y)"
                ")",

                "CREATE INDEX IF NOT EXISTS ogl_regions_node_idx "
                "ON ogl_regions(node_id)",

                "CREATE TABLE IF NOT EXISTS ogl_audit_events ("
                "sequence BIGINT PRIMARY KEY,"
                "actor VARCHAR(512) NOT NULL,"
                "action VARCHAR(512) NOT NULL,"
                "target VARCHAR(512) NOT NULL,"
                "detail VARCHAR(1024) NOT NULL,"
                "unix_time BIGINT NOT NULL"
                ")",

                "CREATE INDEX IF NOT EXISTS ogl_audit_time_idx "
                "ON ogl_audit_events(unix_time)",

                "CREATE TABLE IF NOT EXISTS ogl_moderation_bans ("
                "id VARCHAR(64) PRIMARY KEY,"
                "user_id VARCHAR(128) NOT NULL,"
                "scope VARCHAR(32) NOT NULL,"
                "scope_id VARCHAR(128) NOT NULL,"
                "reason VARCHAR(2048) NOT NULL,"
                "created_by VARCHAR(128) NOT NULL,"
                "created_unix BIGINT NOT NULL,"
                "expires_unix BIGINT NOT NULL"
                ")",

                "CREATE INDEX IF NOT EXISTS ogl_moderation_user_idx "
                "ON ogl_moderation_bans(user_id)",
                "CREATE INDEX IF NOT EXISTS ogl_moderation_expiry_idx "
                "ON ogl_moderation_bans(expires_unix)"
            }
        },
        {
            2U,
            "foundation-metadata",
            {
                "CREATE TABLE IF NOT EXISTS ogl_storage_metadata ("
                "name VARCHAR(128) PRIMARY KEY,"
                "value_text VARCHAR(4096) NOT NULL,"
                "updated_unix BIGINT NOT NULL"
                ")",
                "CREATE TABLE IF NOT EXISTS ogl_admin_roles ("
                "user_id VARCHAR(64) PRIMARY KEY,"
                "role_name VARCHAR(64) NOT NULL,"
                "granted_by VARCHAR(64) NOT NULL,"
                "granted_unix BIGINT NOT NULL"
                ")"
            }
        }
    };
    return definitions;
}

std::uint32_t parse_version(
    const std::optional<std::string>& value) {
    if (!value || value->empty()) return 0U;
    const auto parsed = std::stoull(*value);
    if (parsed > UINT32_MAX) {
        throw std::runtime_error("database schema version overflow");
    }
    return static_cast<std::uint32_t>(parsed);
}

} // namespace

MigrationRunner::MigrationRunner(
    std::shared_ptr<DatabasePool> database)
    : database_(std::move(database)) {
    if (!database_) {
        throw std::invalid_argument("database is required");
    }
}

void MigrationRunner::migrate() {
    database_->execute(
        "CREATE TABLE IF NOT EXISTS ogl_schema_migrations ("
        "version BIGINT PRIMARY KEY,"
        "name VARCHAR(256) NOT NULL,"
        "applied_unix BIGINT NOT NULL"
        ")");

    auto current = current_version();
    if (current > latest_version()) {
        throw std::runtime_error(
            "database schema is newer than this server");
    }

    for (const auto& migration : migrations()) {
        if (migration.version <= current) continue;

        std::vector<SqlStatement> statements;
        statements.reserve(migration.statements.size() + 1U);
        for (const auto& sql : migration.statements) {
            statements.push_back({.sql = sql, .parameters = {}});
        }
        statements.push_back({
            .sql =
                "INSERT INTO ogl_schema_migrations"
                "(version,name,applied_unix) VALUES(?,?,?)",
            .parameters = {
                std::to_string(migration.version),
                std::string{migration.name},
                std::to_string(unix_now())}});
        database_->transaction(statements);
        current = migration.version;
    }
}

std::uint32_t MigrationRunner::current_version() const {
    try {
        return parse_version(database_->scalar(
            "SELECT MAX(version) AS version "
            "FROM ogl_schema_migrations"));
    } catch (...) {
        return 0U;
    }
}

std::vector<AppliedMigration> MigrationRunner::applied() const {
    std::vector<AppliedMigration> result;
    const auto rows = database_->query(
        "SELECT version,name,applied_unix "
        "FROM ogl_schema_migrations ORDER BY version");
    result.reserve(rows.size());
    for (const auto& row : rows) {
        const auto version = row.find("version");
        const auto name = row.find("name");
        const auto applied = row.find("applied_unix");
        if (version == row.end() || !version->second ||
            name == row.end() || !name->second ||
            applied == row.end() || !applied->second) {
            continue;
        }
        result.push_back({
            .version = static_cast<std::uint32_t>(
                std::stoul(*version->second)),
            .name = *name->second,
            .applied_unix = std::stoll(*applied->second)});
    }
    return result;
}

std::uint32_t MigrationRunner::latest_version() noexcept {
    return migrations().empty()
               ? 0U
               : migrations().back().version;
}

} // namespace opengenesis::storage
