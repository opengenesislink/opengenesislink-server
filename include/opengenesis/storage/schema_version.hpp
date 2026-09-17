#pragma once

#include <cstdint>
#include <string>

namespace opengenesis::storage {

class SchemaVersionStore final {
public:
    SchemaVersionStore(std::string path, std::uint32_t current_version,
                       std::uint32_t minimum_supported_version = 1);

    [[nodiscard]] std::uint32_t version() const noexcept { return version_; }
    [[nodiscard]] std::uint32_t current_version() const noexcept { return current_version_; }
    [[nodiscard]] bool upgrade_required() const noexcept { return version_ < current_version_; }

    void upgrade_to(std::uint32_t version);

private:
    void load_or_initialize();
    void persist() const;

    std::string path_;
    std::uint32_t current_version_;
    std::uint32_t minimum_supported_version_;
    std::uint32_t version_{0};
};

} // namespace opengenesis::storage
