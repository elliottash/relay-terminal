# Distribution research: beta packaging, macOS/Windows, website

Checked against the web on 2026-09-17. **Unverified** = not confirmed from a primary source.

Relay facts that drive the choices (`CMakeLists.txt`, `docs/ARCHITECTURE.md`, `keystore.py`):

- Runtime deps: Qt5.15 **or** Qt6.4+, KF Parts + CoreAddons, Qt DBus, the **installed**
  `konsolepart` plugin (loaded at runtime, not linked), Python >= 3.10 (backend is stdlib-only),
  Bash. Optional: KSyntaxHighlighting, QtPdf. Keys via env var or `secret-tool` (libsecret).
- CMake hard-fails on non-Linux when `RELAY_BUILD_APP=ON`. The app reads `/proc/self/exe` and
  `/proc/<pid>/fd/0` / `/proc/<pid>/stat`. Bash integration uses private files (rcfile, event,
  input) that shell and frontend must both see. GPL-3.0; README says "no telemetry".

## 1. Linux beta packaging

### Distro coverage of KF6 + KonsolePart today

| Distro | KDE stack | Relay build |
|---|---|---|
| Ubuntu 24.04 LTS | Plasma 5.27, KF 5.115, Qt 5.15; `konsole-kpart` 23.08.5 ([Kubuntu](https://kubuntu.org/news/kubuntu-24-04-lts-noble-numbat-released/), [packages.ubuntu.com](https://packages.ubuntu.com/search?keywords=konsole-kpart&searchon=names&suite=all&section=all)) | Qt5/KF5 path |
| Ubuntu 25.10 / 26.04 LTS | 26.04: Plasma 6.6, KF 6.24, Gear 25.12.3 ([Kubuntu 26.04 notes](https://kubuntu.org/news/kubuntu-26-04-release-notes/)) | Qt6/KF6 path |
| Debian 13 trixie | Plasma 6.3.x, KF 6.13, `konsole-kpart` 4:25.04.2 ([Debian](https://www.debian.org/releases/trixie/release-notes/whats-new.en.html), [linuxiac](https://linuxiac.com/debian-13-to-ship-with-kde-plasma-6-3-6-desktop-environment/)) | Qt6/KF6 |
| Fedora 43 / 44 / 45 | `konsole-part` 26.04.3 / 26.08.1 ([packages.fedoraproject.org](https://packages.fedoraproject.org/pkgs/konsole/konsole-part/)) | Qt6/KF6 |
| Arch | `konsole` 26.08.1 in `extra`, part included, deps `kparts kpty qt6-base ...` ([archlinux.org](https://archlinux.org/packages/extra/x86_64/konsole/)) | Qt6/KF6 |

Relay builds against Qt5/KF5 or Qt6/KF6, so every current mainstream distro can get **native
packages depending on the distro's KonsolePart**: cheapest path, no bundling, no sandbox.

### AUR (Arch)

- `PKGBUILD` with `depends=(konsole qt6-base kparts kcoreaddons python bash)`,
  `optdepends=(syntax-highlighting qt6-webengine libsecret)` (QtPdf ships in `qt6-webengine` on
  Arch, **unverified**), `makedepends=(cmake extra-cmake-modules)`. Also a `relay-git` VCS package.
- Effort: ~0.5 day. Upload is self-service via SSH key; no review. Updates = bump `pkgver`.

### .deb for Debian/Ubuntu (CI-built) and Launchpad PPA

- CI containers per target (`ubuntu:24.04` KF5, `ubuntu:26.04`/`debian:13` KF6); `Depends:
  konsole-kpart, python3 (>= 3.10)` + `dh_shlibdeps`; `Recommends: libsecret-tools`. CPack DEB or
  `dpkg-buildpackage`; attach to GitHub Releases.
- A PPA ([help.launchpad.net/Packaging/PPA](https://help.launchpad.net/Packaging/PPA)) gives
  `apt upgrade` updates but needs a signed source package, Ubuntu only. Effort: 1-2 days debs, +1-2 PPA.

### Flatpak

- `org.kde.Platform` (current branches 6.10/6.11) **includes** KParts, KPty, KSyntaxHighlighting,
  CoreAddons, KIO etc. but **not** Konsole: the SDK module list in
  [flatpak-kde-runtime `org.kde.Sdk.json.in`](https://invent.kde.org/packaging/flatpak-kde-runtime/-/blob/qt6.11/org.kde.Sdk.json.in)
  has `kparts`, `kpty`, `syntax-highlighting` but no `konsole`. QtPdf is part of QtWebEngine,
  which is not in the runtime (it is normally pulled in via `io.qt.qtwebengine.BaseApp`,
  **unverified for current versions**). Python 3 is in the freedesktop base runtime
  (**unverified** for the Platform vs only the Sdk; check `flatpak run --command=python3 org.kde.Platform//6.11`).
- Precedent: Yakuake's Flathub manifest builds Konsole from the release tarball as a module
  (`cmake-ninja`, `-DINSTALL_ICONS=OFF`) on `org.kde.Platform` 6.10 with
  `--talk-name=org.freedesktop.Flatpak` ([flathub/org.kde.yakuake](https://github.com/flathub/org.kde.yakuake),
  [PR #62](https://github.com/flathub/org.kde.yakuake/pull/62)). Konsole's own manifest uses
  `--device=all` and the same talk-name ([konsole `.flatpak-manifest.json`](https://invent.kde.org/utilities/konsole/-/blob/master/.flatpak-manifest.json)).
- KonsolePart already knows about Flatpak: `Pty.cpp` spawns the shell with `flatpak-spawn --host`
  and `Session.cpp` uses `flatpakSpawnProcessId()` when `KSandbox::isFlatpak()` (Konsole source).
  So the terminal itself works; **Relay's own assumptions break**:
  - `/proc/<pid>/fd/0` and `/proc/<pid>/stat` of the host Bash are invisible (separate PID
    namespace). tty-mode and foreground-pgrp checks need a host-side helper (e.g. run via
    `flatpak-spawn --host`) or a shell-reported state instead.
  - The sandbox `/tmp` is private. The rcfile, event file and input file must live in a path
    visible on both sides, e.g. `$XDG_RUNTIME_DIR/app/$FLATPAK_ID` (**unverified** that it is
    host-visible at the same path; confirm).
  - Agent tools (Python worker subprocesses) would run *inside* the sandbox unless wrapped with
    `flatpak-spawn --host`; users will expect host tools (git, compilers).
  - `secret-tool` is not in the runtime (**unverified**); use libsecret over D-Bus with
    `--talk-name=org.freedesktop.secrets` or the Secret portal.
  - `--talk-name=org.freedesktop.Flatpak` is a full sandbox escape; Flathub reviewers accept
    it for terminals/IDEs (Konsole, Yakuake, VS Code) but it must be justified
    ([Flatpak sandbox permissions](https://docs.flatpak.org/en/latest/sandbox-permissions.html),
    [flatpak-spawn(1)](https://man7.org/linux/man-pages/man1/flatpak-spawn.1.html)).
- Verdict: good follow-up for reach (immutable distros, Steam Deck), but needs ~1-2 weeks of
  Relay changes to abstract "host process introspection" and shared-state paths.

### AppImage

- Must bundle Qt6, KF6 Parts and its whole dependency tree, **plus** `konsolepart.so` in
  `plugins/kf6/parts/`, KonsolePart data (keyboard layouts, colour schemes), Qt plugins,
  and ideally a Python (or rely on host `python3`, which most distros ship).
- Tools: [linuxdeploy](https://docs.appimage.org/packaging-guide/from-source/linuxdeploy-user-guide.html) +
  [linuxdeploy-plugin-qt](https://github.com/linuxdeploy/linuxdeploy-plugin-qt) (supports Qt6).
  The Qt plugin does not know about KParts plugins; they must be copied and their deps deployed
  manually (**unverified** beyond the README). KDE Craft can also emit AppImages
  ([Craft wiki](https://community.kde.org/Craft)).
- glibc baseline: build on the oldest distro you support; binaries run on newer but not older
  glibc ([AppImage best practices](https://docs.appimage.org/reference/best-practices.html)).
  Practically: build on Ubuntu 22.04/24.04 with Qt6 from aqtinstall or Craft, since those
  distros lack KF6.
- Relay's `XDG_CONFIG_DIRS` profile trick adds fragility. Effort 1-2 weeks; below Flatpak.

### Snap

- Terminals need **classic** confinement; "terminal emulators, multiplexers and shells" are a
  recognised category, requested on the forum `store-requests` category, with security-team
  review and publisher vetting; reviews "should start within two weeks"
  ([Snapcraft docs](https://snapcraft.io/docs/reviewing-classic-confinement-snaps),
  [forum process](https://forum.snapcraft.io/t/process-for-reviewing-classic-confinement-snaps/1460)).
- Classic snaps see host `/proc` and `/tmp`; KF6 must be bundled (`kde-neon-6` extension,
  **unverified**). Mostly redundant with the `.deb`s. Defer.

### Recommendation for Beta 0

AUR (`relay` + `relay-git`), CI-built `.deb` for Ubuntu 24.04 (KF5), Ubuntu 26.04 and Debian 13
(KF6), and documented source build (Fedora users: `dnf install konsole-part kf6-kparts-devel ...`,
optional COPR later). Flatpak next, after the `/proc` and shared-path abstraction.

## 2. macOS and Windows

### What actually blocks today

- **Not Konsole itself.** Konsole builds for Windows and macOS: KDE CI publishes nightly
  `konsole-master-*-windows-cl-msvc2022-x86_64.exe` and `macos-arm64`/`macos-x86_64` builds
  ([cdn.kde.org/ci-builds/utilities/konsole/master](https://cdn.kde.org/ci-builds/utilities/konsole/master/)).
  The Windows port (2023, lets Kate embed KonsolePart on Windows,
  [Phoronix](https://www.phoronix.com/news/KDE-Konsole-On-Windows)) uses a vendored ptyqt with a
  **ConPTY** backend (`src/ptyqt/conptyprocess.cpp`); KPty is only required `if(NOT WIN32)`.
  macOS uses KPty (Unix PTY). Builds are via [KDE Craft](https://develop.kde.org/docs/getting-started/building/craft/).
  How complete/robust KonsolePart is there is **unverified** (nightly, not a stable release).
- **Relay's own code is Linux-only:**
  - `/proc/<pid>/fd/0` + `/proc/<pid>/stat` tpgid for tty readiness/foreground ownership.
    macOS equivalent: `proc_pidinfo`/`sysctl(KERN_PROC)` + `tcgetpgrp` on a PTY fd; Windows has
    no tty/pgrp concept at all.
  - Bash Readline integration (rcfile, `bind -x`, DEBUG trap, `PROMPT_COMMAND`). Fine on macOS
    (but default shell is zsh and system Bash is 3.2); on Windows needs PowerShell/cmd
    integration or Git-Bash/WSL-only support.
  - `/proc/self/exe`, CMake `FATAL_ERROR`, DBus usage, `secret-tool` keystore (macOS Keychain /
    Windows Credential Manager needed; Python `keyring` is not stdlib).
  - Shipping Python: bundle a relocatable CPython (e.g. python-build-standalone) or rewrite the
    worker; macOS no longer ships `python3` without Xcode CLT (**unverified** wording).
- Realistic: **macOS via Craft + KonsolePart is plausible** (weeks); **Windows is a larger port**
  (shell integration model differs), with KonsolePart-on-ConPTY as the least-change route.

### If Relay owns its terminal engine

| Option | Status | Notes |
|---|---|---|
| KonsolePart everywhere (status quo) | Linux stable; Win/mac nightlies | Least code; inherits KF6 bundle size (~90 MB Windows installer per CDN listing) |
| [libghostty-vt](https://github.com/ghostty-org/ghostty/blob/main/include/ghostty/vt.h) | C API usable, **explicitly unstable, no version tag** ([Hashimoto](https://mitchellh.com/writing/libghostty-is-coming), [awesome-libghostty](https://github.com/Uzaaft/awesome-libghostty)) | Parser, screen/scrollback, reflow, render state, key/mouse encoding. No PTY, no renderer: Relay writes a Qt renderer + PTY layer (openpty / ConPTY) |
| libvterm | Mature C lib (Neovim) | Same "bring your own renderer + PTY" work; weaker reflow (**unverified**) |
| [QTermWidget](https://github.com/lxqt/qtermwidget) | Linux/BSD/macOS; no official Windows | Smaller than KF6, still Unix-PTY |

Do an engine abstraction (`ITerminalView`: bytes, resize, focus, selection, foreground-process
query) before any port; KonsolePart stays the Linux default. Composer/router/backend are engine-independent.

### Signing and notarization costs

- **macOS:** Apple Developer Program **$99/yr** ([developer.apple.com/programs](https://developer.apple.com/programs/)),
  Developer ID Application cert + hardened runtime + `xcrun notarytool submit --wait` + `stapler`
  ([notarization docs](https://developer.apple.com/documentation/security/notarizing-macos-software-before-distribution)).
  Homebrew is disabling casks that fail Gatekeeper from **September 2026**
  ([Homebrew 5.0.0](https://brew.sh/2025/11/12/homebrew-5.0.0/)), so an unsigned cask is not viable.
- **Windows, Azure Artifact Signing** (renamed from Trusted Signing): $9.99/mo Basic (5,000
  signatures), $99.99/mo Premium ([pricing](https://azure.microsoft.com/en-us/pricing/details/trusted-signing/)).
  Public Trust for **individuals only in the US/Canada**; organizations in US, CA, EU, UK, AU, NZ,
  JP, KR, SG, CH, NO, IL; paid Azure subscription required; no EV certs; validation 1-20 business
  days ([quickstart](https://learn.microsoft.com/en-us/azure/artifact-signing/quickstart),
  [FAQ](https://learn.microsoft.com/en-us/azure/artifact-signing/faq)). GitHub Actions integration exists.
- **Windows, CA certs:** OV roughly $220-540/yr, EV roughly $280-600/yr; keys must be on FIPS
  hardware (token or cloud HSM); max validity 460 days since 2026-03-01
  ([Sectigo](https://www.sectigo.com/ssl-certificates-tls/code-signing), [sslinsights](https://sslinsights.com/sectigo-code-signing-certificate-guide/)).
  SmartScreen reputation still builds over time for non-EV/Artifact Signing
  ([Microsoft FAQ](https://learn.microsoft.com/en-us/azure/artifact-signing/faq)).

### Installers and updates

- macOS: `macdeployqt` (or Craft's DMG packaging) -> sign -> notarize -> `create-dmg`
  ([create-dmg](https://github.com/create-dmg/create-dmg)); Homebrew cask pointing at the release DMG.
  Updates: [Sparkle](https://sparkle-project.org/) (EdDSA-signed appcast).
- Windows: NSIS (Craft default, **unverified**), WiX MSI, or MSIX (Store/winget friendly);
  publish to [winget-pkgs](https://github.com/microsoft/winget-pkgs) via `wingetcreate`.
  Updates: [WinSparkle](https://winsparkle.org/) or winget.
- Linux: distro repos/AUR/PPA and Flathub update naturally; AppImage via AppImageUpdate + zsync
  info embedded at build time ([AppImage update docs](https://docs.appimage.org/packaging-guide/optional/updates.html)).
  `.deb` from GitHub Releases does **not** auto-update: show an in-app "new version" notice
  (opt-in check, to keep the no-telemetry claim honest).

## 3. Website

- **Hosting:** GitHub Pages (free, `gh-pages` or Actions deploy, custom domain + HTTPS)
  ([docs](https://docs.github.com/en/pages)); Cloudflare Pages (free tier, fast CDN, preview deploys)
  ([docs](https://developers.cloudflare.com/pages/)); Vercel (Hobby tier is non-commercial use only,
  [fair use](https://vercel.com/docs/limits/fair-use-guidelines)). GitHub Pages is enough for a beta.
- **Generator:** plain HTML for a single landing page; [Astro](https://astro.build/) if docs +
  blog/changelog pages grow (Markdown content collections); [Hugo](https://gohugo.io/) if a
  single binary with no Node toolchain is preferred.
- **Beta landing page checklist:**
  - One-line pitch + 20-40 s GIF/WebM of composer -> shell -> agent flow; 3-4 screenshots.
  - Download panel detected by OS: Arch (AUR command), Ubuntu 24.04 / 26.04, Debian 13 (`.deb`
    links + `sha256`), Fedora (source build), Flatpak "coming soon", macOS and Windows "not yet
    supported" with an email/issue "notify me" link (no tracking pixel).
  - Privacy/BYOK: keys stay local (env var or system Secret Service keyring, passed over a pipe,
    never written to Relay files); requests go directly to the provider you configure; no
    account, no Relay server, no telemetry. Only state these while they remain true, and say
    what *is* sent (prompts, tool output, file contents the agent reads) to the chosen provider.
  - Beta warning (agent tools run without per-action approval); install/build steps, known
    limits (Bash/Linux-only), changelog, issues link, GPL-3.0 licence, source link.
- **Release automation (GitHub Actions + Releases):**
  - Trigger on `push: tags: ['v*']`. Matrix jobs in containers: `ubuntu:24.04` (Qt5/KF5),
    `ubuntu:26.04`, `debian:13` (Qt6/KF6); each runs `ctest`, builds `.deb`, uploads artifact.
  - Final job downloads artifacts, writes `SHA256SUMS`, optionally signs it (minisign/GPG) or
    uses [artifact attestations](https://docs.github.com/en/actions/security-for-github-actions/using-artifact-attestations),
    and creates the release with `gh release create "$TAG" --generate-notes --prerelease dist/*`
    ([gh release create](https://cli.github.com/manual/gh_release_create)).
  - A job bumps the AUR `PKGBUILD` `pkgver`/`sha256sums` from the tag tarball.
  - Website links to `releases/latest/download/<fixed-name>` so it needs no rebuild per release.

## 4. Phased plan

| Phase | Scope | Rough effort |
|---|---|---|
| **Beta 0 (Linux, native)** | Tag-triggered CI: tests + `.deb` for Ubuntu 24.04/26.04 and Debian 13, `SHA256SUMS`, GitHub prerelease; AUR `relay` + `relay-git`; source build docs (Fedora); static landing page on GitHub Pages; privacy/BYOK and known-limitations text | 4-6 days |
| **Beta 1 (more Linux)** | Abstract host-process introspection (`/proc` checks) and shared state dir; Flatpak manifest building Konsole as a module, host shell and agent tools via `flatpak-spawn --host`; submit to Flathub; optional Launchpad PPA / Fedora COPR; opt-in update notice | 2-3 weeks |
| **Engine and platform abstraction** | `ITerminalView` interface over KonsolePart; replace `/proc` and Bash-only assumptions with per-OS backends; keystore backends (Keychain, Credential Manager); bundled Python strategy; spike libghostty-vt + Qt renderer vs. KonsolePart-on-Craft | 3-6 weeks |
| **macOS** | Craft (or engine) build, zsh + Bash integration, `.app` + DMG, Developer ID signing + notarization in CI, Sparkle, Homebrew cask | 3-5 weeks + $99/yr |
| **Windows** | ConPTY path (KonsolePart via Craft or own engine), PowerShell integration (Bash via Git-Bash/WSL optional), MSIX or NSIS installer, Artifact Signing or OV cert, winget manifest, WinSparkle | 5-8 weeks + ~$120/yr (Artifact Signing, if eligible) or ~$220-600/yr (CA cert) |
| AppImage / Snap (optional) | Only if users ask; Flatpak + native packages cover most users | 1-2 weeks each |
Effort figures are single-developer estimates, not measured.
