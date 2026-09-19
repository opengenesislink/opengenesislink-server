#include "opengenesis/common/log.hpp"
#include "opengenesis/config/toml_config.hpp"
#include "opengenesis/network/tcp.hpp"
#include "opengenesis/protocol/frame.hpp"
#include "opengenesis/world/region_persistence.hpp"
#include "opengenesis/world/region_runtime.hpp"
#include "opengenesis/world/scene_server.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

using opengenesis::common::LogLevel;
namespace protocol = opengenesis::protocol;
namespace world = opengenesis::world;

namespace {
std::atomic_bool running{true};
void signal_handler(int) { running = false; }

std::pair<std::string, std::uint16_t> parse_endpoint(const std::string& endpoint) {
    const auto split = endpoint.rfind(':');
    if (split == std::string::npos || split == 0 || split + 1 >= endpoint.size()) {
        throw std::runtime_error("endpoint must use host:port");
    }
    const auto port = std::stoul(endpoint.substr(split + 1));
    if (port < 1 || port > 65535) throw std::runtime_error("invalid endpoint port");
    return {endpoint.substr(0, split), static_cast<std::uint16_t>(port)};
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

struct RegionConfig {
    std::string id;
    std::string name;
    int grid_x{0};
    int grid_y{0};
};

std::string clean_wire_field(std::string value) {
    value.erase(std::remove_if(value.begin(), value.end(), [](const char c) {
        return c == '\n' || c == '\r' || c == '|';
    }), value.end());
    if (value.size() > 96) value.resize(96);
    return value;
}

std::string presence_snapshot_payload(const world::RegionRuntime& runtime) {
    const auto entities = runtime.snapshot_entities();
    std::ostringstream body;
    body << std::fixed << std::setprecision(3) << "region=" << runtime.id() << '\n';
    std::size_t count = 0;
    for (const auto& entity : entities) {
        if (entity.kind != world::EntityKind::avatar || entity.owner_user_id.empty()) continue;
        ++count;
        body << "presence=" << clean_wire_field(entity.owner_user_id) << '|'
             << clean_wire_field(entity.name) << '|' << entity.id << '|'
             << entity.transform.position.x << '|' << entity.transform.position.y << '|'
             << entity.transform.position.z << '\n';
    }
    body << "count=" << count << '\n';
    return body.str();
}

bool vector3(const std::string& text, opengenesis::physics::Vec3& value) {
    std::istringstream input(text);
    std::string extra;
    if (!(input >> value.x >> value.y >> value.z) || (input >> extra)) return false;
    return true;
}

bool apply_script_action(
    const std::vector<std::shared_ptr<world::RegionRuntime>>& runtimes,
    const std::string& body) {
    const auto region_id = field(body, "region");
    const auto owner = field(body, "owner");
    const auto type = field(body, "type");
    const auto payload = field(body, "payload");
    const auto entity_text = field(body, "entity");
    if (region_id.empty() || owner.empty() || type.empty() || entity_text.empty()) {
        return false;
    }

    const auto runtime = std::find_if(
        runtimes.begin(), runtimes.end(),
        [&](const auto& candidate) { return candidate->id() == region_id; });
    if (runtime == runtimes.end()) return false;

    std::uint64_t entity_id = 0;
    try {
        entity_id = std::stoull(entity_text);
    } catch (...) {
        return false;
    }

    const auto entity = (*runtime)->entity(entity_id);
    if (!entity || entity->kind != world::EntityKind::object ||
        entity->owner_user_id != owner) {
        return false;
    }

    if (type == "physics") {
        return (*runtime)->set_physical(entity_id, payload == "1");
    }
    if (type == "say" || type == "whisper" || type == "shout") {
        const auto event_type = type == "whisper" ? "chat_whisper"
                              : type == "shout" ? "chat_shout"
                                                : "chat";
        return (*runtime)->chat(entity_id, payload, event_type) != 0;
    }

    opengenesis::physics::Vec3 vector;
    if (!vector3(payload, vector)) return false;
    auto transform = entity->transform;
    if (type == "move") transform.position = vector;
    else if (type == "rotate") transform.rotation = vector;
    else if (type == "scale") transform.scale = vector;
    else return false;
    return (*runtime)->update_transform(entity_id, transform);
}
} // namespace

int main(int argc, char** argv) {
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);
    try {
        const auto config = opengenesis::config::TomlConfig::load_file(
            argc > 1 ? argv[1] : "config/world.toml");
        const auto node_id = config.get_string("node.id", "world-01");
        const auto node_name = config.get_string("node.name", "World Node 01");
        const auto public_endpoint = config.get_string("network.public_endpoint", "127.0.0.1:19100");
        const auto scene_address = config.get_string("network.scene_listen_address", "127.0.0.1");
        const auto scene_port_value = config.get_int("network.scene_port", 19100);
        if (scene_port_value < 1 || scene_port_value > 65535) {
            throw std::runtime_error("invalid network.scene_port");
        }
        const auto scene_port = static_cast<std::uint16_t>(scene_port_value);
        const auto [core_host, core_port] =
            parse_endpoint(config.get_string("core.endpoint", "127.0.0.1:19000"));
        const auto reconnect = std::chrono::seconds{config.get_int("core.reconnect_seconds", 2)};
        const auto lease = std::chrono::seconds{config.get_int("core.lease_seconds", 5)};
        const auto tick_hz = config.get_double("runtime.tick_hz", 45.0);
        const auto terrain_base = config.get_double("runtime.terrain_base_height", 21.0);
        const auto scene_ticket_secret = config.get_string(
            "security.scene_ticket_secret", "development-only-change-this-scene-ticket-secret");
        if (scene_ticket_secret.size() < 32) throw std::runtime_error("security.scene_ticket_secret must contain at least 32 bytes");
        const auto storage_root = std::filesystem::path{
            config.get_string("storage.root", "data/world")};
        const auto save_interval = std::chrono::seconds{
            std::max<std::int64_t>(1, config.get_int("storage.save_interval_seconds", 2))};

        const int count = static_cast<int>(config.get_int("regions.count", 1));
        std::vector<RegionConfig> region_configs;
        for (int index = 0; index < count; ++index) {
            const auto prefix = "region" + std::to_string(index) + ".";
            region_configs.push_back({
                .id = config.get_string(prefix + "id", "region-" + std::to_string(index + 1)),
                .name = config.get_string(prefix + "name", "Region " + std::to_string(index + 1)),
                .grid_x = static_cast<int>(config.get_int(prefix + "grid_x", 1000 + index)),
                .grid_y = static_cast<int>(config.get_int(prefix + "grid_y", 1000))});
        }

        std::vector<std::shared_ptr<world::RegionRuntime>> runtimes;
        std::vector<std::unique_ptr<world::RegionPersistence>> persistence;
        runtimes.reserve(region_configs.size());
        persistence.reserve(region_configs.size());
        for (const auto& region : region_configs) {
            auto runtime = std::make_shared<world::RegionRuntime>(region.id, tick_hz, terrain_base);
            auto store = std::make_unique<world::RegionPersistence>(storage_root / region.id);
            store->load(*runtime);
            runtime->start();
            runtimes.push_back(std::move(runtime));
            persistence.push_back(std::move(store));
        }

        world::SceneServer scene_server(scene_address, scene_port, runtimes, scene_ticket_secret,
                                       config.get_string("storage.parcels", "data/parcels.db"),
                                       config.get_string("storage.moderation", "data/moderation.db"));
        if (scene_ticket_secret == "development-only-change-this-scene-ticket-secret") {
            opengenesis::common::log(LogLevel::warning, "world.security",
                                     "Using development scene-ticket secret; replace it before network exposure");
        }
        scene_server.start();

        std::thread persistence_thread([&] {
            while (running) {
                std::this_thread::sleep_for(save_interval);
                for (std::size_t index = 0; index < runtimes.size(); ++index) {
                    try {
                        persistence[index]->save(*runtimes[index]);
                    } catch (const std::exception& error) {
                        opengenesis::common::log(LogLevel::warning, "world.persistence", error.what());
                    }
                }
            }
        });

        opengenesis::common::log(LogLevel::info, "world",
                                 "OpenGenesis World " OGL_VERSION " running " +
                                     std::to_string(runtimes.size()) + " region(s)");

        while (running) {
            try {
                opengenesis::common::log(LogLevel::info, "world.core",
                                         "Connecting to core " + core_host + ':' +
                                             std::to_string(core_port));
                auto socket = opengenesis::network::TcpSocket::connect(core_host, core_port);
                std::uint32_t request_id = 1;
                socket.send_frame({protocol::MessageType::hello, request_id,
                                   protocol::payload_from_string(
                                       "client=opengenesis-world\nprotocol=1\n")});
                if (socket.receive_frame().type != protocol::MessageType::hello_ack) {
                    throw std::runtime_error("HELLO rejected");
                }

                std::ostringstream registration;
                registration << "id=" << node_id << '\n' << "name=" << node_name << '\n'
                             << "endpoint=" << public_endpoint << '\n';
                socket.send_frame({protocol::MessageType::world_register, ++request_id,
                                   protocol::payload_from_string(registration.str())});
                const auto world_ack = socket.receive_frame();
                if (world_ack.type != protocol::MessageType::world_register_ack) {
                    throw std::runtime_error("world registration rejected");
                }
                const auto generation = std::stoull(
                    field(protocol::payload_as_string(world_ack), "generation"));
                opengenesis::common::log(LogLevel::info, "world.core",
                                         "Connected generation " + std::to_string(generation));

                for (const auto& region : region_configs) {
                    std::ostringstream body;
                    body << "id=" << region.id << '\n' << "name=" << region.name << '\n'
                         << "grid_x=" << region.grid_x << '\n' << "grid_y=" << region.grid_y << '\n';
                    socket.send_frame({protocol::MessageType::region_register, ++request_id,
                                       protocol::payload_from_string(body.str())});
                    if (socket.receive_frame().type != protocol::MessageType::region_register_ack) {
                        throw std::runtime_error("region registration rejected: " + region.id);
                    }
                    for (const char* state : {"starting", "online"}) {
                        socket.send_frame({protocol::MessageType::region_state_update, ++request_id,
                                           protocol::payload_from_string("id=" + region.id +
                                                                         "\nstate=" + state + "\n")});
                        if (socket.receive_frame().type != protocol::MessageType::region_state_ack) {
                            throw std::runtime_error("region state rejected: " + region.id);
                        }
                    }
                }

                auto next_lease = std::chrono::steady_clock::now();
                auto next_metrics = next_lease;
                auto next_script_poll = next_lease;
                while (running) {
                    const auto now = std::chrono::steady_clock::now();
                    if (now >= next_lease) {
                        socket.send_frame({protocol::MessageType::world_lease, ++request_id,
                                           protocol::payload_from_string(
                                               "node=" + node_id + "\ngeneration=" +
                                               std::to_string(generation) + "\n")});
                        if (socket.receive_frame().type != protocol::MessageType::world_lease_ack) {
                            throw std::runtime_error("lease rejected");
                        }
                        next_lease = now + lease;
                    }
                    if (now >= next_script_poll) {
                        for (const auto& runtime : runtimes) {
                            socket.send_frame({
                                protocol::MessageType::script_action_poll, ++request_id,
                                protocol::payload_from_string(
                                    "region=" + runtime->id() + "\n")});
                            const auto action = socket.receive_frame();
                            if (action.type != protocol::MessageType::script_action) {
                                throw std::runtime_error("script action poll rejected");
                            }
                            const auto action_body = protocol::payload_as_string(action);
                            if (field(action_body, "status") == "action" &&
                                !apply_script_action(runtimes, action_body)) {
                                opengenesis::common::log(
                                    LogLevel::warning, "world.script",
                                    "Rejected Script World Action " +
                                        field(action_body, "id"));
                            }
                        }
                        next_script_poll = now + std::chrono::milliseconds{100};
                    }
                    if (now >= next_metrics) {
                        for (const auto& runtime : runtimes) {
                            const auto metrics = runtime->metrics();
                            std::ostringstream body;
                            body << std::fixed << std::setprecision(2) << "id=" << runtime->id()
                                 << "\nticks=" << metrics.ticks << "\nentities=" << metrics.entities
                                 << "\navatars=" << metrics.avatars << "\nphysics_bodies="
                                 << metrics.physics_bodies << "\nscene_events=" << metrics.scene_events
                                 << "\nterrain_revision=" << metrics.terrain_revision << "\nsim_fps="
                                 << metrics.sim_fps << '\n';
                            socket.send_frame({protocol::MessageType::region_metrics, ++request_id,
                                               protocol::payload_from_string(body.str())});
                            if (socket.receive_frame().type != protocol::MessageType::region_metrics_ack) {
                                throw std::runtime_error("metrics rejected");
                            }
                            socket.send_frame({protocol::MessageType::presence_snapshot, ++request_id,
                                               protocol::payload_from_string(
                                                   presence_snapshot_payload(*runtime))});
                            if (socket.receive_frame().type !=
                                protocol::MessageType::presence_snapshot_ack) {
                                throw std::runtime_error("presence snapshot rejected");
                            }
                        }
                        next_metrics = now + std::chrono::seconds{2};
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds{100});
                }

                for (const auto& region : region_configs) {
                    for (const char* state : {"stopping", "offline"}) {
                        socket.send_frame({protocol::MessageType::region_state_update, ++request_id,
                                           protocol::payload_from_string("id=" + region.id +
                                                                         "\nstate=" + state + "\n")});
                        (void)socket.receive_frame();
                    }
                }
                socket.send_frame({protocol::MessageType::goodbye, ++request_id, {}});
            } catch (const std::exception& error) {
                if (!running) break;
                opengenesis::common::log(LogLevel::warning, "world.reconnect", error.what());
                std::this_thread::sleep_for(reconnect);
            }
        }

        scene_server.stop();
        if (persistence_thread.joinable()) persistence_thread.join();
        for (std::size_t index = 0; index < runtimes.size(); ++index) {
            runtimes[index]->stop();
            persistence[index]->save(*runtimes[index], true);
        }
        opengenesis::common::log(LogLevel::info, "world", "Shutdown complete");
        return 0;
    } catch (const std::exception& error) {
        opengenesis::common::log(LogLevel::error, "world", error.what());
        return 1;
    }
}
