# Skill: Upgrading logos-cpp-sdk in a Logos Module

**Source:** logos-notes SDK upgrade from Feb 25 to Apr 9 (2026-04-10)
**Applies to:** Any Logos module (notes, keycard-basecamp, etc.)

## Why you might need this

The logos-cpp-sdk evolves fast. Between Feb 25 and Apr 9:
- 7 headers → 17 headers
- New `LogosObject` type replaces `QObject*` in `requestObject` / `onEvent`
- New `logos_registry.h`, `logos_transport.h`, `plugin_registry.h`
- New `invokeRemoteMethodAsync` with proper callbacks
- `onEvent` changed from 4-arg `(QObject*, QObject*, name, callback)` to 3-arg `(LogosObject*, name, callback)`

An outdated SDK may cause:
- `capability_module.requestModule` returning `"false"` for valid requests
- `requestObject` returning NULL even when the target module is running
- Cross-module `invokeRemoteMethod` silently returning invalid QVariant
- Missing types (`LogosObject`) needed by generated module wrappers

## How to check your SDK version

```bash
# From your module's flake.lock:
cat flake.lock | python3 -c "
import json, sys, datetime
lock = json.load(sys.stdin)
for name, node in lock.get('nodes', {}).items():
    if 'logos-cpp-sdk' in name.lower():
        locked = node.get('locked', {})
        ts = locked.get('lastModified', 0)
        dt = datetime.datetime.fromtimestamp(ts) if ts else 'unknown'
        print(f'{name}: rev={locked.get(\"rev\",\"?\")[:12]} date={dt}')
"

# Count headers (rough version indicator):
ls /nix/store/*logos-cpp-sdk*/include/cpp/*.h | wc -l
# 7 = old (pre-Mar 2026), 17 = current
```

## How to upgrade

```bash
# Update just the SDK input (doesn't touch other deps):
nix flake update logos-cpp-sdk

# Verify the lock changed:
git diff flake.lock | grep logos-cpp-sdk

# Build in a FRESH directory (don't mix with old build artifacts):
rm -rf build-new
nix develop -c bash -c "cmake -B build-new -G Ninja -DCMAKE_BUILD_TYPE=Debug && cmake --build build-new -j4"

# Run tests INSIDE nix develop (new SDK has transitive deps like
# libzstd.so.1 that aren't in system library path):
nix develop -c bash -c "cd build-new && ctest --output-on-failure"
```

## Critical: tests must run inside nix develop

After upgrading, `ctest` outside the nix shell fails with:
```
error while loading shared libraries: libzstd.so.1: cannot open shared object file
```

The new SDK pulls in `libzstd`, `libglib-2.0`, `libz`, `libgthread-2.0`
as transitive dependencies. These exist in the nix store but not in
the system library path. Always run tests via:
```bash
nix develop -c bash -c "cd build-new && ctest --output-on-failure"
```

## What code changes to expect

### If you use LogosStorageTransport or similar direct IPC:

Old SDK:
```cpp
QObject* replica = client->requestObject("module_name");
client->onEvent(replica, this, "eventResponse", callback);  // 4 args
```

New SDK:
```cpp
LogosObject* replica = client->requestObject("module_name");
client->onEvent(replica, "eventResponse", callback);  // 3 args, no dest
```

### If you use invokeRemoteMethod:
No signature change — still works the same. But the internal token
acquisition may behave differently with the new registry/transport layer.

### If you use generated SDK wrappers (LogosModules):
Regenerate with the new `logos-cpp-generator`. The generator binary
is at `<sdk>/bin/logos-cpp-generator`.

## Backward compatibility with Basecamp runtime

Your module's `.so` links `liblogos_sdk.a` statically. The SDK
version in your binary does NOT need to match the Basecamp AppImage's
built-in SDK version — each module carries its own copy. No ABI
conflict at runtime.

Verified: notes plugin built with Apr 9 SDK loads and runs correctly
in Basecamp built with an older SDK (the pre-built AppImage).

## Version reference table

| Date | Rev (first 12) | Headers | Notable changes |
|------|----------------|---------|-----------------|
| 2025-10-23 | `4b143922c190` | 6 | Early SDK |
| 2026-01-06 | `32f1d7080d78` | ~8 | Added some types |
| 2026-02-25 | `95f763b48d74` | 7 | Our old version |
| 2026-03-23 | `4197ee183041` | ~17 | Tictactoe uses this; has LogosObject |
| 2026-04-09 | `8b1cfadf090f` | 17 | Our new version (latest as of Apr 10) |

## Checklist before merging an SDK upgrade

- [ ] `nix flake update logos-cpp-sdk` completed
- [ ] Fresh `build-new/` directory (don't reuse old build)
- [ ] `cmake -B build-new` inside `nix develop` — no errors
- [ ] `cmake --build build-new` — zero new warnings
- [ ] All tests pass inside `nix develop -c bash -c "cd build-new && ctest"`
- [ ] Manual Basecamp test: module loads, basic flow works
- [ ] Old `build/` directory preserved until upgrade is confirmed stable
- [ ] Commit only `flake.lock` — no code changes in the same commit
- [ ] Note the old and new SDK revs + dates in commit message
