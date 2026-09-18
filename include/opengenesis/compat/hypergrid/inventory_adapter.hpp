#pragma once

#include "opengenesis/compat/hypergrid/asset_adapter.hpp"
#include "opengenesis/core/identity_store.hpp"
#include "opengenesis/core/inventory_store.hpp"

#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace opengenesis::compat::hypergrid {

class HypergridInventoryAdapter final {
public:
    HypergridInventoryAdapter(std::shared_ptr<core::IdentityStore> identities,
                              std::shared_ptr<core::InventoryStore> inventory,
                              std::shared_ptr<core::AssetStore> assets);

    [[nodiscard]] std::string handle_form(std::string_view body) const;
    [[nodiscard]] static std::string legacy_folder_uuid(std::string_view native_id);
    [[nodiscard]] static std::string legacy_item_uuid(std::string_view native_id);

private:
    [[nodiscard]] std::optional<std::string> native_user_for_legacy(
        std::string_view legacy_uuid) const;

    std::shared_ptr<core::IdentityStore> identities_;
    std::shared_ptr<core::InventoryStore> inventory_;
    std::shared_ptr<core::AssetStore> assets_;
};

} // namespace opengenesis::compat::hypergrid
