#pragma once
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
namespace opengenesis::core {
struct LandmarkInfo { std::string id,user_id,name,region_id; double x{128},y{128},z{0}; std::int64_t created_unix{0}; };
class LandmarkStore final {
public:
 explicit LandmarkStore(std::string path);
 [[nodiscard]] std::optional<LandmarkInfo> create(std::string user,std::string name,std::string region,double x,double y,double z,std::string& reason);
 [[nodiscard]] std::optional<LandmarkInfo> find(std::string_view id) const;
 [[nodiscard]] std::vector<LandmarkInfo> list_for_user(std::string_view user) const;
 bool remove(std::string_view user,std::string_view id);
 [[nodiscard]] std::size_t count() const;
private: void load(); void persist_locked() const; std::string path_; mutable std::mutex mutex_; std::vector<LandmarkInfo> items_;
};
}
