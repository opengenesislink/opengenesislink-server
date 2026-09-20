#include "opengenesis/core/world_registry.hpp"
#include "opengenesis/platform/filesystem.hpp"
#include "opengenesis/storage/database.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace opengenesis::core {
namespace {

long long epoch_ms(const std::chrono::system_clock::time_point point) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               point.time_since_epoch())
        .count();
}

std::chrono::system_clock::time_point from_ms(const long long value) {
    return std::chrono::system_clock::time_point{
        std::chrono::milliseconds{value}};
}

std::optional<std::string> cell(
    const storage::DatabaseRow& row,
    const std::string& name) {
    const auto it = row.find(name);
    if (it == row.end()) return std::nullopt;
    return it->second;
}

WorldNodeInfo row_to_world(const storage::DatabaseRow& row) {
    const auto id = cell(row, "id").value_or("");
    const auto name = cell(row, "name").value_or("");
    const auto endpoint = cell(row, "endpoint").value_or("");
    const auto state = cell(row, "state").value_or("offline");
    const auto generation = cell(row, "generation").value_or("0");
    const auto registered = cell(row, "registered_ms").value_or("0");
    const auto last_seen = cell(row, "last_seen_ms").value_or("0");
    return {
        .id = id,
        .name = name,
        .endpoint = endpoint,
        .state = state,
        .generation = static_cast<std::uint64_t>(
            std::stoull(generation)),
        .registered_at = from_ms(std::stoll(registered)),
        .last_seen = from_ms(std::stoll(last_seen))};
}

} // namespace

WorldRegistry::WorldRegistry(std::string path)
    : storage_path_(std::move(path)) {
    load();
}

WorldRegistry::WorldRegistry(
    std::shared_ptr<storage::DatabasePool> database)
    : database_(std::move(database)) {
    if (!database_) {
        throw std::invalid_argument("world registry database is required");
    }
}

WorldNodeInfo WorldRegistry::register_or_reconnect(
    std::string id,
    std::string name,
    std::string endpoint) {
    if (id.empty() || name.empty()) {
        throw std::runtime_error("invalid world registration");
    }

    if (database_) {
        const auto now = std::chrono::system_clock::now();
        const auto rows = database_->query(
            "SELECT generation,registered_ms "
            "FROM ogl_world_nodes WHERE id=?",
            {id});
        std::uint64_t generation = 1;
        auto registered = now;
        if (!rows.empty()) {
            const auto old_generation =
                cell(rows.front(), "generation").value_or("0");
            const auto old_registered =
                cell(rows.front(), "registered_ms").value_or("0");
            generation =
                static_cast<std::uint64_t>(
                    std::stoull(old_generation)) +
                1U;
            registered = from_ms(std::stoll(old_registered));
            database_->execute(
                "UPDATE ogl_world_nodes SET "
                "name=?,endpoint=?,state=?,generation=?,last_seen_ms=? "
                "WHERE id=?",
                {name, endpoint, std::string{"online"},
                 std::to_string(generation),
                 std::to_string(epoch_ms(now)), id});
        } else {
            database_->execute(
                "INSERT INTO ogl_world_nodes"
                "(id,name,endpoint,state,generation,registered_ms,last_seen_ms)"
                " VALUES(?,?,?,?,?,?,?)",
                {id, name, endpoint, std::string{"online"},
                 std::to_string(generation),
                 std::to_string(epoch_ms(registered)),
                 std::to_string(epoch_ms(now))});
        }
        return {
            .id = std::move(id),
            .name = std::move(name),
            .endpoint = std::move(endpoint),
            .state = "online",
            .generation = generation,
            .registered_at = registered,
            .last_seen = now};
    }

    std::scoped_lock lock(mutex_);
    auto& node = nodes_[id];
    node.id = std::move(id);
    node.name = std::move(name);
    node.endpoint = std::move(endpoint);
    node.state = "online";
    ++node.generation;
    const auto now = std::chrono::system_clock::now();
    if (node.registered_at.time_since_epoch().count() == 0) {
        node.registered_at = now;
    }
    node.last_seen = now;
    persist_locked();
    return node;
}

bool WorldRegistry::touch(
    const std::string& id,
    const std::uint64_t generation) {
    if (database_) {
        const auto rows = database_->query(
            "SELECT generation FROM ogl_world_nodes WHERE id=?",
            {id});
        if (rows.empty() ||
            std::stoull(
                cell(rows.front(), "generation").value_or("0")) !=
                generation) {
            return false;
        }
        database_->execute(
            "UPDATE ogl_world_nodes SET state=?,last_seen_ms=? "
            "WHERE id=? AND generation=?",
            {std::string{"online"},
             std::to_string(epoch_ms(
                 std::chrono::system_clock::now())),
             id, std::to_string(generation)});
        return true;
    }

    std::scoped_lock lock(mutex_);
    const auto it = nodes_.find(id);
    if (it == nodes_.end() ||
        it->second.generation != generation) {
        return false;
    }
    it->second.last_seen =
        std::chrono::system_clock::now();
    it->second.state = "online";
    persist_locked();
    return true;
}

