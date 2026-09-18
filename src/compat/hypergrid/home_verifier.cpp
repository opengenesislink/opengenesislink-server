#include "opengenesis/compat/hypergrid/home_verifier.hpp"

#include "opengenesis/compat/hypergrid/xmlrpc.hpp"
#include "opengenesis/platform/socket.hpp"

#include <chrono>
#include <cstddef>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

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

    if (authority.front() == '[') {
        const auto close = authority.find(']');
        if (close == std::string_view::npos) return std::nullopt;
        url.host = std::string{authority.substr(1, close - 1)};
        if (close + 1 < authority.size()) {
            if (authority[close + 1] != ':') return std::nullopt;
            try {
                const auto port = std::stoul(std::string{authority.substr(close + 2)});
                if (port == 0 || port > 65535) return std::nullopt;
                url.port = static_cast<std::uint16_t>(port);
            } catch (...) {
                return std::nullopt;
            }
        }
    } else {
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
        throw std::runtime_error("Hypergrid home DNS resolution failed");
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
    if (!platform::socket_valid(socket)) {
        throw std::runtime_error("Hypergrid home connection failed");
    }
    return socket;
}

void send_all(const platform::SocketHandle socket, std::string_view data) {
    while (!data.empty()) {
        const auto sent = platform::send_bytes(socket, data.data(), data.size());
        if (sent <= 0) throw std::runtime_error("Hypergrid home send failed");
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
            "POST " + url.path + " HTTP/1.1\r\nHost: " + url.host + "\r\n"
            "User-Agent: OpenGenesisLINK-Hypergrid/3.5\r\n"
            "Content-Type: text/xml\r\n"
            "Content-Length: " + std::to_string(body.size()) + "\r\n"
            "Connection: close\r\n\r\n" + std::string{body};
        send_all(socket, request);
        const auto response = receive_all(socket);
        platform::close_socket(socket);

        const auto header_end = response.find("\r\n\r\n");
        if (header_end == std::string::npos) return std::nullopt;
        const auto first_end = response.find("\r\n");
        if (first_end == std::string::npos) return std::nullopt;
        const auto status = response.substr(0, first_end);
        if (status.find(" 200 ") == std::string::npos) return std::nullopt;
        return response.substr(header_end + 4);
    } catch (...) {
        platform::close_socket(socket);
        throw;
    }
}

bool bool_text(const std::string_view value) {
    return value == "true" || value == "True" || value == "TRUE" || value == "1";
}

} // namespace

bool HttpHypergridHomeVerifier::verify_agent(
    const std::string_view home_uri,
    const std::string_view session_id,
    const std::string_view service_token,
    std::string& reason) {
    return call_bool(home_uri, "verify_agent", session_id, "token", service_token, reason);
}

bool HttpHypergridHomeVerifier::verify_client(
    const std::string_view home_uri,
    const std::string_view session_id,
    const std::string_view reported_ip,
    std::string& reason) {
    return call_bool(home_uri, "verify_client", session_id, "token", reported_ip, reason);
}

bool HttpHypergridHomeVerifier::call_bool(
    const std::string_view home_uri,
    const std::string_view method,
    const std::string_view session_id,
    const std::string_view value_name,
    const std::string_view value,
    std::string& reason) {
    if (home_uri.starts_with("https://")) {
        reason = "https-home-verification-not-yet-supported";
        return false;
    }
    const auto url = parse_http_url(home_uri);
    if (!url) {
        reason = "invalid-home-uri";
        return false;
    }

    try {
        const auto body = xmlrpc_struct_call(
            method, {{"sessionID", std::string{session_id}},
                     {std::string{value_name}, std::string{value}}});
        const auto response = post_xml(*url, body);
        if (!response) {
            reason = "home-verification-http-failed";
            return false;
        }
        const auto fields = parse_xmlrpc_struct_response(*response);
        if (!fields) {
            reason = "home-verification-invalid-response";
            return false;
        }
        const auto it = fields->find("result");
        if (it == fields->end() || !bool_text(it->second)) {
            reason = "home-verification-rejected";
            return false;
        }
        reason.clear();
        return true;
    } catch (...) {
        reason = "home-verification-unreachable";
        return false;
    }
}

} // namespace opengenesis::compat::hypergrid
