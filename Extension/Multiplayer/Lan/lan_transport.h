#pragma once
#include "Extension/Multiplayer/Net/transport.h"
#include <chrono>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace dingosdk::multiplayer {
// A transport for people on the same network, with no Steam anywhere: plain UDP on one port, a
// three-packet handshake that hands out peer numbers, and the same contract SteamTransport
// satisfies (Net/transport.h). Reliable messages are acknowledged and resent, and are delivered
// in order; unreliable ones go once. There is no relaying and no auth: anyone who can reach the
// port can ask to join, which is what makes it a LAN transport.
class LanTransport final : public Transport {
  public:
    // `player_name` is what other players see; the host shows it in its player list.
    explicit LanTransport(std::string player_name);
    ~LanTransport() override;

    // Binds the socket to `port` on every interface (0 picks one); host() then serves on it.
    bool open(std::uint16_t port);
    bool host(unsigned capacity) override;
    // Joins a host at `direct_ip`/`direct_port` (host byte order, as IPv4). `steam_id` is unused.
    bool join(std::uint64_t steam_id, std::uint32_t direct_ip = 0, std::uint16_t direct_port = 0) override;

    void stop() override;
    void poll() override;
    std::vector<TransportMessage> receive() override;
    bool send(std::uint64_t id, std::span<const std::uint8_t> bytes, bool reliable, bool fresh,
              TrafficLane lane) override;
    void send_batch(std::span<TransportSend> messages) override;
    void disconnect(std::uint64_t id, const char *reason) override;
    std::int64_t pending(std::uint64_t id) const override;
    const TransportStatus &status() const override;
    std::string name(std::uint64_t id) override;
    std::vector<TransportLink> links() override;
    std::string take_closed(std::uint64_t id) override;
    std::string link_report(std::uint64_t id) override;
    std::string relay_status() const override { return "lan"; }
    bool is_lan() const override { return true; }
    std::vector<std::string> take_direct_notes() override;
    bool set_steam_debug(bool on) override { return false; }
    bool set_send_rate(int bytes_per_second) override;
    int send_rate() const override { return send_rate_; }
    void set_packing(unsigned milliseconds) override { packing_ms_ = milliseconds; }
    bool listen_direct(std::uint16_t port) override { return false; }
    void allow_peers(std::span<const Member> members) override {}
    void connect_peer(std::uint64_t) {}

    // The port actually bound, for a log line or a join code.
    std::uint16_t port() const { return port_; }
    void set_player_name(std::string name) { player_name_ = std::move(name); }

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::uint16_t port_{};
    unsigned capacity_{};
    int send_rate_ = connection_send_rate;
    unsigned packing_ms_{};
    std::string player_name_;
};
} // namespace dingosdk::multiplayer
