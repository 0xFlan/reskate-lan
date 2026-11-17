#pragma once
#include <cstdint>

namespace dingosdk::multiplayer {
// A LAN session is not authenticated (LAN.md): its packets carry this fixed session tag instead of
// the random per-host secret a Steam join code carries, so a player can join by address without
// reading a code out of the host's log. Who a message came from is the transport's business: the
// host checks the address against the peer it handshook with, and only that address is answered.
inline constexpr std::uint64_t lan_session_secret = 0x4C414E0100000001ULL;
// A LAN peer's number is shaped like a SteamID64 (public universe, individual account) because
// everything above the transport — the roster, roles, bans, the session's own admission — is keyed
// by SteamID64s. The account part sits at the top of the range real accounts never reach, so no LAN
// player can be taken for a Steam player who is banned, reserved or given a role. The host is this
// one; the players it hands numbers to are the ones after it.
inline constexpr std::uint64_t lan_host_peer = 76561197960265728ULL + 0xFF000001ULL;
// The UDP port a LAN session listens on unless it says otherwise (Server/main.cpp).
inline constexpr std::uint16_t lan_game_port = 27015;
} // namespace dingosdk::multiplayer
