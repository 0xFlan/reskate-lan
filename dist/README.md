# LAN pack

Steam-free LAN play for ReSkate: one machine hosts on the local network, everyone on that network
joins it over plain UDP, with no accounts, no relay and no internet.

## What is in here

| Path | What it is |
| `bin/ReSkateServer` | The LAN server, for this platform: `--lan`, `--lan-auto`, `--lan-discover` |
| `bin/windows/ReSkateServer.exe` | The same server, built by CI for Windows |
| `bin/windows/start-server.bat` | Starts it there: double-click, or pass options after the name |
| `bin/ReSkate.dll` | The Windows runtime with the LAN client, built by CI from the same commit |
| `install.sh` | Finds ReSkate, backs up, copies, writes the launchers |
| `pack.sh` | Assembles a release zip from a finished build |

## Hosting

```sh
bin/ReSkateServer --lan            # host on UDP 27015, announcing on 27026
bin/ReSkateServer --lan-auto       # the same, but stand down if a LAN server is already up
bin/ReSkateServer --lan-discover   # list the servers answering on this network
```

On Windows, `bin\windows\start-server.bat` runs `--lan-auto` for you. The server needs no game
install and no Steam: it is the only file you have to run. It prints the address to hand out:

```
LAN: players join this machine on UDP 27015.
```

## Joining

Replace the runtime and start the game as usual, then in the console:

```
mp lan 192.168.1.20:27015
```

That is the address the host printed (the port is optional when it is 27015). The session then runs
over the same UDP transport the server runs, and the Steam lobby, server browser and Steam
networking test are not used at all while it does. `mp browse` says so rather than asking Steam.

Steam remains the default: `mp host`, `mp join` and everything else about a Steam session are
unchanged. A LAN session is not authenticated — its packets carry a fixed session tag instead of a
join code's secret, and who sent a message is decided by the address it came from — which is what
makes joining by address possible at all, and is only fine on a network you trust.

## Installing the runtime

```sh
./install.sh                              # dry run, shows what would change
./install.sh --apply                      # finds ReSkate and does it
./install.sh --reskate-dir "$HOME/ReSkate" --apply   # or point at its folder
```

It backs everything up into `LAN-backup/`, and `LAN/restore.sh` undoes all of it. Hosting alone
needs none of this: run the server from `bin/` and leave the game as it is.

## How far this has been tested

- The server and the LAN transport are run on macOS, Linux and Windows in CI: 39 checks over the
  handshake, both lanes, fragmentation, ordering, two guests at once, idle survival and clean
  disconnects — plus a smoke test that starts a real server and finds it with `--lan-discover`.
- The join path is the same code (`LanClient` → `LanTransport`), and the loopback test joins a host
  through it on all three platforms: fifty reliable messages in each direction in order, an idle
  period, and a disconnect with its reason.
- The Windows runtime (`ReSkate.dll`) including the LAN client is compiled by CI under `/W4 /WX`.
- **Not tested:** joining an actual game on Windows. That needs a Windows machine with skate.
  installed, and the maintainer of this fork has none. The first real join is the test.

## Known limits

- UDP 27015 (game) and 27026 (discovery) must be allowed in; macOS asks the first time.
- A password-protected LAN server cannot be joined from the game yet: `mp lan` takes an address, and
  the password prompt travels with a join code a LAN session does not use.
- No peer authentication: anyone who can reach the port can join. Fine on a home LAN, not on a
  shared network.
- Everyone needs this same build: stock ReSkate speaks Steam's network, this one speaks LAN.
