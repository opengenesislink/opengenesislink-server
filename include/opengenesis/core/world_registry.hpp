#pragma once
#include <chrono>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>
#include <memory>

namespace opengenesis::storage { class DatabasePool; }
namespace opengenesis::core {
struct WorldNodeInfo {
    std::string id, name, endpoint, state{"offline"};
    std::uint64_t generation{0};
    std::chrono::system_clock::time_point registered_at{}, last_seen{};
};
class WorldRegistry final {
public:
    explicit WorldRegistry(std::string storage_path = {});
    explicit WorldRegistry(std::shared_ptr<storage::DatabasePool> database);
    WorldNodeInfo register_or_reconnect(std::string id,std::string name,std::string endpoint);
    bool touch(const std::string& id,std::uint64_t generation);
    bool mark_offline(const std::string& id,std::uint64_t generation);
    std::size_t expire_stale(std::chrono::seconds lease_timeout);
    [[nodiscard]] std::optional<WorldNodeInfo> find(const std::string& id) const;
    [[nodiscard]] std::vector<WorldNodeInfo> list() const;
    [[nodiscard]] std::size_t size() const;
private:
    void load(); void persist_locked() const;
    std::string storage_path_; std::shared_ptr<storage::DatabasePool> database_; mutable std::mutex mutex_; std::unordered_map<std::string,WorldNodeInfo> nodes_;
};
}
