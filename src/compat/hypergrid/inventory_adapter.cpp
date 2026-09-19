#include "opengenesis/compat/hypergrid/inventory_adapter.hpp"

#include "opengenesis/compat/hypergrid/friends_adapter.hpp"
#include "opengenesis/compat/hypergrid/session_store.hpp"
#include "opengenesis/core/permissions.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <utility>
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

std::string folder_legacy(const core::InventoryFolder& folder) {
    return folder.legacy_id.empty()
               ? HypergridInventoryAdapter::legacy_folder_uuid(folder.id)
               : folder.legacy_id;
}

std::string item_legacy(const core::InventoryItem& item) {
    return item.legacy_id.empty()
               ? HypergridInventoryAdapter::legacy_item_uuid(item.id)
               : item.legacy_id;
}

std::string folder_xml(const core::InventoryFolder& folder,
                       const std::string_view owner_legacy,
                       const bool root,
                       const std::optional<core::InventoryFolder>& parent) {
    const auto parent_id = root || !parent
                               ? "00000000-0000-0000-0000-000000000000"
                               : folder_legacy(*parent);
    return node("ParentID", parent_id) +
           node("Type", root ? "8" : "-1") +
           node("Version", "1") +
           node("Name", folder.name) +
           node("Owner", owner_legacy) +
           node("ID", folder_legacy(folder));
}

std::string item_xml(const core::InventoryItem& item,
                     const std::string_view owner_legacy,
                     const std::optional<core::AssetInfo>& asset,
                     const std::string_view parent_legacy) {
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
           node("Folder", parent_legacy) +
           node("GroupID", "00000000-0000-0000-0000-000000000000") +
           node("GroupOwned", "False") +
           node("GroupPermissions", "0") +
           node("ID", item_legacy(item)) +
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

std::string bool_response(const bool value) {
    return response(node("RESULT", value ? "True" : "False"));
}

int hex_value(const char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + c - 'a';
    if (c >= 'A' && c <= 'F') return 10 + c - 'A';
    return -1;
}

std::string decode_component(const std::string_view value) {
    std::string output;
    output.reserve(value.size());
    for (std::size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '+') {
            output.push_back(' ');
        } else if (value[i] == '%' && i + 2U < value.size()) {
            const auto hi = hex_value(value[i + 1U]);
            const auto lo = hex_value(value[i + 2U]);
            if (hi < 0 || lo < 0) throw std::runtime_error("invalid form encoding");
            output.push_back(static_cast<char>((hi << 4) | lo));
            i += 2U;
        } else {
            output.push_back(value[i]);
        }
    }
    return output;
}

std::vector<std::pair<std::string, std::string>> form_pairs(const std::string_view body) {
    std::vector<std::pair<std::string, std::string>> output;
    std::size_t start = 0;
    while (start <= body.size()) {
        const auto end = body.find('&', start);
        const auto part = body.substr(start, end == std::string_view::npos
                                                ? std::string_view::npos
                                                : end - start);
        if (!part.empty()) {
            const auto split = part.find('=');
            auto key = decode_component(part.substr(0, split));
            auto value = split == std::string_view::npos
                             ? std::string{}
                             : decode_component(part.substr(split + 1U));
            output.emplace_back(std::move(key), std::move(value));
        }
        if (end == std::string_view::npos) break;
        start = end + 1U;
    }
    return output;
}

std::vector<std::string> list_values(
    const std::vector<std::pair<std::string, std::string>>& fields,
    const std::string_view name) {
    std::vector<std::string> output;
    const auto array_name = std::string{name} + "[]";
    for (const auto& [key, value] : fields) {
        if (key != name && key != array_name) continue;
        std::size_t start = 0;
        while (start <= value.size()) {
            const auto end = value.find(',', start);
            const auto entry = value.substr(start, end == std::string::npos
                                                       ? std::string::npos
                                                       : end - start);
            if (!entry.empty()) output.push_back(entry);
            if (end == std::string::npos) break;
            start = end + 1U;
        }
    }
    return output;
}

} // namespace

