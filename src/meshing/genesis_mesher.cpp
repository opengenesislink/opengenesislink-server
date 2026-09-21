#include "opengenesis/meshing/genesis_mesher.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <numbers>
#include <sstream>
#include <string>
#include <utility>

namespace opengenesis::meshing {
namespace {

bool finite(const Vec3& value) {
    return std::isfinite(value.x) &&
           std::isfinite(value.y) &&
           std::isfinite(value.z);
}

Vec3 subtract(const Vec3& a, const Vec3& b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

Vec3 cross(const Vec3& a, const Vec3& b) {
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x};
}

double length_squared(const Vec3& value) {
    return value.x * value.x +
           value.y * value.y +
           value.z * value.z;
}

Vec3 normalized(Vec3 value) {
    const auto length = std::sqrt(length_squared(value));
    if (length <= 1e-12 || !std::isfinite(length)) {
        return {0.0, 0.0, 1.0};
    }
    value.x /= length;
    value.y /= length;
    value.z /= length;
    return value;
}

void append_triangle(
    TriangleMesh& mesh,
    const std::uint32_t a,
    const std::uint32_t b,
    const std::uint32_t c) {
    mesh.indices.push_back(a);
    mesh.indices.push_back(b);
    mesh.indices.push_back(c);
}

void compute_bounds(TriangleMesh& mesh) {
    if (mesh.vertices.empty()) {
        mesh.bounds = {};
        return;
    }

    Vec3 minimum{
        std::numeric_limits<double>::max(),
        std::numeric_limits<double>::max(),
        std::numeric_limits<double>::max()};
    Vec3 maximum{
        std::numeric_limits<double>::lowest(),
        std::numeric_limits<double>::lowest(),
        std::numeric_limits<double>::lowest()};

    for (const auto& vertex : mesh.vertices) {
        minimum.x = std::min(minimum.x, vertex.position.x);
        minimum.y = std::min(minimum.y, vertex.position.y);
        minimum.z = std::min(minimum.z, vertex.position.z);
        maximum.x = std::max(maximum.x, vertex.position.x);
        maximum.y = std::max(maximum.y, vertex.position.y);
        maximum.z = std::max(maximum.z, vertex.position.z);
    }

    mesh.bounds = {.minimum = minimum, .maximum = maximum};
}

void remove_degenerate_triangles(TriangleMesh& mesh) {
    std::vector<std::uint32_t> filtered;
    filtered.reserve(mesh.indices.size());

    for (std::size_t index = 0;
         index + 2U < mesh.indices.size();
         index += 3U) {
        const auto a_index = mesh.indices[index];
        const auto b_index = mesh.indices[index + 1U];
        const auto c_index = mesh.indices[index + 2U];
        if (a_index >= mesh.vertices.size() ||
            b_index >= mesh.vertices.size() ||
            c_index >= mesh.vertices.size()) {
            continue;
        }

        const auto& a = mesh.vertices[a_index].position;
        const auto& b = mesh.vertices[b_index].position;
        const auto& c = mesh.vertices[c_index].position;
        const auto area_vector =
            cross(subtract(b, a), subtract(c, a));
        if (length_squared(area_vector) <= 1e-18) {
            continue;
        }

        filtered.push_back(a_index);
        filtered.push_back(b_index);
        filtered.push_back(c_index);
    }

    mesh.indices = std::move(filtered);
}

std::uint32_t bounded_radial_segments(
    const MeshingOptions& options,
    const PrimitiveKind kind) {
    auto radial = std::clamp(options.radial_segments, 6U, 96U);
    const auto max_triangles =
        std::max<std::size_t>(options.max_triangles, 12U);

    auto estimate = [&](const std::uint32_t segments) {
        switch (kind) {
            case PrimitiveKind::box:
                return std::size_t{12};
            case PrimitiveKind::sphere:
            case PrimitiveKind::capsule: {
                const auto vertical =
                    std::clamp(options.vertical_segments, 4U, 48U);
                return static_cast<std::size_t>(
                    segments * vertical * 2U);
            }
            case PrimitiveKind::cylinder:
                return static_cast<std::size_t>(segments) * 4U;
        }
        return std::size_t{12};
    };

    while (radial > 6U && estimate(radial) > max_triangles) {
        --radial;
    }
    return radial;
}

std::uint32_t bounded_vertical_segments(
    const MeshingOptions& options,
    const PrimitiveKind kind,
    const std::uint32_t radial) {
    if (kind != PrimitiveKind::sphere &&
        kind != PrimitiveKind::capsule) {
        return 1U;
    }

    auto vertical =
        std::clamp(options.vertical_segments, 4U, 48U);
    const auto max_triangles =
        std::max<std::size_t>(options.max_triangles, 12U);
    while (vertical > 4U &&
           static_cast<std::size_t>(radial) *
                   static_cast<std::size_t>(vertical) * 2U >
               max_triangles) {
        --vertical;
    }
    return vertical;
}

TriangleMesh make_box(
    const PrimitiveDescriptor& descriptor,
    const bool generate_uvs) {
    TriangleMesh mesh;
    const auto hx = descriptor.size.x * 0.5;
    const auto hy = descriptor.size.y * 0.5;
    const auto hz = descriptor.size.z * 0.5;

    struct Face {
        Vec3 normal;
        Vec3 a;
        Vec3 b;
        Vec3 c;
        Vec3 d;
    };

    const Face faces[] = {
        {{1, 0, 0}, {hx, -hy, -hz}, {hx, hy, -hz},
         {hx, hy, hz}, {hx, -hy, hz}},
        {{-1, 0, 0}, {-hx, hy, -hz}, {-hx, -hy, -hz},
         {-hx, -hy, hz}, {-hx, hy, hz}},
        {{0, 1, 0}, {hx, hy, -hz}, {-hx, hy, -hz},
         {-hx, hy, hz}, {hx, hy, hz}},
        {{0, -1, 0}, {-hx, -hy, -hz}, {hx, -hy, -hz},
         {hx, -hy, hz}, {-hx, -hy, hz}},
        {{0, 0, 1}, {-hx, -hy, hz}, {hx, -hy, hz},
         {hx, hy, hz}, {-hx, hy, hz}},
        {{0, 0, -1}, {-hx, hy, -hz}, {hx, hy, -hz},
         {hx, -hy, -hz}, {-hx, -hy, -hz}}};

    for (const auto& face : faces) {
        const auto base =
            static_cast<std::uint32_t>(mesh.vertices.size());
        const double uv[][2] = {
            {0.0, 0.0}, {1.0, 0.0},
            {1.0, 1.0}, {0.0, 1.0}};
        const Vec3 positions[] = {
            face.a, face.b, face.c, face.d};
        for (std::size_t i = 0; i < 4U; ++i) {
            mesh.vertices.push_back({
                .position = positions[i],
                .normal = face.normal,
                .u = generate_uvs ? uv[i][0] : 0.0,
                .v = generate_uvs ? uv[i][1] : 0.0});
        }
        append_triangle(mesh, base, base + 1U, base + 2U);
        append_triangle(mesh, base, base + 2U, base + 3U);
    }
    return mesh;
}

TriangleMesh make_latitude_mesh(
    const PrimitiveDescriptor& descriptor,
    const std::uint32_t radial,
    const std::uint32_t vertical,
    const bool capsule,
    const bool generate_uvs) {
    TriangleMesh mesh;
    const auto rx = descriptor.size.x * 0.5;
    const auto ry = descriptor.size.y * 0.5;
    const auto rz = capsule
        ? std::min(rx, ry)
        : descriptor.size.z * 0.5;
    const auto capsule_half_cylinder = capsule
        ? std::max(0.0, descriptor.size.z * 0.5 - rz)
        : 0.0;

    for (std::uint32_t y = 0; y <= vertical; ++y) {
        const auto v =
            static_cast<double>(y) /
            static_cast<double>(vertical);
        const auto latitude =
            -std::numbers::pi / 2.0 +
            v * std::numbers::pi;
        const auto ring =
            std::cos(latitude);
        const auto z_unit =
            std::sin(latitude);

        for (std::uint32_t x = 0; x <= radial; ++x) {
            const auto u =
                static_cast<double>(x) /
                static_cast<double>(radial);
            const auto longitude =
                u * 2.0 * std::numbers::pi;
            const auto x_unit =
                ring * std::cos(longitude);
            const auto y_unit =
                ring * std::sin(longitude);

            Vec3 position{
                x_unit * rx,
                y_unit * ry,
                z_unit * rz};
            if (capsule) {
                if (z_unit > 0.0) {
                    position.z += capsule_half_cylinder;
                } else if (z_unit < 0.0) {
                    position.z -= capsule_half_cylinder;
                }
            }

            const Vec3 normal = capsule
                ? normalized({
                      x_unit / std::max(rx, 1e-9),
                      y_unit / std::max(ry, 1e-9),
                      z_unit / std::max(rz, 1e-9)})
                : normalized({
                      x_unit / std::max(rx, 1e-9),
                      y_unit / std::max(ry, 1e-9),
                      z_unit /
                          std::max(descriptor.size.z * 0.5, 1e-9)});

            mesh.vertices.push_back({
                .position = position,
                .normal = normal,
                .u = generate_uvs ? u : 0.0,
                .v = generate_uvs ? v : 0.0});
        }
    }

    const auto stride = radial + 1U;
    for (std::uint32_t y = 0; y < vertical; ++y) {
        for (std::uint32_t x = 0; x < radial; ++x) {
            const auto a = y * stride + x;
            const auto b = a + 1U;
            const auto c = a + stride;
            const auto d = c + 1U;
            append_triangle(mesh, a, c, b);
            append_triangle(mesh, b, c, d);
        }
    }

    remove_degenerate_triangles(mesh);
    return mesh;
}

TriangleMesh make_cylinder(
    const PrimitiveDescriptor& descriptor,
    const std::uint32_t radial,
    const bool generate_uvs) {
    TriangleMesh mesh;
    const auto rx = descriptor.size.x * 0.5;
    const auto ry = descriptor.size.y * 0.5;
    const auto hz = descriptor.size.z * 0.5;

    for (std::uint32_t side = 0; side <= 1U; ++side) {
        const auto z = side == 0U ? -hz : hz;
        for (std::uint32_t i = 0; i <= radial; ++i) {
            const auto u =
                static_cast<double>(i) /
                static_cast<double>(radial);
            const auto angle =
                u * 2.0 * std::numbers::pi;
            const auto cx = std::cos(angle);
            const auto cy = std::sin(angle);
            mesh.vertices.push_back({
                .position = {cx * rx, cy * ry, z},
                .normal = normalized({
                    cx / std::max(rx, 1e-9),
                    cy / std::max(ry, 1e-9),
                    0.0}),
                .u = generate_uvs ? u : 0.0,
                .v = generate_uvs
                    ? static_cast<double>(side)
                    : 0.0});
        }
    }

    const auto stride = radial + 1U;
    for (std::uint32_t i = 0; i < radial; ++i) {
        const auto a = i;
        const auto b = i + 1U;
        const auto c = stride + i;
        const auto d = c + 1U;
        append_triangle(mesh, a, b, c);
        append_triangle(mesh, b, d, c);
    }

    for (int cap = -1; cap <= 1; cap += 2) {
        const auto normal = Vec3{0.0, 0.0, static_cast<double>(cap)};
        const auto z = static_cast<double>(cap) * hz;
        const auto center =
            static_cast<std::uint32_t>(mesh.vertices.size());
        mesh.vertices.push_back({
            .position = {0.0, 0.0, z},
            .normal = normal,
            .u = generate_uvs ? 0.5 : 0.0,
            .v = generate_uvs ? 0.5 : 0.0});

        const auto ring =
            static_cast<std::uint32_t>(mesh.vertices.size());
        for (std::uint32_t i = 0; i <= radial; ++i) {
            const auto angle =
                static_cast<double>(i) /
                static_cast<double>(radial) *
                2.0 * std::numbers::pi;
            const auto cx = std::cos(angle);
            const auto cy = std::sin(angle);
            mesh.vertices.push_back({
                .position = {cx * rx, cy * ry, z},
                .normal = normal,
                .u = generate_uvs ? cx * 0.5 + 0.5 : 0.0,
                .v = generate_uvs ? cy * 0.5 + 0.5 : 0.0});
        }

        for (std::uint32_t i = 0; i < radial; ++i) {
            if (cap > 0) {
                append_triangle(
                    mesh, center, ring + i, ring + i + 1U);
            } else {
                append_triangle(
                    mesh, center, ring + i + 1U, ring + i);
            }
        }
    }

    return mesh;
}

} // namespace


GenesisMeshCache::GenesisMeshCache(
    const std::size_t max_entries,
    const std::size_t max_triangles)
    : max_entries_(std::max<std::size_t>(1U, max_entries)),
      max_triangles_(std::max<std::size_t>(12U, max_triangles)) {}

std::optional<TriangleMesh> GenesisMeshCache::get(
    const std::string& key) {
    std::scoped_lock lock(mutex_);
    const auto it = entries_.find(key);
    if (it == entries_.end()) {
        ++misses_;
        return std::nullopt;
    }
    ++hits_;
    return it->second;
}

bool GenesisMeshCache::put(TriangleMesh mesh) {
    if (mesh.cache_key.empty() || mesh.empty()) {
        return false;
    }
    const auto triangles = mesh.triangle_count();
    if (triangles == 0U || triangles > max_triangles_) {
        return false;
    }

    std::scoped_lock lock(mutex_);
    const auto existing = entries_.find(mesh.cache_key);
    if (existing != entries_.end()) {
        const auto key = existing->first;
        triangles_ -= existing->second.triangle_count();
        existing->second = std::move(mesh);
        triangles_ += triangles;
        evict_locked();
        return entries_.contains(key);
    }

    const auto key = mesh.cache_key;
    entries_.emplace(key, std::move(mesh));
    insertion_order_.push_back(key);
    triangles_ += triangles;
    evict_locked();
    return entries_.contains(key);
}

void GenesisMeshCache::clear() {
    std::scoped_lock lock(mutex_);
    entries_.clear();
    insertion_order_.clear();
    triangles_ = 0;
}

MeshCacheStats GenesisMeshCache::stats() const {
    std::scoped_lock lock(mutex_);
    return {
        .entries = entries_.size(),
        .triangles = triangles_,
        .hits = hits_,
        .misses = misses_,
        .evictions = evictions_};
}

void GenesisMeshCache::evict_locked() {
    while ((entries_.size() > max_entries_ ||
            triangles_ > max_triangles_) &&
           !insertion_order_.empty()) {
        const auto key = insertion_order_.front();
        insertion_order_.pop_front();
        const auto it = entries_.find(key);
        if (it == entries_.end()) {
            continue;
        }
        triangles_ -= it->second.triangle_count();
        entries_.erase(it);
        ++evictions_;
    }
}

const char* primitive_kind_name(const PrimitiveKind kind) noexcept {
    switch (kind) {
        case PrimitiveKind::box: return "box";
        case PrimitiveKind::sphere: return "sphere";
        case PrimitiveKind::cylinder: return "cylinder";
        case PrimitiveKind::capsule: return "capsule";
    }
    return "unknown";
}

const char* collision_representation_name(
    const CollisionRepresentation representation) noexcept {
    switch (representation) {
        case CollisionRepresentation::primitive_proxy:
            return "primitive_proxy";
        case CollisionRepresentation::convex_hull:
            return "convex_hull";
        case CollisionRepresentation::triangle_mesh:
            return "triangle_mesh";
    }
    return "unknown";
}

MeshBuildResult GenesisMesher::mesh_primitive(
    const PrimitiveDescriptor& descriptor,
    MeshingOptions options) const {
    MeshBuildResult result;
    if (!finite(descriptor.size) ||
        descriptor.size.x <= 0.0 ||
        descriptor.size.y <= 0.0 ||
        descriptor.size.z <= 0.0) {
        result.error = "primitive dimensions must be finite and positive";
        return result;
    }

    options.max_triangles =
        std::clamp<std::size_t>(
            options.max_triangles, 12U, 1'000'000U);
    const auto radial =
        bounded_radial_segments(options, descriptor.kind);
    const auto vertical =
        bounded_vertical_segments(
            options, descriptor.kind, radial);

    switch (descriptor.kind) {
        case PrimitiveKind::box:
            result.mesh =
                make_box(descriptor, options.generate_uvs);
            break;
        case PrimitiveKind::sphere:
            result.mesh = make_latitude_mesh(
                descriptor, radial, vertical, false,
                options.generate_uvs);
            break;
        case PrimitiveKind::cylinder:
            result.mesh = make_cylinder(
                descriptor, radial, options.generate_uvs);
            break;
        case PrimitiveKind::capsule:
            result.mesh = make_latitude_mesh(
                descriptor, radial, vertical, true,
                options.generate_uvs);
            break;
    }

    remove_degenerate_triangles(result.mesh);
    compute_bounds(result.mesh);
    result.mesh.cache_key = cache_key(descriptor, options);

    if (result.mesh.triangle_count() > options.max_triangles) {
        result.error = "generated mesh exceeds triangle budget";
        result.mesh = {};
        return result;
    }

    std::string validation_error;
    if (!validate(result.mesh, &validation_error)) {
        result.error = validation_error;
        result.mesh = {};
    }
    return result;
}

bool GenesisMesher::validate(
    const TriangleMesh& mesh,
    std::string* error) const {
    auto fail = [&](const std::string& message) {
        if (error) *error = message;
        return false;
    };

    if (mesh.vertices.empty()) {
        return fail("mesh has no vertices");
    }
    if (mesh.indices.empty() ||
        mesh.indices.size() % 3U != 0U) {
        return fail("mesh index buffer is not triangular");
    }

    for (const auto& vertex : mesh.vertices) {
        if (!finite(vertex.position) ||
            !finite(vertex.normal) ||
            !std::isfinite(vertex.u) ||
            !std::isfinite(vertex.v)) {
            return fail("mesh contains non-finite vertex data");
        }
    }

    for (const auto index : mesh.indices) {
        if (index >= mesh.vertices.size()) {
            return fail("mesh index references a missing vertex");
        }
    }

    for (std::size_t index = 0;
         index < mesh.indices.size();
         index += 3U) {
        const auto& a =
            mesh.vertices[mesh.indices[index]].position;
        const auto& b =
            mesh.vertices[mesh.indices[index + 1U]].position;
        const auto& c =
            mesh.vertices[mesh.indices[index + 2U]].position;
        if (length_squared(
                cross(subtract(b, a), subtract(c, a))) <=
            1e-18) {
            return fail("mesh contains a degenerate triangle");
        }
    }

    if (error) error->clear();
    return true;
}

std::string GenesisMesher::cache_key(
    const PrimitiveDescriptor& descriptor,
    const MeshingOptions& options) const {
    std::ostringstream key;
    key << "oglmesh:v1:"
        << primitive_kind_name(descriptor.kind) << ':'
        << std::setprecision(17)
        << descriptor.size.x << ':'
        << descriptor.size.y << ':'
        << descriptor.size.z << ':'
        << options.radial_segments << ':'
        << options.vertical_segments << ':'
        << options.max_triangles << ':'
        << (options.generate_uvs ? 1 : 0);
    return key.str();
}

CollisionRepresentation
GenesisMesher::recommended_collision_representation(
    const PrimitiveDescriptor& descriptor,
    const TriangleMesh& mesh,
    const bool dynamic_body) const noexcept {
    if (descriptor.kind == PrimitiveKind::box ||
        descriptor.kind == PrimitiveKind::sphere ||
        descriptor.kind == PrimitiveKind::capsule) {
        return CollisionRepresentation::primitive_proxy;
    }
    if (dynamic_body) {
        return CollisionRepresentation::convex_hull;
    }
    return mesh.triangle_count() <= 32768U
        ? CollisionRepresentation::triangle_mesh
        : CollisionRepresentation::convex_hull;
}

} // namespace opengenesis::meshing
