#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>

namespace opengenesis::federation {

class TravelReplayCache final {
public:
    [[nodiscard]] bool consume(std::string nonce, std::int64_t expires_unix);
    void purge_expired(std::int64_t now_unix);
    [[nodiscard]] std::size_t size() const;

private:
    mutable std::mutex mutex_;
    std::unordered_map<std::string, std::int64_t> consumed_;
};

} // namespace opengenesis::federation
