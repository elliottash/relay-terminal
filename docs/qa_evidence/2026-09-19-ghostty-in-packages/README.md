# The .debs ship the libghostty-vt core (2026-09-19)

Owner decision: the packaged builds carry the ghostty emulator core, so the default core in the
`.deb` is ghostty rather than libvterm — 16x the libvterm throughput (756 vs 46 MiB/s below),
the 1.2 s for 200 MB of `docs/ENGINE-PERF.md`, in the package rather than a source-build option.
This folder is the local proof that the packaging path does that, run on the owner's DGX Spark
(aarch64, Docker, no Zig installed on the host), from a clean export of `main` at `59c19b7` plus
the packaging change under test.

## What changed

- `engine/scripts/zig.sha256` (new): the pinned Zig release, `0.16.0`, with the sha256 of the
  x86_64 and aarch64 Linux tarballs from https://ziglang.org/download/index.json
  (`70e49664…` and `ea4b09bf…`).
- `engine/scripts/install-zig.sh` (new): downloads that tarball for `uname -m` from
  ziglang.org, verifies it against `zig.sha256` (every time, cached or not), unpacks it, prints
  the `zig` path. A tarball that does not match is removed and the script exits 1.
- `engine/scripts/build-libghostty-vt.sh`: honours `RELAY_GHOSTTY_ZIG_CPU` (`-Dcpu=`; the
  packages use `baseline`, developers keep `native`) and `GHOSTTY_SRC_DIR`; records what it
  built in `PREFIX/.relay-libghostty-vt` and skips the clone and build when the prefix already
  holds this commit, this Zig and this CPU.
- `packaging/deb/build-deb.sh`: installs `curl xz-utils`, runs the two scripts into
  `RELAY_CACHE_DIR/ghostty-vt/<distro><version>-<arch>`, configures with
  `-DRELAY_ENGINE_WITH_GHOSTTY=ON -DRELAY_GHOSTTY_VT_PREFIX=…`, and after the build greps the
  binary for `Relay(libghostty-vt)` (the XTVERSION reply that exists only in
  `GhosttyCore.cpp`). Any failure in that chain fails the package build; `RELAY_WITH_GHOSTTY=0`
  is the only way to get a libvterm-only package. `RELAY_BUILD_DIR` keeps the build tree.
- `packaging/deb/docker-build-all.sh`: mounts `OUT_DIR/cache` as `RELAY_CACHE_DIR`.
- `packaging/smoke-installed.sh`: a new step fails the smoke test when the installed
  `/usr/bin/relay` lacks the marker string (a `--engine-core=ghostty` start proves nothing:
  `TerminalSession` falls back to the first available core silently).
- `.github/workflows/release.yml`, `ci.yml`: `actions/cache` on that cache directory, keyed on
  `hashFiles('engine/scripts/zig.sha256', 'engine/scripts/build-libghostty-vt.sh')`, image
  and arch.
- `docs/ENGINE.md` ("Zig in the toolchain", item 5 of the plan, the build snippet) and
  `docs/RELEASING.md` (packaging pieces, what CI does, the AUR paragraph).

