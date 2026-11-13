#include "lan_transport.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <deque>
#include <map>
#include <utility>

#ifndef _WIN32
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace dingosdk::multiplayer {
namespace {
// Every frame starts with this, so a stray datagram (another program's, an old version's) is
// dropped rather than read as a message.
constexpr std::uint8_t magic[4] = {'R', 'L', 'N', '1'};
constexpr std::size_t header_size = 6; // magic, type, flags
constexpr std::size_t max_datagram = 8000;
constexpr std::uint64_t resend_after_us = 200'000;
constexpr unsigned max_tries = 10;
constexpr std::uint64_t hello_every_us = 500'000;
constexpr std::uint64_t peer_timeout_us = 10'000'000;

enum class FrameType : std::uint8_t { hello = 1, welcome = 2, data = 3, acknowledgement = 4, bye = 5, ping = 6, pong = 7 };
constexpr std::uint8_t flag_reliable = 1;

std::uint64_t now_us() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}

void put_u32(std::vector<std::uint8_t> &bytes, std::uint32_t value) {
    for (int shift = 0; shift < 32; shift += 8) bytes.push_back(static_cast<std::uint8_t>(value >> shift));
}
void put_u64(std::vector<std::uint8_t> &bytes, std::uint64_t value) {
    for (int shift = 0; shift < 64; shift += 8) bytes.push_back(static_cast<std::uint8_t>(value >> shift));
}
void put_text(std::vector<std::uint8_t> &bytes, std::string_view text) {
    const auto length = static_cast<std::uint8_t>(std::min<std::size_t>(text.size(), 255));
    bytes.push_back(length);
    bytes.insert(bytes.end(), text.begin(), text.begin() + length);
}
bool read_u32(const std::uint8_t *data, std::size_t size, std::size_t &at, std::uint32_t &out) {
    if (at + 4 > size) return false;
    out = 0;
    for (int index = 0; index < 4; ++index) out |= static_cast<std::uint32_t>(data[at + index]) << (8 * index);
    at += 4;
    return true;
}
bool read_u64(const std::uint8_t *data, std::size_t size, std::size_t &at, std::uint64_t &out) {
    if (at + 8 > size) return false;
    out = 0;
    for (int index = 0; index < 8; ++index) out |= static_cast<std::uint64_t>(data[at + index]) << (8 * index);
    at += 8;
    return true;
}
bool read_text(const std::uint8_t *data, std::size_t size, std::size_t &at, std::string &out) {
    if (at + 1 > size) return false;
    const std::size_t length = data[at++];
    if (at + length > size) return false;
    out.assign(reinterpret_cast<const char *>(data + at), length);
    at += length;
    return true;
}
bool same_address(const sockaddr_in &a, const sockaddr_in &b) {
    return a.sin_addr.s_addr == b.sin_addr.s_addr && a.sin_port == b.sin_port;
}
} // namespace

struct LanTransport::Impl {
    int fd = -1;
    bool server = false;
    bool running = false;
    bool connected = false;
    std::uint64_t my_id = 0;
    std::uint64_t next_peer_id = 2;
    std::uint32_t next_seq = 1;
    sockaddr_in server_address{};
    bool have_server_address = false;
    std::uint64_t last_hello_us = 0;
    bool hello_sent = false;
    std::uint64_t ping_sent_us = 0;
    int ping_ms = 0;
    std::uint64_t bytes_sent = 0, bytes_received = 0, messages_sent = 0, messages_received = 0;
    std::uint64_t last_status_us = 0;

    struct Peer {
        sockaddr_in address{};
        std::string name;
        std::uint64_t last_heard_us = 0;
        std::uint32_t send_seq_next = 1;
        std::uint32_t deliver_next = 1;
        std::map<std::uint32_t, std::vector<std::uint8_t>> waiting;
        std::int64_t pending = 0;
    };
    std::map<std::uint64_t, Peer> peers;
    struct Inflight {
        std::uint64_t peer = 0;
        std::uint32_t seq = 0;
        std::vector<std::uint8_t> frame;
        std::uint64_t sent_us = 0;
        unsigned tries = 0;
        std::size_t size = 0;
    };
    std::map<std::pair<std::uint64_t, std::uint32_t>, Inflight> inflight;
    std::deque<TransportMessage> inbox;
    std::map<std::uint64_t, std::string> closed;
    std::vector<std::string> notes;
    TransportStatus status;

