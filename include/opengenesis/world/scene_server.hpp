#pragma once

#include "opengenesis/world/region_runtime.hpp"

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace opengenesis::world {

class SceneAuthContext;

class SceneServer final {
public:
    SceneServer(std::string address, std::uint16_t port,
                const std::vector<std::shared_ptr<RegionRuntime>>& regions,
                std::string ticket_secret);
    ~SceneServer();
    SceneServer(const SceneServer&) = delete;
    SceneServer& operator=(const SceneServer&) = delete;

    void start();
    void stop();

private:
    void run();

    std::string address_;
    std::uint16_t port_;
    std::unordered_map<std::string, std::shared_ptr<RegionRuntime>> regions_;
    std::shared_ptr<SceneAuthContext> auth_;
    std::atomic_bool running_{false};
    std::thread thread_;
};

} // namespace opengenesis::world
