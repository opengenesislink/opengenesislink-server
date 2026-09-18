#include "opengenesis/compat/hypergrid/http_client.hpp"

#include "opengenesis/platform/socket.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstddef>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace opengenesis::compat::hypergrid {
namespace {

struct ParsedUrl {
    std::string host;
    std::uint16_t port{80};
    std::string path{"/"};
};

std::optional<ParsedUrl> parse_url(const std::string_view value) {
    constexpr std::string_view scheme = "http://";
    if (!value.starts_with(scheme)) return std::nullopt;

    auto rest = value.substr(scheme.size());
    const auto slash = rest.find('/');
    const auto authority = rest.substr(0, slash);
    const auto path = slash == std::string_view::npos ? std::string_view{"/"} : rest.substr(slash);
    if (authority.empty()) return std::nullopt;

    ParsedUrl url;
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
    return url.host.empty() ? std::nullopt : std::optional<ParsedUrl>{std::move(url)};
}

void send_all(const platform::SocketHandle socket, std::string_view data) {
    while (!data.empty()) {
        const auto sent = platform::send_bytes(socket, data.data(), data.size());
        if (sent <= 0) throw std::runtime_error("send failed");
        data.remove_prefix(static_cast<std::size_t>(sent));
    }
}

std::string receive_all(const platform::SocketHandle socket) {
    std::string response;
    char buffer[4096];
    while (response.size() < 8U * 1024U * 1024U) {
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

std::optional<std::string> header_value(const std::string_view headers,
                                        const std::string_view wanted) {
    std::istringstream stream{std::string{headers}};
    std::string line;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const auto colon = line.find(':');
        if (colon == std::string::npos) continue;
        auto name = line.substr(0, colon);
        std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        if (name != wanted) continue;
        auto value = line.substr(colon + 1);
        while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())) != 0) {
            value.erase(value.begin());
        }
        return value;
    }
    return std::nullopt;
}

} // namespace

std::optional<HttpResponse> http_request(
    const std::string_view url_value,
    const std::string_view method,
    const std::string_view content_type,
    const std::string_view body,
    const std::unordered_map<std::string, std::string>& headers,
    std::string& reason) {
    if (url_value.starts_with("https://")) {
        reason = "https-not-yet-supported";
        return std::nullopt;
    }
    const auto url = parse_url(url_value);
    if (!url) {
        reason = "invalid-http-url";
        return std::nullopt;
    }

    platform::initialize_sockets();
    addrinfo hints{};
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_family = AF_UNSPEC;
    addrinfo* results = nullptr;
    const auto port_text = std::to_string(url->port);
    if (::getaddrinfo(url->host.c_str(), port_text.c_str(), &hints, &results) != 0) {
        reason = "dns-failed";
        return std::nullopt;
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
        reason = "connect-failed";
        return std::nullopt;
    }

    try {
        std::ostringstream request;
        request << method << ' ' << url->path << " HTTP/1.1\r\n"
                << "Host: " << url->host << "\r\n"
                << "User-Agent: OpenGenesisLINK-Hypergrid/4.5\r\n"
                << "Connection: close\r\n";
        if (!content_type.empty()) request << "Content-Type: " << content_type << "\r\n";
        for (const auto& [name, value] : headers) {
            request << name << ": " << value << "\r\n";
        }
        request << "Content-Length: " << body.size() << "\r\n\r\n" << body;
        send_all(socket, request.str());
        const auto raw = receive_all(socket);
        platform::close_socket(socket);

        const auto header_end = raw.find("\r\n\r\n");
        const auto first_end = raw.find("\r\n");
        if (header_end == std::string::npos || first_end == std::string::npos) {
            reason = "invalid-http-response";
            return std::nullopt;
        }

        const auto first = raw.substr(0, first_end);
        const auto first_space = first.find(' ');
        if (first_space == std::string::npos) {
            reason = "invalid-http-status";
            return std::nullopt;
        }
        int status = 0;
        try {
            status = std::stoi(first.substr(first_space + 1, 3));
        } catch (...) {
            reason = "invalid-http-status";
            return std::nullopt;
        }

        const auto header_block = std::string_view{raw}.substr(first_end + 2, header_end - first_end - 2);
        reason.clear();
        return HttpResponse{
            .status = status,
            .content_type = header_value(header_block, "content-type").value_or(""),
            .body = raw.substr(header_end + 4)};
    } catch (...) {
        platform::close_socket(socket);
        reason = "http-request-failed";
        return std::nullopt;
    }
}

} // namespace opengenesis::compat::hypergrid
