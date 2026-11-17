#include "Extension/Multiplayer/Net/session_transport.h"
#include "Extension/Multiplayer/Lan/lan_client.h"
#include "Extension/Multiplayer/Steam/steam_transport.h"

namespace dingosdk::multiplayer {
SessionTransport::SessionTransport() : steam_(std::make_unique<SteamTransport>()) {}

SessionTransport::~SessionTransport() { stop(); }

Transport &SessionTransport::active() {
    return lan_ ? static_cast<Transport &>(lan_->transport()) : static_cast<Transport &>(*steam_);
}

const Transport &SessionTransport::active() const {
    return lan_ ? static_cast<const Transport &>(lan_->transport()) : static_cast<const Transport &>(*steam_);
}

bool SessionTransport::use_lan(std::string player_name, std::string_view endpoint, std::string_view password,
                               std::string &error) {
    auto client = std::make_unique<LanClient>(std::move(player_name));
    if (!client->join(endpoint, password)) {
        error = client->error();
        return false;
    }
    if (lan_) lan_->stop();
    lan_ = std::move(client);
    return true;
}

// Steam's own extras. A LAN session has none of what they do: it browses no lobbies, it runs no
// Steam networking test, and every one of its peers is connected by the handshake it just made.
bool SessionTransport::open() { return lan_ ? false : steam_->open(); }

bool SessionTransport::socket_test() { return lan_ ? active().status().ready : steam_->socket_test(); }

void SessionTransport::connect_peer(std::uint64_t id) {
    if (!lan_) steam_->connect_peer(id);
}

bool SessionTransport::host(unsigned capacity) { return active().host(capacity); }

bool SessionTransport::join(std::uint64_t id, std::uint32_t direct_ip, std::uint16_t direct_port) {
    return active().join(id, direct_ip, direct_port);
}

void SessionTransport::stop() {
    // A LAN join ends here and the session goes back to Steam: the next host or join finds the
    // Steam transport exactly as it left it.
    if (lan_) {
        lan_->stop();
        lan_.reset();
    }
    steam_->stop();
}

void SessionTransport::poll() { active().poll(); }

std::vector<TransportMessage> SessionTransport::receive() { return active().receive(); }

bool SessionTransport::send(std::uint64_t id, std::span<const std::uint8_t> bytes, bool reliable, bool fresh,
                            TrafficLane lane) {
    return active().send(id, bytes, reliable, fresh, lane);
}

void SessionTransport::send_batch(std::span<TransportSend> messages) { active().send_batch(messages); }

void SessionTransport::disconnect(std::uint64_t id, const char *reason) { active().disconnect(id, reason); }

std::int64_t SessionTransport::pending(std::uint64_t id) const { return active().pending(id); }

const TransportStatus &SessionTransport::status() const { return active().status(); }

std::string SessionTransport::name(std::uint64_t id) { return active().name(id); }

std::vector<TransportLink> SessionTransport::links() { return active().links(); }

std::string SessionTransport::take_closed(std::uint64_t id) { return active().take_closed(id); }

std::string SessionTransport::link_report(std::uint64_t id) { return active().link_report(id); }

std::string SessionTransport::relay_status() const { return active().relay_status(); }

bool SessionTransport::is_lan() const { return active().is_lan(); }

std::vector<std::string> SessionTransport::take_direct_notes() { return active().take_direct_notes(); }

bool SessionTransport::set_steam_debug(bool on) { return lan_ ? false : steam_->set_steam_debug(on); }

bool SessionTransport::set_send_rate(int bytes_per_second) { return active().set_send_rate(bytes_per_second); }

int SessionTransport::send_rate() const { return active().send_rate(); }

void SessionTransport::set_packing(unsigned milliseconds) { active().set_packing(milliseconds); }

bool SessionTransport::listen_direct(std::uint16_t port) { return lan_ ? false : steam_->listen_direct(port); }

void SessionTransport::allow_peers(std::span<const Member> members) { active().allow_peers(members); }

bool SessionTransport::set_password(std::string_view password, bool hosting) {
    return active().set_password(password, hosting);
}

std::string SessionTransport::take_join_refusal() { return active().take_join_refusal(); }
} // namespace dingosdk::multiplayer
