#pragma once
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
namespace opengenesis::core {
struct NotificationInfo { std::string id,user_id,type,title,body,target; std::int64_t created_unix{0},read_unix{0}; };
class NotificationStore final {
public:
 explicit NotificationStore(std::string path);
 NotificationInfo push(std::string user,std::string type,std::string title,std::string body,std::string target={});
 [[nodiscard]] std::vector<NotificationInfo> list_for_user(std::string_view user,std::size_t limit=100) const;
 bool mark_read(std::string_view user,std::string_view id);
 [[nodiscard]] std::size_t unread_count(std::string_view user) const;
 [[nodiscard]] std::size_t count() const;
private: void load(); void persist_locked() const; static std::string clean(std::string value,std::size_t max); std::string path_; mutable std::mutex mutex_; std::vector<NotificationInfo> items_;
};
}
