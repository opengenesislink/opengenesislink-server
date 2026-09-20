#include "opengenesis/compat/hypergrid/service.hpp"
#include "opengenesis/compat/hypergrid/xmlrpc.hpp"
#include "opengenesis/core/region_registry.hpp"
#include "opengenesis/federation/grid_identity_store.hpp"
#include "opengenesis/federation/runtime.hpp"
#include "opengenesis/federation/session_store.hpp"
#include "opengenesis/federation/service_grant_store.hpp"
#include "opengenesis/federation/trust_store.hpp"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

namespace {

void require(const bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

std::filesystem::path make_root() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto root = std::filesystem::temp_directory_path() /
                      ("ogl-300-tests-" + std::to_string(stamp));
    std::filesystem::create_directories(root);
    return root;
}

std::shared_ptr<opengenesis::federation::FederationRuntime> make_runtime(
    const std::filesystem::path& root,
    const std::string& prefix,
    const std::string& grid_id,
    const std::string& base_url) {
    auto identity = std::make_shared<opengenesis::federation::GridIdentityStore>(
        (root / (prefix + "-identity.db")).string(), grid_id, base_url);
    auto trust = std::make_shared<opengenesis::federation::FederationTrustStore>(
        (root / (prefix + "-trust.db")).string());
    auto sessions = std::make_shared<opengenesis::federation::FederationSessionStore>(
        (root / (prefix + "-sessions.db")).string());
    auto grants =
        std::make_shared<opengenesis::federation::FederationServiceGrantStore>(
            (root / (prefix + "-grants.db")).string());
    return std::make_shared<opengenesis::federation::FederationRuntime>(
        std::move(identity), std::move(trust),
        std::move(sessions), std::move(grants));
}

} // namespace

