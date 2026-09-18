#pragma once

#include "opengenesis/core/asset_store.hpp"

#include <memory>
#include <string>
#include <string_view>

namespace opengenesis::compat::hypergrid {

struct LegacyAssetResponse {
    int status{404};
    std::string content_type{"text/plain"};
    std::string body;
};

class HypergridAssetAdapter final {
public:
    explicit HypergridAssetAdapter(std::shared_ptr<core::AssetStore> assets);

    [[nodiscard]] LegacyAssetResponse handle_get(std::string_view path) const;
    [[nodiscard]] static std::string legacy_asset_uuid(std::string_view native_asset_id);

private:
    [[nodiscard]] std::optional<core::AssetInfo> find_by_legacy_uuid(
        std::string_view legacy_uuid) const;

    std::shared_ptr<core::AssetStore> assets_;
};

} // namespace opengenesis::compat::hypergrid
