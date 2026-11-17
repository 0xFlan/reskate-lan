#pragma once
#include "lan_transport.h"
#include <string>
#include <string_view>

namespace dingosdk::multiplayer {
// The LAN client: the transport a session uses when it joins a LAN server (`mp lan <address>`). It
// is LanTransport — the same handshake, reliability, fragmentation and liveness the LAN server
// runs — plus the address it dials, so the protocol has one implementation and not two.
class LanClient {
  public:
    explicit LanClient(std::string player_name);

    // "address" or "address:port" (the port defaults to lan_game_port), and the host's password if
    // it has one. False, with error(), when the address cannot be read, the password is unusable, or
    // no socket can be opened.
    bool join(std::string_view endpoint, std::string_view password = {});
    // The same dial with the address already read: what a command uses once it has checked the
    // address, so a bad one is refused before a session is ended for it.
    bool join_at(std::uint32_t network_order, std::uint16_t port, std::string_view password = {});
    void stop() { transport_.stop(); }

    LanTransport &transport() { return transport_; }
    const LanTransport &transport() const { return transport_; }
    const std::string &endpoint() const { return endpoint_; }
    const std::string &error() const { return error_; }
    // The host's reason for refusing (a wrong password, or that one is needed), once it said so.
    std::string take_join_refusal() { return transport_.take_join_refusal(); }

    // Reads "1.2.3.4" into an IPv4 address in network order (as LanTransport::join wants it).
    static bool parse_address(std::string_view text, std::uint32_t &network_order);
    // Reads "address" or "address:port" into what join_at takes. False, with error(), for anything
    // else — a name, a port out of range, a bare dot.
    static bool parse_endpoint(std::string_view endpoint, std::uint32_t &network_order, std::uint16_t &port,
                               std::string &error);

  private:
    LanTransport transport_;
    std::string endpoint_;
    std::string error_;
};
} // namespace dingosdk::multiplayer