int main() {
    try {
        const auto root = make_root();

        auto grid_a = make_runtime(root, "a", "grid-a.example", "https://grid-a.example");
        auto grid_b = make_runtime(root, "b", "grid-b.example", "https://grid-b.example");

        const auto info_a = grid_a->info();
        const auto info_b = grid_b->info();
        require(info_a.public_key_hex.size() == 64, "grid A public key");
        require(info_b.public_key_hex.size() == 64, "grid B public key");

        std::string reason;
        require(grid_a->trust_peer(
                    {.grid_id = info_b.grid_id,
                     .base_url = info_b.base_url,
                     .public_key_hex = info_b.public_key_hex,
                     .trusted = true,
                     .revoked = false,
                     .updated_unix = 0},
                    reason),
                "grid A trusts grid B");
        require(grid_b->trust_peer(
                    {.grid_id = info_a.grid_id,
                     .base_url = info_a.base_url,
                     .public_key_hex = info_a.public_key_hex,
                     .trusted = true,
                     .revoked = false,
                     .updated_unix = 0},
                    reason),
                "grid B trusts grid A");

        const auto issued = grid_a->issue_travel(
            {.subject_user = "alice",
             .display_name = "Alice",
             .audience_grid = "grid-b.example",
             .origin_region = "region-a",
             .destination_region = "region-b",
             .lifetime = std::chrono::seconds{90}},
            reason);
        require(issued.has_value(), "travel token issued");
        require(issued->claims.home_url == "https://grid-a.example",
                "travel token carries home grid URL");
        require(!issued->claims.service_grant_id.empty() &&
                    issued->claims.service_token.size() >= 32U,
                "travel token carries remote service grant");
        require(
            opengenesis::federation::federation_service_capability(
                issued->claims.service_capabilities, "inventory"),
            "travel service scopes include inventory");
        require(grid_a->authorize_remote_service(
                    issued->claims.service_grant_id,
                    issued->claims.service_token,
                    "grid-b.example", "alice", "inventory"),
                "home grid authorizes audience-bound remote inventory");
        require(!grid_a->authorize_remote_service(
                    issued->claims.service_grant_id,
                    issued->claims.service_token,
                    "grid-c.example", "alice", "inventory"),
                "service grant rejects wrong audience");
        require(!grid_a->authorize_remote_service(
                    issued->claims.service_grant_id,
                    issued->claims.service_token,
                    "grid-b.example", "alice", "admin"),
                "service grant rejects missing scope");

        const auto accepted = grid_b->accept_travel(
            "grid-a.example", issued->token, reason);
        require(accepted.has_value(), "travel token accepted");
        require(accepted->session.state ==
                    opengenesis::federation::ForeignSessionState::active,
                "foreign session active");
        require(accepted->claims.destination_region == "region-b",
                "destination preserved");
        require(accepted->session.home_url == "https://grid-a.example" &&
                    accepted->session.service_grant_id ==
                        issued->claims.service_grant_id &&
                    accepted->session.service_token ==
                        issued->claims.service_token,
                "foreign session persists OGL-FED v2 service context");
        require(!grid_b->accept_travel("grid-a.example", issued->token, reason).has_value(),
                "travel replay rejected");

        require(grid_b->logout_session(accepted->session.id), "foreign session logout");
        require(grid_b->revoke_peer("grid-a.example"), "grid A revoked");
        const auto second = grid_a->issue_travel(
            {.subject_user = "alice",
             .display_name = "Alice",
             .audience_grid = "grid-b.example",
             .origin_region = "region-a",
             .destination_region = "region-b",
             .lifetime = std::chrono::seconds{90}},
            reason);
        require(second.has_value(), "second token issued");
        require(!grid_b->accept_travel("grid-a.example", second->token, reason).has_value(),
                "revoked grid rejected");

        require(grid_a->revoke_peer("grid-b.example"),
                "grid B revoked on home grid");
        require(!grid_a->authorize_remote_service(
                    issued->claims.service_grant_id,
                    issued->claims.service_token,
                    "grid-b.example", "alice", "inventory"),
                "peer revocation invalidates outbound service grants");

        auto regions = std::make_shared<opengenesis::core::RegionRegistry>(
            (root / "regions.db").string());
        require(regions->register_region(
                    {.id = "region-b",
                     .name = "Region B",
                     .node_id = "world-b",
                     .state = "registered",
                     .grid_x = 1000,
                     .grid_y = 1001,
                     .node_generation = 1,
                     .ticks = 0,
                     .entities = 0,
                     .avatars = 0,
                     .physics_bodies = 0,
                     .scene_events = 0,
                     .terrain_revision = 0,
                     .sim_fps = 0.0},
                    reason),
                "register HG test region");
        require(regions->update_state("region-b", "world-b", 1, "starting", reason),
                "region starting");
        require(regions->update_state("region-b", "world-b", 1, "online", reason),
                "region online");

        opengenesis::compat::hypergrid::HypergridService hg(
            {.enabled = true,
             .external_name = "https://grid-b.example:8002",
             .home_uri = "https://grid-b.example:8002",
             .asset_uri = "https://grid-b.example:8002",
             .inventory_uri = "https://grid-b.example:8002",
             .avatar_uri = "https://grid-b.example:8002",
             .friends_uri = "https://grid-b.example:8002",
             .im_uri = "https://grid-b.example:8002",
             .region_host = "grid-b.example",
             .http_port = 9000,
             .internal_port = 9000},
            regions);

        const std::string xml =
            "<?xml version=\"1.0\"?><methodCall><methodName>link_region</methodName>"
            "<params><param><value><struct><member><name>region_name</name>"
            "<value><string>Region B</string></value></member></struct></value></param>"
            "</params></methodCall>";
        const auto call = opengenesis::compat::hypergrid::parse_xmlrpc_call(xml);
        require(call && call->method == "link_region", "parse link_region");
        require(call->fields.at("region_name") == "Region B", "parse region name");

        const auto linked = hg.handle(*call);
        require(linked.at("result") == "True", "HG link succeeds");
        require(linked.at("uuid").size() == 36, "legacy UUID generated");
        require(!linked.at("handle").empty(), "legacy handle generated");

        const auto region_call = opengenesis::compat::hypergrid::XmlRpcCall{
            .method = "get_region",
            .fields = {{"region_uuid", linked.at("uuid")}}};
        const auto remote = hg.handle(region_call);
        require(remote.at("result") == "true", "HG region lookup succeeds");
        require(remote.at("region_name") == "Region B", "HG region name returned");
        require(remote.at("hostname") == "grid-b.example", "HG region host returned");

        const auto urls = hg.handle(
            {.method = "get_server_urls", .fields = {}});
        require(urls.at("SRV_HomeURI") == "https://grid-b.example:8002",
                "HG HomeURI returned");

        const auto response =
            opengenesis::compat::hypergrid::xmlrpc_struct_response(linked);
        require(response.find("<methodResponse>") != std::string::npos,
                "XML-RPC response serialized");
        require(response.find("external_name") != std::string::npos,
                "XML-RPC response fields serialized");

        std::filesystem::remove_all(root);
        std::cout << "OpenGenesisLINK 3.0 federation/hypergrid tests: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "OpenGenesisLINK 3.0 test failure: " << error.what() << '\n';
        return 1;
    }
}
