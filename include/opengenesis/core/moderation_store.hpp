#pragma once
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
namespace opengenesis::core {
struct BanRecord { std::string id,user_id,scope,scope_id,reason,created_by; std::int64_t created_unix{0},expires_unix{0}; };
class ModerationStore final {
public:
 explicit ModerationStore(std::string path);
 void reload();
 [[nodiscard]] std::optional<BanRecord> ban(std::string actor,std::string user,std::string scope,std::string scope_id,std::string reason,std::int64_t expires_unix,std::string& error);
 bool unban(std::string_view id);
 [[nodiscard]] bool is_banned(std::string_view user,std::string_view region) const;
 [[nodiscard]] std::vector<BanRecord> list() const;
 [[nodiscard]] std::size_t active_count() const;
private:
 void load_locked(); void persist_locked() const;
 std::string path_; mutable std::mutex mutex_; std::unordered_map<std::string,BanRecord> bans_;
};
}
