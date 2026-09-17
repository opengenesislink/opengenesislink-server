#include "opengenesis/network/tcp.hpp"

#include <array>
#include <cstddef>
#include <netdb.h>
#include <stdexcept>
#include <vector>

namespace opengenesis::network {
namespace {

void send_all(const platform::SocketHandle fd, const std::byte* data, std::size_t size) {
    while (size != 0) {
        const auto count = platform::send_bytes(fd, data, size);
        if (count < 0) {
            const int error = platform::last_socket_error();
            if (platform::socket_error_interrupted(error)) continue;
            throw std::runtime_error(platform::socket_error_message("send", error));
        }
        if (count == 0) throw std::runtime_error("socket closed during send");
        const auto sent = static_cast<std::size_t>(count);
        data += sent;
        size -= sent;
    }
}

void receive_all(const platform::SocketHandle fd, std::byte* data, std::size_t size) {
    while (size != 0) {
        const auto count = platform::receive_bytes(fd, data, size);
        if (count < 0) {
            const int error = platform::last_socket_error();
            if (platform::socket_error_interrupted(error)) continue;
            throw std::runtime_error(platform::socket_error_message("recv", error));
        }
        if (count == 0) throw std::runtime_error("peer closed connection");
        const auto received = static_cast<std::size_t>(count);
        data += received;
        size -= received;
    }
}

} // namespace

TcpSocket::TcpSocket(const platform::SocketHandle fd) : fd_(fd) {
    platform::initialize_sockets();
}

TcpSocket::~TcpSocket() { close(); }

TcpSocket::TcpSocket(TcpSocket&& other) noexcept : fd_(other.fd_) {
    other.fd_ = platform::kInvalidSocket;
}

TcpSocket& TcpSocket::operator=(TcpSocket&& other) noexcept {
    if (this != &other) {
        close();
        fd_ = other.fd_;
        other.fd_ = platform::kInvalidSocket;
    }
    return *this;
}

void TcpSocket::close() {
    if (!platform::socket_valid(fd_)) return;
    platform::shutdown_socket(fd_);
    platform::close_socket(fd_);
}

TcpSocket TcpSocket::connect(const std::string& host, const std::uint16_t port,
                             const std::chrono::milliseconds timeout) {
    platform::initialize_sockets();

    addrinfo hints{};
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_family = AF_UNSPEC;

    addrinfo* results = nullptr;
    const auto port_text = std::to_string(port);
    if (::getaddrinfo(host.c_str(), port_text.c_str(), &hints, &results) != 0) {
        throw std::runtime_error("getaddrinfo failed for " + host);
    }

    for (auto* current = results; current; current = current->ai_next) {
        auto fd = ::socket(current->ai_family, current->ai_socktype, current->ai_protocol);
        if (!platform::socket_valid(fd)) continue;

        platform::set_socket_timeouts(fd, timeout);
        if (::connect(fd, current->ai_addr, static_cast<int>(current->ai_addrlen)) == 0) {
            ::freeaddrinfo(results);
            return TcpSocket(fd);
        }
        platform::close_socket(fd);
    }

    ::freeaddrinfo(results);
    throw std::runtime_error("connect failed to " + host + ':' + std::to_string(port));
}

void TcpSocket::send_frame(const protocol::Frame& frame) const {
    const auto bytes = protocol::encode(frame);
    send_all(fd_, bytes.data(), bytes.size());
}

protocol::Frame TcpSocket::receive_frame() const {
    std::array<std::byte, protocol::kHeaderSize> header{};
    receive_all(fd_, header.data(), header.size());

    const std::uint32_t size =
        (std::to_integer<unsigned>(header[12]) << 24U) |
        (std::to_integer<unsigned>(header[13]) << 16U) |
        (std::to_integer<unsigned>(header[14]) << 8U) |
        std::to_integer<unsigned>(header[15]);

    if (size > protocol::kMaxPayloadSize) throw std::runtime_error("payload too large");

    std::vector<std::byte> bytes(header.begin(), header.end());
    bytes.resize(protocol::kHeaderSize + size);
    if (size != 0) receive_all(fd_, bytes.data() + protocol::kHeaderSize, size);
    return protocol::decode(bytes);
}

TcpListener::TcpListener(const std::string& address, const std::uint16_t port, const int backlog) {
    platform::initialize_sockets();

    addrinfo hints{};
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_family = AF_UNSPEC;
    hints.ai_flags = AI_PASSIVE;

    addrinfo* results = nullptr;
    const auto port_text = std::to_string(port);
    if (::getaddrinfo(address.empty() ? nullptr : address.c_str(), port_text.c_str(), &hints,
                      &results) != 0) {
        throw std::runtime_error("listener getaddrinfo failed");
    }

    for (auto* current = results; current; current = current->ai_next) {
        fd_ = ::socket(current->ai_family, current->ai_socktype, current->ai_protocol);
        if (!platform::socket_valid(fd_)) continue;
        platform::set_reuse_address(fd_);

        if (::bind(fd_, current->ai_addr, static_cast<int>(current->ai_addrlen)) == 0 &&
            ::listen(fd_, backlog) == 0) {
            break;
        }
        platform::close_socket(fd_);
    }

    ::freeaddrinfo(results);
    if (!platform::socket_valid(fd_)) throw std::runtime_error("cannot bind listener");
}

TcpListener::~TcpListener() {
    platform::close_socket(fd_);
}

std::optional<TcpSocket> TcpListener::accept_for(const std::chrono::milliseconds timeout) const {
    if (!platform::wait_readable(fd_, timeout)) return std::nullopt;

    const auto client = ::accept(fd_, nullptr, nullptr);
    if (!platform::socket_valid(client)) {
        const int error = platform::last_socket_error();
        if (platform::socket_error_interrupted(error)) return std::nullopt;
        throw std::runtime_error(platform::socket_error_message("accept", error));
    }
    return TcpSocket(client);
}

} // namespace opengenesis::network