    std::uint64_t peer_at(const sockaddr_in &address) const {
        for (const auto &[id, peer] : peers)
            if (same_address(peer.address, address)) return id;
        return 0;
    }
    bool send_frame(const sockaddr_in &address, const std::vector<std::uint8_t> &frame) {
        if (fd < 0) return false;
        const auto sent = ::sendto(fd, frame.data(), frame.size(), 0, reinterpret_cast<const sockaddr *>(&address),
                                   sizeof(address));
        if (sent < 0) return false;
        bytes_sent += static_cast<std::uint64_t>(sent);
        return true;
    }
    void frame_header(std::vector<std::uint8_t> &frame, FrameType type, std::uint8_t flags) {
        frame.assign(std::begin(magic), std::end(magic));
        frame.push_back(static_cast<std::uint8_t>(type));
        frame.push_back(flags);
    }
    void note(std::string text) {
        notes.push_back(std::move(text));
        if (notes.size() > 64) notes.erase(notes.begin());
    }
    void deliver(std::uint64_t peer, const std::uint8_t *data, std::size_t size) {
        TransportMessage message;
        message.peer = peer;
        message.arrived = now_us();
        message.bytes.assign(data, data + size);
        inbox.push_back(std::move(message));
        ++messages_received;
    }
    void refresh_status() {
        status.ready = running;
        status.hosting = server && running;
        status.connected = connected;
        status.local_id = my_id;
        status.peer_id = server ? 0 : 1;
        status.peers.clear();
        for (const auto &[id, peer] : peers) status.peers.push_back({id, true});
        status.ping_ms = ping_ms;
        status.sent = messages_sent;
        status.received = messages_received;
        status.sent_bytes = bytes_sent;
        status.received_bytes = bytes_received;
        status.pending_bytes = 0;
        for (const auto &[id, peer] : peers) status.pending_bytes += static_cast<int>(peer.pending);
        status.detail = server ? "LAN host" : (connected ? "LAN connected" : "LAN looking for the host");
    }
};

LanTransport::LanTransport(std::string player_name) : impl_(std::make_unique<Impl>()), player_name_(std::move(player_name)) {}

LanTransport::~LanTransport() { stop(); }

bool LanTransport::open(std::uint16_t port) {
#ifndef _WIN32
    if (impl_->fd >= 0) return true;
    const int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return false;
    int yes = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    ::setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &yes, sizeof(yes));
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(port);
    if (::bind(fd, reinterpret_cast<sockaddr *>(&address), sizeof(address)) < 0) {
        ::close(fd);
        return false;
    }
    const int flags = ::fcntl(fd, F_GETFL, 0);
    ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    socklen_t length = sizeof(address);
    ::getsockname(fd, reinterpret_cast<sockaddr *>(&address), &length);
    port_ = ntohs(address.sin_port);
    impl_->fd = fd;
    return true;
#else
    (void)port;
    return false;
#endif
}

bool LanTransport::host(unsigned capacity) {
    if (!open(port_)) return false;
    impl_->server = true;
    impl_->running = true;
    impl_->my_id = 1;
    capacity_ = capacity;
    impl_->refresh_status();
    return true;
}

bool LanTransport::join(std::uint64_t, std::uint32_t direct_ip, std::uint16_t direct_port) {
    if (!open(0)) return false;
    if (!direct_ip || !direct_port) return false;
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = direct_ip; // already in network order
    address.sin_port = htons(direct_port);
    impl_->server_address = address;
    impl_->have_server_address = true;
    impl_->running = true;
    impl_->server = false;
    impl_->connected = false;
    impl_->refresh_status();
    return true;
}

void LanTransport::stop() {
#ifndef _WIN32
    if (impl_->fd >= 0) {
        if (impl_->server)
            for (const auto &[id, peer] : impl_->peers) {
                std::vector<std::uint8_t> frame;
                impl_->frame_header(frame, FrameType::bye, 0);
                put_text(frame, "The server is closing.");
                impl_->send_frame(peer.address, frame);
            }
        ::close(impl_->fd);
        impl_->fd = -1;
    }
#endif
    impl_->running = false;
    impl_->connected = false;
    impl_->peers.clear();
    impl_->inflight.clear();
    impl_->inbox.clear();
    impl_->refresh_status();
}

