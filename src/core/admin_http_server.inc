AdminHttpServer::AdminHttpServer(std::string address, const std::uint16_t port,
                                 std::shared_ptr<WorldRegistry> worlds,
                                 std::shared_ptr<RegionRegistry> regions,
                                 std::shared_ptr<IdentityStore> identities,
                                 std::shared_ptr<SessionStore> sessions,
                                 std::shared_ptr<AssetStore> assets,
                                 std::shared_ptr<avatar::AppearanceStore> appearance,
                                 std::shared_ptr<InventoryStore> inventory,
                                 std::shared_ptr<PresenceStore> presences,
                                 std::shared_ptr<FriendsStore> friends,
                                 std::shared_ptr<MessageStore> messages,
                                 std::shared_ptr<GroupStore> groups,
                                 std::shared_ptr<ParcelStore> parcels,
                                 std::shared_ptr<ModerationStore> moderation,
                                 std::shared_ptr<AuditStore> audit,
                                 std::shared_ptr<EstateStore> estates,
                                 std::shared_ptr<LandmarkStore> landmarks,
                                 std::shared_ptr<NotificationStore> notifications,
                                 std::shared_ptr<GroupChannelStore> group_channels,
                                 std::shared_ptr<CrossingStore> crossings,
                                 std::shared_ptr<ObjectCrossingStore> object_crossings,
                                 std::shared_ptr<scripting::ScriptRuntime> scripts,
                                 std::shared_ptr<scripting::ScriptHost> script_host,
                                 std::shared_ptr<federation::FederationRuntime> federation_runtime,
                                 std::shared_ptr<compat::hypergrid::HypergridService> hypergrid_service,
                                 std::shared_ptr<compat::hypergrid::HypergridSessionStore> hypergrid_sessions,
                                 std::shared_ptr<compat::hypergrid::HypergridInstantMessageAdapter> hypergrid_im,
                                 std::shared_ptr<storage::DatabasePool> database,
                                 std::shared_ptr<AdminRoleStore> admin_roles,
                                 std::string admin_api_key,
                                 std::string scene_ticket_secret,
                                 std::chrono::seconds scene_ticket_lifetime,
                                 const std::size_t login_attempts_per_minute,
                                 const std::size_t registration_attempts_per_minute)
    : address_(std::move(address)), port_(port), worlds_(std::move(worlds)), regions_(std::move(regions)),
      identities_(std::move(identities)), sessions_(std::move(sessions)), assets_(std::move(assets)),
      appearance_(std::move(appearance)), inventory_(std::move(inventory)), presences_(std::move(presences)), friends_(std::move(friends)),
      messages_(std::move(messages)), groups_(std::move(groups)), parcels_(std::move(parcels)),
      moderation_(std::move(moderation)), audit_(std::move(audit)), estates_(std::move(estates)),
      landmarks_(std::move(landmarks)), notifications_(std::move(notifications)),
      group_channels_(std::move(group_channels)), crossings_(std::move(crossings)),
      object_crossings_(std::move(object_crossings)), scripts_(std::move(scripts)), script_host_(std::move(script_host)),
      federation_runtime_(std::move(federation_runtime)),
      hypergrid_service_(std::move(hypergrid_service)), hypergrid_sessions_(std::move(hypergrid_sessions)),
      hypergrid_im_(std::move(hypergrid_im)), database_(std::move(database)),
      admin_roles_(std::move(admin_roles)),
      login_attempts_per_minute_(login_attempts_per_minute),
      registration_attempts_per_minute_(registration_attempts_per_minute),
      admin_api_key_(std::move(admin_api_key)),
      scene_ticket_secret_(std::move(scene_ticket_secret)),
      scene_ticket_lifetime_(scene_ticket_lifetime) {
    if (!admin_roles_) {
        throw std::invalid_argument("admin role store is required");
    }
}

AdminHttpServer::~AdminHttpServer() { stop(); }

void AdminHttpServer::start() {
    if (running_.exchange(true)) return;
    started_at_ = std::chrono::steady_clock::now();
    thread_ = std::thread(&AdminHttpServer::run, this);
}

void AdminHttpServer::stop() {
    if (!running_.exchange(false)) return;
    if (platform::socket_valid(listen_fd_)) {
        platform::shutdown_socket(listen_fd_);
        platform::close_socket(listen_fd_);
    }
    if (thread_.joinable()) thread_.join();
}

