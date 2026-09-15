#include "opengenesis/world/terrain.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace opengenesis::world {

Terrain::Terrain(const std::size_t width, const std::size_t height, const double cell_size,
                 const double base_height)
    : width_(width), height_(height), cell_size_(cell_size), base_height_(base_height),
      heights_(width * height, base_height) {
    if (width_ < 2 || height_ < 2 || cell_size_ <= 0.0) {
        throw std::runtime_error("invalid terrain dimensions");
    }
}

std::size_t Terrain::index(const std::size_t x, const std::size_t y) const {
    return y * width_ + x;
}

double Terrain::height_at(const std::size_t x, const std::size_t y) const {
    std::scoped_lock lock(mutex_);
    if (x >= width_ || y >= height_) {
        throw std::out_of_range("terrain sample outside bounds");
    }
    return heights_[index(x, y)];
}

double Terrain::sample(const double world_x, const double world_y) const {
    const auto max_x = static_cast<double>(width_ - 1);
    const auto max_y = static_cast<double>(height_ - 1);
    const double gx = std::clamp(world_x / cell_size_, 0.0, max_x);
    const double gy = std::clamp(world_y / cell_size_, 0.0, max_y);
    const auto x0 = static_cast<std::size_t>(std::floor(gx));
    const auto y0 = static_cast<std::size_t>(std::floor(gy));
    const auto x1 = std::min(x0 + 1, width_ - 1);
    const auto y1 = std::min(y0 + 1, height_ - 1);
    const double tx = gx - static_cast<double>(x0);
    const double ty = gy - static_cast<double>(y0);

    std::scoped_lock lock(mutex_);
    const double h00 = heights_[index(x0, y0)];
    const double h10 = heights_[index(x1, y0)];
    const double h01 = heights_[index(x0, y1)];
    const double h11 = heights_[index(x1, y1)];
    const double hx0 = h00 + (h10 - h00) * tx;
    const double hx1 = h01 + (h11 - h01) * tx;
    return hx0 + (hx1 - hx0) * ty;
}

bool Terrain::set_height(const std::size_t x, const std::size_t y, const double value) {
    if (x >= width_ || y >= height_ || !std::isfinite(value)) return false;
    std::scoped_lock lock(mutex_);
    heights_[index(x, y)] = value;
    revision_.fetch_add(1);
    return true;
}

} // namespace opengenesis::world
