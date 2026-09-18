#include "opengenesis/avatar/appearance_store.hpp"
#include "opengenesis/compat/hypergrid/asset_adapter.hpp"
#include "opengenesis/compat/hypergrid/friends_adapter.hpp"
#include "opengenesis/compat/hypergrid/session_store.hpp"
#include "opengenesis/core/asset_store.hpp"
#include "opengenesis/core/friends_store.hpp"
#include "opengenesis/core/identity_store.hpp"
#include "opengenesis/core/notification_store.hpp"
#include "opengenesis/core/permissions.hpp"
#include "opengenesis/core/presence_store.hpp"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

namespace {
void require(bool ok, const std::string& message) {
    if (!ok) throw std::runtime_error(message);
}
std::filesystem::path temp_root() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto root = std::filesystem::temp_directory_path() /
                      ("ogl-400-tests-" + std::to_string(stamp));
    std::filesystem::create_directories(root);
    return root;
}
}

int main() {
    try {
        const auto root = temp_root();
        std::string reason;

        const auto appearance_path = root / "appearance.db";
        {
            opengenesis::avatar::AppearanceStore appearance(appearance_path.string());
            require(appearance.ensure("user-a").revision == 1, "appearance create");
            const auto wearable = appearance.set_wearable(
                "user-a", "shirt", "item-shirt", "asset-shirt", reason);
            require(wearable && wearable->wearables.size() == 1, "wearable");
            const auto attached = appearance.attach(
                "user-a", "right-hand", "item-tool", "asset-tool", reason);
            require(attached && attached->attachments.size() == 1, "attachment");
        }
        {
            opengenesis::avatar::AppearanceStore appearance(appearance_path.string());
            const auto restored = appearance.find("user-a");
            require(restored && restored->wearables.size() == 1 &&
                        restored->attachments.size() == 1,
                    "appearance persistence");
        }

        auto identities = std::make_shared<opengenesis::core::IdentityStore>(
            (root / "users.db").string());
        const auto alice = identities->register_user(
            "alice.hg", "Alice HG", "correct horse battery staple", reason);
        const auto bob = identities->register_user(
            "bob.hg", "Bob HG", "correct horse battery staple", reason);
        require(alice && bob, "identities");

        auto friends = std::make_shared<opengenesis::core::FriendsStore>(
            (root / "friends.db").string());
        auto presences = std::make_shared<opengenesis::core::PresenceStore>();
        auto notifications = std::make_shared<opengenesis::core::NotificationStore>(
            (root / "notifications.db").string());
        auto hg_sessions =
            std::make_shared<opengenesis::compat::hypergrid::HypergridSessionStore>(
                (root / "hg-sessions.db").string());

        opengenesis::compat::hypergrid::HypergridFriendsAdapter hg_friends(
            identities, friends, presences, notifications, hg_sessions);

        const auto alice_legacy =
            opengenesis::compat::hypergrid::legacy_uuid_from_seed(alice->id);
        const auto bob_legacy =
            opengenesis::compat::hypergrid::legacy_uuid_from_seed(bob->id);
        const std::string remote = "11111111-2222-4333-8444-555555555555";
        const std::string remote_uui =
            remote + ";http://remote.example:8002;Remote User;secret123";

        const auto travel = hg_sessions->issue_home_travel(
            alice->id, alice_legacy, "http://remote.example:8002",
            "192.0.2.50", std::chrono::seconds{600});

        auto response = hg_friends.handle_form(
            "METHOD=newfriendship&PrincipalID=" + alice_legacy +
            "&Friend=" + remote_uui +
            "&MyFlags=1&TheirFlags=2&SESSIONID=" + travel.session_id +
            "&KEY=" + travel.service_token);
        require(response.find("Success") != std::string::npos, "HG pending");
        auto relation = friends->find_relation(alice->id, "hg:" + remote);
        require(relation && relation->status == "pending" &&
                    relation->interop_secret == "secret123",
                "native pending relation");

        response = hg_friends.handle_form(
            "METHOD=newfriendship&PrincipalID=" + alice_legacy +
            "&Friend=" + remote_uui + "&MyFlags=1&TheirFlags=2");
        require(response.find("Success") != std::string::npos, "HG accept");
        relation = friends->find_relation(alice->id, "hg:" + remote);
        require(relation && relation->status == "accepted", "native accepted relation");

        response = hg_friends.handle_form(
            "METHOD=getfriendperms&PRINCIPALID=" + alice_legacy +
            "&FRIENDID=" + remote +
            "&SESSIONID=" + travel.session_id +
            "&KEY=" + travel.service_token);
        require(response.find("<Value>2</Value>") != std::string::npos, "HG perms");

        presences->replace_region_snapshot(
            "region-a", "world-a", 1,
            {{.user_id = alice->id,
              .display_name = alice->display_name,
              .region_id = {},
              .node_id = {},
              .node_generation = 0,
              .entity_id = 42,
              .x = 128.0,
              .y = 128.0,
              .z = 23.0,
              .updated_unix = 0}});

        response = hg_friends.handle_form(
            "METHOD=statusnotification&userID=" + remote +
            "&online=true&friend_0=" + alice_legacy +
            ";http://local.example:8002;Alice HG;secret123");
        require(response.find(alice_legacy) != std::string::npos, "HG status");

        response = hg_friends.handle_form(
            "METHOD=deletefriendship&PrincipalID=" + alice_legacy +
            "&Friend=" + remote + "&SECRET=secret123");
        require(response.find("true") != std::string::npos, "HG delete");

        const std::string remote2 = "aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeeee";
        response = hg_friends.handle_form(
            "METHOD=friendship_offered&FromID=" + remote2 +
            "&ToID=" + bob_legacy +
            "&FromName=Visitor@remote.example&Message=hello");
        require(response.find("true") != std::string::npos, "HG offer");
        require(notifications->unread_count(bob->id) == 1, "HG notification");

        response = hg_friends.handle_form(
            "METHOD=validate_friendship_offered&PrincipalID=" + remote2 +
            "&Friend=" + bob_legacy);
        require(response.find("true") != std::string::npos, "HG validate");

        auto assets = std::make_shared<opengenesis::core::AssetStore>(
            (root / "assets.db").string(), root / "blobs", 1024U * 1024U);
        const std::string payload = "OpenGenesis-Hypergrid-Asset";
        const auto exported = assets->create(
            alice->id, "HG Texture", "image/jp2", payload, reason,
            opengenesis::core::perm_all,
            opengenesis::core::perm_copy | opengenesis::core::perm_transfer);
        require(exported.has_value(), "exportable asset");

        opengenesis::compat::hypergrid::HypergridAssetAdapter hg_assets(assets);
        const auto legacy_asset =
            opengenesis::compat::hypergrid::HypergridAssetAdapter::legacy_asset_uuid(
                exported->id);
        const auto data = hg_assets.handle_get("/assets/" + legacy_asset + "/data");
        require(data.status == 200 && data.body == payload, "HG asset data");

        const auto metadata =
            hg_assets.handle_get("/assets/" + legacy_asset + "/metadata");
        require(metadata.status == 200 &&
                    metadata.body.find("<AssetMetadata>") != std::string::npos,
                "HG metadata");

        const auto full = hg_assets.handle_get("/assets/" + legacy_asset);
        require(full.status == 200 &&
                    full.body.find("<AssetBase>") != std::string::npos,
                "HG full asset");

        const auto private_asset = assets->create(
            alice->id, "Private", "application/octet-stream", "private", reason,
            opengenesis::core::perm_copy | opengenesis::core::perm_transfer,
            opengenesis::core::perm_copy);
        require(private_asset.has_value(), "private asset");
        const auto private_legacy =
            opengenesis::compat::hypergrid::HypergridAssetAdapter::legacy_asset_uuid(
                private_asset->id);
        require(hg_assets.handle_get("/assets/" + private_legacy + "/data").status == 404,
                "Export permission");

        std::filesystem::remove_all(root);
        std::cout << "OpenGenesisLINK 4.0 avatar/HG social/content tests: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "OpenGenesisLINK 4.0 test failure: " << error.what() << '\n';
        return 1;
    }
}
