#pragma once

#include "opengenesis/core/asset_store.hpp"
#include "opengenesis/avatar/appearance_store.hpp"
#include "opengenesis/core/audit_store.hpp"
#include "opengenesis/core/admin_role_store.hpp"
#include "opengenesis/core/group_store.hpp"
#include "opengenesis/core/group_channel_store.hpp"
#include "opengenesis/core/notification_store.hpp"
#include "opengenesis/core/landmark_store.hpp"
#include "opengenesis/core/estate_store.hpp"
#include "opengenesis/core/moderation_store.hpp"
#include "opengenesis/core/parcel_store.hpp"
#include "opengenesis/core/friends_store.hpp"
#include "opengenesis/core/identity_store.hpp"
#include "opengenesis/core/inventory_store.hpp"
#include "opengenesis/core/message_store.hpp"
#include "opengenesis/core/presence_store.hpp"
#include "opengenesis/core/region_registry.hpp"
#include "opengenesis/core/session_store.hpp"
#include "opengenesis/core/world_registry.hpp"
#include "opengenesis/core/crossing_store.hpp"
#include "opengenesis/core/object_crossing_store.hpp"
#include "opengenesis/scripting/script_runtime.hpp"
#include "opengenesis/scripting/script_host.hpp"
#include "opengenesis/federation/runtime.hpp"
#include "opengenesis/compat/hypergrid/service.hpp"
#include "opengenesis/compat/hypergrid/session_store.hpp"
#include "opengenesis/compat/hypergrid/im_adapter.hpp"
#include "opengenesis/platform/socket.hpp"
#include "opengenesis/security/rate_limiter.hpp"
#include "opengenesis/storage/database.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>

namespace opengenesis::core {

class AdminHttpServer final {
public:
    AdminHttpServer(std::string address, std::uint16_t port,
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
                    std::size_t login_attempts_per_minute,
                    std::size_t registration_attempts_per_minute);
    ~AdminHttpServer();

    void start();
    void stop();

private:
    void run();

    std::string address_;
    std::uint16_t port_;
    std::shared_ptr<WorldRegistry> worlds_;
    std::shared_ptr<RegionRegistry> regions_;
    std::shared_ptr<IdentityStore> identities_;
    std::shared_ptr<SessionStore> sessions_;
    std::shared_ptr<AssetStore> assets_;
    std::shared_ptr<avatar::AppearanceStore> appearance_;
    std::shared_ptr<InventoryStore> inventory_;
    std::shared_ptr<PresenceStore> presences_;
    std::shared_ptr<FriendsStore> friends_;
    std::shared_ptr<MessageStore> messages_;
    std::shared_ptr<GroupStore> groups_;
    std::shared_ptr<ParcelStore> parcels_;
    std::shared_ptr<ModerationStore> moderation_;
    std::shared_ptr<AuditStore> audit_;
    std::shared_ptr<EstateStore> estates_;
    std::shared_ptr<LandmarkStore> landmarks_;
    std::shared_ptr<NotificationStore> notifications_;
    std::shared_ptr<GroupChannelStore> group_channels_;
    std::shared_ptr<CrossingStore> crossings_;
    std::shared_ptr<ObjectCrossingStore> object_crossings_;
    std::shared_ptr<scripting::ScriptRuntime> scripts_;
    std::shared_ptr<scripting::ScriptHost> script_host_;
    std::shared_ptr<federation::FederationRuntime> federation_runtime_;
    std::shared_ptr<compat::hypergrid::HypergridService> hypergrid_service_;
    std::shared_ptr<compat::hypergrid::HypergridSessionStore> hypergrid_sessions_;
    std::shared_ptr<compat::hypergrid::HypergridInstantMessageAdapter> hypergrid_im_;
    std::shared_ptr<storage::DatabasePool> database_;
    std::shared_ptr<AdminRoleStore> admin_roles_;
    security::RateLimiter auth_rate_limiter_;
    std::size_t login_attempts_per_minute_{12};
    std::size_t registration_attempts_per_minute_{6};
    std::string admin_api_key_;
    std::string scene_ticket_secret_;
    std::chrono::seconds scene_ticket_lifetime_;
    std::chrono::steady_clock::time_point started_at_{std::chrono::steady_clock::now()};
    std::atomic_bool running_{false};
    std::thread thread_;
    platform::SocketHandle listen_fd_{platform::kInvalidSocket};
};

} // namespace opengenesis::core
