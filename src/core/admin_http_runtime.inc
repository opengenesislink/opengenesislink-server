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
        else if (action.type == scripting::ScriptActionType::notify_owner) type = "notify";
        else if (action.type == scripting::ScriptActionType::direct_message) type = "message";
        else if (action.type == scripting::ScriptActionType::world_move) type = "move";
        else if (action.type == scripting::ScriptActionType::world_rotate) type = "rotate";
        else if (action.type == scripting::ScriptActionType::world_scale) type = "scale";
        else if (action.type == scripting::ScriptActionType::world_physics) type = "physics";
        else if (action.type == scripting::ScriptActionType::world_chat_say) type = "say";
        else if (action.type == scripting::ScriptActionType::world_chat_whisper) type = "whisper";
        else if (action.type == scripting::ScriptActionType::world_chat_shout) type = "shout";
        else if (action.type == scripting::ScriptActionType::world_query_object) type = "object_info";
        else if (action.type == scripting::ScriptActionType::world_query_region) type = "region_info";
        else if (action.type == scripting::ScriptActionType::world_query_terrain) type = "terrain_height";
        else if (action.type == scripting::ScriptActionType::world_query_nearby) type = "nearby_avatars";
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
           "},\"rotation\":{\"x\":" + std::to_string(crossing.rotation.x) +
           ",\"y\":" + std::to_string(crossing.rotation.y) +
           ",\"z\":" + std::to_string(crossing.rotation.z) +
           "},\"angular_velocity\":{\"x\":" +
           std::to_string(crossing.angular_velocity.x) +
           ",\"y\":" + std::to_string(crossing.angular_velocity.y) +
           ",\"z\":" + std::to_string(crossing.angular_velocity.z) +
           "},\"attachment_state\":" +
           (crossing.attachment_state.empty() ? "null" : crossing.attachment_state) +
           ",\"script_state\":" +
           (crossing.script_state.empty() ? "null" : crossing.script_state) +
           ",\"physics_state\":" +
           (crossing.physics_state.empty() ? "null" : crossing.physics_state) +
           ",\"linkset_state\":" +
           (crossing.linkset_state.empty() ? "null" : crossing.linkset_state) +
           ",\"reservation_token\":" +
           (crossing.reservation_token.empty()
                ? "null"
                : "\"" + json_escape(crossing.reservation_token) + "\"") +
           ",\"rollback_reason\":" +
           (crossing.rollback_reason.empty()
                ? "null"
                : "\"" + json_escape(crossing.rollback_reason) + "\"") +
           ",\"created_unix\":" + std::to_string(crossing.created_unix) +
           ",\"expires_unix\":" + std::to_string(crossing.expires_unix) +
           ",\"reserved_unix\":" + std::to_string(crossing.reserved_unix) +
           ",\"completed_unix\":" + std::to_string(crossing.completed_unix) +
           ",\"rolled_back_unix\":" + std::to_string(crossing.rolled_back_unix) + "}";
}


std::string object_crossing_json(
    const ObjectCrossingRecord& crossing) {
    return "{\"id\":\"" + json_escape(crossing.id) +
           "\",\"owner_user_id\":\"" + json_escape(crossing.owner_user_id) +
           "\",\"source_region\":\"" + json_escape(crossing.source_region) +
           "\",\"destination_region\":\"" +
           json_escape(crossing.destination_region) +
           "\",\"source_entity_id\":" +
           std::to_string(crossing.source_entity_id) +
           ",\"destination_entity_id\":" +
           std::to_string(crossing.destination_entity_id) +
           ",\"state\":\"" +
           std::string(object_crossing_state_name(crossing.state)) +
           "\",\"destination_position\":{\"x\":" +
           std::to_string(crossing.destination_position.x) +
           ",\"y\":" + std::to_string(crossing.destination_position.y) +
           ",\"z\":" + std::to_string(crossing.destination_position.z) +
           "},\"export_attempts\":" +
           std::to_string(crossing.export_attempts) +
           ",\"import_attempts\":" +
           std::to_string(crossing.import_attempts) +
           ",\"remove_attempts\":" +
           std::to_string(crossing.remove_attempts) +
           ",\"cleanup_attempts\":" +
           std::to_string(crossing.cleanup_attempts) +
           ",\"created_unix\":" +
           std::to_string(crossing.created_unix) +
           ",\"expires_unix\":" +
           std::to_string(crossing.expires_unix) +
           ",\"exported_unix\":" +
           std::to_string(crossing.exported_unix) +
           ",\"imported_unix\":" +
           std::to_string(crossing.imported_unix) +
           ",\"completed_unix\":" +
           std::to_string(crossing.completed_unix) +
           ",\"rolled_back_unix\":" +
           std::to_string(crossing.rolled_back_unix) +
           ",\"last_error\":" +
           (crossing.last_error.empty()
                ? "null"
                : "\"" + json_escape(crossing.last_error) + "\"") +
           ",\"rollback_reason\":" +
           (crossing.rollback_reason.empty()
                ? "null"
                : "\"" + json_escape(crossing.rollback_reason) + "\"") +
           "}";
}

std::string object_crossings_json(
    const std::vector<ObjectCrossingRecord>& crossings,
    const std::string_view owner_user_id) {
    std::ostringstream out;
    out << '[';
    bool first = true;
    for (const auto& crossing : crossings) {
        if (!owner_user_id.empty() &&
            crossing.owner_user_id != owner_user_id) {
            continue;
        }
        if (!first) out << ',';
        first = false;
        out << object_crossing_json(crossing);
    }
    out << ']';
    return out.str();
}

std::string string_list_json(const std::vector<std::string>& values) {
    std::ostringstream out;
    out << '[';
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i) out << ',';
        out << "\"" << json_escape(values[i]) << "\"";
    }
    out << ']';
    return out.str();
}
