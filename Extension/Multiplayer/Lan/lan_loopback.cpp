// A check of the LAN transport with no game and no Steam: one process runs a host and a guest
// against each other over 127.0.0.1, and asserts the handshake, a reliable message each way, an
// unreliable one, and a clean disconnect. Built as `lan_loopback` by cmake/Apps.cmake.
#include "Extension/Multiplayer/Lan/lan_transport.h"

#include <arpa/inet.h>
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
    check(host.status().peers.empty(), "host dropped the peer");
    std::printf("%s\n", failures ? "LAN loopback: FAILED" : "LAN loopback: all checks passed");
    return failures ? 1 : 0;
}
