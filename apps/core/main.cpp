#include "opengenesis/common/log.hpp"
#include "opengenesis/config/toml_config.hpp"
#include "opengenesis/core/admin_http.hpp"
#include "opengenesis/core/identity_store.hpp"
#include "opengenesis/core/node_sessions.hpp"
#include "opengenesis/core/region_registry.hpp"
#include "opengenesis/core/session_store.hpp"
#include "opengenesis/core/world_registry.hpp"
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

        auto worlds = std::make_shared<core::WorldRegistry>(
            config.get_string("storage.worlds", "data/worlds.db"));
        auto regions = std::make_shared<core::RegionRegistry>(
            config.get_string("storage.regions", "data/regions.db"));
        auto identities = std::make_shared<core::IdentityStore>(
            config.get_string("storage.users", "data/users.db"));
        auto auth_sessions = std::make_shared<core::SessionStore>(
            config.get_string("storage.sessions", "data/sessions.db"), session_lifetime);
        auto node_sessions = std::make_shared<core::NodeSessions>();

        core::AdminHttpServer admin(
            config.get_string("admin.listen_address", "127.0.0.1"),
            static_cast<std::uint16_t>(config.get_int("admin.port", 18080)), worlds, regions,
            identities, auth_sessions);
        admin.start();

        opengenesis::network::TcpListener listener(address, port);
        opengenesis::common::log(LogLevel::info, "core",
                                 "OpenGenesis Core " OGL_VERSION " listening on " + address + ':' +
                                     std::to_string(port));

        std::thread maintenance_thread([&] {
            while (running) {
                for (const auto& session : node_sessions->expired(lease_timeout)) {
                    worlds->mark_offline(session.node_id, session.generation);
                    regions->mark_node_offline(session.node_id, session.generation);
                    node_sessions->close(session.node_id, session.generation);
                    opengenesis::common::log(LogLevel::warning, "core.lease",
                                             "Lease expired for " + session.node_id);
                }
                (void)auth_sessions->purge_expired();
                std::this_thread::sleep_for(std::chrono::seconds{1});
            }
        });

        while (running) {
            auto accepted = listener.accept_for(std::chrono::milliseconds{250});
            if (!accepted) continue;
            std::thread([socket = std::move(*accepted), worlds, regions, node_sessions, lease_timeout]() mutable {
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
                }
            }).detach();
        }

        if (maintenance_thread.joinable()) maintenance_thread.join();
        admin.stop();
        return 0;
    } catch (const std::exception& error) {
        opengenesis::common::log(LogLevel::error, "core", error.what());
        return 1;
    }
}
