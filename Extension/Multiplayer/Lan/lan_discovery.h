#pragma once
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace dingosdk::multiplayer {
// A server found on the local network.
struct LanServer {
    std::string name;
    std::string map;
    std::string address; // the address the beacon came from, as a dotted quad
    std::uint16_t port{};
    unsigned players{}, max_players{};
    std::chrono::milliseconds age{}; // how long ago it was heard
};
// LAN discovery: a server shouts a small beacon into the broadcast address twice a second, and
// anyone listening collects them. No Steam, no directory server, nothing to configure — a LAN
// party finds every server that is running on the same network, which is what makes an
// "everyone pile onto one server" session possible without anyone typing an IP.
class LanDiscovery {
  public:
    static constexpr std::uint16_t discovery_port = 27026;

    LanDiscovery();
    ~LanDiscovery();
    LanDiscovery(const LanDiscovery &) = delete;
    LanDiscovery &operator=(const LanDiscovery &) = delete;

    // Starts announcing this server. `game_port` is the UDP port players join on.
    bool advertise(std::string name, std::string map, std::uint16_t game_port, unsigned max_players);
    // The count the next beacon carries; cheap, called from the server's own loop.
    void update(unsigned players);
    void stop();

    // Listen for `wait` and return what was heard, one entry per server, freshest first.
    static std::vector<LanServer> discover(std::chrono::milliseconds wait);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace dingosdk::multiplayer
