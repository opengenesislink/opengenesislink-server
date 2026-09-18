#pragma once

#include "opengenesis/avatar/appearance_store.hpp"
#include "opengenesis/compat/hypergrid/asset_adapter.hpp"
#include "opengenesis/compat/hypergrid/session_store.hpp"
#include "opengenesis/core/identity_store.hpp"
#include "opengenesis/core/inventory_store.hpp"

#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace opengenesis::compat::hypergrid {

class HypergridAppearanceAdapter final {
public:
    HypergridAppearanceAdapter(std::shared_ptr<core::IdentityStore> identities,
                               std::shared_ptr<avatar::AppearanceStore> appearance,
                               std::shared_ptr<core::InventoryStore> inventory,
                               std::shared_ptr<core::AssetStore> assets,
                               std::shared_ptr<HypergridSessionStore> sessions);

    [[nodiscard]] std::string handle_form(std::string_view body) const;

private:
    [[nodiscard]] std::optional<std::string> native_user_for_legacy(
        std::string_view legacy_uuid) const;

    std::shared_ptr<core::IdentityStore> identities_;
    std::shared_ptr<avatar::AppearanceStore> appearance_;
    std::shared_ptr<core::InventoryStore> inventory_;
    std::shared_ptr<core::AssetStore> assets_;
    std::shared_ptr<HypergridSessionStore> sessions_;
};

} // namespace opengenesis::compat::hypergrid
