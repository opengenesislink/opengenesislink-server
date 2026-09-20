#include "opengenesis/storage/database_config.hpp"

#include <algorithm>
#include <cstdlib>
#include <stdexcept>
#include <string>

namespace opengenesis::storage {

DatabaseSelection database_selection_from_config(
    const config::TomlConfig& source,
    const bool production_mode) {
    const auto backend_name =
        source.get_string("database.backend", "file");
    if (backend_name == "file") {
        return {.file_mode = true, .config = {}};
    }

    const auto backend =
        parse_database_backend(backend_name);
    if (!backend) {
        throw std::runtime_error(
            "database.backend must be file, sqlite, postgresql or mariadb");
    }

    const auto pool_size =
        source.get_int("database.pool_size", 4);
    const auto connect_timeout =
        source.get_int("database.connect_timeout_seconds", 5);
    const auto port_value =
        source.get_int("database.port", 0);
    if (pool_size < 1 || pool_size > 64 ||
        connect_timeout < 1 || connect_timeout > 120 ||
        port_value < 0 || port_value > 65535) {
        throw std::runtime_error(
            "invalid database pool, timeout or port configuration");
    }

    std::string password =
        source.get_string("database.password", "");
    const auto password_env =
        source.get_string(
            "database.password_env",
            "OGL_DATABASE_PASSWORD");
    if (!password_env.empty()) {
#ifdef _WIN32
        char* value = nullptr;
        std::size_t value_size = 0U;
        if (_dupenv_s(
                &value, &value_size,
                password_env.c_str()) == 0 &&
            value != nullptr) {
            if (*value != '\0') password = value;
            std::free(value);
        }
#else
        if (const auto* value =
                std::getenv(password_env.c_str());
            value && *value != '\0') {
            password = value;
        }
#endif
    }

    DatabaseConfig result{
        .backend = *backend,
        .sqlite_path =
            source.get_string(
                "database.sqlite_path",
                "data/opengenesis.db"),
        .host =
            source.get_string(
                "database.host", "127.0.0.1"),
        .port =
            static_cast<std::uint16_t>(port_value),
        .database =
            source.get_string(
                "database.name", "opengenesislink"),
        .user =
            source.get_string("database.user", ""),
        .password = std::move(password),
        .ssl_mode =
            source.get_string(
                "database.ssl_mode", "preferred"),
        .pool_size =
            static_cast<std::size_t>(pool_size),
        .connect_timeout_seconds =
            static_cast<std::uint32_t>(
                connect_timeout)};

    if (result.backend == DatabaseBackend::sqlite) {
        if (result.sqlite_path.empty()) {
            throw std::runtime_error(
                "database.sqlite_path is required");
        }
        return {.file_mode = false, .config = std::move(result)};
    }

    if (result.host.empty() || result.database.empty()) {
        throw std::runtime_error(
            "network database host and name are required");
    }

    const auto ssl_valid =
        result.ssl_mode == "disable" ||
        result.ssl_mode == "preferred" ||
        result.ssl_mode == "required" ||
        result.ssl_mode == "verify-ca" ||
        result.ssl_mode == "verify-full";
    if (!ssl_valid) {
        throw std::runtime_error(
            "database.ssl_mode must be disable, preferred, required, verify-ca or verify-full");
    }

    if (result.backend == DatabaseBackend::mariadb &&
        result.ssl_mode != "disable" &&
        result.ssl_mode != "preferred" &&
        result.ssl_mode != "required") {
        throw std::runtime_error(
            "MariaDB supports database.ssl_mode disable, preferred or required");
    }

    if (production_mode && result.password.empty()) {
        throw std::runtime_error(
            "production SQL backend requires a database password");
    }
    if (production_mode && result.ssl_mode == "disable") {
        throw std::runtime_error(
            "production SQL backend refuses database.ssl_mode=disable");
    }

    return {.file_mode = false, .config = std::move(result)};
}

} // namespace opengenesis::storage