void LanTransport::poll() {
    if (impl_->fd < 0 || !impl_->running) return;
#ifndef _WIN32
    std::uint8_t buffer[max_datagram];
    for (;;) {
        sockaddr_in from{};
        socklen_t length = sizeof(from);
        const auto received = ::recvfrom(impl_->fd, buffer, sizeof(buffer), 0, reinterpret_cast<sockaddr *>(&from), &length);
        if (received <= 0) break;
        if (static_cast<std::size_t>(received) < header_size) continue;
        if (std::memcmp(buffer, magic, sizeof(magic)) != 0) continue;
        const auto type = static_cast<FrameType>(buffer[4]);
        const std::uint8_t flags = buffer[5];
        impl_->bytes_received += static_cast<std::uint64_t>(received);
        std::size_t at = header_size;
        if (type == FrameType::hello) {
            std::string name;
            if (!read_text(buffer, static_cast<std::size_t>(received), at, name)) continue;
            if (!impl_->server) continue;
            auto id = impl_->peer_at(from);
            if (!id) {
                if (impl_->peers.size() >= capacity_) {
                    std::vector<std::uint8_t> frame;
                    impl_->frame_header(frame, FrameType::bye, 0);
                    put_text(frame, "The server is full.");
                    impl_->send_frame(from, frame);
                    continue;
                }
                id = impl_->next_peer_id++;
                Impl::Peer peer;
                peer.address = from;
                peer.name = name.empty() ? "Player " + std::to_string(id) : name;
                peer.last_heard_us = now_us();
                impl_->peers[id] = std::move(peer);
                impl_->note("joined " + impl_->peers[id].name + " from " +
                            std::string(inet_ntoa(from.sin_addr)) + ":" + std::to_string(ntohs(from.sin_port)));
            }
            impl_->peers[id].last_heard_us = now_us();
            std::vector<std::uint8_t> frame;
            impl_->frame_header(frame, FrameType::welcome, 0);
            put_u64(frame, id);
            put_text(frame, player_name_);
            impl_->send_frame(from, frame);
        } else if (type == FrameType::welcome) {
            std::uint64_t id{};
            if (!read_u64(buffer, static_cast<std::size_t>(received), at, id)) continue;
            std::string host_name;
            read_text(buffer, static_cast<std::size_t>(received), at, host_name);
            impl_->my_id = id;
            impl_->connected = true;
            Impl::Peer peer;
            peer.address = from;
            peer.name = host_name;
            peer.last_heard_us = now_us();
            impl_->peers[1] = std::move(peer);
            impl_->note("joined the host at " + std::string(inet_ntoa(from.sin_addr)) + ":" +
                        std::to_string(ntohs(from.sin_port)));
        } else if (type == FrameType::data) {
            std::uint64_t from_id{};
            std::uint32_t seq{};
            if (!read_u64(buffer, static_cast<std::size_t>(received), at, from_id)) continue;
            if (!read_u32(buffer, static_cast<std::size_t>(received), at, seq)) continue;
            const auto peer = impl_->server ? impl_->peer_at(from) : 1;
            if (!peer) continue;
            auto &entry = impl_->peers[peer];
            entry.last_heard_us = now_us();
            if (flags & flag_reliable) {
                std::vector<std::uint8_t> ack;
                impl_->frame_header(ack, FrameType::acknowledgement, 0);
                // The acknowledgement names this side, because that is what the sender filed its
                // in-flight copy under: the guest under the host's id (1), the host under the
                // guest's assigned id. Naming the sender here would never match anything.
                put_u64(ack, impl_->server ? 1 : impl_->my_id);
                put_u32(ack, seq);
                impl_->send_frame(from, ack);
                if (seq >= entry.deliver_next) {
                    const auto payload = buffer + at;
                    const auto payload_size = static_cast<std::size_t>(received) - at;
                    if (seq == entry.deliver_next) {
                        impl_->deliver(peer, payload, payload_size);
                        ++entry.deliver_next;
                        for (auto waiting = entry.waiting.find(entry.deliver_next); waiting != entry.waiting.end();
                             waiting = entry.waiting.find(entry.deliver_next)) {
                            impl_->deliver(peer, waiting->second.data(), waiting->second.size());
                            entry.waiting.erase(waiting);
                            ++entry.deliver_next;
                        }
                    } else if (entry.waiting.size() < 256) {
                        entry.waiting[seq].assign(buffer + at, buffer + received);
                    }
                }
            } else {
                impl_->deliver(peer, buffer + at, static_cast<std::size_t>(received) - at);
            }
        } else if (type == FrameType::acknowledgement) {
            std::uint64_t peer{};
            std::uint32_t seq{};
            if (!read_u64(buffer, static_cast<std::size_t>(received), at, peer)) continue;
            if (!read_u32(buffer, static_cast<std::size_t>(received), at, seq)) continue;
            const auto key = std::make_pair(peer, seq);
            if (const auto found = impl_->inflight.find(key); found != impl_->inflight.end()) {
                if (const auto owner = impl_->peers.find(peer); owner != impl_->peers.end())
                    owner->second.pending -= static_cast<std::int64_t>(found->second.size);
                impl_->inflight.erase(found);
            }
        } else if (type == FrameType::bye) {
            std::string reason;
            read_text(buffer, static_cast<std::size_t>(received), at, reason);
            const auto peer = impl_->server ? impl_->peer_at(from) : 1;
            if (!peer) continue;
            const auto name = impl_->peers.count(peer) ? impl_->peers[peer].name : std::to_string(peer);
            impl_->closed[peer] = name + " left: " + (reason.empty() ? "closed the connection" : reason);
            impl_->peers.erase(peer);
            if (!impl_->server) impl_->connected = false;
        } else if (type == FrameType::ping) {
            std::vector<std::uint8_t> frame;
            impl_->frame_header(frame, FrameType::pong, 0);
            impl_->send_frame(from, frame);
        } else if (type == FrameType::pong) {
            if (impl_->ping_sent_us && now_us() > impl_->ping_sent_us)
                impl_->ping_ms = static_cast<int>((now_us() - impl_->ping_sent_us) / 1000);
            impl_->ping_sent_us = 0;
        }
    }
    // Anything unacknowledged for a while goes again, and a connection that never answers ends.
    const auto now = now_us();
    for (auto it = impl_->inflight.begin(); it != impl_->inflight.end();) {
        auto &entry = it->second;
        if (now - entry.sent_us < resend_after_us) {
            ++it;
            continue;
        }
        if (entry.tries >= max_tries) {
            const auto peer = entry.peer;
            impl_->closed[peer] = "no answer after " + std::to_string(max_tries) + " tries";
            impl_->peers.erase(peer);
            if (!impl_->server) impl_->connected = false;
            it = impl_->inflight.erase(it);
            continue;
        }
        if (const auto owner = impl_->peers.find(entry.peer); owner != impl_->peers.end())
            impl_->send_frame(owner->second.address, entry.frame);
        entry.sent_us = now;
        ++entry.tries;
        ++it;
    }
    // A guest says hello until it is welcomed; both sides keep a ping going for the status line.
    if (!impl_->server && impl_->have_server_address) {
        if (!impl_->connected && (!impl_->hello_sent || now - impl_->last_hello_us > hello_every_us)) {
            std::vector<std::uint8_t> frame;
            impl_->frame_header(frame, FrameType::hello, 0);
            put_text(frame, player_name_);
            impl_->send_frame(impl_->server_address, frame);
            impl_->last_hello_us = now;
            impl_->hello_sent = true;
        }
        if (impl_->connected && now - impl_->ping_sent_us > 1'000'000) {
            impl_->ping_sent_us = now;
            std::vector<std::uint8_t> frame;
            impl_->frame_header(frame, FrameType::ping, 0);
            impl_->send_frame(impl_->server_address, frame);
        }
    }
    // A host forgets a player it has not heard from at all.
    for (auto it = impl_->peers.begin(); it != impl_->peers.end();) {
        if (impl_->server && now - it->second.last_heard_us > peer_timeout_us) {
            impl_->closed[it->first] = it->second.name + " timed out";
            it = impl_->peers.erase(it);
        } else {
            ++it;
        }
    }
#endif
    const auto status_now = now_us();
    if (status_now - impl_->last_status_us > 250'000) {
        impl_->last_status_us = status_now;
        impl_->refresh_status();
    }
}

