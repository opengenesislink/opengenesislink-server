#include "opengenesis/compat/hypergrid/appearance_adapter.hpp"

#include "opengenesis/compat/hypergrid/friends_adapter.hpp"
#include "opengenesis/compat/hypergrid/inventory_adapter.hpp"
#include "opengenesis/core/permissions.hpp"

#include <algorithm>
#include <map>
#include <sstream>
#include <stdexcept>
#include <utility>

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

std::string response(const std::string_view body) {
    return "<?xml version=\"1.0\"?><ServerResponse>" + std::string{body} +
           "</ServerResponse>";
}

int wearable_index(const std::string_view slot) {
    static const std::map<std::string_view, int> slots{
        {"body",0},{"shape",0},{"skin",1},{"hair",2},{"eyes",3},
        {"shirt",4},{"pants",5},{"shoes",6},{"socks",7},{"jacket",8},
        {"gloves",9},{"undershirt",10},{"underpants",11},{"skirt",12},
        {"alpha",13},{"tattoo",14},{"physics",15},{"universal",16}};
    if (const auto it = slots.find(slot); it != slots.end()) return it->second;
    try {
        const auto value = std::stoi(std::string{slot});
        return value >= 0 && value <= 31 ? value : -1;
    } catch (...) {
        return -1;
    }
}

int attachment_point(const std::string_view point) {
    try {
        const auto value = std::stoi(std::string{point});
        return value >= 0 && value <= 255 ? value : -1;
    } catch (...) {
        return -1;
    }
}

} // namespace

HypergridAppearanceAdapter::HypergridAppearanceAdapter(
    std::shared_ptr<core::IdentityStore> identities,
    std::shared_ptr<avatar::AppearanceStore> appearance,
    std::shared_ptr<core::InventoryStore> inventory,
    std::shared_ptr<core::AssetStore> assets,
    std::shared_ptr<HypergridSessionStore> sessions)
    : identities_(std::move(identities)),
      appearance_(std::move(appearance)),
      inventory_(std::move(inventory)),
      assets_(std::move(assets)),
      sessions_(std::move(sessions)) {
    if (!identities_ || !appearance_ || !inventory_ || !assets_ || !sessions_) {
        throw std::invalid_argument("HG Appearance dependencies required");
    }
}

std::optional<std::string> HypergridAppearanceAdapter::native_user_for_legacy(
    const std::string_view legacy_uuid) const {
    for (const auto& user : identities_->list()) {
        if (legacy_uuid_from_seed(user.id) == legacy_uuid) return user.id;
    }
    return std::nullopt;
}

std::string HypergridAppearanceAdapter::handle_form(const std::string_view body) const {
    std::unordered_map<std::string, std::string> fields;
    try {
        fields = parse_form_urlencoded(body);
    } catch (...) {
        return response(node("result", "Failure"));
    }

    const auto method_it = fields.find("METHOD");
    const auto user_it = fields.find("UserID");
    if (method_it == fields.end() || user_it == fields.end()) {
        return response(node("result", "Failure"));
    }
    const auto native_user = native_user_for_legacy(user_it->second);
    if (!native_user) return response(node("result", "Failure"));

    if (method_it->second == "getavatar") {
        const auto appearance = appearance_->ensure(*native_user);
        std::ostringstream data;
        data << "<result>" << node("AvatarType", "1")
             << node("Serial", std::to_string(appearance.revision))
             << node("AvatarHeight", "1.9");

        for (const auto& wearable : appearance.wearables) {
            const auto index = wearable_index(wearable.slot);
            const auto asset = assets_->find(wearable.asset_id);
            if (index < 0 || !asset ||
                !core::has_permission(asset->permissions, core::perm_export)) {
                continue;
            }
            const auto field = "Wearable " + std::to_string(index) + ":0";
            const auto item = HypergridInventoryAdapter::legacy_item_uuid(wearable.item_id);
            const auto asset_id = HypergridAssetAdapter::legacy_asset_uuid(asset->id);
            data << node(field, item + ":" + asset_id);
        }

        std::map<int, std::vector<std::string>> attachments;
        for (const auto& attachment : appearance.attachments) {
            const auto point = attachment_point(attachment.point);
            const auto asset = assets_->find(attachment.asset_id);
            if (point < 0 || !asset ||
                !core::has_permission(asset->permissions, core::perm_export)) {
                continue;
            }
            attachments[point].push_back(
                HypergridInventoryAdapter::legacy_item_uuid(attachment.item_id));
        }
        for (const auto& [point, items] : attachments) {
            std::string joined;
            for (std::size_t i = 0; i < items.size(); ++i) {
                if (i) joined += ',';
                joined += items[i];
            }
            data << node("_ap_" + std::to_string(point), joined);
        }
        data << "</result>";
        return response(data.str());
    }

    // OpenSim's AvatarService has no per-user travel token in its normal setavatar
    // request. Mutating appearance is therefore denied unless the caller supplies
    // the active HG SESSIONID/KEY extension used by OpenGenesisLINK.
    if (method_it->second == "setavatar") {
        const auto session_it = fields.find("SESSIONID");
        const auto key_it = fields.find("KEY");
        if (session_it == fields.end() || key_it == fields.end() ||
            !sessions_->verify_agent(session_it->second, key_it->second)) {
            return response(node("result", "Failure"));
        }
        const auto home = sessions_->home(session_it->second);
        if (!home || home->native_user_id != *native_user) {
            return response(node("result", "Failure"));
        }

        std::string reason;
        for (const auto& [name, value] : fields) {
            if (!name.starts_with("Wearable ")) continue;
            const auto space = name.find(' ');
            const auto colon = name.find(':', space + 1);
            if (colon == std::string::npos) continue;
            const auto ids = value.find(':');
            if (ids == std::string::npos) continue;
            const auto legacy_asset = value.substr(ids + 1);

            std::optional<core::AssetInfo> native_asset;
            for (const auto& asset : assets_->list_for_user(*native_user)) {
                if (HypergridAssetAdapter::legacy_asset_uuid(asset.id) == legacy_asset) {
                    native_asset = asset;
                    break;
                }
            }
            if (!native_asset) continue;

            const auto slot = name.substr(space + 1, colon - space - 1);
            (void)appearance_->set_wearable(
                *native_user, slot, value.substr(0, ids), native_asset->id, reason);
        }
        return response(node("result", "Success"));
    }

    return response(node("result", "Failure"));
}

} // namespace opengenesis::compat::hypergrid
