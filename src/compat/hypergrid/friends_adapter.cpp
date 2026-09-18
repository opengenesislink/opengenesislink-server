#include "opengenesis/compat/hypergrid/friends_adapter.hpp"

#include "opengenesis/compat/hypergrid/session_store.hpp"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace opengenesis::compat::hypergrid {
namespace {

int hex_value(const char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + c - 'a';
    if (c >= 'A' && c <= 'F') return 10 + c - 'A';
    return -1;
}

std::string decode_component(const std::string_view value) {
    std::string output;
    output.reserve(value.size());
    for (std::size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '+') {
            output.push_back(' ');
        } else if (value[i] == '%' && i + 2 < value.size()) {
            const int hi = hex_value(value[i + 1]);
            const int lo = hex_value(value[i + 2]);
            if (hi < 0 || lo < 0) throw std::runtime_error("invalid form encoding");
            output.push_back(static_cast<char>((hi << 4) | lo));
            i += 2;
        } else {
            output.push_back(value[i]);
        }
    }
    return output;
}

std::string xml_escape(const std::string_view value) {
    std::string output;
    output.reserve(value.size());
    for (const char c : value) {
        switch (c) {
            case '&': output += "&amp;"; break;
            case '<': output += "&lt;"; break;
            case '>': output += "&gt;"; break;
            case '"': output += "&quot;"; break;
            case '\'': output += "&apos;"; break;
            default: output.push_back(c); break;
        }
    }
    return output;
}

std::string response_fields(const std::vector<std::pair<std::string, std::string>>& fields) {
    std::ostringstream output;
    output << "<?xml version=\"1.0\"?><ServerResponse>";
    for (const auto& [name, value] : fields) {
        output << '<' << name << '>' << xml_escape(value) << "</" << name << '>';
    }
    output << "</ServerResponse>";
    return output.str();
}

std::string bool_response(const bool value) {
    return response_fields({{"RESULT", value ? "true" : "false"}});
}

std::string success_response() {
    return response_fields({{"Result", "true"}, {"RESULT", "Success"}});
}

std::string failure_response(const std::string_view message = {}) {
    return response_fields({{"RESULT", "Failure"}, {"Message", std::string{message}}});
}

std::optional<std::uint32_t> u32(const std::unordered_map<std::string, std::string>& fields,
                                 const std::string_view name) {
    const auto it = fields.find(std::string{name});
    if (it == fields.end()) return std::nullopt;
    std::uint32_t value = 0;
    const auto [end, error] = std::from_chars(
        it->second.data(), it->second.data() + it->second.size(), value);
    if (error != std::errc{} || end != it->second.data() + it->second.size()) {
        return std::nullopt;
    }
    return value;
}

std::string first_segment(const std::string_view value) {
    const auto end = value.find(';');
    return std::string{value.substr(0, end)};
}

} // namespace

std::unordered_map<std::string, std::string> parse_form_urlencoded(const std::string_view body) {
    std::unordered_map<std::string, std::string> fields;
    std::size_t start = 0;
    while (start <= body.size()) {
        const auto end = body.find('&', start);
        const auto part = body.substr(start, end == std::string_view::npos
                                                ? std::string_view::npos
                                                : end - start);
        if (!part.empty()) {
            const auto equal = part.find('=');
            const auto key = decode_component(part.substr(0, equal));
            const auto value = equal == std::string_view::npos
                                   ? std::string{}
                                   : decode_component(part.substr(equal + 1));
            if (!key.empty() && key.size() <= 128 && value.size() <= 4096) {
                fields[key] = value;
            }
        }
        if (end == std::string_view::npos) break;
        start = end + 1;
    }
    return fields;
}

