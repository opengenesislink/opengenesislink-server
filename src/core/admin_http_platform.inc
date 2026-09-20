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


bool is_platform_service_path(const std::string_view path) {
    return path == "/v1/social/policies" ||
           path == "/v1/social/block" ||
           path == "/v1/social/mute" ||
           path == "/v1/groups/invites" ||
           path == "/v1/groups/invites/accept" ||
           path == "/v1/groups/invites/revoke" ||
           (path.starts_with("/v1/parcels/") &&
            path.ends_with("/access")) ||
           path == "/v1/parcels/access" ||
           path == "/v1/parcels/access/remove" ||
           path.starts_with("/v1/economy/") ||
           path.starts_with("/v1/marketplace/");
}

HttpResponse platform_service_response(
    const HttpRequest& request,
    SessionStore& sessions,
    IdentityStore& identities,
    SocialPolicyStore& social_policies,
    FriendsStore& friends,
    GroupStore& groups,
    ParcelStore& parcels,
    EconomyLedger& economy,
    MarketplaceStore& marketplace,
    AssetStore& assets,
    InventoryStore& inventory,
    NotificationStore& notifications,
    AuditStore& audit,
    AdminRoleStore& admin_roles,
    const std::string_view admin_api_key) {
    const auto auth = [&]()
        -> std::pair<std::optional<AuthSession>, std::optional<UserInfo>> {
        const auto session = sessions.find(bearer_token(request));
        const auto user =
            session ? identities.find_by_id(session->user_id)
                    : std::nullopt;
        return {session, user};
    };
    const auto admin_allowed =
        [&](const std::string_view minimum_role) {
            if (admin_authorized(request, admin_api_key)) return true;
            const auto [session, user] = auth();
            return session && user &&
                   admin_roles.has_role(user->id, minimum_role);
        };
    const auto admin_actor = [&]() {
        if (admin_authorized(request, admin_api_key)) {
            return std::string{"admin-api-key"};
        }
        const auto [session, user] = auth();
        return session && user
                   ? user->id
                   : std::string{"unknown-admin"};
    };

    const auto& method = request.method;
    const auto& path = request.path;

    if (path == "/v1/social/policies") {
        if (method != "GET") {
            return {.status = 405, .body = "{\"error\":\"method-not-allowed\"}"};
        }
        auto [session, user] = auth();
        if (!session || !user) {
            return {.status = 401, .body = "{\"error\":\"invalid-session\"}"};
        }
        return {.body = "{\"policies\":" +
                        social_policies_json(
                            social_policies.list_for_user(user->id)) + "}"};
    }

    if (path == "/v1/social/block" || path == "/v1/social/mute") {
        if (method != "POST") {
            return {.status = 405, .body = "{\"error\":\"method-not-allowed\"}"};
        }
        auto [session, user] = auth();
        const auto target = json_string(request.body, "user_id");
        const auto value = json_bool(
            request.body, path == "/v1/social/block" ? "blocked" : "muted");
        if (!session || !user) {
            return {.status = 401, .body = "{\"error\":\"invalid-session\"}"};
        }
        if (!target || !value.has_value() ||
            !identities.find_by_id(*target)) {
            return {.status = 400, .body = "{\"error\":\"invalid-social-policy\"}"};
        }

        std::string reason;
        const bool ok = path == "/v1/social/block"
                            ? social_policies.set_blocked(
                                  user->id, *target, *value, reason)
                            : social_policies.set_muted(
                                  user->id, *target, *value, reason);
        if (!ok) {
            return {.status = 400,
                    .body = "{\"error\":\"" +
                            json_escape(reason) + "\"}"};
        }
        if (path == "/v1/social/block" && *value) {
            (void)friends.remove(user->id, *target);
        }
        audit.append(
            user->id,
            path == "/v1/social/block" ? "social.block" : "social.mute",
            *target,
            *value ? (path == "/v1/social/block" ? "blocked" : "muted")
                   : (path == "/v1/social/block" ? "unblocked" : "unmuted"));
        return {.body = "{\"status\":\"updated\"}"};
    }

    if (path == "/v1/groups/invites") {
        auto [session, user] = auth();
        if (!session || !user) {
            return {.status = 401, .body = "{\"error\":\"invalid-session\"}"};
        }
        if (method == "GET") {
            return {.body = "{\"invites\":" +
                            group_invites_json(
                                groups.invites_for_user(user->id)) + "}"};
        }
        if (method != "POST") {
            return {.status = 405, .body = "{\"error\":\"method-not-allowed\"}"};
        }

        const auto group_id = json_string(request.body, "group_id");
        const auto invited_user = json_string(request.body, "user_id");
        const auto role =
            json_string(request.body, "role").value_or("member");
        const auto lifetime = static_cast<std::int64_t>(
            std::clamp(
                json_number(request.body, "lifetime_seconds")
                    .value_or(604800.0),
                60.0, 2592000.0));
        if (!group_id || !invited_user ||
            !identities.find_by_id(*invited_user)) {
            return {.status = 400,
                    .body = "{\"error\":\"invalid-group-invite\"}"};
        }
        if (social_policies.blocks(*invited_user, user->id) ||
            social_policies.blocks(user->id, *invited_user)) {
            return {.status = 403,
                    .body = "{\"error\":\"social-policy-blocked\"}"};
        }

        std::string reason;
        const auto invite = groups.invite(
            user->id, *group_id, *invited_user, role,
            std::chrono::seconds{lifetime}, reason);
        if (!invite) {
            return {.status = 403,
                    .body = "{\"error\":\"" +
                            json_escape(reason) + "\"}"};
        }
        notifications.push(
            *invited_user, "group_invite", "Group invitation",
            user->display_name + " invited you to a group.",
            invite->id);
        audit.append(
            user->id, "group.invite", *group_id,
            invite->id + ":" + *invited_user);
        return {.status = 201,
                .body = "{\"invite\":" +
                        group_invites_json({*invite}) + "}"};
    }

    if (path == "/v1/groups/invites/accept" ||
        path == "/v1/groups/invites/revoke") {
        if (method != "POST") {
            return {.status = 405, .body = "{\"error\":\"method-not-allowed\"}"};
        }
        auto [session, user] = auth();
        const auto invite_id = json_string(request.body, "invite_id");
        if (!session || !user) {
            return {.status = 401, .body = "{\"error\":\"invalid-session\"}"};
        }
        if (!invite_id) {
            return {.status = 400, .body = "{\"error\":\"missing-invite-id\"}"};
        }

        std::string reason;
        if (path.ends_with("/accept")) {
            const auto member =
                groups.accept_invite(user->id, *invite_id, reason);
            if (!member) {
                return {.status = 409,
                        .body = "{\"error\":\"" +
                                json_escape(reason) + "\"}"};
            }
            audit.append(
                user->id, "group.invite.accept",
                member->group_id, *invite_id);
            return {.body = "{\"status\":\"accepted\",\"group_id\":\"" +
                            json_escape(member->group_id) + "\"}"};
        }

        if (!groups.revoke_invite(user->id, *invite_id, reason)) {
            return {.status = 403,
                    .body = "{\"error\":\"" +
                            json_escape(reason) + "\"}"};
        }
        audit.append(
            user->id, "group.invite.revoke", *invite_id);
        return {.body = "{\"status\":\"revoked\"}"};
    }

    if (path.starts_with("/v1/parcels/") &&
        path.ends_with("/access") &&
        path != "/v1/parcels/access") {
        if (method != "GET") {
            return {.status = 405, .body = "{\"error\":\"method-not-allowed\"}"};
        }
        auto [session, user] = auth();
        constexpr std::string_view prefix = "/v1/parcels/";
        constexpr std::string_view suffix = "/access";
        const auto parcel_id = path.substr(
            prefix.size(),
            path.size() - prefix.size() - suffix.size());
        const auto parcel = parcels.find(parcel_id);
        if (!session || !user) {
            return {.status = 401, .body = "{\"error\":\"invalid-session\"}"};
        }
        if (!parcel) {
            return {.status = 404, .body = "{\"error\":\"parcel-not-found\"}"};
        }
        if (parcel->owner_user_id != user->id) {
            return {.status = 403, .body = "{\"error\":\"permission-denied\"}"};
        }
        return {.body = "{\"parcel_id\":\"" + json_escape(parcel_id) +
                        "\",\"access\":" +
                        parcel_access_json(
                            parcels.access_list(parcel_id)) + "}"};
    }

    if (path == "/v1/parcels/access" ||
        path == "/v1/parcels/access/remove") {
        if (method != "POST") {
            return {.status = 405, .body = "{\"error\":\"method-not-allowed\"}"};
        }
        auto [session, user] = auth();
        const auto parcel_id = json_string(request.body, "parcel_id");
        const auto target_user = json_string(request.body, "user_id");
        if (!session || !user) {
            return {.status = 401, .body = "{\"error\":\"invalid-session\"}"};
        }
        if (!parcel_id || !target_user) {
            return {.status = 400,
                    .body = "{\"error\":\"invalid-parcel-access\"}"};
        }

        std::string reason;
        if (path.ends_with("/remove")) {
            if (!parcels.remove_access(
                    user->id, *parcel_id, *target_user, reason)) {
                return {.status = 404,
                        .body = "{\"error\":\"" +
                                json_escape(reason) + "\"}"};
            }
            audit.append(
                user->id, "parcel.access.remove",
                *parcel_id, *target_user);
            return {.body = "{\"status\":\"removed\"}"};
        }

        const auto allowed = json_bool(request.body, "allowed");
        if (!allowed.has_value() ||
            !identities.find_by_id(*target_user)) {
            return {.status = 400,
                    .body = "{\"error\":\"invalid-parcel-access\"}"};
        }
        if (!parcels.set_access(
                user->id, *parcel_id, *target_user, *allowed, reason)) {
            return {.status = 403,
                    .body = "{\"error\":\"" +
                            json_escape(reason) + "\"}"};
        }
        audit.append(
            user->id, "parcel.access.set", *parcel_id,
            *target_user + ":" + (*allowed ? "allow" : "deny"));
        return {.body = "{\"status\":\"updated\"}"};
    }

    if (path == "/v1/economy/info") {
        if (method != "GET") {
            return {.status = 405, .body = "{\"error\":\"method-not-allowed\"}"};
        }
        return {.body =
                    "{\"currency\":\"" + json_escape(economy.currency_code()) +
                    "\",\"accounts\":" +
                    std::to_string(economy.account_count()) +
                    ",\"entries\":" +
                    std::to_string(economy.entry_count()) +
                    ",\"total_supply_minor\":" +
                    std::to_string(economy.total_supply_minor()) + "}"};
    }

    if (path == "/v1/economy/wallet" ||
        path == "/v1/economy/ledger" ||
        path == "/v1/economy/escrows") {
        if (method != "GET") {
            return {.status = 405, .body = "{\"error\":\"method-not-allowed\"}"};
        }
        auto [session, user] = auth();
        if (!session || !user) {
            return {.status = 401, .body = "{\"error\":\"invalid-session\"}"};
        }
        if (path.ends_with("/wallet")) {
            return {.body = economy_account_json(
                        economy.ensure_account(user->id),
                        economy.currency_code())};
        }
        if (path.ends_with("/ledger")) {
            return {.body =
                        "{\"currency\":\"" +
                        json_escape(economy.currency_code()) +
                        "\",\"entries\":" +
                        economy_entries_json(
                            economy.entries_for(user->id)) + "}"};
        }
        return {.body = "{\"escrows\":" +
                        economy_escrows_json(
                            economy.escrows_for(user->id)) + "}"};
    }

    if (path == "/v1/economy/transfer") {
        if (method != "POST") {
            return {.status = 405, .body = "{\"error\":\"method-not-allowed\"}"};
        }
        auto [session, user] = auth();
        const auto recipient = json_string(request.body, "recipient_id");
        const auto amount = json_u64(request.body, "amount_minor");
        const auto reference =
            json_string(request.body, "reference")
                .value_or("transfer:" + security::random_hex(16));
        const auto memo =
            json_string(request.body, "memo").value_or("");
        if (!session || !user) {
            return {.status = 401, .body = "{\"error\":\"invalid-session\"}"};
        }
        if (!recipient || !amount || *amount == 0U ||
            *amount > 9000000000000000ULL ||
            !identities.find_by_id(*recipient)) {
            return {.status = 400,
                    .body = "{\"error\":\"invalid-economy-transfer\"}"};
        }
        if (social_policies.blocks(user->id, *recipient) ||
            social_policies.blocks(*recipient, user->id)) {
            return {.status = 403,
                    .body = "{\"error\":\"social-policy-blocked\"}"};
        }

        std::string reason;
        const auto entry = economy.transfer(
            user->id, *recipient,
            static_cast<std::int64_t>(*amount),
            reference, memo, reason);
        if (!entry) {
            return {.status = 409,
                    .body = "{\"error\":\"" +
                            json_escape(reason) + "\"}"};
        }
        audit.append(
            user->id, "economy.transfer", entry->id,
            *recipient + ":" + std::to_string(entry->amount_minor));
        notifications.push(
            *recipient, "economy_transfer", "Wallet credit",
            user->display_name + " transferred funds to you.",
            entry->id);
        return {.status = 201,
                .body = "{\"entry\":" +
                        economy_entries_json({*entry}) + "}"};
    }

    if (path == "/v1/economy/admin/mint" ||
        path == "/v1/economy/admin/burn") {
        if (method != "POST") {
            return {.status = 405, .body = "{\"error\":\"method-not-allowed\"}"};
        }
        if (!admin_allowed("administrator")) {
            return {.status = 403,
                    .body = "{\"error\":\"admin-authorization-required\"}"};
        }
        const auto target = json_string(request.body, "user_id");
        const auto amount = json_u64(request.body, "amount_minor");
        const auto reference = json_string(request.body, "reference");
        const auto is_mint = path.ends_with("/mint");
        const auto memo = json_string(request.body, "memo").value_or(
            is_mint ? "operator mint" : "operator burn");
        if (!target || !amount || !reference ||
            *amount == 0U || *amount > 9000000000000000ULL ||
            !identities.find_by_id(*target)) {
            return {.status = 400,
                    .body = "{\"error\":\"invalid-economy-admin-fields\"}"};
        }

        std::string reason;
        const auto entry =
            is_mint
                ? economy.mint(
                      *target, static_cast<std::int64_t>(*amount),
                      *reference, memo, reason)
                : economy.burn(
                      *target, static_cast<std::int64_t>(*amount),
                      *reference, memo, reason);
        if (!entry) {
            return {.status = 409,
                    .body = "{\"error\":\"" +
                            json_escape(reason) + "\"}"};
        }
        audit.append(
            admin_actor(),
            is_mint ? "economy.mint" : "economy.burn",
            *target,
            std::to_string(entry->amount_minor) +
                ":" + entry->reference);
        return {.status = 201,
                .body = "{\"entry\":" +
                        economy_entries_json({*entry}) + "}"};
    }

    if (path == "/v1/marketplace/listings") {
        if (method == "GET") {
            return {.body =
                        "{\"currency\":\"" +
                        json_escape(economy.currency_code()) +
                        "\",\"listings\":" +
                        marketplace_listings_json(
                            marketplace.active()) + "}"};
        }
        if (method != "POST") {
            return {.status = 405, .body = "{\"error\":\"method-not-allowed\"}"};
        }
        auto [session, user] = auth();
        const auto asset_id = json_string(request.body, "asset_id");
        const auto title = json_string(request.body, "title");
        const auto description =
            json_string(request.body, "description").value_or("");
        const auto price = json_u64(request.body, "price_minor");
        if (!session || !user) {
            return {.status = 401, .body = "{\"error\":\"invalid-session\"}"};
        }
        if (!asset_id || !title || !price || *price == 0U ||
            *price > 9000000000000000ULL) {
            return {.status = 400,
                    .body = "{\"error\":\"invalid-marketplace-listing\"}"};
        }

        const auto asset = assets.find(*asset_id);
        const auto required =
            static_cast<PermissionMask>(perm_copy | perm_transfer);
        if (!asset || asset->owner_user_id != user->id) {
            return {.status = 404, .body = "{\"error\":\"asset-not-owned\"}"};
        }
        if (!has_permission(asset->permissions, required)) {
            return {.status = 403, .body = "{\"error\":\"asset-not-saleable\"}"};
        }

        std::string reason;
        const auto listing = marketplace.create(
            user->id, *asset_id, *title, description,
            static_cast<std::int64_t>(*price),
            economy.currency_code(), reason);
        if (!listing) {
            return {.status = 400,
                    .body = "{\"error\":\"" +
                            json_escape(reason) + "\"}"};
        }
        audit.append(
            user->id, "marketplace.list", listing->id,
            listing->asset_id + ":" +
                std::to_string(listing->price_minor));
        return {.status = 201,
                .body = marketplace_listing_json(*listing)};
    }

    if (path == "/v1/marketplace/mine") {
        if (method != "GET") {
            return {.status = 405, .body = "{\"error\":\"method-not-allowed\"}"};
        }
        auto [session, user] = auth();
        if (!session || !user) {
            return {.status = 401, .body = "{\"error\":\"invalid-session\"}"};
        }
        return {.body = "{\"listings\":" +
                        marketplace_listings_json(
                            marketplace.for_seller(user->id)) + "}"};
    }

    if (path == "/v1/marketplace/cancel") {
        if (method != "POST") {
            return {.status = 405, .body = "{\"error\":\"method-not-allowed\"}"};
        }
        auto [session, user] = auth();
        const auto listing_id = json_string(request.body, "listing_id");
        if (!session || !user) {
            return {.status = 401, .body = "{\"error\":\"invalid-session\"}"};
        }
        if (!listing_id) {
            return {.status = 400, .body = "{\"error\":\"missing-listing-id\"}"};
        }
        std::string reason;
        if (!marketplace.cancel(user->id, *listing_id, reason)) {
            return {.status = 409,
                    .body = "{\"error\":\"" +
                            json_escape(reason) + "\"}"};
        }
        audit.append(user->id, "marketplace.cancel", *listing_id);
        return {.body = "{\"status\":\"cancelled\"}"};
    }

    if (path == "/v1/marketplace/purchase") {
        if (method != "POST") {
            return {.status = 405, .body = "{\"error\":\"method-not-allowed\"}"};
        }
        auto [session, user] = auth();
        const auto listing_id = json_string(request.body, "listing_id");
        if (!session || !user) {
            return {.status = 401, .body = "{\"error\":\"invalid-session\"}"};
        }
        if (!listing_id) {
            return {.status = 400, .body = "{\"error\":\"missing-listing-id\"}"};
        }
        return marketplace_purchase_response(
            *user, *listing_id, assets, inventory,
            social_policies, economy, marketplace,
            audit, notifications);
    }

    return {.status = 404, .body = "{\"error\":\"not-found\"}"};
}
