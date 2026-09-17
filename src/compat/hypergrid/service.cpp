#include "opengenesis/compat/hypergrid/service.hpp"

#include "opengenesis/security/crypto.hpp"

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <utility>

namespace opengenesis::compat::hypergrid {
namespace {

std::string format_uuid(std::string hex) {
    if (hex.size() < 32) throw std::runtime_error("hash too short for UUID");
    hex.resize(32);
    hex[12] = '5';
    const char variant_char = static_cast<char>(
        std::tolower(static_cast<unsigned char>(hex[16])));
    const unsigned variant = variant_char >= '0' && variant_char <= '9'
                                 ? static_cast<unsigned>(variant_char - '0')
                                 : static_cast<unsigned>(10 + variant_char - 'a');
    constexpr char digits[] = "0123456789abcdef";
    hex[16] = digits[(variant & 0x3U) | 0x8U];
    return hex.substr(0, 8) + "-" + hex.substr(8, 4) + "-" + hex.substr(12, 4) + "-" +
           hex.substr(16, 4) + "-" + hex.substr(20, 12);
}

} // namespace

HypergridService::HypergridService(HypergridConfig config,
                                   std::shared_ptr<core::RegionRegistry> regions)
    : config_(std::move(config)), regions_(std::move(regions)) {
    if (!regions_) throw std::invalid_argument("Hypergrid RegionRegistry required");
}

std::string HypergridService::legacy_region_uuid(const std::string_view region_id) {
    return format_uuid(security::sha256_hex("OpenGenesisLINK-HG-region:" + std::string{region_id}));
}

std::uint64_t HypergridService::legacy_region_handle(const std::int32_t grid_x,
                                                     const std::int32_t grid_y) {
    const auto x = static_cast<std::uint32_t>(std::max(grid_x, 0) * 256);
    const auto y = static_cast<std::uint32_t>(std::max(grid_y, 0) * 256);
    return (static_cast<std::uint64_t>(x) << 32U) | static_cast<std::uint64_t>(y);
}


std::optional<core::RegionInfo> HypergridService::region_by_legacy_uuid(
    const std::string_view legacy_uuid) const {
    const auto regions = regions_->list();
    const auto it = std::find_if(regions.begin(), regions.end(), [&](const core::RegionInfo& region) {
        return legacy_region_uuid(region.id) == legacy_uuid;
    });
    return it == regions.end() ? std::nullopt : std::optional<core::RegionInfo>{*it};
}

std::unordered_map<std::string, std::string> HypergridService::handle(
    const XmlRpcCall& call) const {
    if (!config_.enabled) return {{"result", "false"}, {"message", "Hypergrid disabled"}};
    if (call.method == "link_region") return link_region(call);
    if (call.method == "get_region") return get_region(call);
    if (call.method == "get_server_urls") return get_server_urls();
    return {{"result", "false"}, {"message", "Unsupported Hypergrid method"}};
}

std::unordered_map<std::string, std::string> HypergridService::link_region(
    const XmlRpcCall& call) const {
    const auto name_it = call.fields.find("region_name");
    const std::string requested = name_it == call.fields.end() ? std::string{} : name_it->second;

    const auto regions = regions_->list();
    const auto it = std::find_if(regions.begin(), regions.end(), [&](const core::RegionInfo& region) {
        if (region.state != "online") return false;
        return requested.empty() || region.name == requested || region.id == requested;
    });
    if (it == regions.end()) {
        return {{"result", "false"}, {"message", "Region unavailable"}};
    }

    return {{"result", "True"},
            {"uuid", legacy_region_uuid(it->id)},
            {"handle", std::to_string(legacy_region_handle(it->grid_x, it->grid_y))},
            {"size_x", "256"},
            {"size_y", "256"},
            {"region_image", ""},
            {"external_name", config_.external_name}};
}

std::unordered_map<std::string, std::string> HypergridService::get_region(
    const XmlRpcCall& call) const {
    const auto id_it = call.fields.find("region_uuid");
    if (id_it == call.fields.end()) return {{"result", "false"}};

    const auto regions = regions_->list();
    const auto it = std::find_if(regions.begin(), regions.end(), [&](const core::RegionInfo& region) {
        return legacy_region_uuid(region.id) == id_it->second && region.state == "online";
    });
    if (it == regions.end()) return {{"result", "false"}};

    const auto x = static_cast<std::int64_t>(it->grid_x) * 256;
    const auto y = static_cast<std::int64_t>(it->grid_y) * 256;
    return {{"result", "true"},
            {"uuid", legacy_region_uuid(it->id)},
            {"x", std::to_string(x)},
            {"y", std::to_string(y)},
            {"size_x", "256"},
            {"size_y", "256"},
            {"region_name", it->name},
            {"hostname", config_.region_host},
            {"http_port", std::to_string(config_.http_port)},
            {"internal_port", std::to_string(config_.internal_port)},
            {"server_uri", config_.external_name}};
}

std::unordered_map<std::string, std::string> HypergridService::get_server_urls() const {
    std::unordered_map<std::string, std::string> result;
    if (!config_.home_uri.empty()) result["SRV_HomeURI"] = config_.home_uri;
    if (!config_.asset_uri.empty()) result["SRV_AssetServerURI"] = config_.asset_uri;
    if (!config_.inventory_uri.empty()) result["SRV_InventoryServerURI"] = config_.inventory_uri;
    if (!config_.friends_uri.empty()) result["SRV_FriendsServerURI"] = config_.friends_uri;
    if (!config_.im_uri.empty()) result["SRV_IMServerURI"] = config_.im_uri;
    if (result.empty()) result["result"] = "No Service URLs";
    return result;
}

} // namespace opengenesis::compat::hypergrid
