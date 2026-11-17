// A check of the LAN transport with no game and no Steam: one process runs a host and a guest
// against each other over 127.0.0.1, and asserts the handshake, a reliable message each way, an
// unreliable one, and a clean disconnect. Built as `lan_loopback` by cmake/Apps.cmake.
#include "Extension/Multiplayer/Lan/lan_socket.h"
#include "Extension/Multiplayer/Lan/lan_transport.h"
#include "Extension/Multiplayer/Lan/lan_client.h"
#include "Extension/Multiplayer/Lan/lan_session.h"

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
            sent_hello = guest.send(lan_host_peer, as_bytes("hello from the guest"), true, false, TrafficLane::control);
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
            sent_unreliable = guest.send(lan_host_peer, as_bytes("unreliable from the guest"), false, false, TrafficLane::gameplay);
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
    const bool big_sent = guest.send(lan_host_peer, big, true, false, TrafficLane::control);
    const bool big_unreliable_sent = guest.send(lan_host_peer, big, false, false, TrafficLane::gameplay);
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
        guest.send(lan_host_peer, {reinterpret_cast<const std::uint8_t *>(text.data()), text.size()}, true, false, TrafficLane::control);
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
    check(!guest.take_closed(lan_host_peer).empty(), "guest was told why the host closed it");
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
        client.transport().send(lan_host_peer, {reinterpret_cast<const std::uint8_t *>(up.data()), up.size()}, true, false,
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
    check(!client.transport().take_closed(lan_host_peer).empty(), "the client was told why the host closed it");

    // ---- A password-protected server: no password, a wrong one, the right one ----
    // A host that demands a password challenges the peer, takes nothing from it until it proves the
    // password, and never gives it a place in the session: the answer is a refusal with its reason,
    // not a join that quietly never happened.
    {
        LanTransport locked("Locked host");
        check(locked.set_password("hunter2", true), "a host takes a password");
        check(locked.open(0) && locked.host(4), "a password-protected host listens");
        const auto locked_address = "127.0.0.1:" + std::to_string(locked.port());

        LanTransport none("Guest with no password");
        check(none.join(0, htonl(INADDR_LOOPBACK), locked.port()), "a guest with no password dials it");
        std::string refusal;
        for (int i = 0; i < 400 && refusal.empty(); ++i) {
            locked.poll();
            none.poll();
            locked.receive();
            none.receive();
            refusal = none.take_join_refusal();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        check(refusal.find("password") != std::string::npos,
              "a guest with no password is told the server needs its password");
        check(refusal.find(locked_address) != std::string::npos,
              "that refusal says which address to retry with");
        check(!none.status().connected, "a guest with no password is not connected");
        check(locked.status().peers.empty(), "the host has no phantom peer for a guest with no password");
        check(!none.send(lan_host_peer, as_bytes("session traffic"), true, false, TrafficLane::control),
              "a guest with no password cannot send session traffic");

        LanTransport wrong("Guest with the wrong password");
        check(wrong.set_password("hunter3", false), "a guest takes the wrong password");
        check(wrong.join(0, htonl(INADDR_LOOPBACK), locked.port()), "a guest with the wrong password dials it");
        std::string wrong_refusal;
        for (int i = 0; i < 400 && wrong_refusal.empty(); ++i) {
            locked.poll();
            wrong.poll();
            locked.receive();
            wrong.receive();
            wrong_refusal = wrong.take_join_refusal();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        check(wrong_refusal.find("Incorrect server password") != std::string::npos,
              "a guest with the wrong password is told the host refused it");
        check(!wrong.status().connected, "a guest with the wrong password is not connected");
        check(locked.status().peers.empty(), "the host has no phantom peer for a guest with the wrong password");

        // A peer that says hello and never proves anything is never welcomed, so it is never in the
        // session: nothing it sends is read, and nothing of the session is sent to it.
        const auto raw = ::socket(AF_INET, SOCK_DGRAM, 0);
        if (lan_socket_valid(raw)) {
            lan_socket_nonblocking(raw);
            sockaddr_in target{};
            target.sin_family = AF_INET;
            target.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            target.sin_port = htons(locked.port());
            std::vector<std::uint8_t> hello{'R', 'L', 'N', '1', 1, 0};
            const std::string name = "Never proves it";
            hello.push_back(static_cast<std::uint8_t>(name.size()));
            hello.insert(hello.end(), name.begin(), name.end());
            for (int i = 0; i < 8; ++i) hello.push_back(0); // its nonce, and no proof after it
            ::sendto(raw, reinterpret_cast<const char *>(hello.data()), static_cast<int>(hello.size()), 0,
                     reinterpret_cast<const sockaddr *>(&target), static_cast<int>(sizeof(target)));
            bool welcomed = false;
            char reply[512]{};
            for (int i = 0; i < 100 && !welcomed; ++i) {
                locked.poll();
                locked.receive();
                sockaddr_in from{};
                LanSockLen length = static_cast<LanSockLen>(sizeof(from));
                const auto got = ::recvfrom(raw, reply, static_cast<int>(sizeof(reply)), 0,
                                            reinterpret_cast<sockaddr *>(&from), &length);
                if (got >= 6 && reply[4] == 2) welcomed = true; // a welcome frame
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            check(!welcomed, "a peer that never authenticates is never welcomed");
            check(locked.status().peers.empty(), "and the host keeps no place in the session for it");
            lan_socket_close(raw);
        }

        LanTransport right("Guest with the right password");
        check(right.set_password("hunter2", false), "a guest takes the right password");
        check(right.join(0, htonl(INADDR_LOOPBACK), locked.port()), "a guest with the right password dials it");
        bool joined = false;
        for (int i = 0; i < 400 && !joined; ++i) {
            locked.poll();
            right.poll();
            locked.receive();
            right.receive();
            joined = right.status().connected;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        check(joined, "the right password joins");
        bool host_lists_it = false;
        for (int i = 0; i < 200 && !host_lists_it; ++i) {
            locked.poll();
            locked.receive();
            host_lists_it = locked.status().peers.size() == 1;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        check(host_lists_it, "the host has exactly that one player");
        bool up = false, down = false;
        const auto peer_id = locked.status().peers.empty() ? 0 : locked.status().peers.front().id;
        for (int i = 0; i < 200 && !(up && down); ++i) {
            right.send(lan_host_peer, as_bytes("proved it"), true, false, TrafficLane::control);
            locked.send(peer_id, as_bytes("welcome in"), true, false, TrafficLane::control);
            locked.poll();
            right.poll();
            for (const auto &message : locked.receive()) up = up || is(message.bytes, "proved it");
            for (const auto &message : right.receive()) down = down || is(message.bytes, "welcome in");
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        check(up && down, "session traffic flows both ways once the password is proved");
    }

    // An open server still takes a guest that has no password at all.
    {
        LanTransport open_host("Open host");
        check(open_host.open(0) && open_host.host(4), "an open host listens");
        LanTransport open_guest("Open guest");
        check(open_guest.join(0, htonl(INADDR_LOOPBACK), open_host.port()), "a guest with no password dials it");
        bool joined = false;
        for (int i = 0; i < 400 && !joined; ++i) {
            open_host.poll();
            open_guest.poll();
            open_host.receive();
            open_guest.receive();
            joined = open_guest.status().connected;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        check(joined && open_host.status().peers.size() == 1, "an open server still accepts a passwordless join");
    }

    std::printf("%s\n", failures ? "LAN loopback: FAILED" : "LAN loopback: all checks passed");
    return failures ? 1 : 0;
}
