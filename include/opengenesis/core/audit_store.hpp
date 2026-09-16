#pragma once
#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>
namespace opengenesis::core {
struct AuditEvent { std::uint64_t sequence{0}; std::string actor,action,target,detail; std::int64_t unix_time{0}; };
class AuditStore final {
public:
 explicit AuditStore(std::string path);
 void append(std::string actor,std::string action,std::string target,std::string detail={});
 [[nodiscard]] std::vector<AuditEvent> recent(std::size_t limit=200) const;
 [[nodiscard]] std::size_t count() const;
private:
 void load();
 std::string path_; mutable std::mutex mutex_; std::vector<AuditEvent> events_; std::uint64_t next_{1};
};
}
