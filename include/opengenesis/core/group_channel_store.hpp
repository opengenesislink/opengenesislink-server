#pragma once
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
namespace opengenesis::core {
struct GroupPost { std::string id,group_id,sender_id,kind,title,text; std::int64_t sent_unix{0}; };
class GroupChannelStore final {
public:
 explicit GroupChannelStore(std::string path);
 [[nodiscard]] std::optional<GroupPost> send(std::string group,std::string sender,std::string kind,std::string title,std::string text,std::string& reason);
 [[nodiscard]] std::vector<GroupPost> list(std::string_view group,std::size_t limit=100) const;
 [[nodiscard]] std::size_t count() const;
private: void load(); void persist_locked() const; static std::string clean(std::string v,std::size_t max); std::string path_; mutable std::mutex mutex_; std::vector<GroupPost> posts_;
};
}
