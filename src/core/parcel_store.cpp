#include "opengenesis/core/parcel_store.hpp"

#include "opengenesis/platform/filesystem.hpp"
#include "opengenesis/security/crypto.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
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

std::string hex(std::string_view value) {
    constexpr char digits[] = "0123456789abcdef";
    std::string output(value.size() * 2U, '0');
    for (std::size_t i = 0; i < value.size(); ++i) {
        const auto c = static_cast<unsigned char>(value[i]);
        output[i * 2U] = digits[c >> 4U];
        output[i * 2U + 1U] = digits[c & 15U];
    }
    return output;
}

unsigned char nibble(const char c) {
    if (c >= '0' && c <= '9') {
        return static_cast<unsigned char>(c - '0');
    }
    if (c >= 'a' && c <= 'f') {
        return static_cast<unsigned char>(c - 'a' + 10);
    }
    if (c >= 'A' && c <= 'F') {
        return static_cast<unsigned char>(c - 'A' + 10);
    }
    throw std::runtime_error("invalid hex");
}

std::string unhex(const std::string_view value) {
    if ((value.size() % 2U) != 0U) {
        throw std::runtime_error("invalid hex");
    }
    std::string output(value.size() / 2U, '\0');
    for (std::size_t i = 0; i < output.size(); ++i) {
        output[i] = static_cast<char>(
            (nibble(value[i * 2U]) << 4U) |
            nibble(value[i * 2U + 1U]));
    }
    return output;
}

std::vector<std::string> split_tab(const std::string& line) {
    std::vector<std::string> fields;
    std::size_t start = 0;
    while (true) {
        const auto end = line.find('\t', start);
        fields.push_back(
            line.substr(
                start,
                end == std::string::npos
                    ? std::string::npos
                    : end - start));
        if (end == std::string::npos) break;
        start = end + 1U;
    }
    return fields;
}

bool overlap(
    const ParcelInfo& parcel,
    const std::string_view region,
    const std::uint16_t x1,
    const std::uint16_t y1,
    const std::uint16_t x2,
    const std::uint16_t y2) {
    return parcel.region_id == region &&
           !(x2 < parcel.x1 || x1 > parcel.x2 ||
             y2 < parcel.y1 || y1 > parcel.y2);
}

bool valid_id(const std::string_view value) {
    return !value.empty() && value.size() <= 256U &&
           value.find('\t') == std::string_view::npos &&
           value.find('\r') == std::string_view::npos &&
           value.find('\n') == std::string_view::npos;
}

} // namespace

ParcelStore::ParcelStore(std::string path)
    : path_(std::move(path)) {
    reload();
}

std::string ParcelStore::access_key(
    const std::string_view parcel_id,
    const std::string_view user_id) {
    return std::string{parcel_id} + "|" + std::string{user_id};
}

void ParcelStore::reload() {
    std::scoped_lock lock(mutex_);
    load_locked();
}

std::optional<ParcelInfo> ParcelStore::create(
    std::string owner,
    std::string region,
    std::string name,
    const std::uint16_t x1,
    const std::uint16_t y1,
    const std::uint16_t x2,
    const std::uint16_t y2,
    std::string& reason) {
    if (owner.empty() || region.empty() || name.empty() ||
        x1 > x2 || y1 > y2) {
        reason = "invalid-parcel";
        return std::nullopt;
    }

    std::scoped_lock lock(mutex_);
    for (const auto& [_, parcel] : parcels_) {
        if (overlap(parcel, region, x1, y1, x2, y2)) {
            reason = "parcel-overlap";
            return std::nullopt;
        }
    }

    const auto now = unix_now();
    ParcelInfo parcel{
        .id = security::random_hex(16),
        .region_id = std::move(region),
        .name = std::move(name),
        .owner_user_id = std::move(owner),
        .group_id = {},
        .x1 = x1,
        .y1 = y1,
        .x2 = x2,
        .y2 = y2,
        .created_unix = now,
        .updated_unix = now};
    parcels_[parcel.id] = parcel;
    persist_locked();
    reason.clear();
    return parcel;
}

