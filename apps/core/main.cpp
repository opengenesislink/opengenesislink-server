#include "opengenesis/common/log.hpp"
#include "opengenesis/avatar/appearance_store.hpp"
#include "opengenesis/config/toml_config.hpp"
#include "opengenesis/core/admin_http.hpp"
#include "opengenesis/core/asset_store.hpp"
#include "opengenesis/core/audit_store.hpp"
#include "opengenesis/core/admin_role_store.hpp"
#include "opengenesis/core/group_store.hpp"
#include "opengenesis/core/group_channel_store.hpp"
#include "opengenesis/core/notification_store.hpp"
#include "opengenesis/core/landmark_store.hpp"
#include "opengenesis/core/estate_store.hpp"
#include "opengenesis/core/moderation_store.hpp"
#include "opengenesis/core/parcel_store.hpp"
#include "opengenesis/core/inventory_store.hpp"
#include "opengenesis/core/identity_store.hpp"
#include "opengenesis/core/node_sessions.hpp"
#include "opengenesis/core/presence_store.hpp"
#include "opengenesis/core/friends_store.hpp"
#include "opengenesis/core/message_store.hpp"
#include "opengenesis/core/region_registry.hpp"
#include "opengenesis/core/session_store.hpp"
#include "opengenesis/core/world_registry.hpp"
#include "opengenesis/core/crossing_store.hpp"
#include "opengenesis/core/object_crossing_store.hpp"
#include "opengenesis/scripting/script_runtime.hpp"
#include "opengenesis/scripting/script_host.hpp"
#include "opengenesis/scripting/world_action_queue.hpp"
#include "opengenesis/federation/grid_identity_store.hpp"
#include "opengenesis/federation/runtime.hpp"
#include "opengenesis/federation/session_store.hpp"
#include "opengenesis/federation/service_grant_store.hpp"
#include "opengenesis/federation/trust_store.hpp"
#include "opengenesis/compat/hypergrid/home_verifier.hpp"
#include "opengenesis/compat/hypergrid/friends_adapter.hpp"
#include "opengenesis/compat/hypergrid/asset_adapter.hpp"
#include "opengenesis/compat/hypergrid/im_adapter.hpp"
#include "opengenesis/compat/hypergrid/inventory_adapter.hpp"
#include "opengenesis/compat/hypergrid/appearance_adapter.hpp"
#include "opengenesis/compat/hypergrid/server.hpp"
#include "opengenesis/compat/hypergrid/service.hpp"
#include "opengenesis/compat/hypergrid/session_store.hpp"
#include "opengenesis/network/tcp.hpp"
#include "opengenesis/protocol/frame.hpp"
#include "opengenesis/security/crypto.hpp"
#include "opengenesis/storage/database.hpp"
#include "opengenesis/storage/database_config.hpp"
#include "opengenesis/storage/migrations.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

using opengenesis::common::LogLevel;
namespace protocol = opengenesis::protocol;
namespace core = opengenesis::core;

