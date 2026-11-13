# LAN play

This fork of [ReSkate](https://github.com/Dingo-Shenanigans/ReSkate) plays skate. over a local
network instead of Steam's lobbies and relay. The game is unchanged; only the networking layer is.

Stock ReSkate reaches players "through Steam's relay network". This fork replaces that with a small
UDP protocol, so a LAN party needs no Steam account, no relay and no internet.

## What it adds

| Path | What it is |
| `Extension/Multiplayer/Net/transport.h` | The transport contract the session layer talks to |
| `Extension/Multiplayer/Lan/lan_transport.{h,cpp}` | The UDP transport: handshake, reliability, ordering |
| `Extension/Multiplayer/Lan/lan_discovery.{h,cpp}` | Beacons that let a machine find servers on its network |
| `Extension/Multiplayer/Lan/lan_loopback.cpp` | Two transports over loopback; every check below |
| `Server/main.cpp` | `--lan`, `--lan-auto`, `--lan-discover` |

`SteamTransport` implements the same contract, so the session layer above it is untouched and still
works the Steam way when this fork is not used.

## Running it

```sh
ReSkateServer --lan --no-update        # host, no Steam
ReSkateServer --lan-auto               # host unless another LAN server answers
ReSkateServer --lan-discover           # list servers on this network
```

The server binds UDP 27015 for play and sends beacons on UDP 27026. A host prints the address to
hand out; players see the server in the game's LAN list.

## Tests

```sh
cmake --build <build> --target lan_loopback && ./lan_loopback
```

Eighteen checks cover the handshake, both lanes, peer naming, acknowledgement (a reliable message
must be acknowledged rather than resent until the peer drops), messages larger than one datagram
being split and put back together on both lanes, clean disconnects, and the reason a peer was told
why it was closed.

## Limits

- Both ends must run this fork: stock ReSkate speaks Steam's protocol and the two cannot talk.
- No peer authentication. Anyone who can reach the port can join, which is fine on a home LAN and
  not fine on a shared network.
- `send_rate` is not enforced: a LAN has no relay to protect, so the transport sends as fast as it is given.
- AI skaters (`reskate.ai`) are refused inside a session by ReSkate itself, so bots and LAN play do
  not combine.
