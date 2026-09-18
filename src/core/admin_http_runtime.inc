std::string script_record_json(const scripting::ScriptRecord& script) {
    return "{\"id\":\"" + json_escape(script.id) +
           "\",\"object_id\":\"" + json_escape(script.object_id) +
           "\",\"owner_user_id\":\"" + json_escape(script.owner_user_id) +
           "\",\"source_hash\":\"" + json_escape(script.source_hash) +
           "\",\"state\":\"" + json_escape(script.state) +
           "\",\"enabled\":" + (script.enabled ? "true" : "false") +
           ",\"timer_interval_ms\":" + std::to_string(script.timer_interval_ms) +
           ",\"next_timer_unix_ms\":" + std::to_string(script.next_timer_unix_ms) +
           ",\"chat_channel\":" + std::to_string(script.chat_channel) +
           ",\"chat_enabled\":" + (script.chat_enabled ? "true" : "false") +
           ",\"event_count\":" + std::to_string(script.event_count) + "}";
}

std::string script_actions_json(const std::vector<scripting::ScriptAction>& actions) {
    std::ostringstream out;
    out << '[';
    for (std::size_t i = 0; i < actions.size(); ++i) {
        if (i) out << ',';
        const auto& action = actions[i];
        const char* type = "emit";
        if (action.type == scripting::ScriptActionType::state_change) type = "state";
        else if (action.type == scripting::ScriptActionType::set_timer) type = "timer";
        else if (action.type == scripting::ScriptActionType::listen) type = "listen";
        out << "{\"type\":\"" << type << "\",\"value\":\"" << json_escape(action.value)
            << "\",\"number\":" << action.number << "}";
    }
    out << ']';
    return out.str();
}

std::string crossing_json(const RegionCrossing& crossing) {
    return "{\"id\":\"" + json_escape(crossing.id) +
           "\",\"user_id\":\"" + json_escape(crossing.user_id) +
           "\",\"from_region\":\"" + json_escape(crossing.from_region) +
           "\",\"to_region\":\"" + json_escape(crossing.to_region) +
           "\",\"state\":\"" + std::string(crossing_state_name(crossing.state)) +
           "\",\"position\":{\"x\":" + std::to_string(crossing.position.x) +
           ",\"y\":" + std::to_string(crossing.position.y) +
           ",\"z\":" + std::to_string(crossing.position.z) +
           "},\"velocity\":{\"x\":" + std::to_string(crossing.velocity.x) +
           ",\"y\":" + std::to_string(crossing.velocity.y) +
           ",\"z\":" + std::to_string(crossing.velocity.z) +
           "},\"attachment_state\":" +
           (crossing.attachment_state.empty() ? "null" : crossing.attachment_state) +
           ",\"script_state\":" +
           (crossing.script_state.empty() ? "null" : crossing.script_state) +
           ",\"created_unix\":" + std::to_string(crossing.created_unix) +
           ",\"expires_unix\":" + std::to_string(crossing.expires_unix) +
           ",\"completed_unix\":" + std::to_string(crossing.completed_unix) + "}";
}
