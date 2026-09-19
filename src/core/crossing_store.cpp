#include "opengenesis/core/crossing_store.hpp"
#include "opengenesis/platform/filesystem.hpp"
#include "opengenesis/security/crypto.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <stdexcept>
#include <utility>
#include <vector>

namespace opengenesis::core {
namespace {

std::int64_t unix_now() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

bool finite_vector(const CrossingVector value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

std::vector<std::string> split_tab(const std::string& line) {
    std::vector<std::string> fields;
    std::size_t start = 0;
    while (true) {
        const auto end = line.find('\t', start);
        fields.push_back(line.substr(
            start, end == std::string::npos ? std::string::npos : end - start));
        if (end == std::string::npos) break;
        start = end + 1U;
    }
    return fields;
}

CrossingState parse_state(const std::string_view value) {
    if (value == "reserved") return CrossingState::reserved;
    if (value == "completed") return CrossingState::completed;
    if (value == "rolled_back") return CrossingState::rolled_back;
    if (value == "aborted") return CrossingState::aborted;
    return CrossingState::prepared;
}

bool terminal(const CrossingState state) {
    return state == CrossingState::completed ||
           state == CrossingState::rolled_back ||
           state == CrossingState::aborted;
}

} // namespace

std::string_view crossing_state_name(const CrossingState state) noexcept {
    switch (state) {
        case CrossingState::prepared: return "prepared";
        case CrossingState::reserved: return "reserved";
        case CrossingState::completed: return "completed";
        case CrossingState::rolled_back: return "rolled_back";
        case CrossingState::aborted: return "aborted";
    }
    return "prepared";
}

CrossingStore::CrossingStore(std::string path) : path_(std::move(path)) {
    load();
}

std::optional<RegionCrossing> CrossingStore::prepare(
    std::string user_id,
    std::string from_region,
    std::string to_region,
    const CrossingVector position,
    const CrossingVector velocity,
    const std::int64_t expires_unix,
    std::string& reason,
    std::string attachment_state,
    std::string script_state,
    const CrossingVector rotation,
    const CrossingVector angular_velocity,
    const bool physical,
    std::string object_state,
    std::string linkset_state) {
    const auto now = unix_now();
    if (user_id.empty() || from_region.empty() || to_region.empty() ||
        from_region == to_region || expires_unix <= now || expires_unix > now + 300 ||
        !finite_vector(position) || !finite_vector(velocity) ||
        !finite_vector(rotation) || !finite_vector(angular_velocity) ||
        attachment_state.size() > 64U * 1024U ||
        script_state.size() > 64U * 1024U ||
        object_state.size() > 64U * 1024U ||
        linkset_state.size() > 64U * 1024U) {
        reason = "invalid-crossing";
        return std::nullopt;
    }

    RegionCrossing crossing{
        .id = security::random_hex(16),
        .user_id = std::move(user_id),
        .from_region = std::move(from_region),
        .to_region = std::move(to_region),
        .position = position,
        .velocity = velocity,
        .attachment_state = std::move(attachment_state),
        .script_state = std::move(script_state),
        .rotation = rotation,
        .angular_velocity = angular_velocity,
        .physical = physical,
        .object_state = std::move(object_state),
        .linkset_state = std::move(linkset_state),
        .reservation_token = {},
        .state = CrossingState::prepared,
        .created_unix = now,
        .expires_unix = expires_unix,
        .reserved_unix = 0,
        .completed_unix = 0,
        .rolled_back_unix = 0,
        .rollback_reason = {}};

    std::scoped_lock lock(mutex_);
    crossings_[crossing.id] = crossing;
    persist_locked();
    reason.clear();
    return crossing;
}

std::optional<RegionCrossing> CrossingStore::reserve(
    const std::string_view crossing_id,
    const std::string_view user_id,
    const std::string_view destination_region,
    std::string& reason) {
    const auto now = unix_now();
    std::scoped_lock lock(mutex_);

    const auto it = crossings_.find(std::string{crossing_id});
    if (it == crossings_.end()) {
        reason = "crossing-not-found";
        return std::nullopt;
    }
    auto& crossing = it->second;
    if (crossing.user_id != user_id || crossing.to_region != destination_region) {
        reason = "crossing-binding-mismatch";
        return std::nullopt;
    }
    if (crossing.state != CrossingState::prepared) {
        reason = "crossing-not-prepared";
        return std::nullopt;
    }
    if (crossing.expires_unix <= now) {
        crossing.state = CrossingState::aborted;
        persist_locked();
        reason = "crossing-expired";
        return std::nullopt;
    }

    crossing.state = CrossingState::reserved;
    crossing.reservation_token = security::random_hex(24);
    crossing.reserved_unix = now;
    persist_locked();
    reason.clear();
    return crossing;
}

std::optional<RegionCrossing> CrossingStore::complete(
    const std::string_view crossing_id,
    const std::string_view user_id,
    const std::string_view destination_region,
    std::string& reason,
    const std::string_view reservation_token) {
    const auto now = unix_now();
    std::scoped_lock lock(mutex_);

    const auto it = crossings_.find(std::string{crossing_id});
    if (it == crossings_.end()) {
        reason = "crossing-not-found";
        return std::nullopt;
    }
    auto& crossing = it->second;
    if (crossing.user_id != user_id || crossing.to_region != destination_region) {
        reason = "crossing-binding-mismatch";
        return std::nullopt;
    }
    if (terminal(crossing.state)) {
        reason = "crossing-already-consumed";
        return std::nullopt;
    }
    if (crossing.expires_unix <= now) {
        crossing.state = CrossingState::aborted;
        persist_locked();
        reason = "crossing-expired";
        return std::nullopt;
    }

    if (crossing.state == CrossingState::reserved) {
        if (reservation_token.empty() ||
            !security::secure_equals(
                crossing.reservation_token, reservation_token)) {
            reason = "crossing-reservation-token-invalid";
            return std::nullopt;
        }
    } else if (crossing.state != CrossingState::prepared ||
               !reservation_token.empty()) {
        reason = "crossing-state-invalid";
        return std::nullopt;
    }

    crossing.state = CrossingState::completed;
    crossing.completed_unix = now;
    persist_locked();
    reason.clear();
    return crossing;
}

std::optional<RegionCrossing> CrossingStore::rollback(
    const std::string_view crossing_id,
    const std::string_view user_id,
    std::string rollback_reason,
    std::string& reason) {
    const auto now = unix_now();
    if (rollback_reason.size() > 1024U) rollback_reason.resize(1024U);

    std::scoped_lock lock(mutex_);
    const auto it = crossings_.find(std::string{crossing_id});
    if (it == crossings_.end()) {
        reason = "crossing-not-found";
        return std::nullopt;
    }
    auto& crossing = it->second;
    if (crossing.user_id != user_id) {
        reason = "crossing-binding-mismatch";
        return std::nullopt;
    }
    if (crossing.state != CrossingState::prepared &&
        crossing.state != CrossingState::reserved) {
        reason = "crossing-already-consumed";
        return std::nullopt;
    }

    crossing.state = CrossingState::rolled_back;
    crossing.rolled_back_unix = now;
    crossing.rollback_reason = std::move(rollback_reason);
    persist_locked();
    reason.clear();
    return crossing;
}

bool CrossingStore::abort(const std::string_view crossing_id) {
    std::scoped_lock lock(mutex_);
    const auto it = crossings_.find(std::string{crossing_id});
    if (it == crossings_.end() ||
        (it->second.state != CrossingState::prepared &&
         it->second.state != CrossingState::reserved)) {
        return false;
    }
    it->second.state = CrossingState::aborted;
    persist_locked();
    return true;
}

std::optional<RegionCrossing> CrossingStore::find(
    const std::string_view crossing_id) const {
    std::scoped_lock lock(mutex_);
    const auto it = crossings_.find(std::string{crossing_id});
    return it == crossings_.end()
               ? std::nullopt
               : std::optional<RegionCrossing>{it->second};
}

std::vector<RegionCrossing> CrossingStore::list() const {
    std::scoped_lock lock(mutex_);
    std::vector<RegionCrossing> output;
    output.reserve(crossings_.size());
    for (const auto& [_, crossing] : crossings_) output.push_back(crossing);
    std::sort(output.begin(), output.end(),
              [](const RegionCrossing& a, const RegionCrossing& b) {
                  return a.created_unix < b.created_unix;
              });
    return output;
}

std::size_t CrossingStore::purge_expired(const std::int64_t now_unix) {
    std::scoped_lock lock(mutex_);
    std::size_t removed = 0;
    for (auto it = crossings_.begin(); it != crossings_.end();) {
        if (it->second.expires_unix <= now_unix &&
            it->second.state != CrossingState::completed) {
            it = crossings_.erase(it);
            ++removed;
        } else {
            ++it;
        }
    }
    if (removed != 0U) persist_locked();
    return removed;
}

void CrossingStore::load() {
    std::scoped_lock lock(mutex_);
    crossings_.clear();
    std::ifstream input(path_);
    if (!input) return;

    std::string line;
    while (std::getline(input, line)) {
        if (line.empty() || line[0] == '#') continue;
        const auto fields = split_tab(line);
        if (fields.size() != 14U &&
            fields.size() != 16U &&
            fields.size() != 29U) {
            continue;
        }
        try {
            RegionCrossing crossing{
                .id = fields[0],
                .user_id = fields[1],
                .from_region = fields[2],
                .to_region = fields[3],
                .position = {
                    std::stod(fields[4]), std::stod(fields[5]),
                    std::stod(fields[6])},
                .velocity = {
                    std::stod(fields[7]), std::stod(fields[8]),
                    std::stod(fields[9])},
                .attachment_state =
                    fields.size() >= 16U
                        ? security::base64_decode(fields[14], 64U * 1024U)
                        : std::string{},
                .script_state =
                    fields.size() >= 16U
                        ? security::base64_decode(fields[15], 64U * 1024U)
                        : std::string{},
                .rotation =
                    fields.size() == 29U
                        ? CrossingVector{
                              std::stod(fields[16]), std::stod(fields[17]),
                              std::stod(fields[18])}
                        : CrossingVector{},
                .angular_velocity =
                    fields.size() == 29U
                        ? CrossingVector{
                              std::stod(fields[19]), std::stod(fields[20]),
                              std::stod(fields[21])}
                        : CrossingVector{},
                .physical =
                    fields.size() == 29U && fields[22] == "1",
                .object_state =
                    fields.size() == 29U
                        ? security::base64_decode(fields[23], 64U * 1024U)
                        : std::string{},
                .linkset_state =
                    fields.size() == 29U
                        ? security::base64_decode(fields[24], 64U * 1024U)
                        : std::string{},
                .reservation_token =
                    fields.size() == 29U ? fields[25] : std::string{},
                .state = parse_state(fields[10]),
                .created_unix = std::stoll(fields[11]),
                .expires_unix = std::stoll(fields[12]),
                .reserved_unix =
                    fields.size() == 29U ? std::stoll(fields[26]) : 0,
                .completed_unix = std::stoll(fields[13]),
                .rolled_back_unix =
                    fields.size() == 29U ? std::stoll(fields[27]) : 0,
                .rollback_reason =
                    fields.size() == 29U
                        ? security::base64_decode(fields[28], 1024U)
                        : std::string{}};
            crossings_[crossing.id] = std::move(crossing);
        } catch (...) {
        }
    }
}

void CrossingStore::persist_locked() const {
    const std::filesystem::path path(path_);
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path());
    }
    const auto temp = path.string() + ".tmp";