namespace {
std::atomic_bool running{true};
void signal_handler(int) { running = false; }

std::int64_t unix_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

std::string field(const std::string& payload, const std::string& key) {
    std::istringstream input(payload);
    std::string line;
    while (std::getline(input, line)) {
        const auto split = line.find('=');
        if (split != std::string::npos && line.substr(0, split) == key) return line.substr(split + 1);
    }
    return {};
}

std::uint64_t u64(const std::string& payload, const std::string& key) {
    const auto value = field(payload, key);
    return value.empty() ? 0 : std::stoull(value);
}

double number(const std::string& payload, const std::string& key) {
    const auto value = field(payload, key);
    return value.empty() ? 0.0 : std::stod(value);
}

std::vector<std::string> split_pipe(const std::string& text) {
    std::vector<std::string> result;
    std::size_t start = 0;
    while (true) {
        const auto end = text.find('|', start);
        result.push_back(text.substr(start, end == std::string::npos ? std::string::npos : end - start));
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return result;
}

std::optional<std::vector<std::pair<std::uint64_t, std::uint64_t>>>
parse_entity_map(const std::string_view text) {
    if (text.empty() || text.size() > 16U * 1024U) return std::nullopt;
    std::vector<std::pair<std::uint64_t, std::uint64_t>> result;
    std::size_t start = 0;
    while (start <= text.size()) {
        const auto end = text.find(',', start);
        const auto token = text.substr(
            start, end == std::string_view::npos
                       ? std::string_view::npos
                       : end - start);
        const auto split = token.find(':');
        if (split == std::string_view::npos || split == 0 ||
            split + 1U >= token.size()) {
            return std::nullopt;
        }
        try {
            const auto source =
                std::stoull(std::string{token.substr(0, split)});
            const auto destination =
                std::stoull(std::string{token.substr(split + 1U)});
            if (source == 0 || destination == 0 ||
                std::find_if(
                    result.begin(), result.end(),
                    [&](const auto& item) {
                        return item.first == source ||
                               item.second == destination;
                    }) != result.end()) {
                return std::nullopt;
            }
            result.emplace_back(source, destination);
        } catch (...) {
            return std::nullopt;
        }
        if (result.size() > 64U) return std::nullopt;
        if (end == std::string_view::npos) break;
        start = end + 1U;
    }
    return result.empty()
               ? std::nullopt
               : std::optional<std::vector<
                     std::pair<std::uint64_t, std::uint64_t>>>{
                     std::move(result)};
}

std::vector<core::PresenceInfo> parse_presences(const std::string& payload) {
    std::vector<core::PresenceInfo> result;
    std::istringstream input(payload);
    std::string line;
    while (std::getline(input, line)) {
        if (!line.starts_with("presence=")) continue;
        const auto parts = split_pipe(line.substr(9));
        if (parts.size() != 6) continue;
        try {
            result.push_back({.user_id = parts[0],
                              .display_name = parts[1],
                              .entity_id = std::stoull(parts[2]),
                              .x = std::stod(parts[3]),
                              .y = std::stod(parts[4]),
                              .z = std::stod(parts[5])});
        } catch (...) {
        }
    }
    return result;
}
} // namespace

int main(int argc, char** argv) {
    try {
        std::signal(SIGINT, signal_handler);
        std::signal(SIGTERM, signal_handler);
        const auto config = opengenesis::config::TomlConfig::load_file(
            argc > 1 ? argv[1] : "config/core.toml");
        const auto address = config.get_string("network.listen_address", "0.0.0.0");
        const auto port = static_cast<std::uint16_t>(config.get_int("network.port", 19000));
        const auto lease_timeout = std::chrono::seconds{config.get_int("lease.timeout_seconds", 15)};
        const auto session_lifetime = std::chrono::seconds{
            config.get_int("identity.session_lifetime_seconds", 86400)};
        const auto scene_ticket_lifetime = std::chrono::seconds{
            config.get_int("identity.scene_ticket_lifetime_seconds", 60)};
        const auto login_attempts_per_minute =
            config.get_int("security.login_attempts_per_minute", 12);
        const auto registration_attempts_per_minute =
            config.get_int("security.registration_attempts_per_minute", 30);
        if (login_attempts_per_minute < 1 ||
            login_attempts_per_minute > 1000 ||
            registration_attempts_per_minute < 1 ||
            registration_attempts_per_minute > 1000) {
            throw std::runtime_error("invalid authentication rate-limit configuration");
        }
        const auto production_mode =
            config.get_bool("security.production_mode", false);
        const auto secret_from_env =
            [&](const std::string& value_key,
                const std::string& env_key,
                const std::string& fallback) {
                const auto env_name =
                    config.get_string(env_key, "");
                if (!env_name.empty()) {
#ifdef _WIN32
                    char* value = nullptr;
                    std::size_t value_size = 0U;
                    if (_dupenv_s(
                            &value, &value_size,
                            env_name.c_str()) == 0 &&
                        value != nullptr) {
                        const std::string result{value};
                        std::free(value);
                        if (!result.empty()) return result;
                    }
#else
                    if (const auto* value =
                            std::getenv(env_name.c_str());
                        value && *value != '\0') {
                        return std::string{value};
                    }
#endif
                }
                return config.get_string(value_key, fallback);
            };
        const auto scene_ticket_secret = secret_from_env(
            "security.scene_ticket_secret",
            "security.scene_ticket_secret_env",
            "development-only-change-this-scene-ticket-secret");
        const auto admin_api_key = secret_from_env(
            "security.admin_api_key",
            "security.admin_api_key_env",
            "development-only-change-this-admin-api-key");
        const auto world_node_secret = secret_from_env(
            "security.world_node_secret",
            "security.world_node_secret_env",
            "");
        if (scene_ticket_secret.size() < 32) {
            throw std::runtime_error(
                "security.scene_ticket_secret must contain at least 32 bytes");
        }
        if (admin_api_key.size() < 24) {
            throw std::runtime_error(
                "security.admin_api_key must contain at least 24 bytes");
        }
        if (!world_node_secret.empty() &&
            world_node_secret.size() < 32U) {
            throw std::runtime_error(
                "security.world_node_secret must be empty or contain at least 32 bytes");
        }
        if (production_mode && world_node_secret.size() < 32U) {
            throw std::runtime_error(
                "production_mode requires security.world_node_secret");
        }
        if (production_mode &&
            (scene_ticket_secret ==
                 "development-only-change-this-scene-ticket-secret" ||
             admin_api_key ==
                 "development-only-change-this-admin-api-key")) {
            throw std::runtime_error(
                "production_mode refuses development security secrets");
        }

        std::shared_ptr<opengenesis::storage::DatabasePool> database;
        const auto database_selection =
            opengenesis::storage::database_selection_from_config(
                config, production_mode);
        if (!database_selection.file_mode) {
            database =
                opengenesis::storage::DatabasePool::connect(
                    database_selection.config);
            opengenesis::storage::MigrationRunner migrations(database);
            migrations.migrate();
            if (!database->ping()) {
                throw std::runtime_error(
                    "database health check failed after migration");
            }
            opengenesis::common::log(
                LogLevel::info,
                "core.storage",
                "SQL storage ready: " +
                    database->backend_name() +
                    " schema=" +
                    std::to_string(migrations.current_version()));
        }

        auto worlds = database
            ? std::make_shared<core::WorldRegistry>(database)
            : std::make_shared<core::WorldRegistry>(
                  config.get_string(
                      "storage.worlds", "data/worlds.db"));
        auto regions = database
            ? std::make_shared<core::RegionRegistry>(database)
            : std::make_shared<core::RegionRegistry>(
                  config.get_string(
                      "storage.regions", "data/regions.db"));
        auto identities = database
            ? std::make_shared<core::IdentityStore>(database)
            : std::make_shared<core::IdentityStore>(
                  config.get_string(
                      "storage.users", "data/users.db"));
        auto auth_sessions = database
            ? std::make_shared<core::SessionStore>(
                  database, session_lifetime)
            : std::make_shared<core::SessionStore>(
                  config.get_string(
                      "storage.sessions", "data/sessions.db"),
                  session_lifetime);
        auto appearance = std::make_shared<opengenesis::avatar::AppearanceStore>(
            config.get_string("storage.appearance", "data/appearance.db"));
        auto assets = std::make_shared<core::AssetStore>(
            config.get_string("storage.assets_metadata", "data/assets.db"),
            config.get_string("storage.assets_blobs", "data/assets"),
            static_cast<std::size_t>(config.get_int("assets.max_bytes", 1048576)));
        auto inventory = std::make_shared<core::InventoryStore>(
            config.get_string("storage.inventory", "data/inventory.db"));
        auto presences = std::make_shared<core::PresenceStore>();
        auto friends = std::make_shared<core::FriendsStore>(
            config.get_string("storage.friends", "data/friends.db"));
        auto messages = std::make_shared<core::MessageStore>(
            config.get_string("storage.messages", "data/messages.db"));
        auto groups = std::make_shared<core::GroupStore>(
            config.get_string("storage.groups", "data/groups.db"));
        auto parcels = std::make_shared<core::ParcelStore>(
            config.get_string("storage.parcels", "data/parcels.db"));
        auto moderation = database
            ? std::make_shared<core::ModerationStore>(database)
            : std::make_shared<core::ModerationStore>(
                  config.get_string(
                      "storage.moderation", "data/moderation.db"));
        auto audit = database
            ? std::make_shared<core::AuditStore>(database)
            : std::make_shared<core::AuditStore>(
                  config.get_string(
                      "storage.audit", "data/audit.log"));
        auto admin_roles = database
            ? std::make_shared<core::AdminRoleStore>(database)
            : std::make_shared<core::AdminRoleStore>(
                  config.get_string(
                      "storage.admin_roles", "data/admin-roles.db"));
        auto estates = std::make_shared<core::EstateStore>(
            config.get_string("storage.estates", "data/estates.db"));
        auto landmarks = std::make_shared<core::LandmarkStore>(
            config.get_string("storage.landmarks", "data/landmarks.db"));
        auto notifications = std::make_shared<core::NotificationStore>(
            config.get_string("storage.notifications", "data/notifications.db"));
        auto group_channels = std::make_shared<core::GroupChannelStore>(
            config.get_string("storage.group_channels", "data/group_channels.db"));
        auto crossings = std::make_shared<core::CrossingStore>(
            config.get_string("storage.crossings", "data/crossings.db"));
        auto object_crossings = std::make_shared<core::ObjectCrossingStore>(
            config.get_string("storage.object_crossings", "data/object-crossings.db"),
            static_cast<std::uint32_t>(std::max<std::int64_t>(
                1, config.get_int("crossing.object_max_attempts", 5))));
        auto scripts = std::make_shared<opengenesis::scripting::ScriptRuntime>(
            config.get_string("storage.scripts", "data/scripts.db"));
        const auto script_world_max_pending =
            config.get_int("scripting.max_pending_world_actions", 4096);
        if (script_world_max_pending < 1) {
            throw std::runtime_error("scripting.max_pending_world_actions must be positive");
        }
        auto script_world_actions =
            std::make_shared<opengenesis::scripting::ScriptWorldActionQueue>(
                config.get_string("storage.script_world_actions",
                                  "data/script-world-actions.db"),
                static_cast<std::size_t>(script_world_max_pending),
                static_cast<std::uint32_t>(
                    std::max<std::int64_t>(
                        1, config.get_int("scripting.world_action_max_attempts", 5))),
                config.get_int("scripting.world_action_lease_ms", 2000),
                config.get_int("scripting.world_action_ttl_ms", 60000));
        auto script_host = std::make_shared<opengenesis::scripting::ScriptHost>(
            identities, friends, messages, notifications, script_world_actions);
        auto federation_identity = std::make_shared<opengenesis::federation::GridIdentityStore>(
            config.get_string("storage.federation_identity", "data/federation-identity.db"),
            config.get_string("federation.grid_id", "local.opengenesislink"),
            config.get_string("federation.base_url", "http://127.0.0.1:18080"));
        auto federation_trust = std::make_shared<opengenesis::federation::FederationTrustStore>(
            config.get_string("storage.federation_trust", "data/federation-trust.db"));
        auto federation_sessions = std::make_shared<opengenesis::federation::FederationSessionStore>(
            config.get_string("storage.federation_sessions", "data/federation-sessions.db"));
        auto federation_grants =
            std::make_shared<opengenesis::federation::FederationServiceGrantStore>(
                config.get_string(
                    "storage.federation_service_grants",
                    "data/federation-service-grants.db"));
        auto federation_runtime = std::make_shared<opengenesis::federation::FederationRuntime>(
            federation_identity, federation_trust,
            federation_sessions, federation_grants);

        const auto hypergrid_port_value = config.get_int("hypergrid.port", 18081);
        const auto hypergrid_http_port_value = config.get_int("hypergrid.region_http_port", 19100);
        const auto hypergrid_internal_port_value = config.get_int("hypergrid.region_internal_port", 19100);
        if (hypergrid_port_value < 1 || hypergrid_port_value > 65535 ||
            hypergrid_http_port_value < 1 || hypergrid_http_port_value > 65535 ||
            hypergrid_internal_port_value < 1 || hypergrid_internal_port_value > 65535) {
            throw std::runtime_error("invalid Hypergrid port configuration");
        }
        auto hypergrid_sessions = std::make_shared<opengenesis::compat::hypergrid::HypergridSessionStore>(
            config.get_string("storage.hypergrid_sessions", "data/hypergrid-sessions.db"));
        const auto hypergrid_external_name =
            config.get_string("hypergrid.external_name", "http://127.0.0.1:18081");
        auto hypergrid_service = std::make_shared<opengenesis::compat::hypergrid::HypergridService>(
            opengenesis::compat::hypergrid::HypergridConfig{
                .enabled = config.get_bool("hypergrid.enabled", false),
                .external_name = hypergrid_external_name,
                .home_uri = config.get_string("hypergrid.home_uri", hypergrid_external_name),
                .asset_uri = config.get_string("hypergrid.asset_uri", hypergrid_external_name),
                .inventory_uri = config.get_string("hypergrid.inventory_uri", hypergrid_external_name),
                .avatar_uri = config.get_string("hypergrid.avatar_uri", hypergrid_external_name),
                .friends_uri = config.get_string("hypergrid.friends_uri", hypergrid_external_name),
                .im_uri = config.get_string("hypergrid.im_uri", hypergrid_external_name),
                .region_host = config.get_string("hypergrid.region_host", "127.0.0.1"),
                .http_port = static_cast<std::uint16_t>(hypergrid_http_port_value),
                .internal_port = static_cast<std::uint16_t>(hypergrid_internal_port_value)},
            regions);
        auto hypergrid_verifier =
            std::make_shared<opengenesis::compat::hypergrid::HttpHypergridHomeVerifier>();
        auto hypergrid_friends =
            std::make_shared<opengenesis::compat::hypergrid::HypergridFriendsAdapter>(
                identities, friends, presences, notifications, hypergrid_sessions);
        auto hypergrid_assets =
            std::make_shared<opengenesis::compat::hypergrid::HypergridAssetAdapter>(assets);
        auto hypergrid_im =
            std::make_shared<opengenesis::compat::hypergrid::HypergridInstantMessageAdapter>(
                identities, messages, notifications, hypergrid_sessions);
        auto hypergrid_inventory =
            std::make_shared<opengenesis::compat::hypergrid::HypergridInventoryAdapter>(
                identities, inventory, assets,
                config.get_bool("hypergrid.inventory_write_enabled", false),
                config.get_string("hypergrid.inventory_write_secret", ""));
        auto hypergrid_appearance =
            std::make_shared<opengenesis::compat::hypergrid::HypergridAppearanceAdapter>(
                identities, appearance, inventory, assets, hypergrid_sessions);
        auto hypergrid_server = std::make_unique<opengenesis::compat::hypergrid::HypergridServer>(
            config.get_string("hypergrid.listen_address", "127.0.0.1"),
            static_cast<std::uint16_t>(hypergrid_port_value),
            hypergrid_service, hypergrid_sessions, hypergrid_verifier, hypergrid_friends,
            hypergrid_assets, hypergrid_im, hypergrid_inventory, hypergrid_appearance);
        auto node_sessions = std::make_shared<core::NodeSessions>();

        if (!production_mode &&
            scene_ticket_secret ==
                "development-only-change-this-scene-ticket-secret") {
            opengenesis::common::log(LogLevel::warning, "core.security",
                                     "Using development scene-ticket secret; replace it before network exposure");
        }
        if (!production_mode &&
            admin_api_key ==
                "development-only-change-this-admin-api-key") {
            opengenesis::common::log(LogLevel::warning, "core.security",
                                     "Using development admin API key; replace it before network exposure");
        }

        core::AdminHttpServer admin(
            config.get_string("admin.listen_address", "127.0.0.1"),
            static_cast<std::uint16_t>(config.get_int("admin.port", 18080)), worlds, regions,
            identities, auth_sessions, assets, appearance, inventory, presences, friends, messages, groups, parcels,
            moderation, audit, estates, landmarks, notifications, group_channels, crossings,
            object_crossings, scripts, script_host, federation_runtime,
            hypergrid_service, hypergrid_sessions, hypergrid_im, database,
            admin_roles, admin_api_key, scene_ticket_secret,
            scene_ticket_lifetime,
            static_cast<std::size_t>(login_attempts_per_minute),
            static_cast<std::size_t>(registration_attempts_per_minute));
        admin.start();
        if (hypergrid_service->enabled()) hypergrid_server->start();

        opengenesis::network::TcpListener listener(address, port);
        opengenesis::common::log(LogLevel::info, "core",
                                 "OpenGenesis Core " OGL_VERSION " listening on " + address + ':' +
                                     std::to_string(port));

        std::thread maintenance_thread([&] {
            while (running) {
                for (const auto& session : node_sessions->expired(lease_timeout)) {
                    worlds->mark_offline(session.node_id, session.generation);
                    regions->mark_node_offline(session.node_id, session.generation);
                    presences->mark_node_offline(session.node_id, session.generation);
                    node_sessions->close(session.node_id, session.generation);
                    opengenesis::common::log(LogLevel::warning, "core.lease",
                                             "Lease expired for " + session.node_id);
                }
                (void)auth_sessions->purge_expired();
                const auto now_unix = std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count();
                (void)federation_runtime->maintenance(now_unix);
                (void)hypergrid_sessions->purge_expired(now_unix);
                (void)crossings->purge_expired(now_unix);
                (void)object_crossings->maintenance(now_unix);
                const auto now_ms = unix_ms();
                (void)script_world_actions->purge_expired(now_ms);
                for (const auto& event : scripts->due_timers(now_ms)) {
                    const auto script = scripts->find(event.script_id);
                    if (!script) continue;
                    std::string reason;
                    const auto result =
                        scripts->execute_event(
                            event.script_id, event.type, now_ms, reason, {},
                            event.payload);
                    if (result) {
                        (void)script_host->apply(
                            script->owner_user_id, script->id, result->actions,
                            script->object_id);
                    }
                }
                std::this_thread::sleep_for(std::chrono::seconds{1});
            }
        });

        while (running) {
            auto accepted = listener.accept_for(std::chrono::milliseconds{250});
            if (!accepted) continue;
            std::thread([socket = std::move(*accepted), worlds, regions, presences,
                         node_sessions, script_world_actions, object_crossings, scripts,
                         lease_timeout, world_node_secret]() mutable {
                std::string node_id;
                std::uint64_t generation = 0;
                try {
                    const auto hello = socket.receive_frame();
                    if (hello.type != protocol::MessageType::hello) {
                        throw std::runtime_error("HELLO required");
                    }
                    const auto auth_challenge =
                        world_node_secret.empty()
                            ? std::string{}
                            : opengenesis::security::random_hex(16);
                    socket.send_frame({
                        protocol::MessageType::hello_ack,
                        hello.request_id,
                        protocol::payload_from_string(
                            "protocol=1\nserver=opengenesis-core\n"
                            "auth=" +
                            std::string(
                                world_node_secret.empty()
                                    ? "none"
                                    : "hmac-sha256") +
                            "\nchallenge=" + auth_challenge + "\n")});

                    const auto registration = socket.receive_frame();
                    if (registration.type != protocol::MessageType::world_register) {
                        throw std::runtime_error("WORLD_REGISTER required");
                    }
                    const auto registration_body =
                        protocol::payload_as_string(registration);
                    const auto registration_id =
                        field(registration_body, "id");
                    const auto registration_endpoint =
                        field(registration_body, "endpoint");
                    if (!world_node_secret.empty()) {
                        const auto expected =
                            opengenesis::security::hmac_sha256_hex(
                                world_node_secret,
                                auth_challenge + "\n" +
                                    registration_id + "\n" +
                                    registration_endpoint);
                        if (!opengenesis::security::secure_equals(
                                expected,
                                field(registration_body, "auth"))) {
                            socket.send_frame({
                                protocol::MessageType::error,
                                registration.request_id,
                                protocol::payload_from_string(
                                    "reason=world-node-authentication-failed\n")});
                            throw std::runtime_error(
                                "world node authentication failed");
                        }
                    }
                    const auto info = worlds->register_or_reconnect(
                        registration_id, field(registration_body, "name"),
                        registration_endpoint);
                    node_id = info.id;
                    generation = info.generation;
                    node_sessions->open(node_id, generation);
                    socket.send_frame({
                        protocol::MessageType::world_register_ack,
                        registration.request_id,
                        protocol::payload_from_string(
                            "status=registered\ngeneration=" + std::to_string(generation) +
                            "\nlease_seconds=" + std::to_string(lease_timeout.count()) + "\n")});

                    while (running) {
                        const auto frame = socket.receive_frame();
                        const auto body = protocol::payload_as_string(frame);
                        if (frame.type == protocol::MessageType::world_lease ||
                            frame.type == protocol::MessageType::ping) {
                            const bool ok = node_sessions->renew(node_id, generation) &&
                                            worlds->touch(node_id, generation);
                            socket.send_frame({frame.type == protocol::MessageType::ping
                                                   ? protocol::MessageType::pong
                                                   : protocol::MessageType::world_lease_ack,
                                               frame.request_id,
                                               protocol::payload_from_string(ok ? "status=ok\n"
                                                                                : "status=stale\n")});
                            if (!ok) break;
                        } else if (frame.type == protocol::MessageType::region_register) {
                            core::RegionInfo region{.id = field(body, "id"),
                                                    .name = field(body, "name"),
                                                    .node_id = node_id,
                                                    .state = "registered",
                                                    .grid_x = static_cast<std::int32_t>(
                                                        std::stoi(field(body, "grid_x"))),
                                                    .grid_y = static_cast<std::int32_t>(
                                                        std::stoi(field(body, "grid_y"))),
                                                    .node_generation = generation};
                            std::string reason;
                            const bool ok = regions->register_region(region, reason);
                            socket.send_frame({ok ? protocol::MessageType::region_register_ack
                                                  : protocol::MessageType::error,
                                               frame.request_id,
                                               protocol::payload_from_string(ok
                                                                                 ? "status=registered\n"
                                                                                 : "reason=" + reason + "\n")});
                        } else if (frame.type == protocol::MessageType::region_state_update) {
                            std::string reason;
                            const bool ok = regions->update_state(field(body, "id"), node_id,
                                                                  generation, field(body, "state"), reason);
                            socket.send_frame({ok ? protocol::MessageType::region_state_ack
                                                  : protocol::MessageType::error,
                                               frame.request_id,
                                               protocol::payload_from_string(ok ? "status=ok\n"
                                                                                : "reason=" + reason + "\n")});
                        } else if (frame.type == protocol::MessageType::region_metrics) {
                            core::RegionInfo metrics{.id = field(body, "id"),
                                                     .node_id = node_id,
                                                     .node_generation = generation,
                                                     .ticks = u64(body, "ticks"),
                                                     .entities = u64(body, "entities"),
                                                     .avatars = u64(body, "avatars"),
                                                     .physics_bodies = u64(body, "physics_bodies"),
                                                     .scene_events = u64(body, "scene_events"),
                                                     .terrain_revision = u64(body, "terrain_revision"),
                                                     .sim_fps = number(body, "sim_fps")};
                            std::string reason;
                            const bool ok = regions->update_metrics(metrics, reason);
                            socket.send_frame({ok ? protocol::MessageType::region_metrics_ack
                                                  : protocol::MessageType::error,
                                               frame.request_id,
                                               protocol::payload_from_string(ok ? "status=ok\n"
                                                                                : "reason=" + reason + "\n")});
                        } else if (frame.type == protocol::MessageType::presence_snapshot) {
                            const auto region_id = field(body, "region");
                            const auto region = regions->find(region_id);
                            const bool ok = region && region->node_id == node_id &&
                                            region->node_generation == generation;
                            if (ok) {
                                presences->replace_region_snapshot(region_id, node_id, generation,
                                                                   parse_presences(body));
                            }
                            socket.send_frame({ok ? protocol::MessageType::presence_snapshot_ack
                                                  : protocol::MessageType::error,
                                               frame.request_id,
                                               protocol::payload_from_string(
                                                   ok ? "status=ok\n" : "reason=stale-region\n")});
                        } else if (frame.type == protocol::MessageType::script_action_poll) {
                            const auto region_id = field(body, "region");
                            const auto region = regions->find(region_id);
                            const bool owns_region =
                                region && region->node_id == node_id &&
                                region->node_generation == generation;
                            if (!owns_region) {
                                socket.send_frame({
                                    protocol::MessageType::error, frame.request_id,
                                    protocol::payload_from_string(
                                        "reason=stale-region\n")});
                                continue;
                            }
                            const auto action =
                                script_world_actions->lease(region_id, unix_ms());
                            if (!action) {
                                socket.send_frame({
                                    protocol::MessageType::script_action, frame.request_id,
                                    protocol::payload_from_string("status=none\n")});
                                continue;
                            }
                            std::ostringstream action_body;
                            action_body << "status=action\n"
                                        << "id=" << action->id << '\n'
                                        << "region=" << action->region_id << '\n'
                                        << "entity=" << action->entity_id << '\n'
                                        << "owner=" << action->owner_user_id << '\n'
                                        << "script=" << action->script_id << '\n'
                                        << "type="
                                        << opengenesis::scripting::script_world_action_name(
                                               action->type)
                                        << '\n'
                                        << "payload=" << action->payload << '\n'
                                        << "attempt=" << action->attempts << '\n'
                                        << "expires_unix_ms=" << action->expires_unix_ms << '\n';
                            socket.send_frame({
                                protocol::MessageType::script_action, frame.request_id,
                                protocol::payload_from_string(action_body.str())});
                        } else if (frame.type ==
                                   protocol::MessageType::script_action_result) {
                            const auto action_id = field(body, "id");
                            const auto region_id = field(body, "region");
                            const auto status = field(body, "status");
                            const auto action = script_world_actions->find(action_id);
                            const auto region = regions->find(region_id);
                            const bool owns_region =
                                region && region->node_id == node_id &&
                                region->node_generation == generation;
                            if (!action || action->region_id != region_id || !owns_region) {
                                socket.send_frame({
                                    protocol::MessageType::script_action_result_ack,
                                    frame.request_id,
                                    protocol::payload_from_string(
                                        "status=missing-or-stale\n")});
                                continue;
                            }

                            bool accepted = false;
                            std::string result_error;
                            if (status == "ok") {
                                accepted = true;
                                if (opengenesis::scripting::script_world_action_is_query(
                                        action->type)) {
                                    try {
                                        const auto decoded =
                                            opengenesis::security::base64_decode(
                                                field(body, "result_b64"), 16U * 1024U);
                                        accepted = scripts->apply_world_result(
                                            action->script_id,
                                            opengenesis::scripting::
                                                script_world_action_result_prefix(*action),
                                            decoded, result_error);
                                    } catch (...) {
                                        accepted = false;
                                        result_error = "world-result-decode-failed";
                                    }
                                }
                            } else {
                                result_error = field(body, "error");
                                if (result_error.empty()) {
                                    result_error = "world-action-rejected";
                                }
                            }

                            if (accepted) {
                                (void)script_world_actions->ack(action_id);
                            } else {
                                (void)script_world_actions->nack(
                                    action_id, result_error, unix_ms() + 250);
                            }
                            socket.send_frame({
                                protocol::MessageType::script_action_result_ack,
                                frame.request_id,
                                protocol::payload_from_string(
                                    accepted ? "status=acked\n"
                                             : "status=retry-or-dropped\n")});
                        } else if (frame.type ==
                                   protocol::MessageType::object_crossing_poll) {
                            const auto region_id = field(body, "region");
                            const auto region = regions->find(region_id);
                            const bool owns_region =
                                region && region->node_id == node_id &&
                                region->node_generation == generation;
                            if (!owns_region) {
                                socket.send_frame({
                                    protocol::MessageType::error, frame.request_id,
                                    protocol::payload_from_string(
                                        "reason=stale-region\n")});
                                continue;
                            }

                            const auto command =
                                object_crossings->command_for_region(region_id);
                            if (!command) {
                                socket.send_frame({
                                    protocol::MessageType::object_crossing_command,
                                    frame.request_id,
                                    protocol::payload_from_string(
                                        "status=none\n")});
                                continue;
                            }

                            const auto& crossing = command->crossing;
                            std::ostringstream command_body;
                            command_body
                                << "status=command\n"
                                << "id=" << crossing.id << '\n'
                                << "command="
                                << core::object_crossing_command_name(command->type)
                                << '\n'
                                << "owner=" << crossing.owner_user_id << '\n'
                                << "source_region=" << crossing.source_region << '\n'
                                << "destination_region="
                                << crossing.destination_region << '\n'
                                << "source_entity="
                                << crossing.source_entity_id << '\n'
                                << "destination_entity="
                                << crossing.destination_entity_id << '\n'
                                << "x=" << crossing.destination_position.x << '\n'
                                << "y=" << crossing.destination_position.y << '\n'
                                << "z=" << crossing.destination_position.z << '\n'
                                << "snapshot_b64="
                                << opengenesis::security::base64_encode(
                                       crossing.snapshot)
                                << '\n';

                            socket.send_frame({
                                protocol::MessageType::object_crossing_command,
                                frame.request_id,
                                protocol::payload_from_string(
                                    command_body.str())});
                        } else if (frame.type ==
                                   protocol::MessageType::object_crossing_result) {
                            const auto crossing_id = field(body, "id");
                            const auto region_id = field(body, "region");
                            const auto command_name = field(body, "command");
                            const auto status = field(body, "status");
                            const auto region = regions->find(region_id);
                            const bool owns_region =
                                region && region->node_id == node_id &&
                                region->node_generation == generation;
                            const auto expected =
                                object_crossings->command_for_region(region_id);

                            if (!owns_region || !expected ||
                                expected->crossing.id != crossing_id ||
                                core::object_crossing_command_name(expected->type) !=
                                    command_name) {
                                socket.send_frame({
                                    protocol::MessageType::object_crossing_result_ack,
                                    frame.request_id,
                                    protocol::payload_from_string(
                                        "status=missing-or-stale\n")});
                                continue;
                            }

                            bool accepted = false;
                            std::string result_reason;
                            if (status == "ok") {
                                switch (expected->type) {
                                    case core::ObjectCrossingCommandType::export_source:
                                        try {
                                            accepted =
                                                object_crossings->record_export(
                                                    crossing_id, region_id,
                                                    opengenesis::security::base64_decode(
                                                        field(body, "snapshot_b64"),
                                                        256U * 1024U),
                                                    result_reason);
                                        } catch (...) {
                                            result_reason =
                                                "object-snapshot-decode-failed";
                                        }
                                        break;
                                    case core::ObjectCrossingCommandType::import_destination:
                                        try {
                                            const auto entity_map =
                                                opengenesis::security::base64_decode(
                                                    field(body, "entity_map_b64"),
                                                    16U * 1024U);
                                            if (!parse_entity_map(entity_map)) {
                                                result_reason =
                                                    "object-crossing-entity-map-invalid";
                                                accepted = false;
                                            } else {
                                                accepted =
                                                    object_crossings->record_import(
                                                        crossing_id, region_id,
                                                        u64(body, "destination_entity"),
                                                        entity_map,
                                                        result_reason);
                                            }
                                        } catch (...) {
                                            result_reason =
                                                "object-crossing-entity-map-decode-failed";
                                        }
                                        break;
                                    case core::ObjectCrossingCommandType::remove_source: {
                                        const auto entity_map =
                                            parse_entity_map(
                                                expected->crossing.entity_map);
                                        if (!entity_map) {
                                            result_reason =
                                                "object-crossing-entity-map-invalid";
                                            accepted = false;
                                            break;
                                        }
                                        if (!scripts->rebind_objects(
                                                expected->crossing.source_region,
                                                expected->crossing.destination_region,
                                                *entity_map,
                                                expected->crossing.owner_user_id,
                                                result_reason)) {
                                            accepted = false;
                                            break;
                                        }
                                        accepted = object_crossings->record_remove(
                                            crossing_id, region_id, result_reason);
                                        break;
                                    }
                                    case core::ObjectCrossingCommandType::cleanup_destination:
                                        accepted = object_crossings->record_cleanup(
                                            crossing_id, region_id, result_reason);
                                        break;
                                    case core::ObjectCrossingCommandType::restore_source:
                                        accepted = object_crossings->record_restore(
                                            crossing_id, region_id, result_reason);
                                        break;
                                }
                            } else {
                                accepted = object_crossings->reject_command(
                                    crossing_id, expected->type,
                                    field(body, "error"), result_reason);
                            }

                            socket.send_frame({
                                protocol::MessageType::object_crossing_result_ack,
                                frame.request_id,
                                protocol::payload_from_string(
                                    accepted ? "status=acked\n"
                                             : "status=rejected\nreason=" +
                                                   result_reason + "\n")});
                        } else if (frame.type == protocol::MessageType::goodbye) {
                            break;
                        } else {
                            socket.send_frame({protocol::MessageType::error, frame.request_id,
                                               protocol::payload_from_string(
                                                   "reason=unsupported-message\n")});
                        }
                    }
                } catch (const std::exception& error) {
                    opengenesis::common::log(LogLevel::warning, "core.connection", error.what());
                }
                if (!node_id.empty()) {
                    node_sessions->close(node_id, generation);
                    worlds->mark_offline(node_id, generation);
                    regions->mark_node_offline(node_id, generation);
                    presences->mark_node_offline(node_id, generation);
                }
            }).detach();
        }

        if (maintenance_thread.joinable()) maintenance_thread.join();
        hypergrid_server->stop();
        admin.stop();
        return 0;
    } catch (const std::exception& error) {
        opengenesis::common::log(LogLevel::error, "core", error.what());
        return 1;
    }
}
