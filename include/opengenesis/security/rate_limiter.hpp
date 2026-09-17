#pragma once

#include <chrono>
#include <cstddef>
#include <deque>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>

namespace opengenesis::security {

class RateLimiter final {
public:
    [[nodiscard]] bool allow(std::string_view key,
                             std::size_t limit,
                             std::chrono::milliseconds window,
                             std::chrono::steady_clock::time_point now =
                                 std::chrono::steady_clock::now());

    void clear(std::string_view key);
    void purge(std::chrono::steady_clock::time_point now =
                   std::chrono::steady_clock::now());
    [[nodiscard]] std::size_t tracked_keys() const;

private:
    struct Bucket {
        std::deque<std::chrono::steady_clock::time_point> events;
        std::chrono::milliseconds window{0};
    };

    mutable std::mutex mutex_;
    std::unordered_map<std::string, Bucket> buckets_;
};

} // namespace opengenesis::security
