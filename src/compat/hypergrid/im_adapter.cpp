#include "opengenesis/compat/hypergrid/im_adapter.hpp"

#include "opengenesis/compat/hypergrid/http_client.hpp"
#include "opengenesis/security/crypto.hpp"

#include <chrono>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace opengenesis::compat::hypergrid {
namespace {

bool true_text(const std::string_view value) {
    return value == "TRUE" || value == "True" || value == "true" || value == "1";
}

std::int64_t unix_now() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

} // namespace

HypergridInstantMessageAdapter::HypergridInstantMessageAdapter(
    std::shared_ptr<core::IdentityStore> identities,
    std::shared_ptr<core::MessageStore> messages,
    std::shared_ptr<core::NotificationStore> notifications,
    std::shared_ptr<HypergridSessionStore> sessions)
    : identities_(std::move(identities)),
      messages_(std::move(messages)),
      notifications_(std::move(notifications)),
      sessions_(std::move(sessions)) {
    if (!identities_ || !messages_ || !notifications_ || !sessions_) {
        throw std::invalid_argument("HG IM dependencies required");
    }
}

std::optional<std::string> HypergridInstantMessageAdapter::native_user_for_legacy(
    const std::string_view legacy_uuid) const {
    for (const auto& user : identities_->list()) {
        if (legacy_uuid_from_seed(user.id) == legacy_uuid) return user.id;
    }
    return std::nullopt;
}

std::unordered_map<std::string, std::string>
HypergridInstantMessageAdapter::handle_incoming(const XmlRpcCall& call) const {
    if (call.method != "grid_instant_message") return {{"success", "FALSE"}};
    const auto get = [&](const std::string_view key) -> std::string {
        const auto it = call.fields.find(std::string{key});
        return it == call.fields.end() ? std::string{} : it->second;
    };

    const auto from = get("from_agent_id");
    const auto to = native_user_for_legacy(get("to_agent_id"));
    const auto name = get("from_agent_name");
    const auto text = get("message");
    if (from.empty() || !to || text.empty()) return {{"success", "FALSE"}};

    std::string reason;
    const auto message = messages_->send("hg:" + from, *to, text, reason);
    if (!message) return {{"success", "FALSE"}};

    notifications_->push(*to, "hypergrid_im",
                         name.empty() ? "Hypergrid message" : name,
                         text, message->id);
    return {{"success", "TRUE"}};
}

bool HypergridInstantMessageAdapter::send_remote(
    const std::string_view sender_native_id,
    const std::string_view sender_name,
    const std::string_view target_agent_id,
    const std::string_view text,
    std::string& reason) const {
    const auto visitor = sessions_->foreign_by_agent(target_agent_id);
    if (!visitor || visitor->im_uri.empty()) {
        reason = "remote-im-service-unavailable";
        return false;
    }
    const auto sender_legacy = legacy_uuid_from_seed(sender_native_id);
    const auto session = legacy_uuid_from_seed(security::random_hex(32));
    const auto body = xmlrpc_struct_call(
        "grid_instant_message",
        {{"from_agent_id", sender_legacy},
         {"from_agent_session", "00000000-0000-0000-0000-000000000000"},
         {"to_agent_id", std::string{target_agent_id}},
         {"im_session_id", session},
         {"timestamp", std::to_string(unix_now())},
         {"from_agent_name", std::string{sender_name}},
         {"message", std::string{text}},
         {"dialog", "AA=="},
         {"from_group", "FALSE"},
         {"offline", "AA=="},
         {"parent_estate_id", "0"},
         {"position_x", "0"},
         {"position_y", "0"},
         {"position_z", "0"},
         {"region_id", "00000000-0000-0000-0000-000000000000"},
         {"binary_bucket", ""}});

    try {
        const auto response = http_request(
            visitor->im_uri, "POST", "text/xml", body,
            std::unordered_map<std::string, std::string>{}, reason);
        if (!response || response->status != 200) {
            if (reason == "https-not-yet-supported") {
                reason = "https-hg-im-not-yet-supported";
            } else if (reason.empty()) {
                reason = "remote-im-http-failed";
            }
            return false;
        }
        const auto fields = parse_xmlrpc_struct_response(response->body);
        if (!fields) {
            reason = "remote-im-invalid-response";
            return false;
        }
        const auto it = fields->find("success");
        if (it == fields->end() || !true_text(it->second)) {
            reason = "remote-im-rejected";
            return false;
        }

        std::string store_reason;
        (void)messages_->send(std::string{sender_native_id},
                              "hg:" + std::string{target_agent_id},
                              std::string{text}, store_reason);
        reason.clear();
        return true;
    } catch (...) {
        reason = "remote-im-unreachable";
        return false;
    }
}

} // namespace opengenesis::compat::hypergrid
