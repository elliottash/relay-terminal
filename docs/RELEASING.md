# Releasing Relay

How to cut a Linux beta (Beta 0 in `docs/DISTRIBUTION-RESEARCH.md`). Only the owner pushes
tags, publishes releases, updates the AUR and enables GitHub Pages.

## What gets built

| Channel | Built by | Files |
|---|---|---|
| Ubuntu 24.04 (Qt5/KF5) | `release.yml`, `ubuntu:24.04` container | `relay_<version>_ubuntu24.04_{amd64,arm64}.deb` |
| Debian 13 (Qt6/KF6) | `release.yml`, `debian:trixie` container | `relay_<version>_debian13_{amd64,arm64}.deb` |
| Ubuntu 26.04 (Qt6/KF6) | `release.yml`, `ubuntu:26.04` container | `relay_<version>_ubuntu26.04_{amd64,arm64}.deb` |
| Source | `release.yml`, `git archive` | `relay-<version>.tar.gz` |
| Checksums | `release.yml` | `SHA256SUMS` |
| Arch (AUR) | the owner, or the disabled `aur` job | `packaging/arch/relay-terminal`, `packaging/arch/relay-terminal-git` |
| Website | `pages.yml` (inactive until enabled) | `site/` |

Packaging pieces:

- `CMakeLists.txt` `install()` rules: `bin/relay`, `share/relay/{backend,shell,scripts,theme}`,
  icons, desktop file, `share/metainfo/org.relayterminal.Relay.metainfo.xml`,
  `share/doc/relay/{README.md,copyright}`.
- `packaging/cpack.cmake`: CPack DEB settings. `dpkg-shlibdeps` computes library dependencies;
  the file adds `python3 (>= 3.10)` and `bash`, and recommends `libsecret-tools` and
  `xdg-utils`.
- `packaging/deb/build-deb.sh`: installs build deps, builds out of tree, runs ctest, runs cpack.
  It first builds the **libghostty-vt core into the package** (2026-09-19): it downloads the
  Zig release pinned in `engine/scripts/zig.sha256` (`engine/scripts/install-zig.sh` verifies
  the checksum), runs `engine/scripts/build-libghostty-vt.sh` at the pinned ghostty commit with
  `-Dcpu=baseline`, configures with `-DRELAY_ENGINE_WITH_GHOSTTY=ON`, and checks the binary
  carries the core. The archive is linked statically, so the `.deb` has no new dependency;
  the packaged Relay defaults to the ghostty core (`--engine-core=libvterm` still picks the
  other). A failed download or Zig build fails the package build on purpose;
  `RELAY_WITH_GHOSTTY=0` is the deliberate libvterm-only package. `RELAY_CACHE_DIR` keeps the
  tarball and the built archive between runs: `docker-build-all.sh` mounts `OUT_DIR/cache`,
  and `release.yml`/`ci.yml` cache it with `actions/cache` keyed on the two pin files.
- `packaging/deb/smoke-test.sh` + `packaging/smoke-installed.sh`: installs a `.deb` with apt
  in a fresh container and checks the installed files, `relay --version/--help`, that the
  binary carries the libghostty-vt core, the backend
  worker's `ready` event, and a 10-second GUI start under Xvfb
  (offscreen and xcb) that must spawn the Bash integration shell and the agent worker.
- `packaging/deb/docker-build-all.sh [OUT] [SUFFIX] [IMAGE...]`: the same as CI, locally.

Why CPack rather than a `debian/` directory: one CMake install manifest serves `cmake --install`,
the `.deb`s, the PKGBUILD and a future Flatpak, and a per-distro `.deb` needs only a container.
A `debian/` source package becomes worthwhile for a Launchpad PPA or a Debian upload; add it then.

## Version scheme

| Where | Beta | Final |
|---|---|---|
| Git tag | `v0.1.0-beta.1` | `v0.1.0` |
| `project(Relay VERSION …)` | `0.1.0` | `0.1.0` |
| `.deb` Version | `0.1.0~beta.1-1~ubuntu24.04` | `0.1.0-1~ubuntu24.04` |
| Release asset | `relay_0.1.0-beta.1_ubuntu24.04_amd64.deb` | `relay_0.1.0_ubuntu24.04_amd64.deb` |
| AUR `pkgver` | `0.1.0beta.1` | `0.1.0` |

`~` sorts before the final version in dpkg, and `0.1.0beta.1` sorts before `0.1.0` in pacman,
so users upgrade cleanly from beta to final. Tags containing `-` become GitHub pre-releases.

## Pre-release checklist

