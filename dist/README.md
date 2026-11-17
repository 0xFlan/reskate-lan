# LAN pack

A Steam-free server for ReSkate: one machine hosts on the local network, with no accounts, no
relay and no internet.

## What works today, and what does not

The **host** half is finished. `ReSkateServer --lan` drops Steam entirely, binds UDP 27015 for play
and announces itself on UDP 27026, so a LAN party finds the server without being told an address.
It builds and runs on macOS, Linux and Windows, and the LAN transport under it is tested on all
three (`lan_loopback`, 27 checks, run in CI).

The **joining** half is not in the game yet. Stock ReSkate builds a session on Steam's transport,
and so does this fork's runtime so far: `ReSkate.dll` still hands the session a `SteamTransport`, so
a player's game cannot yet join a LAN server. That wiring is the next piece of work. Until it
lands, this pack is a host: you can stand a server up with no Steam account, and the transport that
will carry players is finished and tested underneath it.

## Why this is a pack and not a mod

ReSkate mods carry game data and Lua plugins; ReSkate refuses mods that "ship programs", and the
plugin sandbox has no sockets — `reskate.net` only carries messages between copies of a plugin.
The connection itself lives in the runtime and the server, so LAN play has to replace those two
files. This pack does that, from a folder, with no compiler and no checkout:

```sh
./install.sh                              # dry run, shows what would change
./install.sh --apply                      # finds ReSkate and does it
./install.sh --reskate-dir "$HOME/ReSkate" --apply   # or point at its folder
```

It backs up everything it replaces into `LAN-backup/`, and `LAN/restore.sh` undoes all of it.

## What is in here

| Path | What it is |
| `bin/ReSkateServer` | The LAN server, for this platform: `--lan`, `--lan-auto`, `--lan-discover` |
| `bin/windows/ReSkateServer.exe` | The same server, built by CI for Windows |
| `bin/windows/start-server.bat` | Starts it there: double-click, or pass options after the name |
| `bin/<runtime>` | The LAN-capable runtime, when this pack was built where one can be built |
| `install.sh` | Finds ReSkate, backs up, copies, writes the launchers |
| `pack.sh` | Assembles a release zip from a finished build |

## Hosting

```sh
bin/ReSkateServer --lan            # host on UDP 27015, announcing on 27026
bin/ReSkateServer --lan-auto       # the same, but stand down if a LAN server is already up
bin/ReSkateServer --lan-discover   # list the servers answering on this network
```

On Windows, `bin\windows\start-server.bat` runs `--lan-auto` for you. The server needs no game
install and no Steam: it is the only file you have to run. The log prints the address to hand out.

Everyone who will play needs the same build of the fork once the client half lands: stock ReSkate
speaks Steam's network, this one speaks LAN, and the two cannot be mixed.

## Known limits

- UDP 27015 (game) and 27026 (discovery) must be allowed in; macOS asks the first time.
- The runtime part of the pack only exists for platforms whose runtime builds. The server builds
  everywhere this repository's CMake does.
- The host half of the pack is what works; see "What works today" above.
- No peer authentication: anyone who can reach the port can join. Fine on a home LAN, not on a
  shared network.