std::vector<TransportMessage> LanTransport::receive() {
    std::vector<TransportMessage> messages;
    messages.reserve(impl_->inbox.size());
    while (!impl_->inbox.empty()) {
        messages.push_back(std::move(impl_->inbox.front()));
        impl_->inbox.pop_front();
    }
    return messages;
}

bool LanTransport::send(std::uint64_t id, std::span<const std::uint8_t> bytes, bool reliable, bool fresh,
                        TrafficLane lane) {
    (void)fresh;
    (void)lane;
    if (!impl_->running) return false;
    sockaddr_in address{};
    if (impl_->server) {
        const auto peer = impl_->peers.find(id);
        if (peer == impl_->peers.end()) return false;
        address = peer->second.address;
    } else {
        if (!impl_->have_server_address || !impl_->connected) return false;
        address = impl_->server_address;
        id = 1;
    }
    std::vector<std::uint8_t> frame;
    impl_->frame_header(frame, FrameType::data, reliable ? flag_reliable : 0);
    put_u64(frame, impl_->my_id);
    std::uint32_t seq = 0;
    auto &peer = impl_->peers[id];
    if (reliable) seq = peer.send_seq_next++;
    put_u32(frame, seq);
    frame.insert(frame.end(), bytes.begin(), bytes.end());
    if (!impl_->send_frame(address, frame)) return false;
    ++impl_->messages_sent;
    if (reliable) {
        Impl::Inflight entry;
        entry.peer = id;
        entry.seq = seq;
        entry.frame = std::move(frame); // the sent copy is the retransmit copy
        entry.sent_us = now_us();
        entry.tries = 1;
        entry.size = frame.size();
        peer.pending += static_cast<std::int64_t>(frame.size());
        impl_->inflight[std::make_pair(id, seq)] = std::move(entry);
    }
    return true;
}

