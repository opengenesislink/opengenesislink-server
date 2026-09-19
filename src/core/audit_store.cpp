#include "opengenesis/core/audit_store.hpp"
#include "opengenesis/storage/database.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace opengenesis::core {
namespace {

std::int64_t now() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

std::string clean(std::string value) {
    for (char& c : value) {
        if (c == '	' || c == '
' || c == '') c = ' ';
    }
    if (value.size() > 512U) value.resize(512U);
    return value;
}

std::optional<std::string> cell(
    const storage::DatabaseRow& row,
    const std::string& name) {
    const auto it = row.find(name);
    if (it == row.end()) return std::nullopt;
    return it->second;
}

AuditEvent row_to_event(const storage::DatabaseRow& row) {
    return {
        .sequence = static_cast<std::uint64_t>(
            std::stoull(cell(row, "sequence").value_or("0"))),
        .actor = cell(row, "actor").value_or(""),
        .action = cell(row, "action").value_or(""),
        .target = cell(row, "target").value_or(""),
        .detail = cell(row, "detail").value_or(""),
        .unix_time =
            std::stoll(cell(row, "unix_time").value_or("0"))};
}

} // namespace

AuditStore::AuditStore(std::string path)
    : path_(std::move(path)) {
    load();
}

AuditStore::AuditStore(
    std::shared_ptr<storage::DatabasePool> database)
    : database_(std::move(database)) {
    if (!database_) {
        throw std::invalid_argument("audit database is required");
    }
}

void AuditStore::append(
    std::string actor,
    std::string action,
    std::string target,
    std::string detail) {
    actor = clean(std::move(actor));
    action = clean(std::move(action));
    target = clean(std::move(target));
    detail = clean(std::move(detail));

    if (database_) {
        std::scoped_lock lock(mutex_);
        const auto max_sequence = database_->scalar(
            "SELECT MAX(sequence) AS sequence FROM ogl_audit_events");
        const auto sequence =
            max_sequence && !max_sequence->empty()
                ? std::stoull(*max_sequence) + 1U
                : 1U;
        database_->execute(
            "INSERT INTO ogl_audit_events"
            "(sequence,actor,action,target,detail,unix_time)"
            " VALUES(?,?,?,?,?,?)",
            {std::to_string(sequence), actor, action,
             target, detail, std::to_string(now())});
        return;
    }

    std::scoped_lock lock(mutex_);
    AuditEvent event{
        .sequence = next_++,
        .actor = std::move(actor),
        .action = std::move(action),
        .target = std::move(target),
        .detail = std::move(detail),
        .unix_time = now()};
    events_.push_back(event);
    std::filesystem::path path(path_);
    if (path.has_parent_path()) {
        std::filesystem::create_directories(
            path.parent_path());
    }
    std::ofstream output(path_, std::ios::app);
    if (!output) {
        throw std::runtime_error("cannot write audit store");
    }
    output << event.sequence << '	'
           << event.unix_time << '	'
           << event.actor << '	'
           << event.action << '	'
           << event.target << '	'
           << event.detail << '
';
    output.close();
    if (!output) {
        throw std::runtime_error("cannot flush audit store");
    }
}

std::vector<AuditEvent> AuditStore::recent(
    std::size_t limit) const {
    if (database_) {
        limit = std::min<std::size_t>(limit, 5000U);
        const auto rows = database_->query(
            "SELECT sequence,actor,action,target,detail,unix_time "
            "FROM ogl_audit_events ORDER BY sequence DESC LIMIT ?",
            {std::to_string(limit)});
        std::vector<AuditEvent> result;
        result.reserve(rows.size());
        for (const auto& row : rows) {
            result.push_back(row_to_event(row));
        }
        std::reverse(result.begin(), result.end());
        return result;
    }

    std::scoped_lock lock(mutex_);
    limit = std::min(limit, events_.size());
    return std::vector<AuditEvent>(
        events_.end() -
            static_cast<std::ptrdiff_t>(limit),
        events_.end());
}

std::size_t AuditStore::count() const {
    if (database_) {
        const auto value = database_->scalar(
            "SELECT COUNT(*) AS count FROM ogl_audit_events");
        return value
                   ? static_cast<std::size_t>(std::stoull(*value))
                   : 0U;
    }

    std::scoped_lock lock(mutex_);
    return events_.size();
}

void AuditStore::load() {
    if (database_) return;
    std::scoped_lock lock(mutex_);
    events_.clear();
    std::ifstream input(path_);
    std::string line;
    while (std::getline(input, line)) {
        std::istringstream stream(line);
        AuditEvent event;
        std::string sequence;
        std::string time;
        if (!std::getline(stream, sequence, '	') ||
            !std::getline(stream, time, '	') ||
            !std::getline(stream, event.actor, '	') ||
            !std::getline(stream, event.action, '	') ||
            !std::getline(stream, event.target, '	') ||
            !std::getline(stream, event.detail)) {
            continue;
        }
        try {
            event.sequence = std::stoull(sequence);
            event.unix_time = std::stoll(time);
            events_.push_back(event);
            next_ = std::max(next_, event.sequence + 1U);
        } catch (...) {
        }
    }
}

} // namespace opengenesis::core
