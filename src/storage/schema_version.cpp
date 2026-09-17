#include "opengenesis/storage/schema_version.hpp"
#include "opengenesis/platform/filesystem.hpp"

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <utility>

namespace opengenesis::storage {

SchemaVersionStore::SchemaVersionStore(std::string path,
                                       const std::uint32_t current_version,
                                       const std::uint32_t minimum_supported_version)
    : path_(std::move(path)),
      current_version_(current_version),
      minimum_supported_version_(minimum_supported_version) {
    if (current_version_ == 0 || minimum_supported_version_ == 0 ||
        minimum_supported_version_ > current_version_) {
        throw std::invalid_argument("invalid schema version policy");
    }
    load_or_initialize();
}

void SchemaVersionStore::load_or_initialize() {
    std::ifstream input(path_);
    if (!input) {
        version_ = current_version_;
        persist();
        return;
    }

    std::uint64_t value = 0;
    input >> value;
    if (!input || value == 0 || value > UINT32_MAX) {
        throw std::runtime_error("invalid storage schema version");
    }

    version_ = static_cast<std::uint32_t>(value);
    if (version_ > current_version_) {
        throw std::runtime_error("storage schema is newer than this server");
    }
    if (version_ < minimum_supported_version_) {
        throw std::runtime_error("storage schema is too old for direct upgrade");
    }
}

void SchemaVersionStore::upgrade_to(const std::uint32_t version) {
    if (version <= version_ || version > current_version_) {
        throw std::invalid_argument("invalid storage schema upgrade target");
    }
    version_ = version;
    persist();
}

void SchemaVersionStore::persist() const {
    const std::filesystem::path path(path_);
    if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path());

    const auto temp = path.string() + ".tmp";
    std::ofstream output(temp, std::ios::trunc);
    if (!output) throw std::runtime_error("cannot write schema version");
    output << version_ << '\n';
    output.close();
    if (!output) throw std::runtime_error("cannot flush schema version");

    opengenesis::platform::replace_file(temp, path);
}

} // namespace opengenesis::storage
