#include "opengenesis/compat/hypergrid/legacy_simulator_gateway.hpp"

#include "opengenesis/common/log.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <stdexcept>
#include <sstream>
#include <string>
#include <utility>

namespace opengenesis::compat::hypergrid {
namespace {

std::string numeric_host(const sockaddr* address,
                         const platform::SocketLength length) {
    char host[NI_MAXHOST]{};
    if (::getnameinfo(address, length, host, sizeof(host), nullptr, 0,
                      NI_NUMERICHOST) != 0) {
        return {};
    }
    return host;
}

std::array<float, 3> parse_start_position(std::string text) {
    std::array<float, 3> value{128.0F, 128.0F, 25.0F};
    for (char& c : text) {
        if (c == '<' || c == '>' || c == ',') c = ' ';
    }
    std::istringstream input{text};
    float x = value[0];
    float y = value[1];
    float z = value[2];
    if (input >> x >> y >> z) {
        value = {x, y, z};
    }
    return value;
}

std::uint16_t numeric_port(const sockaddr* address) {
    if (address->sa_family == AF_INET) {
        const auto* ipv4 = reinterpret_cast<const sockaddr_in*>(address);
        return ntohs(ipv4->sin_port);
    }
    if (address->sa_family == AF_INET6) {
        const auto* ipv6 = reinterpret_cast<const sockaddr_in6*>(address);
        return ntohs(ipv6->sin6_port);
    }
    return 0U;
}

std::ptrdiff_t receive_datagram(
    const platform::SocketHandle socket,
    void* data,
    const std::size_t size,
    sockaddr_storage& peer,
    platform::SocketLength& peer_length) {
#ifdef _WIN32
    const auto count = ::recvfrom(
        socket, static_cast<char*>(data), static_cast<int>(size), 0,
        reinterpret_cast<sockaddr*>(&peer), &peer_length);
    return count == SOCKET_ERROR ? -1 : static_cast<std::ptrdiff_t>(count);
#else
    return static_cast<std::ptrdiff_t>(::recvfrom(
        socket, data, size, 0,
        reinterpret_cast<sockaddr*>(&peer), &peer_length));
#endif
}

std::ptrdiff_t send_datagram(
    const platform::SocketHandle socket,
    const std::span<const std::uint8_t> data,
    const sockaddr_storage& peer,
    const platform::SocketLength peer_length) {
#ifdef _WIN32
    const auto count = ::sendto(
        socket, reinterpret_cast<const char*>(data.data()),
        static_cast<int>(data.size()), 0,
        reinterpret_cast<const sockaddr*>(&peer), peer_length);
    return count == SOCKET_ERROR ? -1 : static_cast<std::ptrdiff_t>(count);
#else
    return static_cast<std::ptrdiff_t>(::sendto(
        socket, data.data(), data.size(), 0,
        reinterpret_cast<const sockaddr*>(&peer), peer_length));
#endif
}

} // namespace

LegacyCircuitRouter::LegacyCircuitRouter(
    std::shared_ptr<HypergridService> service,
    std::shared_ptr<HypergridSessionStore> sessions)
    : service_(std::move(service)),
      sessions_(std::move(sessions)) {
    if (!service_ || !sessions_) {
        throw std::invalid_argument(
            "LegacyCircuitRouter dependencies required");
    }
}

std::string LegacyCircuitRouter::endpoint_key(
    const std::string_view peer_ip,
    const std::uint16_t peer_port) {
    return std::string{peer_ip} + ":" + std::to_string(peer_port);
}

LegacyDatagramResult LegacyCircuitRouter::handle(
    const std::span<const std::uint8_t> packet,
    std::string peer_ip,
    const std::uint16_t peer_port) {
    LegacyDatagramResult result;
    std::string reason;
    const auto header = lludp::parse_header(packet, reason);
    if (!header) {
        result.kind = LegacyDatagramKind::rejected;
        result.reason = std::move(reason);
        return result;
    }

    const auto maybe_ack = [&]() {
        if (header->reliable) {
            result.replies.push_back(
                lludp::build_packet_ack(header->sequence));
        }
    };

    if (header->frequency == lludp::Frequency::low &&
        header->id == 3U) {
        const auto handshake =
            lludp::parse_use_circuit_code(packet, reason);
        if (!handshake) {
            result.kind = LegacyDatagramKind::rejected;
            result.reason = std::move(reason);
            return result;
        }

        const auto visitor = sessions_->foreign(handshake->session_id);
        if (!visitor || !visitor->verified ||
            visitor->agent_id != handshake->agent_id ||
            visitor->circuit_code != handshake->circuit_code) {
            result.kind = LegacyDatagramKind::rejected;
            result.reason = "legacy-circuit-not-authorized";
            return result;
        }
        const auto region = service_->region(
            visitor->destination_region);
        if (!region || region->state != "online") {
            result.kind = LegacyDatagramKind::rejected;
            result.reason = "legacy-circuit-region-unavailable";
            return result;
        }

        LegacyCircuitBinding binding{
            .session_id = visitor->session_id,
            .agent_id = visitor->agent_id,
            .region_id = visitor->destination_region,
            .peer_ip = std::move(peer_ip),
            .peer_port = peer_port,
            .circuit_code = visitor->circuit_code,
            .state = LegacyCircuitState::circuit_bound};
        {
            std::scoped_lock lock(mutex_);
            by_endpoint_[endpoint_key(binding.peer_ip, peer_port)] =
                binding;
        }
        result.kind = LegacyDatagramKind::use_circuit;
        result.session_id = visitor->session_id;
        maybe_ack();

        std::uint32_t sequence = 0U;
        {
            std::scoped_lock lock(mutex_);
            sequence = server_sequence_++;
        }
        result.replies.push_back(
            lludp::build_region_handshake(
                sequence,
                {.region_id =
                     HypergridService::legacy_region_uuid(region->id),
                 .region_name = region->name,
                 .region_handle =
                     HypergridService::legacy_region_handle(
                         region->grid_x, region->grid_y),
                 .water_height = 20.0F,
                 .sim_access = 21U}));
        return result;
    }

    if (header->frequency == lludp::Frequency::low &&
        header->id == 249U) {
        const auto movement =
            lludp::parse_complete_agent_movement(packet, reason);
        if (!movement) {
            result.kind = LegacyDatagramKind::rejected;
            result.reason = std::move(reason);
            return result;
        }

        const auto key = endpoint_key(peer_ip, peer_port);
        std::uint32_t sequence = 0U;
        {
            std::scoped_lock lock(mutex_);
            const auto it = by_endpoint_.find(key);
            if (it == by_endpoint_.end() ||
                it->second.session_id != movement->session_id ||
                it->second.agent_id != movement->agent_id ||
                it->second.circuit_code != movement->circuit_code) {
                result.kind = LegacyDatagramKind::rejected;
                result.reason = "legacy-movement-circuit-mismatch";
                return result;
            }
            it->second.state =
                LegacyCircuitState::movement_completed;
            sequence = server_sequence_++;
        }

        result.kind = LegacyDatagramKind::complete_movement;
        result.session_id = movement->session_id;
        if (header->reliable) {
            result.replies.push_back(
                lludp::build_packet_ack(header->sequence));
        }

        const auto visitor =
            sessions_->foreign(movement->session_id);
        const auto region =
            visitor ? service_->region(visitor->destination_region)
                    : std::nullopt;
        if (!visitor || !region) {
            result.kind = LegacyDatagramKind::rejected;
            result.reason =
                "legacy-movement-region-state-missing";
            result.replies.clear();
            return result;
        }
        const auto position =
            parse_start_position(visitor->start_pos);
        const auto now =
            std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch())
                .count();
        result.replies.push_back(
            lludp::build_agent_movement_complete(
                sequence,
                {.agent_id = movement->agent_id,
                 .session_id = movement->session_id,
                 .x = position[0],
                 .y = position[1],
                 .z = position[2],
                 .look_x = 0.0F,
                 .look_y = 1.0F,
                 .look_z = 0.0F,
                 .region_handle =
                     HypergridService::legacy_region_handle(
                         region->grid_x, region->grid_y),
                 .timestamp = static_cast<std::uint32_t>(
                     std::max<std::int64_t>(0, now)),
                 .channel_version =
                     "OpenGenesisLINK Hypergrid"}));
        return result;
    }