bool ParcelStore::update_policy(
    const std::string_view actor,
    const std::string_view id,
    std::string group,
    const bool public_entry,
    const bool public_build,
    const bool group_build,
    const bool group_terraform,
    std::string& reason) {
    std::scoped_lock lock(mutex_);
    const auto it = parcels_.find(std::string{id});
    if (it == parcels_.end()) {
        reason = "parcel-not-found";
        return false;
    }
    if (it->second.owner_user_id != actor) {
        reason = "permission-denied";
        return false;
    }
    it->second.group_id = std::move(group);
    it->second.public_entry = public_entry;
    it->second.public_build = public_build;
    it->second.group_build = group_build;
    it->second.group_terraform = group_terraform;
    it->second.updated_unix = unix_now();
    persist_locked();
    reason.clear();
    return true;
}

bool ParcelStore::set_access(
    const std::string_view actor,
    const std::string_view parcel_id,
    std::string user_id,
    const bool allowed,
    std::string& reason) {
    if (!valid_id(user_id)) {
        reason = "invalid-parcel-access-user";
        return false;
    }

    std::scoped_lock lock(mutex_);
    const auto parcel =
        parcels_.find(std::string{parcel_id});
    if (parcel == parcels_.end()) {
        reason = "parcel-not-found";
        return false;
    }
    if (parcel->second.owner_user_id != actor) {
        reason = "permission-denied";
        return false;
    }
    if (parcel->second.owner_user_id == user_id) {
        reason = "owner-access-is-implicit";
        return false;
    }

    ParcelAccessEntry entry{
        .parcel_id = std::string{parcel_id},
        .user_id = std::move(user_id),
        .allowed = allowed,
        .updated_unix = unix_now()};
    access_[access_key(entry.parcel_id, entry.user_id)] =
        std::move(entry);
    parcel->second.updated_unix = unix_now();
    persist_locked();
    reason.clear();
    return true;
}

bool ParcelStore::remove_access(
    const std::string_view actor,
    const std::string_view parcel_id,
    const std::string_view user_id,
    std::string& reason) {
    std::scoped_lock lock(mutex_);
    const auto parcel =
        parcels_.find(std::string{parcel_id});
    if (parcel == parcels_.end()) {
        reason = "parcel-not-found";
        return false;
    }
    if (parcel->second.owner_user_id != actor) {
        reason = "permission-denied";
        return false;
    }
    if (access_.erase(access_key(parcel_id, user_id)) == 0U) {
        reason = "parcel-access-not-found";
        return false;
    }
    parcel->second.updated_unix = unix_now();
    persist_locked();
    reason.clear();
    return true;
}

std::vector<ParcelAccessEntry> ParcelStore::access_list(
    const std::string_view parcel_id) const {
    std::scoped_lock lock(mutex_);
    std::vector<ParcelAccessEntry> result;
    for (const auto& [_, entry] : access_) {
        if (entry.parcel_id == parcel_id) {
            result.push_back(entry);
        }
    }
    std::sort(
        result.begin(), result.end(),
        [](const ParcelAccessEntry& left,
           const ParcelAccessEntry& right) {
            return left.user_id < right.user_id;
        });
    return result;
}

std::optional<ParcelInfo> ParcelStore::find(
    const std::string_view id) const {
    std::scoped_lock lock(mutex_);
    const auto it = parcels_.find(std::string{id});
    return it == parcels_.end()
               ? std::nullopt
               : std::optional<ParcelInfo>{it->second};
}

std::optional<ParcelInfo> ParcelStore::at(
    const std::string_view region,
    const double x,
    const double y) const {
    if (!std::isfinite(x) || !std::isfinite(y)) {
        return std::nullopt;
    }
    std::scoped_lock lock(mutex_);
    for (const auto& [_, parcel] : parcels_) {
        if (parcel.region_id == region &&
            x >= static_cast<double>(parcel.x1) &&
            x <= static_cast<double>(parcel.x2) &&
            y >= static_cast<double>(parcel.y1) &&
            y <= static_cast<double>(parcel.y2)) {
            return parcel;
        }
    }
    return std::nullopt;
}