HypergridFriendsAdapter::HypergridFriendsAdapter(
    std::shared_ptr<core::IdentityStore> identities,
    std::shared_ptr<core::FriendsStore> friends,
    std::shared_ptr<core::PresenceStore> presences,
    std::shared_ptr<core::NotificationStore> notifications,
    std::shared_ptr<HypergridSessionStore> sessions)
    : identities_(std::move(identities)),
      friends_(std::move(friends)),
      presences_(std::move(presences)),
      notifications_(std::move(notifications)),
      sessions_(std::move(sessions)) {
    if (!identities_ || !friends_ || !presences_ || !notifications_ || !sessions_) {
        throw std::invalid_argument("Hypergrid Friends dependencies required");
    }
}

std::optional<std::string> HypergridFriendsAdapter::native_user_for_legacy(
    const std::string_view legacy_uuid) const {
    for (const auto& user : identities_->list()) {
        if (legacy_uuid_from_seed(user.id) == legacy_uuid) return user.id;
    }
    return std::nullopt;
}

std::string HypergridFriendsAdapter::remote_key(const std::string_view value) {
    const auto id = first_segment(value);
    return id.empty() ? std::string{} : "hg:" + id;
}

std::string HypergridFriendsAdapter::uui_secret(const std::string_view value) {
    const auto first = value.find(';');
    if (first == std::string_view::npos) return {};
    const auto last = value.rfind(';');
    if (last == std::string_view::npos || last == first || last + 1 >= value.size()) return {};
    return std::string{value.substr(last + 1)};
}

bool HypergridFriendsAdapter::verified(
    const std::unordered_map<std::string, std::string>& fields) const {
    const auto session = fields.find("SESSIONID");
    const auto key = fields.find("KEY");
    return session != fields.end() && key != fields.end() &&
           sessions_->verify_agent(session->second, key->second);
}