    if (header->frequency == lludp::Frequency::high &&
        header->id == 1U) {
        if (header->body_offset >= header->body_end) {
            result.kind = LegacyDatagramKind::rejected;
            result.reason = "legacy-ping-body-invalid";
            return result;
        }
        const auto key = endpoint_key(peer_ip, peer_port);
        {
            std::scoped_lock lock(mutex_);
            if (!by_endpoint_.contains(key)) {
                result.kind = LegacyDatagramKind::rejected;
                result.reason = "legacy-ping-circuit-unbound";
                return result;
            }
            result.replies.push_back(
                lludp::build_complete_ping_check(
                    server_sequence_++, packet[header->body_offset]));
        }
        result.kind = LegacyDatagramKind::ping;
        return result;
    }

    result.kind = LegacyDatagramKind::ignored;
    result.reason = "legacy-packet-not-yet-bridged";
    return result;
}

std::vector<LegacyCircuitBinding> LegacyCircuitRouter::bindings() const {
    std::scoped_lock lock(mutex_);
    std::vector<LegacyCircuitBinding> result;
    result.reserve(by_endpoint_.size());
    for (const auto& [key, binding] : by_endpoint_) {
        (void)key;
        result.push_back(binding);
    }
    return result;
}

void LegacyCircuitRouter::remove_session(const std::string_view session_id) {
    std::scoped_lock lock(mutex_);
    for (auto it = by_endpoint_.begin(); it != by_endpoint_.end();) {
        if (it->second.session_id == session_id) {
            it = by_endpoint_.erase(it);
        } else {
            ++it;
        }
    }
}

