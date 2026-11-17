#pragma once
// The LAN transport and discovery speak plain UDP: BSD sockets on Unix, WinSock on Windows. This
// header is the whole of the difference between them, so the two files that use sockets read the
// same on both platforms.
#include <cstddef>
#include <string>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace dingosdk::multiplayer {
#ifdef _WIN32
using LanSocket = SOCKET;
using LanSockLen = int;
inline constexpr LanSocket lan_no_socket = INVALID_SOCKET;
// WinSock has to be started once per process before any socket call. The runtime and the server
// already start it for their own networking; this makes the LAN path work in a tool that did not.
inline bool lan_sockets_ready() {
    static const bool started = [] {
        WSADATA data{};
        return ::WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }();
    return started;
}
inline bool lan_socket_valid(LanSocket fd) { return fd != INVALID_SOCKET; }
inline void lan_socket_close(LanSocket fd) { ::closesocket(fd); }
inline bool lan_socket_nonblocking(LanSocket fd) {
    u_long on = 1;
    return ::ioctlsocket(fd, FIONBIO, &on) == 0;
}
#else
using LanSocket = int;
using LanSockLen = socklen_t;
inline constexpr LanSocket lan_no_socket = -1;
inline bool lan_sockets_ready() { return true; }
inline bool lan_socket_valid(LanSocket fd) { return fd >= 0; }
inline void lan_socket_close(LanSocket fd) { ::close(fd); }
inline bool lan_socket_nonblocking(LanSocket fd) {
    const int flags = ::fcntl(fd, F_GETFL, 0);
    return ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}
#endif

// A socket option's value, in the pointer type each platform's setsockopt takes.
inline const char *lan_option(const void *value) { return static_cast<const char *>(value); }

// The dotted form of an IPv4 address. (Not inet_ntoa, which takes a static buffer and is deprecated
// on Windows.)
inline std::string lan_address_text(const sockaddr_in &address) {
    char dotted[INET_ADDRSTRLEN]{};
    if (!::inet_ntop(AF_INET, &address.sin_addr, dotted, sizeof(dotted))) return {};
    return dotted;
}
} // namespace dingosdk::multiplayer
