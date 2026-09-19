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
- It takes the **highest-versioned** release in the chosen channel — GitHub lists releases
  in created-at order, so the first non-draft one is not the newest version and a patch cut
  for an older tag was being offered as "latest" — and compares dpkg versions with
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

## Decisions
- 2026-09-19, owner (decision C7): the release channel is an option. The default is every
  non-draft release, **prereleases included**, as before; a "stable only" choice excludes
  prereleases. Within the chosen channel the highest version wins, not the first listed.

## Update channel (2026-09-19)
`--channel all|stable` on both subcommands (`pick_release`, `in_channel`, `CHANNELS`):

- `all` (the default) is every non-draft release, betas included. `stable` drops the ones GitHub
  marks `prerelease`, which `gh release create --prerelease` sets for every tag with a `-`
  (`docs/RELEASING.md`), so the flag and the `~` in the .deb version agree.
- Within the channel the highest version wins, compared with the same `dpkg --compare-versions`
  helper (`apt_newer`) on the upstream version alone — the `-1~<slug>` suffix is equal for all
  candidates on one machine. A tag dpkg cannot read takes no part in the ordering (there is no
  answer to guess); it is skipped while any readable tag is in the channel, and only a channel of
  nothing but unreadable tags falls back to GitHub's order, where `apt_newer` then refuses the
  install with "Could not compare …" as it already did.
- A stable channel with nothing but betas in it says so and names the way out, rather than
  reporting the machine as up to date.
- Surface: **Options › General › Updates › Update channel** (`update/channel`, `choiceRow` like its
  neighbours; "all" removes the key so the row's reset and the script's default are one value).
  `RelayWindow::updateChannel()` reads it and `updateApp()` passes `--channel <value>` to the
  script, so `/update` and the palette's Update action both honour it.

Files: `scripts/relay-update.py`, `tests/test_update.py` (26 tests, offline pure
functions), hunks in `src/Pane.h` (`/update` handler, `onUpdateApp`, help row),
`src/RelayWindow.h` (`updateApp()`, `updateChannel()`, the Updates option row, palette
hookup, members), `src/Keymap.h` (`app.update`), `CMakeLists.txt` (install rule),
`README.md`, `docs/VALIDATION.md`. The channel slice above is committed; the rest was
in the working tree beside other cards' in-flight work when this card was first written.

## QA checklist
- [x] `python3 -m unittest tests.test_update` — 26/26 offline decision tests (slug, arch,
      version mapping, asset name, checksum lookup, dpkg ordering, source-checkout refusal,
      and the channel: out-of-order tags, a newest prerelease, drafts, an unreadable tag, an
      empty stable channel, and the flag on both subcommands).
- [x] Full `relay` target built clean with the complete wiring (before the concurrent
      sessions' edits, see evidence NOTES.txt).
- [x] `check` on a source checkout refuses with one line, exit 1.
- [x] Live dry-run against the real release (installed version faked to beta.1):
      discovers beta.2, downloads `relay_0.1.0-beta.2_ubuntu24.04_arm64.deb`, verifies
      SHA256SUMS, stops before pkexec. Exit 0.
- [x] Asset names in the script match `release.yml`'s actual published assets exactly.
- [x] Options › General › Updates › Update channel appears, `choiceRow` like its neighbours;
      the `relay` target compiles with it.
- [ ] On a .deb-installed Relay: `/update` end to end — pkexec dialog, install, restart
      into the new version. Needs a packaged machine; the guards above are what could be
      tested without one.
