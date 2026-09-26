# Building Relay

This is the canonical build map for people and repository agents. `AGENTS.md`, `CLAUDE.md` and
`RELAY.md` all point here so Codex, Claude and Relay use the same commands. The native workflows are
the executable specification when this document and a command ever disagree:

- Linux and the combined release: `.github/workflows/ci.yml` and `.github/workflows/release.yml`
- Windows: `.github/workflows/windows.yml`
- macOS: `.github/workflows/macos.yml`

For release policy, versioning, signing limitations and publication recovery, read
[`RELEASING.md`](RELEASING.md). This guide covers building and checking the artifacts.

## Developer build in the shared Linux checkout

Install the Ubuntu 24.04 dependencies:

```bash
sudo apt install build-essential cmake ninja-build python3 python3-cryptography libsecret-tools \
  qtbase5-dev libkf5syntaxhighlighting-dev qtpdf5-dev
```

Use the repository wrapper, especially when agents share one checkout:

```bash
scripts/relay-build
./build/relay --workspace /path/to/project
```

`scripts/relay-build` configures when needed, serializes concurrent builds and prevents a source
edit made during compilation from being hidden by a newer object file. Do not run `cmake --build`
directly in the shared checkout. To select Qt or build a test target:

```bash
./scripts/build.sh -DRELAY_QT_MAJOR=6       # full build and test suite, for release-scale testing
scripts/relay-build --target relay-engine-tests
ctest --test-dir build -R 'the-targeted-test' --output-on-failure
```

For a faster local edit loop, use the separate Ninja developer build:

```bash
scripts/relay-build --fast --target relay
./build-fast/relay --workspace /path/to/project
```

`--fast` uses `-O0 -g1` and leaves the normal `build/` configuration alone. Use the normal build
for performance checks, release preparation and full validation; the fast binary runs without
optimization. Both directories are ignored by Git and protected by separate build locks.

