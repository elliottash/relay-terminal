---
id: V52P
type: work
status: needs-verification
labels: [feature, build, performance]
assignee: agent
implemented_by: anthropic/claude-opus-5-5 via claude-code
session: 73b7fca5-c808-4080-89e3-8ee47f586b7f
rank: zzzzzzzzzzzzzzzzzzzzzw
created: '2026-09-24'
source: Claude Code pane, 2026-09-24, analysis of session 3f4a20ad (#234Z)
links: {plans: [], commits: [52ea9245, 282ceccf], evidence: [docs/qa_evidence/2026-09-25-v52p-compiler-cache/measurements.md], related: [SZHQ, 76QW], github: null}
---
# Compiler cache (ccache) for build/, the land.py verify slots and CI: every agent's build recompiles Pane.h from scratch

## Issue
write cards for all 6 of your lessons and how to address them, and all 5 of your suggestsions.

## Planning notes
**Evidence.** Neither `ccache` nor `sccache` is installed, and nothing in `CMakeLists.txt`, `scripts/relay-build` or `scripts/land.py` sets `CMAKE_<LANG>_COMPILER_LAUNCHER`. #234Z's session ran 28 builds; every `Pane.h` touch recompiles every window source that includes it, and single build-plus-test calls took 40–190 s. Before #SZHQ there were 255 verify build trees, each compiled cold. Even with the 2-slot pool, a slot's objects are recompiled whenever headers differ between the sessions that use it in turn.

**How to address.**
1. Detect `ccache` (then `sccache`) at configure time and set `CMAKE_C_COMPILER_LAUNCHER`/`CMAKE_CXX_COMPILER_LAUNCHER`, with `-DRELAY_COMPILER_CACHE=OFF` to opt out. One shared cache dir (`$XDG_CACHE_HOME/relay/ccache`, capped at e.g. 10 GB, and listed by `relay-scratch`) for `build/`, `build-fast/` and every verify slot, so a header state compiled once by any session is reused by all of them.
2. `CCACHE_BASEDIR` set to the source root, so objects hit across the checkout and the slot paths (`/tmp/.../verify-slots/*/src`).
3. CI: cache the ccache dir between runs on all three platforms (docs/BUILDING.md is the map).
4. `relay-tooling-setup` offers to install ccache.
5. Measure: the cold versus warm `relay` build time on the verify slot, recorded on this card.

**Done means.** A verify-slot build right after another session built the same headers is mostly cache hits (`ccache -s`); the timing before and after is recorded here.

## Done means
- Every cmake configure of a Relay tree — `build/`, `build-fast/`, a `land.py` verify slot — runs C++ compiles through one shared compiler cache when `ccache` (or `sccache`) is installed, and `-DRELAY_COMPILER_CACHE=OFF` turns it off; a test proves both.
- A verify-slot build of headers another session just compiled is mostly cache hits (`ccache -s`), with the cold vs warm build times recorded in `## Execution Summary`.
- Linux and Windows CI cache the compiler cache dir between runs (macOS already does) and print cache stats in the log.
- Failure looks like: a near-zero hit rate in `ccache -s` across two sessions' builds of the same tree, no launcher line in a configure log, or the cache dir growing past its cap.

## Plan
**Goal.** Make every Relay build — `build/`, `build-fast/`, the `land.py` verify slots, CI — compile through one shared compiler cache, so a header state compiled by one session is not recompiled by the next.

**Findings.**
- `CMakeLists.txt` (1,339 lines; `project(Relay ... LANGUAGES CXX)` at line 2 — no C compiler is enabled, so only `CMAKE_CXX_COMPILER_LAUNCHER` is needed) sets no launcher; options live at the top (`RELAY_BUILD_APP`, `RELAY_QT_MAJOR`). Neither ccache nor sccache is installed locally.
- `scripts/relay-build` configures with a plain `cmake -S/-B` (no launcher), and `scripts/land.py` `_run_verify_in()` (configure step, line ~1199) runs `cmake -S <slot>/src -B <slot>/build` for every verify build — both would pick a launcher set inside `CMakeLists.txt` with no script change: editing CMakeLists.txt itself triggers regeneration of `build/`, and verify slots reconfigure on every build.
- Slot sources mirror the checkout layout from the repo root (`src/…`), so with `CCACHE_BASEDIR` = each tree's source root, relative paths match across the checkout (`/home/elliott/repos/relay-terminal/src/Pane.h`) and slots (`/tmp/claude-1000/land/verify-slots/<repo>-<n>/src/src/Pane.h`) — cross-tree hits work.
- `.github/workflows/macos.yml` (lines 65–114) **already** wires ccache end-to-end: brew install, `CCACHE_DIR=$RUNNER_TEMP/relay-ccache` (400 MB cap, compression), `actions/cache` keyed on arch/Qt/SDK/compiler, `-DCMAKE_C/CXX_COMPILER_LAUNCHER=ccache`, `ccache --show-stats`. `ci.yml` (ubuntu; configure at line 31) and `windows.yml` (MSVC; app configure at line 57) have nothing.
- `scripts/relay-tooling-setup` has the optional-apt pattern (`want+=(…)` + a check table) — `ccache` belongs there.
- `backend/relay_core/scratch.py` `default_roots()`/`report()` cover the tmp scratch roots only; a cache dir under `~/.cache` is invisible to `relay-scratch` today.

**Steps.**
1. New `cmake/CompilerCache.cmake`, included from `CMakeLists.txt` right after `project()`: option `RELAY_COMPILER_CACHE` (default ON); `find_program` for `ccache`, then `sccache`; only when `CMAKE_CXX_COMPILER_LAUNCHER` is empty (explicit flags, e.g. macOS CI's, keep winning). Set `CMAKE_CXX_COMPILER_LAUNCHER` to `${CMAKE_COMMAND};-E;env;CCACHE_DIR=<dir>;CCACHE_BASEDIR=${CMAKE_SOURCE_DIR};CCACHE_CONFIGPATH=<dir>/ccache.conf;<tool>`, where `<dir>` = `$XDG_CACHE_HOME|~/.cache`/relay/ccache. Write that `ccache.conf` idempotently at configure (`max_size = 10G`, `compression = true`; `RELAY_CCACHE_MAX` overrides the cap), and `message(STATUS)` one line saying which tool and dir.
2. No change to `scripts/relay-build` or `scripts/land.py` — confirm instead, by checking `CMakeCache.txt` in `build/` and a slot after their next configure, that the launcher landed.
3. `scripts/relay-tooling-setup`: add `ccache` to the optional apt `want` list and the check table (row: optional, compiler cache for build/ and verify slots).
4. `backend/relay_core/scratch.py` (+ `scripts/relay-scratch` CLI): `report()` lists the shared ccache dir as its own kind (e.g. `compiler-cache`) when it exists, `check()` counts it against the budget; `gc()` never removes it (ccache evicts by `max_size`).
5. CI: `ci.yml` — apt install ccache, `actions/cache@v4` on `$RUNNER_TEMP/relay-ccache` with a linux key on Qt major + compiler (copy the macos.yml pattern), launcher flags on the line-31 configure. `windows.yml` — `choco install ccache`, same cache step, launcher flags on the line-57 app configure (leave the line-50 packaging configure alone). `macos.yml` needs nothing: its explicit flag wins over step 1's empty-check.
6. Docs: a short ccache paragraph in `docs/BUILDING.md` (the CI map) and the build section of `CLAUDE.md`; the `disk-hygiene` skill notes the cache dir and its cap.
7. Tests: extend `tests/test_relay_build.py` (or a new `tests/test_compiler_cache.py` using the same throwaway-project harness) — with a fake `ccache` on PATH: launcher set, conf written, `CCACHE_BASEDIR` correct; with `-DRELAY_COMPILER_CACHE=OFF`: launcher empty; with a pre-set launcher: not overridden. Extend `tests/test_scratch.py`: the cache dir appears as its own kind and is never GC'd.
8. Measure on this machine (needs ccache installed first): time a cold verify-slot build, then a second slot building the same tree; record cold/warm times and `ccache -s` in `## Execution Summary`.

**Risks.**
- ccache+MSVC on `windows-2022` is the least-travelled path; if the run misses or configure objects, fall back to sccache there or leave Windows uncached, and say which in the Execution Summary. Qt 5 and Qt 6 builds never hit each other (different flags) — expected, not a bug.
- `CCACHE_CONFIGPATH` replaces the user-level ccache config for these builds only; anyone with their own `~/.config/ccache/ccache.conf` tunings can set `-DRELAY_COMPILER_CACHE=OFF`.
- Assumption to flag: 10 GiB cap under `~/.cache/relay/ccache`. Say if you want a different size or location before Run.

**Verify.** `python3 -m unittest tests.test_relay_build tests.test_scratch` (new cases); then for real — install ccache, `scripts/relay-build` twice and check the second run's `ccache -s` shows hits; two verify-slot builds in a row (or a slot build right after a checkout build of the same tree) mostly hits; CI green on all three workflows with cache stats in the logs and a cache hit on the second run of each.

## Execution Summary
Landed in `52ea9245` (code) and `282ceccf` (evidence: `docs/qa_evidence/2026-09-25-v52p-compiler-cache/measurements.md`). ccache 4.9.1 installed on this machine via apt.

**Cold vs warm, `relay` target, slot-shaped trees (`<slot>/src` + `<slot>/build`, no build type, like land.py), RELAY_JOBS 8:**
- cold (empty cache): 33 s, 0/189 hits.
- warm second slot, plan as written: 22 s, 174/189 hits (92%). The 15 misses were the heaviest window sources: every `AppPaths.h` includer carried `-DRELAY_SOURCE_DIR="<that tree>"` on its command line. The floor with every compile cached was 3.9 s, so those 15 misses cost about 17 s.
- warm second slot, as landed: **4.0 s, 190/191 hits (99.5%)**. The one miss is `src/SourceDir.cpp`, which is meant to miss.
- land.py's own verify build of `52ea9245` configured the launcher itself (`CCACHE_BASEDIR=/tmp/claude-1000/land/verify-slots/relay-terminal-1006c7a3-0`) and hit the cache.

**Deviations from the plan, and why:**
- `CCACHE_BASEDIR` is the common parent of the source and build dirs, not the source root. In a slot, `<slot>/build/relay_autogen/include` is on every AUTOMOC target's compile line and sits outside `<slot>/src`, so with a source-root base two slots never hash alike.
- The checkout's `build/` (RelWithDebInfo) and the verify slots (no build type) never share objects: their flags differ. Slot↔slot is where sharing happens. `build/` still caches against itself across header states.
- New `relay-sourcedir` library (`src/SourceDir.{h,cpp}`). `RELAY_SOURCE_DIR` is now only on that one TU. `AppPaths.h`, `Theme.cpp`, `RemotePane.cpp` and `RemoteShare.cpp` call `relaySourceDir()`. Side effect: `relay-highlight`'s copy of `Theme.cpp` used to see `"."`; it now sees the real source dir, which is also the last fallback.
- Added `-DRELAY_COMPILER_CACHE_TOOL=AUTO|ccache|sccache`. A cached tool path whose binary is gone is dropped and searched again, so uninstalling ccache cannot break every compile.
- Windows CI: a Visual Studio generator ignores `CMAKE_CXX_COMPILER_LAUNCHER`, so `windows.yml` uses ccache's MSBuild recipe: ccache 4.10.2 from GitHub releases, `ccache.exe` copied as `cl.exe`, named through `CMAKE_VS_GLOBALS`. If the download fails it builds uncached. **Not yet run on GitHub** (commits are local on `main`; pushing is the owner's call). This is the least-travelled path, and the first CI run is its test.
- 10G cap kept, as planned. Measured size after ~5 tree states: 0.1 GB (no -g in slots), so the budget in `relay-scratch check` is not at risk.
- Not done: `scripts/relay-build` against the shared `build/`. The checkout holds other sessions' uncommitted code, so I left it alone. The next `relay-build` regenerates from the new `CMakeLists.txt` and picks up the launcher the same way the verify slot did.

## Tests
- `python3 -m unittest tests.test_compiler_cache` → 7 OK: a fake ccache runs every compile with the shared dir, conf (`max_size = 10G`) and base (checkout layout → source root; slot layout → slot root); `RELAY_CCACHE_DIR`/`RELAY_CCACHE_MAX` override; `-DRELAY_COMPILER_CACHE=OFF` → no launcher; a command-line launcher wins; sccache when chosen; only `src/SourceDir.*` reads `RELAY_SOURCE_DIR`.
- `python3 -m unittest tests.test_scratch` → 16 OK (4 new: `compiler-cache` kind, never removed even at `--idle-hours 0`, counted against the budget, follows `XDG_CACHE_HOME`).
- Slot A (HEAD + this change), all targets built; `ctest -R 'theme|filepane|remotepane|remoteshare|settingspane|consolemode|projectpicker|filterpopup'` 9/9 and `-R '^board|^settings$|remotesettings'` 11/11.
- land.py's gate built the exact landed tree (`relay` target) in a verify slot through ccache.
- Pending: first GitHub run of `ci.yml` and `windows.yml`, which needs a push.
