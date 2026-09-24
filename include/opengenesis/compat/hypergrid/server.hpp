#pragma once

#include "opengenesis/compat/hypergrid/home_verifier.hpp"
#include "opengenesis/compat/hypergrid/friends_adapter.hpp"
#include "opengenesis/compat/hypergrid/asset_adapter.hpp"
#include "opengenesis/compat/hypergrid/im_adapter.hpp"
#include "opengenesis/compat/hypergrid/inventory_adapter.hpp"
#include "opengenesis/compat/hypergrid/appearance_adapter.hpp"
#include "opengenesis/compat/hypergrid/service.hpp"
#include "opengenesis/compat/hypergrid/session_store.hpp"
#include "opengenesis/compat/hypergrid/legacy_simulator_gateway.hpp"
#include "opengenesis/platform/socket.hpp"

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>

namespace opengenesis::compat::hypergrid {

class HypergridServer final {
public:
    HypergridServer(std::string address,
                    std::uint16_t port,
                    std::shared_ptr<HypergridService> service,
                    std::shared_ptr<HypergridSessionStore> sessions,
                    std::shared_ptr<IHypergridHomeVerifier> verifier,
                    std::shared_ptr<HypergridFriendsAdapter> friends,
                    std::shared_ptr<HypergridAssetAdapter> assets,
                    std::shared_ptr<HypergridInstantMessageAdapter> instant_messages,
                    std::shared_ptr<HypergridInventoryAdapter> inventory,
                    std::shared_ptr<HypergridAppearanceAdapter> appearance);
    ~HypergridServer();

    HypergridServer(const HypergridServer&) = delete;
    HypergridServer& operator=(const HypergridServer&) = delete;

    void start();
    void stop();

private:
    void run();

    std::string address_;
    std::uint16_t port_;
    std::shared_ptr<HypergridService> service_;
    std::shared_ptr<HypergridSessionStore> sessions_;
    std::shared_ptr<IHypergridHomeVerifier> verifier_;
    std::shared_ptr<HypergridFriendsAdapter> friends_;
    std::shared_ptr<HypergridAssetAdapter> assets_;
    std::shared_ptr<HypergridInstantMessageAdapter> instant_messages_;
    std::shared_ptr<HypergridInventoryAdapter> inventory_;
    std::shared_ptr<HypergridAppearanceAdapter> appearance_;
    std::shared_ptr<LegacyCircuitRouter> legacy_router_;
    std::unique_ptr<LegacySimulatorUdpGateway> legacy_udp_;
    std::atomic_bool running_{false};
    std::thread thread_;
    platform::SocketHandle listen_fd_{platform::kInvalidSocket};
};

} // namespace opengenesis::compat::hypergrid
