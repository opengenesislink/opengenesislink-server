#include "opengenesis/common/log.hpp"
#include "opengenesis/config/toml_config.hpp"
#include "opengenesis/core/admin_http.hpp"
#include "opengenesis/core/asset_store.hpp"
#include "opengenesis/core/audit_store.hpp"
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
#include "opengenesis/federation/grid_identity_store.hpp"
#include "opengenesis/federation/runtime.hpp"
#include "opengenesis/federation/session_store.hpp"
#include "opengenesis/federation/trust_store.hpp"
#include "opengenesis/compat/hypergrid/home_verifier.hpp"
#include "opengenesis/compat/hypergrid/server.hpp"
#include "opengenesis/compat/hypergrid/service.hpp"
#include "opengenesis/compat/hypergrid/session_store.hpp"
#include "opengenesis/network/tcp.hpp"
#include "opengenesis/protocol/frame.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using opengenesis::common::LogLevel;
namespace protocol = opengenesis::protocol;
namespace core = opengenesis::core;

namespace {
std::atomic_bool running{true};
void signal_handler(int) { running = false; }

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
        const auto scene_ticket_secret = config.get_string(
            "security.scene_ticket_secret", "development-only-change-this-scene-ticket-secret");
        const auto admin_api_key = config.get_string(
            "security.admin_api_key", "development-only-change-this-admin-api-key");
        if (scene_ticket_secret.size() < 32) throw std::runtime_error("security.scene_ticket_secret must contain at least 32 bytes");
        if (admin_api_key.size() < 24) throw std::runtime_error("security.admin_api_key must contain at least 24 bytes");

        auto worlds = std::make_shared<core::WorldRegistry>(
            config.get_string("storage.worlds", "data/worlds.db"));
        auto regions = std::make_shared<core::RegionRegistry>(
            config.get_string("storage.regions", "data/regions.db"));
        auto identities = std::make_shared<core::IdentityStore>(
            config.get_string("storage.users", "data/users.db"));
        auto auth_sessions = std::make_shared<core::SessionStore>(
            config.get_string("storage.sessions", "data/sessions.db"), session_lifetime);
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
        auto moderation = std::make_shared<core::ModerationStore>(
            config.get_string("storage.moderation", "data/moderation.db"));
        auto audit = std::make_shared<core::AuditStore>(
            config.get_string("storage.audit", "data/audit.log"));
        auto estates = std::make_shared<core::EstateStore>(
            config.get_string("storage.estates", "data/estates.db"));
        auto landmarks = std::make_shared<core::LandmarkStore>(
            config.get_string("storage.landmarks", "data/landmarks.db"));
        auto notifications = std::make_shared<core::NotificationStore>(
            config.get_string("storage.notifications", "data/notifications.db"));
        auto group_channels = std::make_shared<core::GroupChannelStore>(
            config.get_string("storage.group_channels", "data/group_channels.db"));
        auto federation_identity = std::make_shared<opengenesis::federation::GridIdentityStore>(
            config.get_string("storage.federation_identity", "data/federation-identity.db"),
            config.get_string("federation.grid_id", "local.opengenesislink"),
            config.get_string("federation.base_url", "http://127.0.0.1:18080"));
        auto federation_trust = std::make_shared<opengenesis::federation::FederationTrustStore>(
            config.get_string("storage.federation_trust", "data/federation-trust.db"));
        auto federation_sessions = std::make_shared<opengenesis::federation::FederationSessionStore>(
            config.get_string("storage.federation_sessions", "data/federation-sessions.db"));
        auto federation_runtime = std::make_shared<opengenesis::federation::FederationRuntime>(
            federation_identity, federation_trust, federation_sessions);

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
        auto hypergrid_service = std::make_shared<opengenesis::compat::hypergrid::HypergridService>(
            opengenesis::compat::hypergrid::HypergridConfig{
                .enabled = config.get_bool("hypergrid.enabled", false),
                .external_name = config.get_string("hypergrid.external_name", "http://127.0.0.1:18081"),
                .home_uri = config.get_string("hypergrid.home_uri", "http://127.0.0.1:18081"),
                .asset_uri = config.get_string("hypergrid.asset_uri", ""),
                .inventory_uri = config.get_string("hypergrid.inventory_uri", ""),
                .friends_uri = config.get_string("hypergrid.friends_uri", ""),
                .im_uri = config.get_string("hypergrid.im_uri", ""),
                .region_host = config.get_string("hypergrid.region_host", "127.0.0.1"),
                .http_port = static_cast<std::uint16_t>(hypergrid_http_port_value),
                .internal_port = static_cast<std::uint16_t>(hypergrid_internal_port_value)},
            regions);
        auto hypergrid_verifier =
            std::make_shared<opengenesis::compat::hypergrid::HttpHypergridHomeVerifier>();
        auto hypergrid_server = std::make_unique<opengenesis::compat::hypergrid::HypergridServer>(
            config.get_string("hypergrid.listen_address", "127.0.0.1"),
            static_cast<std::uint16_t>(hypergrid_port_value),
            hypergrid_service, hypergrid_sessions, hypergrid_verifier);
        auto node_sessions = std::make_shared<core::NodeSessions>();

        if (scene_ticket_secret == "development-only-change-this-scene-ticket-secret") {
            opengenesis::common::log(LogLevel::warning, "core.security",
                                     "Using development scene-ticket secret; replace it before network exposure");
        }
        if (admin_api_key == "development-only-change-this-admin-api-key") {
            opengenesis::common::log(LogLevel::warning, "core.security",
                                     "Using development admin API key; replace it before network exposure");
        }

        core::AdminHttpServer admin(
            config.get_string("admin.listen_address", "127.0.0.1"),
            static_cast<std::uint16_t>(config.get_int("admin.port", 18080)), worlds, regions,
            identities, auth_sessions, assets, inventory, presences, friends, messages, groups, parcels,
            moderation, audit, estates, landmarks, notifications, group_channels, federation_runtime,
            hypergrid_service, hypergrid_sessions, admin_api_key, scene_ticket_secret,
            scene_ticket_lifetime);
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
                std::this_thread::sleep_for(std::chrono::seconds{1});
            }
        });

        while (running) {
            auto accepted = listener.accept_for(std::chrono::milliseconds{250});
            if (!accepted) continue;
            std::thread([socket = std::move(*accepted), worlds, regions, presences, node_sessions, lease_timeout]() mutable {
                std::string node_id;
                std::uint64_t generation = 0;
                try {
                    const auto hello = socket.receive_frame();
                    if (hello.type != protocol::MessageType::hello) {
                        throw std::runtime_error("HELLO required");
                    }
                    socket.send_frame({protocol::MessageType::hello_ack, hello.request_id,
                                       protocol::payload_from_string(
                                           "protocol=1\nserver=opengenesis-core\n")});

                    const auto registration = socket.receive_frame();
                    if (registration.type != protocol::MessageType::world_register) {
                        throw std::runtime_error("WORLD_REGISTER required");
                    }
                    const auto registration_body = protocol::payload_as_string(registration);
                    const auto info = worlds->register_or_reconnect(
                        field(registration_body, "id"), field(registration_body, "name"),
                        field(registration_body, "endpoint"));
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
