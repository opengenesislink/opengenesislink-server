#pragma once
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>
namespace opengenesis::core {
struct NodeSession { std::string node_id; std::uint64_t generation{0}; std::chrono::steady_clock::time_point last_lease{}; };
class NodeSessions final {
public:
    void open(std::string node_id,std::uint64_t generation);
    bool renew(const std::string& node_id,std::uint64_t generation);
    void close(const std::string& node_id,std::uint64_t generation);
    [[nodiscard]] std::vector<NodeSession> expired(std::chrono::seconds timeout) const;
private: mutable std::mutex mutex_; std::unordered_map<std::string,NodeSession> sessions_;
};
}