std::string HypergridFriendsAdapter::handle_form(const std::string_view body) const {
    std::unordered_map<std::string, std::string> fields;
    try {
        fields = parse_form_urlencoded(body);
    } catch (...) {
        return failure_response("invalid form encoding");
    }

    const auto method_it = fields.find("METHOD");
    if (method_it == fields.end()) return failure_response("missing METHOD");
    const auto& method = method_it->second;

    if (method == "getfriendperms") {
        if (!verified(fields)) return failure_response("invalid service session");
        const auto principal = fields.find("PRINCIPALID");
        const auto friend_id = fields.find("FRIENDID");
        if (principal == fields.end() || friend_id == fields.end()) {
            return failure_response("missing friend identifiers");
        }
        const auto local = native_user_for_legacy(principal->second);
        if (!local) return failure_response("local user not found");
        const auto other_local = native_user_for_legacy(friend_id->second);
        const auto other = other_local.value_or(remote_key(friend_id->second));
        const auto relation = friends_->find_relation(*local, other);
        if (!relation || relation->status != "accepted") return failure_response("Friend not found");

        const auto permissions = relation->user_a == *local
                                     ? relation->flags_b_to_a
                                     : relation->flags_a_to_b;
        return response_fields({{"RESULT", "Success"}, {"Value", std::to_string(permissions)}});
    }

    if (method == "newfriendship") {
        const auto principal = fields.find("PrincipalID");
        const auto friend_value = fields.find("Friend");
        if (principal == fields.end() || friend_value == fields.end()) return bool_response(false);

        const auto local = native_user_for_legacy(principal->second);
        if (!local) return bool_response(false);
        const auto other_local = native_user_for_legacy(first_segment(friend_value->second));
        const auto other = other_local.value_or(remote_key(friend_value->second));
        if (other.empty()) return bool_response(false);

        const auto my_flags = u32(fields, "MyFlags").value_or(0);
        const auto their_flags = u32(fields, "TheirFlags").value_or(0);
        const auto secret = uui_secret(friend_value->second);
        std::string reason;

        if (verified(fields)) {
            const auto pending = friends_->upsert_pending(
                *local, other, my_flags, their_flags, secret, reason);
            return pending ? success_response() : bool_response(false);
        }

        const auto existing = friends_->find_relation(*local, other);
        if (!existing || existing->status != "pending") return bool_response(false);
        const auto accepted = friends_->upsert_accepted(
            *local, other, my_flags == 0 ? 1U : my_flags,
            their_flags == 0 ? 1U : their_flags, secret, reason);
        return accepted ? success_response() : bool_response(false);
    }

    if (method == "deletefriendship") {
        const auto principal = fields.find("PrincipalID");
        const auto friend_value = fields.find("Friend");
        const auto secret = fields.find("SECRET");
        if (principal == fields.end() || friend_value == fields.end() ||
            secret == fields.end() || secret->second.empty()) {
            return bool_response(false);
        }
        const auto local = native_user_for_legacy(principal->second);
        if (!local) return bool_response(false);
        const auto other_local = native_user_for_legacy(first_segment(friend_value->second));
        const auto other = other_local.value_or(remote_key(friend_value->second));
        const auto relation = friends_->find_relation(*local, other);
        if (!relation || relation->interop_secret != secret->second) return bool_response(false);
        return bool_response(friends_->remove(*local, other));
    }

    if (method == "friendship_offered") {
        const auto from = fields.find("FromID");
        const auto to = fields.find("ToID");
        if (from == fields.end() || to == fields.end()) return bool_response(false);
        const auto local = native_user_for_legacy(to->second);
        if (!local) return bool_response(false);
        const auto remote = remote_key(from->second);
        if (remote.empty()) return bool_response(false);

        std::string reason;
        const auto pending = friends_->upsert_pending(remote, *local, 0, 0, {}, reason);
        if (!pending) return bool_response(false);

        const auto name_it = fields.find("FromName");
        const auto message_it = fields.find("Message");
        notifications_->push(
            *local, "friend_request",
            "Hypergrid friendship request",
            (name_it == fields.end() ? from->second : name_it->second) +
                (message_it == fields.end() || message_it->second.empty()
                     ? std::string{}
                     : ": " + message_it->second),
            pending->id);
        return bool_response(true);
    }

    if (method == "validate_friendship_offered") {
        const auto principal = fields.find("PrincipalID");
        const auto friend_value = fields.find("Friend");
        if (principal == fields.end() || friend_value == fields.end()) return bool_response(false);
        const auto to_local = native_user_for_legacy(first_segment(friend_value->second));
        const auto from_local = native_user_for_legacy(principal->second);
        if (!to_local) return bool_response(false);
        const auto from = from_local.value_or(remote_key(principal->second));
        const auto relation = friends_->find_relation(from, *to_local);
        return bool_response(relation && relation->status == "pending");
    }

    if (method == "statusnotification") {
        const auto foreign_id = fields.find("userID");
        const auto online_it = fields.find("online");
        if (foreign_id == fields.end() || online_it == fields.end()) {
            return failure_response("missing status fields");
        }
        const bool online = online_it->second == "true" || online_it->second == "True" ||
                            online_it->second == "1";
        if (!online) return response_fields({{"RESULT", "NULL"}});

        const auto foreign = remote_key(foreign_id->second);
        std::vector<std::pair<std::string, std::string>> response;
        std::size_t index = 0;
        for (const auto& [name, value] : fields) {
            if (!name.starts_with("friend_")) continue;
            const auto local_uuid = first_segment(value);
            const auto local = native_user_for_legacy(local_uuid);
            if (!local) continue;
            const auto relation = friends_->find_relation(*local, foreign);
            if (!relation || relation->status != "accepted") continue;
            const auto secret = uui_secret(value);
            if (!relation->interop_secret.empty() && relation->interop_secret != secret) continue;
            if (!presences_->find_user(*local)) continue;
            response.emplace_back("friend_" + std::to_string(index++), local_uuid);
        }
        if (response.empty()) response.emplace_back("RESULT", "NULL");
        return response_fields(response);
    }

    return failure_response("unsupported METHOD");
}

} // namespace opengenesis::compat::hypergrid
