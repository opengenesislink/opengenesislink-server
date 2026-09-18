#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace opengenesis::core {

enum class CrossingState {
    prepared,
    completed,
    aborted
};

struct CrossingVector {
    double x{0.0};
    double y{0.0};
    double z{0.0};
};

struct RegionCrossing {
    std::string id;
    std::string user_id;
    std::string from_region;
    std::string to_region;
    CrossingVector position;
    CrossingVector velocity;
    std::string attachment_state;
    std::string script_state;
    CrossingState state{CrossingState::prepared};
    std::int64_t created_unix{0};
    std::int64_t expires_unix{0};
    std::int64_t completed_unix{0};
};

class CrossingStore final {
public:
    explicit CrossingStore(std::string path);

    [[nodiscard]] std::optional<RegionCrossing> prepare(
        std::string user_id,
        std::string from_region,
        std::string to_region,
        CrossingVector position,
        CrossingVector velocity,
        std::int64_t expires_unix,
        std::string& reason,
        std::string attachment_state = {},
        std::string script_state = {});

    [[nodiscard]] std::optional<RegionCrossing> complete(
        std::string_view crossing_id,
        std::string_view user_id,
        std::string_view destination_region,
        std::string& reason);

    [[nodiscard]] bool abort(std::string_view crossing_id);
    [[nodiscard]] std::optional<RegionCrossing> find(std::string_view crossing_id) const;
    [[nodiscard]] std::vector<RegionCrossing> list() const;
    std::size_t purge_expired(std::int64_t now_unix);

private:
    void load();
    void persist_locked() const;

    std::string path_;
    mutable std::mutex mutex_;
    std::unordered_map<std::string, RegionCrossing> crossings_;
};

[[nodiscard]] std::string_view crossing_state_name(CrossingState state) noexcept;

} // namespace opengenesis::core