1. **Tests green**: CI on `main` passes (`ci.yml`: Ubuntu 24.04 Qt5 build and tests, Debian 13
   Qt6 build, tests and `.deb`). Locally: `./scripts/build.sh`.
2. **QA lane**: every feature in `issues/features/needs_qa_llm/` meant for this release has a
   non-Claude QA session recorded under `docs/qa_evidence/` and the issue moved to `done/`.
   Items still waiting on QA are either QA'd now or listed as known issues in the release notes.
   Features not ready are held back, not shipped unchecked.
3. **Packages locally** (about 15 minutes with Docker):
   `packaging/deb/docker-build-all.sh /tmp/relay-packages "~beta.1"`
   All three distributions must print `PASS`.
4. **Manual smoke on a real desktop** for at least Ubuntu 24.04 and one KF6 distro (VM is fine):
   install the `.deb`, start from the application menu, check the icon, run a shell command,
   run an agent request with a real key, save a key in the keyring, split a pane, `relay open .`,
   quit and relaunch.
5. **Privacy text is still true**: `site/index.html` "Your key, your provider" and the README
   (no telemetry; keys in the keyring or env; prompts and tool results go to the provider;
   agent tools run without approval unless that changed).
6. **Metadata**: add a `<release>` entry to `packaging/org.relayterminal.Relay.metainfo.xml` and run
   `appstreamcli validate --no-net packaging/org.relayterminal.Relay.metainfo.xml`.
7. **Known limitations** drafted for the release notes (Bash-only rich prompt; zsh/fish/tmux/SSH
   use native input; Linux only; no PDF preview in Qt6 packages until the QPdfView fix lands).

## Cutting a beta

1. **Bump versions** (for a new base version; a new beta of the same base skips this):
   - `CMakeLists.txt`: `project(Relay VERSION X.Y.Z …)`. The release workflow fails if the tag
     does not match.
   - `backend/relay_core/__init__.py`: `__version__` (the app and worker read their version from CMake and this; `tests/test_version.py` catches drift).
   - `packaging/org.relayterminal.Relay.metainfo.xml`: new `<release version="X.Y.Z~beta.N" date=…>`.
   - `site/index.html`: the `v=` lines and the note under Install.
   - `packaging/arch/relay-terminal/PKGBUILD`: `_tag`, `pkgver`, `pkgrel=1`.
2. Commit, push `main`, and wait for CI to pass.
3. **Tag and push**:
   ```bash
   git tag -a v0.1.0-beta.1 -m "Relay 0.1.0 beta 1"
   git push origin v0.1.0-beta.1
   ```
4. **What CI does** (`.github/workflows/release.yml`):
   - `source`: checks the tag against `CMakeLists.txt`, builds `relay-0.1.0-beta.1.tar.gz`.
   - `deb` (6 jobs: 3 distributions x amd64/arm64): restores the Zig/libghostty-vt cache,
     builds each `.deb` in its container with
     the checkout mounted read-only (Zig and the ghostty clone come from the network), runs
     ctest, installs the `.deb` in a fresh container and
     runs the smoke test. Logs are uploaded as artifacts even on failure.
   - `release`: collects the `.deb`s and tarball, writes `SHA256SUMS`, and runs
     `gh release create --generate-notes --verify-tag` (`--prerelease` for tags with `-`).
   - `aur`: disabled (see below).
   The arm64 jobs use GitHub's `ubuntu-24.04-arm` runners; if they are unavailable for the
   repository, delete the `arm64` matrix entry.
5. **Edit the release notes** on GitHub: add install commands, known limitations, the privacy
   summary, and the QA status. Keep it a pre-release.
6. **Download and verify** one `.deb` from the release on a clean VM:
   `sha256sum --check --ignore-missing SHA256SUMS`, `sudo apt install ./relay_…deb`, launch.

If a tag build fails, fix on `main`, delete the tag locally and remotely
(`git push --delete origin v0.1.0-beta.1`), delete the draft release if one was created, and
re-tag. Never re-use a tag that users may have downloaded; bump to `beta.2` instead.

## AUR

First time (owner):

1. Create an AUR account and add an SSH public key.
2. `git clone ssh://aur@aur.archlinux.org/relay-terminal.git` (an empty repo creates the package).
3. Copy `packaging/arch/relay-terminal/PKGBUILD` in, then in an Arch environment:
   `updpkgsums && makepkg --printsrcinfo > .SRCINFO && makepkg -sf` (builds and runs `check()`).
4. `git add PKGBUILD .SRCINFO && git commit -m "Initial import" && git push`.
5. Same for `relay-terminal-git` from `packaging/arch/relay-terminal-git`.

