---
id: HDA9
type: work
status: needs-qa-llm
labels: [feature, packaging]
implemented_by: GLM-5.3 (Relay agent session), 2026-09-19
rank: zzzzzz
created: '2026-09-19'
source: pane 1, 2026-09-19
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-19-update-slash-command/], related: [], github: null}
---
# /update: download and install the latest release, then restart

## Issue
add a /update function that will download and install the latest version and restart

## Built
`/update` in any prompt box (and the palette's Update action, `app.update`, no default key —
the prompt box is the fast path) runs `scripts/relay-update.py install`:

- It reads the machine's `/etc/os-release` and `uname -m`, maps them to the release asset
  naming (`ubuntu24.04`/`ubuntu26.04`/`debian13` × `amd64`/`arm64`; anything else is refused
  with the list of what is built), and refuses a source checkout (`dpkg-query` finds no
  `relay` package) rather than let a `/usr` install shadow it.
- It takes the newest non-draft GitHub release, compares dpkg versions with
  `dpkg --compare-versions` (a `v0.1.0-beta.3` tag becomes `0.1.0~beta.3-1~slug`, so the
  tilde sorts as dpkg sorts it), downloads the right `.deb` plus the release's
  `SHA256SUMS`, verifies the checksum, and installs with `pkexec apt-get install` (the
  password dialog is polkit's; nothing is ever read from stdin; without pkexec it prints
  the sudo line instead).
- Every line it prints becomes the window's notice (20 s). On the final `UPDATED <tag>`
  marker Relay starts the new binary with this instance's arguments, then closes its
  windows through their ordinary path so layout and scrollback are saved. `CURRENT <tag>`
  means already latest; one updater at a time per window.
- `relay-update.py check` does the discovery without installing (`AVAILABLE`/`CURRENT`).

Files: `scripts/relay-update.py`, `tests/test_update.py` (8 tests, offline pure
functions), hunks in `src/Pane.h` (`/update` handler, `onUpdateApp`, help row),
`src/RelayWindow.h` (`updateApp()`, palette hookup, members), `src/Keymap.h`
(`app.update`), `CMakeLists.txt` (install rule), `README.md`. **Not committed:** the
working tree holds in-flight work from other cards in the same files; this slice is
complete in the tree and verified.

## QA checklist
- [x] `python3 -m unittest tests.test_update` — 8/8 offline decision tests (slug, arch,
      version mapping, asset name, checksum lookup, dpkg ordering, source-checkout refusal).
- [x] Full `relay` target built clean with the complete wiring (before the concurrent
      sessions' edits, see evidence NOTES.txt).
- [x] `check` on a source checkout refuses with one line, exit 1.
- [x] Live dry-run against the real release (installed version faked to beta.1):
      discovers beta.2, downloads `relay_0.1.0-beta.2_ubuntu24.04_arm64.deb`, verifies
      SHA256SUMS, stops before pkexec. Exit 0.
- [x] Asset names in the script match `release.yml`'s actual published assets exactly.
- [ ] On a .deb-installed Relay: `/update` end to end — pkexec dialog, install, restart
      into the new version. Needs a packaged machine; the guards above are what could be
      tested without one.