std::vector<ParcelInfo> ParcelStore::list_region(
    const std::string_view region) const {
    std::scoped_lock lock(mutex_);
    std::vector<ParcelInfo> result;
    for (const auto& [_, parcel] : parcels_) {
        if (parcel.region_id == region) {
            result.push_back(parcel);
        }
    }
    std::sort(
        result.begin(), result.end(),
        [](const ParcelInfo& left,
           const ParcelInfo& right) {
            return left.created_unix < right.created_unix;
        });
    return result;
}

std::vector<ParcelInfo> ParcelStore::list_owner(
    const std::string_view owner) const {
    std::scoped_lock lock(mutex_);
    std::vector<ParcelInfo> result;
    for (const auto& [_, parcel] : parcels_) {
        if (parcel.owner_user_id == owner) {
            result.push_back(parcel);
        }
    }
    return result;
}

std::size_t ParcelStore::count() const {
    std::scoped_lock lock(mutex_);
    return parcels_.size();
}

bool ParcelStore::member_of(
    const std::string_view group,
    const std::vector<std::string>& groups) {
    return !group.empty() &&
           std::find(groups.begin(), groups.end(), group) !=
               groups.end();
}

std::optional<bool> ParcelStore::explicit_access_locked(
    const std::string_view parcel_id,
    const std::string_view user_id) const {
    const auto it =
        access_.find(access_key(parcel_id, user_id));
    return it == access_.end()
               ? std::nullopt
               : std::optional<bool>{it->second.allowed};
}

bool ParcelStore::can_enter(
    const std::string_view region,
    const double x,
    const double y,
    const std::string_view user,
    const std::vector<std::string>& groups) const {
    if (!std::isfinite(x) || !std::isfinite(y)) return false;

    std::scoped_lock lock(mutex_);
    const ParcelInfo* found = nullptr;
    for (const auto& [_, parcel] : parcels_) {
        if (parcel.region_id == region &&
            x >= static_cast<double>(parcel.x1) &&
            x <= static_cast<double>(parcel.x2) &&
            y >= static_cast<double>(parcel.y1) &&
            y <= static_cast<double>(parcel.y2)) {
            found = &parcel;
            break;
        }
    }
    if (!found) return true;
    if (found->owner_user_id == user) return true;

    const auto explicit_access =
        explicit_access_locked(found->id, user);
    if (explicit_access.has_value()) {
        return *explicit_access;
    }

    return found->public_entry ||
           member_of(found->group_id, groups);
}

bool ParcelStore::can_build(
    const std::string_view region,
    const double x,
    const double y,
    const std::string_view user,
    const std::vector<std::string>& groups) const {
    const auto parcel = at(region, x, y);
    return !parcel ||
           parcel->public_build ||
           parcel->owner_user_id == user ||
           (parcel->group_build &&
            member_of(parcel->group_id, groups));
}

bool ParcelStore::can_terraform(
    const std::string_view region,
    const double x,
    const double y,
    const std::string_view user,
    const std::vector<std::string>& groups) const {
    const auto parcel = at(region, x, y);
    return !parcel ||
           parcel->owner_user_id == user ||
           (parcel->group_terraform &&
            member_of(parcel->group_id, groups));
}

