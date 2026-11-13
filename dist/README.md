# LAN pack

A Steam-free LAN server for ReSkate: one machine hosts, everyone on the network joins, no
accounts and no relay.

## Why this is a pack and not a mod

ReSkate mods carry game data and Lua plugins; ReSkate refuses mods that "ship programs", and the
plugin sandbox has no sockets — `reskate.net` only carries messages between copies of a plugin.
The connection itself lives in the runtime and the server, so LAN play has to replace those two
files. This pack does exactly that, from a folder, with no compiler and no checkout:

```sh
./install.sh                              # dry run, shows what would change
./install.sh --reskate-dir ~/Skate3Recomp --apply
```

It backs up everything it replaces into `LAN-backup/`, and `LAN/restore.sh` undoes all of it.

## What is in here

| Path | What it is |
| `bin/ReSkateServer` | The LAN server: `--lan`, `--lan-auto`, `--lan-discover` |
| `bin/<runtime>` | The LAN-capable runtime, when this pack was built on the platform that has one |
| `install.sh` | Finds ReSkate, backs up, copies, writes the launchers |
| `pack.sh` | Assembles a release zip from a finished build |

## Using it

```sh
LAN/start-server.sh        # hosts; --lan-auto stands down if a server is already up
ReSkateServer --lan-discover   # lists servers answering on the LAN
```

Everyone playing needs the same pack: stock ReSkate speaks Steam's network and this one speaks
LAN, so the two cannot be mixed. The pack's version of the runtime is what makes a machine able
to join without Steam.

## Known limits

- UDP 27015 (game) and 27026 (discovery) must be allowed in; macOS asks the first time.
- The runtime part of the pack only exists for platforms whose runtime builds. The server builds
  everywhere this repository's CMake does.
- No peer authentication: anyone who can reach the port can join. Fine on a home LAN, not on a
  shared network.
