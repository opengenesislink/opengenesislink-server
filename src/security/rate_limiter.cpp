#include "opengenesis/security/rate_limiter.hpp"

#include <stdexcept>

namespace opengenesis::security {

bool RateLimiter::allow(const std::string_view key,
                        const std::size_t limit,
                        const std::chrono::milliseconds window,
                        const std::chrono::steady_clock::time_point now) {
    if (key.empty() || limit == 0 || window <= std::chrono::milliseconds{0}) {
        throw std::invalid_argument("invalid rate-limit policy");
    }

    std::scoped_lock lock(mutex_);
    auto& bucket = buckets_[std::string{key}];
    bucket.window = window;

    const auto oldest = now - window;
    while (!bucket.events.empty() && bucket.events.front() <= oldest) {
        bucket.events.pop_front();
    }

    if (bucket.events.size() >= limit) return false;
    bucket.events.push_back(now);
    return true;
}

void RateLimiter::clear(const std::string_view key) {
    std::scoped_lock lock(mutex_);
    buckets_.erase(std::string{key});
}

void RateLimiter::purge(const std::chrono::steady_clock::time_point now) {
    std::scoped_lock lock(mutex_);
    for (auto it = buckets_.begin(); it != buckets_.end();) {
        const auto oldest = now - it->second.window;
        while (!it->second.events.empty() && it->second.events.front() <= oldest) {
            it->second.events.pop_front();
        }
        if (it->second.events.empty()) it = buckets_.erase(it);
        else ++it;
    }
}

std::size_t RateLimiter::tracked_keys() const {
    std::scoped_lock lock(mutex_);
    return buckets_.size();
}

} // namespace opengenesis::security
