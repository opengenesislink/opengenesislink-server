#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#endif

namespace opengenesis::platform {

#ifdef _WIN32
using SocketHandle = SOCKET;
inline constexpr SocketHandle kInvalidSocket = INVALID_SOCKET;
#else
using SocketHandle = int;
inline constexpr SocketHandle kInvalidSocket = -1;
#endif

void initialize_sockets();
[[nodiscard]] bool socket_valid(SocketHandle handle) noexcept;
[[nodiscard]] int last_socket_error() noexcept;
[[nodiscard]] bool socket_error_interrupted(int error) noexcept;
[[nodiscard]] std::string socket_error_message(const char* operation, int error);

void close_socket(SocketHandle& handle) noexcept;
void shutdown_socket(SocketHandle handle) noexcept;

[[nodiscard]] std::ptrdiff_t send_bytes(SocketHandle handle, const void* data, std::size_t size);
[[nodiscard]] std::ptrdiff_t receive_bytes(SocketHandle handle, void* data, std::size_t size);
[[nodiscard]] bool wait_readable(SocketHandle handle, std::chrono::milliseconds timeout);
void set_socket_timeouts(SocketHandle handle, std::chrono::milliseconds timeout);
void set_reuse_address(SocketHandle handle);

} // namespace opengenesis::platform
