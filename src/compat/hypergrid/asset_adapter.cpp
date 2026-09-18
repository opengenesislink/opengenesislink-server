#include "opengenesis/compat/hypergrid/asset_adapter.hpp"

#include "opengenesis/compat/hypergrid/session_store.hpp"
#include "opengenesis/security/crypto.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace opengenesis::compat::hypergrid {
namespace {

std::string xml_escape(const std::string_view value) {
    std::string output;
    output.reserve(value.size());
    for (const char c : value) {
        switch (c) {
            case '&': output += "&amp;"; break;
            case '<': output += "&lt;"; break;
            case '>': output += "&gt;"; break;
            case '"': output += "&quot;"; break;
            case '\'': output += "&apos;"; break;
            default: output.push_back(c); break;
        }
    }
    return output;
}

int legacy_asset_type(const std::string_view mime) {
    if (mime == "image/jp2" || mime == "image/j2c" || mime == "image/x-j2c" ||
        mime == "image/tga") return 0;
    if (mime == "audio/ogg" || mime == "audio/x-ogg") return 1;
    if (mime == "application/vnd.ll.landmark") return 3;
    if (mime == "application/vnd.ll.clothing") return 5;
    if (mime == "application/vnd.ll.primitive") return 6;
    if (mime == "application/vnd.ll.notecard") return 7;
    if (mime == "application/vnd.ll.lsltext") return 10;
    if (mime == "application/vnd.ll.bodypart") return 13;
    if (mime == "application/vnd.ll.animation") return 20;
    if (mime == "application/vnd.ll.gesture") return 21;
    if (mime == "application/vnd.ll.mesh") return 49;
    return -1;
}

std::string creation_date(const std::int64_t unix_seconds) {
    const auto point = std::chrono::system_clock::time_point{
        std::chrono::seconds{unix_seconds}};
    const std::time_t time = std::chrono::system_clock::to_time_t(point);
    std::tm tm{};
#ifdef _WIN32
    if (gmtime_s(&tm, &time) != 0) tm = {};
#else
    if (gmtime_r(&time, &tm) == nullptr) tm = {};
#endif
    char buffer[32]{};
    if (std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &tm) == 0) {
        return "1970-01-01T00:00:00Z";
    }
    return buffer;
}

std::string metadata_xml(const core::AssetInfo& asset, const std::string_view legacy_id) {
    std::ostringstream out;
    out << "<?xml version=\"1.0\"?><AssetMetadata>"
        << "<FullID>" << legacy_id << "</FullID>"
        << "<ID>" << legacy_id << "</ID>"
        << "<Name>" << xml_escape(asset.name) << "</Name>"
        << "<Description></Description>"
        << "<CreationDate>" << creation_date(asset.created_unix) << "</CreationDate>"
        << "<Type>" << legacy_asset_type(asset.mime_type) << "</Type>"
        << "<ContentType>" << xml_escape(asset.mime_type) << "</ContentType>"
        << "<Local>false</Local><Temporary>false</Temporary>"
        << "<CreatorID>" << xml_escape(asset.owner_user_id) << "</CreatorID>"
        << "<Flags>0</Flags></AssetMetadata>";
    return out.str();
}

std::string asset_xml(const core::AssetInfo& asset,
                      const std::string_view legacy_id,
                      const std::string_view data) {
    std::ostringstream out;
    out << "<?xml version=\"1.0\"?><AssetBase>"
        << "<Data>" << security::base64_encode(data) << "</Data>"
        << "<FullID>" << legacy_id << "</FullID>"
        << "<ID>" << legacy_id << "</ID>"
        << "<Name>" << xml_escape(asset.name) << "</Name>"
        << "<Description></Description>"
        << "<Type>" << legacy_asset_type(asset.mime_type) << "</Type>"
        << "<UploadAttempts>0</UploadAttempts>"
        << "<Local>false</Local><Temporary>false</Temporary>"
        << "<CreatorID>" << xml_escape(asset.owner_user_id) << "</CreatorID>"
        << "<Flags>0</Flags></AssetBase>";
    return out.str();
}

} // namespace

HypergridAssetAdapter::HypergridAssetAdapter(std::shared_ptr<core::AssetStore> assets)
    : assets_(std::move(assets)) {
    if (!assets_) throw std::invalid_argument("Hypergrid AssetStore required");
}

std::string HypergridAssetAdapter::legacy_asset_uuid(const std::string_view native_asset_id) {
    return legacy_uuid_from_seed("asset:" + std::string{native_asset_id});
}

std::optional<core::AssetInfo> HypergridAssetAdapter::find_by_legacy_uuid(
    const std::string_view legacy_uuid) const {
    for (const auto& asset : assets_->list_all()) {
        if (legacy_asset_uuid(asset.id) == legacy_uuid) return asset;
    }
    return std::nullopt;
}

LegacyAssetResponse HypergridAssetAdapter::handle_get(const std::string_view path) const {
    constexpr std::string_view prefix = "/assets/";
    if (!path.starts_with(prefix)) return {};

    auto remainder = path.substr(prefix.size());
    const auto slash = remainder.find('/');
    const auto legacy_id = remainder.substr(0, slash);
    if (legacy_id.empty()) return {.status = 400, .content_type = "text/plain", .body = {}};

    const auto asset = find_by_legacy_uuid(legacy_id);
    if (!asset || !core::has_permission(asset->permissions, core::perm_export)) {
        return {.status = 404, .content_type = "text/plain", .body = {}};
    }

    if (slash == std::string_view::npos) {
        const auto data = assets_->read_exportable(asset->id);
        if (!data) return {};
        return {.status = 200,
                .content_type = "text/xml",
                .body = asset_xml(*asset, legacy_id, *data)};
    }

    const auto command = remainder.substr(slash + 1);
    if (command == "data") {
        const auto data = assets_->read_exportable(asset->id);
        if (!data) return {};
        return {.status = 200,
                .content_type = "application/octet-stream",
                .body = *data};
    }
    if (command == "metadata") {
        return {.status = 200,
                .content_type = asset->mime_type.empty() ? "application/octet-stream"
                                                        : asset->mime_type,
                .body = metadata_xml(*asset, legacy_id)};
    }

    return {.status = 400, .content_type = "text/plain", .body = {}};
}

} // namespace opengenesis::compat::hypergrid