void AdminHttpServer::run() {
    try {
        platform::initialize_sockets();
        addrinfo hints{};
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_family = AF_UNSPEC;
        hints.ai_flags = AI_PASSIVE;
        addrinfo* results = nullptr;
        const auto port_text = std::to_string(port_);
        if (::getaddrinfo(address_.c_str(), port_text.c_str(), &hints, &results) != 0) {
            throw std::runtime_error("admin getaddrinfo failed");
        }
        for (auto* current = results; current; current = current->ai_next) {
            listen_fd_ = ::socket(current->ai_family, current->ai_socktype, current->ai_protocol);
            if (!platform::socket_valid(listen_fd_)) continue;
            platform::set_reuse_address(listen_fd_);
            if (::bind(listen_fd_, current->ai_addr, static_cast<platform::SocketLength>(current->ai_addrlen)) == 0 &&
                ::listen(listen_fd_, 64) == 0) {
                break;
            }
            platform::close_socket(listen_fd_);
        }
        ::freeaddrinfo(results);
        if (!platform::socket_valid(listen_fd_)) throw std::runtime_error("admin bind failed");

        common::log(common::LogLevel::info, "core.admin",
                    "Web/API interface listening on http://" + address_ + ':' + port_text);

        while (running_) {
            auto fd = ::accept(listen_fd_, nullptr, nullptr);
            if (!platform::socket_valid(fd)) {
                if (!running_) break;
                const int error = platform::last_socket_error();
                if (platform::socket_error_interrupted(error)) continue;
                continue;
            }
            try {
                const auto request = read_request(fd);
                if (!request) {
                    send_response(fd, {.status = 400, .body = "{\"error\":\"bad-request\"}"});
                    platform::close_socket(fd);
                    continue;
                }

                HttpResponse response;
                const auto& method = request->method;
                const auto& path = request->path;
                const auto auth = [&]() -> std::pair<std::optional<AuthSession>, std::optional<UserInfo>> {
                    const auto session = sessions_->find(bearer_token(*request));
                    const auto user = session ? identities_->find_by_id(session->user_id) : std::nullopt;
                    return {session, user};
                };
                const auto admin_allowed =
                    [&](const std::string_view minimum_role) {
                        if (admin_authorized(*request, admin_api_key_)) {
                            return true;
                        }
                        const auto [session, user] = auth();
                        return session && user &&
                               admin_roles_->has_role(
                                   user->id, minimum_role);
                    };
                const auto admin_actor = [&]() {
                    if (admin_authorized(*request, admin_api_key_)) {
                        return std::string{"admin-api-key"};
                    }
                    const auto [session, user] = auth();
                    return session && user
                               ? user->id
                               : std::string{"unknown-admin"};
                };
                const auto estate_entry_error = [&](const UserInfo& user, const RegionInfo& region) -> std::string {
                    const auto policy = estates_->policy_for_region(region.id);
                    if (!policy) return {};
                    if (!policy->public_access && !estates_->can_manage(policy->estate_id, user.id)) {
                        return "estate-entry-denied";
                    }
                    const auto current = presences_->find_user(user.id);
                    const bool already_here = current && current->region_id == region.id;
                    if (!already_here && presences_->count_region(region.id) >= policy->max_agents) {
                        return "region-capacity-reached";
                    }
                    return {};
                };
                const auto object_estate_error =
                    [&](const UserInfo& user,
                        const RegionInfo& region) -> std::string {
                    const auto policy =
                        estates_->policy_for_region(region.id);
                    if (!policy) return {};
                    if (!policy->public_access &&
                        !estates_->can_manage(
                            policy->estate_id, user.id)) {
                        return "estate-object-entry-denied";
                    }
                    return {};
                };
                const auto viewer_response = [&](const UserInfo& user, const RegionInfo& region,
                                                 const WorldNodeInfo& world,
                                                 std::string handoff_from = {},
                                                 double spawn_x = 128.0, double spawn_y = 128.0,
                                                 double spawn_z = 0.0,
                                                 std::string crossing_id = {}) {
                    const auto group_ids = groups_->group_ids_for_user(user.id);
                    std::string groups_csv;
                    for (std::size_t i = 0; i < group_ids.size(); ++i) {
                        if (i) groups_csv += ',';
                        groups_csv += group_ids[i];
                    }
                    const auto ticket = security::issue_scene_ticket(
                        scene_ticket_secret_, user.id, user.display_name, region.id,
                        scene_ticket_lifetime_, std::string{security::kDefaultSceneCapabilities},
                        std::move(handoff_from), groups_csv, spawn_x, spawn_y, spawn_z,
                        std::move(crossing_id));
                    return "{\"user\":{\"id\":\"" + json_escape(user.id) +
                           "\",\"display_name\":\"" + json_escape(user.display_name) +
                           "\"},\"region\":{\"id\":\"" + json_escape(region.id) +
                           "\",\"name\":\"" + json_escape(region.name) +
                           "\"},\"scene_endpoint\":\"" + json_escape(world.endpoint) +
                           "\",\"scene_ticket\":\"" + ticket.token +
                           "\",\"capabilities\":\"" + json_escape(ticket.claims.capabilities) +
                           "\",\"handoff_from\":\"" + json_escape(ticket.claims.handoff_from_region) +
                           "\",\"crossing_id\":\"" + json_escape(ticket.claims.crossing_id) +
                           "\",\"groups\":\"" + json_escape(ticket.claims.group_ids_csv) +
                           "\",\"spawn\":{\"x\":" + std::to_string(ticket.claims.spawn_x) +
                           ",\"y\":" + std::to_string(ticket.claims.spawn_y) +
                           ",\"z\":" + std::to_string(ticket.claims.spawn_z) +
                           "},\"expires_unix\":" + std::to_string(ticket.claims.expires_unix) + "}";
                };

                const auto storage_status_json = [&]() {
                    if (!database_) {
                        return std::string{
                            "{\"backend\":\"file\",\"ready\":true,"
                            "\"pool_size\":0,\"successful_operations\":0,"
                            "\"failed_operations\":0,\"last_error\":null}"};
                    }
                    const auto ready = database_->ping();
                    const auto health = database_->health();
                    return "{\"backend\":\"" +
                           json_escape(health.backend) +
                           "\",\"ready\":" +
                           std::string(ready ? "true" : "false") +
                           ",\"pool_size\":" +
                           std::to_string(health.pool_size) +
                           ",\"successful_operations\":" +
                           std::to_string(health.successful_operations) +
                           ",\"failed_operations\":" +
                           std::to_string(health.failed_operations) +
                           ",\"last_error\":" +
                           (health.last_error.empty()
                                ? std::string{"null"}
                                : "\"" +
                                      json_escape(health.last_error) +
                                      "\"") +
                           "}";
                };

                if (method == "GET" && path == "/") {
                    response.content_type = "text/html; charset=utf-8";
                    response.body = dashboard_html();
                } else if (method == "GET" && path == "/health") {
                    const auto storage_json = storage_status_json();
                    const auto database_ready =
                        !database_ || database_->health().ready;
                    response.status = database_ready ? 200 : 503;
                    response.body =
                        "{\"status\":\"" +
                        std::string(database_ready ? "ok" : "degraded") +
                        "\",\"version\":\"" +
                        std::string(OGL_VERSION) +
                        "\",\"storage\":" + storage_json + "}";
                } else if (method == "GET" &&
                           path == "/v1/storage/status") {
                    response.body = storage_status_json();
                } else if (method == "GET" && path == "/metrics") {
                    const auto uptime = std::chrono::duration_cast<std::chrono::seconds>(
                        std::chrono::steady_clock::now() - started_at_).count();
                    std::ostringstream out;
                    out << "# HELP opengenesis_core_uptime_seconds Core process uptime.\n"
                        << "# TYPE opengenesis_core_uptime_seconds gauge\n"
                        << "opengenesis_core_uptime_seconds " << uptime << "\n"
                        << "opengenesis_world_nodes " << worlds_->list().size() << "\n"
                        << "opengenesis_regions " << regions_->list().size() << "\n"
                        << "opengenesis_online_presences " << presences_->count() << "\n"
                        << "opengenesis_active_sessions " << sessions_->active_count() << "\n"
                        << "opengenesis_groups " << groups_->count() << "\n"
                        << "opengenesis_parcels " << parcels_->count() << "\n"
                        << "opengenesis_estates " << estates_->count() << "\n"
                        << "opengenesis_landmarks " << landmarks_->count() << "\n"
                        << "opengenesis_notifications " << notifications_->count() << "\n"
                        << "opengenesis_group_posts " << group_channels_->count() << "\n"
                        << "opengenesis_assets " << assets_->count() << "\n"
                        << "opengenesis_federation_peers " << federation_runtime_->peers().size() << "\n"
                        << "opengenesis_foreign_sessions " << federation_runtime_->sessions().size() << "\n"
                        << "opengenesis_hypergrid_home_sessions " << hypergrid_sessions_->home_sessions().size() << "\n"
                        << "opengenesis_hypergrid_foreign_sessions " << hypergrid_sessions_->foreign_sessions().size() << "\n"
                        << "opengenesis_crossings " << crossings_->list().size() << "\n"
                        << "opengenesis_object_crossings "
                        << object_crossings_->list().size() << "\n"
                        << "opengenesis_scripts " << scripts_->list().size() << "\n";
                    if (database_) {
                        const auto database_health = database_->health();
                        out << "opengenesis_database_ready "
                            << (database_health.ready ? 1 : 0) << "\n"
                            << "opengenesis_database_pool_size "
                            << database_health.pool_size << "\n"
                            << "opengenesis_database_operations_total{result=\"success\"} "
                            << database_health.successful_operations << "\n"
                            << "opengenesis_database_operations_total{result=\"failure\"} "
                            << database_health.failed_operations << "\n";
                    } else {
                        out << "opengenesis_database_ready 1\n"
                            << "opengenesis_database_pool_size 0\n";
                    }
                    for (const auto& region : regions_->list()) {
                        out << "opengenesis_region_sim_fps{region=\"" << json_escape(region.id) << "\"} " << region.sim_fps << "\n"
                            << "opengenesis_region_entities{region=\"" << json_escape(region.id) << "\"} " << region.entities << "\n"
                            << "opengenesis_region_avatars{region=\"" << json_escape(region.id) << "\"} " << region.avatars << "\n";
                    }
                    response.content_type = "text/plain; version=0.0.4; charset=utf-8";
                    response.body = out.str();
                } else if (method == "GET" && path == "/v1") {
                    response.body = R"JSON({"service":"OpenGenesisLINK Core","version":")JSON" +
                                    std::string(OGL_VERSION) +
                                    R"JSON(","api_version":1,"capabilities":["identity","scene-ticket-v2","scene-capabilities-v1","avatar-movement-v1","presence-v1","friends-v1","messaging-v1","groups-v1","land-parcels-v1","object-permissions-v1","asset-permissions-v1","teleport-v1","region-handoff-v2","moderation-v1","audit-v1","estates-v1","landmarks-v1","notifications-v1","group-channels-v1","prometheus-metrics-v1","sql-storage-v1","sqlite-storage-v1","postgresql-storage-v1","mariadb-storage-v1","storage-health-v1","admin-rbac-v1","auth-rate-limit-v1","assets-v2","inventory-v1","world-status","ogl-fed-v1","hypergrid-session-v1","hypergrid-xinventory-v2","hypergrid-xinventory-auth-v1","script-vm-v1","script-host-v1","script-engine-v2","script-engine-v3","script-language-lsl-v1","script-language-ogl-v1","script-language-ogl-v2","lsl-catalog-v1","script-world-actions-v1","script-world-actions-v2","script-world-actions-v3","script-world-queries-v1","script-world-queries-v2","crossing-v3","crossing-reservation-v1","object-crossing-v1","object-crossing-v2","linkset-runtime-v1","scene-motion-v1","scene-text-v1","scene-persistence-v5"],"endpoints":{"status":"/v1/status","storage_status":"GET /v1/storage/status","worlds":"/v1/worlds","regions":"/v1/regions","presence":"GET /v1/presence","viewer_session":"POST /v1/viewer/session","viewer_teleport":"POST /v1/viewer/teleport","viewer_handoff":"POST /v1/viewer/handoff","viewer_handoff_reserve":"POST /v1/viewer/handoff/reserve","viewer_handoff_complete":"POST /v1/viewer/handoff/complete","viewer_handoff_rollback":"POST /v1/viewer/handoff/rollback","object_crossings":"GET|POST /v1/world/object-crossings","object_crossing_rollback":"POST /v1/world/object-crossings/rollback","scripts":"GET|POST /v1/scripts","script_capabilities":"GET /v1/scripts/capabilities","script_event":"POST /v1/scripts/event","groups":"GET|POST /v1/groups","parcels":"POST /v1/parcels","friends":"GET /v1/social/friends","messages":"GET|POST /v1/social/messages","assets":"GET|POST /v1/assets","asset_transfer":"POST /v1/assets/transfer","inventory":"GET /v1/inventory","admin_roles":"GET|POST /v1/admin/roles","admin_moderation":"GET|POST /v1/admin/moderation","estates":"GET|POST /v1/estates","landmarks":"GET|POST /v1/landmarks","notifications":"GET /v1/notifications","group_channel":"GET|POST /v1/groups/{id}/channel","metrics":"GET /metrics","admin_audit":"GET /v1/admin/audit","hypergrid_info":"GET /v1/hypergrid/info","hypergrid_travel_issue":"POST /v1/hypergrid/travel/issue","hypergrid_return":"POST /v1/hypergrid/travel/return","hypergrid_im_send":"POST /v1/hypergrid/im/send","federation_info":"GET /v1/federation/info","federation_issue":"POST /v1/federation/travel/issue","federation_accept":"POST /v1/federation/travel/accept","register":"POST /v1/auth/register","login":"POST /v1/auth/login","me":"GET /v1/auth/me","logout":"POST /v1/auth/logout"}})JSON";
                } else if (method == "GET" && path == "/v1/worlds") {
                    response.body = "{\"world_nodes\":" + worlds_json(worlds_->list()) + "}";
                } else if (method == "GET" && path == "/v1/regions") {
                    response.body = "{\"regions\":" + regions_json(regions_->list()) + "}";
                } else if (method == "GET" && path.starts_with("/v1/regions/") &&
                           path.ends_with("/neighbors")) {
                    constexpr std::string_view prefix = "/v1/regions/";
                    constexpr std::string_view suffix = "/neighbors";
                    const auto id = path.substr(prefix.size(), path.size() - prefix.size() - suffix.size());
                    if (!regions_->find(id)) {
                        response = {.status = 404, .body = "{\"error\":\"region-not-found\"}"};
                    } else {
                        response.body = "{\"region_id\":\"" + json_escape(id) +
                                        "\",\"neighbors\":" + regions_json(regions_->neighbors(id)) + "}";
                    }
                } else if (method == "GET" && path == "/v1/identity/stats") {
                    response.body = "{\"users\":" + std::to_string(identities_->count()) +
                                    ",\"active_sessions\":" + std::to_string(sessions_->active_count()) + "}";
                } else if (method == "GET" && path == "/v1/identity/users") {
                    response.body = "{\"users\":" + users_json(identities_->list()) + "}";
                } else if (method == "GET" && path == "/v1/content/stats") {
                    response.body = "{\"assets\":" + std::to_string(assets_->count()) +
                                    ",\"asset_bytes\":" + std::to_string(assets_->total_bytes()) +
                                    ",\"inventory_folders\":" + std::to_string(inventory_->folder_count()) +
                                    ",\"inventory_items\":" + std::to_string(inventory_->item_count()) +
                                    ",\"federation_peers\":" + std::to_string(federation_runtime_->peers().size()) +
                                    ",\"foreign_sessions\":" + std::to_string(federation_runtime_->sessions().size()) +
                                    ",\"hypergrid_home_sessions\":" + std::to_string(hypergrid_sessions_->home_sessions().size()) +
                                    ",\"hypergrid_foreign_sessions\":" + std::to_string(hypergrid_sessions_->foreign_sessions().size()) + "}";
                } else if (method == "GET" && path == "/v1/status") {
                    const auto uptime = std::chrono::duration_cast<std::chrono::seconds>(
                        std::chrono::steady_clock::now() - started_at_).count();
                    response.body = "{\"version\":\"" + std::string(OGL_VERSION) +
                                    "\",\"uptime_seconds\":" + std::to_string(uptime) +
                                    ",\"world_nodes\":" + worlds_json(worlds_->list()) +
                                    ",\"regions\":" + regions_json(regions_->list()) +
                                    ",\"online_presences\":" + std::to_string(presences_->count()) +
                                    ",\"identities\":" + std::to_string(identities_->count()) +
                                    ",\"active_sessions\":" + std::to_string(sessions_->active_count()) +
                                    ",\"friendships\":" + std::to_string(friends_->accepted_count()) +
                                    ",\"pending_friend_requests\":" + std::to_string(friends_->pending_count()) +
                                    ",\"messages\":" + std::to_string(messages_->count()) +
                                    ",\"groups\":" + std::to_string(groups_->count()) +
                                    ",\"parcels\":" + std::to_string(parcels_->count()) +
                                    ",\"active_bans\":" + std::to_string(moderation_->active_count()) +
                                    ",\"audit_events\":" + std::to_string(audit_->count()) +
                                    ",\"estates\":" + std::to_string(estates_->count()) +
                                    ",\"landmarks\":" + std::to_string(landmarks_->count()) +
                                    ",\"notifications\":" + std::to_string(notifications_->count()) +
                                    ",\"group_posts\":" + std::to_string(group_channels_->count()) +
                                    ",\"assets\":" + std::to_string(assets_->count()) +
                                    ",\"inventory_items\":" + std::to_string(inventory_->item_count()) +
                                    ",\"object_crossings\":" +
                                    std::to_string(object_crossings_->list().size()) +
                                    ",\"storage\":" + storage_status_json() + "}";
                } else if (method == "GET" && path == "/v1/avatar/appearance") {
                    auto [session, user] = auth();
                    if (!session || !user) {
                        response = {.status = 401, .body = "{\"error\":\"invalid-session\"}"};
                    } else {
                        response.body = appearance_json(appearance_->ensure(user->id));
                    }
                } else if (method == "POST" && path == "/v1/avatar/appearance/wearable") {
                    auto [session, user] = auth();
                    const auto slot = json_string(request->body, "slot");
                    const auto item_id = json_string(request->body, "item_id");
                    const auto asset_id = json_string(request->body, "asset_id");
                    if (!session || !user) {
                        response = {.status = 401, .body = "{\"error\":\"invalid-session\"}"};
                    } else if (!slot || !item_id || !asset_id) {
                        response = {.status = 400, .body = "{\"error\":\"invalid-wearable-fields\"}"};
                    } else {
                        const auto asset = assets_->find(*asset_id);
                        if (!asset || asset->owner_user_id != user->id) {
                            response = {.status = 403, .body = "{\"error\":\"asset-not-owned\"}"};
                        } else {
                            std::string reason;
                            const auto appearance = appearance_->set_wearable(
                                user->id, *slot, *item_id, *asset_id, reason);
                            if (!appearance) {
                                response = {.status = 400,
                                            .body = "{\"error\":\"" + json_escape(reason) + "\"}"};
                            } else {
                                response.body = appearance_json(*appearance);
                            }
                        }
                    }
                } else if (method == "POST" && path == "/v1/avatar/appearance/wearable/remove") {
                    auto [session, user] = auth();
                    const auto slot = json_string(request->body, "slot");
                    if (!session || !user) {
                        response = {.status = 401, .body = "{\"error\":\"invalid-session\"}"};
                    } else if (!slot) {
                        response = {.status = 400, .body = "{\"error\":\"invalid-wearable-fields\"}"};
                    } else {
                        const auto appearance = appearance_->remove_wearable(user->id, *slot);
                        response.body = appearance_json(
                            appearance.value_or(appearance_->ensure(user->id)));
                    }
                } else if (method == "POST" && path == "/v1/avatar/appearance/attachment") {
                    auto [session, user] = auth();
                    const auto point = json_string(request->body, "point");
                    const auto item_id = json_string(request->body, "item_id");
                    const auto asset_id = json_string(request->body, "asset_id");
                    if (!session || !user) {
                        response = {.status = 401, .body = "{\"error\":\"invalid-session\"}"};
                    } else if (!point || !item_id || !asset_id) {
                        response = {.status = 400, .body = "{\"error\":\"invalid-attachment-fields\"}"};
                    } else {
                        const auto asset = assets_->find(*asset_id);
                        if (!asset || asset->owner_user_id != user->id) {
                            response = {.status = 403, .body = "{\"error\":\"asset-not-owned\"}"};
                        } else {
                            std::string reason;
                            const auto appearance = appearance_->attach(
                                user->id, *point, *item_id, *asset_id, reason);
                            if (!appearance) {
                                response = {.status = 400,
                                            .body = "{\"error\":\"" + json_escape(reason) + "\"}"};
                            } else {
                                response.body = appearance_json(*appearance);
                            }
                        }
                    }
                } else if (method == "POST" && path == "/v1/avatar/appearance/attachment/remove") {
                    auto [session, user] = auth();
                    const auto point = json_string(request->body, "point");
                    const auto item_id = json_string(request->body, "item_id");
                    if (!session || !user) {
                        response = {.status = 401, .body = "{\"error\":\"invalid-session\"}"};
                    } else if (!point) {
                        response = {.status = 400, .body = "{\"error\":\"invalid-attachment-fields\"}"};
                    } else {
                        const auto appearance = appearance_->detach(
                            user->id, *point, item_id.value_or(""));
                        response.body = appearance_json(
                            appearance.value_or(appearance_->ensure(user->id)));
                    }
                } else if (method == "GET" && path == "/v1/hypergrid/info") {
                    const auto& cfg = hypergrid_service_->config();
                    response.body = "{\"enabled\":" + std::string(cfg.enabled ? "true" : "false") +
                                    ",\"external_name\":\"" + json_escape(cfg.external_name) +
                                    "\",\"home_uri\":\"" + json_escape(cfg.home_uri) +
                                    "\",\"foreign_sessions\":" +
                                    std::to_string(hypergrid_sessions_->foreign_sessions().size()) +
                                    ",\"home_sessions\":" +
                                    std::to_string(hypergrid_sessions_->home_sessions().size()) + "}";
                } else if (method == "POST" && path == "/v1/hypergrid/travel/issue") {
                    auto [session, user] = auth();
                    const auto destination = json_string(request->body, "destination_gatekeeper");
                    const auto client_ip = json_string(request->body, "client_ip");
                    if (!session || !user) {
                        response = {.status = 401, .body = "{\"error\":\"invalid-session\"}"};
                    } else if (!hypergrid_service_->enabled()) {
                        response = {.status = 503, .body = "{\"error\":\"hypergrid-disabled\"}"};
                    } else if (!destination || !client_ip) {
                        response = {.status = 400, .body = "{\"error\":\"invalid-hypergrid-travel-fields\"}"};
                    } else {
                        try {
                            const auto legacy_user_id =
                                compat::hypergrid::legacy_uuid_from_seed(user->id);
                            const auto travel = hypergrid_sessions_->issue_home_travel(
                                user->id, legacy_user_id, *destination, *client_ip);
                            std::string first = user->display_name;
                            std::string last = "Resident";
                            const auto split = first.find(' ');
                            if (split != std::string::npos) {
                                last = first.substr(split + 1);
                                first.resize(split);
                            }
                            response.status = 201;
                            response.body =
                                "{\"agent_id\":\"" + json_escape(travel.user_id) +
                                "\",\"session_id\":\"" + json_escape(travel.session_id) +
                                "\",\"service_session_id\":\"" +
                                json_escape(travel.service_token) +
                                "\",\"home_uri\":\"" +
                                json_escape(hypergrid_service_->config().home_uri) +
                                "\",\"first_name\":\"" + json_escape(first) +
                                "\",\"last_name\":\"" + json_escape(last) +
                                "\",\"expires_unix\":" +
                                std::to_string(travel.expires_unix) + "}";
                        } catch (const std::exception&) {
                            response = {.status = 400, .body = "{\"error\":\"hypergrid-travel-issue-failed\"}"};
                        }
                    }
                } else if (method == "POST" && path == "/v1/hypergrid/travel/return") {
                    auto [session, user] = auth();
                    const auto session_id = json_string(request->body, "session_id");
                    if (!session || !user) {
                        response = {.status = 401, .body = "{\"error\":\"invalid-session\"}"};
                    } else if (!session_id) {
                        response = {.status = 400, .body = "{\"error\":\"missing-hypergrid-session-id\"}"};
                    } else if (!hypergrid_sessions_->request_return_home(
                                   user->id, *session_id,
                                   hypergrid_service_->config().external_name)) {
                        response = {.status = 404, .body = "{\"error\":\"hypergrid-session-not-found\"}"};
                    } else {
                        const auto home = hypergrid_service_->handle(
                            compat::hypergrid::XmlRpcCall{.method = "get_home_region",
                                                         .fields = {{"userID",
                                                                     compat::hypergrid::legacy_uuid_from_seed(user->id)}}});
                        const auto uuid = home.contains("uuid") ? home.at("uuid") : std::string{};
                        const auto name = home.contains("region_name") ? home.at("region_name") : std::string{};
                        response.body =
                            "{\"status\":\"returning-home\",\"session_id\":\"" +
                            json_escape(*session_id) +
                            "\",\"home_uri\":\"" +
                            json_escape(hypergrid_service_->config().home_uri) +
                            "\",\"home_region_uuid\":\"" + json_escape(uuid) +
                            "\",\"home_region_name\":\"" + json_escape(name) + "\"}";
                    }
                } else if (method == "POST" && path == "/v1/hypergrid/im/send") {
                    auto [session, user] = auth();
                    const auto target = json_string(request->body, "target_agent_id");
                    const auto text_value = json_string(request->body, "message");
                    if (!session || !user) {
                        response = {.status = 401, .body = "{\"error\":\"invalid-session\"}"};
                    } else if (!target || !text_value || text_value->empty()) {
                        response = {.status = 400, .body = "{\"error\":\"invalid-hypergrid-im-fields\"}"};
                    } else {
                        std::string reason;
                        if (!hypergrid_im_->send_remote(
                                user->id, user->display_name, *target, *text_value, reason)) {
                            response = {.status = 502,
                                        .body = "{\"error\":\"" + json_escape(reason) + "\"}"};
                        } else {
                            response.status = 201;
                            response.body = "{\"status\":\"sent\"}";
                        }
                    }
                } else if (method == "GET" && path == "/v1/hypergrid/sessions") {
                    if (!admin_allowed("operator")) {
                        response = {.status = 403, .body = "{\"error\":\"admin-authorization-required\"}"};
                    } else {
                        response.body =
                            "{\"home_sessions\":" +
                            std::to_string(hypergrid_sessions_->home_sessions().size()) +
                            ",\"foreign_sessions\":" +
                            std::to_string(hypergrid_sessions_->foreign_sessions().size()) + "}";
                    }
                } else if (method == "GET" && path == "/v1/federation/info") {
                    const auto info = federation_runtime_->info();
                    response.body = "{\"protocol\":\"" + json_escape(info.protocol) +
                                    "\",\"grid_id\":\"" + json_escape(info.grid_id) +
                                    "\",\"base_url\":\"" + json_escape(info.base_url) +
                                    "\",\"public_key\":\"" + json_escape(info.public_key_hex) + "\"}";
                } else if (method == "GET" && path == "/v1/federation/peers") {
                    if (!admin_allowed("operator")) {
                        response = {.status = 403, .body = "{\"error\":\"admin-authorization-required\"}"};
                    } else {
                        response.body = "{\"peers\":" +
                                        federation_peers_json(federation_runtime_->peers()) + "}";
                    }
                } else if (method == "POST" && path == "/v1/federation/trust") {
                    if (!admin_allowed("administrator")) {
                        response = {.status = 403, .body = "{\"error\":\"admin-authorization-required\"}"};
                    } else {
                        const auto grid_id = json_string(request->body, "grid_id");
                        const auto base_url = json_string(request->body, "base_url");
                        const auto public_key = json_string(request->body, "public_key");
                        if (!grid_id || !base_url || !public_key) {
                            response = {.status = 400, .body = "{\"error\":\"invalid-peer-fields\"}"};
                        } else {
                            std::string reason;
                            const bool ok = federation_runtime_->trust_peer(
                                {.grid_id = *grid_id,
                                 .base_url = *base_url,
                                 .public_key_hex = *public_key,
                                 .trusted = true,
                                 .revoked = false,
                                 .updated_unix = 0},
                                reason);
                            if (!ok) {
                                response = {.status = 400,
                                            .body = "{\"error\":\"" + json_escape(reason) + "\"}"};
                            } else {
                                audit_->append("admin-api", "federation.trust", *grid_id, *base_url);
                                response.status = 201;
                                response.body = "{\"status\":\"trusted\"}";
                            }
                        }
                    }
                } else if (method == "POST" && path == "/v1/federation/revoke") {
                    if (!admin_allowed("administrator")) {
                        response = {.status = 403, .body = "{\"error\":\"admin-authorization-required\"}"};
                    } else {
                        const auto grid_id = json_string(request->body, "grid_id");
                        if (!grid_id || !federation_runtime_->revoke_peer(*grid_id)) {
                            response = {.status = 404, .body = "{\"error\":\"peer-not-found\"}"};
                        } else {
                            audit_->append("admin-api", "federation.revoke", *grid_id);
                            response.body = "{\"status\":\"revoked\"}";
                        }
                    }
                } else if (method == "POST" && path == "/v1/federation/travel/issue") {
                    auto [session, user] = auth();
                    const auto audience = json_string(request->body, "audience_grid");
                    const auto destination = json_string(request->body, "destination_region");
                    const auto origin = json_string(request->body, "origin_region");
                    const auto lifetime_value = json_number(request->body, "lifetime_seconds").value_or(120.0);
                    if (!session || !user) {
                        response = {.status = 401, .body = "{\"error\":\"invalid-session\"}"};
                    } else if (!audience || !destination) {
                        response = {.status = 400, .body = "{\"error\":\"invalid-travel-fields\"}"};
                    } else {
                        std::string reason;
                        const auto issued = federation_runtime_->issue_travel(
                            {.subject_user = user->id,
                             .display_name = user->display_name,
                             .audience_grid = *audience,
                             .origin_region = origin.value_or(""),
                             .destination_region = *destination,
                             .lifetime = std::chrono::seconds{
                                 static_cast<std::int64_t>(lifetime_value)}},
                            reason);
                        if (!issued) {
                            response = {.status = 403,
                                        .body = "{\"error\":\"" + json_escape(reason) + "\"}"};
                        } else {
                            response.status = 201;
                            response.body = "{\"protocol\":\"OGL-FED/1\",\"travel_token\":\"" +
                                            json_escape(issued->token) +
                                            "\",\"audience_grid\":\"" +
                                            json_escape(issued->claims.audience_grid) +
                                            "\",\"destination_region\":\"" +
                                            json_escape(issued->claims.destination_region) +
                                            "\",\"expires_unix\":" +
                                            std::to_string(issued->claims.expires_unix) + "}";
                        }
                    }
                } else if (method == "POST" && path == "/v1/federation/travel/accept") {
                    const auto issuer = json_string(request->body, "issuer_grid");
                    const auto token = json_string(request->body, "travel_token");
                    if (!issuer || !token) {
                        response = {.status = 400, .body = "{\"error\":\"invalid-travel-fields\"}"};
                    } else {
                        std::string reason;
                        const auto accepted = federation_runtime_->accept_travel(*issuer, *token, reason);
                        if (!accepted) {
                            response = {.status = 403,
                                        .body = "{\"error\":\"" + json_escape(reason) + "\"}"};
                        } else {
                            const auto region = regions_->find(accepted->claims.destination_region);
                            const auto world = region ? worlds_->find(region->node_id) : std::nullopt;
                            const auto estate = region ? estates_->policy_for_region(region->id) : std::nullopt;
                            const bool estate_ok = !estate || estate->public_access;
                            const bool parcel_ok = region && parcels_->can_enter(
                                region->id, 128.0, 128.0, accepted->claims.subject_user, {});
                            if (!region || region->state != "online" || !world ||
                                world->state != "online" || !estate_ok || !parcel_ok) {
                                (void)federation_runtime_->logout_session(accepted->session.id);
                                response = {.status = 403, .body = "{\"error\":\"destination-unavailable\"}"};
                            } else {
                                const double spawn_x = estate ? estate->landing_x : 128.0;
                                const double spawn_y = estate ? estate->landing_y : 128.0;
                                const double spawn_z = estate ? estate->landing_z : 0.0;
                                const auto ticket = security::issue_scene_ticket(
                                    scene_ticket_secret_,
                                    accepted->claims.subject_user,
                                    accepted->claims.display_name,
                                    region->id,
                                    scene_ticket_lifetime_,
                                    std::string{security::kDefaultSceneCapabilities},
                                    accepted->claims.origin_region,
                                    {},
                                    spawn_x, spawn_y, spawn_z);
                                response.status = 201;
                                response.body = "{\"status\":\"accepted\",\"foreign\":true,"
                                                "\"foreign_session_id\":\"" +
                                                json_escape(accepted->session.id) +
                                                "\",\"issuer_grid\":\"" +
                                                json_escape(accepted->claims.issuer_grid) +
                                                "\",\"scene_endpoint\":\"" +
                                                json_escape(world->endpoint) +
                                                "\",\"scene_ticket\":\"" +
                                                json_escape(ticket.token) +
                                                "\",\"destination_region\":\"" +
                                                json_escape(region->id) +
                                                "\",\"expires_unix\":" +
                                                std::to_string(ticket.claims.expires_unix) + "}";
                            }
                        }
                    }
                } else if (method == "GET" && path == "/v1/federation/sessions") {
                    if (!admin_allowed("operator")) {
                        response = {.status = 403, .body = "{\"error\":\"admin-authorization-required\"}"};
                    } else {
                        response.body = "{\"sessions\":" +
                                        federation_sessions_json(federation_runtime_->sessions()) + "}";
                    }
                } else if (method == "POST" && path == "/v1/federation/sessions/logout") {
                    if (!admin_allowed("operator")) {
                        response = {.status = 403, .body = "{\"error\":\"admin-authorization-required\"}"};
                    } else {
                        const auto session_id = json_string(request->body, "session_id");
                        if (!session_id || !federation_runtime_->logout_session(*session_id)) {
                            response = {.status = 404, .body = "{\"error\":\"session-not-found\"}"};
                        } else {
                            response.body = "{\"status\":\"logged-out\"}";
                        }
                    }
                } else if (method == "POST" && path == "/v1/auth/register") {
                    const auto username = json_string(request->body, "username");
                    const auto display = json_string(request->body, "display_name");
                    const auto password = json_string(request->body, "password");
                    auth_rate_limiter_.purge();
                    const auto register_global_allowed =
                        auth_rate_limiter_.allow(
                            "register:global",
                            registration_attempts_per_minute_,
                            std::chrono::minutes{1});
                    const auto register_user_allowed =
                        register_global_allowed &&
                        (!username ||
                         auth_rate_limiter_.allow(
                             "register:" + lower(*username),
                             3U, std::chrono::minutes{1}));
                    if (!register_global_allowed ||
                        !register_user_allowed) {
                        response = {
                            .status = 429,
                            .body = "{\"error\":\"registration-rate-limited\"}"};
                    } else if (!username || !display || !password) {
                        response = {.status = 400, .body = "{\"error\":\"invalid-json-fields\"}"};
                    } else {
                        std::string reason;
                        const auto user = identities_->register_user(*username, *display, *password, reason);
                        if (!user) {
                            response = {.status = reason == "username-exists" ? 409 : 400,
                                        .body = "{\"error\":\"" + json_escape(reason) + "\"}"};
                        } else {
                            (void)inventory_->ensure_root(user->id);
                            const auto created = sessions_->create(user->id);
                            response.status = 201;
                            response.body = "{\"user\":{\"id\":\"" + json_escape(user->id) +
                                            "\",\"username\":\"" + json_escape(user->username) +
                                            "\",\"display_name\":\"" + json_escape(user->display_name) +
                                            "\"},\"token\":\"" + created.token +
                                            "\",\"expires_unix\":" +
                                            std::to_string(created.session.expires_unix) + "}";
                        }
                    }
                } else if (method == "POST" && path == "/v1/auth/login") {
                    const auto username = json_string(request->body, "username");
                    const auto password = json_string(request->body, "password");
                    auth_rate_limiter_.purge();
                    const auto login_key =
                        username ? "login:" + lower(*username)
                                 : std::string{"login:invalid"};
                    const auto login_global_allowed =
                        auth_rate_limiter_.allow(
                            "login:global",
                            login_attempts_per_minute_ * 50U,
                            std::chrono::minutes{1});
                    if (!login_global_allowed ||
                        !auth_rate_limiter_.allow(
                            login_key,
                            login_attempts_per_minute_,
                            std::chrono::minutes{1})) {
                        response = {
                            .status = 429,
                            .body = "{\"error\":\"login-rate-limited\"}"};
                    } else if (!username || !password) {
                        response = {.status = 400, .body = "{\"error\":\"invalid-json-fields\"}"};
                    } else if (const auto user = identities_->authenticate(*username, *password)) {
                        auth_rate_limiter_.clear(login_key);
                        (void)inventory_->ensure_root(user->id);
                        const auto created = sessions_->create(user->id);
                        response.body = "{\"user\":{\"id\":\"" + json_escape(user->id) +
                                        "\",\"username\":\"" + json_escape(user->username) +
                                        "\",\"display_name\":\"" + json_escape(user->display_name) +
                                        "\"},\"token\":\"" + created.token +
                                        "\",\"expires_unix\":" +
                                        std::to_string(created.session.expires_unix) + "}";
                    } else {
                        response = {.status = 401, .body = "{\"error\":\"invalid-credentials\"}"};
                    }
                } else if (method == "GET" && path == "/v1/auth/me") {
                    auto [session, user] = auth();
                    if (!session || !user) {
                        response = {.status = 401, .body = "{\"error\":\"invalid-session\"}"};
                    } else {
                        response.body = "{\"user\":{\"id\":\"" + json_escape(user->id) +
                                        "\",\"username\":\"" + json_escape(user->username) +
                                        "\",\"display_name\":\"" + json_escape(user->display_name) +
                                        "\"},\"session\":{\"id\":\"" + json_escape(session->id) +
                                        "\",\"expires_unix\":" + std::to_string(session->expires_unix) +
                                        "},\"online\":" +
                                        (presences_->find_user(user->id).has_value() ? "true" : "false") + "}";
                    }
                } else if (method == "POST" && path == "/v1/auth/logout") {
                    const auto token = bearer_token(*request);
                    if (token.empty() || !sessions_->revoke(token)) {
                        response = {.status = 401, .body = "{\"error\":\"invalid-session\"}"};
                    } else response.body = "{\"status\":\"logged-out\"}";
                } else if (method == "POST" && path == "/v1/viewer/session") {
                    auto [session, user] = auth();
                    const auto region_id = json_string(request->body, "region");
                    if (!session || !user) {
                        response = {.status = 401, .body = "{\"error\":\"invalid-session\"}"};
                    } else if (!region_id) {
                        response = {.status = 400, .body = "{\"error\":\"missing-region\"}"};
                    } else {
                        const auto region = regions_->find(*region_id);
                        const auto world = region ? worlds_->find(region->node_id) : std::nullopt;
                        const auto x = json_number(request->body, "x").value_or(128.0);
                        const auto y = json_number(request->body, "y").value_or(128.0);
                        const auto z = json_number(request->body, "z").value_or(0.0);
                        const auto group_ids = groups_->group_ids_for_user(user->id);
                        if (!region || !world || region->state != "online" || world->state != "online") {
                            response = {.status = 409,
                                        .body = "{\"error\":\"region-offline-or-unknown\"}"};
                        } else if (moderation_->is_banned(user->id, region->id)) {
                            response = {.status = 403, .body = "{\"error\":\"user-banned\"}"};
                        } else if (const auto error = estate_entry_error(*user, *region); !error.empty()) {
                            response = {.status = 403, .body = "{\"error\":\"" + json_escape(error) + "\"}"};
                        } else if (!parcels_->can_enter(region->id, x, y, user->id, group_ids)) {
                            response = {.status = 403, .body = "{\"error\":\"parcel-entry-denied\"}"};
                        } else response.body = viewer_response(*user, *region, *world, {}, x, y, z);
                    }
                } else if (method == "POST" && path == "/v1/viewer/teleport") {
                    auto [session, user] = auth();
                    const auto landmark_id = json_string(request->body, "landmark_id");
                    auto region_id = json_string(request->body, "region");
                    std::optional<LandmarkInfo> landmark;
                    if (landmark_id) landmark = landmarks_->find(*landmark_id);
                    if (landmark && user && landmark->user_id == user->id) region_id = landmark->region_id;
                    if (!session || !user) response = {.status = 401, .body = "{\"error\":\"invalid-session\"}"};
                    else if (landmark_id && (!landmark || landmark->user_id != user->id)) response = {.status = 404, .body = "{\"error\":\"landmark-not-found\"}"};
                    else if (!region_id) response = {.status = 400, .body = "{\"error\":\"missing-region\"}"};
                    else {
                        const auto region = regions_->find(*region_id);
                        const auto world = region ? worlds_->find(region->node_id) : std::nullopt;
                        double x = landmark ? landmark->x : json_number(request->body, "x").value_or(128.0);
                        double y = landmark ? landmark->y : json_number(request->body, "y").value_or(128.0);
                        double z = landmark ? landmark->z : json_number(request->body, "z").value_or(0.0);
                        if (!landmark && region && !json_number(request->body, "x") && !json_number(request->body, "y")) {
                            if (const auto policy = estates_->policy_for_region(region->id)) {
                                x = policy->landing_x; y = policy->landing_y; z = policy->landing_z;
                            }
                        }
                        const auto group_ids = groups_->group_ids_for_user(user->id);
                        if (!region || !world || region->state != "online" || world->state != "online") response = {.status = 409, .body = "{\"error\":\"region-offline-or-unknown\"}"};
                        else if (moderation_->is_banned(user->id, region->id)) response = {.status = 403, .body = "{\"error\":\"user-banned\"}"};
                        else if (const auto error = estate_entry_error(*user, *region); !error.empty()) response = {.status = 403, .body = "{\"error\":\"" + json_escape(error) + "\"}"};
                        else if (!parcels_->can_enter(region->id, x, y, user->id, group_ids)) response = {.status = 403, .body = "{\"error\":\"parcel-entry-denied\"}"};
                        else { audit_->append(user->id, landmark ? "teleport.landmark" : "teleport.ticket", region->id, landmark ? landmark->id : std::string{}); response.body = viewer_response(*user,*region,*world,{},x,y,z); }
                    }
                } else if (method == "POST" && path == "/v1/viewer/handoff") {
                    auto [session, user] = auth();
                    const auto from = json_string(request->body, "from_region");
                    const auto to = json_string(request->body, "to_region");
                    if (!session || !user) {
                        response = {.status = 401, .body = "{\"error\":\"invalid-session\"}"};
                    } else if (!from || !to) {
                        response = {.status = 400, .body = "{\"error\":\"invalid-json-fields\"}"};
                    } else {
                        bool adjacent = false;
                        for (const auto& candidate : regions_->neighbors(*from)) {
                            if (candidate.id == *to) adjacent = true;
                        }
                        const auto region = adjacent ? regions_->find(*to) : std::nullopt;
                        const auto world = region ? worlds_->find(region->node_id) : std::nullopt;
                        if (!adjacent || !region || !world || region->state != "online" ||
                            world->state != "online") {
                            response = {.status = 409,
                                        .body = "{\"error\":\"handoff-target-not-adjacent-or-offline\"}"};
                        } else {
                            const auto source = regions_->find(*from);
                            double x = 128.0, y = 128.0;
                            if (source) {
                                if (region->grid_x > source->grid_x) x = 0.5;
                                else if (region->grid_x < source->grid_x) x = 255.5;
                                if (region->grid_y > source->grid_y) y = 0.5;
                                else if (region->grid_y < source->grid_y) y = 255.5;
                            }
                            const auto group_ids = groups_->group_ids_for_user(user->id);
                            if (moderation_->is_banned(user->id, region->id)) {
                                response = {.status = 403, .body = "{\"error\":\"user-banned\"}"};
                            } else if (const auto error = estate_entry_error(*user, *region); !error.empty()) {
                                response = {.status = 403, .body = "{\"error\":\"" + json_escape(error) + "\"}"};
                            } else if (!parcels_->can_enter(region->id, x, y, user->id, group_ids)) {
                                response = {.status = 403, .body = "{\"error\":\"parcel-entry-denied\"}"};
                            } else {
                                const auto vx = json_number(request->body, "vx").value_or(0.0);
                                const auto vy = json_number(request->body, "vy").value_or(0.0);
                                const auto vz = json_number(request->body, "vz").value_or(0.0);
                                const auto rx = json_number(request->body, "rx").value_or(0.0);
                                const auto ry = json_number(request->body, "ry").value_or(0.0);
                                const auto rz = json_number(request->body, "rz").value_or(0.0);
                                const auto avx = json_number(request->body, "avx").value_or(0.0);
                                const auto avy = json_number(request->body, "avy").value_or(0.0);
                                const auto avz = json_number(request->body, "avz").value_or(0.0);
                                const auto avatar = appearance_->find(user->id);
                                const auto attachment_state = avatar ? appearance_json(*avatar) : std::string{"{}"};

                                std::ostringstream script_state;
                                script_state << '[';
                                bool first_script = true;
                                for (const auto& script : scripts_->list()) {
                                    if (script.owner_user_id != user->id) continue;
                                    if (!first_script) script_state << ',';
                                    first_script = false;
                                    script_state << "{\"id\":\"" << json_escape(script.id)
                                                 << "\",\"object_id\":\"" << json_escape(script.object_id)
                                                 << "\",\"state\":\"" << json_escape(script.state)
                                                 << "\",\"vm_state\":\"" << json_escape(script.vm_state)
                                                 << "\"}";
                                }
                                script_state << ']';

                                std::ostringstream physics_state;
                                physics_state << "{\"linear_velocity\":{\"x\":" << vx
                                              << ",\"y\":" << vy
                                              << ",\"z\":" << vz
                                              << "},\"angular_velocity\":{\"x\":" << avx
                                              << ",\"y\":" << avy
                                              << ",\"z\":" << avz << "}}";

                                const auto now = std::chrono::duration_cast<std::chrono::seconds>(
                                    std::chrono::system_clock::now().time_since_epoch()).count();
                                std::string reason;
                                const auto crossing = crossings_->prepare(
                                    user->id, *from, *to,
                                    {.x = x, .y = y, .z = 0.0},
                                    {.x = vx, .y = vy, .z = vz},
                                    now + std::min<std::int64_t>(scene_ticket_lifetime_.count(), 300),
                                    reason, attachment_state, script_state.str(),
                                    {.x = rx, .y = ry, .z = rz},
                                    {.x = avx, .y = avy, .z = avz},
                                    physics_state.str(), "[]");
                                if (!crossing) {
                                    response = {.status = 409,
                                                .body = "{\"error\":\"" + json_escape(reason) + "\"}"};
                                } else {
                                    audit_->append(user->id, "region.handoff.prepare",
                                                   *from + "->" + *to, crossing->id);
                                    response.body = viewer_response(
                                        *user, *region, *world, *from, x, y, 0.0, crossing->id);
                                }
                            }
                        }
                    }
                } else if (method == "POST" && path == "/v1/viewer/handoff/reserve") {
                    auto [session, user] = auth();
                    const auto crossing_id = json_string(request->body, "crossing_id");
                    const auto region_id = json_string(request->body, "region");
                    if (!session || !user) {
                        response = {.status = 401, .body = "{\"error\":\"invalid-session\"}"};
                    } else if (!crossing_id || !region_id) {
                        response = {.status = 400, .body = "{\"error\":\"invalid-crossing-fields\"}"};
                    } else {
                        std::string reason;
                        const auto crossing =
                            crossings_->reserve(*crossing_id, user->id, *region_id, reason);
                        if (!crossing) {
                            response = {.status = 409,
                                        .body = "{\"error\":\"" + json_escape(reason) + "\"}"};
                        } else {
                            audit_->append(user->id, "region.handoff.reserve", crossing->id,
                                           crossing->from_region + "->" + crossing->to_region);
                            response.body = crossing_json(*crossing);
                        }
                    }
                } else if (method == "POST" && path == "/v1/viewer/handoff/complete") {
                    auto [session, user] = auth();
                    const auto crossing_id = json_string(request->body, "crossing_id");
                    const auto region_id = json_string(request->body, "region");
                    const auto reservation_token =
                        json_string(request->body, "reservation_token");
                    if (!session || !user) {
                        response = {.status = 401, .body = "{\"error\":\"invalid-session\"}"};
                    } else if (!crossing_id || !region_id || !reservation_token) {
                        response = {.status = 400, .body = "{\"error\":\"invalid-crossing-fields\"}"};
                    } else {
                        std::string reason;
                        const auto crossing = crossings_->complete(
                            *crossing_id, user->id, *region_id,
                            *reservation_token, reason);
                        if (!crossing) {
                            response = {.status = 409,
                                        .body = "{\"error\":\"" + json_escape(reason) + "\"}"};
                        } else {
                            audit_->append(user->id, "region.handoff.complete", crossing->id,
                                           crossing->from_region + "->" + crossing->to_region);
                            response.body = crossing_json(*crossing);
                        }
                    }
                } else if (method == "POST" && path == "/v1/viewer/handoff/rollback") {
                    auto [session, user] = auth();
                    const auto crossing_id = json_string(request->body, "crossing_id");
                    const auto rollback_reason =
                        json_string(request->body, "reason").value_or("client-rollback");
                    if (!session || !user) {
                        response = {.status = 401, .body = "{\"error\":\"invalid-session\"}"};
                    } else if (!crossing_id) {
                        response = {.status = 400, .body = "{\"error\":\"invalid-crossing-fields\"}"};
                    } else {
                        std::string reason;
                        const auto crossing = crossings_->rollback(
                            *crossing_id, user->id, rollback_reason, reason);
                        if (!crossing) {
                            response = {.status = 409,
                                        .body = "{\"error\":\"" + json_escape(reason) + "\"}"};
                        } else {
                            audit_->append(user->id, "region.handoff.rollback", crossing->id,
                                           crossing->rollback_reason);
                            response.body = crossing_json(*crossing);
                        }
                    }
                } else if (method == "GET" &&
                           path == "/v1/world/object-crossings") {
                    auto [session, user] = auth();
                    if (!session || !user) {
                        response = {.status = 401,
                                    .body = "{\"error\":\"invalid-session\"}"};
                    } else {
                        response.body =
                            "{\"object_crossings\":" +
                            object_crossings_json(
                                object_crossings_->list(), user->id) +
                            "}";
                    }
                } else if (method == "POST" &&
                           path == "/v1/world/object-crossings") {
                    auto [session, user] = auth();
                    const auto source_region =
                        json_string(request->body, "source_region");
                    const auto destination_region =
                        json_string(request->body, "destination_region");
                    const auto source_entity_id =
                        json_u64(request->body, "source_entity_id");
                    const auto requested_z =
                        json_number(request->body, "z").value_or(0.0);

                    if (!session || !user) {
                        response = {.status = 401,
                                    .body = "{\"error\":\"invalid-session\"}"};
                    } else if (!source_region || !destination_region ||
                               !source_entity_id || *source_entity_id == 0) {
                        response = {.status = 400,
                                    .body = "{\"error\":\"invalid-object-crossing-fields\"}"};
                    } else {
                        const auto source = regions_->find(*source_region);
                        const auto destination =
                            regions_->find(*destination_region);
                        bool adjacent = false;
                        if (source && destination) {
                            for (const auto& candidate :
                                 regions_->neighbors(*source_region)) {
                                if (candidate.id == *destination_region) {
                                    adjacent = true;
                                    break;
                                }
                            }
                        }

                        if (!source || !destination || !adjacent ||
                            source->state != "online" ||
                            destination->state != "online") {
                            response = {
                                .status = 409,
                                .body = "{\"error\":\"object-crossing-target-not-adjacent-or-offline\"}"};
                        } else {
                            double x = 128.0;
                            double y = 128.0;
                            if (destination->grid_x > source->grid_x) x = 1.0;
                            else if (destination->grid_x < source->grid_x) x = 255.0;
                            if (destination->grid_y > source->grid_y) y = 1.0;
                            else if (destination->grid_y < source->grid_y) y = 255.0;

                            const auto group_ids =
                                groups_->group_ids_for_user(user->id);
                            if (moderation_->is_banned(
                                    user->id, destination->id)) {
                                response = {.status = 403,
                                            .body = "{\"error\":\"user-banned\"}"};
                            } else if (const auto error =
                                           object_estate_error(
                                               *user, *destination);
                                       !error.empty()) {
                                response = {
                                    .status = 403,
                                    .body = "{\"error\":\"" +
                                            json_escape(error) + "\"}"};
                            } else if (!parcels_->can_build(
                                           destination->id, x, y,
                                           user->id, group_ids)) {
                                response = {
                                    .status = 403,
                                    .body = "{\"error\":\"parcel-build-denied\"}"};
                            } else {
                                const auto now =
                                    std::chrono::duration_cast<
                                        std::chrono::seconds>(
                                        std::chrono::system_clock::now()
                                            .time_since_epoch())
                                        .count();
                                const auto lifetime =
                                    std::max<std::int64_t>(
                                        15,
                                        std::min<std::int64_t>(
                                            scene_ticket_lifetime_.count(),
                                            300));
                                std::string reason;
                                std::optional<ObjectCrossingRecord> crossing;
                                try {
                                    crossing = object_crossings_->prepare(
                                        user->id, *source_region,
                                        *destination_region,
                                        *source_entity_id,
                                        {.x = x,
                                         .y = y,
                                         .z = requested_z},
                                        now + lifetime, reason);
                                } catch (const std::exception& store_error) {
                                    common::log(
                                        common::LogLevel::error,
                                        "core.object_crossing",
                                        std::string{"prepare persistence failed: "} +
                                            store_error.what());
                                    response = {
                                        .status = 503,
                                        .body = "{\"error\":\"object-crossing-store-unavailable\"}"};
                                }
                                if (!crossing && response.status != 503) {
                                    response = {
                                        .status = 409,
                                        .body = "{\"error\":\"" +
                                                json_escape(reason) + "\"}"};
                                } else if (crossing) {
                                    response.status = 202;
                                    response.body =
                                        object_crossing_json(*crossing);
                                    try {
                                        audit_->append(
                                            user->id,
                                            "object.crossing.prepare",
                                            crossing->id,
                                            crossing->source_region + "->" +
                                                crossing->destination_region +
                                                ":" +
                                                std::to_string(
                                                    crossing->source_entity_id));
                                    } catch (const std::exception& audit_error) {
                                        common::log(
                                            common::LogLevel::warning,
                                            "core.object_crossing.audit",
                                            audit_error.what());
                                    }
                                }
                            }
                        }
                    }
                } else if (method == "POST" &&
                           path == "/v1/world/object-crossings/rollback") {
                    auto [session, user] = auth();
                    const auto crossing_id =
                        json_string(request->body, "crossing_id");
                    const auto rollback_reason =
                        json_string(request->body, "reason")
                            .value_or("client-rollback");
                    if (!session || !user) {
                        response = {.status = 401,
                                    .body = "{\"error\":\"invalid-session\"}"};
                    } else if (!crossing_id) {
                        response = {.status = 400,
                                    .body = "{\"error\":\"invalid-object-crossing-fields\"}"};
                    } else {
                        std::string reason;
                        const auto crossing =
                            object_crossings_->rollback(
                                *crossing_id, user->id,
                                rollback_reason, reason);
                        if (!crossing) {
                            response = {
                                .status = 409,
                                .body = "{\"error\":\"" +
                                        json_escape(reason) + "\"}"};
                        } else {
                            audit_->append(
                                user->id, "object.crossing.rollback",
                                crossing->id,
                                crossing->rollback_reason);
                            response.body =
                                object_crossing_json(*crossing);
                        }
                    }
                } else if (method == "GET" &&
                           path == "/v1/scripts/capabilities") {
                    response.body = script_engine_capabilities_json();
                } else if (method == "GET" && path == "/v1/scripts") {
                    auto [session, user] = auth();
                    if (!session || !user) {
                        response = {.status = 401, .body = "{\"error\":\"invalid-session\"}"};
                    } else {
                        std::ostringstream out;
                        out << "{\"scripts\":[";
                        bool first_script = true;
                        for (const auto& script : scripts_->list()) {
                            if (script.owner_user_id != user->id) continue;
                            if (!first_script) out << ',';
                            first_script = false;
                            out << script_record_json(script);
                        }
                        out << "]}";
                        response.body = out.str();
                    }
                } else if (method == "POST" && path == "/v1/scripts") {
                    auto [session, user] = auth();
                    const auto object_id = json_string(request->body, "object_id");
                    const auto source = json_string(request->body, "source");
                    const auto language_name =
                        json_string(request->body, "language")
                            .value_or("legacy");
                    const auto language =
                        scripting::parse_script_language(language_name);
                    if (!session || !user) {
                        response = {.status = 401, .body = "{\"error\":\"invalid-session\"}"};
                    } else if (!object_id || !source || !language) {
                        response = {.status = 400, .body = "{\"error\":\"invalid-script-fields\"}"};
                    } else {
                        const auto id = security::random_hex(16);
                        std::string reason;
                        const bool stored = scripts_->upsert(
                            {.id = id,
                             .object_id = *object_id,
                             .owner_user_id = user->id,
                             .source_hash = "pending",
                             .language = *language,
                             .source = {},
                             .vm_state = {},
                             .state = "default",
                             .enabled = true,
                             .timer_interval_ms = 0,
                             .next_timer_unix_ms = 0,
                             .chat_channel = 0,
                             .chat_enabled = false,
                             .event_count = 0},
                            reason);
                        if (!stored || !scripts_->set_program(id, *source, reason)) {
                            (void)scripts_->remove(id);
                            response = {.status = 400,
                                        .body = "{\"error\":\"" + json_escape(reason) + "\"}"};
                        } else {
                            const auto script = scripts_->find(id);
                            response.status = 201;
                            response.body = script ? script_record_json(*script)
                                                   : "{\"error\":\"script-not-found\"}";
                        }
                    }
                } else if (method == "POST" && path == "/v1/scripts/event") {
                    auto [session, user] = auth();
                    const auto script_id = json_string(request->body, "script_id");
                    const auto event = json_string(request->body, "event");
                    const auto event_payload =
                        json_string(request->body, "payload").value_or("");
                    if (!session || !user) {
                        response = {.status = 401, .body = "{\"error\":\"invalid-session\"}"};
                    } else if (!script_id || !event) {
                        response = {.status = 400, .body = "{\"error\":\"invalid-script-event\"}"};
                    } else {
                        const auto script = scripts_->find(*script_id);
                        if (!script || script->owner_user_id != user->id) {
                            response = {.status = 404, .body = "{\"error\":\"script-not-found\"}"};
                        } else {
                            const auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::system_clock::now().time_since_epoch()).count();
                            std::string reason;
                            const auto result = scripts_->execute_event(
                                *script_id, *event, now_ms, reason, {},
                                event_payload);
                            if (!result) {
                                response = {.status = 400,
                                            .body = "{\"error\":\"" + json_escape(reason) + "\"}"};
                            } else {
                                const auto host =
                                    script_host_->apply(user->id, *script_id, result->actions,
                                                        script->object_id);
                                response.body = "{\"status\":\"executed\",\"instructions\":" +
                                                std::to_string(result->instructions_executed) +
                                                ",\"state\":\"" +
                                                json_escape(result->state.state) +
                                                "\",\"actions\":" +
                                                script_actions_json(result->actions) +
                                                ",\"host_applied\":" +
                                                std::to_string(host.applied) +
                                                ",\"host_errors\":" +
                                                string_list_json(host.errors) + "}";
                            }
                        }
                    }
                } else if (method == "GET" && path == "/v1/groups") {
                    auto [session, user] = auth();
                    if (!session || !user) response = {.status = 401, .body = "{\"error\":\"invalid-session\"}"};
                    else response.body = "{\"groups\":" + groups_json(groups_->list_for_user(user->id)) + "}";
                } else if (method == "POST" && path == "/v1/groups") {
                    auto [session, user] = auth();
                    const auto name = json_string(request->body, "name");
                    if (!session || !user) response = {.status = 401, .body = "{\"error\":\"invalid-session\"}"};
                    else if (!name) response = {.status = 400, .body = "{\"error\":\"missing-name\"}"};
                    else {
                        std::string reason;
                        const auto group = groups_->create(user->id, *name, reason);
                        if (!group) response = {.status = 409, .body = "{\"error\":\"" + json_escape(reason) + "\"}"};
                        else { audit_->append(user->id, "group.create", group->id, group->name); response.status = 201; response.body = "{\"group_id\":\"" + group->id + "\"}"; }
                    }
                } else if (method == "GET" && path.starts_with("/v1/groups/") && path.ends_with("/members")) {
                    auto [session, user] = auth();
                    constexpr std::string_view prefix = "/v1/groups/";
                    constexpr std::string_view suffix = "/members";
                    const auto id = path.substr(prefix.size(), path.size() - prefix.size() - suffix.size());
                    if (!session || !user) response = {.status = 401, .body = "{\"error\":\"invalid-session\"}"};
                    else if (!groups_->is_member(id, user->id)) response = {.status = 403, .body = "{\"error\":\"not-group-member\"}"};
                    else response.body = "{\"members\":" + group_members_json(groups_->members(id)) + "}";
                } else if (method == "POST" && path == "/v1/groups/members") {
                    auto [session, user] = auth();
                    const auto group_id = json_string(request->body, "group_id");
                    const auto member_id = json_string(request->body, "user_id");
                    const auto role = json_string(request->body, "role");
                    if (!session || !user) response = {.status = 401, .body = "{\"error\":\"invalid-session\"}"};
                    else if (!group_id || !member_id || !identities_->find_by_id(*member_id)) response = {.status = 400, .body = "{\"error\":\"invalid-member\"}"};
                    else {
                        std::string reason;
                        const auto member = groups_->add_member(user->id, *group_id, *member_id, role.value_or("member"), reason);
                        if (!member) response = {.status = 403, .body = "{\"error\":\"" + json_escape(reason) + "\"}"};
                        else { notifications_->push(*member_id, "group_membership", "Added to group", "You were added to a group.", *group_id); audit_->append(user->id, "group.member.add", *group_id, *member_id); response.status = 201; response.body = "{\"status\":\"added\"}"; }
                    }
                } else if (method == "POST" && path == "/v1/groups/role") {
                    auto [session, user] = auth();
                    const auto group_id = json_string(request->body, "group_id");
                    const auto member_id = json_string(request->body, "user_id");
                    const auto role = json_string(request->body, "role");
                    std::string reason;
                    if (!session || !user) response = {.status = 401, .body = "{\"error\":\"invalid-session\"}"};
                    else if (!group_id || !member_id || !role || !groups_->set_role(user->id, *group_id, *member_id, *role, reason)) response = {.status = 403, .body = "{\"error\":\"" + json_escape(reason) + "\"}"};
                    else { audit_->append(user->id, "group.role", *group_id, *member_id + ":" + *role); response.body = "{\"status\":\"updated\"}"; }
                } else if (method == "POST" && path == "/v1/groups/remove") {
                    auto [session, user] = auth();
                    const auto group_id = json_string(request->body, "group_id");
                    const auto member_id = json_string(request->body, "user_id");
                    std::string reason;
                    if (!session || !user) response = {.status = 401, .body = "{\"error\":\"invalid-session\"}"};
                    else if (!group_id || !member_id || !groups_->remove_member(user->id, *group_id, *member_id, reason)) response = {.status = 403, .body = "{\"error\":\"" + json_escape(reason) + "\"}"};
                    else { audit_->append(user->id, "group.member.remove", *group_id, *member_id); response.body = "{\"status\":\"removed\"}"; }
                } else if (method == "POST" && path == "/v1/parcels") {
                    auto [session, user] = auth();
                    const auto region_id = json_string(request->body, "region_id");
                    const auto name = json_string(request->body, "name");
                    if (!session || !user) response = {.status = 401, .body = "{\"error\":\"invalid-session\"}"};
                    else if (!region_id || !name || !regions_->find(*region_id)) response = {.status = 400, .body = "{\"error\":\"invalid-parcel-fields\"}"};
                    else {
                        const auto x1 = static_cast<std::uint16_t>(std::clamp(json_number(request->body, "x1").value_or(0.0), 0.0, 255.0));
                        const auto y1 = static_cast<std::uint16_t>(std::clamp(json_number(request->body, "y1").value_or(0.0), 0.0, 255.0));
                        const auto x2 = static_cast<std::uint16_t>(std::clamp(json_number(request->body, "x2").value_or(255.0), 0.0, 255.0));
                        const auto y2 = static_cast<std::uint16_t>(std::clamp(json_number(request->body, "y2").value_or(255.0), 0.0, 255.0));
                        std::string reason;
                        const auto parcel = parcels_->create(user->id, *region_id, *name, x1, y1, x2, y2, reason);
                        if (!parcel) response = {.status = 409, .body = "{\"error\":\"" + json_escape(reason) + "\"}"};
                        else { audit_->append(user->id, "parcel.create", parcel->id, parcel->region_id); response.status = 201; response.body = "{\"parcel_id\":\"" + parcel->id + "\"}"; }
                    }
                } else if (method == "GET" && path.starts_with("/v1/regions/") && path.ends_with("/parcels")) {
                    constexpr std::string_view prefix = "/v1/regions/";
                    constexpr std::string_view suffix = "/parcels";
                    const auto id = path.substr(prefix.size(), path.size() - prefix.size() - suffix.size());
                    response.body = "{\"parcels\":" + parcels_json(parcels_->list_region(id)) + "}";
                } else if (method == "GET" && path == "/v1/parcels/owned") {
                    auto [session, user] = auth();
                    if (!session || !user) response = {.status = 401, .body = "{\"error\":\"invalid-session\"}"};
                    else response.body = "{\"parcels\":" + parcels_json(parcels_->list_owner(user->id)) + "}";
                } else if (method == "POST" && path == "/v1/parcels/policy") {
                    auto [session, user] = auth();
                    const auto parcel_id = json_string(request->body, "parcel_id");
                    const auto group_id = json_string(request->body, "group_id");
                    if (!session || !user) response = {.status = 401, .body = "{\"error\":\"invalid-session\"}"};
                    else if (!parcel_id) response = {.status = 400, .body = "{\"error\":\"missing-parcel-id\"}"};
                    else if (group_id && !group_id->empty() && !groups_->is_member(*group_id, user->id)) response = {.status = 403, .body = "{\"error\":\"not-group-member\"}"};
                    else {
                        std::string reason;
                        const bool ok = parcels_->update_policy(user->id, *parcel_id, group_id.value_or(""),
                            json_bool(request->body, "public_entry").value_or(true),
                            json_bool(request->body, "public_build").value_or(false),
                            json_bool(request->body, "group_build").value_or(true),
                            json_bool(request->body, "group_terraform").value_or(false), reason);
                        if (!ok) response = {.status = 403, .body = "{\"error\":\"" + json_escape(reason) + "\"}"};
                        else { audit_->append(user->id, "parcel.policy", *parcel_id); response.body = "{\"status\":\"updated\"}"; }
                    }
                } else if (method == "GET" && path == "/v1/estates") {
                    auto [session, user] = auth();
                    if (!session || !user) response = {.status = 401, .body = "{\"error\":\"invalid-session\"}"};
                    else response.body = "{\"estates\":" + estates_json(estates_->list_for_user(user->id)) + "}";
                } else if (method == "POST" && path == "/v1/estates") {
                    auto [session, user] = auth();
                    const auto name = json_string(request->body, "name");
                    if (!session || !user) response = {.status = 401, .body = "{\"error\":\"invalid-session\"}"};
                    else if (!name) response = {.status = 400, .body = "{\"error\":\"missing-name\"}"};
                    else { std::string reason; const auto estate = estates_->create(user->id, *name, reason); if(!estate) response={.status=400,.body="{\"error\":\""+json_escape(reason)+"\"}"}; else { audit_->append(user->id,"estate.create",estate->id,estate->name); response.status=201; response.body="{\"estate_id\":\""+estate->id+"\"}"; } }
                } else if (method == "POST" && path == "/v1/estates/managers") {
                    auto [session, user] = auth();
                    const auto estate_id=json_string(request->body,"estate_id"), manager=json_string(request->body,"user_id"), action=json_string(request->body,"action");
                    if(!session||!user) response={.status=401,.body="{\"error\":\"invalid-session\"}"};
                    else if(!estate_id||!manager||!action||!identities_->find_by_id(*manager)) response={.status=400,.body="{\"error\":\"invalid-manager-fields\"}"};
                    else { std::string reason; bool ok=*action=="add"?estates_->add_manager(user->id,*estate_id,*manager,reason):(*action=="remove"?estates_->remove_manager(user->id,*estate_id,*manager,reason):false); if(!ok) response={.status=403,.body="{\"error\":\""+json_escape(reason.empty()?"invalid-action":reason)+"\"}"}; else {audit_->append(user->id,"estate.manager."+*action,*estate_id,*manager);response.body="{\"status\":\"ok\"}";} }
                } else if (method == "POST" && path == "/v1/estates/regions") {
                    auto [session, user] = auth(); const auto estate_id=json_string(request->body,"estate_id"), region_id=json_string(request->body,"region_id");
                    if(!session||!user) response={.status=401,.body="{\"error\":\"invalid-session\"}"};
                    else if(!estate_id||!region_id||!regions_->find(*region_id)) response={.status=400,.body="{\"error\":\"invalid-region-fields\"}"};
                    else {std::string reason;if(!estates_->attach_region(user->id,*estate_id,*region_id,reason))response={.status=403,.body="{\"error\":\""+json_escape(reason)+"\"}"};else{audit_->append(user->id,"estate.region.attach",*estate_id,*region_id);response.body="{\"status\":\"attached\"}";}}
                } else if (method == "POST" && path == "/v1/estates/regions/policy") {
                    auto [session, user] = auth(); const auto estate_id=json_string(request->body,"estate_id"), region_id=json_string(request->body,"region_id");
                    if(!session||!user) response={.status=401,.body="{\"error\":\"invalid-session\"}"};
                    else if(!estate_id||!region_id) response={.status=400,.body="{\"error\":\"invalid-policy-fields\"}"};
                    else { RegionEstatePolicy policy{.region_id=*region_id,.estate_id=*estate_id,.public_access=json_bool(request->body,"public_access").value_or(true),.allow_fly=json_bool(request->body,"allow_fly").value_or(true),.allow_scripts=json_bool(request->body,"allow_scripts").value_or(true),.allow_voice=json_bool(request->body,"allow_voice").value_or(true),.max_agents=static_cast<std::uint32_t>(std::clamp(json_number(request->body,"max_agents").value_or(100.0),1.0,1000.0)),.maturity=static_cast<std::uint8_t>(std::clamp(json_number(request->body,"maturity").value_or(0.0),0.0,2.0)),.landing_x=json_number(request->body,"landing_x").value_or(128.0),.landing_y=json_number(request->body,"landing_y").value_or(128.0),.landing_z=json_number(request->body,"landing_z").value_or(0.0)}; std::string reason; if(!estates_->update_region_policy(user->id,policy,reason)) response={.status=403,.body="{\"error\":\""+json_escape(reason)+"\"}"}; else {audit_->append(user->id,"estate.region.policy",*estate_id,*region_id);response.body="{\"status\":\"updated\"}";} }
                } else if (method == "GET" && path.starts_with("/v1/regions/") && path.ends_with("/estate")) {
                    constexpr std::string_view prefix="/v1/regions/", suffix="/estate"; const auto id=path.substr(prefix.size(),path.size()-prefix.size()-suffix.size()); const auto policy=estates_->policy_for_region(id); if(!policy) response={.status=404,.body="{\"error\":\"estate-policy-not-found\"}"}; else response.body="{\"policies\":"+estate_policies_json({*policy})+"}";
                } else if (method == "GET" && path == "/v1/landmarks") {
                    auto [session,user]=auth(); if(!session||!user)response={.status=401,.body="{\"error\":\"invalid-session\"}"};else response.body="{\"landmarks\":"+landmarks_json(landmarks_->list_for_user(user->id))+"}";
                } else if (method == "POST" && path == "/v1/landmarks") {
                    auto [session,user]=auth(); const auto name=json_string(request->body,"name"),region_id=json_string(request->body,"region_id"); if(!session||!user)response={.status=401,.body="{\"error\":\"invalid-session\"}"};else if(!name||!region_id||!regions_->find(*region_id))response={.status=400,.body="{\"error\":\"invalid-landmark-fields\"}"};else{std::string reason;auto item=landmarks_->create(user->id,*name,*region_id,json_number(request->body,"x").value_or(128.0),json_number(request->body,"y").value_or(128.0),json_number(request->body,"z").value_or(0.0),reason);if(!item)response={.status=400,.body="{\"error\":\""+json_escape(reason)+"\"}"};else{audit_->append(user->id,"landmark.create",item->id,item->region_id);response.status=201;response.body="{\"landmark\":"+landmark_json(*item)+"}";}}
                } else if (method == "POST" && path == "/v1/landmarks/remove") {
                    auto [session,user]=auth(); const auto id=json_string(request->body,"landmark_id"); if(!session||!user)response={.status=401,.body="{\"error\":\"invalid-session\"}"};else if(!id||!landmarks_->remove(user->id,*id))response={.status=404,.body="{\"error\":\"landmark-not-found\"}"};else{audit_->append(user->id,"landmark.remove",*id);response.body="{\"status\":\"removed\"}";}
                } else if (method == "GET" && path == "/v1/notifications") {
                    auto [session,user]=auth(); if(!session||!user)response={.status=401,.body="{\"error\":\"invalid-session\"}"};else response.body="{\"unread\":"+std::to_string(notifications_->unread_count(user->id))+",\"notifications\":"+notifications_json(notifications_->list_for_user(user->id))+"}";
                } else if (method == "POST" && path == "/v1/notifications/read") {
                    auto [session,user]=auth(); const auto id=json_string(request->body,"notification_id"); if(!session||!user)response={.status=401,.body="{\"error\":\"invalid-session\"}"};else if(!id||!notifications_->mark_read(user->id,*id))response={.status=404,.body="{\"error\":\"notification-not-found\"}"};else response.body="{\"status\":\"read\"}";
                } else if (method == "GET" && path.starts_with("/v1/groups/") && path.ends_with("/channel")) {
                    auto [session,user]=auth(); constexpr std::string_view prefix="/v1/groups/",suffix="/channel"; const auto id=path.substr(prefix.size(),path.size()-prefix.size()-suffix.size()); if(!session||!user)response={.status=401,.body="{\"error\":\"invalid-session\"}"};else if(!groups_->is_member(id,user->id))response={.status=403,.body="{\"error\":\"not-group-member\"}"};else response.body="{\"posts\":"+group_posts_json(group_channels_->list(id))+"}";
                } else if (method == "POST" && path == "/v1/groups/channel") {
                    auto [session,user]=auth(); const auto group_id=json_string(request->body,"group_id"),kind=json_string(request->body,"kind"),title=json_string(request->body,"title"),text=json_string(request->body,"text"); if(!session||!user)response={.status=401,.body="{\"error\":\"invalid-session\"}"};else if(!group_id||!kind||!text)response={.status=400,.body="{\"error\":\"invalid-group-post-fields\"}"};else if(!groups_->is_member(*group_id,user->id)||!groups_->has_power(*group_id,user->id,*kind=="notice"?GroupPower::notice:GroupPower::chat))response={.status=403,.body="{\"error\":\"permission-denied\"}"};else{std::string reason;auto post=group_channels_->send(*group_id,user->id,*kind,title.value_or(""),*text,reason);if(!post)response={.status=400,.body="{\"error\":\""+json_escape(reason)+"\"}"};else{if(*kind=="notice")for(const auto&m:groups_->members(*group_id))if(m.user_id!=user->id)notifications_->push(m.user_id,"group_notice",title.value_or("Group notice"),*text,*group_id);audit_->append(user->id,"group."+*kind,*group_id,post->id);response.status=201;response.body="{\"post_id\":\""+post->id+"\"}";}}
                } else if (method == "GET" && path == "/v1/presence") {
                    auto [session, user] = auth();
                    if (!session || !user) {
                        response = {.status = 401, .body = "{\"error\":\"invalid-session\"}"};
                    } else response.body = "{\"presences\":" + presences_json(presences_->list()) + "}";
                } else if (method == "GET" && path == "/v1/social/friends") {
                    auto [session, user] = auth();
                    if (!session || !user) {
                        response = {.status = 401, .body = "{\"error\":\"invalid-session\"}"};
                    } else response.body = "{\"friends\":" +
                                            friends_json(friends_->list_for_user(user->id), user->id) + "}";
                } else if (method == "POST" && path == "/v1/social/friends/request") {
                    auto [session, user] = auth();
                    const auto target_id = json_string(request->body, "user_id");
                    if (!session || !user) {
                        response = {.status = 401, .body = "{\"error\":\"invalid-session\"}"};
                    } else if (!target_id || !identities_->find_by_id(*target_id)) {
                        response = {.status = 404, .body = "{\"error\":\"user-not-found\"}"};
                    } else {
                        std::string reason;
                        const auto relation = friends_->request(user->id, *target_id, reason);
                        if (!relation) response = {.status = 409, .body = "{\"error\":\"" + json_escape(reason) + "\"}"};
                        else {
                            notifications_->push(*target_id, "friend_request", "Friend request", user->display_name + " sent you a friend request.", relation->id);
                            response.status = 201;
                            response.body = "{\"status\":\"pending\",\"relation_id\":\"" + relation->id + "\"}";
                        }
                    }
                } else if (method == "POST" && path == "/v1/social/friends/accept") {
                    auto [session, user] = auth();
                    const auto other_id = json_string(request->body, "user_id");
                    if (!session || !user) {
                        response = {.status = 401, .body = "{\"error\":\"invalid-session\"}"};
                    } else if (!other_id) {
                        response = {.status = 400, .body = "{\"error\":\"missing-user-id\"}"};
                    } else {
                        std::string reason;
                        const auto relation = friends_->accept(user->id, *other_id, reason);
                        if (!relation) response = {.status = 409, .body = "{\"error\":\"" + json_escape(reason) + "\"}"};
                        else { notifications_->push(*other_id, "friend_accept", "Friend request accepted", user->display_name + " accepted your friend request.", relation->id); response.body = "{\"status\":\"accepted\",\"relation_id\":\"" + relation->id + "\"}"; }
                    }
                } else if (method == "POST" && path == "/v1/social/friends/remove") {
                    auto [session, user] = auth();
                    const auto other_id = json_string(request->body, "user_id");
                    if (!session || !user) {
                        response = {.status = 401, .body = "{\"error\":\"invalid-session\"}"};
                    } else if (!other_id || !friends_->remove(user->id, *other_id)) {
                        response = {.status = 404, .body = "{\"error\":\"friend-relation-not-found\"}"};
                    } else response.body = "{\"status\":\"removed\"}";
                } else if (method == "GET" && path == "/v1/social/messages") {
                    auto [session, user] = auth();
                    if (!session || !user) {
                        response = {.status = 401, .body = "{\"error\":\"invalid-session\"}"};
                    } else {
                        response.body = "{\"unread\":" + std::to_string(messages_->unread_count(user->id)) +
                                        ",\"messages\":" + messages_json(messages_->list_for_user(user->id), user->id) + "}";
                    }
                } else if (method == "POST" && path == "/v1/social/messages") {
                    auto [session, user] = auth();
                    const auto recipient = json_string(request->body, "recipient_id");
                    const auto text = json_string(request->body, "text");
                    if (!session || !user) {
                        response = {.status = 401, .body = "{\"error\":\"invalid-session\"}"};
                    } else if (!recipient || !text || !identities_->find_by_id(*recipient)) {
                        response = {.status = 400, .body = "{\"error\":\"invalid-message-fields\"}"};
                    } else if (!friends_->are_friends(user->id, *recipient)) {
                        response = {.status = 403, .body = "{\"error\":\"recipient-not-friend\"}"};
                    } else {
                        std::string reason;
                        const auto message = messages_->send(user->id, *recipient, *text, reason);
                        if (!message) response = {.status = 400, .body = "{\"error\":\"" + json_escape(reason) + "\"}"};
                        else {
                            notifications_->push(*recipient, "direct_message", "Message from " + user->display_name, *text, message->id);
                            response.status = 201;
                            response.body = "{\"message_id\":\"" + message->id + "\",\"sent_unix\":" +
                                            std::to_string(message->sent_unix) + "}";
                        }
                    }
                } else if (method == "POST" && path == "/v1/social/messages/read") {
                    auto [session, user] = auth();
                    const auto message_id = json_string(request->body, "message_id");
                    if (!session || !user) {
                        response = {.status = 401, .body = "{\"error\":\"invalid-session\"}"};
                    } else if (!message_id || !messages_->mark_read(user->id, *message_id)) {
                        response = {.status = 404, .body = "{\"error\":\"message-not-found\"}"};
                    } else response.body = "{\"status\":\"read\"}";
                } else if (method == "GET" && path == "/v1/social/stats") {
                    auto [session, user] = auth();
                    if (!session || !user) {
                        response = {.status = 401, .body = "{\"error\":\"invalid-session\"}"};
                    } else {
                        const auto relations = friends_->list_for_user(user->id);
                        std::size_t accepted = 0;
                        std::size_t pending = 0;
                        for (const auto& relation : relations) {
                            if (relation.status == "accepted") ++accepted;
                            else if (relation.status == "pending") ++pending;
                        }
                        response.body = "{\"friends\":" + std::to_string(accepted) +
                                        ",\"pending\":" + std::to_string(pending) +
                                        ",\"unread_messages\":" +
                                        std::to_string(messages_->unread_count(user->id)) +
                                        ",\"unread_notifications\":" +
                                        std::to_string(notifications_->unread_count(user->id)) + "}";
                    }
                } else if (method == "POST" && path == "/v1/assets/transfer") {
                    auto [session, user] = auth();
                    const auto asset_id = json_string(request->body, "asset_id");
                    const auto recipient = json_string(request->body, "recipient_id");
                    const bool keep_copy = json_bool(request->body, "keep_copy").value_or(true);
                    if (!session || !user) response = {.status = 401, .body = "{\"error\":\"invalid-session\"}"};
                    else if (!asset_id || !recipient || !identities_->find_by_id(*recipient)) response = {.status = 400, .body = "{\"error\":\"invalid-transfer\"}"};
                    else {
                        std::string reason;
                        const auto moved = assets_->transfer(*asset_id, user->id, *recipient, keep_copy, reason);
                        if (!moved) response = {.status = 403, .body = "{\"error\":\"" + json_escape(reason) + "\"}"};
                        else {
                            const auto root = inventory_->ensure_root(*recipient);
                            const auto item = inventory_->create_item(*recipient, root.id, moved->id, moved->name, reason);
                            audit_->append(user->id, "asset.transfer", moved->id, *recipient);
                            response.body = "{\"asset\":" + asset_json(*moved) + ",\"inventory_delivered\":" + (item ? "true" : "false") + "}";
                        }
                    }
                } else if (method == "GET" && path == "/v1/assets") {
                    auto [session, user] = auth();
                    if (!session || !user) response = {.status = 401, .body = "{\"error\":\"invalid-session\"}"};
                    else response.body = "{\"assets\":" + assets_json(assets_->list_for_user(user->id)) + "}";
                } else if (method == "POST" && path == "/v1/assets") {
                    auto [session, user] = auth();
                    const auto name = json_string(request->body, "name");
                    const auto mime = json_string(request->body, "mime_type");
                    const auto encoded = json_string(request->body, "data_base64");
                    if (!session || !user) response = {.status = 401, .body = "{\"error\":\"invalid-session\"}"};
                    else if (!name || !mime || !encoded) response = {.status = 400, .body = "{\"error\":\"invalid-json-fields\"}"};
                    else {
                        try {
                            auto data = security::base64_decode(*encoded);
                            std::string reason;
                            const auto asset = assets_->create(user->id, *name, *mime, std::move(data), reason);
                            if (!asset) response = {.status = 400, .body = "{\"error\":\"" + json_escape(reason) + "\"}"};
                            else {
                                response.status = 201;
                                response.body = "{\"asset\":" + asset_json(*asset) + "}";
                            }
                        } catch (const std::exception&) {
                            response = {.status = 400, .body = "{\"error\":\"invalid-asset-data\"}"};
                        }
                    }
                } else if (method == "GET" && path.starts_with("/v1/assets/") && path.size() > 11) {
                    auto [session, user] = auth();
                    if (!session || !user) response = {.status = 401, .body = "{\"error\":\"invalid-session\"}"};
                    else {
                        const auto id = path.substr(11);
                        const auto asset = assets_->find(id);
                        const auto data = asset ? assets_->read(id, user->id) : std::nullopt;
                        if (!asset || !data) response = {.status = 404, .body = "{\"error\":\"asset-not-found\"}"};
                        else response.body = "{\"asset\":" + asset_json(*asset) +
                                             ",\"data_base64\":\"" + security::base64_encode(*data) + "\"}";
                    }
                } else if (method == "GET" && path == "/v1/inventory") {
                    auto [session, user] = auth();
                    if (!session || !user) response = {.status = 401, .body = "{\"error\":\"invalid-session\"}"};
                    else response.body = inventory_json(inventory_->list(user->id));
                } else if (method == "POST" && path == "/v1/inventory/folders") {
                    auto [session, user] = auth();
                    const auto name = json_string(request->body, "name");
                    const auto parent = json_string(request->body, "parent_id");
                    if (!session || !user) response = {.status = 401, .body = "{\"error\":\"invalid-session\"}"};
                    else if (!name) response = {.status = 400, .body = "{\"error\":\"missing-name\"}"};
                    else {
                        std::string reason;
                        const auto folder = inventory_->create_folder(user->id, parent.value_or(""), *name, reason);
                        if (!folder) response = {.status = 400, .body = "{\"error\":\"" + json_escape(reason) + "\"}"};
                        else {
                            response.status = 201;
                            response.body = "{\"folder\":{\"id\":\"" + folder->id +
                                            "\",\"parent_id\":\"" + folder->parent_id +
                                            "\",\"name\":\"" + json_escape(folder->name) + "\"}}";
                        }
                    }
                } else if (method == "POST" && path == "/v1/inventory/items") {
                    auto [session, user] = auth();
                    const auto name = json_string(request->body, "name");
                    const auto parent = json_string(request->body, "parent_id");
                    const auto asset_id = json_string(request->body, "asset_id");
                    if (!session || !user) response = {.status = 401, .body = "{\"error\":\"invalid-session\"}"};
                    else if (!name || !asset_id) response = {.status = 400, .body = "{\"error\":\"invalid-json-fields\"}"};
                    else {
                        const auto asset = assets_->find(*asset_id);
                        if (!asset || asset->owner_user_id != user->id) {
                            response = {.status = 403, .body = "{\"error\":\"asset-not-owned\"}"};
                        } else {
                            std::string reason;
                            const auto item = inventory_->create_item(user->id, parent.value_or(""), *asset_id, *name, reason);
                            if (!item) response = {.status = 400, .body = "{\"error\":\"" + json_escape(reason) + "\"}"};
                            else {
                                response.status = 201;
                                response.body = "{\"item\":{\"id\":\"" + item->id +
                                                "\",\"asset_id\":\"" + item->asset_id +
                                                "\",\"name\":\"" + json_escape(item->name) + "\"}}";
                            }
                        }
                    }
                } else if (method == "GET" &&
                           path == "/v1/admin/roles") {
                    if (!admin_allowed("administrator")) {
                        response = {
                            .status = 403,
                            .body = "{\"error\":\"admin-authorization-required\"}"};
                    } else {
                        const auto roles = admin_roles_->list();
                        std::ostringstream out;
                        out << "{\"roles\":[";
                        for (std::size_t index = 0; index < roles.size(); ++index) {
                            if (index != 0U) out << ',';
                            const auto& role = roles[index];
                            out << "{\"user_id\":\"" << json_escape(role.user_id)
                                << "\",\"role\":\"" << json_escape(role.role_name)
                                << "\",\"granted_by\":\"" << json_escape(role.granted_by)
                                << "\",\"granted_unix\":" << role.granted_unix << "}";
                        }
                        out << "]}";
                        response.body = out.str();
                    }
                } else if (method == "POST" &&
                           path == "/v1/admin/roles/grant") {
                    if (!admin_allowed("administrator")) {
                        response = {
                            .status = 403,
                            .body = "{\"error\":\"admin-authorization-required\"}"};
                    } else {
                        const auto user_id =
                            json_string(request->body, "user_id");
                        const auto role =
                            json_string(request->body, "role");
                        if (!user_id || !role ||
                            !identities_->find_by_id(*user_id)) {
                            response = {
                                .status = 400,
                                .body = "{\"error\":\"invalid-admin-role-fields\"}"};
                        } else {
                            std::string reason;
                            if (!admin_roles_->grant(
                                    *user_id, *role, admin_actor(), reason)) {
                                response = {
                                    .status = 400,
                                    .body = "{\"error\":\"" +
                                            json_escape(reason) + "\"}"};
                            } else {
                                audit_->append(
                                    admin_actor(), "admin.role.grant",
                                    *user_id, *role);
                                response.body =
                                    "{\"status\":\"granted\"}";
                            }
                        }
                    }
                } else if (method == "POST" &&
                           path == "/v1/admin/roles/revoke") {
                    if (!admin_allowed("administrator")) {
                        response = {
                            .status = 403,
                            .body = "{\"error\":\"admin-authorization-required\"}"};
                    } else {
                        const auto user_id =
                            json_string(request->body, "user_id");
                        if (!user_id || !admin_roles_->revoke(*user_id)) {
                            response = {
                                .status = 404,
                                .body = "{\"error\":\"admin-role-not-found\"}"};
                        } else {
                            audit_->append(
                                admin_actor(), "admin.role.revoke",
                                *user_id);
                            response.body =
                                "{\"status\":\"revoked\"}";
                        }
                    }
                } else if (method == "GET" && path == "/v1/admin/moderation") {
                    if (!admin_allowed("moderator")) response = {.status = 403, .body = "{\"error\":\"admin-authorization-required\"}"};
                    else response.body = "{\"bans\":" + bans_json(moderation_->list()) + "}";
                } else if (method == "POST" && path == "/v1/admin/moderation/ban") {
                    if (!admin_allowed("moderator")) response = {.status = 403, .body = "{\"error\":\"admin-authorization-required\"}"};
                    else {
                        const auto user_id = json_string(request->body, "user_id");
                        const auto scope = json_string(request->body, "scope");
                        const auto scope_id = json_string(request->body, "scope_id");
                        const auto reason = json_string(request->body, "reason");
                        const auto expires = static_cast<std::int64_t>(json_number(request->body, "expires_unix").value_or(0.0));
                        std::string error;
                        if (!user_id || !scope || !identities_->find_by_id(*user_id)) response = {.status = 400, .body = "{\"error\":\"invalid-ban\"}"};
                        else {
                            const auto ban = moderation_->ban(admin_actor(), *user_id, *scope, scope_id.value_or(""), reason.value_or(""), expires, error);
                            if (!ban) response = {.status = 400, .body = "{\"error\":\"" + json_escape(error) + "\"}"};
                            else { audit_->append(admin_actor(), "moderation.ban", *user_id, *scope + ":" + scope_id.value_or("")); response.status = 201; response.body = "{\"ban_id\":\"" + ban->id + "\"}"; }
                        }
                    }
                } else if (method == "POST" && path == "/v1/admin/moderation/unban") {
                    if (!admin_allowed("moderator")) response = {.status = 403, .body = "{\"error\":\"admin-authorization-required\"}"};
                    else {
                        const auto ban_id = json_string(request->body, "ban_id");
                        if (!ban_id || !moderation_->unban(*ban_id)) response = {.status = 404, .body = "{\"error\":\"ban-not-found\"}"};
                        else { audit_->append(admin_actor(), "moderation.unban", *ban_id); response.body = "{\"status\":\"removed\"}"; }
                    }
                } else if (method == "GET" && path == "/v1/admin/audit") {
                    if (!admin_allowed("moderator")) response = {.status = 403, .body = "{\"error\":\"admin-authorization-required\"}"};
                    else response.body = "{\"events\":" + audit_json(audit_->recent()) + "}";
                } else if (path == "/v1/auth/register" || path == "/v1/auth/login" ||
                           path == "/v1/auth/logout" || path == "/v1/viewer/session" ||
                           path == "/v1/viewer/teleport" || path == "/v1/viewer/handoff" ||
                           path == "/v1/viewer/handoff/reserve" ||
                           path == "/v1/viewer/handoff/complete" ||
                           path == "/v1/viewer/handoff/rollback" ||
                           path == "/v1/world/object-crossings" ||
                           path == "/v1/world/object-crossings/rollback" ||
                           path == "/v1/groups" || path == "/v1/groups/members" ||
                           path == "/v1/groups/role" || path == "/v1/groups/remove" ||
                           path == "/v1/parcels" || path == "/v1/parcels/policy" ||
                           path == "/v1/estates" || path == "/v1/estates/managers" ||
                           path == "/v1/estates/regions" || path == "/v1/estates/regions/policy" ||
                           path == "/v1/landmarks" || path == "/v1/landmarks/remove" ||
                           path == "/v1/notifications/read" || path == "/v1/groups/channel" ||
                           path == "/v1/assets/transfer" || path == "/v1/admin/roles/grant" ||
                           path == "/v1/admin/roles/revoke" || path == "/v1/admin/moderation/ban" ||
                           path == "/v1/admin/moderation/unban" || path == "/v1/social/friends/request" ||
                           path == "/v1/social/friends/accept" || path == "/v1/social/friends/remove" ||
                           path == "/v1/social/messages" || path == "/v1/social/messages/read" ||
                           path == "/v1/assets" || path == "/v1/inventory/folders" ||
                           path == "/v1/inventory/items") {
                    response = {.status = 405, .body = "{\"error\":\"method-not-allowed\"}"};
                } else {
                    response = {.status = 404, .body = "{\"error\":\"not-found\"}"};
                }
                send_response(fd, response);
            } catch (const std::exception& error) {
                common::log(common::LogLevel::warning, "core.admin.client", error.what());
                send_response(fd, {.status = 500, .body = "{\"error\":\"internal-error\"}"});
            }
            platform::close_socket(fd);
        }
    } catch (const std::exception& error) {
        if (running_) common::log(common::LogLevel::error, "core.admin", error.what());
    }
}

} // namespace opengenesis::core