void ParcelStore::load_locked() {
    parcels_.clear();
    access_.clear();

    std::ifstream input(path_);
    if (!input) return;

    std::string line;
    while (std::getline(input, line)) {
        if (line.empty() || line[0] == '#') continue;
        const auto fields = split_tab(line);
        try {
            if (fields.size() == 15U) {
                ParcelInfo parcel{
                    .id = fields[0],
                    .region_id = fields[1],
                    .name = unhex(fields[2]),
                    .owner_user_id = fields[3],
                    .group_id = fields[4],
                    .x1 = static_cast<std::uint16_t>(
                        std::stoul(fields[5])),
                    .y1 = static_cast<std::uint16_t>(
                        std::stoul(fields[6])),
                    .x2 = static_cast<std::uint16_t>(
                        std::stoul(fields[7])),
                    .y2 = static_cast<std::uint16_t>(
                        std::stoul(fields[8])),
                    .public_entry = fields[9] == "1",
                    .public_build = fields[10] == "1",
                    .group_build = fields[11] == "1",
                    .group_terraform = fields[12] == "1",
                    .created_unix = std::stoll(fields[13]),
                    .updated_unix = std::stoll(fields[14])};
                parcels_[parcel.id] = std::move(parcel);
            } else if (fields.size() == 16U &&
                       fields[0] == "P") {
                ParcelInfo parcel{
                    .id = fields[1],
                    .region_id = fields[2],
                    .name = unhex(fields[3]),
                    .owner_user_id = fields[4],
                    .group_id = fields[5],
                    .x1 = static_cast<std::uint16_t>(
                        std::stoul(fields[6])),
                    .y1 = static_cast<std::uint16_t>(
                        std::stoul(fields[7])),
                    .x2 = static_cast<std::uint16_t>(
                        std::stoul(fields[8])),
                    .y2 = static_cast<std::uint16_t>(
                        std::stoul(fields[9])),
                    .public_entry = fields[10] == "1",
                    .public_build = fields[11] == "1",
                    .group_build = fields[12] == "1",
                    .group_terraform = fields[13] == "1",
                    .created_unix = std::stoll(fields[14]),
                    .updated_unix = std::stoll(fields[15])};
                parcels_[parcel.id] = std::move(parcel);
            } else if (fields.size() == 5U &&
                       fields[0] == "A") {
                ParcelAccessEntry entry{
                    .parcel_id = fields[1],
                    .user_id = fields[2],
                    .allowed = fields[3] == "1",
                    .updated_unix = std::stoll(fields[4])};
                if (valid_id(entry.parcel_id) &&
                    valid_id(entry.user_id)) {
                    access_[access_key(
                        entry.parcel_id,
                        entry.user_id)] = std::move(entry);
                }
            }
        } catch (...) {
        }
    }

    for (auto it = access_.begin(); it != access_.end();) {
        if (!parcels_.contains(it->second.parcel_id)) {
            it = access_.erase(it);
        } else {
            ++it;
        }
    }
}

void ParcelStore::persist_locked() const {
    const std::filesystem::path path(path_);
    if (path.has_parent_path()) {
        std::filesystem::create_directories(
            path.parent_path());
    }
    const auto temporary = path.string() + ".tmp";
    std::ofstream output(temporary, std::ios::trunc);
    if (!output) {
        throw std::runtime_error("cannot write parcels");
    }
    output << "# OpenGenesisLINK parcels v2\n";

    for (const auto& [_, parcel] : parcels_) {
        output << "P\t" << parcel.id << '\t'
               << parcel.region_id << '\t'
               << hex(parcel.name) << '\t'
               << parcel.owner_user_id << '\t'
               << parcel.group_id << '\t'
               << parcel.x1 << '\t' << parcel.y1 << '\t'
               << parcel.x2 << '\t' << parcel.y2 << '\t'
               << (parcel.public_entry ? 1 : 0) << '\t'
               << (parcel.public_build ? 1 : 0) << '\t'
               << (parcel.group_build ? 1 : 0) << '\t'
               << (parcel.group_terraform ? 1 : 0) << '\t'
               << parcel.created_unix << '\t'
               << parcel.updated_unix << '\n';
    }

    for (const auto& [_, entry] : access_) {
        output << "A\t" << entry.parcel_id << '\t'
               << entry.user_id << '\t'
               << (entry.allowed ? 1 : 0) << '\t'
               << entry.updated_unix << '\n';
    }

    output.close();
    if (!output) {
        throw std::runtime_error("cannot flush parcels");
    }
    platform::replace_file(temporary, path);
}

} // namespace opengenesis::core
