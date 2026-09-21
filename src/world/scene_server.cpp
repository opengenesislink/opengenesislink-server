#include "opengenesis/world/scene_server.hpp"

#include "opengenesis/common/log.hpp"
#include "opengenesis/network/tcp.hpp"
#include "opengenesis/protocol/frame.hpp"
#include "opengenesis/security/scene_ticket.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace opengenesis::world {
namespace protocol = opengenesis::protocol;

class SceneAuthContext final {
public:
    explicit SceneAuthContext(std::string secret) : secret_(std::move(secret)) {
        if (secret_.size() < 32) throw std::runtime_error("scene ticket secret must be at least 32 bytes");
    }

    std::optional<security::SceneTicketClaims> consume(std::string_view token,
                                                       std::string_view region) {
        const auto claims = security::verify_scene_ticket(secret_, token, region);
        if (!claims) return std::nullopt;
        const auto now = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        std::scoped_lock lock(mutex_);
        for (auto it = used_.begin(); it != used_.end();) {
            if (it->second <= now) it = used_.erase(it);
            else ++it;
        }
        if (used_.contains(claims->nonce)) return std::nullopt;
        used_[claims->nonce] = claims->expires_unix;
        return claims;
    }

private:
    std::string secret_;
    std::mutex mutex_;
    std::unordered_map<std::string, std::int64_t> used_;
};