bool WorldRegistry::mark_offline(
    const std::string& id,
    const std::uint64_t generation) {
    if (database_) {
        const auto rows = database_->query(
            "SELECT generation FROM ogl_world_nodes WHERE id=?",
            {id});
        if (rows.empty() ||
            std::stoull(
                cell(rows.front(), "generation").value_or("0")) !=
                generation) {
            return false;
        }
        database_->execute(
            "UPDATE ogl_world_nodes SET state=? "
            "WHERE id=? AND generation=?",
            {std::string{"offline"},
             id, std::to_string(generation)});
        return true;
    }

    std::scoped_lock lock(mutex_);
    const auto it = nodes_.find(id);
    if (it == nodes_.end() ||
        it->second.generation != generation) {
        return false;
    }
    it->second.state = "offline";
    persist_locked();
    return true;
}

std::size_t WorldRegistry::expire_stale(
    const std::chrono::seconds timeout) {
    const auto now = std::chrono::system_clock::now();

    if (database_) {
        const auto cutoff =
            epoch_ms(now - timeout);
        const auto rows = database_->query(
            "SELECT id,generation FROM ogl_world_nodes "
            "WHERE state=? AND last_seen_ms<?",
            {std::string{"online"}, std::to_string(cutoff)});
        if (rows.empty()) return 0U;

        std::vector<storage::SqlStatement> statements;
        statements.reserve(rows.size());
        for (const auto& row : rows) {
            const auto id = cell(row, "id");
            const auto generation = cell(row, "generation");
            if (!id || !generation) continue;
            statements.push_back({
                .sql =
                    "UPDATE ogl_world_nodes SET state=? "
                    "WHERE id=? AND generation=?",
                .parameters = {
                    std::string{"offline"},
                    *id, *generation}});
        }
        if (!statements.empty()) {
            database_->transaction(statements);
        }
        return statements.size();
    }

    std::scoped_lock lock(mutex_);
    std::size_t count = 0;
    for (auto& [_, node] : nodes_) {
        if (node.state == "online" &&
            now - node.last_seen > timeout) {
            node.state = "offline";
            ++count;
        }
    }
    if (count != 0U) persist_locked();
    return count;
}

std::optional<WorldNodeInfo> WorldRegistry::find(
    const std::string& id) const {
    if (database_) {
        const auto rows = database_->query(
            "SELECT id,name,endpoint,state,generation,"
            "registered_ms,last_seen_ms "
            "FROM ogl_world_nodes WHERE id=?",
            {id});
        if (rows.empty()) return std::nullopt;
        return row_to_world(rows.front());
    }

    std::scoped_lock lock(mutex_);
    if (const auto it = nodes_.find(id);
        it != nodes_.end()) {
        return it->second;
    }
    return std::nullopt;
}

std::vector<WorldNodeInfo> WorldRegistry::list() const {
    if (database_) {
        const auto rows = database_->query(
            "SELECT id,name,endpoint,state,generation,"
            "registered_ms,last_seen_ms "
            "FROM ogl_world_nodes ORDER BY id");
        std::vector<WorldNodeInfo> result;
        result.reserve(rows.size());
        for (const auto& row : rows) {
            result.push_back(row_to_world(row));
        }
        return result;
    }

    std::scoped_lock lock(mutex_);
    std::vector<WorldNodeInfo> out;
    out.reserve(nodes_.size());
    for (const auto& [_, node] : nodes_) {
        out.push_back(node);
    }
    return out;
}

std::size_t WorldRegistry::size() const {
    if (database_) {
        const auto value = database_->scalar(
            "SELECT COUNT(*) AS count FROM ogl_world_nodes");
        return value
                   ? static_cast<std::size_t>(
                         std::stoull(*value))
                   : 0U;
    }

    std::scoped_lock lock(mutex_);
    return nodes_.size();
}

void WorldRegistry::load() {
    if (database_ || storage_path_.empty()) return;
    std::ifstream input(storage_path_);
    std::string line;
    while (std::getline(input, line)) {
        std::istringstream stream(line);
        WorldNodeInfo node;
        long long registered = 0;
        long long last = 0;
        stream >> std::quoted(node.id)
               >> std::quoted(node.name)
               >> std::quoted(node.endpoint)
               >> std::quoted(node.state)
               >> node.generation
               >> registered
               >> last;
        if (!node.id.empty()) {
            node.registered_at = from_ms(registered);
            node.last_seen = from_ms(last);
            node.state = "offline";
            nodes_[node.id] = std::move(node);
        }
    }
}

void WorldRegistry::persist_locked() const {
    if (database_ || storage_path_.empty()) return;
    const std::filesystem::path path(storage_path_);
    if (path.has_parent_path()) {
        std::filesystem::create_directories(
            path.parent_path());
    }
    const auto temporary = storage_path_ + ".tmp";
    std::ofstream output(
        temporary, std::ios::trunc);
    if (!output) {
        throw std::runtime_error(
            "cannot persist world registry");
    }
    for (const auto& [_, node] : nodes_) {
        output << std::quoted(node.id) << ' '
               << std::quoted(node.name) << ' '
               << std::quoted(node.endpoint) << ' '
               << std::quoted(node.state) << ' '
               << node.generation << ' '
               << epoch_ms(node.registered_at) << ' '
               << epoch_ms(node.last_seen) << '\n';
    }
    output.close();
    if (!output) {
        throw std::runtime_error(
            "cannot flush world registry");
    }
    opengenesis::platform::replace_file(
        temporary, storage_path_);
}

} // namespace opengenesis::core
