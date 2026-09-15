#include "opengenesis/common/log.hpp"
#include "opengenesis/config/toml_config.hpp"
#include "opengenesis/network/tcp.hpp"
#include "opengenesis/protocol/frame.hpp"
#include "opengenesis/world/region_runtime.hpp"
#include "opengenesis/world/scene_server.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
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

        std::vector<std::shared_ptr<opengenesis::world::RegionRuntime>> runtimes;
        runtimes.reserve(region_configs.size());
        for (const auto& region : region_configs) {
            auto runtime = std::make_shared<opengenesis::world::RegionRuntime>(region.id, tick_hz, terrain_base);
            runtime->start();
            runtimes.push_back(std::move(runtime));
        }

        opengenesis::world::SceneServer scene_server(scene_address, scene_port, runtimes);
        scene_server.start();
        opengenesis::common::log(LogLevel::info, "world",
                                 "OpenGenesis World " OGL_VERSION " running " +
                                     std::to_string(runtimes.size()) + " region(s)");

        while (running) {
            try {
                opengenesis::common::log(LogLevel::info, "world.core",
                                         "Connecting to core " + core_host + ":" +
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

                for (std::size_t index = 0; index < region_configs.size(); ++index) {
                    const auto& region = region_configs[index];
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
        for (const auto& runtime : runtimes) runtime->stop();
        opengenesis::common::log(LogLevel::info, "world", "Shutdown complete");
        return 0;
    } catch (const std::exception& error) {
        opengenesis::common::log(LogLevel::error, "world", error.what());
        return 1;
    }
}
