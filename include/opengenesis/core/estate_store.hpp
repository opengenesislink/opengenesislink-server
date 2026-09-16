#pragma once
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace opengenesis::core {

struct EstateInfo {
    std::string id;
    std::string name;
    std::string owner_user_id;
    std::vector<std::string> manager_user_ids;
    std::int64_t created_unix{0};
};

struct RegionEstatePolicy {
    std::string region_id;
    std::string estate_id;
    bool public_access{true};
    bool allow_fly{true};
    bool allow_scripts{true};
    bool allow_voice{true};
    std::uint32_t max_agents{100};
    std::uint8_t maturity{0};
    double landing_x{128.0};
    double landing_y{128.0};
    double landing_z{0.0};
    std::int64_t updated_unix{0};
};

class EstateStore final {
public:
    explicit EstateStore(std::string path);
    [[nodiscard]] std::optional<EstateInfo> create(std::string owner, std::string name, std::string& reason);
    bool add_manager(std::string_view actor, std::string_view estate_id, std::string user_id, std::string& reason);
    bool remove_manager(std::string_view actor, std::string_view estate_id, std::string_view user_id, std::string& reason);
    bool attach_region(std::string_view actor, std::string_view estate_id, std::string region_id, std::string& reason);
    bool update_region_policy(std::string_view actor, const RegionEstatePolicy& policy, std::string& reason);
    [[nodiscard]] std::optional<EstateInfo> find(std::string_view id) const;
    [[nodiscard]] std::optional<RegionEstatePolicy> policy_for_region(std::string_view region_id) const;
    [[nodiscard]] std::vector<EstateInfo> list_for_user(std::string_view user_id) const;
    [[nodiscard]] std::vector<RegionEstatePolicy> regions_for_estate(std::string_view estate_id) const;
    [[nodiscard]] bool can_manage(std::string_view estate_id, std::string_view user_id) const;
    [[nodiscard]] std::size_t count() const;
private:
    void load();
    void persist_locked() const;
    std::string path_;
    mutable std::mutex mutex_;
    std::unordered_map<std::string, EstateInfo> estates_;
    std::unordered_map<std::string, RegionEstatePolicy> policies_;
};

} // namespace opengenesis::core
