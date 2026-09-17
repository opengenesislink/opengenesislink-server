#pragma once

#include "opengenesis/compat/hypergrid/xmlrpc.hpp"
#include "opengenesis/core/region_registry.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

namespace opengenesis::compat::hypergrid {

struct HypergridConfig {
    bool enabled{false};
    std::string external_name;
    std::string home_uri;
    std::string asset_uri;
    std::string inventory_uri;
    std::string friends_uri;
    std::string im_uri;
    std::string region_host{"127.0.0.1"};
    std::uint16_t http_port{0};
    std::uint16_t internal_port{0};
};

class HypergridService final {
public:
    HypergridService(HypergridConfig config,
                     std::shared_ptr<core::RegionRegistry> regions);

    [[nodiscard]] bool enabled() const noexcept { return config_.enabled; }
    [[nodiscard]] std::unordered_map<std::string, std::string> handle(
        const XmlRpcCall& call) const;

    [[nodiscard]] static std::string legacy_region_uuid(std::string_view region_id);
    [[nodiscard]] static std::uint64_t legacy_region_handle(std::int32_t grid_x,
                                                            std::int32_t grid_y);

private:
    [[nodiscard]] std::unordered_map<std::string, std::string> link_region(
        const XmlRpcCall& call) const;
    [[nodiscard]] std::unordered_map<std::string, std::string> get_region(
        const XmlRpcCall& call) const;
    [[nodiscard]] std::unordered_map<std::string, std::string> get_server_urls() const;

    HypergridConfig config_;
    std::shared_ptr<core::RegionRegistry> regions_;
};

} // namespace opengenesis::compat::hypergrid
