#pragma once
#include "Extension/Multiplayer/Net/protocol.h"
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

// What a transport is to everything above it: opaque bytes, lanes, peers by number. ReSkate's own
// transport is Steam's (Steam/steam_transport.h); LanTransport (Lan/lan_transport.h) is the same
// contract over plain UDP, for a fork that runs without Steam.
namespace dingosdk::multiplayer {
enum class TrafficLane : std::uint16_t { gameplay = 0, control = 1, cosmetics = 2, voice = 3 };
constexpr TrafficLane traffic_lane(PacketKind kind) {
    return kind == PacketKind::voice ? TrafficLane::voice
        : kind == PacketKind::pose || kind == PacketKind::audio || kind == PacketKind::effects ? TrafficLane::gameplay
        : kind == PacketKind::cosmetics ? TrafficLane::cosmetics : TrafficLane::control;
}
struct TransportPeer {
    std::uint64_t id{};
    bool connected{};
};
// What the transport measures of one connection right now (Transport::links).
struct TransportLink {
    std::uint64_t id{};
    bool connected{}, measured{};
    int ping_ms{}, pending_bytes{}, send_rate{}; // send_rate: bytes a second the transport will let out to them
    float quality_local = -1, quality_remote = -1; // 0-1 of packets delivered each way; -1 unknown
    float out_bps{}, in_bps{};
    std::uint64_t queue_us{}; // how long a message sent now would wait
    // Which relay the connection goes through, by short names: Steam's ("ord", "fra") for the
    // Steam transport; empty for a direct one. A ping far above the direct one is the route.
    std::string relay, remote_relay;
    bool direct{}; // not through a relay at all
};
struct TransportMessage {
    std::uint64_t peer{};
    std::vector<std::uint8_t> bytes;
    // When it was received, in microseconds on a clock of its own (only differences mean
    // anything); 0 when the transport does not say.
    std::uint64_t arrived{};
};
// One message of Transport::send_batch, with send()'s arguments; `sent` receives what send()
// would have returned for it.
struct TransportSend {
    std::uint64_t id{};
    std::span<const std::uint8_t> bytes;
    bool reliable{}, fresh{};
    TrafficLane lane = TrafficLane::control;
    bool sent{};
};
struct TransportStatus {
    bool ready{}, hosting{}, connected{};
    std::uint64_t local_id{}, peer_id{}, sent{}, received{}, dropped{};
    std::string detail;
    std::string peer_name;
    std::vector<TransportPeer> peers;
    bool telemetry{};
    unsigned prioritized_connections{};
    std::uint64_t cosmetic_queue_us{};
    int ping_ms{}, send_rate{}, pending_bytes{};
    float outgoing_bps{}, incoming_bps{}, delivery_local = -1, delivery_remote = -1;
    std::uint64_t queue_us{}, skipped{}, send_failures{}, invalid_messages{}, sent_bytes{}, received_bytes{},
        raw_sent_bytes{};
};
// What each connection may send a second (bytes): a crowded server's players, mostly.
inline constexpr int connection_send_rate = 900 * 1024, min_send_rate = 128 * 1024, max_send_rate = 16 * 1024 * 1024;

class Transport {
  public:
    virtual ~Transport() = default;
    // Hosting: capacity is how many players may connect at once.
    virtual bool host(unsigned capacity) = 0;
    // Joining a host: `id` is the Steam ID for the Steam transport; a LAN transport ignores it
    // and dials `direct_ip`/`direct_port` (host byte order).
    virtual bool join(std::uint64_t id, std::uint32_t direct_ip = 0, std::uint16_t direct_port = 0) = 0;
    virtual void stop() = 0;
    virtual void poll() = 0;
    virtual std::vector<TransportMessage> receive() = 0;
    virtual bool send(std::uint64_t id, std::span<const std::uint8_t> bytes, bool reliable, bool fresh,
                      TrafficLane lane) = 0;
    virtual void send_batch(std::span<TransportSend> messages) = 0;
    virtual void disconnect(std::uint64_t id, const char *reason) = 0;
    virtual std::int64_t pending(std::uint64_t id) const = 0;
    virtual const TransportStatus &status() const = 0;
    virtual std::string name(std::uint64_t id) = 0;
    virtual std::vector<TransportLink> links() = 0;
    virtual std::string take_closed(std::uint64_t id) = 0;
    virtual std::string link_report(std::uint64_t id) = 0;
    virtual std::string relay_status() const = 0;
    // True for the Steam-free LAN transport: the server's log and its join wording differ.
    virtual bool is_lan() const = 0;
    virtual std::vector<std::string> take_direct_notes() = 0;
    virtual bool set_steam_debug(bool on) = 0;
    virtual bool set_send_rate(int bytes_per_second) = 0;
    virtual int send_rate() const = 0;
    virtual void set_packing(unsigned milliseconds) = 0;
    virtual bool listen_direct(std::uint16_t port) = 0;
    virtual void allow_peers(std::span<const Member> members) = 0;
};
} // namespace dingosdk::multiplayer
