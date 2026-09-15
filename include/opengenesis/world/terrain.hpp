#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

namespace opengenesis::world {

class Terrain final {
public:
    Terrain(std::size_t width = 256, std::size_t height = 256, double cell_size = 1.0,
            double base_height = 21.0);

    [[nodiscard]] std::size_t width() const { return width_; }
    [[nodiscard]] std::size_t height() const { return height_; }
    [[nodiscard]] double cell_size() const { return cell_size_; }
    [[nodiscard]] double base_height() const { return base_height_; }
    [[nodiscard]] std::uint64_t revision() const { return revision_.load(); }

    [[nodiscard]] double height_at(std::size_t x, std::size_t y) const;
    [[nodiscard]] double sample(double world_x, double world_y) const;
    bool set_height(std::size_t x, std::size_t y, double value);
    [[nodiscard]] std::vector<double> snapshot_heights() const;
    bool restore_heights(std::vector<double> heights, std::uint64_t revision);

private:
    [[nodiscard]] std::size_t index(std::size_t x, std::size_t y) const;

    std::size_t width_;
    std::size_t height_;
    double cell_size_;
    double base_height_;
    mutable std::mutex mutex_;
    std::vector<double> heights_;
    std::atomic<std::uint64_t> revision_{1};
};

} // namespace opengenesis::world
