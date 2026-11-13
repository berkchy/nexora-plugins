#!/bin/sh
# Builds the store payload bundles (nexora-plugins-bundle.zip) from the plugin
# sources. Mirrors the amxx-bundle schema consumed by the patcher.
#
# Usage: package-plugins.sh [arm64-v8a|armeabi-v7a|all]
#   arm64-v8a (default): produces dist/nexora-plugins-bundle.zip
#   armeabi-v7a        : produces dist/nexora-plugins-bundle-armeabi-v7a.zip
#   all                : both
set -eu

HERE="$(cd "$(dirname "$0")" && pwd)"
VER="${RELEASE_VERSION:-$(git -C "$HERE" rev-parse --short HEAD 2>/dev/null || echo dev)}"
ABI="${1:-arm64-v8a}"

arm64()
{
	_abi=arm64-v8a; _tag=arm64; _out=nexora-plugins-bundle.zip
	_build_one "$_abi" "$_tag" "$_out"
}

armeabi_v7a()
{
	_abi=armeabi-v7a; _tag=arm; _out=nexora-plugins-bundle-armeabi-v7a.zip
	_build_one "$_abi" "$_tag" "$_out"
}

_build_one()
{
	_abi="$1"; _tag="$2"; _out="$3"

	WORK="$HERE/dist/build/$_abi"
	STAGE="$HERE/dist/stage/$_abi"

	rm -rf "$WORK" "$STAGE"
	mkdir -p "$WORK" "$STAGE/lib/$_abi" "$HERE/dist"

	if [ -n "${ANDROID_NDK_HOME:-}" ] || [ -n "${ANDROID_HOME:-}" ]; then
		NDK="${ANDROID_NDK_HOME:-$(ls -d "$ANDROID_HOME"/ndk/* | head -1)}"
		TOOLCHAIN="-DCMAKE_TOOLCHAIN_FILE=$NDK/build/cmake/android.toolchain.cmake -DANDROID_ABI=$_abi -DANDROID_PLATFORM=android-21 -DANDROID_STL=c++_static"
	else
		TOOLCHAIN=""
	fi

	for p in centerprint hudpos thirdperson killfade killfeed scoreboard2; do
		cmake -S "$HERE/$p" -B "$WORK/$p" $TOOLCHAIN -DCMAKE_BUILD_TYPE=Release
		cmake --build "$WORK/$p"
		cp "$WORK/$p/lib${p}_nexora_${_tag}.so" "$STAGE/lib/$_abi/lib${p}_nexora_${_tag}.so"
	done

	cat > "$STAGE/bundle.json" <<EOF
{"version":"$VER","game":"cs16client","abi":"$_abi","entries":[
{"source":"lib/$_abi/libcenterprint_nexora_${_tag}.so","target":"lib/$_abi/libcenterprint_nexora_${_tag}.so","method":"STORED","required":true,"description":"centerprint"},
{"source":"lib/$_abi/libhudpos_nexora_${_tag}.so","target":"lib/$_abi/libhudpos_nexora_${_tag}.so","method":"STORED","required":true,"description":"hudpos"},
{"source":"lib/$_abi/libthirdperson_nexora_${_tag}.so","target":"lib/$_abi/libthirdperson_nexora_${_tag}.so","method":"STORED","required":true,"description":"thirdperson"},
{"source":"lib/$_abi/libkillfade_nexora_${_tag}.so","target":"lib/$_abi/libkillfade_nexora_${_tag}.so","method":"STORED","required":true,"description":"killfade"},
{"source":"lib/$_abi/libkillfeed_nexora_${_tag}.so","target":"lib/$_abi/libkillfeed_nexora_${_tag}.so","method":"STORED","required":true,"description":"killfeed"},
{"source":"lib/$_abi/libscoreboard2_nexora_${_tag}.so","target":"lib/$_abi/libscoreboard2_nexora_${_tag}.so","method":"STORED","required":true,"description":"scoreboard2"}
]}
EOF

	rm -f "$HERE/dist/$_out"
	( cd "$STAGE" && zip -qr "$HERE/dist/$_out" bundle.json lib )
	echo "$HERE/dist/$_out ($VER, $_abi)"
}

case "$ABI" in
	all)
		arm64
		armeabi_v7a
		;;
	arm64-v8a)
		arm64
		;;
	armeabi-v7a)
		armeabi_v7a
		;;
	*)
		echo "usage: $0 [arm64-v8a|armeabi-v7a|all]" >&2
		exit 2
		;;
esac
