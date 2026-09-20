HttpResponse marketplace_purchase_response(
    const UserInfo& buyer,
    const std::string_view listing_id,
    AssetStore& assets,
    InventoryStore& inventory,
    SocialPolicyStore& social_policies,
    EconomyLedger& economy,
    MarketplaceStore& marketplace,
    AuditStore& audit,
    NotificationStore& notifications) {
    const auto listing = marketplace.find(listing_id);
    if (!listing) {
        return {.status = 404, .body = "{\"error\":\"listing-not-found\"}"};
    }
    if (listing->state != MarketplaceListingState::active) {
        return {.status = 409, .body = "{\"error\":\"listing-not-active\"}"};
    }
    if (listing->seller_user_id == buyer.id) {
        return {.status = 409, .body = "{\"error\":\"cannot-buy-own-listing\"}"};
    }
    if (listing->currency_code != economy.currency_code()) {
        return {.status = 409, .body = "{\"error\":\"listing-currency-mismatch\"}"};
    }
    if (social_policies.blocks(buyer.id, listing->seller_user_id) ||
        social_policies.blocks(listing->seller_user_id, buyer.id)) {
        return {.status = 403, .body = "{\"error\":\"social-policy-blocked\"}"};
    }

    const auto source_asset = assets.find(listing->asset_id);
    const auto required =
        static_cast<PermissionMask>(perm_copy | perm_transfer);
    if (!source_asset ||
        source_asset->owner_user_id != listing->seller_user_id ||
        !has_permission(source_asset->permissions, required)) {
        return {.status = 409, .body = "{\"error\":\"listing-asset-unavailable\"}"};
    }

    const auto sale_reference =
        "market:" + listing->id + ":" + security::random_hex(12);
    std::string reason;
    if (!marketplace.reserve_purchase(
            listing->id, buyer.id, sale_reference, reason)) {
        return {.status = 409,
                .body = "{\"error\":\"" + json_escape(reason) + "\"}"};
    }

    const auto release_listing = [&]() {
        std::string ignored;
        (void)marketplace.rollback_purchase(
            listing->id, buyer.id, sale_reference, ignored);
    };

    const auto escrow = economy.reserve(
        buyer.id, listing->seller_user_id,
        listing->price_minor, sale_reference, reason);
    if (!escrow) {
        release_listing();
        return {.status = 409,
                .body = "{\"error\":\"" + json_escape(reason) + "\"}"};
    }

    const auto release_escrow = [&]() {
        std::string ignored;
        (void)economy.release_escrow(escrow->id, ignored);
    };

    const auto delivered = assets.transfer(
        listing->asset_id, listing->seller_user_id,
        buyer.id, true, reason);
    if (!delivered) {
        release_escrow();
        release_listing();
        return {.status = 409,
                .body = "{\"error\":\"" + json_escape(reason) + "\"}"};
    }

    const auto root = inventory.ensure_root(buyer.id);
    const auto item = inventory.create_item(
        buyer.id, root.id, delivered->id,
        delivered->name, reason);
    if (!item) {
        (void)assets.remove_owned(delivered->id, buyer.id);
        release_escrow();
        release_listing();
        return {.status = 503,
                .body = "{\"error\":\"marketplace-fulfillment-failed\"}"};
    }

    const auto rollback_fulfillment = [&]() {
        std::string ignored;
        (void)inventory.delete_item(buyer.id, item->id, ignored);
        (void)assets.remove_owned(delivered->id, buyer.id);
        release_escrow();
        release_listing();
    };

    if (!marketplace.complete_purchase(
            listing->id, buyer.id, sale_reference, reason)) {
        rollback_fulfillment();
        return {.status = 503,
                .body = "{\"error\":\"marketplace-finalize-failed\"}"};
    }

    if (!economy.commit_escrow(escrow->id, reason)) {
        rollback_fulfillment();
        return {.status = 503,
                .body = "{\"error\":\"marketplace-payment-finalize-failed\"}"};
    }

    audit.append(
        buyer.id, "marketplace.purchase",
        listing->id, sale_reference);
    notifications.push(
        listing->seller_user_id,
        "marketplace_sale",
        "Marketplace sale",
        "An item was purchased from your listing.",
        listing->id);

    return {
        .status = 201,
        .body =
            "{\"status\":\"purchased\",\"listing_id\":\"" +
            json_escape(listing->id) +
            "\",\"asset\":" + asset_json(*delivered) +
            ",\"inventory_item_id\":\"" +
            json_escape(item->id) +
            "\",\"escrow_id\":\"" +
            json_escape(escrow->id) +
            "\",\"sale_reference\":\"" +
            json_escape(sale_reference) + "\"}"};
}
