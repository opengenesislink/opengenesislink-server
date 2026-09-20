#include "opengenesis/core/asset_store.hpp"
#include "opengenesis/core/economy_ledger.hpp"
#include "opengenesis/core/group_store.hpp"
#include "opengenesis/core/marketplace_store.hpp"
#include "opengenesis/core/parcel_store.hpp"
#include "opengenesis/core/social_policy_store.hpp"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void require(const bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

std::filesystem::path temp_root() {
    const auto stamp =
        std::chrono::steady_clock::now().time_since_epoch().count();
    const auto root =
        std::filesystem::temp_directory_path() /
        ("ogl-platform-services-" + std::to_string(stamp));
    std::filesystem::create_directories(root);
    return root;
}

} // namespace

int main() {
    try {
        const auto root = temp_root();
        std::string reason;

        {
            opengenesis::core::SocialPolicyStore policies(
                (root / "social.db").string());
            require(
                policies.set_blocked("alice", "bob", true, reason),
                "block policy created");
            require(
                policies.blocks("alice", "bob"),
                "block policy enforced");
            require(
                !policies.blocks("bob", "alice"),
                "block direction preserved");
            require(
                policies.set_muted("alice", "charlie", true, reason),
                "mute policy created");
            require(
                policies.muted("alice", "charlie"),
                "mute policy enforced");
            require(
                policies.list_for_user("alice").size() == 2U,
                "social policy listing");
        }
        {
            opengenesis::core::SocialPolicyStore policies(
                (root / "social.db").string());
            require(
                policies.blocks("alice", "bob") &&
                    policies.muted("alice", "charlie"),
                "social policies survive restart");
            require(
                policies.set_blocked("alice", "bob", false, reason),
                "unblock succeeds");
            require(
                !policies.blocks("alice", "bob"),
                "unblock persisted");
        }

        std::string invite_id;
        {
            opengenesis::core::GroupStore groups(
                (root / "groups.db").string());
            const auto group =
                groups.create("alice", "Builders", reason);
            require(group.has_value(), "group created");
            const auto invite =
                groups.invite(
                    "alice", group->id, "bob", "member",
                    std::chrono::seconds{600}, reason);
            require(invite.has_value(), "group invite created");
            invite_id = invite->id;
            require(
                groups.invites_for_user("bob").size() == 1U,
                "group invite visible to target");
        }
        {
            opengenesis::core::GroupStore groups(
                (root / "groups.db").string());
            const auto member =
                groups.accept_invite("bob", invite_id, reason);
            require(member.has_value(), "persisted invite accepted");
            require(
                groups.is_member(member->group_id, "bob"),
                "invite acceptance created membership");
            const auto second =
                groups.invite(
                    "alice", member->group_id, "charlie", "member",
                    std::chrono::seconds{60}, reason);
            require(second.has_value(), "second invite created");
            require(
                groups.purge_expired_invites(
                    second->expires_unix + 1) == 1U,
                "expired group invite reconciled");
        }

        std::string parcel_id;
        {
            opengenesis::core::ParcelStore parcels(
                (root / "parcels.db").string());
            const auto parcel =
                parcels.create(
                    "alice", "region-a", "Private parcel",
                    0, 0, 127, 127, reason);
            require(parcel.has_value(), "parcel created");
            parcel_id = parcel->id;
            require(
                parcels.update_policy(
                    "alice", parcel_id, "", true,
                    false, false, false, reason),
                "parcel policy updated");
            require(
                parcels.set_access(
                    "alice", parcel_id, "bob", false, reason),
                "parcel deny created");
            require(
                !parcels.can_enter(
                    "region-a", 10.0, 10.0, "bob", {}),
                "explicit parcel deny overrides public entry");
            require(
                parcels.set_access(
                    "alice", parcel_id, "charlie", true, reason),
                "parcel allow created");
        }
        {
            opengenesis::core::ParcelStore parcels(
                (root / "parcels.db").string());
            require(
                !parcels.can_enter(
                    "region-a", 10.0, 10.0, "bob", {}),
                "parcel deny survives restart");
            require(
                parcels.can_enter(
                    "region-a", 10.0, 10.0, "charlie", {}),
                "parcel allow survives restart");
            require(
                parcels.access_list(parcel_id).size() == 2U,
                "parcel access list restored");
        }

        std::string first_escrow_id;
        {
            opengenesis::core::EconomyLedger ledger(
                (root / "economy.db").string(), "OGL");
            const auto mint = ledger.mint(
                "alice", 10000, "mint:alice:1",
                "test funding", reason);
            require(mint.has_value(), "wallet mint succeeds");
            require(
                ledger.total_supply_minor() == 10000,
                "mint increases total supply");
            require(
                !ledger.mint(
                    "alice", 1, "mint:alice:1",
                    "duplicate", reason)
                     .has_value() &&
                    reason == "duplicate-reference",
                "ledger references are idempotent");

            const auto transfer = ledger.transfer(
                "alice", "bob", 1500, "transfer:1",
                "test transfer", reason);
            require(transfer.has_value(), "wallet transfer succeeds");
            require(
                ledger.account("alice")->balance_minor == 8500 &&
                    ledger.account("bob")->balance_minor == 1500,
                "wallet transfer updates balances");

            const auto escrow = ledger.reserve(
                "alice", "bob", 2000,
                "market:reservation:1", reason);
            require(escrow.has_value(), "escrow reservation succeeds");
            first_escrow_id = escrow->id;
            require(
                ledger.account("alice")->balance_minor == 6500 &&
                    ledger.total_supply_minor() == 10000,
                "reserved value remains in total supply");
            require(
                ledger.commit_escrow(escrow->id, reason),
                "escrow commit succeeds");
            require(
                ledger.account("bob")->balance_minor == 3500,
                "escrow commit credits seller");

            const auto refundable = ledger.reserve(
                "alice", "bob", 1000,
                "market:reservation:2", reason);
            require(refundable.has_value(), "second escrow reserved");
            require(
                ledger.release_escrow(refundable->id, reason),
                "escrow release succeeds");
            require(
                ledger.account("alice")->balance_minor == 6500,
                "escrow release refunds buyer");
            require(
                ledger.total_supply_minor() == 10000,
                "escrow operations conserve supply");
        }
        {
            opengenesis::core::EconomyLedger ledger(
                (root / "economy.db").string(), "OGL");
            require(
                ledger.account("alice")->balance_minor == 6500 &&
                    ledger.account("bob")->balance_minor == 3500,
                "wallet balances survive restart");
            const auto escrow = ledger.escrow(first_escrow_id);
            require(
                escrow &&
                    escrow->state ==
                        opengenesis::core::EscrowState::committed,
                "escrow state survives restart");
        }

        std::string listing_id;
        {
            opengenesis::core::MarketplaceStore market(
                (root / "market.db").string());
            const auto listing = market.create(
                "alice", "asset-1", "Virtual Shirt",
                "A test listing", 2500, "OGL", reason);
            require(listing.has_value(), "market listing created");
            listing_id = listing->id;
            require(
                market.reserve_purchase(
                    listing_id, "bob", "sale:1", reason),
                "market listing reserved");
            require(
                !market.reserve_purchase(
                    listing_id, "charlie", "sale:2", reason) &&
                    reason == "listing-not-active",
                "reserved listing rejects double purchase");
            require(
                market.release_purchase(
                    listing_id, "bob", "sale:1", reason),
                "market reservation released");
            require(
                market.reserve_purchase(
                    listing_id, "charlie", "sale:3", reason),
                "market listing can be reserved after release");
            require(
                market.complete_purchase(
                    listing_id, "charlie", "sale:3", reason),
                "market purchase completed");
        }
        {
            opengenesis::core::MarketplaceStore market(
                (root / "market.db").string());
            const auto listing = market.find(listing_id);
            require(
                listing &&
                    listing->state ==
                        opengenesis::core::MarketplaceListingState::sold &&
                    listing->buyer_user_id == "charlie",
                "market sale survives restart");
        }

        {
            opengenesis::core::AssetStore assets(
                (root / "assets.db").string(),
                root / "asset-blobs");
            const auto asset = assets.create(
                "alice", "Compensated Asset",
                "application/octet-stream", "payload", reason);
            require(asset.has_value(), "asset created");
            require(
                !assets.remove_owned(asset->id, "bob"),
                "asset compensation enforces owner");
            require(
                assets.remove_owned(asset->id, "alice"),
                "asset compensation removes owner asset");
            require(
                !assets.find(asset->id).has_value(),
                "removed asset metadata is gone");
        }

        std::filesystem::remove_all(root);
        std::cout
            << "OpenGenesisLINK 15.0 platform services tests: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr
            << "OpenGenesisLINK 15.0 platform services test failure: "
            << error.what() << '\n';
        return 1;
    }
}
