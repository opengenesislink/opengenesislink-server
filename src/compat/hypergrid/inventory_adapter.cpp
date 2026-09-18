#include "opengenesis/compat/hypergrid/inventory_adapter.hpp"

#include "opengenesis/compat/hypergrid/friends_adapter.hpp"
#include "opengenesis/compat/hypergrid/session_store.hpp"
#include "opengenesis/core/permissions.hpp"

#include <algorithm>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <unordered_map>
#include <vector>

namespace opengenesis::compat::hypergrid {
namespace {

std::string xml_escape(const std::string_view value) {
    std::string output;
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

std::string node(const std::string_view name, const std::string_view value) {
    return "<" + std::string{name} + ">" + xml_escape(value) + "</" +
           std::string{name} + ">";
}

std::string folder_xml(const core::InventoryFolder& folder,
                       const std::string_view owner_legacy,
                       const bool root) {
    const auto id = HypergridInventoryAdapter::legacy_folder_uuid(folder.id);
    const auto parent = root || folder.parent_id.empty()
                            ? "00000000-0000-0000-0000-000000000000"
                            : HypergridInventoryAdapter::legacy_folder_uuid(folder.parent_id);
    return node("ParentID", parent) +
           node("Type", root ? "8" : "-1") +
           node("Version", "1") +
           node("Name", folder.name) +
           node("Owner", owner_legacy) +
           node("ID", id);
}

std::string item_xml(const core::InventoryItem& item,
                     const std::string_view owner_legacy,
                     const std::optional<core::AssetInfo>& asset) {
    const auto asset_id = asset
                              ? HypergridAssetAdapter::legacy_asset_uuid(asset->id)
                              : legacy_uuid_from_seed("missing-asset:" + item.asset_id);
    const auto owner_mask = asset ? asset->permissions : 0U;
    const auto next_mask = asset ? asset->next_owner_permissions : 0U;
    return node("AssetID", asset_id) +
           node("AssetType", "-1") +
           node("BasePermissions", std::to_string(owner_mask)) +
           node("CreationDate", std::to_string(item.created_unix)) +
           node("CreatorId", std::string{owner_legacy}) +
           node("CreatorData", "") +
           node("CurrentPermissions", std::to_string(owner_mask)) +
           node("Description", "") +
           node("EveryOnePermissions", "0") +
           node("Flags", "0") +
           node("Folder", HypergridInventoryAdapter::legacy_folder_uuid(item.parent_id)) +
           node("GroupID", "00000000-0000-0000-0000-000000000000") +
           node("GroupOwned", "False") +
           node("GroupPermissions", "0") +
           node("ID", HypergridInventoryAdapter::legacy_item_uuid(item.id)) +
           node("InvType", "-1") +
           node("Name", item.name) +
           node("NextPermissions", std::to_string(next_mask)) +
           node("Owner", owner_legacy) +
           node("SalePrice", "0") +
           node("SaleType", "0");
}

std::string response(const std::string_view body) {
    return "<?xml version=\"1.0\"?><ServerResponse>" + std::string{body} +
           "</ServerResponse>";
}

} // namespace

HypergridInventoryAdapter::HypergridInventoryAdapter(
    std::shared_ptr<core::IdentityStore> identities,
    std::shared_ptr<core::InventoryStore> inventory,
    std::shared_ptr<core::AssetStore> assets)
    : identities_(std::move(identities)),
      inventory_(std::move(inventory)),
      assets_(std::move(assets)) {
    if (!identities_ || !inventory_ || !assets_) {
        throw std::invalid_argument("HG Inventory dependencies required");
    }
}

std::string HypergridInventoryAdapter::legacy_folder_uuid(const std::string_view native_id) {
    return legacy_uuid_from_seed("inventory-folder:" + std::string{native_id});
}

std::string HypergridInventoryAdapter::legacy_item_uuid(const std::string_view native_id) {
    return legacy_uuid_from_seed("inventory-item:" + std::string{native_id});
}

std::optional<std::string> HypergridInventoryAdapter::native_user_for_legacy(
    const std::string_view legacy_uuid) const {
    for (const auto& user : identities_->list()) {
        if (legacy_uuid_from_seed(user.id) == legacy_uuid) return user.id;
    }
    return std::nullopt;
}

std::string HypergridInventoryAdapter::handle_form(const std::string_view body) const {
    std::unordered_map<std::string, std::string> fields;
    try {
        fields = parse_form_urlencoded(body);
    } catch (...) {
        return response(node("RESULT", "False"));
    }

    const auto method_it = fields.find("METHOD");
    const auto principal_it = fields.find("PRINCIPAL");
    if (method_it == fields.end() || principal_it == fields.end()) {
        return response(node("RESULT", "False"));
    }

    const auto native_user = native_user_for_legacy(principal_it->second);
    if (!native_user) return response(node("RESULT", "False"));
    (void)inventory_->ensure_root(*native_user);
    const auto inventory = inventory_->list(*native_user);
    const auto& method = method_it->second;

    if (method == "GETROOTFOLDER") {
        return response("<folder type=\"List\">" +
                        folder_xml(inventory.root, principal_it->second, true) +
                        "</folder>");
    }

    if (method == "GETINVENTORYSKELETON") {
        std::ostringstream out;
        out << "<FOLDERS type=\"List\"><folder_0 type=\"List\">"
            << folder_xml(inventory.root, principal_it->second, true)
            << "</folder_0>";
        std::size_t index = 1;
        for (const auto& folder : inventory.folders) {
            out << "<folder_" << index << " type=\"List\">"
                << folder_xml(folder, principal_it->second, false)
                << "</folder_" << index << ">";
            ++index;
        }
        out << "</FOLDERS>";
        return response(out.str());
    }

    const auto folder_it = fields.find("FOLDER");
    if (method == "GETFOLDERCONTENT" || method == "GETFOLDERITEMS") {
        if (folder_it == fields.end()) return response(node("RESULT", "False"));
        const auto requested = folder_it->second;
        std::ostringstream folders;
        std::ostringstream items;
        std::size_t folder_index = 0;
        std::size_t item_index = 0;

        for (const auto& folder : inventory.folders) {
            const auto parent_legacy = folder.parent_id.empty()
                                           ? legacy_folder_uuid(inventory.root.id)
                                           : legacy_folder_uuid(folder.parent_id);
            if (parent_legacy != requested) continue;
            folders << "<folder_" << folder_index << " type=\"List\">"
                    << folder_xml(folder, principal_it->second, false)
                    << "</folder_" << folder_index << ">";
            ++folder_index;
        }
        for (const auto& item : inventory.items) {
            if (legacy_folder_uuid(item.parent_id) != requested) continue;
            const auto asset = assets_->find(item.asset_id);
            if (!asset || !core::has_permission(asset->permissions, core::perm_export)) continue;
            items << "<item_" << item_index << " type=\"List\">"
                  << item_xml(item, principal_it->second, asset)
                  << "</item_" << item_index << ">";
            ++item_index;
        }

        if (method == "GETFOLDERITEMS") {
            return response("<ITEMS type=\"List\">" + items.str() + "</ITEMS>");
        }
        return response(node("FID", requested) + node("VERSION", "1") +
                        "<FOLDERS type=\"List\">" + folders.str() + "</FOLDERS>" +
                        "<ITEMS type=\"List\">" + items.str() + "</ITEMS>");
    }

    if (method == "GETITEM") {
        const auto id_it = fields.find("ID");
        if (id_it == fields.end()) return response("");
        for (const auto& item : inventory.items) {
            if (legacy_item_uuid(item.id) != id_it->second) continue;
            const auto asset = assets_->find(item.asset_id);
            if (!asset || !core::has_permission(asset->permissions, core::perm_export)) {
                return response("");
            }
            return response("<item type=\"List\">" +
                            item_xml(item, principal_it->second, asset) +
                            "</item>");
        }
        return response("");
    }

    if (method == "GETASSETPERMISSIONS") {
        const auto asset_it = fields.find("ASSET");
        if (asset_it == fields.end()) return response(node("RESULT", "0"));
        for (const auto& asset : assets_->list_for_user(*native_user)) {
            if (HypergridAssetAdapter::legacy_asset_uuid(asset.id) == asset_it->second) {
                return response(node("RESULT", std::to_string(asset.permissions)));
            }
        }
        return response(node("RESULT", "0"));
    }

    return response(node("RESULT", "False"));
}

} // namespace opengenesis::compat::hypergrid
