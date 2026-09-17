#pragma once

#include "opengenesis/platform/socket.hpp"
#include "opengenesis/protocol/frame.hpp"

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>

namespace opengenesis::network {

class TcpSocket final {
public:
    TcpSocket() = default;
    explicit TcpSocket(platform::SocketHandle fd);
    ~TcpSocket();

    TcpSocket(const TcpSocket&) = delete;
    TcpSocket& operator=(const TcpSocket&) = delete;
    TcpSocket(TcpSocket&& other) noexcept;
    TcpSocket& operator=(TcpSocket&& other) noexcept;

    static TcpSocket connect(const std::string& host, std::uint16_t port,
                             std::chrono::milliseconds timeout = std::chrono::seconds{5});

    void send_frame(const protocol::Frame& frame) const;
    [[nodiscard]] protocol::Frame receive_frame() const;
    void close();
    [[nodiscard]] bool valid() const { return platform::socket_valid(fd_); }

private:
    platform::SocketHandle fd_{platform::kInvalidSocket};
};

class TcpListener final {
public:
    TcpListener(const std::string& address, std::uint16_t port, int backlog = 128);
    ~TcpListener();

    TcpListener(const TcpListener&) = delete;
    TcpListener& operator=(const TcpListener&) = delete;

    [[nodiscard]] std::optional<TcpSocket> accept_for(std::chrono::milliseconds timeout) const;

private:
    platform::SocketHandle fd_{platform::kInvalidSocket};
};

} // namespace opengenesis::network