HypergridInventoryAdapter::HypergridInventoryAdapter(
    std::shared_ptr<core::IdentityStore> identities,
    std::shared_ptr<core::InventoryStore> inventory,
    std::shared_ptr<core::AssetStore> assets,
    const bool write_enabled)
    : identities_(std::move(identities)),
      inventory_(std::move(inventory)),
      assets_(std::move(assets)),
      write_enabled_(write_enabled) {
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

std::optional<core::InventoryFolder> HypergridInventoryAdapter::folder_by_legacy(
    const std::string_view native_user,
    const std::string_view legacy_uuid) const {
    const auto current = inventory_->list(native_user);
    if (folder_legacy(current.root) == legacy_uuid) return current.root;
    for (const auto& folder : current.folders) {
        if (folder_legacy(folder) == legacy_uuid) return folder;
    }
    return std::nullopt;
}

std::optional<core::InventoryItem> HypergridInventoryAdapter::item_by_legacy(
    const std::string_view native_user,
    const std::string_view legacy_uuid) const {
    const auto current = inventory_->list(native_user);
    for (const auto& item : current.items) {
        if (item_legacy(item) == legacy_uuid) return item;
    }
    return std::nullopt;
}

std::optional<core::AssetInfo> HypergridInventoryAdapter::asset_by_legacy(
    const std::string_view native_user,
    const std::string_view legacy_uuid,
    const bool require_transfer) const {
    for (const auto& asset : assets_->list_for_user(native_user)) {
        if (HypergridAssetAdapter::legacy_asset_uuid(asset.id) != legacy_uuid) continue;
        if (!core::has_permission(asset.permissions, core::perm_export)) return std::nullopt;
        if (require_transfer &&
            !core::has_permission(asset.permissions, core::perm_transfer)) {
            return std::nullopt;
        }
        return asset;
    }
    return std::nullopt;
}

std::string HypergridInventoryAdapter::handle_form(const std::string_view body) const {
    std::unordered_map<std::string, std::string> fields;
    std::vector<std::pair<std::string, std::string>> pairs;
    try {
        fields = parse_form_urlencoded(body);
        pairs = form_pairs(body);
    } catch (...) {
        return bool_response(false);
    }

    const auto method_it = fields.find("METHOD");
    if (method_it == fields.end()) return bool_response(false);
    const auto& method = method_it->second;

    const auto principal_user = [&]() -> std::optional<std::string> {
        const auto principal = fields.find("PRINCIPAL");
        if (principal == fields.end()) return std::nullopt;
        return native_user_for_legacy(principal->second);
    };

    const auto owner_user = [&]() -> std::optional<std::string> {
        const auto owner = fields.find("Owner");
        if (owner == fields.end()) return std::nullopt;
        return native_user_for_legacy(owner->second);
    };

    if (method == "CREATEUSERINVENTORY") {
        const auto user = principal_user();
        if (!user) return bool_response(false);
        (void)inventory_->ensure_root(*user);
        return bool_response(true);
    }

    const auto read_user = principal_user();
    if (method == "GETROOTFOLDER" || method == "GETINVENTORYSKELETON" ||
        method == "GETFOLDERCONTENT" || method == "GETFOLDERITEMS" ||
        method == "GETITEM" || method == "GETFOLDER" ||
        method == "GETASSETPERMISSIONS") {
        if (!read_user) return bool_response(false);
        const auto inventory = inventory_->list(*read_user);
        const auto principal = fields.at("PRINCIPAL");

        if (method == "GETROOTFOLDER") {
            return response("<folder type=\"List\">" +
                            folder_xml(inventory.root, principal, true, std::nullopt) +
                            "</folder>");
        }

        if (method == "GETINVENTORYSKELETON") {
            std::ostringstream out;
            out << "<FOLDERS type=\"List\"><folder_0 type=\"List\">"
                << folder_xml(inventory.root, principal, true, std::nullopt)
                << "</folder_0>";
            std::size_t index = 1;
            for (const auto& folder : inventory.folders) {
                const auto parent = inventory_->find_folder(*read_user, folder.parent_id);
                out << "<folder_" << index << " type=\"List\">"
                    << folder_xml(folder, principal, false, parent)
                    << "</folder_" << index << ">";
                ++index;
            }
            out << "</FOLDERS>";
            return response(out.str());
        }

        if (method == "GETFOLDER") {
            const auto id = fields.find("ID");
            if (id == fields.end()) return response("");
            const auto folder = folder_by_legacy(*read_user, id->second);
            if (!folder) return response("");
            const auto root = folder->parent_id.empty();
            const auto parent = root
                                    ? std::optional<core::InventoryFolder>{}
                                    : inventory_->find_folder(*read_user, folder->parent_id);
            return response("<folder type=\"List\">" +
                            folder_xml(*folder, principal, root, parent) +
                            "</folder>");
        }

        if (method == "GETFOLDERCONTENT" || method == "GETFOLDERITEMS") {
            const auto requested_it = fields.find("FOLDER");
            if (requested_it == fields.end()) return bool_response(false);
            const auto requested_folder =
                folder_by_legacy(*read_user, requested_it->second);
            if (!requested_folder) return bool_response(false);

            std::ostringstream folders;
            std::ostringstream items;
            std::size_t folder_index = 0;
            std::size_t item_index = 0;

            for (const auto& folder : inventory.folders) {
                if (folder.parent_id != requested_folder->id) continue;
                folders << "<folder_" << folder_index << " type=\"List\">"
                        << folder_xml(folder, principal, false, requested_folder)
                        << "</folder_" << folder_index << ">";
                ++folder_index;
            }
            for (const auto& item : inventory.items) {
                if (item.parent_id != requested_folder->id) continue;
                const auto asset = assets_->find(item.asset_id);
                if (!asset || !core::has_permission(asset->permissions, core::perm_export)) {
                    continue;
                }
                items << "<item_" << item_index << " type=\"List\">"
                      << item_xml(item, principal, asset, folder_legacy(*requested_folder))
                      << "</item_" << item_index << ">";
                ++item_index;
            }

            if (method == "GETFOLDERITEMS") {
                return response("<ITEMS type=\"List\">" + items.str() + "</ITEMS>");
            }
            return response(node("FID", requested_it->second) +
                            node("VERSION", "1") +
                            "<FOLDERS type=\"List\">" + folders.str() + "</FOLDERS>" +
                            "<ITEMS type=\"List\">" + items.str() + "</ITEMS>");
        }

        if (method == "GETITEM") {
            const auto id = fields.find("ID");
            if (id == fields.end()) return response("");
            const auto item = item_by_legacy(*read_user, id->second);
            if (!item) return response("");
            const auto asset = assets_->find(item->asset_id);
            if (!asset || !core::has_permission(asset->permissions, core::perm_export)) {
                return response("");
            }
            const auto parent = inventory_->find_folder(*read_user, item->parent_id);
            if (!parent) return response("");
            return response("<item type=\"List\">" +
                            item_xml(*item, principal, asset, folder_legacy(*parent)) +
                            "</item>");
        }

        if (method == "GETASSETPERMISSIONS") {
            const auto asset = fields.find("ASSET");
            if (asset == fields.end()) return response(node("RESULT", "0"));
            const auto found = asset_by_legacy(*read_user, asset->second, false);
            return response(node("RESULT", found ? std::to_string(found->permissions) : "0"));
        }
    }

    if (!write_enabled_) return bool_response(false);

    if (method == "ADDFOLDER" || method == "UPDATEFOLDER") {
        const auto user = owner_user();
        const auto id = fields.find("ID");
        const auto parent = fields.find("ParentID");
        const auto name = fields.find("Name");
        if (!user || id == fields.end() || parent == fields.end() || name == fields.end()) {
            return bool_response(false);
        }
        const auto parent_folder = folder_by_legacy(*user, parent->second);
        if (!parent_folder) return bool_response(false);

        std::string reason;
        if (method == "ADDFOLDER") {
            const auto created = inventory_->create_folder(
                *user, parent_folder->id, name->second, reason, id->second);
            return bool_response(created.has_value());
        }

        const auto folder = folder_by_legacy(*user, id->second);
        if (!folder) return bool_response(false);
        return bool_response(inventory_->update_folder(
            *user, folder->id, parent_folder->id, name->second, reason));
    }

    if (method == "MOVEFOLDER") {
        const auto user = principal_user();
        const auto id = fields.find("ID");
        const auto parent = fields.find("ParentID");
        if (!user || id == fields.end() || parent == fields.end()) return bool_response(false);
        const auto folder = folder_by_legacy(*user, id->second);
        const auto parent_folder = folder_by_legacy(*user, parent->second);
        if (!folder || !parent_folder) return bool_response(false);
        std::string reason;
        return bool_response(inventory_->move_folder(
            *user, folder->id, parent_folder->id, reason));
    }

    if (method == "DELETEFOLDERS") {
        const auto user = principal_user();
        if (!user) return bool_response(false);
        const auto ids = list_values(pairs, "FOLDERS");
        if (ids.empty()) return bool_response(false);
        std::string reason;
        for (const auto& legacy : ids) {
            const auto folder = folder_by_legacy(*user, legacy);
            if (!folder || !inventory_->delete_folder(*user, folder->id, true, reason)) {
                return bool_response(false);
            }
        }
        return bool_response(true);
    }

    if (method == "PURGEFOLDER") {
        const auto id = fields.find("ID");
        if (id == fields.end()) return bool_response(false);
        for (const auto& identity : identities_->list()) {
            const auto folder = folder_by_legacy(identity.id, id->second);
            if (!folder) continue;
            std::string reason;
            return bool_response(inventory_->delete_folder(
                identity.id, folder->id, false, reason));
        }
        return bool_response(false);
    }

    if (method == "ADDITEM" || method == "UPDATEITEM") {
        const auto user = owner_user();
        const auto id = fields.find("ID");
        const auto folder_id = fields.find("Folder");
        const auto asset_id = fields.find("AssetID");
        const auto name = fields.find("Name");
        if (!user || id == fields.end() || folder_id == fields.end() ||
            asset_id == fields.end() || name == fields.end()) {
            return bool_response(false);
        }

        const auto folder = folder_by_legacy(*user, folder_id->second);
        const auto asset = asset_by_legacy(*user, asset_id->second, true);
        if (!folder || !asset) return bool_response(false);

        std::string reason;
        if (method == "ADDITEM") {
            const auto created = inventory_->create_item(
                *user, folder->id, asset->id, name->second, reason, id->second);
            return bool_response(created.has_value());
        }

        const auto item = item_by_legacy(*user, id->second);
        if (!item) return bool_response(false);
        return bool_response(inventory_->update_item(
            *user, item->id, folder->id, asset->id, name->second, reason));
    }

    if (method == "MOVEITEMS") {
        const auto user = principal_user();
        if (!user) return bool_response(false);
        const auto ids = list_values(pairs, "IDLIST");
        const auto destinations = list_values(pairs, "DESTLIST");
        if (ids.empty() || ids.size() != destinations.size()) return bool_response(false);

        std::string reason;
        for (std::size_t i = 0; i < ids.size(); ++i) {
            const auto item = item_by_legacy(*user, ids[i]);
            const auto folder = folder_by_legacy(*user, destinations[i]);
            if (!item || !folder ||
                !inventory_->move_item(*user, item->id, folder->id, reason)) {
                return bool_response(false);
            }
        }
        return bool_response(true);
    }

    if (method == "DELETEITEMS") {
        const auto user = principal_user();
        if (!user) return bool_response(false);
        const auto ids = list_values(pairs, "ITEMS");
        if (ids.empty()) return bool_response(false);

        std::string reason;
        for (const auto& legacy : ids) {
            const auto item = item_by_legacy(*user, legacy);
            if (!item || !inventory_->delete_item(*user, item->id, reason)) {
                return bool_response(false);
            }
        }
        return bool_response(true);
    }

    return bool_response(false);
}

} // namespace opengenesis::compat::hypergrid
