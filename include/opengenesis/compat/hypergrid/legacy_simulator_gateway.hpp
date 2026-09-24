#pragma once

#include "opengenesis/compat/hypergrid/legacy_lludp.hpp"
#include "opengenesis/compat/hypergrid/service.hpp"
#include "opengenesis/compat/hypergrid/session_store.hpp"
#include "opengenesis/platform/socket.hpp"

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace opengenesis::compat::hypergrid {

enum class LegacyCircuitState : std::uint8_t {
    circuit_bound,
    movement_completed
};

struct LegacyCircuitBinding {
    std::string session_id;
    std::string agent_id;
    std::string region_id;
    std::string peer_ip;
    std::uint16_t peer_port{0};
    std::uint32_t circuit_code{0};
    LegacyCircuitState state{LegacyCircuitState::circuit_bound};
};

enum class LegacyDatagramKind : std::uint8_t {
    rejected,
    ignored,
    use_circuit,
    complete_movement,
    ping
};

struct LegacyDatagramResult {
    LegacyDatagramKind kind{LegacyDatagramKind::ignored};
    std::string reason;
    std::string session_id;
    std::vector<std::vector<std::uint8_t>> replies;
};

class LegacyCircuitRouter final {
public:
    LegacyCircuitRouter(std::shared_ptr<HypergridService> service,
                        std::shared_ptr<HypergridSessionStore> sessions);

    [[nodiscard]] LegacyDatagramResult handle(
        std::span<const std::uint8_t> packet,
        std::string peer_ip,
        std::uint16_t peer_port);

    [[nodiscard]] std::vector<LegacyCircuitBinding> bindings() const;
    void remove_session(std::string_view session_id);

private:
    [[nodiscard]] static std::string endpoint_key(
        std::string_view peer_ip,
        std::uint16_t peer_port);

    std::shared_ptr<HypergridService> service_;
    std::shared_ptr<HypergridSessionStore> sessions_;
    mutable std::mutex mutex_;
    std::unordered_map<std::string, LegacyCircuitBinding> by_endpoint_;
    std::uint32_t server_sequence_{1};
};

class LegacySimulatorUdpGateway final {
public:
    LegacySimulatorUdpGateway(
        std::string bind_address,
        std::uint16_t port,
        std::shared_ptr<LegacyCircuitRouter> router);
    ~LegacySimulatorUdpGateway();

    LegacySimulatorUdpGateway(const LegacySimulatorUdpGateway&) = delete;
    LegacySimulatorUdpGateway& operator=(const LegacySimulatorUdpGateway&) = delete;

    void start();
    void stop();
    [[nodiscard]] bool ready() const noexcept;

private:
    void run();

    std::string bind_address_;
    std::uint16_t port_;
    std::shared_ptr<LegacyCircuitRouter> router_;
    std::atomic_bool running_{false};
    std::atomic_bool ready_{false};
    std::thread thread_;
    platform::SocketHandle socket_{platform::kInvalidSocket};
};

} // namespace opengenesis::compat::hypergrid
