#pragma once

#include "opengenesis/config/toml_config.hpp"
#include "opengenesis/storage/database.hpp"

namespace opengenesis::storage {

struct DatabaseSelection {
    bool file_mode{true};
    DatabaseConfig config{};
};

[[nodiscard]] DatabaseSelection database_selection_from_config(
    const config::TomlConfig& config,
    bool production_mode);

} // namespace opengenesis::storage
