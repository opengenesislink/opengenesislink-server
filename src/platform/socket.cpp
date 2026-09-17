#include "opengenesis/platform/socket.hpp"

#include <algorithm>
#include <limits>
#include <mutex>
#include <stdexcept>

#ifndef _WIN32
#include <cerrno>
#include <cstring>
#include <sys/select.h>
#include <unistd.h>
#endif

namespace opengenesis::platform {

void initialize_sockets() {
#ifdef _WIN32
    static std::once_flag once;
    static int startup_error = 0;
    std::call_once(once, [] {
        WSADATA data{};
        startup_error = ::WSAStartup(MAKEWORD(2, 2), &data);
    });
    if (startup_error != 0) {
        throw std::runtime_error("WSAStartup failed with error " + std::to_string(startup_error));
    }
#endif
}

bool socket_valid(const SocketHandle handle) noexcept {
    return handle != kInvalidSocket;
}

int last_socket_error() noexcept {
#ifdef _WIN32
    return ::WSAGetLastError();
#else
    return errno;
#endif
}

bool socket_error_interrupted(const int error) noexcept {
#ifdef _WIN32
    return error == WSAEINTR;
#else
    return error == EINTR;
#endif
}

std::string socket_error_message(const char* operation, const int error) {
#ifdef _WIN32
    return std::string(operation) + " failed with Winsock error " + std::to_string(error);
#else
    return std::string(operation) + ": " + std::strerror(error);
#endif
}

void shutdown_socket(const SocketHandle handle) noexcept {
    if (!socket_valid(handle)) return;
#ifdef _WIN32
    (void)::shutdown(handle, SD_BOTH);
#else
    (void)::shutdown(handle, SHUT_RDWR);
#endif
}

void close_socket(SocketHandle& handle) noexcept {
    if (!socket_valid(handle)) return;
#ifdef _WIN32
    (void)::closesocket(handle);
#else
    (void)::close(handle);
#endif
    handle = kInvalidSocket;
}

std::ptrdiff_t send_bytes(const SocketHandle handle, const void* data, const std::size_t size) {
    const auto chunk = static_cast<int>(std::min<std::size_t>(
        size, static_cast<std::size_t>(std::numeric_limits<int>::max())));
#ifdef _WIN32
    const int result = ::send(handle, static_cast<const char*>(data), chunk, 0);
    return result == SOCKET_ERROR ? -1 : static_cast<std::ptrdiff_t>(result);
#else
    const auto result = ::send(handle, data, static_cast<std::size_t>(chunk), MSG_NOSIGNAL);
    return static_cast<std::ptrdiff_t>(result);
#endif
}

std::ptrdiff_t receive_bytes(const SocketHandle handle, void* data, const std::size_t size) {
    const auto chunk = static_cast<int>(std::min<std::size_t>(
        size, static_cast<std::size_t>(std::numeric_limits<int>::max())));
#ifdef _WIN32
    const int result = ::recv(handle, static_cast<char*>(data), chunk, 0);
    return result == SOCKET_ERROR ? -1 : static_cast<std::ptrdiff_t>(result);
#else
    const auto result = ::recv(handle, data, static_cast<std::size_t>(chunk), 0);
    return static_cast<std::ptrdiff_t>(result);
#endif
}

bool wait_readable(const SocketHandle handle, const std::chrono::milliseconds timeout) {
    fd_set read_set;
    FD_ZERO(&read_set);
    FD_SET(handle, &read_set);
    timeval tv{};
    tv.tv_sec = static_cast<long>(timeout.count() / 1000);
    tv.tv_usec = static_cast<long>((timeout.count() % 1000) * 1000);
#ifdef _WIN32
    const int result = ::select(0, &read_set, nullptr, nullptr, &tv);
#else
    const int result = ::select(handle + 1, &read_set, nullptr, nullptr, &tv);
#endif
    if (result == 0) return false;
    if (result < 0) {
        const int error = last_socket_error();
        if (socket_error_interrupted(error)) return false;
        throw std::runtime_error(socket_error_message("select", error));
    }
    return FD_ISSET(handle, &read_set) != 0;
}

void set_socket_timeouts(const SocketHandle handle, const std::chrono::milliseconds timeout) {
#ifdef _WIN32
    const DWORD millis = static_cast<DWORD>(std::clamp<std::int64_t>(
        timeout.count(), 1, static_cast<std::int64_t>(std::numeric_limits<DWORD>::max())));
    (void)::setsockopt(handle, SOL_SOCKET, SO_RCVTIMEO,
                       reinterpret_cast<const char*>(&millis), sizeof(millis));
    (void)::setsockopt(handle, SOL_SOCKET, SO_SNDTIMEO,
                       reinterpret_cast<const char*>(&millis), sizeof(millis));
#else
    timeval tv{static_cast<long>(timeout.count() / 1000),
               static_cast<long>((timeout.count() % 1000) * 1000)};
    (void)::setsockopt(handle, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    (void)::setsockopt(handle, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
#endif
}

void set_reuse_address(const SocketHandle handle) {
    const int reuse = 1;
#ifdef _WIN32
    (void)::setsockopt(handle, SOL_SOCKET, SO_REUSEADDR,
                       reinterpret_cast<const char*>(&reuse), sizeof(reuse));
#else
    (void)::setsockopt(handle, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
#endif
}

} // namespace opengenesis::platform
