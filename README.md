# Nexora Plugins

Plugin source tree for the Nexora CS 1.6 client. Each plugin is a standalone `.so` loaded by the client's native plugin loader (`lib*_nexora_<abi>.so`) and shipped through the Nexora store as an APK custom library.

The client (`cs16-client`) is kept vanilla; all custom features are plugins.

## Layout

```
include/nexora/nexora_plugin.h   # vendored ABI header (sync from cs16-client/public/nexora/)
killfade/                        # ScreenFade effect on local kills
killfeed/                        # CS:GO-style kill feed (replaces classic death notice)
scoreboard2/                     # CS:GO stacked team scoreboard
csgohud/                         # CS:GO-style HUD (hud_style 1)
sdk/                             # SDK header tree + make-sdk.sh (bundle for external devs)
package-sdk.sh                   # builds dist/nexora-plugin-sdk.zip from a client checkout
dist/                            # release artifacts (nexora-plugin-sdk.zip)
tools/smoketest.cpp              # stub-engine plugin load/tick smoke test (no game needed)
shared-assets/                   # game assets that ship with plugins (gfx/scoreboard/*)
cvars.md                         # cvar reference for the plugin suite
```

## Plugin SDK

`./package-sdk.sh` regenerates the SDK from a cs16-client checkout and produces `dist/nexora-plugin-sdk.zip` (headers + the `nexora-plugin-example` reference project + docs). External plugin authors download the zip and never need the client repo. See `sdk/README.md`.

## Building

Client SDK resolution order (first match wins): `$NEXORA_CLIENT_ROOT`, bundled `sdk/include` (run `sdk/make-sdk.sh` to refresh it), then `../../nexora/cs16-client`. Without any of them, only plugins that don't touch the engine SDK can compile (e.g. the `nexora-plugin-example`-style plugins). Example:

```sh
# Android arm64 (target ABI)
cmake -S killfade -B killfade/build \
	-DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake \
	-DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-21
cmake --build killfade/build
```

Or with just the bundled SDK:

```sh
cmake -S killfeed -B killfeed/build
cmake --build killfeed/build
```

Outputs: `libkillfade_nexora_<abi>.so`, `libkillfeed_nexora_<abi>.so`, `libscoreboard2_nexora_<abi>.so`, `libcsgohud_nexora_<abi>.so` (`arm64`, `arm`, `x86`, `x86_64`).

## Testing

`tools/smoketest.cpp` loads each plugin with a stubbed engine (no-op GL/SPR/console functions, fake cvars, fake player list) and fires the full hook scenario — DEATHMSG, NETMSG, FRAME, PAINT_HUD, toggles, vetoes and hide flags. It catches crashes, missing exports and typos without launching the game.

```sh
cmake -S killfade -B killfade/build && cmake --build killfade/build
# repeat for killfeed, scoreboard2, csgohud
SDK=$PWD/sdk/include && INC=$PWD/include && \
g++ -std=gnu++17 -o /tmp/smoketest tools/smoketest.cpp -I$INC \
  $(for d in engine common public pm_shared game_shared dlls cl_dll cl_dll/include; do printf -- "-I$SDK/$d "; done) -ldl
/tmp/smoketest killfade/build/*.so killfeed/build/*.so scoreboard2/build/*.so csgohud/build/*.so
```

## Shipping (Nexora store)

Publish the built `.so` for each supported ABI in the store manifest. The Nexora patcher embeds them as native APK libraries alongside the client; the loader discovers them on next client start by filename.

## Notes

- `killfeed` replaces the classic death notice via the `deathnotice` HUD element veto hook; `killfade` layers a `ScreenFade` flash on local kills.
- Plugins register their own cvars (`hud_killfade_*`, `hud_killfeed_*`) and must be shipped together with their asset prerequisites (`killfeed` uses hud.txt sprites, no extra files needed).
- Keep the vendored header in sync with `cs16-client/public/nexora/nexora_plugin.h` (`api_version` mismatch = rejected at load).
