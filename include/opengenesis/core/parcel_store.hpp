#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace opengenesis::core {

struct ParcelInfo {
    std::string id;
    std::string region_id;
    std::string name;
    std::string owner_user_id;
    std::string group_id;
    std::uint16_t x1{0};
    std::uint16_t y1{0};
    std::uint16_t x2{255};
    std::uint16_t y2{255};
    bool public_entry{true};
    bool public_build{false};
    bool group_build{true};
    bool group_terraform{false};
    std::int64_t created_unix{0};
    std::int64_t updated_unix{0};
};

struct ParcelAccessEntry {
    std::string parcel_id;
    std::string user_id;
    bool allowed{true};
    std::int64_t updated_unix{0};
};

class ParcelStore final {
public:
    explicit ParcelStore(std::string path);

    void reload();

    [[nodiscard]] std::optional<ParcelInfo> create(
        std::string owner,
        std::string region,
        std::string name,
        std::uint16_t x1,
        std::uint16_t y1,
        std::uint16_t x2,
        std::uint16_t y2,
        std::string& reason);

    bool update_policy(
        std::string_view actor,
        std::string_view parcel_id,
        std::string group_id,
        bool public_entry,
        bool public_build,
        bool group_build,
        bool group_terraform,
        std::string& reason);

    bool set_access(
        std::string_view actor,
        std::string_view parcel_id,
        std::string user_id,
        bool allowed,
        std::string& reason);

    bool remove_access(
        std::string_view actor,
        std::string_view parcel_id,
        std::string_view user_id,
        std::string& reason);

    [[nodiscard]] std::vector<ParcelAccessEntry> access_list(
        std::string_view parcel_id) const;

    [[nodiscard]] std::optional<ParcelInfo> find(
        std::string_view id) const;
    [[nodiscard]] std::optional<ParcelInfo> at(
        std::string_view region,
        double x,
        double y) const;
    [[nodiscard]] std::vector<ParcelInfo> list_region(
        std::string_view region) const;
    [[nodiscard]] std::vector<ParcelInfo> list_owner(
        std::string_view owner) const;
    [[nodiscard]] std::size_t count() const;

    [[nodiscard]] bool can_enter(
        std::string_view region,
        double x,
        double y,
        std::string_view user,
        const std::vector<std::string>& groups) const;
    [[nodiscard]] bool can_build(
        std::string_view region,
        double x,
        double y,
        std::string_view user,
        const std::vector<std::string>& groups) const;
    [[nodiscard]] bool can_terraform(
        std::string_view region,
        double x,
        double y,
        std::string_view user,
        const std::vector<std::string>& groups) const;

private:
    void load_locked();
    void persist_locked() const;
    static bool member_of(
        std::string_view group,
        const std::vector<std::string>& groups);
    [[nodiscard]] std::optional<bool> explicit_access_locked(
        std::string_view parcel_id,
        std::string_view user_id) const;
    static std::string access_key(
        std::string_view parcel_id,
        std::string_view user_id);

    std::string path_;
    mutable std::mutex mutex_;
    std::unordered_map<std::string, ParcelInfo> parcels_;
    std::unordered_map<std::string, ParcelAccessEntry> access_;
};

} // namespace opengenesis::core
