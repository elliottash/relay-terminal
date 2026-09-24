---
id: P4GP
type: work
status: planned
labels: [feature]
component: [gui]
milestone: desktop-alpha
workstream: terminal
rank: 9a
created: '2026-09-17'
acceptance: a public project web page with screenshots and install instructions; a downloadable beta build for at least one Linux channel, installed and launched on a clean machine; the platform plan for Windows and macOS recorded
source: '`issues/feature_intake.txt`, "produce a nice app icon, make a web site and publish a beta build -- help me decide how to get it on windows / mac / linux / etc"'
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# Website and beta release

## Context

The app icon is being handled separately. Distribution options (Flatpak, AppImage, distro packages,
macOS and Windows feasibility given KonsolePart) are researched in `docs/DISTRIBUTION-RESEARCH.md`.
Windows and macOS builds depend on `issues/features/2026-09-17-portable-terminal-engine.md`.

## Website status (live 2026-09-17)

The landing site is **already live** at https://relay-terminal.ai (apex + `www`, HTTPS enforced).

- **Source**: `site/` in this repo — hand-written static `index.html` + `style.css` + `assets/`
  (screenshots). No build step; edit the files directly.
- **Deploy**: `./deploy.sh` from the repo root — rsyncs `site/` to
  `deploy@138.201.189.28:/opt/relay-terminal/site/` with `--checksum --delete`, then verifies
  both live URLs return 200. `./deploy.sh -n` is a dry run.
- **Hosting**: nginx on `elliott-main-1` (shared host) behind a Cloudflare Tunnel; docroot
  `/opt/relay-terminal/site`. Registrar Porkbun, DNS in Cloudflare.
- **Admin docs**: `~/admin/web-sites/relay-terminal/WARP.md` (site) and `~/admin/web-sites/WARP.md`
  (shared-host rules). Never change nginx, tunnel, or DNS from this repo — that host serves a
  dozen unrelated sites through one nginx instance.

Remaining website work: refresh screenshots as the UI changes, and add install/download
instructions once the beta build channel below exists.

## Remaining scope

- Beta build for at least one Linux channel (see `docs/DISTRIBUTION-RESEARCH.md`), installed
  and launched on a clean machine.
- Download/install section on the live page pointing at that build.
- Windows/macOS platform plan recorded (blocked on the portable terminal engine issue).
