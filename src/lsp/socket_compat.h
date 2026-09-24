#ifndef OPENCDC_LSP_SOCKET_COMPAT_H
#define OPENCDC_LSP_SOCKET_COMPAT_H

// Minimal POSIX/Winsock compatibility for the loopback LSP server.
// The server only ever binds 127.0.0.1, so the abstraction stays small:
// socket type, close, error codes, send flags, timeouts, and WSA lifecycle.

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>

#include <cstdint>
#include <cstdio>
#include <string>

namespace opencdc::lsp::compat {

using socket_t = SOCKET;
inline constexpr SOCKET kInvalidSocket = INVALID_SOCKET;
using socklen_t = int;

// No SIGPIPE on Windows: send() simply fails, so the flag is a no-op.
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

using ssize_t = std::int64_t;

inline int close_socket(socket_t fd) {
    return ::closesocket(fd);
}

inline int shutdown_socket(socket_t fd) {
    return ::shutdown(fd, SD_BOTH);
}

// Portable blocking send/recv that maps to the right call per platform.
inline ssize_t socket_send(socket_t fd, const char* data, size_t len, int flags) {
    if (len > static_cast<size_t>(INT_MAX))
        len = static_cast<size_t>(INT_MAX);
    return static_cast<ssize_t>(::send(fd, data, static_cast<int>(len), flags));
}

inline ssize_t socket_recv(socket_t fd, char* data, size_t len) {
    if (len > static_cast<size_t>(INT_MAX))
        len = static_cast<size_t>(INT_MAX);
    return static_cast<ssize_t>(::recv(fd, data, static_cast<int>(len), 0));
}

inline ssize_t socket_read(socket_t fd, char* data, size_t len) {
    return socket_recv(fd, data, len);
}

// Winsock uses DWORD milliseconds for timeouts, not struct timeval.
inline bool set_recv_timeout(socket_t fd, int seconds) {
    DWORD ms = static_cast<DWORD>(seconds) * 1000u;
    return ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&ms),
                        sizeof(ms)) == 0;
}

inline bool set_send_timeout(socket_t fd, int seconds) {
    DWORD ms = static_cast<DWORD>(seconds) * 1000u;
    return ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&ms),
                        sizeof(ms)) == 0;
}

inline int last_would_block() {
    return WSAGetLastError() == WSAEWOULDBLOCK;
}

inline int last_interrupted() {
    return WSAGetLastError() == WSAEINTR;
}

// Process-lifetime Winsock state. WSAStartup is called once; WSACleanup is
// intentionally never called (OS reclaims sockets at exit, and cleanup races
// with test threads still using sockets during teardown).
inline bool ensure_winsock() {
    static const bool ok = [] {
        WSADATA data;
        return ::WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }();
    return ok;
}

}  // namespace opencdc::lsp::compat

#else  // POSIX

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstdint>
#include <string>

namespace opencdc::lsp::compat {

using socket_t = int;
inline constexpr int kInvalidSocket = -1;
using socklen_t = ::socklen_t;
using ssize_t = ::ssize_t;

inline int close_socket(socket_t fd) {
    return ::close(fd);
}

inline int shutdown_socket(socket_t fd) {
    return ::shutdown(fd, SHUT_RDWR);
}

inline ssize_t socket_send(socket_t fd, const char* data, size_t len, int flags) {
    return ::send(fd, data, len, flags);
}

inline ssize_t socket_recv(socket_t fd, char* data, size_t len) {
    return ::recv(fd, data, len, 0);
}

inline ssize_t socket_read(socket_t fd, char* data, size_t len) {
    return ::read(fd, data, len);
}

inline bool set_recv_timeout(socket_t fd, int seconds) {
    struct timeval tv {};
    tv.tv_sec = seconds;
    return ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) == 0;
}

inline bool set_send_timeout(socket_t fd, int seconds) {
    struct timeval tv {};
    tv.tv_sec = seconds;
    return ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) == 0;
}

inline int last_would_block() {
    return errno == EAGAIN || errno == EWOULDBLOCK;
}

inline int last_interrupted() {
    return errno == EINTR;
}

inline bool ensure_winsock() {
    return true;
}

}  // namespace opencdc::lsp::compat

#endif  // _WIN32

#endif  // OPENCDC_LSP_SOCKET_COMPAT_H
