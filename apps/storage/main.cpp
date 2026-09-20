#include "opengenesis/config/toml_config.hpp"
#include "opengenesis/storage/database.hpp"
#include "opengenesis/storage/database_config.hpp"
#include "opengenesis/storage/migrations.hpp"

#include <iostream>
#include <stdexcept>
#include <string>

int main(int argc, char** argv) {
    try {
        if (argc < 2 || argc > 3) {
            std::cerr
                << "Usage: opengenesis-storage "
                   "<ping|status|migrate> [core.toml]\n";
            return 2;
        }

        const std::string command = argv[1];
        const std::string config_path =
            argc == 3 ? argv[2] : "config/core.toml";
        const auto config =
            opengenesis::config::TomlConfig::load_file(config_path);
        const auto production_mode =
            config.get_bool("security.production_mode", false);
        const auto selection =
            opengenesis::storage::database_selection_from_config(
                config, production_mode);

        if (selection.file_mode) {
            if (command == "status") {
                std::cout
                    << "backend=file\n"
                    << "sql=false\n";
                return 0;
            }
            throw std::runtime_error(
                "database.backend=file has no SQL connection to manage");
        }

        auto database =
            opengenesis::storage::DatabasePool::connect(
                selection.config);

        if (command == "ping") {
            if (!database->ping()) {
                std::cerr << "database ping failed\n";
                return 1;
            }
            std::cout
                << "backend=" << database->backend_name() << "\n"
                << "status=ok\n";
            return 0;
        }

        opengenesis::storage::MigrationRunner migrations(database);
        if (command == "migrate") {
            migrations.migrate();
            std::cout
                << "backend=" << database->backend_name() << "\n"
                << "schema_version="
                << migrations.current_version() << "\n"
                << "latest_version="
                << migrations.latest_version() << "\n";
            return 0;
        }

        if (command == "status") {
            const auto health = database->health();
            std::cout
                << "backend=" << database->backend_name() << "\n"
                << "ready=" << (database->ping() ? "true" : "false")
                << "\n"
                << "pool_size=" << health.pool_size << "\n";
            try {
                std::cout
                    << "schema_version="
                    << migrations.current_version() << "\n"
                    << "latest_version="
                    << migrations.latest_version() << "\n";
            } catch (...) {
                std::cout
                    << "schema_version=uninitialized\n"
                    << "latest_version="
                    << migrations.latest_version() << "\n";
            }
            return 0;
        }

        throw std::runtime_error(
            "unknown command; expected ping, status or migrate");
    } catch (const std::exception& error) {
        std::cerr << "opengenesis-storage: "
                  << error.what() << '\n';
        return 1;
    }
}
