#include "opengenesis/core/object_crossing_store.hpp"

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

ObjectCrossingState parse_state(const std::string_view value) {
    if (value == "exported") return ObjectCrossingState::exported;
    if (value == "imported") return ObjectCrossingState::imported;
    if (value == "cleanup_pending") return ObjectCrossingState::cleanup_pending;
    if (value == "restore_pending") return ObjectCrossingState::restore_pending;
    if (value == "completed") return ObjectCrossingState::completed;
    if (value == "rolled_back") return ObjectCrossingState::rolled_back;
    return ObjectCrossingState::prepared;
}

bool finite_vector(const ObjectCrossingVector& value) {
    return std::isfinite(value.x) && std::isfinite(value.y) &&
           std::isfinite(value.z);
}

std::string decode_b64(const std::string& value, const std::size_t max_bytes) {
    if (value.empty()) return {};
    return security::base64_decode(value, max_bytes);
}

} // namespace

std::string_view object_crossing_state_name(
    const ObjectCrossingState state) noexcept {
    switch (state) {
        case ObjectCrossingState::prepared: return "prepared";
        case ObjectCrossingState::exported: return "exported";
        case ObjectCrossingState::imported: return "imported";
        case ObjectCrossingState::cleanup_pending: return "cleanup_pending";
        case ObjectCrossingState::restore_pending: return "restore_pending";
        case ObjectCrossingState::completed: return "completed";
        case ObjectCrossingState::rolled_back: return "rolled_back";
    }
    return "prepared";
}

std::string_view object_crossing_command_name(
    const ObjectCrossingCommandType command) noexcept {
    switch (command) {
        case ObjectCrossingCommandType::export_source: return "export";
        case ObjectCrossingCommandType::import_destination: return "import";
        case ObjectCrossingCommandType::remove_source: return "remove";
        case ObjectCrossingCommandType::cleanup_destination: return "cleanup";
        case ObjectCrossingCommandType::restore_source: return "restore";
    }
    return "export";
}

ObjectCrossingStore::ObjectCrossingStore(
    std::string path, const std::uint32_t max_attempts)
    : path_(std::move(path)),
      max_attempts_(std::max<std::uint32_t>(1U, max_attempts)) {
    load();
}

