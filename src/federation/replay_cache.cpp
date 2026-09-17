#include "opengenesis/federation/replay_cache.hpp"

namespace opengenesis::federation {

bool TravelReplayCache::consume(std::string nonce, const std::int64_t expires_unix) {
    if (nonce.empty() || expires_unix <= 0) return false;
    std::scoped_lock lock(mutex_);
    if (consumed_.contains(nonce)) return false;
    consumed_.emplace(std::move(nonce), expires_unix);
    return true;
}

void TravelReplayCache::purge_expired(const std::int64_t now_unix) {
    std::scoped_lock lock(mutex_);
    for (auto it = consumed_.begin(); it != consumed_.end();) {
        if (it->second <= now_unix) it = consumed_.erase(it);
        else ++it;
    }
}

std::size_t TravelReplayCache::size() const {
    std::scoped_lock lock(mutex_);
    return consumed_.size();
}

} // namespace opengenesis::federation
