#include "opengenesis/compat/hypergrid/im_adapter.hpp"

#include "opengenesis/platform/socket.hpp"
#include "opengenesis/security/crypto.hpp"

#include <chrono>
#include <cstddef>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace opengenesis::compat::hypergrid {
namespace {

struct HttpUrl {
    std::string host;
    std::uint16_t port{80};
    std::string path{"/"};
};

std::optional<HttpUrl> parse_http_url(const std::string_view value) {
    constexpr std::string_view scheme = "http://";
    if (!value.starts_with(scheme)) return std::nullopt;
    auto rest = value.substr(scheme.size());
    const auto slash = rest.find('/');
    const auto authority = rest.substr(0, slash);
    const auto path = slash == std::string_view::npos ? std::string_view{"/"} : rest.substr(slash);
    if (authority.empty()) return std::nullopt;

    HttpUrl url;
    url.path = std::string{path};
    const auto colon = authority.rfind(':');
    if (colon != std::string_view::npos && authority.find(':') == colon) {
        url.host = std::string{authority.substr(0, colon)};
        try {
            const auto port = std::stoul(std::string{authority.substr(colon + 1)});
            if (port == 0 || port > 65535) return std::nullopt;
            url.port = static_cast<std::uint16_t>(port);
        } catch (...) {
            return std::nullopt;
        }
    } else {
        url.host = std::string{authority};
    }
    return url.host.empty() ? std::nullopt : std::optional<HttpUrl>{std::move(url)};
}

platform::SocketHandle connect_socket(const HttpUrl& url) {
    platform::initialize_sockets();
    addrinfo hints{};
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_family = AF_UNSPEC;
    addrinfo* results = nullptr;
    const auto port_text = std::to_string(url.port);
    if (::getaddrinfo(url.host.c_str(), port_text.c_str(), &hints, &results) != 0) {
        throw std::runtime_error("HG IM DNS failed");
    }

    platform::SocketHandle socket = platform::kInvalidSocket;
    for (auto* current = results; current; current = current->ai_next) {
        socket = ::socket(current->ai_family, current->ai_socktype, current->ai_protocol);
        if (!platform::socket_valid(socket)) continue;
        platform::set_socket_timeouts(socket, std::chrono::seconds{10});
        if (::connect(socket, current->ai_addr,
                      static_cast<platform::SocketLength>(current->ai_addrlen)) == 0) {
            break;
        }
        platform::close_socket(socket);
    }
    ::freeaddrinfo(results);
    if (!platform::socket_valid(socket)) throw std::runtime_error("HG IM connect failed");
    return socket;
}

void send_all(const platform::SocketHandle socket, std::string_view data) {
    while (!data.empty()) {
        const auto sent = platform::send_bytes(socket, data.data(), data.size());
        if (sent <= 0) throw std::runtime_error("HG IM send failed");
        data.remove_prefix(static_cast<std::size_t>(sent));
    }
}

std::string receive_all(const platform::SocketHandle socket) {
    std::string response;
    char buffer[4096];
    while (response.size() < 1024U * 1024U) {
        const auto count = platform::receive_bytes(socket, buffer, sizeof(buffer));
        if (count == 0) break;
        if (count < 0) {
            const auto error = platform::last_socket_error();
            if (platform::socket_error_interrupted(error)) continue;
            break;
        }
        response.append(buffer, static_cast<std::size_t>(count));
    }
    return response;
}

std::optional<std::string> post_xml(const HttpUrl& url, const std::string_view body) {
    auto socket = connect_socket(url);
    try {
        const auto request =
            "POST " + url.path + " HTTP/1.1\r\nHost: " + url.host +
            "\r\nUser-Agent: OpenGenesisLINK-Hypergrid/4.5\r\n"
            "Content-Type: text/xml\r\nContent-Length: " +
            std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n" +
            std::string{body};
        send_all(socket, request);
        const auto response = receive_all(socket);
        platform::close_socket(socket);
        const auto header_end = response.find("\r\n\r\n");
        const auto first_end = response.find("\r\n");
        if (header_end == std::string::npos || first_end == std::string::npos) return std::nullopt;
        if (response.substr(0, first_end).find(" 200 ") == std::string::npos) return std::nullopt;
        return response.substr(header_end + 4);
    } catch (...) {
        platform::close_socket(socket);
        throw;
    }
}

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
    if (visitor->im_uri.starts_with("https://")) {
        reason = "https-hg-im-not-yet-supported";
        return false;
    }
    const auto url = parse_http_url(visitor->im_uri);
    if (!url) {
        reason = "invalid-remote-im-uri";
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
        const auto response = post_xml(*url, body);
        if (!response) {
            reason = "remote-im-http-failed";
            return false;
        }
        const auto fields = parse_xmlrpc_struct_response(*response);
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
