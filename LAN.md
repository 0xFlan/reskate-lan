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
| `Extension/Multiplayer/Lan/lan_socket.h` | The only platform-specific part: BSD sockets or WinSock |
| `Extension/Multiplayer/Lan/lan_client.{h,cpp}` | The client: reads an address and dials it with the transport above |
| `Extension/Multiplayer/Lan/lan_session.h` | The session tag a LAN session carries, the default port, and the host's peer number |
| `Extension/Multiplayer/Net/session_transport.{h,cpp}` | The session's transport: Steam's, or the LAN client's once `mp lan` |
| `Extension/Multiplayer/Lan/lan_loopback.cpp` | Three transports over loopback; every check below |
| `Server/main.cpp` | `--lan`, `--lan-auto`, `--lan-discover` |

`SteamTransport` implements the same contract, so the session layer above it is untouched and still
works the Steam way when this fork is not used.

## Running it

```sh
ReSkateServer --lan --no-update        # host, no Steam
ReSkateServer --lan-auto               # host unless another LAN server answers
ReSkateServer --lan-discover           # list servers on this network
```

The server binds UDP 27015 for play and sends beacons on UDP 27026. It prints the address to hand
out (`LAN: players join this machine on UDP 27015.`).

## Joining from the game

```
mp lan 192.168.1.20:27015
```

That is the whole of it: the address the host printed, and the port if it is not 27015. The game
then runs its session over plain UDP, as the server does. Steam stays the default — nothing about a
Steam session changes unless `mp lan` is used — and while a session is on the LAN path the Steam
lobby, the server browser and the Steam networking test are simply not used: `mp browse` says so
instead of asking Steam anything.

A LAN session's packets carry a fixed session tag (`lan_session.h`) rather than a join code's
secret, so joining needs no code out of band; who a message came from is the address it was
delivered from.

## Passwords

A server can keep a password, exactly as on Steam: `password <text>` in its console (or
`"password"` in its config) while it runs, and players who want in send it with the address:

```
mp lan 192.168.1.20:27015 hunter2
```

Nothing gets in without it, and the refusal is the host's own words:

```
That LAN server needs its password: mp lan 192.168.1.20:27015 <password>.
Incorrect server password.
```

The handshake itself carries the password: the host answers a hello with a challenge and a nonce of
its own, the player proves the password over both (HMAC-SHA256 with the key the lobby's own
PBKDF2 gives it), and until that proof arrives the host gives that address no peer number at all —
no place in the roster, nothing read from it, nothing sent to it. A player that never proves itself
is challenged and then forgotten. An open server never sends a challenge, so its handshake is the
three frames it always was.

The session then checks the same password again on its own handshake, as it does on Steam: the
server's `password` command, its config, and its join code's wording are untouched by any of this.

## Tests

```sh
cmake --build <build> --target lan_loopback && ./lan_loopback
```

Sixty-one checks cover the handshake, both lanes, peer naming, acknowledgement (a reliable message
must be acknowledged rather than resent until the peer drops), messages larger than one datagram
being split and put back together on both lanes, two guests kept apart by one host, a hundred
reliable messages arriving in the order they were sent, an idle player not being dropped, clean
disconnects, and the reason a peer was told why it was closed. The last twelve are the client the
session uses: `LanClient` reads `address:port` out of what a player types (and refuses a name or a
port of zero), joins a loopback host, exchanges fifty reliable messages in each direction in order,
stays connected through the peer timeout while idle, and sees the host's disconnect and its reason.
Twenty-two more are the password: a guest with none is told the server needs its password (and
which address to retry with), a guest with the wrong one is told the host refused it, neither is
connected and neither leaves a phantom peer behind, a peer that says hello and never proves itself
is never welcomed and keeps no place in the session, the right password joins and carries traffic
both ways, and an open server still takes a passwordless guest. The same binary is built and run on
Windows in CI, so the WinSock path is tested where nobody here develops on it.

## Limits

- Both ends must run this fork: stock ReSkate speaks Steam's protocol and the two cannot talk.
- The client half is built and loopback-tested, not game-tested. `mp lan` runs the same session
  code over the LAN transport, and CI compiles the runtime with it on Windows (`/W4 /WX`) and runs
  the join path on all three platforms — but nobody has joined a real game with it on Windows yet:
  that needs a Windows machine with skate. installed, which the maintainer of this fork does not
  have. Treat the first join as a test, not as a shipping claim.
- No peer authentication beyond the password: the password protects the session, not who a player
  is. Someone who knows it is in, with whatever name they like.
- No peer authentication. Anyone who can reach the port can join, which is fine on a home LAN and
  not fine on a shared network.
- `send_rate` is not enforced: a LAN has no relay to protect, so the transport sends as fast as it is given.
- AI skaters (`reskate.ai`) are refused inside a session by ReSkate itself, so bots and LAN play do
  not combine.
