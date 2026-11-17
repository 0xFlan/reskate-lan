#!/bin/sh
# Assembles the distributable from a finished build.
#   ./pack.sh [build-dir]        (default: ../build/macos)
set -eu

here=$(cd "$(dirname "$0")" && pwd)
build=${1:-"$here/../build/macos"}
out="$here/bin"

[ -d "$build" ] || { echo "no build at $build" >&2; exit 1; }
mkdir -p "$out"
rm -f "$out/ReSkateServer" "$out/ReSkate.dll" "$out/ReSkate.dylib" "$out/libReSkate.so"

found=0
if [ -f "$build/ReSkateServer" ]; then
    cp "$build/ReSkateServer" "$out/ReSkateServer"
    chmod +x "$out/ReSkateServer"
    found=1
fi
for name in ReSkate.dll libReSkate.so ReSkate.dylib; do
    if [ -f "$build/$name" ]; then cp "$build/$name" "$out/$name"; found=1; fi
done
[ "$found" -eq 1 ] || { echo "nothing to pack in $build" >&2; exit 1; }

# The Windows server (built by CI, copied in by hand: this zip is assembled off a Windows build's
# artifact). Its launcher is kept beside the other sources and copied in here, so the zip always
# carries the one that is in git.
win="$out/windows"
mkdir -p "$win"
[ -f "$here/windows/start-server.bat" ] && cp "$here/windows/start-server.bat" "$win/start-server.bat"
[ -f "$build/ReSkateServer.exe" ] && cp "$build/ReSkateServer.exe" "$win/ReSkateServer.exe"

# ReSkate is GPL-3.0: the zip carries the licence, and the source is the repository it came from.
cp "$here/../LICENSE" "$here/LICENSE" 2>/dev/null || true

version=$(date +%Y.%m.%d)
zip="$here/ReSkate-LAN-$version.zip"
rm -f "$zip"
( cd "$here" && zip -qr "$zip" bin install.sh README.md LICENSE )
echo "wrote $zip"
ls -1 "$out" "$win"
