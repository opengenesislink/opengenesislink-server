#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace opengenesis::core {

enum class ObjectCrossingState {
    prepared,
    exported,
    imported,
    cleanup_pending,
    restore_pending,
    completed,
    rolled_back
};

enum class ObjectCrossingCommandType {
    export_source,
    import_destination,
    remove_source,
    cleanup_destination,
    restore_source
};

struct ObjectCrossingVector {
    double x{0.0};
    double y{0.0};
    double z{0.0};
};

struct ObjectCrossingRecord {
    std::string id;
    std::string owner_user_id;
    std::string source_region;
    std::string destination_region;
    std::uint64_t source_entity_id{0};
    std::uint64_t destination_entity_id{0};
    ObjectCrossingVector destination_position;
    std::string snapshot;
    std::string entity_map;
    ObjectCrossingState state{ObjectCrossingState::prepared};
    std::uint32_t export_attempts{0};
    std::uint32_t import_attempts{0};
    std::uint32_t remove_attempts{0};
    std::uint32_t cleanup_attempts{0};
    std::uint32_t restore_attempts{0};
    std::int64_t created_unix{0};
    std::int64_t expires_unix{0};
    std::int64_t exported_unix{0};
    std::int64_t imported_unix{0};
    std::int64_t completed_unix{0};
    std::int64_t rolled_back_unix{0};
    std::string last_error;
    std::string rollback_reason;
};

struct ObjectCrossingCommand {
    ObjectCrossingCommandType type{ObjectCrossingCommandType::export_source};
    ObjectCrossingRecord crossing;
};

class ObjectCrossingStore final {
public:
    explicit ObjectCrossingStore(std::string path, std::uint32_t max_attempts = 5);

    [[nodiscard]] std::optional<ObjectCrossingRecord> prepare(
        std::string owner_user_id,
        std::string source_region,
        std::string destination_region,
        std::uint64_t source_entity_id,
        ObjectCrossingVector destination_position,
        std::int64_t expires_unix,
        std::string& reason);

    [[nodiscard]] std::optional<ObjectCrossingCommand> command_for_region(
        std::string_view region_id) const;

    [[nodiscard]] bool record_export(
        std::string_view crossing_id,
        std::string_view source_region,
        std::string snapshot,
        std::string& reason);

    [[nodiscard]] bool record_import(
        std::string_view crossing_id,
        std::string_view destination_region,
        std::uint64_t destination_entity_id,
        std::string entity_map,
        std::string& reason);

    [[nodiscard]] bool record_remove(
        std::string_view crossing_id,
        std::string_view source_region,
        std::string& reason);

    [[nodiscard]] bool record_cleanup(
        std::string_view crossing_id,
        std::string_view destination_region,
        std::string& reason);

    [[nodiscard]] bool record_restore(
        std::string_view crossing_id,
        std::string_view source_region,
        std::string& reason);

    [[nodiscard]] bool reject_command(
        std::string_view crossing_id,
        ObjectCrossingCommandType command,
        std::string error,
        std::string& reason);

    [[nodiscard]] std::optional<ObjectCrossingRecord> rollback(
        std::string_view crossing_id,
        std::string_view owner_user_id,
        std::string rollback_reason,
        std::string& reason);

    [[nodiscard]] std::optional<ObjectCrossingRecord> find(
        std::string_view crossing_id) const;
    [[nodiscard]] std::vector<ObjectCrossingRecord> list() const;

    std::size_t maintenance(std::int64_t now_unix);

private:
    void load();
    void persist_locked() const;
    static std::uint64_t destination_id_from_crossing(std::string_view crossing_id);

    std::string path_;
    std::uint32_t max_attempts_;
    mutable std::mutex mutex_;
    std::unordered_map<std::string, ObjectCrossingRecord> crossings_;
};

[[nodiscard]] std::string_view object_crossing_state_name(
    ObjectCrossingState state) noexcept;
[[nodiscard]] std::string_view object_crossing_command_name(
    ObjectCrossingCommandType command) noexcept;

} // namespace opengenesis::core
