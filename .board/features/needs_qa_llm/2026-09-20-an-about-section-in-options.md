---
id: 8WQK
type: work
status: needs-qa-llm
labels: [feature, options]
assignee: agent
implemented_by: anthropic/claude-opus-5
rank: zzzzzzx
created: '2026-09-20'
links: {commits: [], evidence: [docs/qa_evidence/2026-09-20-about-section/], github: null, plans: [], related: []}
---
# An About section in Options

## Issue
Owner, 2026-09-20, after asking where the build number is: "i think there should be an about
section in the options".

It was under **General › Diagnostics**, a heading whose other rows are things you *set* (the log
level), so nobody looked there for something to *read*. The Options search does not help either:
it skips Info and Heading rows on purpose (`src/SettingsPane.cpp:704`), because a search hit there
is something you press — so searching "build" or "version" found nothing but the rows that act.

## Plan
**Goal.** A last Options tab, **About**, holding what this Relay is, and a palette action that
finds it by name.

- The build row moves out of Diagnostics rather than being copied, so there is one place that
  answers "which build am I on". Diagnostics keeps the log level, which is a setting.
- About holds four rows: what Relay is and its licence (AGPL-3.0-or-later, naming the LICENSE
  file); the build line (build id · version · running since · the binary's path, plus the existing
  "a newer build is on disk" note); the parts line (terminal engine, Qt, distribution,
  architecture); and **Copy**, which puts all of it on the clipboard as one block for a bug report
  and says what it copied in a notice.
- Nothing on the page is a setting, so it gets no "Reset to defaults" row — `resetRow()` builds
  that from rows that declare a default and none of these do. That falls out of the existing code.
- **About Relay** in the palette (Relay section) opens Options on that tab, and its own detail
  line carries the version and build id, so the answer is visible before you press anything.

**Not done here:** the Options search still skips Info rows. Changing that would make search
return text you cannot act on, which is a separate decision about what search is for.

## Acceptance
- Options' last tab is About and shows version, licence, build id, the binary's path, engine, Qt
  and platform.
- The build line is no longer under General › Diagnostics, and is not duplicated.
- The palette's "About Relay" opens that tab, and its detail names the running build.
- Copy puts the block on the clipboard and the notice names the version and the build.

## QA checklist
- [ ] Ctrl+Shift+A, type "about": the first hit is About Relay and its detail shows the build id
      this Relay is running (compare with `cat build/relay.build-id`, allowing for a rebuild since).
- [ ] Enter opens Options on About; every line is filled in — no blank engine, no empty version.
- [ ] General › Diagnostics has the log level and no build line.
- [ ] Press Copy, paste somewhere: six lines, `Relay <version>` / `build:` / `binary:` / `engine:`
      / `qt:` / `system:`, and a seventh `build on disk:` only when a newer build is on disk.
- [ ] Rebuild while Relay is open, reopen the page: the "A newer build is on disk" note appears.
- [ ] With `--engine-core ghostty` (or `RELAY_ENGINE_CORE=ghostty`) the engine reads
      "Relay engine (ghostty)"; with neither, "Relay engine".
