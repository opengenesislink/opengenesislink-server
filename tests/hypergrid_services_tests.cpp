#include "opengenesis/avatar/appearance_store.hpp"
#include "opengenesis/compat/hypergrid/appearance_adapter.hpp"
#include "opengenesis/compat/hypergrid/asset_adapter.hpp"
#include "opengenesis/compat/hypergrid/im_adapter.hpp"
#include "opengenesis/compat/hypergrid/inventory_adapter.hpp"
#include "opengenesis/compat/hypergrid/session_store.hpp"
#include "opengenesis/core/asset_store.hpp"
#include "opengenesis/core/identity_store.hpp"
#include "opengenesis/core/inventory_store.hpp"
#include "opengenesis/core/message_store.hpp"
#include "opengenesis/core/notification_store.hpp"
#include "opengenesis/core/permissions.hpp"

#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

namespace {

void require(const bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

std::filesystem::path temp_root() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto root = std::filesystem::temp_directory_path() /
                      ("ogl-450-tests-" + std::to_string(stamp));
    std::filesystem::create_directories(root);
    return root;
}

std::int64_t unix_now() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

} // namespace

int main() {
    try {
        const auto root = temp_root();
        std::string reason;

        auto identities = std::make_shared<opengenesis::core::IdentityStore>(
            (root / "users.db").string());
        const auto user = identities->register_user(
            "hg.services", "HG Services", "correct horse battery staple", reason);
        require(user.has_value(), "identity created");

        auto assets = std::make_shared<opengenesis::core::AssetStore>(
            (root / "assets.db").string(), root / "blobs", 1024U * 1024U);
        const auto asset = assets->create(
            user->id, "Wearable Asset", "application/vnd.ll.clothing",
            "wearable-data", reason, opengenesis::core::perm_all,
            opengenesis::core::perm_copy | opengenesis::core::perm_transfer);
        require(asset.has_value(), "exportable asset created");

        auto inventory = std::make_shared<opengenesis::core::InventoryStore>(
            (root / "inventory.db").string());
        const auto root_folder = inventory->ensure_root(user->id);
        const auto item = inventory->create_item(
            user->id, root_folder.id, asset->id, "Wearable Item", reason);
        require(item.has_value(), "inventory item created");

        auto appearance = std::make_shared<opengenesis::avatar::AppearanceStore>(
            (root / "appearance.db").string());
        require(appearance->set_legacy_body(user->id, 1.87, "1,2,3", reason).has_value(),
                "legacy body stored");
        require(appearance->set_wearable(
                    user->id, "4", item->id, asset->id, reason).has_value(),
                "wearable stored");
        require(appearance->attach(
                    user->id, "5", item->id, asset->id, reason).has_value(),
                "attachment stored");

        auto sessions =
            std::make_shared<opengenesis::compat::hypergrid::HypergridSessionStore>(
                (root / "hg-sessions.db").string());
        const auto legacy_user =
            opengenesis::compat::hypergrid::legacy_uuid_from_seed(user->id);

        opengenesis::compat::hypergrid::HypergridInventoryAdapter hg_inventory(
            identities, inventory, assets);

        const auto root_xml = hg_inventory.handle_form(
            "METHOD=GETROOTFOLDER&PRINCIPAL=" + legacy_user);
        require(root_xml.find("<folder type=\"List\">") != std::string::npos,
                "XInventory root folder encoded as List");

        const auto skeleton_xml = hg_inventory.handle_form(
            "METHOD=GETINVENTORYSKELETON&PRINCIPAL=" + legacy_user);
        require(skeleton_xml.find("<FOLDERS type=\"List\">") != std::string::npos,
                "XInventory skeleton returned");

        const auto legacy_root =
            opengenesis::compat::hypergrid::HypergridInventoryAdapter::legacy_folder_uuid(
                root_folder.id);
        const auto content_xml = hg_inventory.handle_form(
            "METHOD=GETFOLDERCONTENT&PRINCIPAL=" + legacy_user +
            "&FOLDER=" + legacy_root);
        require(content_xml.find("Wearable Item") != std::string::npos,
                "XInventory item returned");
        require(content_xml.find("<ITEMS type=\"List\">") != std::string::npos,
                "XInventory item list encoded");
        const auto blocked_write = hg_inventory.handle_form(
            "METHOD=ADDFOLDER&ParentID=" + legacy_root +
            "&Type=-1&Version=1&Name=Blocked&Owner=" + legacy_user +
            "&ID=11111111-1111-4111-8111-111111111111");
        require(blocked_write.find("<RESULT>False</RESULT>") != std::string::npos,
                "XInventory writes disabled by default");

        const std::string write_key =
            "test-xinventory-service-secret-123456";
        opengenesis::compat::hypergrid::HypergridInventoryAdapter hg_inventory_write(
            identities, inventory, assets, true, write_key);
        const std::string legacy_folder_a =
            "22222222-2222-4222-8222-222222222222";
        const std::string legacy_folder_b =
            "33333333-3333-4333-8333-333333333333";
        const std::string legacy_write_item =
            "44444444-4444-4444-8444-444444444444";
        const auto legacy_asset_for_write =
            opengenesis::compat::hypergrid::HypergridAssetAdapter::legacy_asset_uuid(
                asset->id);

        auto write_xml = hg_inventory_write.handle_form(
            "SERVICEKEY=" + write_key + "&METHOD=ADDFOLDER&ParentID=" + legacy_root +
            "&Type=-1&Version=1&Name=Remote+Folder&Owner=" + legacy_user +
            "&ID=" + legacy_folder_a);
        require(write_xml.find("<RESULT>True</RESULT>") != std::string::npos,
                "XInventory folder add accepted");

        write_xml = hg_inventory_write.handle_form(
            "SERVICEKEY=" + write_key + "&METHOD=ADDFOLDER&ParentID=" + legacy_root +
            "&Type=-1&Version=1&Name=Destination&Owner=" + legacy_user +
            "&ID=" + legacy_folder_b);
        require(write_xml.find("<RESULT>True</RESULT>") != std::string::npos,
                "XInventory second folder add accepted");

        write_xml = hg_inventory_write.handle_form(
            "SERVICEKEY=" + write_key + "&METHOD=ADDITEM&AssetID=" + legacy_asset_for_write +
            "&AssetType=5&Name=Remote+Wearable&Owner=" + legacy_user +
            "&ID=" + legacy_write_item +
            "&InvType=18&Folder=" + legacy_folder_a +
            "&CreatorId=" + legacy_user +
            "&Description=&NextPermissions=0&CurrentPermissions=0"
            "&BasePermissions=0&EveryOnePermissions=0&GroupPermissions=0"
            "&GroupID=00000000-0000-0000-0000-000000000000"
            "&GroupOwned=False&SalePrice=0&SaleType=0&Flags=0&CreationDate=0");
        require(write_xml.find("<RESULT>True</RESULT>") != std::string::npos,
                "XInventory item add accepted");

        write_xml = hg_inventory_write.handle_form(
            "SERVICEKEY=" + write_key + "&METHOD=MOVEITEMS&PRINCIPAL=" + legacy_user +
            "&IDLIST[]=" + legacy_write_item +
            "&DESTLIST[]=" + legacy_folder_b);
        require(write_xml.find("<RESULT>True</RESULT>") != std::string::npos,
                "XInventory item move accepted");

        write_xml = hg_inventory_write.handle_form(
            "SERVICEKEY=" + write_key + "&METHOD=UPDATEFOLDER&ParentID=" + legacy_root +
            "&Type=-1&Version=2&Name=Renamed+Destination&Owner=" + legacy_user +
            "&ID=" + legacy_folder_b);
        require(write_xml.find("<RESULT>True</RESULT>") != std::string::npos,
                "XInventory folder update accepted");

        {
            auto restored_inventory = std::make_shared<opengenesis::core::InventoryStore>(
                (root / "inventory.db").string());
            opengenesis::compat::hypergrid::HypergridInventoryAdapter restored_adapter(
                identities, restored_inventory, assets, true, write_key);
            const auto restored_xml = restored_adapter.handle_form(
                "METHOD=GETFOLDER&PRINCIPAL=" + legacy_user +
                "&ID=" + legacy_folder_b);
            require(restored_xml.find(legacy_folder_b) != std::string::npos &&
                        restored_xml.find("Renamed Destination") != std::string::npos,
                    "legacy folder alias survives Inventory restart");
            const auto restored_item = restored_adapter.handle_form(
                "METHOD=GETITEM&PRINCIPAL=" + legacy_user +
                "&ID=" + legacy_write_item);
            require(restored_item.find(legacy_write_item) != std::string::npos,
                    "legacy item alias survives Inventory restart");
        }

        write_xml = hg_inventory_write.handle_form(
            "SERVICEKEY=" + write_key + "&METHOD=DELETEITEMS&PRINCIPAL=" + legacy_user +
            "&ITEMS[]=" + legacy_write_item);
        require(write_xml.find("<RESULT>True</RESULT>") != std::string::npos,
                "XInventory item delete accepted");

        write_xml = hg_inventory_write.handle_form(
            "SERVICEKEY=" + write_key + "&METHOD=DELETEFOLDERS&PRINCIPAL=" + legacy_user +
            "&FOLDERS[]=" + legacy_folder_a +
            "&FOLDERS[]=" + legacy_folder_b);
        require(write_xml.find("<RESULT>True</RESULT>") != std::string::npos,
                "XInventory folder delete accepted");

        opengenesis::compat::hypergrid::HypergridAppearanceAdapter hg_appearance(
            identities, appearance, inventory, assets, sessions);
        const auto avatar_xml = hg_appearance.handle_form(
            "METHOD=getavatar&UserID=" + legacy_user);
        require(avatar_xml.find("<result type=\"List\">") != std::string::npos,
                "AvatarData List returned");
        require(avatar_xml.find("<AvatarHeight>1.870000</AvatarHeight>") !=
                    std::string::npos,
                "Avatar height exported");
        require(avatar_xml.find("<VisualParams>1,2,3</VisualParams>") !=
                    std::string::npos,
                "Visual params exported");
        require(avatar_xml.find("Wearable_x0020_4_x003A_0") != std::string::npos,
                "OpenSim wearable XML name encoded");
        require(avatar_xml.find("_ap_5") != std::string::npos,
                "attachment exported");

        const auto travel = sessions->issue_home_travel(
            user->id, legacy_user, "http://remote.example:8002",
            "192.0.2.80", std::chrono::seconds{600});
        const auto legacy_item =
            opengenesis::compat::hypergrid::HypergridInventoryAdapter::legacy_item_uuid(
                item->id);
        const auto legacy_asset =
            opengenesis::compat::hypergrid::HypergridAssetAdapter::legacy_asset_uuid(
                asset->id);

        const auto set_xml = hg_appearance.handle_form(
            "METHOD=setavatar&UserID=" + legacy_user +
            "&SESSIONID=" + travel.session_id +
            "&KEY=" + travel.service_token +
            "&AvatarHeight=1.82&VisualParams=4%2C5%2C6"
            "&Wearable+4%3A0=" + legacy_item + "%3A" + legacy_asset +
            "&_ap_5=" + legacy_item);
        require(set_xml.find("<result>Success</result>") != std::string::npos,
                "authenticated setavatar accepted");

        const auto updated = appearance->find(user->id);
        require(updated.has_value() &&
                    std::abs(updated->avatar_height - 1.82) < 0.001 &&
                    updated->visual_params_csv == "4,5,6",
                "legacy body imported");
        require(!updated->wearables.empty() && !updated->attachments.empty(),
                "wearables and attachments imported");

        auto messages = std::make_shared<opengenesis::core::MessageStore>(
            (root / "messages.db").string());
        auto notifications = std::make_shared<opengenesis::core::NotificationStore>(
            (root / "notifications.db").string());
        opengenesis::compat::hypergrid::HypergridInstantMessageAdapter hg_im(
            identities, messages, notifications, sessions);

        const std::string remote_agent =
            "12345678-1234-4234-8234-123456789abc";
        const auto incoming = hg_im.handle_incoming(
            {.method = "grid_instant_message",
             .fields = {{"from_agent_id", remote_agent},
                        {"to_agent_id", legacy_user},
                        {"im_session_id",
                         "aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeeee"},
                        {"timestamp", std::to_string(unix_now())},
                        {"from_agent_name", "Remote Resident"},
                        {"message", "hello over hypergrid"},
                        {"dialog", "AA=="},
                        {"from_group", "FALSE"},
                        {"offline", "AA=="},
                        {"parent_estate_id", "0"},
                        {"position_x", "0"},
                        {"position_y", "0"},
                        {"position_z", "0"},
                        {"region_id",
                         "00000000-0000-0000-0000-000000000000"},
                        {"binary_bucket", ""}}});
        require(incoming.at("success") == "TRUE", "incoming HG IM accepted");
        require(messages->count() == 1 && messages->unread_count(user->id) == 1,
                "incoming HG IM persisted");
        require(notifications->unread_count(user->id) == 1,
                "incoming HG IM notification created");

        require(sessions->request_return_home(
                    user->id, travel.session_id, "http://local.example:8002"),
                "return home requested");
        const auto returning = sessions->home(travel.session_id);
        require(returning &&
                    returning->state ==
                        opengenesis::compat::hypergrid::TravelState::returning_home,
                "return-home state persisted");
        require(sessions->is_agent_coming_home(
                    travel.session_id, "http://local.example:8002"),
                "agent_is_coming_home succeeds");
        require(sessions->verify_agent(travel.session_id, travel.service_token),
                "returning session remains verifiable");
        require(sessions->logout_home(legacy_user, travel.session_id),
                "return-home session logout");
        require(!sessions->verify_agent(travel.session_id, travel.service_token),
                "logged-out return session rejected");

        const auto now = unix_now();
        require(sessions->upsert_foreign(
                    {.session_id =
                         "87654321-4321-4321-8321-cba987654321",
                     .agent_id = remote_agent,
                     .home_uri = "https://remote.example:8002",
                     .asset_uri = "https://remote.example:8002",
                     .inventory_uri = "https://remote.example:8002",
                     .avatar_uri = "https://remote.example:8002",
                     .im_uri = "invalid://remote.example",
                     .service_token =
                         "http://local.example:8002;token",
                     .destination_region = "region-a",
                     .first_name = "Remote",
                     .last_name = "Resident",
                     .client_ip = "192.0.2.90",
                     .verified = true,
                     .created_unix = now,
                     .expires_unix = now + 600},
                    reason),
                "foreign service routes persisted");

        std::string send_reason;
        require(!hg_im.send_remote(
                    user->id, user->display_name, remote_agent,
                    "outbound test", send_reason) &&
                    send_reason == "invalid-http-url",
                "outbound HG IM rejects invalid transport explicitly");

        {
            opengenesis::compat::hypergrid::HypergridSessionStore restored(
                (root / "hg-sessions.db").string());
            const auto visitor = restored.foreign_by_agent(remote_agent);
            require(visitor && visitor->inventory_uri ==
                                   "https://remote.example:8002" &&
                        visitor->avatar_uri == "https://remote.example:8002" &&
                        visitor->im_uri == "invalid://remote.example",
                    "visitor service routes survive restart");
        }

        std::filesystem::remove_all(root);
        std::cout << "OpenGenesisLINK 5.5 HG services tests: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "OpenGenesisLINK 5.5 test failure: "
                  << error.what() << '\n';
        return 1;
    }
}
