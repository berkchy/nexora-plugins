#!/bin/sh
# Builds dist/nexora-plugin-sdk.zip from a cs16-client checkout.
# Usage: ./package-sdk.sh [path-to-cs16-client]
set -eu

HERE="$(cd "$(dirname "$0")" && pwd)"
SRC="${1:-${NEXORA_CLIENT_ROOT:-$HERE/../nexora/cs16-client}}"
EXAMPLE_DIR="${NEXORA_EXAMPLE_DIR:-$HERE/../nexora/nexora-plugin-example}"
NAME=nexora-plugin-sdk
TMP="$(mktemp -d)"
PKG="$TMP/$NAME"

trap 'rm -rf "$TMP"' EXIT

"$HERE/sdk/make-sdk.sh" "$SRC"

mkdir -p "$PKG/example"
cp -r "$HERE/sdk/include" "$PKG/include"
cp -r "$EXAMPLE_DIR/CMakeLists.txt" "$EXAMPLE_DIR/README.md" "$EXAMPLE_DIR/src" "$EXAMPLE_DIR/include" "$PKG/example/"
mkdir -p "$PKG/docs"
cp "$HERE/cvars.md" "$PKG/docs/cvars.md"
cp "$HERE/sdk/README.md" "$PKG/README.md"

mkdir -p "$HERE/dist"
rm -f "$HERE/dist/$NAME.zip"
( cd "$TMP" && zip -qr "$HERE/dist/$NAME.zip" "$NAME" )

echo "wrote $HERE/dist/$NAME.zip"
