#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace opengenesis::storage {

enum class DatabaseBackend {
    sqlite,
    postgresql,
    mariadb
};

struct DatabaseConfig {
    DatabaseBackend backend{DatabaseBackend::sqlite};
    std::string sqlite_path{"data/opengenesis.db"};
    std::string host{"127.0.0.1"};
    std::uint16_t port{0};
    std::string database{"opengenesislink"};
    std::string user;
    std::string password;
    std::string ssl_mode{"preferred"};
    std::size_t pool_size{4};
    std::uint32_t connect_timeout_seconds{5};
};

using DatabaseParameter = std::optional<std::string>;
using DatabaseRow =
    std::unordered_map<std::string, std::optional<std::string>>;

struct SqlStatement {
    std::string sql;
    std::vector<DatabaseParameter> parameters;
};

struct DatabaseHealth {
    std::string backend;
    bool ready{false};
    std::size_t pool_size{0};
    std::uint64_t successful_operations{0};
    std::uint64_t failed_operations{0};
    std::string last_error;
};

class DatabasePool final {
public:
    static std::shared_ptr<DatabasePool> connect(DatabaseConfig config);

    ~DatabasePool();
    DatabasePool(const DatabasePool&) = delete;
    DatabasePool& operator=(const DatabasePool&) = delete;

    [[nodiscard]] DatabaseBackend backend() const noexcept;
    [[nodiscard]] std::string backend_name() const;
    [[nodiscard]] const DatabaseConfig& config() const noexcept;

    void execute(
        std::string_view sql,
        const std::vector<DatabaseParameter>& parameters = {});
    [[nodiscard]] std::vector<DatabaseRow> query(
        std::string_view sql,
        const std::vector<DatabaseParameter>& parameters = {});
    [[nodiscard]] std::optional<std::string> scalar(
        std::string_view sql,
        const std::vector<DatabaseParameter>& parameters = {});

    void transaction(const std::vector<SqlStatement>& statements);
    [[nodiscard]] bool ping();
    [[nodiscard]] DatabaseHealth health() const;

private:
    struct Impl;
    explicit DatabasePool(std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> impl_;
};

[[nodiscard]] std::string database_backend_name(DatabaseBackend backend);
[[nodiscard]] std::optional<DatabaseBackend> parse_database_backend(
    std::string_view value);

} // namespace opengenesis::storage
