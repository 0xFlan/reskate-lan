#!/bin/sh
# Installs the LAN pack over a normal ReSkate install: no compiler, no checkout.
#
#   ./install.sh                 # find ReSkate, show what would change (dry run)
#   ./install.sh --apply         # do it
#   ./install.sh --reskate-dir ~/Skate3Recomp --apply
#
# Everything it replaces is copied into LAN-backup/ first, and `LAN/restore.sh` puts it all back.
set -eu

APPLY=0
TARGET=""
while [ $# -gt 0 ]; do
    case "$1" in
        --apply) APPLY=1 ;;
        --reskate-dir) shift; TARGET="${1:-}" ;;
        -h|--help) sed -n '2,9p' "$0"; exit 0 ;;
        *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
    shift
done

here=$(cd "$(dirname "$0")" && pwd)

if [ -z "$TARGET" ]; then
    # ReSkate for Mac keeps the game under Application Support; Windows keeps it beside the launcher.
    for candidate in "$HOME/Library/Application Support/ReSkate for Mac/game" \
                     "$HOME/ReSkate/game" "$HOME/ReSkate" \
                     "${LOCALAPPDATA:-/nonexistent}/ReSkate/game" "${APPDATA:-/nonexistent}/ReSkate/game"; do
        if [ -d "$candidate" ]; then TARGET="$candidate"; break; fi
    done
fi
if [ -z "$TARGET" ] || [ ! -d "$TARGET" ]; then
    echo "Could not find a ReSkate folder. Point at it with --reskate-dir <folder>." >&2
    echo "On macOS it is ~/Library/Application Support/ReSkate for Mac/game." >&2
    echo "Hosting needs no install at all: run bin/ReSkateServer --lan (Windows: bin\\windows\\start-server.bat)." >&2
    exit 1
fi
TARGET=$(cd "$TARGET" && pwd)

# The name this platform's runtime goes by. The pack carries a Windows one; on the others only the
# server is installed, so a Mac or Linux install is never handed a DLL it would never load.
case "$(uname -s)" in
    Darwin) runtime="ReSkate.dylib" ;;
    Linux) runtime="libReSkate.so" ;;
    *) runtime="ReSkate.dll" ;;
esac

# Skate 3's recomp has its own folder and its own runtime; patching it would break it silently.
if [ ! -f "$TARGET/$runtime" ] && [ ! -f "$TARGET/ReSkateLauncher.exe" ]; then
    echo "$TARGET has no $runtime or ReSkateLauncher.exe: that is not a ReSkate (skate.) install." >&2
    echo "The Skate 3 recomp is a different project and must not be patched." >&2
    exit 1
fi

server_src=""
dll_src=""
if [ -f "$here/bin/ReSkateServer" ]; then server_src="$here/bin/ReSkateServer"; fi
if [ -f "$here/bin/$runtime" ]; then dll_src="$here/bin/$runtime"; fi

echo "ReSkate folder : $TARGET"
echo "Server binary  : ${server_src:-(missing from bin/)}"
echo "Runtime        : ${dll_src:-(missing from bin/)}"

server_dst="$TARGET/ReSkateServer"
dll_dst="$TARGET/$(basename "${dll_src:-ReSkate.dll}")"

plan() { echo "  $1"; }
echo "Would:"
[ -n "$server_src" ] && plan "back up ${server_dst#"$TARGET"/} to LAN-backup/ and replace it"
[ -n "$dll_src" ] && plan "back up ${dll_dst#"$TARGET"/} to LAN-backup/ and replace it"
plan "write LAN/start-server.sh, LAN/join-help.txt and LAN/restore.sh"

if [ "$APPLY" -eq 0 ]; then
    echo "Dry run: nothing written. Add --apply to install."
    exit 0
fi

backup="$TARGET/LAN-backup"
mkdir -p "$backup" "$TARGET/LAN"
if [ -n "$server_src" ]; then
    [ -f "$server_dst" ] && cp -p "$server_dst" "$backup/"
    cp -p "$server_src" "$server_dst"
    chmod +x "$server_dst"
fi
if [ -n "$dll_src" ]; then
    [ -f "$dll_dst" ] && cp -p "$dll_dst" "$backup/"
    cp -p "$dll_src" "$dll_dst"
fi

cat > "$TARGET/LAN/start-server.sh" <<'LAUNCH'
#!/bin/sh
# Starts the Steam-free LAN server. --lan-auto stands down when another LAN server answers.
cd "$(dirname "$0")/.." || exit 1
exec ./ReSkateServer --lan-auto --no-update "$@"
LAUNCH
chmod +x "$TARGET/LAN/start-server.sh"

cat > "$TARGET/LAN/restore.sh" <<'RESTORE'
#!/bin/sh
# Puts the original ReSkate files back and removes the LAN folder.
cd "$(dirname "$0")/.." || exit 1
for f in LAN-backup/*; do
    [ -e "$f" ] || continue
    cp -p "$f" .
    echo "restored $(basename "$f")"
done
rm -rf LAN LAN-backup
echo "Restored. ReSkate is back to its normal build."
RESTORE
chmod +x "$TARGET/LAN/restore.sh"

cat > "$TARGET/LAN/join-help.txt" <<'HELP'
Everyone who wants to play needs this same pack: stock ReSkate speaks Steam's network, this
build speaks LAN, and the two cannot talk to each other.

On this machine:   LAN/start-server.sh
                  (the log prints the address to give out, such as 192.168.1.20:27015)
On each player:    install this pack over their ReSkate the same way, start the game as usual,
                  and in its console join the host:
                      mp lan 192.168.1.20:27015
                  The port is optional when it is 27015. Steam sessions are untouched: mp host
                  and mp join still run them exactly as before, and on a LAN session the Steam
                  lobby and server browser are not used at all.
Firewall:          allow incoming UDP 27015 (game) and 27026 (discovery)
HELP

echo "Installed. Start it with: $TARGET/LAN/start-server.sh"
if [ -z "$dll_src" ]; then
    echo "Note: bin/ has no runtime for $(uname -s), so this installs the host only; a player's game" >&2
    echo "here still joins over Steam. The Windows pack joins with: mp lan <address>." >&2
fi
