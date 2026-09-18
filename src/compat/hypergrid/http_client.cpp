#include "opengenesis/compat/hypergrid/http_client.hpp"

#include "opengenesis/platform/socket.hpp"

#include <openssl/err.h>
#include <openssl/ssl.h>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstddef>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace opengenesis::compat::hypergrid {
namespace {

struct ParsedUrl {
    bool secure{false};
    std::string host;
    std::uint16_t port{80};
    std::string path{"/"};
};

std::optional<ParsedUrl> parse_url(const std::string_view value) {
    ParsedUrl url;
    std::string_view rest;
    if (value.starts_with("http://")) {
        rest = value.substr(7);
        url.port = 80;
    } else if (value.starts_with("https://")) {
        rest = value.substr(8);
        url.secure = true;
        url.port = 443;
    } else {
        return std::nullopt;
    }

    const auto slash = rest.find('/');
    const auto authority = rest.substr(0, slash);
    url.path = slash == std::string_view::npos ? "/" : std::string{rest.substr(slash)};
    if (authority.empty()) return std::nullopt;

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

std::string request_bytes(
    const ParsedUrl& url,
    const std::string_view method,
    const std::string_view content_type,
    const std::string_view body,
    const std::unordered_map<std::string, std::string>& headers) {
    std::ostringstream request;
    request << method << ' ' << url.path << " HTTP/1.1\r\n"
            << "Host: " << url.host << "\r\n"
            << "User-Agent: OpenGenesisLINK-Hypergrid/4.5\r\n"
            << "Connection: close\r\n";
    if (!content_type.empty()) request << "Content-Type: " << content_type << "\r\n";
    for (const auto& [name, value] : headers) request << name << ": " << value << "\r\n";
    request << "Content-Length: " << body.size() << "\r\n\r\n" << body;
    return request.str();
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

std::optional<HttpResponse> parse_response(const std::string& raw, std::string& reason) {
    const auto header_end = raw.find("\r\n\r\n");
    const auto first_end = raw.find("\r\n");
    if (header_end == std::string::npos || first_end == std::string::npos) {
        reason = "invalid-http-response";
        return std::nullopt;
    }
    const auto first = raw.substr(0, first_end);
    const auto first_space = first.find(' ');
    if (first_space == std::string::npos || first_space + 4 > first.size()) {
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

    const auto header_block = std::string_view{raw}.substr(
        first_end + 2, header_end - first_end - 2);
    reason.clear();
    return HttpResponse{
        .status = status,
        .content_type = header_value(header_block, "content-type").value_or(""),
        .body = raw.substr(header_end + 4)};
}

void socket_send_all(const platform::SocketHandle socket, std::string_view data) {
    while (!data.empty()) {
        const auto sent = platform::send_bytes(socket, data.data(), data.size());
        if (sent <= 0) throw std::runtime_error("send failed");
        data.remove_prefix(static_cast<std::size_t>(sent));
    }
}

std::string socket_receive_all(const platform::SocketHandle socket) {
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

std::optional<std::string> plain_request(const ParsedUrl& url,
                                         const std::string_view request,
                                         std::string& reason) {
    platform::initialize_sockets();
    addrinfo hints{};
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_family = AF_UNSPEC;
    addrinfo* results = nullptr;
    const auto port_text = std::to_string(url.port);
    if (::getaddrinfo(url.host.c_str(), port_text.c_str(), &hints, &results) != 0) {
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
        socket_send_all(socket, request);
        auto response = socket_receive_all(socket);
        platform::close_socket(socket);
        return response;
    } catch (...) {
        platform::close_socket(socket);
        reason = "http-request-failed";
        return std::nullopt;
    }
}

struct SslCtxDeleter {
    void operator()(SSL_CTX* value) const noexcept { SSL_CTX_free(value); }
};
struct BioDeleter {
    void operator()(BIO* value) const noexcept { BIO_free_all(value); }
};

std::optional<std::string> tls_request(const ParsedUrl& url,
                                       const std::string_view request,
                                       std::string& reason) {
    std::unique_ptr<SSL_CTX, SslCtxDeleter> context{SSL_CTX_new(TLS_client_method())};
    if (!context) {
        reason = "tls-context-failed";
        return std::nullopt;
    }
    SSL_CTX_set_verify(context.get(), SSL_VERIFY_PEER, nullptr);
    if (SSL_CTX_set_default_verify_paths(context.get()) != 1) {
        reason = "tls-ca-store-failed";
        return std::nullopt;
    }

    std::unique_ptr<BIO, BioDeleter> bio{BIO_new_ssl_connect(context.get())};
    if (!bio) {
        reason = "tls-bio-failed";
        return std::nullopt;
    }

    SSL* ssl = nullptr;
    BIO_get_ssl(bio.get(), &ssl);
    if (!ssl) {
        reason = "tls-session-failed";
        return std::nullopt;
    }
    SSL_set_mode(ssl, SSL_MODE_AUTO_RETRY);
    if (SSL_set_tlsext_host_name(ssl, url.host.c_str()) != 1 ||
        SSL_set1_host(ssl, url.host.c_str()) != 1) {
        reason = "tls-hostname-setup-failed";
        return std::nullopt;
    }

    const auto endpoint = url.host + ":" + std::to_string(url.port);
    BIO_set_conn_hostname(bio.get(), endpoint.c_str());
    if (BIO_do_connect(bio.get()) <= 0 || BIO_do_handshake(bio.get()) <= 0) {
        reason = "tls-connect-failed";
        return std::nullopt;
    }
    if (SSL_get_verify_result(ssl) != X509_V_OK) {
        reason = "tls-certificate-rejected";
        return std::nullopt;
    }

    std::size_t offset = 0;
    while (offset < request.size()) {
        const auto chunk = static_cast<int>(
            std::min<std::size_t>(request.size() - offset, 16U * 1024U));
        const int written = BIO_write(bio.get(), request.data() + offset, chunk);
        if (written <= 0) {
            if (BIO_should_retry(bio.get())) continue;
            reason = "tls-send-failed";
            return std::nullopt;
        }
        offset += static_cast<std::size_t>(written);
    }
    if (BIO_flush(bio.get()) <= 0) {
        reason = "tls-flush-failed";
        return std::nullopt;
    }

    std::string response;
    char buffer[4096];
    while (response.size() < 8U * 1024U * 1024U) {
        const int count = BIO_read(bio.get(), buffer, static_cast<int>(sizeof(buffer)));
        if (count > 0) {
            response.append(buffer, static_cast<std::size_t>(count));
            continue;
        }
        if (BIO_should_retry(bio.get())) continue;
        break;
    }
    if (response.empty()) {
        reason = "tls-empty-response";
        return std::nullopt;
    }
    return response;
}

} // namespace

std::optional<HttpResponse> http_request(
    const std::string_view url_value,
    const std::string_view method,
    const std::string_view content_type,
    const std::string_view body,
    const std::unordered_map<std::string, std::string>& headers,
    std::string& reason) {
    const auto url = parse_url(url_value);
    if (!url) {
        reason = "invalid-http-url";
        return std::nullopt;
    }

    const auto request = request_bytes(*url, method, content_type, body, headers);
    const auto raw = url->secure ? tls_request(*url, request, reason)
                                 : plain_request(*url, request, reason);
    if (!raw) return std::nullopt;
    return parse_response(*raw, reason);
}

} // namespace opengenesis::compat::hypergrid
