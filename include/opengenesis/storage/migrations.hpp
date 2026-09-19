#pragma once

#include "opengenesis/storage/database.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace opengenesis::storage {

struct AppliedMigration {
    std::uint32_t version{0};
    std::string name;
    std::int64_t applied_unix{0};
};

class MigrationRunner final {
public:
    explicit MigrationRunner(std::shared_ptr<DatabasePool> database);

    void migrate();
    [[nodiscard]] std::uint32_t current_version() const;
    [[nodiscard]] std::vector<AppliedMigration> applied() const;

    [[nodiscard]] static std::uint32_t latest_version() noexcept;

private:
    std::shared_ptr<DatabasePool> database_;
};

} // namespace opengenesis::storage
