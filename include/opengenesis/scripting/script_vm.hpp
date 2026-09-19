#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace opengenesis::scripting {

enum class ScriptOpcode { set, add, emit, state, timer, listen, notify, message, stop };

struct ScriptInstruction {
    ScriptOpcode opcode{ScriptOpcode::stop};
    std::string a;
    std::string b;
};

struct ScriptHandler {
    std::string event;
    std::vector<ScriptInstruction> instructions;
};

struct CompiledScript {
    std::vector<ScriptHandler> handlers;
};

struct ScriptVmState {
    std::string state{"default"};
    std::unordered_map<std::string, std::string> variables;
};

enum class ScriptActionType { emit, state_change, set_timer, listen, notify_owner, direct_message };

struct ScriptAction {
    ScriptActionType type{ScriptActionType::emit};
    std::string value;
    std::int64_t number{0};
};

struct ScriptVmLimits {
    std::size_t instruction_budget{256};
    std::size_t max_variables{64};
    std::size_t max_state_bytes{16 * 1024};
    std::size_t max_output_actions{32};
};

struct ScriptVmResult {
    bool ok{false};
    std::string error;
    std::size_t instructions_executed{0};
    ScriptVmState state;
    std::vector<ScriptAction> actions;
};

[[nodiscard]] std::optional<CompiledScript> compile_script(std::string_view source,
                                                           std::string& reason);
[[nodiscard]] ScriptVmResult execute_script_event(const CompiledScript& program,
                                                  std::string_view event,
                                                  const ScriptVmState& initial_state,
                                                  const ScriptVmLimits& limits = {});
[[nodiscard]] std::string serialize_vm_state(const ScriptVmState& state);
[[nodiscard]] std::optional<ScriptVmState> deserialize_vm_state(std::string_view encoded,
                                                               std::string& reason);

} // namespace opengenesis::scripting
