#include "opengenesis/world/scene_server.hpp"

#include "opengenesis/common/log.hpp"
#include "opengenesis/network/tcp.hpp"
#include "opengenesis/protocol/frame.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace opengenesis::world {
namespace protocol = opengenesis::protocol;

namespace {
std::string field(const std::string& payload, const std::string& key) {
    std::istringstream input(payload);
    std::string line;
    while (std::getline(input, line)) {
        const auto split = line.find('=');
        if (split != std::string::npos && line.substr(0, split) == key) return line.substr(split + 1);
    }
    return {};
}

double number(const std::string& payload, const std::string& key, const double fallback) {
    const auto value = field(payload, key);
    if (value.empty()) return fallback;
    try {
        const double parsed = std::stod(value);
        return std::isfinite(parsed) ? parsed : fallback;
    } catch (...) {
        return fallback;
    }
}

std::uint64_t integer(const std::string& payload, const std::string& key, const std::uint64_t fallback = 0) {
    const auto value = field(payload, key);
    if (value.empty()) return fallback;
    try {
        return std::stoull(value);
    } catch (...) {
        return fallback;
    }
}

std::string clean(std::string value, const std::size_t max_length = 96) {
    value.erase(std::remove_if(value.begin(), value.end(), [](const char c) {
                    return c == '\n' || c == '\r' || c == '|';
                }),
                value.end());
    if (value.size() > max_length) value.resize(max_length);
    return value;
}

Transform transform_from_payload(const std::string& payload, const Transform& fallback = {}) {
    Transform transform = fallback;
    transform.position.x = number(payload, "x", fallback.position.x);
    transform.position.y = number(payload, "y", fallback.position.y);
    transform.position.z = number(payload, "z", fallback.position.z);
    transform.rotation.x = number(payload, "rx", fallback.rotation.x);
    transform.rotation.y = number(payload, "ry", fallback.rotation.y);
    transform.rotation.z = number(payload, "rz", fallback.rotation.z);
    transform.scale.x = std::max(0.01, number(payload, "sx", fallback.scale.x));
    transform.scale.y = std::max(0.01, number(payload, "sy", fallback.scale.y));
    transform.scale.z = std::max(0.01, number(payload, "sz", fallback.scale.z));
    return transform;
}

std::string serialize_snapshot(const RegionRuntime& region) {
    const auto entities = region.snapshot_entities();
    std::ostringstream output;
    output << std::fixed << std::setprecision(3)
           << "region=" << region.id() << '\n'
           << "sequence=" << region.latest_sequence() << '\n'
           << "terrain_width=" << region.terrain().width() << '\n'
           << "terrain_height=" << region.terrain().height() << '\n'
           << "terrain_cell_size=" << region.terrain().cell_size() << '\n'
           << "terrain_revision=" << region.terrain().revision() << '\n'
           << "entity_count=" << entities.size() << '\n';
    for (const auto& entity : entities) {
        const auto& t = entity.transform;
        output << "entity=" << entity.id << '|' << entity_kind_name(entity.kind) << '|'
               << clean(entity.name) << '|' << t.position.x << '|' << t.position.y << '|'
               << t.position.z << '|' << t.rotation.x << '|' << t.rotation.y << '|'
               << t.rotation.z << '|' << t.scale.x << '|' << t.scale.y << '|' << t.scale.z << '\n';
    }
    return output.str();
}

std::string serialize_events(const RegionRuntime& region, const std::uint64_t since) {
    const auto events = region.events_since(since);
    std::ostringstream output;
    output << std::fixed << std::setprecision(3)
           << "region=" << region.id() << '\n'
           << "from=" << since << '\n'
           << "latest=" << region.latest_sequence() << '\n'
           << "count=" << events.size() << '\n';
    for (const auto& event : events) {
        const auto& t = event.transform;
        output << "event=" << event.sequence << '|' << event.type << '|' << event.entity_id << '|'
               << clean(event.text, 512) << '|' << t.position.x << '|' << t.position.y << '|'
               << t.position.z << '|' << t.rotation.x << '|' << t.rotation.y << '|'
               << t.rotation.z << '|' << t.scale.x << '|' << t.scale.y << '|' << t.scale.z << '\n';
    }
    return output.str();
}

void handle_client(opengenesis::network::TcpSocket socket,
                   std::unordered_map<std::string, std::shared_ptr<RegionRuntime>> regions) {
    std::shared_ptr<RegionRuntime> region;
    std::uint64_t avatar_id = 0;
    try {
        const auto hello = socket.receive_frame();
        if (hello.type != protocol::MessageType::hello) throw std::runtime_error("scene HELLO required");
        socket.send_frame({protocol::MessageType::hello_ack, hello.request_id,
                           protocol::payload_from_string("protocol=1\nserver=opengenesis-scene\n")});

        const auto join = socket.receive_frame();
        if (join.type != protocol::MessageType::scene_join) throw std::runtime_error("SCENE_JOIN required");
        const auto join_body = protocol::payload_as_string(join);
        const auto region_id = field(join_body, "region");
        const auto it = regions.find(region_id);
        if (it == regions.end()) {
            socket.send_frame({protocol::MessageType::error, join.request_id,
                               protocol::payload_from_string("reason=unknown-region\n")});
            return;
        }
        region = it->second;
        Transform spawn{};
        spawn.position = {number(join_body, "x", 128.0), number(join_body, "y", 128.0),
                          number(join_body, "z", 0.0)};
        avatar_id = region->spawn_avatar(clean(field(join_body, "avatar")).empty()
                                             ? std::string("Development Avatar")
                                             : clean(field(join_body, "avatar")),
                                         spawn);
        std::ostringstream joined;
        joined << "status=joined\nregion=" << region->id() << "\navatar_id=" << avatar_id
               << "\nsequence=" << region->latest_sequence() << "\nterrain_revision="
               << region->terrain().revision() << '\n';
        socket.send_frame({protocol::MessageType::scene_join_ack, join.request_id,
                           protocol::payload_from_string(joined.str())});

        while (true) {
            const auto frame = socket.receive_frame();
            const auto body = protocol::payload_as_string(frame);
            if (frame.type == protocol::MessageType::scene_snapshot_request) {
                socket.send_frame({protocol::MessageType::scene_snapshot, frame.request_id,
                                   protocol::payload_from_string(serialize_snapshot(*region))});
            } else if (frame.type == protocol::MessageType::scene_events_request) {
                socket.send_frame({protocol::MessageType::scene_events, frame.request_id,
                                   protocol::payload_from_string(
                                       serialize_events(*region, integer(body, "since"))) });
            } else if (frame.type == protocol::MessageType::entity_create) {
                auto transform = transform_from_payload(body);
                const auto name = clean(field(body, "name"));
                const bool physical = field(body, "physical") != "false";
                const auto id = region->spawn_object(name.empty() ? "Object" : name, transform, physical);
                socket.send_frame({protocol::MessageType::entity_create_ack, frame.request_id,
                                   protocol::payload_from_string("status=created\nid=" + std::to_string(id) + "\n")});
            } else if (frame.type == protocol::MessageType::entity_update) {
                const auto id = integer(body, "id");
                const auto current = region->entity(id);
                const bool ok = current.has_value() &&
                                region->update_transform(id, transform_from_payload(body, current->transform));
                socket.send_frame({ok ? protocol::MessageType::entity_update_ack : protocol::MessageType::error,
                                   frame.request_id,
                                   protocol::payload_from_string(ok ? "status=updated\n"
                                                                    : "reason=unknown-entity\n")});
            } else if (frame.type == protocol::MessageType::entity_delete) {
                const auto id = integer(body, "id");
                const bool ok = id != avatar_id && region->remove_entity(id);
                socket.send_frame({ok ? protocol::MessageType::entity_delete_ack : protocol::MessageType::error,
                                   frame.request_id,
                                   protocol::payload_from_string(ok ? "status=deleted\n"
                                                                    : "reason=delete-rejected\n")});
            } else if (frame.type == protocol::MessageType::chat_send) {
                const auto sequence = region->chat(avatar_id, field(body, "text"));
                socket.send_frame({sequence ? protocol::MessageType::chat_event : protocol::MessageType::error,
                                   frame.request_id,
                                   protocol::payload_from_string(sequence
                                                                     ? "status=sent\nsequence=" +
                                                                           std::to_string(sequence) + "\n"
                                                                     : "reason=chat-rejected\n")});
            } else if (frame.type == protocol::MessageType::terrain_sample_request) {
                const double x = number(body, "x", 0.0);
                const double y = number(body, "y", 0.0);
                std::ostringstream sample;
                sample << std::fixed << std::setprecision(3) << "x=" << x << "\ny=" << y
                       << "\nheight=" << region->terrain().sample(x, y) << "\nrevision="
                       << region->terrain().revision() << '\n';
                socket.send_frame({protocol::MessageType::terrain_sample, frame.request_id,
                                   protocol::payload_from_string(sample.str())});
            } else if (frame.type == protocol::MessageType::terrain_set_request) {
                const auto x = integer(body, "x");
                const auto y = integer(body, "y");
                const double height = number(body, "height", region->terrain().base_height());
                const bool ok = region->set_terrain_height(static_cast<std::size_t>(x),
                                                           static_cast<std::size_t>(y), height);
                socket.send_frame({ok ? protocol::MessageType::terrain_set_ack : protocol::MessageType::error,
                                   frame.request_id,
                                   protocol::payload_from_string(ok
                                                                     ? "status=updated\nrevision=" +
                                                                           std::to_string(region->terrain().revision()) + "\n"
                                                                     : "reason=terrain-update-rejected\n")});
            } else if (frame.type == protocol::MessageType::ping) {
                socket.send_frame({protocol::MessageType::pong, frame.request_id, frame.payload});
            } else if (frame.type == protocol::MessageType::goodbye) {
                break;
            } else {
                socket.send_frame({protocol::MessageType::error, frame.request_id,
                                   protocol::payload_from_string("reason=unsupported-scene-message\n")});
            }
        }
    } catch (const std::exception& error) {
        common::log(common::LogLevel::debug, "world.scene.client", error.what());
    }
    if (region && avatar_id != 0) region->remove_entity(avatar_id);
}
} // namespace

SceneServer::SceneServer(std::string address, const std::uint16_t port,
                         const std::vector<std::shared_ptr<RegionRuntime>>& regions)
    : address_(std::move(address)), port_(port) {
    for (const auto& region : regions) regions_.emplace(region->id(), region);
}

SceneServer::~SceneServer() { stop(); }

void SceneServer::start() {
    if (running_.exchange(true)) return;
    thread_ = std::thread(&SceneServer::run, this);
}

void SceneServer::stop() {
    if (!running_.exchange(false)) return;
    if (thread_.joinable()) thread_.join();
}

void SceneServer::run() {
    try {
        opengenesis::network::TcpListener listener(address_, port_);
        common::log(common::LogLevel::info, "world.scene",
                    "Scene endpoint listening on " + address_ + ":" + std::to_string(port_));
        while (running_) {
            auto socket = listener.accept_for(std::chrono::milliseconds{250});
            if (!socket) continue;
            auto regions = regions_;
            std::thread(handle_client, std::move(*socket), std::move(regions)).detach();
        }
    } catch (const std::exception& error) {
        if (running_) common::log(common::LogLevel::error, "world.scene", error.what());
    }
}

} // namespace opengenesis::world
