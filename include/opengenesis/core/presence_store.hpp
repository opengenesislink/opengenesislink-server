#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace opengenesis::core {

struct PresenceInfo {
    std::string user_id;
    std::string display_name;
    std::string region_id;
    std::string node_id;
    std::uint64_t node_generation{0};
    std::uint64_t entity_id{0};
    double x{0.0};
    double y{0.0};
    double z{0.0};
    std::int64_t updated_unix{0};
};

class PresenceStore final {
public:
    void replace_region_snapshot(std::string region_id, std::string node_id,
                                 std::uint64_t generation, std::vector<PresenceInfo> presences);
    void mark_node_offline(std::string_view node_id, std::uint64_t generation);
    void mark_region_offline(std::string_view region_id);
    [[nodiscard]] std::optional<PresenceInfo> find_user(std::string_view user_id) const;
    [[nodiscard]] std::vector<PresenceInfo> list() const;
    [[nodiscard]] std::size_t count() const;
    [[nodiscard]] std::size_t count_region(std::string_view region_id) const;

private:
    mutable std::mutex mutex_;
    std::unordered_map<std::string, PresenceInfo> by_user_;
};

} // namespace opengenesis::core
