#!/bin/sh
# Packages the cs16-client SDK headers into sdk/include/ so plugins can be built
# without a cs16-client clone.
# Usage: ./make-sdk.sh [path-to-cs16-client]
set -eu

SRC="${1:-${NEXORA_CLIENT_ROOT:-}}"
DEST="$(cd "$(dirname "$0")" && pwd)/include"

if [ -z "$SRC" ]; then
	echo "usage: $0 <path-to-cs16-client>" >&2
	exit 2
fi

if [ ! -f "$SRC/engine/APIProxy.h" ]; then
	echo "not a cs16-client checkout: $SRC" >&2
	exit 2
fi

rm -rf "$DEST"
mkdir -p "$DEST"

copy_headers()
{
	src_dir="$1"
	dst_dir="$2"
	mkdir -p "$dst_dir"
	( cd "$src_dir" && find . -type f \( -name '*.h' -o -name '*.hpp' \) -print | while read -r f; do
		mkdir -p "$dst_dir/$(dirname "$f")"
		cp "$f" "$dst_dir/$f"
	done )
}

for dir in engine common public pm_shared game_shared dlls; do
	copy_headers "$SRC/$dir" "$DEST/$dir"
done

mkdir -p "$DEST/cl_dll/include"
cp "$SRC/cl_dll/util_vector.h" "$DEST/cl_dll/util_vector.h"
for f in wrect.h parsemsg.h cl_dll.h; do
	cp "$SRC/cl_dll/include/$f" "$DEST/cl_dll/include/$f"
done

echo "SDK written to $DEST"
