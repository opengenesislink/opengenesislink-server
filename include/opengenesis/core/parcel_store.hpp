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
    std::string id,region_id,name,owner_user_id,group_id;
    std::uint16_t x1{0},y1{0},x2{255},y2{255};
    bool public_entry{true}, public_build{false}, group_build{true}, group_terraform{false};
    std::int64_t created_unix{0}, updated_unix{0};
};
class ParcelStore final {
public:
 explicit ParcelStore(std::string path);
 void reload();
 [[nodiscard]] std::optional<ParcelInfo> create(std::string owner,std::string region,std::string name,
                                                std::uint16_t x1,std::uint16_t y1,std::uint16_t x2,std::uint16_t y2,
                                                std::string& reason);
 bool update_policy(std::string_view actor,std::string_view parcel_id,std::string group_id,
                    bool public_entry,bool public_build,bool group_build,bool group_terraform,
                    std::string& reason);
 [[nodiscard]] std::optional<ParcelInfo> find(std::string_view id) const;
 [[nodiscard]] std::optional<ParcelInfo> at(std::string_view region,double x,double y) const;
 [[nodiscard]] std::vector<ParcelInfo> list_region(std::string_view region) const;
 [[nodiscard]] std::vector<ParcelInfo> list_owner(std::string_view owner) const;
 [[nodiscard]] std::size_t count() const;
 [[nodiscard]] bool can_enter(std::string_view region,double x,double y,std::string_view user,const std::vector<std::string>& groups) const;
 [[nodiscard]] bool can_build(std::string_view region,double x,double y,std::string_view user,const std::vector<std::string>& groups) const;
 [[nodiscard]] bool can_terraform(std::string_view region,double x,double y,std::string_view user,const std::vector<std::string>& groups) const;
private:
 void load_locked(); void persist_locked() const;
 static bool member_of(std::string_view group,const std::vector<std::string>& groups);
 std::string path_; mutable std::mutex mutex_; std::unordered_map<std::string,ParcelInfo> parcels_;
};
}
