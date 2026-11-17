#!/bin/sh
# Starts a real LAN server and asks a second copy of it to find that server on this machine's own
# network. All of it runs over loopback — the beacon is sent to 127.0.0.1 as well as the subnet
# broadcast — so it needs no network, no game install and no Steam.
#
#   ./smoke-lan.sh [path-to-ReSkateServer]
#
# Exits 0 when the server came up on --lan and discovery found it on UDP 27015.
set -eu

here=$(cd "$(dirname "$0")" && pwd)
binary=${1:-"$here/bin/ReSkateServer"}
[ -x "$binary" ] || { echo "no executable at $binary" >&2; exit 1; }

work=$(mktemp -d)
server_pid=""
cleanup() {
    if [ -n "$server_pid" ]; then
        kill "$server_pid" 2>/dev/null || true
        wait "$server_pid" 2>/dev/null || true
    fi
    cd /
    rm -rf "$work"
}
trap cleanup EXIT INT TERM

# A copy in a folder of its own: the server writes ReSkateServer.json and its logs beside itself,
# and a smoke test must not touch a server that is already set up on this machine.
cp "$binary" "$work/ReSkateServer"
chmod +x "$work/ReSkateServer"
cd "$work"

./ReSkateServer --lan --no-update > server.log 2>&1 &
server_pid=$!

tries=0
while [ "$tries" -lt 150 ]; do
    grep -q "is up on" server.log 2>/dev/null && break
    sleep 0.1
    tries=$((tries + 1))
done
if ! grep -q "is up on" server.log; then
    echo "FAIL: the server did not come up with --lan:" >&2
    cat server.log >&2
    exit 1
fi

# Two seconds is four beacons: hearing none of them means discovery is broken, not unlucky.
sleep 2
found=$(./ReSkateServer --lan-discover 2>&1 || true)
printf '%s\n' "$found"
if ! printf '%s\n' "$found" | grep -q ":27015"; then
    echo "FAIL: --lan-discover did not find the server that is running on this machine." >&2
    exit 1
fi
echo "LAN smoke test: a --lan server was found by --lan-discover."
