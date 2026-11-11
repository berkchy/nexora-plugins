# Nexora Plugin SDK

Everything you need to write plugins for the Nexora CS 1.6 client — **no cs16-client checkout required**.

The Nexora client loads `lib*_nexora_<abi>.so` files from its native library directory at startup. A plugin is a shared library that exports two symbols (`nexora_plugin_query` and `nexora_plugin_init`) and registers hooks into the client.

## Contents

```
include/                    # SDK header tree (engine + common + the Nexora ABI header)
example/                    # nexora-plugin-example — a complete, buildable reference plugin
docs/cvars.md               # cvars registered by the shipped plugins
```

## Quickstart

```sh
unzip nexora-plugin-sdk.zip
cd nexora-plugin-sdk/example
cmake -S . -B build
cmake --build build
```

Output (on Android aarch64 hosts / with the NDK toolchain): `libsample_nexora_arm64.so`. The filename must match `lib<name>_nexora_<abi>.so` — that's how the loader discovers it.

## Building your own plugin

Copy `example/` as a starting point. Your plugin compiles against `include/` instead of a client checkout; CMake picks this up automatically (see `example/CMakeLists.txt` for the ABI/suffix logic and the include layout):

```cpp
#include <nexora/nexora_plugin.h>   // the ABI
#include <APIProxy.h>               // engine function table (cl_enginefunc_t)
#include <cvardef.h>                // cvar_t / FCVAR_*
```

If you have a cs16-client checkout instead, set `NEXORA_CLIENT_ROOT` — the example falls back to it when the bundled `include/` is absent, so the SDK you downloaded is optional in that case.

### Android (arm64-v8a, the ABI used in the APK)

```sh
cmake -S . -B build-android \
  -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake \
  -DANDROID_ABI=arm64-v8a \
  -DANDROID_PLATFORM=android-21
cmake --build build-android
```

## Shipping

The built `.so` is published through the Nexora store and injected into the client APK as a native library. The loader finds it automatically on the next client start — no config file entries needed.

## Notes

- `api_version` in `nexora_plugin_init` must match the client's (`NEXORA_PLUGIN_API_VERSION`, currently `1`) or the plugin is rejected with a console message.
- Hook callbacks run on the engine main thread; do not call blocking/threaded engine APIs from a hook.
- `RTLD_NODELETE`: unloading a plugin at runtime is not supported — restart the client.
- SDK is a snapshot of the client headers; regenerate it from a cs16-client checkout with `sdk/make-sdk.sh` when the client SDK changes. The bundled `include/` always matches the client SDK version it was packaged from.
