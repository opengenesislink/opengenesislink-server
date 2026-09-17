#include "opengenesis/core/crossing_store.hpp"
#include "opengenesis/platform/filesystem.hpp"

#include "opengenesis/security/crypto.hpp"

#include <algorithm>
#include <chrono>
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

std::vector<std::string> split_tab(const std::string& line) {
    std::vector<std::string> fields;
    std::size_t start = 0;
    while (true) {
        const auto end = line.find('\t', start);
        fields.push_back(line.substr(start, end == std::string::npos
                                               ? std::string::npos
                                               : end - start));
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return fields;
}

CrossingState parse_state(const std::string_view value) {
    if (value == "completed") return CrossingState::completed;
    if (value == "aborted") return CrossingState::aborted;
    return CrossingState::prepared;
}

} // namespace

std::string_view crossing_state_name(const CrossingState state) noexcept {
    switch (state) {
        case CrossingState::prepared: return "prepared";
        case CrossingState::completed: return "completed";
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
    std::string& reason) {
    const auto now = unix_now();
    if (user_id.empty() || from_region.empty() || to_region.empty() ||
        from_region == to_region || expires_unix <= now || expires_unix > now + 300) {
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
        .state = CrossingState::prepared,
        .created_unix = now,
        .expires_unix = expires_unix,
        .completed_unix = 0};

    std::scoped_lock lock(mutex_);
    crossings_[crossing.id] = crossing;
    persist_locked();
    reason.clear();
    return crossing;
}

std::optional<RegionCrossing> CrossingStore::complete(
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
        reason = "crossing-already-consumed";
        return std::nullopt;
    }
    if (crossing.expires_unix <= now) {
        crossing.state = CrossingState::aborted;
        persist_locked();
        reason = "crossing-expired";
        return std::nullopt;
    }

    crossing.state = CrossingState::completed;
    crossing.completed_unix = now;
    persist_locked();
    reason.clear();
    return crossing;
}

bool CrossingStore::abort(const std::string_view crossing_id) {
    std::scoped_lock lock(mutex_);
    const auto it = crossings_.find(std::string{crossing_id});
    if (it == crossings_.end() || it->second.state != CrossingState::prepared) return false;
    it->second.state = CrossingState::aborted;
    persist_locked();
    return true;
}

std::optional<RegionCrossing> CrossingStore::find(const std::string_view crossing_id) const {
    std::scoped_lock lock(mutex_);
    const auto it = crossings_.find(std::string{crossing_id});
    return it == crossings_.end() ? std::nullopt : std::optional<RegionCrossing>{it->second};
}

std::vector<RegionCrossing> CrossingStore::list() const {
    std::scoped_lock lock(mutex_);
    std::vector<RegionCrossing> output;
    output.reserve(crossings_.size());
    for (const auto& [_, crossing] : crossings_) output.push_back(crossing);
    std::sort(output.begin(), output.end(), [](const RegionCrossing& a, const RegionCrossing& b) {
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
    if (removed != 0) persist_locked();
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
        if (fields.size() != 14) continue;
        try {
            RegionCrossing crossing{
                .id = fields[0],
                .user_id = fields[1],
                .from_region = fields[2],
                .to_region = fields[3],
                .position = {std::stod(fields[4]), std::stod(fields[5]), std::stod(fields[6])},
                .velocity = {std::stod(fields[7]), std::stod(fields[8]), std::stod(fields[9])},
                .state = parse_state(fields[10]),
                .created_unix = std::stoll(fields[11]),
                .expires_unix = std::stoll(fields[12]),
                .completed_unix = std::stoll(fields[13])};
            crossings_[crossing.id] = std::move(crossing);
        } catch (...) {
        }
    }
}

void CrossingStore::persist_locked() const {
    const std::filesystem::path path(path_);
    if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path());
    const auto temp = path.string() + ".tmp";

    std::ofstream output(temp, std::ios::trunc);
    if (!output) throw std::runtime_error("cannot write crossing store");
    output << "# OpenGenesisLINK crossing store v1\n";
    output << std::setprecision(17);

    std::vector<RegionCrossing> rows;
    rows.reserve(crossings_.size());
    for (const auto& [_, crossing] : crossings_) rows.push_back(crossing);
    std::sort(rows.begin(), rows.end(), [](const RegionCrossing& a, const RegionCrossing& b) {
        return a.created_unix < b.created_unix;
    });

    for (const auto& crossing : rows) {
        output << crossing.id << '\t' << crossing.user_id << '\t' << crossing.from_region << '\t'
               << crossing.to_region << '\t'
               << crossing.position.x << '\t' << crossing.position.y << '\t' << crossing.position.z << '\t'
               << crossing.velocity.x << '\t' << crossing.velocity.y << '\t' << crossing.velocity.z << '\t'
               << crossing_state_name(crossing.state) << '\t'
               << crossing.created_unix << '\t' << crossing.expires_unix << '\t'
               << crossing.completed_unix << '\n';
    }

    output.close();
    if (!output) throw std::runtime_error("cannot flush crossing store");

    opengenesis::platform::replace_file(temp, path);
}

} // namespace opengenesis::core
