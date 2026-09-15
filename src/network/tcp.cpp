#include "opengenesis/network/tcp.hpp"

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <utility>
#include <vector>

namespace opengenesis::network {
namespace {

[[noreturn]] void throw_socket_error(const std::string& operation) {
    throw std::runtime_error(operation + ": " + std::strerror(errno));
}

void close_fd(int& fd) noexcept {
    if (fd >= 0) {
        ::close(fd);
        fd = -1;
    }
}

void send_all(const int fd, const std::span<const std::byte> data) {
    std::size_t sent = 0;
    while (sent < data.size()) {
        const auto result = ::send(fd, data.data() + sent, data.size() - sent, MSG_NOSIGNAL);
        if (result < 0) {
            if (errno == EINTR) continue;
            throw_socket_error("send");
        }
        if (result == 0) {
            throw std::runtime_error("send: peer closed connection");
        }
        sent += static_cast<std::size_t>(result);
    }
}

void receive_all(const int fd, const std::span<std::byte> data) {
    std::size_t received = 0;
    while (received < data.size()) {
        const auto result = ::recv(fd, data.data() + received, data.size() - received, 0);
        if (result < 0) {
            if (errno == EINTR) continue;
            throw_socket_error("recv");
        }
        if (result == 0) {
            throw std::runtime_error("recv: peer closed connection");
        }
        received += static_cast<std::size_t>(result);
    }
}

std::uint32_t read_payload_size(const std::array<std::byte, protocol::kHeaderSize>& header) {
    return (std::to_integer<std::uint32_t>(header[12]) << 24U) |
           (std::to_integer<std::uint32_t>(header[13]) << 16U) |
           (std::to_integer<std::uint32_t>(header[14]) << 8U) |
           std::to_integer<std::uint32_t>(header[15]);
}

} // namespace

TcpSocket::TcpSocket(const int fd) noexcept : fd_(fd) {}
TcpSocket::~TcpSocket() { close_fd(fd_); }

TcpSocket::TcpSocket(TcpSocket&& other) noexcept : fd_(std::exchange(other.fd_, -1)) {}
TcpSocket& TcpSocket::operator=(TcpSocket&& other) noexcept {
    if (this != &other) {
        close_fd(fd_);
        fd_ = std::exchange(other.fd_, -1);
    }
    return *this;
}

TcpSocket TcpSocket::connect(const std::string& host, const std::uint16_t port) {
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* raw_results = nullptr;
    const auto service = std::to_string(port);
    const auto rc = ::getaddrinfo(host.c_str(), service.c_str(), &hints, &raw_results);
    if (rc != 0) {
        throw std::runtime_error("getaddrinfo: " + std::string{gai_strerror(rc)});
    }

    struct Guard { addrinfo* p; ~Guard() { if (p) freeaddrinfo(p); } } guard{raw_results};
    for (auto* current = raw_results; current != nullptr; current = current->ai_next) {
        const int fd = ::socket(current->ai_family, current->ai_socktype, current->ai_protocol);
        if (fd < 0) continue;
        if (::connect(fd, current->ai_addr, current->ai_addrlen) == 0) {
            return TcpSocket{fd};
        }
        ::close(fd);
    }
    throw_socket_error("connect");
}

void TcpSocket::send_frame(const protocol::Frame& frame) const {
    if (!valid()) throw std::runtime_error("send_frame on invalid socket");
    const auto bytes = protocol::encode(frame);
    send_all(fd_, bytes);
}

protocol::Frame TcpSocket::receive_frame() const {
    if (!valid()) throw std::runtime_error("receive_frame on invalid socket");
    std::array<std::byte, protocol::kHeaderSize> header{};
    receive_all(fd_, header);
    const auto payload_size = read_payload_size(header);
    if (payload_size > protocol::kMaxPayloadSize) {
        throw std::runtime_error("Incoming OGL payload exceeds maximum size");
    }
    std::vector<std::byte> bytes(header.begin(), header.end());
    bytes.resize(protocol::kHeaderSize + payload_size);
    if (payload_size > 0) {
        receive_all(fd_, std::span<std::byte>{bytes}.subspan(protocol::kHeaderSize));
    }
    return protocol::decode(bytes);
}

bool TcpSocket::valid() const noexcept { return fd_ >= 0; }

TcpListener::TcpListener(const std::string& address, const std::uint16_t port) {
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    addrinfo* raw_results = nullptr;
    const auto service = std::to_string(port);
    const char* host = (address.empty() || address == "0.0.0.0" || address == "::") ? nullptr : address.c_str();
    const auto rc = ::getaddrinfo(host, service.c_str(), &hints, &raw_results);
    if (rc != 0) {
        throw std::runtime_error("listener getaddrinfo: " + std::string{gai_strerror(rc)});
    }

    struct Guard { addrinfo* p; ~Guard() { if (p) freeaddrinfo(p); } } guard{raw_results};
    int last_errno = 0;
    for (auto* current = raw_results; current != nullptr; current = current->ai_next) {
        const int candidate = ::socket(current->ai_family, current->ai_socktype, current->ai_protocol);
        if (candidate < 0) {
            last_errno = errno;
            continue;
        }
        const int reuse = 1;
        (void)::setsockopt(candidate, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
        if (current->ai_family == AF_INET6) {
            const int v6_only = 0;
            (void)::setsockopt(candidate, IPPROTO_IPV6, IPV6_V6ONLY, &v6_only, sizeof(v6_only));
        }
        if (::bind(candidate, current->ai_addr, current->ai_addrlen) == 0 && ::listen(candidate, SOMAXCONN) == 0) {
            fd_ = candidate;
            return;
        }
        last_errno = errno;
        ::close(candidate);
    }
    errno = last_errno;
    throw_socket_error("bind/listen");
}
TcpListener::~TcpListener() { close_fd(fd_); }

std::optional<TcpSocket> TcpListener::accept_for(const std::chrono::milliseconds timeout) const {
    pollfd descriptor{.fd = fd_, .events = POLLIN, .revents = 0};
    const auto timeout_ms = static_cast<int>(timeout.count());
    const auto ready = ::poll(&descriptor, 1, timeout_ms);
    if (ready < 0) {
        if (errno == EINTR) return std::nullopt;
        throw_socket_error("poll");
    }
    if (ready == 0) return std::nullopt;
    const int client = ::accept(fd_, nullptr, nullptr);
    if (client < 0) {
        if (errno == EINTR) return std::nullopt;
        throw_socket_error("accept");
    }
    return TcpSocket{client};
}

} // namespace opengenesis::network