For a project that has separately activated Relay's parallel-development queue, development
work happens in its allocated source workspace. Build and test there with that project's normal
commands from `.relay/project.toml`; the publisher reruns the accepted verification gate on the
immutable submitted commit before moving `main`. A successful landing may then build and install
a separate runnable-main release under Relay's state root. Those verification and release builds
consume additional time and disk; `relay-land --repo /path/to/project main-status` reports the
installed SHA and lag. See [the operating contract](TREES-AND-LANDING.md#operating-a-configured-project)
and [migration guide](PARALLEL-DEVELOPMENT-MIGRATION.md). This shared Relay checkout still uses
`scripts/land.py begin`/`try`/`commit` until its own controlled cutover; do not activate it as
part of a developer build.

**Compiler cache.** Install `ccache` (`sudo apt install ccache`, or `scripts/relay-tooling-setup
--install`) and every configure — `build/`, `build-fast/`, each `land.py` verify slot — compiles
through one shared cache in `$XDG_CACHE_HOME/relay/ccache` (default `~/.cache/relay/ccache`, capped
at `RELAY_CCACHE_MAX`, default 10G; a configure never lowers a cap already in `ccache.conf`,
so `CCACHE_DIR=~/.cache/relay/ccache ccache -M 40G` sticks). `cmake/CompilerCache.cmake` sets it up: `ccache` first, then
`sccache` (`-DRELAY_COMPILER_CACHE_TOOL=` picks one), `-DRELAY_COMPILER_CACHE=OFF` turns it off,
and a launcher given on the command line wins. Check it with
`CCACHE_DIR=~/.cache/relay/ccache ccache -s`. Trees built with different flags never share objects:
`build/` is RelWithDebInfo, `build-fast/` is `-O0 -g1`, verify slots use no build type, so it is
the slots that share with each other.

Ubuntu 26.04 and Debian 13 use Qt 6 packages such as `qt6-base-dev` and
`libkf6syntaxhighlighting-dev`. KSyntaxHighlighting and Qt PDF are optional in source builds;
configure with `-DRELAY_REQUIRE_PDF_PREVIEW=ON` to fail rather than produce a build without the
in-app PDF viewer. A normal local install
uses `cmake --install build` and defaults to `~/.local` when configured through `scripts/build.sh`.

## Linux `.deb` packages

Docker builds the same three distribution packages as CI. The second argument is the Debian
version suffix; use an empty string for a final version.

```bash
package_dir=$(mktemp -d)
packaging/deb/docker-build-all.sh "$package_dir" '~beta.4'
```

This builds Ubuntu 24.04, Ubuntu 26.04 and Debian 13 packages for the host architecture, runs the
test suite, installs each package in a fresh container and runs its installed smoke test. CI uses
native amd64 and arm64 runners to produce the full six-package matrix. The packaged Linux build
includes the pinned libghostty-vt core; source builds default to libvterm unless configured with
`-DRELAY_ENGINE_WITH_GHOSTTY=ON` and a built Ghostty archive.
The `.deb` builds require Qt PDF at configure time, test the in-app viewer, and check that the
package declares its Qt PDF and PdfWidgets runtime libraries. Ubuntu 24.04 uses `qtpdf5-dev`;
Debian 13 and Ubuntu 26.04 use `qt6-pdf-dev`. The Arch/AUR recipes leave PDF support disabled:
Arch currently packages Qt PDF in the much larger `qt6-webengine`, so the on-demand install
path is tracked by #7WGJ. Until that path ships, Arch users can open PDFs externally.

## Native Windows x64 installer

Build on 64-bit Windows with Visual Studio 2022 C++ tools, CMake, Python 3.13, Qt 6.8.3 for
`msvc2022_64`, PowerShell 7 and Inno Setup 6. The packaging script downloads checksum-pinned private
Python and PowerShell runtimes; it does not use WSL or modify system runtimes.

In a PowerShell prompt with the Visual Studio build tools available:

```powershell
python -m pip install aqtinstall cryptography
python -m aqt install-qt windows desktop 6.8.3 win64_msvc2022_64 -O C:/Qt

cmake -S packaging/windows -B build-windows -A x64 `
  -DCMAKE_PREFIX_PATH=C:/Qt/6.8.3/msvc2022_64
cmake --build build-windows --config Release --parallel
ctest --test-dir build-windows -C Release --output-on-failure

cmake -S . -B build-app -A x64 -DRELAY_QT_MAJOR=6 -DBUILD_TESTING=OFF `
  -DCMAKE_PREFIX_PATH=C:/Qt/6.8.3/msvc2022_64
cmake --build build-app --config Release --parallel 4
$env:PATH = "C:/Qt/6.8.3/msvc2022_64/bin;$env:PATH"
./packaging/windows/package.ps1 -Version 0.1.0-beta.4
```

The installer is written to `dist/`. To reproduce the installed-app gate, silently install it to a
temporary per-user directory, run `packaging/windows/installed-smoke.py` against that directory,
then run its generated `unins000.exe`. The exact commands are in `windows.yml`.

## Native macOS disk image

Build on the architecture being packaged: Apple Silicon produces `macos_arm64.dmg`, and an Intel
Mac produces `macos_x64.dmg`. The minimum supported target is macOS 13. Install Xcode command-line
tools, CMake, Ninja, Python 3.13 and Qt 6.8.3. Homebrew is useful for CMake/Ninja but is not required
by the resulting app.

```bash
python3 -m pip install aqtinstall cryptography
qt_dir="${TMPDIR%/}/RelayQt"
python3 -m aqt install-qt mac desktop 6.8.3 clang_64 -O "$qt_dir"
export PATH="$qt_dir/6.8.3/macos/bin:$PATH"

cmake -S . -B build-macos -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$qt_dir/6.8.3/macos" \
  -DRELAY_QT_MAJOR=6 -DBUILD_TESTING=ON \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=13.0 -DCMAKE_OSX_ARCHITECTURES="$(uname -m)"
cmake --build build-macos --target relay relay-engine-tests --parallel 3
RELAY_ENGINE_TEST=PtyTest QT_QPA_PLATFORM=offscreen build-macos/engine/relay-engine-tests

/bin/bash packaging/macos/package.sh 0.1.0-beta.4 build-macos stage-macos
python3 packaging/macos/installed-smoke.py dist/*_macos_*.dmg macos-evidence
```

`package.sh` downloads checksum-pinned Python, builds the pinned patched GNU Bash, deploys Qt,
audits Mach-O linkage, ad-hoc signs the app and creates the DMG. `installed-smoke.py` mounts and
relocates the DMG, then checks private runtimes, HTTPS trust, Unicode shell integration, GUI
startup, file-opening IPC, clean shutdown and signatures after execution. See
[`../packaging/macos/README.md`](../packaging/macos/README.md) for bundle details. Local builds are
ad-hoc signed and are not Developer ID signed or notarized.

## Build with GitHub Actions

Use the native workflows for checks without publication:

```bash
gh workflow run windows.yml --ref main
gh workflow run macos.yml --ref main -f version=0.1.0-beta.4
```

All three platforms keep a ccache between runs with `actions/cache` and print `ccache
--show-stats` after the build: `ci.yml`'s Qt 5 job and `macos.yml` through
`-DCMAKE_CXX_COMPILER_LAUNCHER=ccache`, `windows.yml` through ccache's MSBuild recipe
(`ccache.exe` copied as `cl.exe`, named in `CMAKE_VS_GLOBALS`), because a Visual Studio generator
ignores compiler launchers. A Windows run whose ccache download fails builds uncached.

Windows uploads `windows-installer` and `windows-evidence`; macOS uploads an installer and evidence
artifact for each architecture. A versioned, publishable set must come from one successful combined
release run:

```bash
gh workflow run release.yml --ref main -f tag=v0.1.0-beta.4
```

That workflow builds the source archive, six Linux packages, the Windows installer and both Mac
DMGs from one immutable commit. It creates the tag and GitHub release only after every build,
test, installation and smoke gate passes. Publishing a release or deploying the website requires
owner authorization; building and inspecting artifacts does not.