LegacySimulatorUdpGateway::LegacySimulatorUdpGateway(
    std::string bind_address,
    const std::uint16_t port,
    std::shared_ptr<LegacyCircuitRouter> router)
    : bind_address_(std::move(bind_address)),
      port_(port),
      router_(std::move(router)) {
    if (bind_address_.empty() || port_ == 0U || !router_) {
        throw std::invalid_argument(
            "LegacySimulatorUdpGateway configuration invalid");
    }
}

LegacySimulatorUdpGateway::~LegacySimulatorUdpGateway() {
    stop();
}

void LegacySimulatorUdpGateway::start() {
    if (running_.exchange(true)) return;
    thread_ = std::thread(&LegacySimulatorUdpGateway::run, this);
}

void LegacySimulatorUdpGateway::stop() {
    if (!running_.exchange(false)) return;
    if (platform::socket_valid(socket_)) {
        platform::shutdown_socket(socket_);
        platform::close_socket(socket_);
    }
    if (thread_.joinable()) thread_.join();
    ready_ = false;
}

bool LegacySimulatorUdpGateway::ready() const noexcept {
    return ready_.load();
}

void LegacySimulatorUdpGateway::run() {
    try {
        platform::initialize_sockets();
        addrinfo hints{};
        hints.ai_socktype = SOCK_DGRAM;
        hints.ai_family = AF_UNSPEC;
        hints.ai_flags = AI_PASSIVE;
        addrinfo* results = nullptr;
        const auto port_text = std::to_string(port_);
        if (::getaddrinfo(bind_address_.c_str(), port_text.c_str(),
                          &hints, &results) != 0) {
            throw std::runtime_error(
                "Legacy simulator UDP getaddrinfo failed");
        }

        for (auto* current = results; current; current = current->ai_next) {
            socket_ = ::socket(
                current->ai_family,
                current->ai_socktype,
                current->ai_protocol);
            if (!platform::socket_valid(socket_)) continue;
            platform::set_reuse_address(socket_);
            if (::bind(
                    socket_, current->ai_addr,
                    static_cast<platform::SocketLength>(
                        current->ai_addrlen)) == 0) {
                break;
            }
            platform::close_socket(socket_);
        }
        ::freeaddrinfo(results);
        if (!platform::socket_valid(socket_)) {
            throw std::runtime_error(
                "Legacy simulator UDP bind failed");
        }

        platform::set_socket_timeouts(
            socket_, std::chrono::milliseconds{500});
        ready_ = true;
        common::log(
            common::LogLevel::info,
            "compat.hypergrid.lludp",
            "Legacy simulator UDP gateway on " +
                bind_address_ + ":" + port_text);

        std::array<std::uint8_t, 8192> buffer{};
        while (running_) {
            sockaddr_storage peer{};
            platform::SocketLength peer_length =
                static_cast<platform::SocketLength>(sizeof(peer));
            const auto count = receive_datagram(
                socket_, buffer.data(), buffer.size(),
                peer, peer_length);
            if (count <= 0) continue;

            const auto host = numeric_host(
                reinterpret_cast<const sockaddr*>(&peer),
                peer_length);
            const auto port = numeric_port(
                reinterpret_cast<const sockaddr*>(&peer));
            if (host.empty() || port == 0U) continue;

            const auto result = router_->handle(
                std::span<const std::uint8_t>{
                    buffer.data(),
                    static_cast<std::size_t>(count)},
                host, port);
            for (const auto& reply : result.replies) {
                (void)send_datagram(
                    socket_, reply, peer, peer_length);
            }
        }
    } catch (const std::exception& error) {
        if (running_) {
            common::log(
                common::LogLevel::error,
                "compat.hypergrid.lludp",
                error.what());
        }
    }
    ready_ = false;
}

} // namespace opengenesis::compat::hypergrid
