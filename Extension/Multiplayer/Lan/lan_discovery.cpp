#include "lan_discovery.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <map>
#include <thread>

#ifndef _WIN32
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace dingosdk::multiplayer {
namespace {
constexpr std::uint8_t magic[5] = {'R', 'L', 'N', 'D', '1'};
constexpr std::uint64_t beacon_every_us = 500'000;

std::uint64_t now_us() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}

// Beacon: magic, name, map, game port, players, capacity.
std::vector<std::uint8_t> make_beacon(const std::string &name, const std::string &map, std::uint16_t port,
                                      unsigned players, unsigned capacity) {
    std::vector<std::uint8_t> out(std::begin(magic), std::end(magic));
    const auto text = [&out](const std::string &value) {
        const auto length = static_cast<std::uint8_t>(std::min<std::size_t>(value.size(), 255));
        out.push_back(length);
        out.insert(out.end(), value.begin(), value.begin() + length);
    };
    text(name);
    text(map);
    out.push_back(static_cast<std::uint8_t>(port & 0xff));
    out.push_back(static_cast<std::uint8_t>(port >> 8));
    const auto number = [&out](unsigned value) {
        out.push_back(static_cast<std::uint8_t>(value & 0xff));
        out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xff));
    };
    number(players);
    number(capacity);
    return out;
}
} // namespace

struct LanDiscovery::Impl {
#ifndef _WIN32
    int fd = -1;
#endif
    std::string name, map;
    std::uint16_t port{};
    unsigned players{}, capacity{};
    std::uint64_t last_beacon_us{};
    sockaddr_in broadcast{};
};

LanDiscovery::LanDiscovery() : impl_(std::make_unique<Impl>()) {}

LanDiscovery::~LanDiscovery() { stop(); }

bool LanDiscovery::advertise(std::string name, std::string map, std::uint16_t game_port, unsigned max_players) {
    stop();
    impl_->name = std::move(name);
    impl_->map = std::move(map);
    impl_->port = game_port;
    impl_->capacity = max_players;
#ifndef _WIN32
    const int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return false;
    int yes = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &yes, sizeof(yes));
    const int flags = ::fcntl(fd, F_GETFL, 0);
    ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    impl_->fd = fd;
    impl_->broadcast.sin_family = AF_INET;
    impl_->broadcast.sin_addr.s_addr = htonl(INADDR_BROADCAST);
    impl_->broadcast.sin_port = htons(discovery_port);
    impl_->last_beacon_us = 0; // so the next update() sends one straight away
    return true;
#else
    return false;
#endif
}

void LanDiscovery::update(unsigned players) {
#ifndef _WIN32
    if (impl_->fd < 0) return;
    const auto now = now_us();
    if (impl_->last_beacon_us && now - impl_->last_beacon_us < beacon_every_us) return;
    impl_->last_beacon_us = now;
    impl_->players = players;
    const auto beacon = make_beacon(impl_->name, impl_->map, impl_->port, impl_->players, impl_->capacity);
    // The subnet broadcast finds other machines; the loopback copy lets a server and a client on
    // one machine see each other, which is how this gets tested.
    ::sendto(impl_->fd, beacon.data(), beacon.size(), 0, reinterpret_cast<const sockaddr *>(&impl_->broadcast),
             sizeof(impl_->broadcast));
    sockaddr_in loopback{};
    loopback.sin_family = AF_INET;
    loopback.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    loopback.sin_port = htons(discovery_port);
    ::sendto(impl_->fd, beacon.data(), beacon.size(), 0, reinterpret_cast<const sockaddr *>(&loopback), sizeof(loopback));
#endif
}

void LanDiscovery::stop() {
#ifndef _WIN32
    if (impl_->fd >= 0) {
        ::close(impl_->fd);
        impl_->fd = -1;
    }
#endif
    impl_->last_beacon_us = 0;
}

std::vector<LanServer> LanDiscovery::discover(std::chrono::milliseconds wait) {
    std::vector<LanServer> servers;
#ifndef _WIN32
    const int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return servers;
    int yes = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &yes, sizeof(yes));
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(discovery_port);
    if (::bind(fd, reinterpret_cast<sockaddr *>(&address), sizeof(address)) < 0) {
        ::close(fd);
        return servers;
    }
    timeval timeout{0, 100'000}; // 100 ms, so the loop notices the deadline
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    const auto started = now_us();
    const auto deadline = started + static_cast<std::uint64_t>(wait.count()) * 1000;
    std::map<std::string, LanServer> heard; // by address:port
    while (now_us() < deadline) {
        std::uint8_t buffer[600];
        sockaddr_in from{};
        socklen_t length = sizeof(from);
        const auto received = ::recvfrom(fd, buffer, sizeof(buffer), 0, reinterpret_cast<sockaddr *>(&from), &length);
        if (received <= 0) continue;
        if (static_cast<std::size_t>(received) < sizeof(magic) + 8) continue;
        if (std::memcmp(buffer, magic, sizeof(magic)) != 0) continue;
        std::size_t at = sizeof(magic);
        const auto text = [&](std::string &out) {
            const std::size_t size = buffer[at++];
            if (at + size > static_cast<std::size_t>(received)) return false;
            out.assign(reinterpret_cast<const char *>(buffer + at), size);
            at += size;
            return true;
        };
        LanServer found;
        if (!text(found.name) || !text(found.map)) continue;
        if (at + 6 > static_cast<std::size_t>(received)) continue;
        found.port = static_cast<std::uint16_t>(buffer[at] | (buffer[at + 1] << 8));
        found.players = static_cast<unsigned>(buffer[at + 2] | (buffer[at + 3] << 8));
        found.max_players = static_cast<unsigned>(buffer[at + 4] | (buffer[at + 5] << 8));
        char dotted[INET_ADDRSTRLEN]{};
        ::inet_ntop(AF_INET, &from.sin_addr, dotted, sizeof(dotted));
        found.address = dotted;
        found.age = std::chrono::milliseconds(0);
        // One server can answer on several addresses (loopback and its LAN address, say): keep
        // the reachable one, so a LAN party sees one entry per server.
        const auto key = found.name + ":" + std::to_string(found.port);
        const bool loopback = found.address.rfind("127.", 0) == 0;
        if (const auto known = heard.find(key); known != heard.end() && (loopback || known->second.address.rfind("127.", 0) != 0))
            if (loopback || known->second.address.rfind("127.", 0) != 0) continue;
        heard[key] = std::move(found);
    }
    ::close(fd);
    for (auto &[key, server] : heard) servers.push_back(std::move(server));
#endif
    return servers;
}
} // namespace dingosdk::multiplayer