Each release: update `_tag` and `pkgver`, `pkgrel=1`, `updpkgsums`, regenerate `.SRCINFO`,
commit, push. `relay-terminal-git` needs no update per release.

Automating it: add the AUR private key as the repository secret `AUR_SSH_PRIVATE_KEY`, then
in `release.yml` remove `false &&` from the `aur` job's `if:`.

The package is named `relay-terminal` because `relay` is generic and other AUR packages
(for example `sentry-relay`) install a `/usr/bin/relay`.

The AUR packages build the **libvterm core only** (unlike the `.deb`s): the AUR forbids
network access in `build()`, and ghostty's `zig build` fetches its Zig package dependencies
(`build.zig.zon`) over the network. Shipping the ghostty core there means declaring the ghostty
tarball at the pinned commit and every Zig dependency it needs as `source=()` entries with
checksums, building with `zig build --system`, and pinning `zig` to 0.16 in `makedepends`;
that is its own change.

## Website (GitHub Pages)

`site/` is a static page (HTML + CSS, no JavaScript). Preview with
`python3 -m http.server -d site 8000`.

To publish:

1. Repository **Settings > Pages > Build and deployment > Source: GitHub Actions**.
2. **Settings > Secrets and variables > Actions > Variables**: add `RELAY_PAGES_ENABLED` = `true`.
3. Run the **Pages** workflow (Actions tab > Pages > Run workflow), or push a change under `site/`.
4. The site is at `https://elliottash.github.io/relay-terminal/`. For a custom domain, add
   `site/CNAME` and configure DNS.

The repository must be public for Pages on a free plan, and for the release downloads and
install commands on the site to work for anyone.

## Hosted rendezvous (join.relay-terminal.ai)

The rendezvous and the phone's web app are one service on elliott-main-1
(`relay-rendezvous.service`, code in `/opt/relay-rendezvous`, database in
`/var/lib/relay-rendezvous`, a Cloudflare Tunnel straight to `127.0.0.1:8791`). It is not part of
a Relay release: it is deployed **whenever anything under `app/`, `remote/` or `rendezvous/`
changes on `main`**, because a phone loads its client from there and talks the protocol version
the desktop it pairs with speaks. Leaving it behind is invisible — the app keeps loading, just
the old one.

```
rendezvous/deploy.sh -n            # what rsync would change, then stop
rendezvous/deploy.sh               # export main, rsync, restart, verify
rendezvous/deploy.sh --check       # verify only: is what is served exactly main?
rendezvous/deploy.sh --rollback    # put /opt/relay-rendezvous.prev back and restart
```

It deploys a **clean export of a revision** (`main` by default, or any rev given as an argument),
never this checkout's working tree, which usually holds another session's half-finished edit. It
keeps the previous tree in `/opt/relay-rendezvous.prev`, waits for `/v1/health` on the box,
checks `https://join.relay-terminal.ai/v1/health` from here, and compares the sha256 of every
served `app/` file with the export; if the service does not come back it prints the journal and
rolls back by itself. `--check` is the same comparison with no deploy, and is what to run after
anyone else has touched the box.

A restart drops the live sockets through the rendezvous: phones reconnect, but a share in flight
does not survive it. `/v1/health` reports the attached desktop and channel counts — look before
deploying. If `main`'s `relay-rendezvous.service` differs from the unit installed on the box the
script prints the diff; `--install-unit` copies it over and reloads systemd.

The app's icons (`app/icons/*.png`) are rendered from `app/icon.svg`, so the phone's Home Screen
wears the same mark as the desktop. iOS takes the Home-Screen image only from
`<link rel="apple-touch-icon">` and only as a PNG, and only an installed PWA gets Web Push, so
`tests/test_web_manifest.py` guards both. After editing the SVG, re-render them:

```
rsvg-convert -w 192 -h 192 app/icon.svg -o app/icons/icon-192.png
rsvg-convert -w 512 -h 512 app/icon.svg -o app/icons/icon-512.png
# maskable-512.png: the mark at 410px centred on a 512px square of #0e0f12 (the theme colour),
# which keeps it inside the 80% safe zone a circular crop leaves.
# apple-touch-icon.png: the mark at 180px on the same colour, opaque and full bleed — iOS
# rounds the corners itself and draws nothing behind it.
```

## Before the first public beta (owner)

- Replace the screenshots in `site/assets/` (taken from QA evidence) with clean ones: they show
  scratchpad paths and a username/hostname.
- Decide the maintainer identity shown in `.deb` metadata (`CPACK_PACKAGE_CONTACT` in
  `packaging/cpack.cmake`), `packaging/copyright` and the PKGBUILDs.
- Make the repository public (or accept that downloads need GitHub access).
