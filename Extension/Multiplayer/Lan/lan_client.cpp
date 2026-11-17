// First, so WinSock is declared before any other header can bring in windows.h.
#include "lan_socket.h"
#include "lan_client.h"
#include "lan_session.h"
#include <utility>

namespace dingosdk::multiplayer {
LanClient::LanClient(std::string player_name) : transport_(std::move(player_name)) {}

bool LanClient::parse_address(std::string_view text, std::uint32_t &network_order) {
    unsigned parts[4]{};
    std::size_t at = 0;
    for (int index = 0; index < 4; ++index) {
        if (at >= text.size()) return false;
        unsigned value = 0;
        std::size_t digits = 0;
        while (at < text.size() && text[at] >= '0' && text[at] <= '9') {
            value = value * 10 + static_cast<unsigned>(text[at] - '0');
            if (value > 255) return false;
            ++at;
            ++digits;
        }
        if (!digits) return false;
        parts[index] = value;
        if (index < 3) {
            if (at >= text.size() || text[at] != '.') return false;
            ++at;
        }
    }
    if (at != text.size()) return false;
    // The high octet first, as the address is written; htonl puts it in the order a socket wants.
    network_order = htonl((parts[0] << 24) | (parts[1] << 16) | (parts[2] << 8) | parts[3]);
    return true;
}

bool LanClient::parse_endpoint(std::string_view endpoint, std::uint32_t &network_order, std::uint16_t &port,
                               std::string &error) {
    auto host_part = endpoint;
    port = lan_game_port;
    // The last colon is the port: an address has none, "address:port" has one.
    if (const auto colon = endpoint.rfind(':'); colon != std::string_view::npos) {
        host_part = endpoint.substr(0, colon);
        const auto digits = endpoint.substr(colon + 1);
        unsigned value = 0;
        if (digits.empty()) {
            error = "That address has no port after the colon. Use mp lan <address>:<port>.";
            return false;
        }
        for (const char digit : digits) {
            if (digit < '0' || digit > '9') {
                error = "The port must be a number: use mp lan <address>:<port>.";
                return false;
            }
            value = value * 10 + static_cast<unsigned>(digit - '0');
            if (value > 65535) {
                error = "The port must be between 1 and 65535.";
                return false;
            }
        }
        if (!value) {
            error = "The port must be between 1 and 65535.";
            return false;
        }
        port = static_cast<std::uint16_t>(value);
    }
    if (!parse_address(host_part, network_order) || network_order == htonl(INADDR_ANY)) {
        error = "Use a LAN server's address, such as mp lan 192.168.1.20:27015 (the host prints it).";
        return false;
    }
    return true;
}

bool LanClient::join(std::string_view endpoint) {
    endpoint_.assign(endpoint);
    error_.clear();
    std::uint32_t ip{};
    std::uint16_t port{};
    if (!parse_endpoint(endpoint, ip, port, error_)) return false;
    return join_at(ip, port);
}

bool LanClient::join_at(std::uint32_t network_order, std::uint16_t port) {
    error_.clear();
    if (!transport_.join(0, network_order, port)) {
        error_ = "Could not open a UDP socket to reach " + (endpoint_.empty() ? std::string("that address") : endpoint_) + ".";
        return false;
    }
    return true;
}
} // namespace dingosdk::multiplayer