void LanTransport::send_batch(std::span<TransportSend> messages) {
    for (auto &message : messages) message.sent = send(message.id, message.bytes, message.reliable, message.fresh, message.lane);
}

void LanTransport::disconnect(std::uint64_t id, const char *reason) {
    const auto peer = impl_->peers.find(id);
    if (peer == impl_->peers.end()) return;
    std::vector<std::uint8_t> frame;
    impl_->frame_header(frame, FrameType::bye, 0);
    put_text(frame, reason ? reason : "");
    impl_->send_frame(peer->second.address, frame);
    impl_->closed[id] = peer->second.name + " disconnected: " + (reason ? reason : "");
    impl_->peers.erase(peer);
    impl_->refresh_status();
}

std::int64_t LanTransport::pending(std::uint64_t id) const {
    const auto peer = impl_->peers.find(id);
    return peer == impl_->peers.end() ? 0 : peer->second.pending;
}

std::string LanTransport::name(std::uint64_t id) {
    const auto peer = impl_->peers.find(id);
    return peer == impl_->peers.end() ? "Player " + std::to_string(id) : peer->second.name;
}

std::vector<TransportLink> LanTransport::links() {
    std::vector<TransportLink> links;
    for (const auto &[id, peer] : impl_->peers) {
        TransportLink link;
        link.id = id;
        link.connected = true;
        link.measured = true;
        link.direct = true;
        link.ping_ms = impl_->ping_ms;
        link.pending_bytes = static_cast<int>(peer.pending);
        link.send_rate = send_rate_;
        link.quality_local = 1.f;
        link.quality_remote = 1.f;
        links.push_back(std::move(link));
    }
    return links;
}

std::string LanTransport::take_closed(std::uint64_t id) {
    const auto found = impl_->closed.find(id);
    if (found == impl_->closed.end()) return {};
    auto text = found->second;
    impl_->closed.erase(found);
    return text;
}

std::string LanTransport::link_report(std::uint64_t id) {
    const auto peer = impl_->peers.find(id);
    if (peer == impl_->peers.end()) return {};
    return peer->second.name + ": " + std::to_string(impl_->ping_ms) + " ms, " +
           std::to_string(peer->second.pending) + " bytes waiting";
}

std::vector<std::string> LanTransport::take_direct_notes() {
    auto notes = std::move(impl_->notes);
    impl_->notes.clear();
    return notes;
}

bool LanTransport::set_send_rate(int bytes_per_second) {
    if (bytes_per_second < min_send_rate || bytes_per_second > max_send_rate) return false;
    send_rate_ = bytes_per_second;
    return true;
}
} // namespace dingosdk::multiplayer

namespace dingosdk::multiplayer {
const TransportStatus &LanTransport::status() const { return impl_->status; }
} // namespace dingosdk::multiplayer