    std::ofstream output(temp, std::ios::trunc);
    if (!output) throw std::runtime_error("cannot write crossing store");
    output << "# OpenGenesisLINK crossing store v3\n";
    output << std::setprecision(17);

    std::vector<RegionCrossing> rows;
    rows.reserve(crossings_.size());
    for (const auto& [_, crossing] : crossings_) rows.push_back(crossing);
    std::sort(rows.begin(), rows.end(),
              [](const RegionCrossing& a, const RegionCrossing& b) {
                  return a.created_unix < b.created_unix;
              });

    for (const auto& crossing : rows) {
        output << crossing.id << '\t'
               << crossing.user_id << '\t'
               << crossing.from_region << '\t'
               << crossing.to_region << '\t'
               << crossing.position.x << '\t'
               << crossing.position.y << '\t'
               << crossing.position.z << '\t'
               << crossing.velocity.x << '\t'
               << crossing.velocity.y << '\t'
               << crossing.velocity.z << '\t'
               << crossing_state_name(crossing.state) << '\t'
               << crossing.created_unix << '\t'
               << crossing.expires_unix << '\t'
               << crossing.completed_unix << '\t'
               << security::base64_encode(crossing.attachment_state) << '\t'
               << security::base64_encode(crossing.script_state) << '\t'
               << crossing.rotation.x << '\t'
               << crossing.rotation.y << '\t'
               << crossing.rotation.z << '\t'
               << crossing.angular_velocity.x << '\t'
               << crossing.angular_velocity.y << '\t'
               << crossing.angular_velocity.z << '\t'
               << (crossing.physical ? 1 : 0) << '\t'
               << security::base64_encode(crossing.object_state) << '\t'
               << security::base64_encode(crossing.linkset_state) << '\t'
               << crossing.reservation_token << '\t'
               << crossing.reserved_unix << '\t'
               << crossing.rolled_back_unix << '\t'
               << security::base64_encode(crossing.rollback_reason) << '\n';
    }

    output.close();
    if (!output) throw std::runtime_error("cannot flush crossing store");
    opengenesis::platform::replace_file(temp, path);
}

} // namespace opengenesis::core
