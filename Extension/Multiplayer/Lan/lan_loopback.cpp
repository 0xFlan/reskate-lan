// A check of the LAN transport with no game and no Steam: one process runs a host and a guest
// against each other over 127.0.0.1, and asserts the handshake, a reliable message each way, an
// unreliable one, and a clean disconnect. Built as `lan_loopback` by cmake/Apps.cmake.
#include "Extension/Multiplayer/Lan/lan_socket.h"
#include "Extension/Multiplayer/Lan/lan_transport.h"
#include "Extension/Multiplayer/Lan/lan_client.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

using namespace dingosdk::multiplayer;
namespace {
int failures = 0;

void check(bool ok, const char *what) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++failures;
}

bool is(const std::vector<std::uint8_t> &bytes, const char *text) {
    const auto length = std::strlen(text);
    return bytes.size() == length && std::memcmp(bytes.data(), text, length) == 0;
}

std::span<const std::uint8_t> as_bytes(const char *text) {
    return {reinterpret_cast<const std::uint8_t *>(text), std::strlen(text)};
}
} // namespace

int main() {
    LanTransport host("Host");
    check(host.open(0), "host binds a free UDP port");
    check(host.port() != 0, "host reports the port it bound");
    check(host.host(8), "host starts listening");
    std::printf("      host listening on 127.0.0.1:%u\n", host.port());

    LanTransport guest("Guest");
    check(guest.join(0, htonl(INADDR_LOOPBACK), host.port()), "guest dials the host");

    bool guest_connected = false, host_saw_peer = false, sent_hello = false, sent_unreliable = false;
    bool host_got_hello = false, host_got_unreliable = false, guest_got_reply = false, guest_saw_close = false;
    std::uint64_t guest_id = 0;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline) {
        host.poll();
        guest.poll();
        guest_connected = guest_connected || guest.status().connected;
        if (!host_saw_peer && !host.status().peers.empty()) {
            host_saw_peer = true;
            guest_id = host.status().peers.front().id;
        }
        if (guest_connected && !sent_hello) {
            sent_hello = guest.send(1, as_bytes("hello from the guest"), true, false, TrafficLane::control);
        }
        for (const auto &message : host.receive()) {
            if (is(message.bytes, "hello from the guest")) {
                host_got_hello = true;
                // Answer reliably, on the peer the handshake gave us.
                host.send(message.peer, as_bytes("hello from the host"), true, false, TrafficLane::control);
                guest_id = message.peer;
            } else if (is(message.bytes, "unreliable from the guest")) {
                host_got_unreliable = true;
            }
        }
        if (host_got_hello && !sent_unreliable)
            sent_unreliable = guest.send(1, as_bytes("unreliable from the guest"), false, false, TrafficLane::gameplay);
        for (const auto &message : guest.receive()) {
            if (is(message.bytes, "hello from the host")) guest_got_reply = true;
            if (is(message.bytes, "closing")) guest_saw_close = true;
        }
        if (host_got_hello && guest_got_reply && host_got_unreliable) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    check(guest_connected, "guest's handshake completed");
    check(host_saw_peer, "host lists the guest as a peer");
    check(guest_got_reply, "host's reliable reply reached the guest");
    check(host_got_unreliable, "guest's unreliable message reached the host");
    check(host.name(guest_id) == "Guest", "host shows the guest's name");
    check(guest.status().local_id != 0, "guest has a peer id from the handshake");

    // A reliable message must be acknowledged, not resent until the peer is thrown off: the guest
    // holds its own in-flight copy only while the host has not acknowledged it.
    for (int i = 0; i < 60; i++) {
        host.poll();
        guest.poll();
        host.receive();
        guest.receive();
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    check(guest.pending(1) == 0, "guest's reliable message was acknowledged (nothing in flight)");
    check(guest.status().connected, "guest is still connected well after the resend window");

    // A message too big for one datagram is split up and put back together, on both lanes.
    std::vector<std::uint8_t> big(40000);
    for (std::size_t index = 0; index < big.size(); ++index) big[index] = static_cast<std::uint8_t>((index * 37 + 11) & 0xff);
    const bool big_sent = guest.send(1, big, true, false, TrafficLane::control);
    const bool big_unreliable_sent = guest.send(1, big, false, false, TrafficLane::gameplay);
    std::vector<std::uint8_t> big_back;
    std::vector<std::uint8_t> unreliable_back;
    for (int i = 0; i < 300 && (big_back.empty() || unreliable_back.empty()); ++i) {
        host.poll();
        guest.poll();
        guest.receive();
        for (const auto &message : host.receive()) {
            if (message.bytes != big) continue;
            if (big_back.empty()) {
                big_back = message.bytes;
            } else {
                unreliable_back = message.bytes;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    check(big_sent && big_back == big, "a message larger than one datagram arrives whole");
    check(big_unreliable_sent && unreliable_back == big, "a large unreliable message arrives whole too");

    // Two players at once: the host keeps them apart, and a message to one is not the other's.
    LanTransport second("Second");
    check(second.join(0, htonl(INADDR_LOOPBACK), host.port()), "a second guest dials the host");
    std::uint64_t second_id = 0;
    for (int i = 0; i < 400 && !(second_id && second.status().connected); ++i) {
        host.poll();
        guest.poll();
        second.poll();
        guest.receive();
        host.receive();
        second.receive();
        for (const auto &peer : host.status().peers)
            if (host.name(peer.id) == "Second") second_id = peer.id;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    check(second_id != 0, "the host lists the second guest");
    check(second.status().connected, "the second guest's handshake completed");
    check(host.status().peers.size() == 2, "the host lists both guests");

    bool second_got_private = false, guest_got_private = false;
    host.send(second_id, as_bytes("only for the second"), true, false, TrafficLane::control);
    for (int i = 0; i < 200 && !second_got_private; ++i) {
        host.poll();
        guest.poll();
        second.poll();
        host.receive();
        for (const auto &message : guest.receive())
            if (is(message.bytes, "only for the second")) guest_got_private = true;
        for (const auto &message : second.receive())
            if (is(message.bytes, "only for the second")) second_got_private = true;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    check(second_got_private, "a message addressed to one guest reaches it");
    check(!guest_got_private, "a message for one guest is not another's");

    // Reliable messages arrive in the order they were sent, however the network reorders them.
    for (int i = 0; i < 100; ++i) {
        const auto text = "in order " + std::to_string(i);
        guest.send(1, {reinterpret_cast<const std::uint8_t *>(text.data()), text.size()}, true, false, TrafficLane::control);
    }
    std::vector<std::string> ordered;
    for (int i = 0; i < 400 && ordered.size() < 100; ++i) {
        host.poll();
        guest.poll();
        second.poll();
        guest.receive();
        second.receive();
        for (const auto &message : host.receive()) ordered.emplace_back(message.bytes.begin(), message.bytes.end());
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    bool in_order = ordered.size() == 100;
    for (std::size_t i = 0; in_order && i < ordered.size(); ++i) in_order = ordered[i] == "in order " + std::to_string(i);
    check(in_order, "a hundred reliable messages arrive in the order they were sent");

    // A player that says nothing for longer than the peer timeout is still there: its pings are
    // what tells the host (and the host's answers tell the guest).
    const auto idle_until = std::chrono::steady_clock::now() + std::chrono::milliseconds(11'000);
    while (std::chrono::steady_clock::now() < idle_until) {
        host.poll();
        guest.poll();
        second.poll();
        host.receive();
        guest.receive();
        second.receive();
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    check(host.status().peers.size() == 2, "an idle player is not dropped by the host");
    check(guest.status().connected && second.status().connected, "idle guests still count themselves connected");

    // A disconnect is told to the other side, and shows up as a closed connection.
    host.disconnect(guest_id, "closing");
    for (int i = 0; i < 100 && !guest_saw_close; ++i) {
        guest.poll();
        for (const auto &message : guest.receive())
            if (is(message.bytes, "closing")) guest_saw_close = true;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    check(!guest.status().connected, "guest sees the host go away");
    check(!guest.take_closed(1).empty(), "guest was told why the host closed it");
    host.disconnect(second_id, "closing");
    check(host.status().peers.empty(), "host dropped both peers");

    // The client the session uses. LanClient reads what a player types and dials it with the same
    // transport the hosts above run: this is `mp lan <address>`, the whole of the LAN join path.
    LanTransport server2("Server");
    check(server2.open(0) && server2.host(4), "a host starts for the client path");
    const auto endpoint = "127.0.0.1:" + std::to_string(server2.port());

    std::uint32_t parsed_ip{};
    std::uint16_t parsed_port{};
    std::string parse_error;
    check(LanClient::parse_endpoint(endpoint, parsed_ip, parsed_port, parse_error) && parsed_port == server2.port(),
          "the client reads an address and port out of what a player types");
    check(!LanClient::parse_endpoint("192.168.1.20:0", parsed_ip, parsed_port, parse_error) && !parse_error.empty(),
          "a port of zero is refused, with a reason");
    check(!LanClient::parse_endpoint("localhost:27015", parsed_ip, parsed_port, parse_error),
          "a name is refused: a LAN join takes an address");

    LanClient client("Client");
    check(client.join(endpoint), "the LAN client joins a host by address");
    bool client_connected = false;
    std::uint64_t client_peer = 0;
    for (int i = 0; i < 400 && !client_connected; ++i) {
        server2.poll();
        client.transport().poll();
        client.transport().receive();
        server2.receive();
        client_connected = client.transport().status().connected;
        if (!client_peer && !server2.status().peers.empty()) client_peer = server2.status().peers.front().id;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    check(client_connected, "the client's handshake completed");
    check(client_peer != 0, "the host lists the client as a peer");

    // Both directions at once, over the path a session's packets take.
    for (int i = 0; i < 50; ++i) {
        const auto up = "client to host " + std::to_string(i);
        client.transport().send(1, {reinterpret_cast<const std::uint8_t *>(up.data()), up.size()}, true, false,
                                TrafficLane::control);
        const auto down = "host to client " + std::to_string(i);
        server2.send(client_peer, {reinterpret_cast<const std::uint8_t *>(down.data()), down.size()}, true, false,
                     TrafficLane::control);
    }
    std::vector<std::string> went_up, came_down;
    for (int i = 0; i < 600 && (went_up.size() < 50 || came_down.size() < 50); ++i) {
        server2.poll();
        client.transport().poll();
        for (const auto &message : server2.receive()) went_up.emplace_back(message.bytes.begin(), message.bytes.end());
        for (const auto &message : client.transport().receive())
            came_down.emplace_back(message.bytes.begin(), message.bytes.end());
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    bool up_in_order = went_up.size() == 50, down_in_order = came_down.size() == 50;
    for (std::size_t i = 0; up_in_order && i < went_up.size(); ++i)
        up_in_order = went_up[i] == "client to host " + std::to_string(i);
    for (std::size_t i = 0; down_in_order && i < came_down.size(); ++i)
        down_in_order = came_down[i] == "host to client " + std::to_string(i);
    check(up_in_order, "fifty reliable messages from the client arrive in order");
    check(down_in_order, "fifty reliable messages to the client arrive in order");

    // The same idle survival the session relies on: a joined client that says nothing but its pings
    // is still connected to its host after the peer timeout.
    const auto client_idle_until = std::chrono::steady_clock::now() + std::chrono::milliseconds(11'000);
    while (std::chrono::steady_clock::now() < client_idle_until) {
        server2.poll();
        client.transport().poll();
        client.transport().receive();
        server2.receive();
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    check(client.transport().status().connected && server2.status().peers.size() == 1,
          "an idle client is still connected to its host");

    server2.disconnect(client_peer, "closing");
    for (int i = 0; i < 200 && client.transport().status().connected; ++i) {
        client.transport().poll();
        client.transport().receive();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    check(!client.transport().status().connected, "the client sees the host close the session");
    check(!client.transport().take_closed(1).empty(), "the client was told why the host closed it");

    std::printf("%s\n", failures ? "LAN loopback: FAILED" : "LAN loopback: all checks passed");
    return failures ? 1 : 0;
}