namespace {

std::string field(const std::string& payload, const std::string& key) {
    std::istringstream input(payload);
    std::string line;
    while (std::getline(input, line)) {
        const auto split = line.find('=');
        if (split != std::string::npos && line.substr(0, split) == key) return line.substr(split + 1);
    }
    return {};
}

double number(const std::string& payload, const std::string& key, const double fallback) {
    const auto value = field(payload, key);
    if (value.empty()) return fallback;
    try {
        const auto parsed = std::stod(value);
        return std::isfinite(parsed) ? parsed : fallback;
    } catch (...) {
        return fallback;
    }
}

std::uint64_t integer(const std::string& payload, const std::string& key,
                      const std::uint64_t fallback = 0) {
    const auto value = field(payload, key);
    if (value.empty()) return fallback;
    try {
        return std::stoull(value);
    } catch (...) {
        return fallback;
    }
}

std::string clean(std::string value, const std::size_t max_length = 96) {
    value.erase(std::remove_if(value.begin(), value.end(), [](const char c) {
                    return c == '\n' || c == '\r' || c == '|';
                }),
                value.end());
    if (value.size() > max_length) value.resize(max_length);
    return value;
}

std::vector<std::string> groups_from_claims(const security::SceneTicketClaims& claims) {
    std::vector<std::string> groups;
    std::size_t start = 0;
    while (start <= claims.group_ids_csv.size()) {
        const auto end = claims.group_ids_csv.find(',', start);
        const auto token = claims.group_ids_csv.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (!token.empty()) groups.push_back(token);
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return groups;
}

bool can_modify_object(const Entity& entity, const security::SceneTicketClaims& claims) {
    if (entity.owner_user_id == claims.user_id) return core::has_permission(entity.owner_permissions, core::perm_modify);
    if (core::has_permission(entity.everyone_permissions, core::perm_modify)) return true;
    return !entity.group_id.empty() && security::scene_ticket_has_group(claims, entity.group_id) &&
           core::has_permission(entity.group_permissions, core::perm_modify);
}

Transform transform_from_payload(const std::string& payload, const Transform& fallback = {}) {
    Transform transform = fallback;
    transform.position.x = number(payload, "x", fallback.position.x);
    transform.position.y = number(payload, "y", fallback.position.y);
    transform.position.z = number(payload, "z", fallback.position.z);
    transform.rotation.x = number(payload, "rx", fallback.rotation.x);
    transform.rotation.y = number(payload, "ry", fallback.rotation.y);
    transform.rotation.z = number(payload, "rz", fallback.rotation.z);
    transform.scale.x = std::max(0.01, number(payload, "sx", fallback.scale.x));
    transform.scale.y = std::max(0.01, number(payload, "sy", fallback.scale.y));
    transform.scale.z = std::max(0.01, number(payload, "sz", fallback.scale.z));
    return transform;
}

std::string serialize_snapshot(const RegionRuntime& region) {
    const auto entities = region.snapshot_entities();
    std::ostringstream output;
    output << std::fixed << std::setprecision(3) << "region=" << region.id() << '\n'
           << "sequence=" << region.latest_sequence() << '\n'
           << "terrain_width=" << region.terrain().width() << '\n'
           << "terrain_height=" << region.terrain().height() << '\n'
           << "terrain_cell_size=" << region.terrain().cell_size() << '\n'
           << "terrain_revision=" << region.terrain().revision() << '\n'
           << "water_height=" << region.water_height() << '\n'
           << "entity_count=" << entities.size() << '\n';
    for (const auto& entity : entities) {
        const auto& transform = entity.transform;
        output << "entity=" << entity.id << '|' << entity_kind_name(entity.kind) << '|'
               << clean(entity.name) << '|' << transform.position.x << '|' << transform.position.y
               << '|' << transform.position.z << '|' << transform.rotation.x << '|'
               << transform.rotation.y << '|' << transform.rotation.z << '|' << transform.scale.x
               << '|' << transform.scale.y << '|' << transform.scale.z << '|'
               << clean(entity.owner_user_id, 64) << '|' << clean(entity.group_id, 64) << '|'
               << entity.owner_permissions << '|' << entity.group_permissions << '|'
               << entity.everyone_permissions << '|'
               << entity.parent_entity_id << '|' << entity.link_number << '|'
               << (entity.physics_body != 0 ? 1 : 0) << '|'
               << clean(entity.floating_text, 512);
        if (entity.kind == EntityKind::object) {
            const auto transfer = region.export_object(entity.id);
            const physics::Vec3 velocity =
                transfer ? transfer->velocity : physics::Vec3{};
            const physics::Vec3 angular =
                transfer ? transfer->angular_velocity : physics::Vec3{};
            const auto body = region.physics_body_state(entity.id);
            output << '|' << velocity.x << '|' << velocity.y << '|' << velocity.z
                   << '|' << angular.x << '|' << angular.y << '|' << angular.z
                   << '|' << (body ? body->mass : 0.0)
                   << '|' << (body ? body->restitution : 0.0)
                   << '|' << (body ? body->friction : 0.0)
                   << '|' << (body ? body->buoyancy : 0.0);
        } else {
            const auto body = region.physics_body_state(entity.id);
            output << "|0|0|0|0|0|0"
                   << '|' << (body ? body->mass : 0.0)
                   << '|' << (body ? body->restitution : 0.0)
                   << '|' << (body ? body->friction : 0.0)
                   << '|' << (body ? body->buoyancy : 0.0);
        }
        output << '\n';
    }
    return output.str();
}

std::string serialize_events(
    const RegionRuntime& region,
    const std::uint64_t since,
    const std::size_t max_events = 256U) {
    const auto events = region.events_since(since, max_events);
    std::ostringstream output;
    output << std::fixed << std::setprecision(3) << "region=" << region.id() << '\n'
           << "from=" << since << '\n' << "latest=" << region.latest_sequence() << '\n'
           << "count=" << events.size() << '\n';
    for (const auto& event : events) {
        const auto& transform = event.transform;
        output << "event=" << event.sequence << '|' << event.type << '|' << event.entity_id << '|'
               << clean(event.text, 512) << '|' << transform.position.x << '|' << transform.position.y
               << '|' << transform.position.z << '|' << transform.rotation.x << '|'
               << transform.rotation.y << '|' << transform.rotation.z << '|' << transform.scale.x
               << '|' << transform.scale.y << '|' << transform.scale.z << '\n';
    }
    return output.str();
}

std::string serialize_region_metadata(const RegionRuntime& region) {
    const auto metrics = region.metrics();
    std::ostringstream out;
    out << std::fixed << std::setprecision(3)
        << "region=" << region.id() << '\n'
        << "terrain_width=" << region.terrain().width() << '\n'
        << "terrain_height=" << region.terrain().height() << '\n'
        << "terrain_cell_size=" << region.terrain().cell_size() << '\n'
        << "terrain_revision=" << region.terrain().revision() << '\n'
        << "water_height=" << region.water_height() << '\n'
        << "sequence=" << region.latest_sequence() << '\n'
        << "ticks=" << metrics.ticks << '\n'
        << "entities=" << metrics.entities << '\n'
        << "avatars=" << metrics.avatars << '\n'
        << "physics_bodies=" << metrics.physics_bodies << '\n'
        << "collision_contacts=" << metrics.collision_contacts << '\n'
        << "physics_constraints=" << metrics.physics_constraints << '\n'
        << "sim_fps=" << metrics.sim_fps << '\n';
    return out.str();
}

std::string serialize_parcel(const core::ParcelInfo& parcel) {
    std::ostringstream out;
    out << "parcel=" << clean(parcel.id, 128) << '|'
        << clean(parcel.name, 128) << '|'
        << clean(parcel.owner_user_id, 128) << '|'
        << clean(parcel.group_id, 128) << '|'
        << parcel.x1 << '|' << parcel.y1 << '|'
        << parcel.x2 << '|' << parcel.y2 << '|'
        << (parcel.public_entry ? 1 : 0) << '|'
        << (parcel.public_build ? 1 : 0) << '|'
        << (parcel.group_build ? 1 : 0) << '|'
        << (parcel.group_terraform ? 1 : 0) << '\n';
    return out.str();
}

std::string serialize_sync(
    const RegionRuntime& region,
    const std::uint64_t since,
    const std::size_t max_events) {
    const auto latest = region.latest_sequence();
    const bool snapshot =
        since == 0U || (latest > since && latest - since > 4096U);
    std::ostringstream out;
    if (snapshot) {
        out << "mode=snapshot\n";
        out << serialize_snapshot(region);
    } else {
        out << "mode=delta\n";
        out << serialize_events(region, since, max_events);
    }
    return out.str();
}

void send_capability_error(opengenesis::network::TcpSocket& socket, const protocol::Frame& frame,
                           std::string_view capability) {
    socket.send_frame({protocol::MessageType::error, frame.request_id,
                       protocol::payload_from_string("reason=missing-capability\ncapability=" +
                                                     std::string{capability} + "\n")});
}

void handle_client(opengenesis::network::TcpSocket socket,
                   std::unordered_map<std::string, std::shared_ptr<RegionRuntime>> regions,
                   std::shared_ptr<SceneAuthContext> auth,
                   std::shared_ptr<core::ParcelStore> parcels,
                   std::shared_ptr<core::ModerationStore> moderation) {
    std::shared_ptr<RegionRuntime> region;
    std::uint64_t avatar_id = 0;
    std::uint64_t last_client_move_sequence = 0;
    std::string user_id;
    security::SceneTicketClaims claims;
    try {
        const auto hello = socket.receive_frame();
        if (hello.type != protocol::MessageType::hello) throw std::runtime_error("scene HELLO required");
        socket.send_frame({protocol::MessageType::hello_ack, hello.request_id,
                           protocol::payload_from_string(
                               "protocol=1\nserver=opengenesis-scene\nauth=scene-ticket-v1\n"
                               "scene_contract=2\nmovement=avatar-reconcile-v1\n"
                               "sync=scene-sync-v1\nmetadata=region-metadata-v1,parcel-read-v1\n"
                               "capabilities=scene-capabilities-v2\n"
                               "object_runtime=linkset-v2,motion-v1,text-v1,physics-v2,collision-events-v1,interaction-v1\n")});

        const auto join = socket.receive_frame();
        if (join.type != protocol::MessageType::scene_join) throw std::runtime_error("SCENE_JOIN required");
        const auto body = protocol::payload_as_string(join);
        const auto region_id = field(body, "region");
        const auto region_it = regions.find(region_id);
        if (region_it == regions.end()) {
            socket.send_frame({protocol::MessageType::error, join.request_id,
                               protocol::payload_from_string("reason=unknown-region\n")});
            return;
        }
        const auto verified = auth->consume(field(body, "ticket"), region_id);
        if (!verified || !security::has_scene_capability(*verified, "scene.join")) {
            socket.send_frame({protocol::MessageType::error, join.request_id,
                               protocol::payload_from_string("reason=invalid-scene-ticket\n")});
            return;
        }
        claims = *verified;
        region = region_it->second;
        user_id = claims.user_id;
        moderation->reload();
        parcels->reload();
        const auto joined_groups = groups_from_claims(claims);
        if (moderation->is_banned(user_id, region_id)) {
            socket.send_frame({protocol::MessageType::error, join.request_id,
                               protocol::payload_from_string("reason=user-banned\n")});
            return;
        }
        Transform spawn{};
        spawn.position = {claims.spawn_x, claims.spawn_y, claims.spawn_z};
        if (!parcels->can_enter(region_id, spawn.position.x, spawn.position.y, user_id, joined_groups)) {
            socket.send_frame({protocol::MessageType::error, join.request_id,
                               protocol::payload_from_string("reason=parcel-entry-denied\n")});
            return;
        }
        avatar_id = region->spawn_avatar(user_id, clean(claims.display_name), spawn);

        std::ostringstream joined;
        joined << "status=joined\nregion=" << region->id() << "\nuser_id=" << user_id
               << "\navatar_id=" << avatar_id << "\nsequence=" << region->latest_sequence()
               << "\nscene_contract=2\nsync=scene-sync-v1\nmovement=avatar-reconcile-v1"
               << "\nterrain_revision=" << region->terrain().revision() << "\ncapabilities="
               << claims.capabilities << "\nhandoff_from=" << claims.handoff_from_region
               << "\ncrossing_id=" << claims.crossing_id
               << "\ngroups=" << claims.group_ids_csv << "\nspawn_x=" << claims.spawn_x
               << "\nspawn_y=" << claims.spawn_y << "\nspawn_z=" << claims.spawn_z << '\n';
        socket.send_frame({protocol::MessageType::scene_join_ack, join.request_id,
                           protocol::payload_from_string(joined.str())});

        while (true) {
            const auto frame = socket.receive_frame();
            const auto request = protocol::payload_as_string(frame);
            if (frame.type == protocol::MessageType::scene_snapshot_request) {
                if (!security::has_scene_capability(claims, "scene.read")) {
                    send_capability_error(socket, frame, "scene.read");
                    continue;
                }
                socket.send_frame({protocol::MessageType::scene_snapshot, frame.request_id,
                                   protocol::payload_from_string(serialize_snapshot(*region))});
            } else if (frame.type == protocol::MessageType::scene_events_request) {
                if (!security::has_scene_capability(claims, "scene.read")) {
                    send_capability_error(socket, frame, "scene.read");
                    continue;
                }
                socket.send_frame({protocol::MessageType::scene_events, frame.request_id,
                                   protocol::payload_from_string(
                                       serialize_events(*region, integer(request, "since")))});
            } else if (frame.type == protocol::MessageType::scene_sync_request) {
                if (!security::has_scene_capability(claims, "scene.sync")) {
                    send_capability_error(socket, frame, "scene.sync");
                    continue;
                }
                const auto since = integer(request, "since");
                const auto requested = integer(request, "max_events", 256U);
                const auto max_events = static_cast<std::size_t>(
                    std::clamp<std::uint64_t>(requested, 1U, 1024U));
                socket.send_frame({
                    protocol::MessageType::scene_sync,
                    frame.request_id,
                    protocol::payload_from_string(
                        serialize_sync(*region, since, max_events))});
            } else if (frame.type == protocol::MessageType::region_metadata_request) {
                if (!security::has_scene_capability(
                        claims, "scene.region.metadata")) {
                    send_capability_error(
                        socket, frame, "scene.region.metadata");
                    continue;
                }
                socket.send_frame({
                    protocol::MessageType::region_metadata,
                    frame.request_id,
                    protocol::payload_from_string(
                        serialize_region_metadata(*region))});
            } else if (frame.type == protocol::MessageType::parcel_info_request) {
                if (!security::has_scene_capability(
                        claims, "scene.parcel.read")) {
                    send_capability_error(
                        socket, frame, "scene.parcel.read");
                    continue;
                }
                parcels->reload();
                std::ostringstream out;
                const auto x_text = field(request, "x");
                const auto y_text = field(request, "y");
                if (!x_text.empty() || !y_text.empty()) {
                    const auto parcel = parcels->at(
                        region->id(),
                        number(request, "x", 128.0),
                        number(request, "y", 128.0));
                    out << "mode=point\ncount=" << (parcel ? 1 : 0) << '\n';
                    if (parcel) out << serialize_parcel(*parcel);
                } else {
                    const auto list = parcels->list_region(region->id());
                    out << "mode=region\ncount=" << list.size() << '\n';
                    for (const auto& parcel : list) {
                        out << serialize_parcel(parcel);
                    }
                }
                socket.send_frame({
                    protocol::MessageType::parcel_info,
                    frame.request_id,
                    protocol::payload_from_string(out.str())});
            } else if (frame.type == protocol::MessageType::avatar_reconcile) {
                if (!security::has_scene_capability(
                        claims, "scene.avatar.reconcile")) {
                    send_capability_error(
                        socket, frame, "scene.avatar.reconcile");
                    continue;
                }
                const auto client_sequence =
                    integer(request, "client_sequence");
                if (client_sequence == 0U ||
                    client_sequence <= last_client_move_sequence) {
                    socket.send_frame({
                        protocol::MessageType::error,
                        frame.request_id,
                        protocol::payload_from_string(
                            "reason=stale-client-sequence\n")});
                    continue;
                }
                const auto current = region->entity(avatar_id);
                if (!current) {
                    socket.send_frame({
                        protocol::MessageType::error,
                        frame.request_id,
                        protocol::payload_from_string(
                            "reason=avatar-not-found\n")});
                    continue;
                }
                auto transform =
                    transform_from_payload(request, current->transform);
                const physics::Vec3 velocity{
                    number(request, "vx", 0.0),
                    number(request, "vy", 0.0),
                    number(request, "vz", 0.0)};
                std::string boundary;
                const bool ok =
                    region->move_avatar(
                        avatar_id, transform, velocity, boundary);
                if (!ok) {
                    socket.send_frame({
                        protocol::MessageType::error,
                        frame.request_id,
                        protocol::payload_from_string(
                            "reason=avatar-move-rejected\n")});
                    continue;
                }
                last_client_move_sequence = client_sequence;
                const auto authoritative = region->entity(avatar_id);
                const auto body_state =
                    region->physics_body_state(avatar_id);
                const auto metrics = region->metrics();
                std::ostringstream response;
                response << std::fixed << std::setprecision(3)
                         << "status=reconciled\nclient_sequence="
                         << client_sequence
                         << "\nserver_sequence="
                         << region->latest_sequence()
                         << "\ntick=" << metrics.ticks
                         << "\nboundary=" << boundary;
                if (authoritative) {
                    response << "\nx=" << authoritative->transform.position.x
                             << "\ny=" << authoritative->transform.position.y
                             << "\nz=" << authoritative->transform.position.z
                             << "\nrx=" << authoritative->transform.rotation.x
                             << "\nry=" << authoritative->transform.rotation.y
                             << "\nrz=" << authoritative->transform.rotation.z;
                }
                const auto velocity_state =
                    body_state ? body_state->velocity : velocity;
                response << "\nvx=" << velocity_state.x
                         << "\nvy=" << velocity_state.y
                         << "\nvz=" << velocity_state.z << '\n';
                socket.send_frame({
                    protocol::MessageType::avatar_reconcile_ack,
                    frame.request_id,
                    protocol::payload_from_string(response.str())});
            } else if (frame.type == protocol::MessageType::avatar_move) {
                if (!security::has_scene_capability(claims, "scene.move")) {
                    send_capability_error(socket, frame, "scene.move");
                    continue;
                }
                const auto current = region->entity(avatar_id);
                if (!current) {
                    socket.send_frame({protocol::MessageType::error, frame.request_id,
                                       protocol::payload_from_string("reason=avatar-not-found\n")});
                    continue;
                }
                auto transform = transform_from_payload(request, current->transform);
                const physics::Vec3 velocity{number(request, "vx", 0.0), number(request, "vy", 0.0),
                                             number(request, "vz", 0.0)};
                std::string boundary;
                const bool ok = region->move_avatar(avatar_id, transform, velocity, boundary);
                std::ostringstream response;
                response << "status=" << (ok ? "moved" : "rejected") << "\nboundary=" << boundary
                         << "\nsequence=" << region->latest_sequence() << '\n';
                socket.send_frame({ok ? protocol::MessageType::avatar_move_ack
                                      : protocol::MessageType::error,
                                   frame.request_id, protocol::payload_from_string(response.str())});
            } else if (frame.type == protocol::MessageType::entity_create) {
                if (!security::has_scene_capability(claims, "scene.object.create")) {
                    send_capability_error(socket, frame, "scene.object.create");
                    continue;
                }
                auto transform = transform_from_payload(request);
                auto name = clean(field(request, "name"));
                const bool physical = field(request, "physical") != "false";
                parcels->reload();
                const auto groups = groups_from_claims(claims);
                if (!parcels->can_build(region->id(), transform.position.x, transform.position.y, user_id, groups)) {
                    socket.send_frame({protocol::MessageType::error, frame.request_id,
                                       protocol::payload_from_string("reason=parcel-build-denied\n")});
                    continue;
                }
                auto group_id = clean(field(request, "group_id"), 64);
                if (!group_id.empty() && !security::scene_ticket_has_group(claims, group_id)) group_id.clear();
                const auto group_permissions = static_cast<core::PermissionMask>(integer(request, "group_permissions"));
                const auto everyone_permissions = static_cast<core::PermissionMask>(integer(request, "everyone_permissions"));
                const auto id = region->spawn_object(name.empty() ? "Object" : name, transform, physical,
                                                     user_id, group_id, group_permissions, everyone_permissions);
                socket.send_frame({protocol::MessageType::entity_create_ack, frame.request_id,
                                   protocol::payload_from_string("status=created\nid=" +
                                                                 std::to_string(id) + "\n")});
            } else if (frame.type == protocol::MessageType::entity_update) {
                if (!security::has_scene_capability(claims, "scene.object.modify.own")) {
                    send_capability_error(socket, frame, "scene.object.modify.own");
                    continue;
                }
                const auto id = integer(request, "id");
                const auto current = region->entity(id);
                const auto next_transform = current ? transform_from_payload(request, current->transform) : Transform{};
                parcels->reload();
                const bool land_ok = current && parcels->can_build(region->id(), next_transform.position.x, next_transform.position.y, user_id, groups_from_claims(claims));
                const bool ok = current && current->kind == EntityKind::object && can_modify_object(*current, claims) && land_ok &&
                                region->update_transform(id, next_transform);
                socket.send_frame({ok ? protocol::MessageType::entity_update_ack
                                      : protocol::MessageType::error,
                                   frame.request_id,
                                   protocol::payload_from_string(
                                       ok ? "status=updated\n"
                                          : "reason=object-or-land-permission-denied\n")});
            } else if (frame.type == protocol::MessageType::entity_delete) {
                if (!security::has_scene_capability(claims, "scene.object.modify.own")) {
                    send_capability_error(socket, frame, "scene.object.modify.own");
                    continue;
                }
                const auto id = integer(request, "id");
                const auto current = region->entity(id);
                const bool ok = current && current->kind == EntityKind::object &&
                                can_modify_object(*current, claims) && region->remove_entity(id);
                socket.send_frame({ok ? protocol::MessageType::entity_delete_ack
                                      : protocol::MessageType::error,
                                   frame.request_id,
                                   protocol::payload_from_string(
                                       ok ? "status=deleted\n"
                                          : "reason=object-permission-denied\n")});
            } else if (frame.type == protocol::MessageType::entity_permissions) {
                if (!security::has_scene_capability(claims, "scene.object.permissions")) {
                    send_capability_error(socket, frame, "scene.object.permissions");
                    continue;
                }
                const auto id = integer(request, "id");
                const auto current = region->entity(id);
                auto group_id = clean(field(request, "group_id"), 64);
                const bool group_ok = group_id.empty() || security::scene_ticket_has_group(claims, group_id);
                const bool owner = current && current->kind == EntityKind::object && current->owner_user_id == user_id;
                const auto group_permissions = static_cast<core::PermissionMask>(integer(request, "group_permissions"));
                const auto everyone_permissions = static_cast<core::PermissionMask>(integer(request, "everyone_permissions"));
                const bool ok = owner && group_ok && region->set_object_permissions(id, std::move(group_id), group_permissions, everyone_permissions);
                socket.send_frame({ok ? protocol::MessageType::entity_permissions_ack : protocol::MessageType::error,
                                   frame.request_id, protocol::payload_from_string(ok ? "status=permissions-updated\n" : "reason=permission-update-denied\n")});
            } else if (frame.type == protocol::MessageType::entity_link) {
                if (!security::has_scene_capability(claims, "scene.object.link")) {
                    send_capability_error(socket, frame, "scene.object.link");
                    continue;
                }
                const auto action = field(request, "action");
                const auto root_id = integer(request, "root_id");
                const auto child_id = integer(request, "child_id");
                const auto root = region->entity(root_id);
                const auto child = region->entity(child_id);
                std::string reason;
                bool ok = false;
                if (action == "unlink") {
                    ok = child && child->kind == EntityKind::object &&
                         can_modify_object(*child, claims) &&
                         region->unlink_object(child_id, reason);
                } else {
                    ok = root && child &&
                         root->kind == EntityKind::object &&
                         child->kind == EntityKind::object &&
                         can_modify_object(*root, claims) &&
                         can_modify_object(*child, claims) &&
                         region->link_objects(root_id, child_id, reason);
                }
                socket.send_frame({
                    ok ? protocol::MessageType::entity_link_ack
                       : protocol::MessageType::error,
                    frame.request_id,
                    protocol::payload_from_string(
                        ok ? "status=updated\n"
                           : "reason=" +
                                 (reason.empty()
                                      ? std::string{"linkset-operation-denied"}
                                      : reason) +
                                 "\n")});
            } else if (frame.type == protocol::MessageType::entity_text) {
                if (!security::has_scene_capability(
                        claims, "scene.object.modify.own")) {
                    send_capability_error(
                        socket, frame, "scene.object.modify.own");
                    continue;
                }
                const auto id = integer(request, "id");
                const auto current = region->entity(id);
                const auto text_value =
                    clean(field(request, "text"), 512);
                const bool ok =
                    current && current->kind == EntityKind::object &&
                    can_modify_object(*current, claims) &&
                    region->set_floating_text(id, text_value);
                socket.send_frame({
                    ok ? protocol::MessageType::entity_text_ack
                       : protocol::MessageType::error,
                    frame.request_id,
                    protocol::payload_from_string(
                        ok ? "status=text-updated\n"
                           : "reason=object-permission-denied\n")});
            } else if (frame.type == protocol::MessageType::entity_motion) {
                if (!security::has_scene_capability(
                        claims, "scene.object.modify.own")) {
                    send_capability_error(
                        socket, frame, "scene.object.modify.own");
                    continue;
                }
                const auto id = integer(request, "id");
                const auto current = region->entity(id);
                const physics::Vec3 velocity{
                    number(request, "vx", 0.0),
                    number(request, "vy", 0.0),
                    number(request, "vz", 0.0)};
                const physics::Vec3 angular{
                    number(request, "avx", 0.0),
                    number(request, "avy", 0.0),
                    number(request, "avz", 0.0)};
                const bool permitted =
                    current && current->kind == EntityKind::object &&
                    can_modify_object(*current, claims);
                const bool ok =
                    permitted && region->set_velocity(id, velocity) &&
                    region->set_angular_velocity(id, angular);
                socket.send_frame({
                    ok ? protocol::MessageType::entity_motion_ack
                       : protocol::MessageType::error,
                    frame.request_id,
                    protocol::payload_from_string(
                        ok ? "status=motion-updated\n"
                           : "reason=physical-object-motion-denied\n")});
            } else if (frame.type == protocol::MessageType::entity_physics) {
                if (!security::has_scene_capability(
                        claims, "scene.object.modify.own")) {
                    send_capability_error(
                        socket, frame, "scene.object.modify.own");
                    continue;
                }
                const auto id = integer(request, "id");
                const auto current = region->entity(id);
                const auto action = field(request, "action");
                const bool permitted =
                    current && current->kind == EntityKind::object &&
                    can_modify_object(*current, claims);
                bool ok = false;
                std::string reason = "physics-operation-denied";
                std::uint64_t constraint_id = 0;

                if (permitted &&
                    (action == "force" || action == "impulse" ||
                     action == "angular_impulse" || action == "torque")) {
                    const physics::Vec3 vector{
                        number(request, "x", 0.0),
                        number(request, "y", 0.0),
                        number(request, "z", 0.0)};
                    if (action == "force") {
                        ok = region->apply_force(id, vector);
                    } else if (action == "impulse") {
                        ok = region->apply_impulse(id, vector);
                    } else if (action == "angular_impulse") {
                        ok = region->apply_angular_impulse(id, vector);
                    } else {
                        ok = region->apply_torque(id, vector);
                    }
                    reason = ok ? "" : "physical-object-required";
                } else if (permitted && action == "buoyancy") {
                    ok = region->set_buoyancy(
                        id, number(request, "value", 0.0));
                    reason = ok ? "" : "physical-object-required";
                } else if (permitted && action == "material") {
                    ok = region->set_physics_material(
                        id,
                        number(request, "mass", 1.0),
                        number(request, "restitution", 0.15),
                        number(request, "friction", 0.6));
                    reason = ok ? "" : "invalid-physics-material";
                } else if (permitted && action == "constraint") {
                    const auto other_id = integer(request, "other_id");
                    const auto other = region->entity(other_id);
                    if (other && other->kind == EntityKind::object &&
                        can_modify_object(*other, claims)) {
                        constraint_id = region->constrain_distance(
                            id, other_id,
                            number(request, "rest_length", 1.0),
                            number(request, "stiffness", 1.0),
                            reason);
                        ok = constraint_id != 0;
                    }
                } else if (permitted &&
                           action == "constraint_remove") {
                    constraint_id = integer(request, "constraint_id");
                    ok = constraint_id != 0 &&
                         region->remove_constraint(constraint_id);
                    reason = ok ? "" : "constraint-not-found";
                }

                std::ostringstream response;
                if (ok) {
                    response << "status=updated\n";
                    if (constraint_id != 0) {
                        response << "constraint_id="
                                 << constraint_id << '\n';
                    }
                } else {
                    response << "reason="
                             << (reason.empty()
                                     ? "physics-operation-denied"
                                     : reason)
                             << '\n';
                }
                socket.send_frame({
                    ok ? protocol::MessageType::entity_physics_ack
                       : protocol::MessageType::error,
                    frame.request_id,
                    protocol::payload_from_string(response.str())});
            } else if (frame.type == protocol::MessageType::entity_interact) {
                if (!security::has_scene_capability(
                        claims, "scene.object.interact")) {
                    send_capability_error(
                        socket, frame, "scene.object.interact");
                    continue;
                }
                const auto target_id = integer(request, "id");
                const auto phase = field(request, "phase");
                const auto sequence = region->interact_object(
                    target_id, avatar_id, user_id, phase);
                std::ostringstream response;
                if (sequence != 0U) {
                    response << "status=accepted\n"
                             << "phase=" << phase << '\n'
                             << "sequence=" << sequence << '\n';
                } else {
                    response << "reason=interaction-rejected\n";
                }
                socket.send_frame({
                    sequence != 0U
                        ? protocol::MessageType::entity_interact_ack
                        : protocol::MessageType::error,
                    frame.request_id,
                    protocol::payload_from_string(response.str())});
            } else if (frame.type == protocol::MessageType::chat_send) {
                if (!security::has_scene_capability(claims, "scene.chat")) {
                    send_capability_error(socket, frame, "scene.chat");
                    continue;
                }
                const auto sequence = region->chat(avatar_id, field(request, "text"));
                socket.send_frame({sequence ? protocol::MessageType::chat_event
                                            : protocol::MessageType::error,
                                   frame.request_id,
                                   protocol::payload_from_string(
                                       sequence ? "status=sent\nsequence=" +
                                                      std::to_string(sequence) + "\n"
                                                : "reason=chat-rejected\n")});
            } else if (frame.type == protocol::MessageType::terrain_sample_request) {
                if (!security::has_scene_capability(claims, "scene.terrain.sample")) {
                    send_capability_error(socket, frame, "scene.terrain.sample");
                    continue;
                }
                const double x = number(request, "x", 0.0);
                const double y = number(request, "y", 0.0);
                std::ostringstream sample;
                sample << std::fixed << std::setprecision(3) << "x=" << x << "\ny=" << y
                       << "\nheight=" << region->terrain().sample(x, y) << "\nrevision="
                       << region->terrain().revision() << '\n';
                socket.send_frame({protocol::MessageType::terrain_sample, frame.request_id,
                                   protocol::payload_from_string(sample.str())});
            } else if (frame.type == protocol::MessageType::terrain_set_request) {
                if (!security::has_scene_capability(claims, "scene.terrain.modify")) {
                    send_capability_error(socket, frame, "scene.terrain.modify");
                    continue;
                }
                const auto x = integer(request, "x");
                const auto y = integer(request, "y");
                const double height = number(request, "height", region->terrain().base_height());
                parcels->reload();
                const bool permitted = parcels->can_terraform(region->id(), static_cast<double>(x), static_cast<double>(y), user_id, groups_from_claims(claims));
                const bool ok = permitted && region->set_terrain_height(static_cast<std::size_t>(x),
                                                           static_cast<std::size_t>(y), height);
                socket.send_frame({ok ? protocol::MessageType::terrain_set_ack
                                      : protocol::MessageType::error,
                                   frame.request_id,
                                   protocol::payload_from_string(
                                       ok ? "status=updated\nrevision=" +
                                                      std::to_string(region->terrain().revision()) + "\n"
                                          : "reason=terrain-or-parcel-permission-denied\n")});
            } else if (frame.type == protocol::MessageType::ping) {
                socket.send_frame({protocol::MessageType::pong, frame.request_id, frame.payload});
            } else if (frame.type == protocol::MessageType::goodbye) {
                break;
            } else {
                socket.send_frame({protocol::MessageType::error, frame.request_id,
                                   protocol::payload_from_string(
                                       "reason=unsupported-scene-message\n")});
            }
        }
    } catch (const std::exception& error) {
        common::log(common::LogLevel::debug, "world.scene.client", error.what());
    }
    if (region && avatar_id) region->remove_entity(avatar_id);
}

} // namespace

SceneServer::SceneServer(std::string address, const std::uint16_t port,
                         const std::vector<std::shared_ptr<RegionRuntime>>& regions,
                         std::string ticket_secret, std::string parcel_path, std::string moderation_path)
    : address_(std::move(address)), port_(port),
      auth_(std::make_shared<SceneAuthContext>(std::move(ticket_secret))),
      parcels_(std::make_shared<core::ParcelStore>(std::move(parcel_path))),
      moderation_(std::make_shared<core::ModerationStore>(std::move(moderation_path))) {
    for (const auto& region : regions) regions_.emplace(region->id(), region);
}

SceneServer::~SceneServer() { stop(); }

void SceneServer::start() {
    if (running_.exchange(true)) return;
    thread_ = std::thread(&SceneServer::run, this);
}

void SceneServer::stop() {
    if (!running_.exchange(false)) return;
    if (thread_.joinable()) thread_.join();
}

void SceneServer::run() {
    try {
        opengenesis::network::TcpListener listener(address_, port_);
        common::log(common::LogLevel::info, "world.scene",
                    "Authenticated Scene endpoint listening on " + address_ + ":" +
                        std::to_string(port_));
        while (running_) {
            auto socket = listener.accept_for(std::chrono::milliseconds{250});
            if (!socket) continue;
            auto regions = regions_;
            auto auth = auth_;
            auto parcels = parcels_;
            auto moderation = moderation_;
            std::thread(handle_client, std::move(*socket), std::move(regions), std::move(auth),
                        std::move(parcels), std::move(moderation)).detach();
        }
    } catch (const std::exception& error) {
        if (running_) common::log(common::LogLevel::error, "world.scene", error.what());
    }
}

} // namespace opengenesis::world
