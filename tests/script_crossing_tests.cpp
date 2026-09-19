#include "opengenesis/core/crossing_store.hpp"
#include "opengenesis/core/friends_store.hpp"
#include "opengenesis/core/identity_store.hpp"
#include "opengenesis/core/message_store.hpp"
#include "opengenesis/core/notification_store.hpp"
#include "opengenesis/scripting/script_host.hpp"
#include "opengenesis/scripting/script_runtime.hpp"
#include "opengenesis/scripting/script_vm.hpp"
#include "opengenesis/scripting/world_action_queue.hpp"
#include "opengenesis/security/scene_ticket.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <memory>
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

        const std::string ogl_program =
            "@ogl 1\n"
            "state default\n"
            "on touch\n"
            "let count = 1\n"
            "inc count by 2\n"
            "world.say Hello from OGL\n"
            "world.move 10 20 30\n"
            "goto active\n"
            "end\n"
            "state active\n"
            "on timer\n"
            "world.text OGL active\n"
            "end\n";
        const auto ogl_compiled =
            opengenesis::scripting::compile_script_source(
                opengenesis::scripting::ScriptLanguage::ogl,
                ogl_program, reason);
        require(ogl_compiled && ogl_compiled->handlers.size() == 2,
                "native OGL language compiles state handlers");
        const auto ogl_touch =
            opengenesis::scripting::execute_script_event(
                *ogl_compiled, "touch", {});
        require(ogl_touch.ok &&
                    ogl_touch.state.state == "active" &&
                    ogl_touch.state.variables.at("count") == "3" &&
                    ogl_touch.actions.size() == 3,
                "OGL touch event executes through shared VM");
        const auto ogl_timer =
            opengenesis::scripting::execute_script_event(
                *ogl_compiled, "timer", ogl_touch.state);
        require(ogl_timer.ok && ogl_timer.actions.size() == 1 &&
                    ogl_timer.actions.front().type ==
                        opengenesis::scripting::ScriptActionType::world_text,
                "OGL active-state timer selects state-specific handler");

        const std::string lsl_program =
            "default {\n"
            "  state_entry() {\n"
            "    llOwnerSay(\"ready\");\n"
            "    llSetTimerEvent(1.5);\n"
            "  }\n"
            "  listen(integer channel, string name, key id, string message) {\n"
            "    integer length = llStringLength(message);\n"
            "    string upper = llToUpper(message);\n"
            "    float magnitude = llVecMag(<3,4,0>);\n"
            "    integer absolute = llAbs(-3);\n"
            "    llSay(0, upper);\n"
            "  }\n"
            "  touch_start(integer total_number) {\n"
            "    llSetPos(<10,20,30>);\n"
            "    state active;\n"
            "  }\n"
            "}\n"
            "active {\n"
            "  timer() {\n"
            "    llSetText(\"active\", <1,1,1>, 1.0);\n"
            "  }\n"
            "}\n";
        const auto lsl_compiled =
            opengenesis::scripting::compile_script_source(
                opengenesis::scripting::ScriptLanguage::lsl,
                lsl_program, reason);
        require(lsl_compiled && lsl_compiled->handlers.size() == 4,
                "LSL compatibility frontend compiles default and named states");

        const auto lsl_entry =
            opengenesis::scripting::execute_script_event(
                *lsl_compiled, "state_entry", {});
        require(lsl_entry.ok && lsl_entry.actions.size() == 2 &&
                    lsl_entry.actions[0].type ==
                        opengenesis::scripting::ScriptActionType::notify_owner &&
                    lsl_entry.actions[1].type ==
                        opengenesis::scripting::ScriptActionType::set_timer &&
                    lsl_entry.actions[1].number == 1500,
                "LSL state_entry maps owner say and timer semantics");

        const auto lsl_listen =
            opengenesis::scripting::execute_script_event(
                *lsl_compiled, "listen", {}, {},
                "7\nAlice Example\nagent-key\nHello from listen");
        require(lsl_listen.ok && lsl_listen.actions.size() == 1 &&
                    lsl_listen.actions.front().type ==
                        opengenesis::scripting::ScriptActionType::world_chat_say &&
                    lsl_listen.actions.front().value == "HELLO FROM LISTEN" &&
                    lsl_listen.state.variables.at("channel") == "7" &&
                    lsl_listen.state.variables.at("message") ==
                        "Hello from listen" &&
                    lsl_listen.state.variables.at("length") == "17" &&
                    lsl_listen.state.variables.at("upper") ==
                        "HELLO FROM LISTEN" &&
                    lsl_listen.state.variables.at("magnitude") ==
                        "5.000000" &&
                    lsl_listen.state.variables.at("absolute") == "3",
                "LSL event parameters and deterministic builtins execute");

        const auto lsl_touch =
            opengenesis::scripting::execute_script_event(
                *lsl_compiled, "touch_start", lsl_entry.state, {},
                "1");
        require(lsl_touch.ok &&
                    lsl_touch.state.state == "active" &&
                    lsl_touch.actions.size() == 2 &&
                    lsl_touch.actions.front().type ==
                        opengenesis::scripting::ScriptActionType::world_move,
                "LSL state change and world mutation execute");

        const auto lsl_timer =
            opengenesis::scripting::execute_script_event(
                *lsl_compiled, "timer", lsl_touch.state);
        require(lsl_timer.ok && lsl_timer.actions.size() == 1 &&
                    lsl_timer.actions.front().type ==
                        opengenesis::scripting::ScriptActionType::world_text,
                "LSL named-state timer executes correct handler");

        require(opengenesis::scripting::lsl_function_catalog().size() >= 500,
                "LSL canonical function catalog is populated");
        require(opengenesis::scripting::lsl_event_catalog().size() == 44,
                "LSL event category catalog mirrors official event pages");
        require(opengenesis::scripting::ogl_feature_catalog().size() >= 30,
                "OGL native command catalog is populated");
        const auto count_status = [](const auto& catalog,
                                     const auto status) {
            return static_cast<std::size_t>(std::count_if(
                catalog.begin(), catalog.end(),
                [&](const auto& feature) {
                    return feature.status == status;
                }));
        };
        const auto& lsl_functions =
            opengenesis::scripting::lsl_function_catalog();
        const auto& lsl_events =
            opengenesis::scripting::lsl_event_catalog();
        const auto& ogl_features =
            opengenesis::scripting::ogl_feature_catalog();
        require(lsl_functions.size() == 523 &&
                    count_status(
                        lsl_functions,
                        opengenesis::scripting::ScriptFeatureStatus::implemented) ==
                        27 &&
                    count_status(
                        lsl_functions,
                        opengenesis::scripting::ScriptFeatureStatus::partial) ==
                        24 &&
                    count_status(
                        lsl_functions,
                        opengenesis::scripting::ScriptFeatureStatus::recognized) ==
                        447 &&
                    count_status(
                        lsl_functions,
                        opengenesis::scripting::ScriptFeatureStatus::unsupported) ==
                        25,
                "LSL function status matrix matches 9.0 contract");
        require(lsl_events.size() == 44 &&
                    count_status(
                        lsl_events,
                        opengenesis::scripting::ScriptFeatureStatus::implemented) ==
                        1 &&
                    count_status(
                        lsl_events,
                        opengenesis::scripting::ScriptFeatureStatus::partial) ==
                        3,
                "LSL event status matrix matches 9.0 contract");
        require(ogl_features.size() == 31 &&
                    count_status(
                        ogl_features,
                        opengenesis::scripting::ScriptFeatureStatus::implemented) ==
                        27 &&
                    count_status(
                        ogl_features,
                        opengenesis::scripting::ScriptFeatureStatus::unsupported) ==
                        4,
                "OGL feature status matrix matches 9.0 contract");


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
        const auto world_actions_path = (root / "script-world-actions.db").string();
        auto world_actions =
            std::make_shared<opengenesis::scripting::ScriptWorldActionQueue>(
                world_actions_path, 16, 3, 500, 60000);
        opengenesis::scripting::ScriptHost host(
            identities, friends, messages, notifications, world_actions);

        const std::string host_program =
            "event touch\n"
            "notify Script owner notified\n"
            "message " + bob->id + " Hello from script\n"
            "move 10 20 30\n"
            "rotate 0 0 90\n"
            "scale 2 2 2\n"
            "velocity 1 2 3\n"
            "angular_velocity 0 0 15\n"
            "physics 1\n"
            "text Runtime v3\n"
            "say Hello region\n"
            "whisper Quiet region\n"
            "shout Loud region\n"
            "object_info obj\n"
            "region_info region\n"
            "terrain_height ground\n"
            "water_level water\n"
            "world_time clock\n"
            "nearby_avatars nearby 32\n"
            "end\n";
        const auto host_compiled =
            opengenesis::scripting::compile_script(host_program, reason);
        require(host_compiled.has_value(), "Script host program compiles");
        const auto host_vm = opengenesis::scripting::execute_script_event(
            *host_compiled, "touch", {});
        require(host_vm.ok && host_vm.actions.size() == 18,
                "Script host and World v3 actions emitted");
        const auto host_result = host.apply(
            alice->id, "host-script", host_vm.actions, "region-a/42");
        require(host_result.applied == 18 && host_result.errors.empty(),
                "Script host and World v3 actions applied");
        require(world_actions->size() == 16,
                "Script World v3 Actions queued");

        const auto queue_now = unix_now() * 1000;
        const auto move_action = world_actions->lease("region-a", queue_now);
        require(move_action &&
                    move_action->entity_id == 42 &&
                    move_action->owner_user_id == alice->id &&
                    move_action->type ==
                        opengenesis::scripting::ScriptWorldActionType::move &&
                    move_action->payload == "10 20 30" &&
                    move_action->attempts == 1,
                "Script World Action lease preserves binding and payload");

        opengenesis::scripting::ScriptWorldActionQueue restored_actions(
            world_actions_path, 16, 3, 500, 60000);
        const auto restored_move = restored_actions.find(move_action->id);
        require(restored_move && restored_move->attempts == 1 &&
                    restored_move->lease_until_unix_ms > queue_now,
                "leased Script World Action survives restart");
        require(restored_actions.ack(move_action->id),
                "Script World Action ACK removes durable entry");

        const auto rotate_action = restored_actions.lease("region-a", queue_now);
        require(rotate_action &&
                    rotate_action->type ==
                        opengenesis::scripting::ScriptWorldActionType::rotate,
                "next Script World Action leased");
        require(restored_actions.nack(
                    rotate_action->id, "temporary-world-error", queue_now + 1000),
                "Script World Action NACK schedules retry");

        for (int index = 0; index < 14; ++index) {
            const auto action = restored_actions.lease("region-a", queue_now);
            require(action.has_value(), "other Script World Action leased");
            require(restored_actions.ack(action->id),
                    "other Script World Action ACKed");
        }
        require(restored_actions.size() == 1,
                "NACKed Script World Action remains queued");
        require(!restored_actions.lease("region-a", queue_now + 500),
                "NACK retry delay is enforced");
        const auto retried = restored_actions.lease("region-a", queue_now + 1000);
        require(retried && retried->id == rotate_action->id &&
                    retried->attempts == 2,
                "NACKed Script World Action is retried");
        require(restored_actions.ack(retried->id) &&
                    restored_actions.size() == 0,
                "retried Script World Action can be ACKed");
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
                         .object_id = "region-a/42",
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
            require(scripts.apply_world_result(
                        "script-1", "obj",
                        "position=1.000 2.000 3.000\nname=Query Cube\n",
                        reason),
                    "Script World query result stored");
            const auto query_record = scripts.find("script-1");
            require(query_record.has_value(),
                    "Script record remains after World query result");
            const auto query_state =
                opengenesis::scripting::deserialize_vm_state(
                    query_record->vm_state, reason);
            require(query_state &&
                        query_state->variables.at("obj.position") ==
                            "1.000 2.000 3.000" &&
                        query_state->variables.at("obj.name") == "Query Cube" &&
                        query_state->variables.at("obj.ready") == "1",
                    "Script World query result is persisted into VM state");
            require(scripts.rebind_objects(
                        "region-a", "region-b", {{42, 9001}},
                        "user-1", reason),
                    "Script binding migrates with crossed object");
            const auto rebound = scripts.find("script-1");
            require(rebound && rebound->object_id == "region-b/9001",
                    "Script object binding points at destination entity");
            require(scripts.rebind_objects(
                        "region-a", "region-b", {{42, 9001}},
                        "user-1", reason),
                    "Script binding migration retry is idempotent");
        }
        {
            opengenesis::scripting::ScriptRuntime scripts(scripts_path);
            const auto executed = scripts.execute_event("script-1", "timer", 11500, reason);
            require(executed && executed->state.variables.at("count") == "4",
                    "VM state survives restart");
        }

        {
            opengenesis::scripting::ScriptRuntime scripts(scripts_path);
            require(scripts.upsert(
                        {.id = "script-lsl",
                         .object_id = "region-a/77",
                         .owner_user_id = "user-1",
                         .source_hash = "pending",
                         .language =
                             opengenesis::scripting::ScriptLanguage::lsl,
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
                    "LSL runtime record created");
            require(scripts.set_program("script-lsl", lsl_program, reason),
                    "LSL source persists through common Script Runtime");
            const auto entry =
                scripts.execute_event(
                    "script-lsl", "state_entry", 20000, reason);
            require(entry && entry->actions.size() == 2,
                    "persisted LSL script executes");
        }
        {
            opengenesis::scripting::ScriptRuntime scripts(scripts_path);
            const auto restored_lsl = scripts.find("script-lsl");
            require(restored_lsl &&
                        restored_lsl->language ==
                            opengenesis::scripting::ScriptLanguage::lsl,
                    "Script language survives Runtime restart");
        }


        const auto crossing_path = (root / "crossings.db").string();
        std::string crossing_id;
        std::string reservation_token;
        {
            opengenesis::core::CrossingStore crossings(crossing_path);
            const auto crossing = crossings.prepare(
                "user-1", "region-a", "region-b",
                {.x = 0.5, .y = 128.0, .z = 22.0},
                {.x = 4.0, .y = 1.5, .z = 0.25},
                unix_now() + 60, reason,
                "{\"attachments\":[{\"item_id\":\"hat\"}]}",
                "[{\"id\":\"script-1\",\"vm_state\":\"" + state_encoded + "\"}]",
                {.x = 0.0, .y = 0.0, .z = 90.0},
                {.x = 0.0, .y = 0.0, .z = 1.25},
                "{\"physical\":true}",
                "[{\"root\":\"vehicle-1\",\"children\":[\"seat-1\"]}]");
            require(crossing.has_value(), "crossing v3 prepared with runtime state");
            crossing_id = crossing->id;
        }
        {
            opengenesis::core::CrossingStore crossings(crossing_path);
            const auto restored = crossings.find(crossing_id);
            require(restored && restored->velocity.x == 4.0 &&
                        restored->rotation.z == 90.0 &&
                        restored->angular_velocity.z == 1.25 &&
                        restored->attachment_state.find("hat") != std::string::npos &&
                        restored->script_state.find("script-1") != std::string::npos &&
                        restored->physics_state.find("physical") != std::string::npos &&
                        restored->linkset_state.find("vehicle-1") != std::string::npos,
                    "crossing v3 motion/runtime state persisted");

            require(!crossings.complete(
                        crossing_id, "user-1", "region-b", "not-reserved", reason),
                    "crossing cannot commit before destination reservation");

            const auto reserved =
                crossings.reserve(crossing_id, "user-1", "region-b", reason);
            require(reserved &&
                        reserved->state == opengenesis::core::CrossingState::reserved &&
                        !reserved->reservation_token.empty() &&
                        reserved->reserved_unix > 0,
                    "destination crossing reservation created");
            reservation_token = reserved->reservation_token;

            const auto idempotent_reserve =
                crossings.reserve(crossing_id, "user-1", "region-b", reason);
            require(idempotent_reserve &&
                        idempotent_reserve->reservation_token == reservation_token,
                    "destination reservation retry is idempotent");

            require(!crossings.complete(
                        crossing_id, "user-1", "region-b", "wrong-token", reason),
                    "crossing rejects incorrect reservation token");

            const auto completed = crossings.complete(
                crossing_id, "user-1", "region-b", reservation_token, reason);
            require(completed &&
                        completed->state == opengenesis::core::CrossingState::completed &&
                        completed->completed_unix > 0,
                    "reserved crossing commits once");
            require(!crossings.complete(
                        crossing_id, "user-1", "region-b", reservation_token, reason),
                    "crossing commit replay rejected");
        }

        {
            opengenesis::core::CrossingStore crossings(crossing_path);
            const auto rollback_crossing = crossings.prepare(
                "user-1", "region-b", "region-a",
                {.x = 255.5, .y = 128.0, .z = 22.0},
                {.x = -2.0, .y = 0.0, .z = 0.0},
                unix_now() + 60, reason);
            require(rollback_crossing.has_value(), "rollback crossing prepared");
            const auto reserved = crossings.reserve(
                rollback_crossing->id, "user-1", "region-a", reason);
            require(reserved.has_value(), "rollback crossing reserved");
            const auto rolled_back = crossings.rollback(
                rollback_crossing->id, "user-1",
                "destination-scene-rejected", reason);
            require(rolled_back &&
                        rolled_back->state ==
                            opengenesis::core::CrossingState::rolled_back &&
                        rolled_back->rollback_reason == "destination-scene-rejected" &&
                        rolled_back->rolled_back_unix > 0,
                    "reserved crossing rolls back with reason");
            const auto idempotent_rollback = crossings.rollback(
                rollback_crossing->id, "user-1", "ignored", reason);
            require(idempotent_rollback &&
                        idempotent_rollback->rollback_reason ==
                            "destination-scene-rejected",
                    "rollback retry is idempotent");
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
        std::cout << "OpenGenesisLINK 9.0 ScriptEngine/LSL/OGL runtime tests: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "OpenGenesisLINK 9.0 runtime test failure: " << error.what() << '\n';
        return 1;
    }
}
