#pragma once

#include "opengenesis/core/identity_store.hpp"
#include "opengenesis/core/region_registry.hpp"
#include "opengenesis/core/session_store.hpp"
#include "opengenesis/core/world_registry.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>

namespace opengenesis::core {

class AdminHttpServer final {
public:
    AdminHttpServer(std::string address, std::uint16_t port,
                    std::shared_ptr<WorldRegistry> worlds,
                    std::shared_ptr<RegionRegistry> regions,
                    std::shared_ptr<IdentityStore> identities,
                    std::shared_ptr<SessionStore> sessions);
    ~AdminHttpServer();

    void start();
    void stop();

private:
    void run();

    std::string address_;
    std::uint16_t port_;
    std::shared_ptr<WorldRegistry> worlds_;
    std::shared_ptr<RegionRegistry> regions_;
    std::shared_ptr<IdentityStore> identities_;
    std::shared_ptr<SessionStore> sessions_;
    std::chrono::steady_clock::time_point started_at_{std::chrono::steady_clock::now()};
    std::atomic_bool running_{false};
    std::thread thread_;
    int listen_fd_{-1};
};

} // namespace opengenesis::core
