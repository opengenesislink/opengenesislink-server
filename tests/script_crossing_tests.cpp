#include "opengenesis/core/crossing_store.hpp"
#include "opengenesis/core/friends_store.hpp"
#include "opengenesis/core/identity_store.hpp"
#include "opengenesis/core/message_store.hpp"
#include "opengenesis/core/notification_store.hpp"
#include "opengenesis/scripting/script_host.hpp"
#include "opengenesis/scripting/script_runtime.hpp"
#include "opengenesis/scripting/script_vm.hpp"
#include "opengenesis/security/scene_ticket.hpp"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

std::filesystem::path root_path() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto root = std::filesystem::temp_directory_path() /
                      ("ogl-500-runtime-" + std::to_string(stamp));
    std::filesystem::create_directories(root);
    return root;
}

std::int64_t unix_now() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch()).count();
}

} // namespace

int main() {
    try {
        const auto root = root_path();
        std::string reason;

        const std::string program =
            "event touch\n"
            "set count 1\n"
            "add count 2\n"
            "emit $count\n"
            "state active\n"
            "timer 1500\n"
            "listen 7\n"
            "end\n"
            "event timer\n"
            "add count 1\n"
            "emit timer\n"
            "end\n";

        const auto compiled = opengenesis::scripting::compile_script(program, reason);
        require(compiled.has_value(), "script compiles");

        opengenesis::scripting::ScriptVmState state;
        auto vm = opengenesis::scripting::execute_script_event(
            *compiled, "touch", state,
            {.instruction_budget = 16,
             .max_variables = 8,
             .max_state_bytes = 1024,
             .max_output_actions = 8});
        require(vm.ok && vm.state.state == "active", "script event executes");
        require(vm.state.variables.at("count") == "3", "script arithmetic state");
        require(vm.actions.size() == 4, "script host actions emitted");

        auto limited = opengenesis::scripting::execute_script_event(
            *compiled, "touch", state,
            {.instruction_budget = 2,
             .max_variables = 8,
             .max_state_bytes = 1024,
             .max_output_actions = 8});
        require(!limited.ok && limited.error == "instruction-budget-exceeded",
                "instruction budget enforced");

        const auto state_encoded = opengenesis::scripting::serialize_vm_state(vm.state);
        const auto state_decoded =
            opengenesis::scripting::deserialize_vm_state(state_encoded, reason);
        require(state_decoded && state_decoded->variables.at("count") == "3",
                "VM state roundtrip");
        auto identities = std::make_shared<opengenesis::core::IdentityStore>(
            (root / "users.db").string());
        const auto alice = identities->register_user(
            "script.alice", "Script Alice", "correct horse battery staple", reason);
        const auto bob = identities->register_user(
            "script.bob", "Script Bob", "correct horse battery staple", reason);
        require(alice && bob, "Script host identities created");

        auto friends = std::make_shared<opengenesis::core::FriendsStore>(
            (root / "friends.db").string());
        require(friends->request(alice->id, bob->id, reason).has_value(),
                "Script host friendship requested");
        require(friends->accept(bob->id, alice->id, reason).has_value(),
                "Script host friendship accepted");

        auto messages = std::make_shared<opengenesis::core::MessageStore>(
            (root / "messages.db").string());
        auto notifications = std::make_shared<opengenesis::core::NotificationStore>(
            (root / "notifications.db").string());
        opengenesis::scripting::ScriptHost host(
            identities, friends, messages, notifications);

        const std::string host_program =
            "event touch\n"
            "notify Script owner notified\n"
            "message " + bob->id + " Hello from script\n"
            "end\n";
        const auto host_compiled =
            opengenesis::scripting::compile_script(host_program, reason);
        require(host_compiled.has_value(), "Script host program compiles");
        const auto host_vm = opengenesis::scripting::execute_script_event(
            *host_compiled, "touch", {});
        require(host_vm.ok && host_vm.actions.size() == 2,
                "Script host actions emitted");
        const auto host_result = host.apply(
            alice->id, "host-script", host_vm.actions);
        require(host_result.applied == 2 && host_result.errors.empty(),
                "Script host actions applied");
        require(messages->count() == 1 && messages->unread_count(bob->id) == 1,
                "Script friend message persisted");
        require(notifications->unread_count(alice->id) == 1 &&
                    notifications->unread_count(bob->id) == 1,
                "Script notifications persisted");

        const auto scripts_path = (root / "scripts.db").string();
        {
            opengenesis::scripting::ScriptRuntime scripts(scripts_path);
            require(scripts.upsert(
                        {.id = "script-1",
                         .object_id = "object-1",
                         .owner_user_id = "user-1",
                         .source_hash = "pending",
                         .source = {},
                         .vm_state = {},
                         .state = "default",
                         .enabled = true,
                         .timer_interval_ms = 0,
                         .next_timer_unix_ms = 0,
                         .chat_channel = 0,
                         .chat_enabled = false,
                         .event_count = 0},
                        reason),
                    "script record created");
            require(scripts.set_program("script-1", program, reason),
                    "script program stored");
            const auto executed = scripts.execute_event("script-1", "touch", 10000, reason);
            require(executed && executed->state.variables.at("count") == "3",
                    "persistent runtime executes");
            const auto record = scripts.find("script-1");
            require(record && record->state == "active" &&
                        record->timer_interval_ms == 1500 &&
                        record->chat_enabled && record->chat_channel == 7,
                    "host actions persisted");
        }
        {
            opengenesis::scripting::ScriptRuntime scripts(scripts_path);
            const auto executed = scripts.execute_event("script-1", "timer", 11500, reason);
            require(executed && executed->state.variables.at("count") == "4",
                    "VM state survives restart");
        }

        const auto crossing_path = (root / "crossings.db").string();
        std::string crossing_id;
        {
            opengenesis::core::CrossingStore crossings(crossing_path);
            const auto crossing = crossings.prepare(
                "user-1", "region-a", "region-b",
                {.x = 0.5, .y = 128.0, .z = 22.0},
                {.x = 4.0, .y = 1.5, .z = 0.25},
                unix_now() + 60, reason,
                "{\"attachments\":[{\"item_id\":\"hat\"}]}",
                "[{\"id\":\"script-1\",\"vm_state\":\"" + state_encoded + "\"}]");
            require(crossing.has_value(), "crossing prepared with runtime state");
            crossing_id = crossing->id;
        }
        {
            opengenesis::core::CrossingStore crossings(crossing_path);
            const auto restored = crossings.find(crossing_id);
            require(restored && restored->velocity.x == 4.0 &&
                        restored->attachment_state.find("hat") != std::string::npos &&
                        restored->script_state.find("script-1") != std::string::npos,
                    "crossing runtime state persisted");
            const auto completed =
                crossings.complete(crossing_id, "user-1", "region-b", reason);
            require(completed && completed->state == opengenesis::core::CrossingState::completed,
                    "crossing completes once");
            require(!crossings.complete(crossing_id, "user-1", "region-b", reason),
                    "crossing replay rejected");
        }

        const std::string secret = "0123456789abcdef0123456789abcdef0123456789abcdef";
        const auto ticket = opengenesis::security::issue_scene_ticket(
            secret, "user-1", "User One", "region-b", std::chrono::seconds{60},
            std::string{opengenesis::security::kDefaultSceneCapabilities},
            "region-a", {}, 0.5, 128.0, 22.0, crossing_id);
        const auto verified =
            opengenesis::security::verify_scene_ticket(secret, ticket.token, "region-b");
        require(verified && verified->crossing_id == crossing_id,
                "crossing id is signed into Scene Ticket");

        std::filesystem::remove_all(root);
        std::cout << "OpenGenesisLINK 5.5 Script/Crossing runtime tests: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "OpenGenesisLINK 5.5 runtime test failure: " << error.what() << '\n';
        return 1;
    }
}
