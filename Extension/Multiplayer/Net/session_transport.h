#pragma once
#include "Extension/Multiplayer/Net/transport.h"
#include <memory>
#include <string>
#include <string_view>

namespace dingosdk::multiplayer {
class SteamTransport;
class LanClient;
// The session's transport. Steam's is what a session uses, exactly as before; a LAN join replaces it
// with the LAN client's transport, which satisfies the same contract. The session above only ever
// talks to this, so it never learns which one carried the bytes — and the Steam path is untouched
// unless a LAN join is asked for.
class SessionTransport final : public Transport {
  public:
    SessionTransport();
    ~SessionTransport() override;
    SessionTransport(const SessionTransport &) = delete;
    SessionTransport &operator=(const SessionTransport &) = delete;

    // Takes the session onto the LAN: dials `endpoint` ("address" or "address:port"). False, with
    // `error`, when the address cannot be read or no socket can be opened; the Steam transport is
    // left as it was. `stop()` puts the session back on Steam.
    bool use_lan(std::string player_name, std::string_view endpoint, std::string &error);
    // True while a LAN join is what carries this session.
    bool lan_active() const { return lan_ != nullptr; }

    // What the session uses beyond the contract. Steam's only: a LAN session browses nothing and has
    // no Steam peers to connect to, so these are quietly inert there.
    bool open();
    bool socket_test();
    void connect_peer(std::uint64_t id);

    bool host(unsigned capacity) override;
    bool join(std::uint64_t id, std::uint32_t direct_ip = 0, std::uint16_t direct_port = 0) override;
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
    std::string relay_status() const override;
    bool is_lan() const override;
    std::vector<std::string> take_direct_notes() override;
    bool set_steam_debug(bool on) override;
    bool set_send_rate(int bytes_per_second) override;
    int send_rate() const override;
    void set_packing(unsigned milliseconds) override;
    bool listen_direct(std::uint16_t port) override;
    void allow_peers(std::span<const Member> members) override;

  private:
    // Whichever one carries this session. Defined in the .cpp, where both types are complete.
    Transport &active();
    const Transport &active() const;
    std::unique_ptr<SteamTransport> steam_;
    std::unique_ptr<LanClient> lan_;
};
} // namespace dingosdk::multiplayer