Not changed: `packaging/arch/*/PKGBUILD`. The AUR forbids network access in `build()`, and
ghostty's `zig build` fetches its Zig package dependencies (`build.zig.zon`, ~30 entries, a
subset needed for `-Demit-lib-vt`) over the network. Doing it properly means declaring the
ghostty tarball at the pinned commit and each needed dependency as `source=()` entries with
checksums, building with `zig build --system`, and pinning `zig` to 0.16 in `makedepends`
(Arch's `zig` is 0.16.0-1 today, but tracks upstream). That is a separate change; the AUR
packages keep the libvterm core and `docs/RELEASING.md` says so.

## Runs

### Run 1 — cold path: Zig download, clone, Zig build (`run1-before-113b4ab-debian-trixie.excerpt.log`)

`packaging/deb/docker-build-all.sh OUT '' debian:trixie ubuntu:24.04` from an export of
`d0e1628`, empty cache. Both images went through the whole chain:

```
== libghostty-vt core (Zig from engine/scripts/zig.sha256, cache: /cache)
install-zig.sh: downloading https://ziglang.org/download/0.16.0/zig-aarch64-linux-0.16.0.tar.xz
install-zig.sh: Zig 0.16.0 in /tmp/tmp.KlFXpfz50C/zig
Cloning into '/tmp/tmp.KlFXpfz50C/ghostty-src'...
libghostty-vt f9a3f24a56bf05f70894e1a084809d4fffadf420 installed in /cache/ghostty-vt/debian13-aarch64 (ghostty=f9a3f24a56bf05f70894e1a084809d4fffadf420 zig=0.16.0 cpu=baseline)
-- Relay engine: libghostty-vt core enabled (/cache/ghostty-vt/debian13-aarch64/lib/libghostty-vt.a)
[31/502] Localizing non-API symbols of libghostty-vt
== relay carries the libghostty-vt core (default core: ghostty)
```

Download plus clone plus `zig build -Demit-lib-vt -Doptimize=ReleaseFast -Dcpu=baseline` took
about one minute on this machine (20 cores). The archive is 18.9 MB, exports 204 `ghostty_*`
symbols, and the prefix (include, lib, stamp) is 28 MB in the cache.

That run then failed ctest, for reasons outside this change and since fixed on `main`:
`CoreTest::theRowRoleOscMarksItsLine(ghostty)` and
`ViewTest::aUserRowWearsItsRoleFromTheSchemeInForce(ghostty)` failed — the row-role OSC had been
implemented for libvterm only, and nobody had compiled the ghostty core since (this machine had
no Zig); `113b4ab` ("GhosttyCore parses the row-role OSC") landed while the build ran. `paneusage`
failed at `d0e1628` for an unrelated in-flight string change (fixed by `82ea0b4`). On the ctest
retry, `relay-engine-tests` hung in `ViewTest` with QTest printing
`Received signal 1 (SIGHUP) sent by PID 21061 UID 0` millions of times until the 120 s timeout
(3.7 GB of log). That happened once, on the libvterm data row after a first run that had
already failed, and not in the three later runs of the same suite here; it is an engine-test
flake in containers, noted for the engine's owner, not something this change touches.

### Run 2 — the real thing, from `59c19b7` (`docker-build-all.log`, `build-*.excerpt.log`, `smoke-*.excerpt.log`)

Same command, same cache. Both distributions **PASS** end to end (build, ctest 63/63, cpack,
install in a fresh container, smoke test):

```
=== build debian:trixie
=== smoke debian:trixie (relay_0.1.0-1~debian13_arm64.deb)
    PASS
=== build ubuntu:24.04
=== smoke ubuntu:24.04 (relay_0.1.0-1~ubuntu24.04_arm64.deb)
    PASS

debian:trixie    PASS
ubuntu:24.04     PASS
```

The cache-hit path is what ran (`install-zig.sh` verified the cached tarball and unpacked it;
the archive was reused):

```
install-zig.sh: Zig 0.16.0 in /tmp/tmp.Kle6YYFjyo/zig
libghostty-vt f9a3f24a56bf05f70894e1a084809d4fffadf420 already in /cache/ghostty-vt/debian13-aarch64 (ghostty=f9a3f24a56bf05f70894e1a084809d4fffadf420 zig=0.16.0 cpu=baseline)
-- Relay engine: libghostty-vt core enabled (/cache/ghostty-vt/debian13-aarch64/lib/libghostty-vt.a)
[226/502] Building CXX object engine/CMakeFiles/relay-terminal-engine.dir/core/GhosttyCore.cpp.o
== relay carries the libghostty-vt core (default core: ghostty)
63/63 Test #63: relay-engine-tests ...............   Passed   20.08 sec
100% tests passed, 0 tests failed out of 63
CPack: - package: /tmp/tmp.Kle6YYFjyo/relay_0.1.0-1~debian13_arm64.deb generated.
```

(`backend-and-bash` failed once on Debian with an asyncio timeout in
`test_remote_meetcode` and passed on the built-in retry; unrelated.)

The smoke test in a fresh container, new step included:

```
== emulator core: the package ships libghostty-vt
ok /usr/bin/relay carries the libghostty-vt core (default core: ghostty)
== GUI start under Xvfb (offscreen and xcb)
[offscreen] exit status 124 (124 = still running at timeout)
[xcb] exit status 124 (124 = still running at timeout)
smoke test passed
```

The GUI start spawns Relay's Bash integration shell through the engine with no `--engine-core`,
that is, on the default core, which `VtCoreFactory::availableVtCores()` lists as ghostty first
when `RELAY_HAVE_GHOSTTY` is defined.

### The packages (`debian13-deb-inspection.txt`, `ubuntu24.04-deb-inspection.txt`)

No new runtime dependency. `Depends` is what it was: Python, Bash, libc, libgcc, libstdc++,
Qt and KF SyntaxHighlighting (dpkg-shlibdeps output; Qt5 + QtPdf on Ubuntu 24.04, Qt6 on
Debian 13). `readelf -d` on the installed binary lists only those libraries; no `.so` or `.a`
is shipped (the archive is linked in, its non-API symbols made local by the partial link in
`engine/CMakeLists.txt`); `nm -D --undefined-only` has no `zig`, `ghostty` or `compiler_rt`
symbol; the binary is a stripped PIE of 9.0 MB (Debian 13) / 8.8 MB (Ubuntu 24.04).

```
$ dpkg-deb -f relay_0.1.0-1~debian13_arm64.deb Depends
Depends: python3 (>= 3.10), python3-cryptography, bash, libc6 (>= 2.36), libgcc-s1 (>= 3.0), libkf6syntaxhighlighting6 (>= 6.0.0), libqt6core6t64 (>= 6.8.2), libqt6gui6 (>= 6.8.2), libqt6network6 (>= 6.1.2), libqt6widgets6 (>= 6.3.0), libstdc++6 (>= 14)
$ readelf -d usr/bin/relay | grep NEEDED     # Debian 13
libKF6SyntaxHighlighting.so.6 libQt6Network.so.6 libQt6Widgets.so.6 libQt6Gui.so.6 libQt6Core.so.6 libstdc++.so.6 libm.so.6 libgcc_s.so.1 libc.so.6
$ strings -n 8 usr/bin/relay | grep -x 'Relay(libghostty-vt)'
Relay(libghostty-vt)
$ strings -e l usr/bin/relay | grep -xE 'ghostty|libvterm'      # the core names VtCoreFactory registers
ghostty
libvterm
```

### Deeper checks on the cached archive (`deep-checks-ubuntu24.04.log`)

One more `ubuntu:24.04` container, same cache:

1. **A libvterm-only package for comparison** (`RELAY_WITH_GHOSTTY=0`, the deliberate path):
   its `relay` has exactly the same `NEEDED` list as the ghostty one
   (`libKF5SyntaxHighlighting.so.5 libQt5PdfWidgets.so.5 libQt5Pdf.so.5 libQt5Network.so.5
   libQt5Widgets.so.5 libQt5Gui.so.5 libQt5Core.so.5 libstdc++.so.6 libm.so.6 libgcc_s.so.1
   libc.so.6 ld-linux-aarch64.so.1`), and no `Relay(libghostty-vt)` string. So the ghostty
   core adds no shared-library dependency, by direct comparison, not by inspection alone.
2. **`relay-engine-tests` against the cached, baseline-CPU archive** (`-DRELAY_BUILD_ENGINE=ON`
   on the same export): all 56 `CoreTest` rows pass, 28 of them `(ghostty)`, including
   `theRowRoleOscMarksItsLine(ghostty)`; every suite passes (56 / 17 / 18 / 14 / 10 / 16 / 74
   passed, 0 failed).
3. **Throughput of the shipped archive** (`-Dcpu=baseline`, not `native`): `relay-engine-bench`
   on a 209 800 000-byte `big200.txt` (99-character base64 lines), two runs each:

   ```
   core=ghostty  bytes=209800000 ms=264 MiBps=756.5 maxrss_kb=225140
   core=ghostty  bytes=209800000 ms=263 MiBps=760.6 maxrss_kb=224948
   core=libvterm bytes=209800000 ms=4357 MiBps=45.9 maxrss_kb=234840
   core=libvterm bytes=209800000 ms=4356 MiBps=45.9 maxrss_kb=234844
   ```

   756-761 MiB/s is the same figure `docs/ENGINE-PERF.md` recorded for the native build
   (755.6 / 756.7 / 750.2), so `baseline` costs nothing measurable here (simdutf and Highway
   dispatch at run time). Cortex-X925/A725, 20 cores.

### The installer on its own

`engine/scripts/install-zig.sh` on the host (no Docker): fresh download in 33 s and
`zig version` prints `0.16.0`; a second run with the cached tarball and unpacked destination
prints the path and does nothing else; a cached tarball with one byte appended is rejected
(`sha256sum: WARNING: 1 computed checksum did NOT match`), removed, exit 1.

## What remains for CI to prove

- The amd64 path: this machine is aarch64, so the `x86_64` line of `zig.sha256` was checked
  only against ziglang.org's `index.json`, not by a download here. The first tag build's
  `ubuntu-24.04` runners exercise it; `ubuntu-24.04-arm` runners repeat what was done here.
- `actions/cache` restore and save of `out/cache` (root-owned files from the container; they
  are world-readable, which is what the tar needs).
- Ubuntu 26.04 was not built here (same code path as the two that were; Qt6 like Debian 13).
