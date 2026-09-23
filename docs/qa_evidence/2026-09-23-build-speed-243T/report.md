# Build-speed experiment (#243T)

Source baseline: committed `31e9ee10`, isolated worktree. Split version: the same commit plus the changes on this card. Host: `spark-dcc9`, 8 build jobs, GNU C++ 13.3, Qt 5, Ninja 1.11.1. These are single measurements, so machine load can shift the times.

| Case | Before | After | Change |
| --- | ---: | ---: | ---: |
| Cold `RelWithDebInfo` Ninja build (`-O2 -g`) | 85 s | 75 s | 10 s faster |
| Longest compile in that build | `main.cpp.o` 60.7 s | `RelayWindowCore.cpp.o` 37.3 s | 23.4 s shorter critical compile |
| Cold developer Ninja build (`-O0 -g1`) | 28.57 s | 27.33 s | 1.24 s faster |
| Incremental developer rebuild: `Pane.h` before, `PaneUi.cpp` after | 17.81 s | 6.08 s | 11.73 s faster |
| Incremental optimized rebuild after touching `PaneUi.cpp` | — | 14.74 s | no matching baseline at exact revision |

The split moves 18 `Pane` methods and 13 `RelayWindow` methods into nine `.cpp` files. A comparison against the original headers confirmed that every moved method body is byte-identical; only its signature moved out of the class. Remaining header edits still recompile several translation units, so the large incremental gain applies to edits in the new `.cpp` files. The developer build trades runtime optimization and detailed debug information for faster compilation; performance checks and releases keep the normal build.

Commands used:

```bash
# Each cold profile used an isolated Ninja build directory through this command.
RELAY_PROFILE_BUILD_DIR=/tmp/relay-243T-baseline-normal scripts/relay-profile build --out /tmp/relay-243T-profile-baseline-exact
RELAY_PROFILE_BUILD_DIR=/tmp/relay-243T-final-cold scripts/relay-profile build --out /tmp/relay-243T-profile-final

# Separate fast builds of baseline and split trees:
cmake -S . -B /tmp/relay-243T-fast -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo '-DCMAKE_CXX_FLAGS_RELWITHDEBINFO=-O0 -g1 -DNDEBUG'
ninja -C /tmp/relay-243T-fast relay -j8
# Rebuild measurements used `touch src/Pane.h` on baseline and `touch src/PaneUi.cpp` on split.

# User-facing wrapper in the split worktree:
scripts/relay-build --fast --target relay
scripts/relay-build --target relay
python3 -m unittest tests.test_relay_build
ctest --test-dir build -R '^(editor|panetabnavigation|windowstate|panestate|boardpane)$' --output-on-failure
```

Results: normal and fast Relay builds succeeded; eight wrapper tests and five selected CTest suites passed. The normal binary stayed running for an eight-second isolated Xvfb startup check and exited only when `timeout` sent SIGTERM (exit 124). The main checkout was not used for these builds.
