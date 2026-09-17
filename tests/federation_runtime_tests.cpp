#include "opengenesis/core/crossing_store.hpp"
#include "opengenesis/federation/grid_identity.hpp"
#include "opengenesis/federation/replay_cache.hpp"
#include "opengenesis/federation/travel_token.hpp"
#include "opengenesis/federation/trust_store.hpp"
#include "opengenesis/scripting/script_runtime.hpp"
#include "opengenesis/security/rate_limiter.hpp"
#include "opengenesis/storage/schema_version.hpp"
#include "opengenesis/voice/voice_provider.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void require(const bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

std::int64_t unix_now() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

std::filesystem::path temp_root() {
    const auto root = std::filesystem::temp_directory_path() /
                      ("ogl-250-tests-" + std::to_string(unix_now()));
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    return root;
}

} // namespace

int main() {
    try {
        const auto root = temp_root();

        const auto keys = opengenesis::federation::generate_grid_key_pair();
        require(keys.public_key_hex.size() == 64, "public Ed25519 key length");
        require(keys.private_key_hex.size() == 64, "private Ed25519 key length");

        opengenesis::federation::TravelTokenClaims claims{
            .issuer_grid = "grid-a.example",
            .audience_grid = "grid-b.example",
            .subject_user = "alice@grid-a.example",
            .display_name = "Alice",
            .origin_region = "region-a",
            .destination_region = "region-b",
            .session_id = "session-123"};

        const auto issued = opengenesis::federation::issue_travel_token(
            keys.private_key_hex, claims, std::chrono::seconds{90});
        const auto verified = opengenesis::federation::verify_travel_token(
            keys.public_key_hex, issued.token, "grid-b.example", "grid-a.example");
        require(verified.has_value(), "travel token verifies");
        require(verified->subject_user == claims.subject_user, "travel subject preserved");
        require(!opengenesis::federation::verify_travel_token(
                    keys.public_key_hex, issued.token, "grid-c.example").has_value(),
                "audience binding enforced");

        auto tampered = issued.token;
        tampered[tampered.size() / 2] = tampered[tampered.size() / 2] == 'A' ? 'B' : 'A';
        require(!opengenesis::federation::verify_travel_token(
                    keys.public_key_hex, tampered, "grid-b.example").has_value(),
                "signature rejects tampering");

        opengenesis::federation::TravelReplayCache replay;
        require(replay.consume(issued.claims.nonce, issued.claims.expires_unix),
                "first travel nonce accepted");
        require(!replay.consume(issued.claims.nonce, issued.claims.expires_unix),
                "travel nonce replay rejected");

        const auto trust_path = (root / "federation.db").string();
        {
            opengenesis::federation::FederationTrustStore trust(trust_path);
            std::string reason;
            require(trust.trust({.grid_id = "grid-a.example",
                                 .base_url = "https://grid-a.example",
                                 .public_key_hex = keys.public_key_hex},
                                reason),
                    "peer trust succeeds");
            require(trust.is_trusted("grid-a.example", keys.public_key_hex),
                    "trusted key matches");
            require(trust.revoke("grid-a.example"), "peer revocation succeeds");
            require(!trust.is_trusted("grid-a.example", keys.public_key_hex),
                    "revoked peer rejected");
        }
        {
            opengenesis::federation::FederationTrustStore trust(trust_path);
            const auto peer = trust.find("grid-a.example");
            require(peer && peer->revoked, "peer revocation persisted");
        }

        const auto crossing_path = (root / "crossings.db").string();
        {
            opengenesis::core::CrossingStore crossings(crossing_path);
            std::string reason;
            const auto crossing = crossings.prepare(
                "user-1", "region-a", "region-b",
                {.x = 4.0, .y = 5.0, .z = 6.0},
                {.x = 1.0, .y = 2.0, .z = 3.0},
                unix_now() + 60, reason);
            require(crossing.has_value(), "crossing prepared");
            const auto completed = crossings.complete(
                crossing->id, "user-1", "region-b", reason);
            require(completed && completed->velocity.z == 3.0, "crossing state transferred");
            require(!crossings.complete(
                        crossing->id, "user-1", "region-b", reason).has_value(),
                    "crossing completion is one-time");
        }

        const auto scripts_path = (root / "scripts.db").string();
        {
            opengenesis::scripting::ScriptRuntime scripts(scripts_path);
            std::string reason;
            require(scripts.upsert({.id = "script-1",
                                    .object_id = "object-1",
                                    .owner_user_id = "user-1",
                                    .source_hash = "sha256:test"},
                                   reason),
                    "script persisted");
            require(scripts.set_timer("script-1", 1000, 10000), "script timer configured");
            require(scripts.set_chat("script-1", true, 7), "script chat configured");
            require(scripts.due_timers(10999).empty(), "timer not early");
            require(scripts.due_timers(11000).size() == 1, "timer event dispatched");
            require(scripts.dispatch_chat(7, "Alice", "hello").size() == 1,
                    "chat event dispatched");
            require(scripts.set_state("script-1", "running"), "script state updated");
        }
        {
            opengenesis::scripting::ScriptRuntime scripts(scripts_path);
            const auto script = scripts.find("script-1");
            require(script && script->state == "running" && script->event_count == 2,
                    "script runtime state persisted");
        }

        opengenesis::security::RateLimiter limits;
        const auto now = std::chrono::steady_clock::now();
        require(limits.allow("login:alice", 2, std::chrono::seconds{10}, now),
                "rate limit first event");
        require(limits.allow("login:alice", 2, std::chrono::seconds{10}, now),
                "rate limit second event");
        require(!limits.allow("login:alice", 2, std::chrono::seconds{10}, now),
                "rate limit blocks overflow");
        require(limits.allow("login:alice", 2, std::chrono::seconds{10},
                             now + std::chrono::seconds{11}),
                "rate limit window expires");

        const auto schema_path = root / "schema.version";
        {
            std::ofstream schema(schema_path);
            schema << "1\n";
        }
        opengenesis::storage::SchemaVersionStore schema(schema_path.string(), 3, 1);
        require(schema.version() == 1 && schema.upgrade_required(), "schema upgrade detected");
        schema.upgrade_to(2);
        schema.upgrade_to(3);
        require(!schema.upgrade_required(), "schema upgraded to current");

        opengenesis::voice::VoiceProviderConfig voice{
            .enabled = true,
            .provider = "opengenesislink",
            .service_url = "https://voice.opengenesislink.de",
            .sip_domain = "sip.opengenesislink.de",
            .client_id = "grid-test",
            .client_secret = "0123456789abcdef0123456789abcdef",
            .token_lifetime = std::chrono::seconds{300}};
        std::string voice_reason;
        require(opengenesis::voice::validate_provider_config(voice, voice_reason),
                "voice provider config accepted");
        voice.client_secret = "short";
        require(!opengenesis::voice::validate_provider_config(voice, voice_reason),
                "short voice provider secret rejected");

        std::filesystem::remove_all(root);
        std::cout << "OpenGenesisLINK 2.5.0 federation/runtime foundation tests: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "OpenGenesisLINK 2.5.0 test failure: " << error.what() << '\n';
        return 1;
    }
}
