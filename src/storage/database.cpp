#include "opengenesis/storage/database.hpp"

#include <sqlite3.h>
#include <libpq-fe.h>

#if __has_include(<mariadb/mysql.h>)
#include <mariadb/mysql.h>
#elif __has_include(<mysql.h>)
#include <mysql.h>
#else
#error "MariaDB Connector/C headers are required"
#endif

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace opengenesis::storage {
namespace {

class Connection {
public:
    virtual ~Connection() = default;
    virtual void execute(
        std::string_view sql,
        const std::vector<DatabaseParameter>& parameters) = 0;
    [[nodiscard]] virtual std::vector<DatabaseRow> query(
        std::string_view sql,
        const std::vector<DatabaseParameter>& parameters) = 0;
    [[nodiscard]] virtual bool ping() = 0;
};

std::string lower(std::string_view value) {
    std::string result(value);
    std::transform(
        result.begin(), result.end(), result.begin(),
        [](const unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
    return result;
}

std::string pg_placeholders(std::string_view sql) {
    std::string output;
    output.reserve(sql.size() + 16U);
    std::size_t parameter = 1U;
    bool quote = false;
    for (std::size_t index = 0; index < sql.size(); ++index) {
        const char c = sql[index];
        if (c == '\'' && (index == 0U || sql[index - 1U] != '\\')) {
            quote = !quote;
            output.push_back(c);
            continue;
        }
        if (c == '?' && !quote) {
            output.push_back('$');
            output += std::to_string(parameter++);
        } else {
            output.push_back(c);
        }
    }
    return output;
}

class SqliteConnection final : public Connection {
public:
    explicit SqliteConnection(const DatabaseConfig& config) {
        if (config.sqlite_path != ":memory:") {
            const std::filesystem::path path(config.sqlite_path);
            if (path.has_parent_path()) {
                std::filesystem::create_directories(path.parent_path());
            }
        }
        const auto flags =
            SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX;
        if (sqlite3_open_v2(
                config.sqlite_path.c_str(), &db_, flags, nullptr) !=
            SQLITE_OK) {
            const auto message =
                db_ ? sqlite3_errmsg(db_) : "sqlite open failed";
            if (db_) sqlite3_close(db_);
            db_ = nullptr;
            throw std::runtime_error(message);
        }
        sqlite3_busy_timeout(
            db_, static_cast<int>(
                     std::max<std::uint32_t>(
                         1U, config.connect_timeout_seconds) *
                     1000U));
        direct("PRAGMA foreign_keys=ON");
        if (config.sqlite_path != ":memory:") {
            direct("PRAGMA journal_mode=WAL");
            direct("PRAGMA synchronous=NORMAL");
        }
    }

    ~SqliteConnection() override {
        if (db_) sqlite3_close(db_);
    }

    void execute(
        const std::string_view sql,
        const std::vector<DatabaseParameter>& parameters) override {
        sqlite3_stmt* raw = nullptr;
        if (sqlite3_prepare_v2(
                db_, std::string{sql}.c_str(), -1, &raw, nullptr) !=
            SQLITE_OK) {
            throw std::runtime_error(sqlite3_errmsg(db_));
        }
        const auto stmt = std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)>(
            raw, &sqlite3_finalize);
        bind(stmt.get(), parameters);
        const auto result = sqlite3_step(stmt.get());
        if (result != SQLITE_DONE && result != SQLITE_ROW) {
            throw std::runtime_error(sqlite3_errmsg(db_));
        }
    }

    std::vector<DatabaseRow> query(
        const std::string_view sql,
        const std::vector<DatabaseParameter>& parameters) override {
        sqlite3_stmt* raw = nullptr;
        if (sqlite3_prepare_v2(
                db_, std::string{sql}.c_str(), -1, &raw, nullptr) !=
            SQLITE_OK) {
            throw std::runtime_error(sqlite3_errmsg(db_));
        }
        const auto stmt = std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)>(
            raw, &sqlite3_finalize);
        bind(stmt.get(), parameters);

        std::vector<DatabaseRow> rows;
        while (true) {
            const auto result = sqlite3_step(stmt.get());
            if (result == SQLITE_DONE) break;
            if (result != SQLITE_ROW) {
                throw std::runtime_error(sqlite3_errmsg(db_));
            }
            DatabaseRow row;
            const auto count = sqlite3_column_count(stmt.get());
            for (int column = 0; column < count; ++column) {
                const auto* name = sqlite3_column_name(stmt.get(), column);
                if (sqlite3_column_type(stmt.get(), column) == SQLITE_NULL) {
                    row[std::string{name}] = std::nullopt;
                    continue;
                }
                const auto* text = sqlite3_column_text(stmt.get(), column);
                const auto bytes = sqlite3_column_bytes(stmt.get(), column);
                row[std::string{name}] =
                    std::string{
                        reinterpret_cast<const char*>(text),
                        static_cast<std::size_t>(bytes)};
            }
            rows.push_back(std::move(row));
        }
        return rows;
    }

    bool ping() override {
        try {
            return !query("SELECT 1 AS ok", {}).empty();
        } catch (...) {
            return false;
        }
    }

private:
    void direct(const char* sql) {
        char* error = nullptr;
        if (sqlite3_exec(db_, sql, nullptr, nullptr, &error) != SQLITE_OK) {
            const std::string message =
                error ? std::string{error} : "sqlite execution failed";
            sqlite3_free(error);
            throw std::runtime_error(message);
        }
    }

    void bind(
        sqlite3_stmt* stmt,
        const std::vector<DatabaseParameter>& parameters) {
        const auto expected = sqlite3_bind_parameter_count(stmt);
        if (expected != static_cast<int>(parameters.size())) {
            throw std::runtime_error("sqlite parameter count mismatch");
        }
        for (std::size_t index = 0; index < parameters.size(); ++index) {
            const auto position = static_cast<int>(index + 1U);
            const auto& parameter = parameters[index];
            const auto result = parameter
                                    ? sqlite3_bind_text(
                                          stmt, position,
                                          parameter->data(),
                                          static_cast<int>(parameter->size()),
                                          SQLITE_TRANSIENT)
                                    : sqlite3_bind_null(stmt, position);
            if (result != SQLITE_OK) {
                throw std::runtime_error(sqlite3_errmsg(db_));
            }
        }
    }

    sqlite3* db_{nullptr};
};

class PostgresConnection final : public Connection {
public:
    explicit PostgresConnection(const DatabaseConfig& config) {
        const auto port =
            config.port == 0 ? std::uint16_t{5432} : config.port;
        const auto timeout =
            std::to_string(
                std::max<std::uint32_t>(
                    1U, config.connect_timeout_seconds));
        const auto port_text = std::to_string(port);
        const auto ssl_mode =
            config.ssl_mode == "preferred" ? std::string{"prefer"} :
            (config.ssl_mode == "required" ? std::string{"require"} :
             config.ssl_mode);
        const char* keywords[] = {
            "host", "port", "dbname", "user", "password",
            "connect_timeout", "sslmode", nullptr};
        const char* values[] = {
            config.host.c_str(), port_text.c_str(),
            config.database.c_str(), config.user.c_str(),
            config.password.c_str(), timeout.c_str(),
            ssl_mode.c_str(), nullptr};
        connection_ = PQconnectdbParams(keywords, values, 0);
        if (!connection_ ||
            PQstatus(connection_) != CONNECTION_OK) {
            const auto message =
                connection_
                    ? std::string{PQerrorMessage(connection_)}
                    : std::string{"postgresql connection failed"};
            if (connection_) PQfinish(connection_);
            connection_ = nullptr;
            throw std::runtime_error(message);
        }
    }

    ~PostgresConnection() override {
        if (connection_) PQfinish(connection_);
    }

    void execute(
        const std::string_view sql,
        const std::vector<DatabaseParameter>& parameters) override {
        const auto result = run(sql, parameters);
        const auto status = PQresultStatus(result.get());
        if (status != PGRES_COMMAND_OK &&
            status != PGRES_TUPLES_OK) {
            throw std::runtime_error(PQresultErrorMessage(result.get()));
        }
    }

    std::vector<DatabaseRow> query(
        const std::string_view sql,
        const std::vector<DatabaseParameter>& parameters) override {
        const auto result = run(sql, parameters);
        if (PQresultStatus(result.get()) != PGRES_TUPLES_OK) {
            throw std::runtime_error(PQresultErrorMessage(result.get()));
        }
        std::vector<DatabaseRow> rows;
        const auto row_count = PQntuples(result.get());
        const auto column_count = PQnfields(result.get());
        rows.reserve(static_cast<std::size_t>(row_count));
        for (int row_index = 0; row_index < row_count; ++row_index) {
            DatabaseRow row;
            for (int column = 0; column < column_count; ++column) {
                const auto* name = PQfname(result.get(), column);
                if (PQgetisnull(result.get(), row_index, column) != 0) {
                    row[std::string{name}] = std::nullopt;
                } else {
                    row[std::string{name}] = std::string{
                        PQgetvalue(result.get(), row_index, column),
                        static_cast<std::size_t>(
                            PQgetlength(result.get(), row_index, column))};
                }
            }
            rows.push_back(std::move(row));
        }
        return rows;
    }

    bool ping() override {
        if (!connection_ || PQstatus(connection_) != CONNECTION_OK) {
            return false;
        }
        try {
            return !query("SELECT 1 AS ok", {}).empty();
        } catch (...) {
            return false;
        }
    }

private:
    using ResultPtr =
        std::unique_ptr<PGresult, decltype(&PQclear)>;

    ResultPtr run(
        const std::string_view sql,
        const std::vector<DatabaseParameter>& parameters) {
        const auto statement = pg_placeholders(sql);
        std::vector<const char*> values(parameters.size(), nullptr);
        std::vector<int> lengths(parameters.size(), 0);
        std::vector<int> formats(parameters.size(), 0);
        for (std::size_t index = 0; index < parameters.size(); ++index) {
            if (!parameters[index]) continue;
            values[index] = parameters[index]->c_str();
            lengths[index] =
                static_cast<int>(parameters[index]->size());
        }
        auto* result = PQexecParams(
            connection_, statement.c_str(),
            static_cast<int>(parameters.size()), nullptr,
            values.data(), lengths.data(), formats.data(), 0);
        if (!result) {
            throw std::runtime_error(PQerrorMessage(connection_));
        }
        return ResultPtr(result, &PQclear);
    }

    PGconn* connection_{nullptr};
};

class MariaDbConnection final : public Connection {
public:
    explicit MariaDbConnection(const DatabaseConfig& config) {
        connection_ = mysql_init(nullptr);
        if (!connection_) {
            throw std::runtime_error("mariadb mysql_init failed");
        }
        const auto timeout =
            static_cast<unsigned int>(
                std::max<std::uint32_t>(
                    1U, config.connect_timeout_seconds));
        mysql_options(
            connection_, MYSQL_OPT_CONNECT_TIMEOUT, &timeout);
        const auto port =
            config.port == 0 ? std::uint16_t{3306} : config.port;
        unsigned long flags = 0;
        if (config.ssl_mode == "required") flags |= CLIENT_SSL;
        if (!mysql_real_connect(
                connection_, config.host.c_str(),
                config.user.empty() ? nullptr : config.user.c_str(),
                config.password.empty() ? nullptr : config.password.c_str(),
                config.database.c_str(),
                static_cast<unsigned int>(port),
                nullptr, flags)) {
            const auto message = std::string{mysql_error(connection_)};
            mysql_close(connection_);
            connection_ = nullptr;
            throw std::runtime_error(message);
        }
        if (mysql_set_character_set(connection_, "utf8mb4") != 0) {
            throw std::runtime_error(mysql_error(connection_));
        }
    }

    ~MariaDbConnection() override {
        if (connection_) mysql_close(connection_);
    }

    void execute(
        const std::string_view sql,
        const std::vector<DatabaseParameter>& parameters) override {
        auto statement = prepare(sql, parameters);
        if (mysql_stmt_execute(statement.get()) != 0) {
            throw std::runtime_error(mysql_stmt_error(statement.get()));
        }
    }

    std::vector<DatabaseRow> query(
        const std::string_view sql,
        const std::vector<DatabaseParameter>& parameters) override {
        auto statement = prepare(sql, parameters);
        if (mysql_stmt_execute(statement.get()) != 0) {
            throw std::runtime_error(mysql_stmt_error(statement.get()));
        }
        MYSQL_RES* raw_metadata =
            mysql_stmt_result_metadata(statement.get());
        if (!raw_metadata) {
            if (mysql_stmt_field_count(statement.get()) == 0U) {
                return {};
            }
            throw std::runtime_error(mysql_stmt_error(statement.get()));
        }
        const auto metadata =
            std::unique_ptr<MYSQL_RES, decltype(&mysql_free_result)>(
                raw_metadata, &mysql_free_result);
        if (mysql_stmt_store_result(statement.get()) != 0) {
            throw std::runtime_error(mysql_stmt_error(statement.get()));
        }

        const auto columns =
            static_cast<std::size_t>(mysql_num_fields(metadata.get()));
        auto* fields = mysql_fetch_fields(metadata.get());

        using MariaBool =
            std::remove_pointer_t<decltype(MYSQL_BIND{}.is_null)>;
        std::vector<MYSQL_BIND> bindings(columns);
        std::vector<unsigned long> lengths(columns, 0UL);
        std::vector<MariaBool> nulls(columns);
        std::vector<MariaBool> errors(columns);
        std::vector<char> scratch(columns, '\0');

        for (std::size_t index = 0; index < columns; ++index) {
            bindings[index] = MYSQL_BIND{};
            bindings[index].buffer_type = MYSQL_TYPE_STRING;
            bindings[index].buffer = &scratch[index];
            bindings[index].buffer_length = 1UL;
            bindings[index].length = &lengths[index];
            bindings[index].is_null = &nulls[index];
            bindings[index].error = &errors[index];
        }
        if (mysql_stmt_bind_result(
                statement.get(), bindings.data()) != 0) {
            throw std::runtime_error(mysql_stmt_error(statement.get()));
        }

        std::vector<DatabaseRow> rows;
        while (true) {
            const auto status = mysql_stmt_fetch(statement.get());
            if (status == MYSQL_NO_DATA) break;
            if (status != 0 && status != MYSQL_DATA_TRUNCATED) {
                throw std::runtime_error(mysql_stmt_error(statement.get()));
            }
            DatabaseRow row;
            for (std::size_t index = 0; index < columns; ++index) {
                const std::string name{fields[index].name};
                if (nulls[index] != 0) {
                    row[name] = std::nullopt;
                    continue;
                }
                std::string value(lengths[index], '\0');
                if (!value.empty()) {
                    MYSQL_BIND column{};
                    column.buffer_type = MYSQL_TYPE_STRING;
                    column.buffer = value.data();
                    column.buffer_length =
                        static_cast<unsigned long>(value.size());
                    unsigned long actual = 0UL;
                    column.length = &actual;
                    if (mysql_stmt_fetch_column(
                            statement.get(), &column,
                            static_cast<unsigned int>(index), 0UL) != 0) {
                        throw std::runtime_error(
                            mysql_stmt_error(statement.get()));
                    }
                    value.resize(actual);
                }
                row[name] = std::move(value);
            }
            rows.push_back(std::move(row));
        }
        mysql_stmt_free_result(statement.get());
        return rows;
    }

    bool ping() override {
        return connection_ && mysql_ping(connection_) == 0;
    }

private:
    using StatementPtr =
        std::unique_ptr<MYSQL_STMT, decltype(&mysql_stmt_close)>;

    StatementPtr prepare(
        const std::string_view sql,
        const std::vector<DatabaseParameter>& parameters) {
        auto* raw = mysql_stmt_init(connection_);
        if (!raw) {
            throw std::runtime_error(mysql_error(connection_));
        }
        StatementPtr statement(raw, &mysql_stmt_close);
        if (mysql_stmt_prepare(
                statement.get(), sql.data(),
                static_cast<unsigned long>(sql.size())) != 0) {
            throw std::runtime_error(mysql_stmt_error(statement.get()));
        }
        if (mysql_stmt_param_count(statement.get()) != parameters.size()) {
            throw std::runtime_error("mariadb parameter count mismatch");
        }
        if (parameters.empty()) return statement;

        parameter_storage_.clear();
        parameter_storage_.reserve(parameters.size());
        for (const auto& parameter : parameters) {
            parameter_storage_.push_back(parameter.value_or(std::string{}));
        }
        parameter_bindings_.assign(parameters.size(), MYSQL_BIND{});
        parameter_lengths_.assign(parameters.size(), 0UL);
        for (std::size_t index = 0; index < parameters.size(); ++index) {
            auto& binding = parameter_bindings_[index];
            if (!parameters[index]) {
                binding.buffer_type = MYSQL_TYPE_NULL;
                continue;
            }
            parameter_lengths_[index] =
                static_cast<unsigned long>(
                    parameter_storage_[index].size());
            binding.buffer_type = MYSQL_TYPE_STRING;
            binding.buffer = parameter_storage_[index].data();
            binding.buffer_length = parameter_lengths_[index];
            binding.length = &parameter_lengths_[index];
        }
        if (mysql_stmt_bind_param(
                statement.get(), parameter_bindings_.data()) != 0) {
            throw std::runtime_error(mysql_stmt_error(statement.get()));
        }
        return statement;
    }

    MYSQL* connection_{nullptr};
    std::vector<std::string> parameter_storage_;
    std::vector<MYSQL_BIND> parameter_bindings_;
    std::vector<unsigned long> parameter_lengths_;
};

std::unique_ptr<Connection> open_connection(
    const DatabaseConfig& config) {
    switch (config.backend) {
        case DatabaseBackend::sqlite:
            return std::make_unique<SqliteConnection>(config);
        case DatabaseBackend::postgresql:
            return std::make_unique<PostgresConnection>(config);
        case DatabaseBackend::mariadb:
            return std::make_unique<MariaDbConnection>(config);
    }
    throw std::runtime_error("unknown database backend");
}

} // namespace

struct DatabasePool::Impl {
    struct Slot {
        explicit Slot(std::unique_ptr<Connection> value)
            : connection(std::move(value)) {}
        std::unique_ptr<Connection> connection;
        std::mutex mutex;
    };

    explicit Impl(DatabaseConfig settings)
        : config(std::move(settings)) {
        if (config.pool_size == 0U || config.pool_size > 64U) {
            throw std::invalid_argument(
                "database pool_size must be between 1 and 64");
        }
        if (config.backend == DatabaseBackend::sqlite &&
            config.sqlite_path == ":memory:") {
            config.pool_size = 1U;
        }
        slots.reserve(config.pool_size);
        for (std::size_t index = 0; index < config.pool_size; ++index) {
            slots.push_back(
                std::make_unique<Slot>(open_connection(config)));
        }
        ready.store(true);
    }

    template <typename Function>
    decltype(auto) with_slot(Function&& function) {
        const auto index =
            cursor.fetch_add(1U) % slots.size();
        auto& slot = *slots[index];
        std::scoped_lock lock(slot.mutex);
        try {
            if constexpr (std::is_void_v<
                              std::invoke_result_t<
                                  Function, Connection&>>) {
                std::forward<Function>(function)(
                    *slot.connection);
                successful.fetch_add(1U);
                ready.store(true);
                return;
            } else {
                auto result =
                    std::forward<Function>(function)(
                        *slot.connection);
                successful.fetch_add(1U);
                ready.store(true);
                return result;
            }
        } catch (const std::exception& error) {
            failed.fetch_add(1U);
            ready.store(false);
            {
                std::scoped_lock error_lock(error_mutex);
                last_error = error.what();
                if (last_error.size() > 1024U) {
                    last_error.resize(1024U);
                }
            }
            try {
                slot.connection = open_connection(config);
                ready.store(true);
            } catch (...) {
                ready.store(false);
            }
            throw;
        }
    }

    DatabaseConfig config;
    std::vector<std::unique_ptr<Slot>> slots;
    std::atomic<std::size_t> cursor{0U};
    std::atomic<std::uint64_t> successful{0U};
    std::atomic<std::uint64_t> failed{0U};
    std::atomic_bool ready{false};
    mutable std::mutex error_mutex;
    std::string last_error;
};

DatabasePool::DatabasePool(std::unique_ptr<Impl> impl)
    : impl_(std::move(impl)) {}

DatabasePool::~DatabasePool() = default;

std::shared_ptr<DatabasePool> DatabasePool::connect(
    DatabaseConfig config) {
    if (config.database.empty() &&
        config.backend != DatabaseBackend::sqlite) {
        throw std::invalid_argument("database name is required");
    }
    return std::shared_ptr<DatabasePool>(
        new DatabasePool(
            std::make_unique<Impl>(std::move(config))));
}

DatabaseBackend DatabasePool::backend() const noexcept {
    return impl_->config.backend;
}

std::string DatabasePool::backend_name() const {
    return database_backend_name(backend());
}

const DatabaseConfig& DatabasePool::config() const noexcept {
    return impl_->config;
}

void DatabasePool::execute(
    const std::string_view sql,
    const std::vector<DatabaseParameter>& parameters) {
    impl_->with_slot([&](Connection& connection) {
        connection.execute(sql, parameters);
    });
}

std::vector<DatabaseRow> DatabasePool::query(
    const std::string_view sql,
    const std::vector<DatabaseParameter>& parameters) {
    return impl_->with_slot([&](Connection& connection) {
        return connection.query(sql, parameters);
    });
}

std::optional<std::string> DatabasePool::scalar(
    const std::string_view sql,
    const std::vector<DatabaseParameter>& parameters) {
    const auto rows = query(sql, parameters);
    if (rows.empty() || rows.front().empty()) return std::nullopt;
    return rows.front().begin()->second;
}

void DatabasePool::transaction(
    const std::vector<SqlStatement>& statements) {
    impl_->with_slot([&](Connection& connection) {
        connection.execute("BEGIN", {});
        try {
            for (const auto& statement : statements) {
                connection.execute(
                    statement.sql, statement.parameters);
            }
            connection.execute("COMMIT", {});
        } catch (...) {
            try {
                connection.execute("ROLLBACK", {});
            } catch (...) {
            }
            throw;
        }
    });
}

bool DatabasePool::ping() {
    const auto index =
        impl_->cursor.fetch_add(1U) % impl_->slots.size();
    auto& slot = *impl_->slots[index];
    std::scoped_lock lock(slot.mutex);
    try {
        if (!slot.connection->ping()) {
            slot.connection = open_connection(impl_->config);
        }
        const auto ready = slot.connection->ping();
        impl_->ready.store(ready);
        if (ready) {
            impl_->successful.fetch_add(1U);
            return true;
        }
        impl_->failed.fetch_add(1U);
        return false;
    } catch (const std::exception& error) {
        impl_->failed.fetch_add(1U);
        impl_->ready.store(false);
        {
            std::scoped_lock error_lock(impl_->error_mutex);
            impl_->last_error = error.what();
            if (impl_->last_error.size() > 1024U) {
                impl_->last_error.resize(1024U);
            }
        }
        return false;
    }
}

DatabaseHealth DatabasePool::health() const {
    DatabaseHealth result{
        .backend = database_backend_name(impl_->config.backend),
        .ready = impl_->ready.load(),
        .pool_size = impl_->slots.size(),
        .successful_operations = impl_->successful.load(),
        .failed_operations = impl_->failed.load(),
        .last_error = {}};
    {
        std::scoped_lock lock(impl_->error_mutex);
        result.last_error = impl_->last_error;
    }
    return result;
}

std::string database_backend_name(
    const DatabaseBackend backend) {
    switch (backend) {
        case DatabaseBackend::sqlite: return "sqlite";
        case DatabaseBackend::postgresql: return "postgresql";
        case DatabaseBackend::mariadb: return "mariadb";
    }
    return "unknown";
}

std::optional<DatabaseBackend> parse_database_backend(
    const std::string_view value) {
    const auto normalized = lower(value);
    if (normalized == "sqlite" || normalized == "sqlite3") {
        return DatabaseBackend::sqlite;
    }
    if (normalized == "postgresql" ||
        normalized == "postgres" || normalized == "pgsql") {
        return DatabaseBackend::postgresql;
    }
    if (normalized == "mariadb" || normalized == "mysql") {
        return DatabaseBackend::mariadb;
    }
    return std::nullopt;
}

} // namespace opengenesis::storage
