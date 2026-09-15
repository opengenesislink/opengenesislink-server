#pragma once

#include "opengenesis/protocol/frame.hpp"

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>

namespace opengenesis::network {

class TcpSocket final {
public:
    TcpSocket() noexcept = default;
    explicit TcpSocket(int fd) noexcept;
    ~TcpSocket();
    TcpSocket(const TcpSocket&) = delete;
    TcpSocket& operator=(const TcpSocket&) = delete;
    TcpSocket(TcpSocket&& other) noexcept;
    TcpSocket& operator=(TcpSocket&& other) noexcept;

    static TcpSocket connect(const std::string& host, std::uint16_t port);
    void send_frame(const protocol::Frame& frame) const;
    [[nodiscard]] protocol::Frame receive_frame() const;
    [[nodiscard]] bool valid() const noexcept;

private:
    int fd_{-1};
};

class TcpListener final {
public:
    TcpListener(const std::string& address, std::uint16_t port);
    ~TcpListener();
    TcpListener(const TcpListener&) = delete;
    TcpListener& operator=(const TcpListener&) = delete;

    [[nodiscard]] std::optional<TcpSocket> accept_for(std::chrono::milliseconds timeout) const;

private:
    int fd_{-1};
};

} // namespace opengenesis::network
