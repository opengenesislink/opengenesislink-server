#include "opengenesis/world/region_persistence.hpp"

#include "opengenesis/common/log.hpp"
#include "opengenesis/world/region_runtime.hpp"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace opengenesis::world {
namespace {

void put_u32(std::ostream& out, const std::uint32_t value) {
    for (unsigned shift = 0; shift < 32; shift += 8) out.put(static_cast<char>((value >> shift) & 0xffU));
}

void put_u64(std::ostream& out, const std::uint64_t value) {
    for (unsigned shift = 0; shift < 64; shift += 8) out.put(static_cast<char>((value >> shift) & 0xffU));
}

std::uint32_t get_u32(std::istream& in) {
    std::uint32_t value = 0;
    for (unsigned shift = 0; shift < 32; shift += 8) {
        const int c = in.get();
        if (c == EOF) throw std::runtime_error("truncated terrain file");
        value |= static_cast<std::uint32_t>(static_cast<unsigned char>(c)) << shift;
    }
    return value;
}

std::uint64_t get_u64(std::istream& in) {
    std::uint64_t value = 0;
    for (unsigned shift = 0; shift < 64; shift += 8) {
        const int c = in.get();
        if (c == EOF) throw std::runtime_error("truncated terrain file");
        value |= static_cast<std::uint64_t>(static_cast<unsigned char>(c)) << shift;
    }
    return value;
}

void put_double(std::ostream& out, const double value) { put_u64(out, std::bit_cast<std::uint64_t>(value)); }
double get_double(std::istream& in) { return std::bit_cast<double>(get_u64(in)); }

std::string text_hex(const std::string_view value) {
    constexpr char hex[] = "0123456789abcdef";
    std::string output(value.size() * 2, '0');
    for (std::size_t i = 0; i < value.size(); ++i) {
        const auto c = static_cast<unsigned char>(value[i]);
        output[i * 2] = hex[(c >> 4U) & 0x0fU];
        output[i * 2 + 1] = hex[c & 0x0fU];
    }
    return output;
}

unsigned char nibble(const char c) {
    if (c >= '0' && c <= '9') return static_cast<unsigned char>(c - '0');
    if (c >= 'a' && c <= 'f') return static_cast<unsigned char>(c - 'a' + 10);
    if (c >= 'A' && c <= 'F') return static_cast<unsigned char>(c - 'A' + 10);
    throw std::runtime_error("invalid text hex");
}

std::string text_unhex(const std::string_view value) {
    if ((value.size() % 2) != 0) throw std::runtime_error("invalid text hex length");
    std::string output(value.size() / 2, '\0');
    for (std::size_t i = 0; i < output.size(); ++i) {
        output[i] = static_cast<char>((nibble(value[i * 2]) << 4U) | nibble(value[i * 2 + 1]));
    }
    return output;
}

std::vector<std::string> split_tabs(const std::string& line) {
    std::vector<std::string> fields;
    std::size_t start = 0;
    while (true) {
        const auto end = line.find('\t', start);
        fields.push_back(line.substr(start, end == std::string::npos ? std::string::npos : end - start));
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return fields;
}

} // namespace

RegionPersistence::RegionPersistence(std::filesystem::path directory) : directory_(std::move(directory)) {}

void RegionPersistence::load(RegionRuntime& runtime) {
    std::filesystem::create_directories(directory_);
    const auto terrain_path = directory_ / "terrain.oglt";
    if (std::ifstream input(terrain_path, std::ios::binary); input) {
        try {
            char magic[4]{};
            input.read(magic, 4);
            if (std::string_view(magic, 4) != "OGLT") throw std::runtime_error("invalid terrain magic");
            const auto version = get_u32(input);
            const auto width = get_u32(input);
            const auto height = get_u32(input);
            const auto cell_size = get_double(input);
            (void)get_double(input); // stored base height; runtime configuration remains authoritative
            const auto revision = get_u64(input);
            if (version != 1 || width != runtime.terrain().width() || height != runtime.terrain().height() ||
                cell_size != runtime.terrain().cell_size()) {
                throw std::runtime_error("terrain geometry does not match runtime");
            }
            std::vector<double> heights(static_cast<std::size_t>(width) * height);
            for (double& value : heights) value = get_double(input);
            if (!runtime.terrain().restore_heights(std::move(heights), revision)) {
                throw std::runtime_error("terrain restore rejected");
            }
            saved_terrain_revision_ = revision;
        } catch (const std::exception& error) {
            common::log(common::LogLevel::warning, "world.persistence",
                        runtime.id() + " terrain load failed: " + error.what());
        }
    }

    const auto scene_path = directory_ / "objects.db";
    if (std::ifstream input(scene_path); input) {
        std::string line;
        while (std::getline(input, line)) {
            if (line.empty() || line[0] == '#') continue;
            const auto fields = split_tabs(line);
            if (fields.size() != 12) continue;
            try {
                Transform transform;
                const auto id = std::stoull(fields[0]);
                const auto name = text_unhex(fields[1]);
                transform.position = {std::stod(fields[2]), std::stod(fields[3]), std::stod(fields[4])};
                transform.rotation = {std::stod(fields[5]), std::stod(fields[6]), std::stod(fields[7])};
                transform.scale = {std::stod(fields[8]), std::stod(fields[9]), std::stod(fields[10])};
                const bool physical = fields[11] == "1";
                (void)runtime.restore_object(id, name, transform, physical);
            } catch (...) {
            }
        }
    }
    saved_sequence_ = runtime.latest_sequence();
    common::log(common::LogLevel::info, "world.persistence",
                runtime.id() + " loaded from " + directory_.string());
}

void RegionPersistence::save(const RegionRuntime& runtime, const bool force) {
    const auto sequence = runtime.latest_sequence();
    const auto terrain_revision = runtime.terrain().revision();
    if (!force && sequence == saved_sequence_ && terrain_revision == saved_terrain_revision_) return;
    std::filesystem::create_directories(directory_);

    if (force || terrain_revision != saved_terrain_revision_) {
        const auto path = directory_ / "terrain.oglt";
        const auto temporary = path.string() + ".tmp";
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) throw std::runtime_error("cannot write terrain persistence");
        output.write("OGLT", 4);
        put_u32(output, 1);
        put_u32(output, static_cast<std::uint32_t>(runtime.terrain().width()));
        put_u32(output, static_cast<std::uint32_t>(runtime.terrain().height()));
        put_double(output, runtime.terrain().cell_size());
        put_double(output, runtime.terrain().base_height());
        put_u64(output, terrain_revision);
        for (const double value : runtime.terrain().snapshot_heights()) put_double(output, value);
        output.close();
        if (!output) throw std::runtime_error("cannot flush terrain persistence");
        std::filesystem::rename(temporary, path);
        saved_terrain_revision_ = terrain_revision;
    }

    if (force || sequence != saved_sequence_) {
        const auto path = directory_ / "objects.db";
        const auto temporary = path.string() + ".tmp";
        std::ofstream output(temporary, std::ios::trunc);
        if (!output) throw std::runtime_error("cannot write scene persistence");
        output << "# OpenGenesisLINK persistent scene objects v1\n" << std::setprecision(17);
        for (const auto& entity : runtime.snapshot_entities()) {
            if (entity.kind != EntityKind::object) continue;
            const auto& t = entity.transform;
            output << entity.id << '\t' << text_hex(entity.name) << '\t' << t.position.x << '\t'
                   << t.position.y << '\t' << t.position.z << '\t' << t.rotation.x << '\t'
                   << t.rotation.y << '\t' << t.rotation.z << '\t' << t.scale.x << '\t'
                   << t.scale.y << '\t' << t.scale.z << '\t' << (entity.physics_body != 0 ? 1 : 0) << '\n';
        }
        output.close();
        if (!output) throw std::runtime_error("cannot flush scene persistence");
        std::filesystem::rename(temporary, path);
        saved_sequence_ = sequence;
    }
}

} // namespace opengenesis::world