std::uint64_t ObjectCrossingStore::destination_id_from_crossing(
    const std::string_view crossing_id) {
    if (crossing_id.size() < 16U) return 0x8000000000000001ULL;
    std::uint64_t value = 0;
    for (std::size_t i = 0; i < 16U; ++i) {
        const char c = crossing_id[i];
        std::uint64_t nibble = 0;
        if (c >= '0' && c <= '9') nibble = static_cast<std::uint64_t>(c - '0');
        else if (c >= 'a' && c <= 'f') nibble = static_cast<std::uint64_t>(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') nibble = static_cast<std::uint64_t>(c - 'A' + 10);
        value = (value << 4U) | nibble;
    }
    value |= 0x8000000000000000ULL;
    if (value == 0ULL) value = 0x8000000000000001ULL;
    return value;
}

std::optional<ObjectCrossingRecord> ObjectCrossingStore::prepare(
    std::string owner_user_id,
    std::string source_region,
    std::string destination_region,
    const std::uint64_t source_entity_id,
    const ObjectCrossingVector destination_position,
    const std::int64_t expires_unix,
    std::string& reason) {
    const auto now = unix_now();
    if (owner_user_id.empty() || owner_user_id.size() > 256U ||
        source_region.empty() || source_region.size() > 256U ||
        destination_region.empty() || destination_region.size() > 256U ||
        source_region == destination_region || source_entity_id == 0 ||
        expires_unix <= now || expires_unix > now + 300 ||
        !finite_vector(destination_position)) {
        reason = "invalid-object-crossing";
        return std::nullopt;
    }

    ObjectCrossingRecord record;
    record.id = security::random_hex(16);
    record.owner_user_id = std::move(owner_user_id);
    record.source_region = std::move(source_region);
    record.destination_region = std::move(destination_region);
    record.source_entity_id = source_entity_id;
    record.destination_entity_id = destination_id_from_crossing(record.id);
    record.destination_position = destination_position;
    record.created_unix = now;
    record.expires_unix = expires_unix;

    std::scoped_lock lock(mutex_);
    crossings_[record.id] = record;
    try {
        persist_locked();
    } catch (...) {
        crossings_.erase(record.id);
        throw;
    }
    reason.clear();
    return record;
}

std::optional<ObjectCrossingCommand> ObjectCrossingStore::command_for_region(
    const std::string_view region_id) const {
    std::scoped_lock lock(mutex_);

    const ObjectCrossingRecord* selected = nullptr;
    ObjectCrossingCommandType type = ObjectCrossingCommandType::export_source;

    for (const auto& [_, record] : crossings_) {
        bool matches = false;
        ObjectCrossingCommandType candidate =
            ObjectCrossingCommandType::export_source;
        if (record.state == ObjectCrossingState::prepared &&
            record.source_region == region_id) {
            matches = true;
            candidate = ObjectCrossingCommandType::export_source;
        } else if (record.state == ObjectCrossingState::exported &&
                   record.destination_region == region_id) {
            matches = true;
            candidate = ObjectCrossingCommandType::import_destination;
        } else if (record.state == ObjectCrossingState::imported &&
                   record.source_region == region_id) {
            matches = true;
            candidate = ObjectCrossingCommandType::remove_source;
        } else if (record.state == ObjectCrossingState::cleanup_pending &&
                   record.destination_region == region_id) {
            matches = true;
            candidate = ObjectCrossingCommandType::cleanup_destination;
        } else if (record.state == ObjectCrossingState::restore_pending &&
                   record.source_region == region_id) {
            matches = true;
            candidate = ObjectCrossingCommandType::restore_source;
        }
        if (!matches) continue;
        if (!selected || record.created_unix < selected->created_unix) {
            selected = &record;
            type = candidate;
        }
    }

    if (!selected) return std::nullopt;
    return ObjectCrossingCommand{.type = type, .crossing = *selected};
}

bool ObjectCrossingStore::record_export(
    const std::string_view crossing_id,
    const std::string_view source_region,
    std::string snapshot,
    std::string& reason) {
    if (snapshot.empty() || snapshot.size() > 256U * 1024U) {
        reason = "invalid-object-snapshot";
        return false;
    }
    std::scoped_lock lock(mutex_);
    const auto it = crossings_.find(std::string{crossing_id});
    if (it == crossings_.end()) {
        reason = "object-crossing-not-found";
        return false;
    }
    auto& record = it->second;
    if (record.source_region != source_region) {
        reason = "object-crossing-region-mismatch";
        return false;
    }
    if (record.state == ObjectCrossingState::exported) {
        reason.clear();
        return true;
    }
    if (record.state != ObjectCrossingState::prepared) {
        reason = "object-crossing-not-exportable";
        return false;
    }
    record.snapshot = std::move(snapshot);
    record.state = ObjectCrossingState::exported;
    record.exported_unix = unix_now();
    record.last_error.clear();
    persist_locked();
    reason.clear();
    return true;
}

bool ObjectCrossingStore::record_import(
    const std::string_view crossing_id,
    const std::string_view destination_region,
    const std::uint64_t destination_entity_id,
    std::string entity_map,
    std::string& reason) {
    if (entity_map.empty() || entity_map.size() > 16U * 1024U) {
        reason = "invalid-object-crossing-entity-map";
        return false;
    }
    std::scoped_lock lock(mutex_);
    const auto it = crossings_.find(std::string{crossing_id});
    if (it == crossings_.end()) {
        reason = "object-crossing-not-found";
        return false;
    }
    auto& record = it->second;
    if (record.destination_region != destination_region ||
        record.destination_entity_id != destination_entity_id) {
        reason = "object-crossing-destination-mismatch";
        return false;
    }
    if (record.state == ObjectCrossingState::imported) {
        if (record.entity_map != entity_map) {
            reason = "object-crossing-entity-map-mismatch";
            return false;
        }
        reason.clear();
        return true;
    }
    if (record.state != ObjectCrossingState::exported) {
        reason = "object-crossing-not-importable";
        return false;
    }
    record.entity_map = std::move(entity_map);
    record.state = ObjectCrossingState::imported;
    record.imported_unix = unix_now();
    record.last_error.clear();
    persist_locked();
    reason.clear();
    return true;
}

bool ObjectCrossingStore::record_remove(
    const std::string_view crossing_id,
    const std::string_view source_region,
    std::string& reason) {
    std::scoped_lock lock(mutex_);
    const auto it = crossings_.find(std::string{crossing_id});
    if (it == crossings_.end()) {
        reason = "object-crossing-not-found";
        return false;
    }
    auto& record = it->second;
    if (record.source_region != source_region) {
        reason = "object-crossing-region-mismatch";
        return false;
    }
    if (record.state == ObjectCrossingState::completed) {
        reason.clear();
        return true;
    }
    if (record.state != ObjectCrossingState::imported) {
        reason = "object-crossing-not-removable";
        return false;
    }
    record.state = ObjectCrossingState::completed;
    record.completed_unix = unix_now();
    record.last_error.clear();
    persist_locked();
    reason.clear();
    return true;
}

bool ObjectCrossingStore::record_cleanup(
    const std::string_view crossing_id,
    const std::string_view destination_region,
    std::string& reason) {
    std::scoped_lock lock(mutex_);
    const auto it = crossings_.find(std::string{crossing_id});
    if (it == crossings_.end()) {
        reason = "object-crossing-not-found";
        return false;
    }
    auto& record = it->second;
    if (record.destination_region != destination_region) {
        reason = "object-crossing-region-mismatch";
        return false;
    }
    if (record.state == ObjectCrossingState::restore_pending ||
        record.state == ObjectCrossingState::rolled_back) {
        reason.clear();
        return true;
    }
    if (record.state != ObjectCrossingState::cleanup_pending) {
        reason = "object-crossing-cleanup-not-pending";
        return false;
    }
    record.state = ObjectCrossingState::restore_pending;
    record.last_error.clear();
    if (record.rollback_reason.empty()) {
        record.rollback_reason = "destination-cleanup-complete";
    }
    persist_locked();
    reason.clear();
    return true;
}

bool ObjectCrossingStore::record_restore(
    const std::string_view crossing_id,
    const std::string_view source_region,
    std::string& reason) {
    std::scoped_lock lock(mutex_);
    const auto it = crossings_.find(std::string{crossing_id});
    if (it == crossings_.end()) {
        reason = "object-crossing-not-found";
        return false;
    }
    auto& record = it->second;
    if (record.source_region != source_region) {
        reason = "object-crossing-region-mismatch";
        return false;
    }
    if (record.state == ObjectCrossingState::rolled_back) {
        reason.clear();
        return true;
    }
    if (record.state != ObjectCrossingState::restore_pending) {
        reason = "object-crossing-restore-not-pending";
        return false;
    }
    record.state = ObjectCrossingState::rolled_back;
    record.rolled_back_unix = unix_now();
    record.last_error.clear();
    persist_locked();
    reason.clear();
    return true;
}

bool ObjectCrossingStore::reject_command(
    const std::string_view crossing_id,
    const ObjectCrossingCommandType command,
    std::string error,
    std::string& reason) {
    if (error.empty()) error = "world-command-rejected";
    if (error.size() > 512U) error.resize(512U);

    std::scoped_lock lock(mutex_);
    const auto it = crossings_.find(std::string{crossing_id});
    if (it == crossings_.end()) {
        reason = "object-crossing-not-found";
        return false;
    }
    auto& record = it->second;
    record.last_error = error;

    std::uint32_t* attempts = nullptr;
    switch (command) {
        case ObjectCrossingCommandType::export_source:
            if (record.state != ObjectCrossingState::prepared) {
                reason = "object-crossing-command-state-mismatch";
                return false;
            }
            attempts = &record.export_attempts;
            break;
        case ObjectCrossingCommandType::import_destination:
            if (record.state != ObjectCrossingState::exported) {
                reason = "object-crossing-command-state-mismatch";
                return false;
            }
            attempts = &record.import_attempts;
            break;
        case ObjectCrossingCommandType::remove_source:
            if (record.state != ObjectCrossingState::imported) {
                reason = "object-crossing-command-state-mismatch";
                return false;
            }
            attempts = &record.remove_attempts;
            break;
        case ObjectCrossingCommandType::cleanup_destination:
            if (record.state != ObjectCrossingState::cleanup_pending) {
                reason = "object-crossing-command-state-mismatch";
                return false;
            }
            attempts = &record.cleanup_attempts;
            break;
        case ObjectCrossingCommandType::restore_source:
            if (record.state != ObjectCrossingState::restore_pending) {
                reason = "object-crossing-command-state-mismatch";
                return false;
            }
            attempts = &record.restore_attempts;
            break;
    }

    if (*attempts < max_attempts_) {
        ++(*attempts);
    }
    if (*attempts >= max_attempts_) {
        const auto now = unix_now();
        if (command == ObjectCrossingCommandType::remove_source) {
            record.state = ObjectCrossingState::cleanup_pending;
            record.rollback_reason = "source-remove-failed:" + error;
        } else if (command == ObjectCrossingCommandType::cleanup_destination) {
            record.rollback_reason = "destination-cleanup-failed:" + error;
        } else if (command == ObjectCrossingCommandType::restore_source) {
            record.rollback_reason = "source-restore-failed:" + error;
        } else {
            record.state = ObjectCrossingState::rolled_back;
            record.rolled_back_unix = now;
            record.rollback_reason =
                std::string(object_crossing_command_name(command)) + "-failed:" + error;
        }
    }

    persist_locked();
    reason.clear();
    return true;
}

std::optional<ObjectCrossingRecord> ObjectCrossingStore::rollback(
    const std::string_view crossing_id,
    const std::string_view owner_user_id,
    std::string rollback_reason,
    std::string& reason) {
    if (rollback_reason.empty()) rollback_reason = "client-rollback";
    if (rollback_reason.size() > 512U) rollback_reason.resize(512U);

    std::scoped_lock lock(mutex_);
    const auto it = crossings_.find(std::string{crossing_id});
    if (it == crossings_.end()) {
        reason = "object-crossing-not-found";
        return std::nullopt;
    }
    auto& record = it->second;
    if (record.owner_user_id != owner_user_id) {
        reason = "object-crossing-owner-mismatch";
        return std::nullopt;
    }
    if (record.state == ObjectCrossingState::rolled_back) {
        reason.clear();
        return record;
    }
    if (record.state == ObjectCrossingState::completed) {
        reason = "object-crossing-already-completed";
        return std::nullopt;
    }

    if (record.state == ObjectCrossingState::restore_pending) {
        reason.clear();
        return record;
    }

    record.rollback_reason = std::move(rollback_reason);
    if (record.state == ObjectCrossingState::exported ||
        record.state == ObjectCrossingState::imported ||
        record.state == ObjectCrossingState::cleanup_pending) {
        record.state = ObjectCrossingState::cleanup_pending;
    } else {
        record.state = ObjectCrossingState::rolled_back;
        record.rolled_back_unix = unix_now();
    }
    persist_locked();
    reason.clear();
    return record;
}

std::optional<ObjectCrossingRecord> ObjectCrossingStore::find(
    const std::string_view crossing_id) const {
    std::scoped_lock lock(mutex_);
    const auto it = crossings_.find(std::string{crossing_id});
    return it == crossings_.end()
               ? std::nullopt
               : std::optional<ObjectCrossingRecord>{it->second};
}

std::vector<ObjectCrossingRecord> ObjectCrossingStore::list() const {
    std::scoped_lock lock(mutex_);
    std::vector<ObjectCrossingRecord> result;
    result.reserve(crossings_.size());
    for (const auto& [_, record] : crossings_) result.push_back(record);
    std::sort(result.begin(), result.end(),
              [](const auto& left, const auto& right) {
                  return left.created_unix < right.created_unix;
              });
    return result;
}

std::size_t ObjectCrossingStore::maintenance(const std::int64_t now_unix) {
    std::scoped_lock lock(mutex_);
    std::size_t changed = 0;

    for (auto& [_, record] : crossings_) {
        if (record.expires_unix > now_unix) continue;
        if (record.state == ObjectCrossingState::prepared) {
            record.state = ObjectCrossingState::rolled_back;
            record.rolled_back_unix = now_unix;
            record.rollback_reason = "object-crossing-expired";
            ++changed;
        } else if (record.state == ObjectCrossingState::exported ||
                   record.state == ObjectCrossingState::imported) {
            record.state = ObjectCrossingState::cleanup_pending;
            record.rollback_reason = "object-crossing-expired";
            ++changed;
        }
    }

    constexpr std::int64_t retention_seconds = 24 * 60 * 60;
    for (auto it = crossings_.begin(); it != crossings_.end();) {
        const auto terminal =
            it->second.state == ObjectCrossingState::completed
                ? it->second.completed_unix
                : (it->second.state == ObjectCrossingState::rolled_back
                       ? it->second.rolled_back_unix
                       : 0);
        if (terminal > 0 && terminal + retention_seconds <= now_unix) {
            it = crossings_.erase(it);
            ++changed;
        } else {
            ++it;
        }
    }

    if (changed != 0U) persist_locked();
    return changed;
}

void ObjectCrossingStore::load() {
    std::scoped_lock lock(mutex_);
    crossings_.clear();
    std::ifstream input(path_);
    if (!input) return;

    std::string line;
    while (std::getline(input, line)) {
        if (line.empty() || line[0] == '#') continue;
        const auto fields = split_tab(line);
        if (fields.size() != 26U && fields.size() != 27U) continue;
        try {
            ObjectCrossingRecord record;
            record.id = fields[0];
            record.owner_user_id = fields[1];
            record.source_region = fields[2];
            record.destination_region = fields[3];
            record.source_entity_id = std::stoull(fields[4]);
            record.destination_entity_id = std::stoull(fields[5]);
            record.destination_position = {
                std::stod(fields[6]), std::stod(fields[7]), std::stod(fields[8])};
            record.state = parse_state(fields[9]);
            record.export_attempts = static_cast<std::uint32_t>(std::stoul(fields[10]));
            record.import_attempts = static_cast<std::uint32_t>(std::stoul(fields[11]));
            record.remove_attempts = static_cast<std::uint32_t>(std::stoul(fields[12]));
            record.cleanup_attempts = static_cast<std::uint32_t>(std::stoul(fields[13]));
            record.restore_attempts = static_cast<std::uint32_t>(std::stoul(fields[23]));
            record.created_unix = std::stoll(fields[14]);
            record.expires_unix = std::stoll(fields[15]);
            record.exported_unix = std::stoll(fields[16]);
            record.imported_unix = std::stoll(fields[17]);
            record.completed_unix = std::stoll(fields[18]);
            record.rolled_back_unix = std::stoll(fields[19]);
            record.snapshot = decode_b64(fields[20], 256U * 1024U);
            record.last_error = decode_b64(fields[21], 512U);
            record.rollback_reason = decode_b64(fields[22], 512U);
            if (fields.size() == 27U) {
                record.entity_map = decode_b64(fields[24], 16U * 1024U);
            }
            crossings_[record.id] = std::move(record);
        } catch (...) {
        }
    }
}

void ObjectCrossingStore::persist_locked() const {
    const std::filesystem::path path(path_);
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path());
    }
    const auto temporary = path.string() + ".tmp";
    std::ofstream output(temporary, std::ios::trunc);
    if (!output) throw std::runtime_error("cannot write object crossing store");
    output << "# OpenGenesisLINK object crossing store v2\n";
    output << std::setprecision(17);

    std::vector<ObjectCrossingRecord> rows;
    rows.reserve(crossings_.size());
    for (const auto& [_, record] : crossings_) rows.push_back(record);
    std::sort(rows.begin(), rows.end(),
              [](const auto& left, const auto& right) {
                  return left.created_unix < right.created_unix;
              });

    for (const auto& record : rows) {
        output << record.id << '\t'
               << record.owner_user_id << '\t'
               << record.source_region << '\t'
               << record.destination_region << '\t'
               << record.source_entity_id << '\t'
               << record.destination_entity_id << '\t'
               << record.destination_position.x << '\t'
               << record.destination_position.y << '\t'
               << record.destination_position.z << '\t'
               << object_crossing_state_name(record.state) << '\t'
               << record.export_attempts << '\t'
               << record.import_attempts << '\t'
               << record.remove_attempts << '\t'
               << record.cleanup_attempts << '\t'
               << record.created_unix << '\t'
               << record.expires_unix << '\t'
               << record.exported_unix << '\t'
               << record.imported_unix << '\t'
               << record.completed_unix << '\t'
               << record.rolled_back_unix << '\t'
               << security::base64_encode(record.snapshot) << '\t'
               << security::base64_encode(record.last_error) << '\t'
               << security::base64_encode(record.rollback_reason) << '\t'
               << record.restore_attempts << '\t'
               << security::base64_encode(record.entity_map)
               << "\t0\t0\n";
    }

    output.close();
    if (!output) throw std::runtime_error("cannot flush object crossing store");
    opengenesis::platform::replace_file(temporary, path);
}

} // namespace opengenesis::core
