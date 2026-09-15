#pragma once

#include <cstdint>
#include <filesystem>

namespace opengenesis::world {

class RegionRuntime;

class RegionPersistence final {
public:
    explicit RegionPersistence(std::filesystem::path directory);

    void load(RegionRuntime& runtime);
    void save(const RegionRuntime& runtime, bool force = false);

    [[nodiscard]] const std::filesystem::path& directory() const { return directory_; }

private:
    std::filesystem::path directory_;
    std::uint64_t saved_sequence_{0};
    std::uint64_t saved_terrain_revision_{0};
};

} // namespace opengenesis::world
