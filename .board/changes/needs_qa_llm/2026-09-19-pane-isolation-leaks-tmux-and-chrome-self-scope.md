---
id: Y4RX
type: work
status: needs-qa-llm
labels: [bug, isolation]
implemented_by: claude-opus-4-5 (investigation), Claude Fable 5.1 (the opt-in cap), 2026-09-19
rank: zzzzzt
created: '2026-09-19'
source: pane 1, 2026-09-19
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-19-cap-escapees/], related: [], github: null}
---
# Pane isolation leaks: tmux and Chrome self-scope into app.slice, outside any pane cap

## Issue
also, the OOM should be pane specific, and it seems like it affected two panes atonce

## Investigation
Investigated 2026-09-19 after the report. The two agent-worker OOMs were **properly pane-scoped**
and independent, not one event hitting two panes:

- `relay-pane-31a227b5-agent-1.scope`: OOM at 00:05:00, `2.0G memory peak, 512.0M swap peak`
  (kernel: `cc1plus` killed, `constraint=CONSTRAINT_MEMCG` inside that scope).
- `relay-pane-ab81944f-agent-1.scope`: OOM at 00:10:35, same shape, also `cc1plus`.
- `relay.log`: `worker_exit pane=31a227b5 code=15 crashed=1` and `worker_exit pane=ab81944f
  code=15 crashed=1` — SIGTERM from `OOMPolicy=stop` ending each scope. Both panes' agents were
  compiling (this repo) under the same 2G `agent_memory_max` default, five minutes apart.

The genuinely cross-pane events are two more OOM kills reported at **`app.slice`** level
(00:08:21 and 00:16:39), i.e. processes with no pane scope at all.

## Fault
Programs that re-scope themselves escape the per-pane caps entirely; both were observed tonight:

- `tmux`: the server moves into `tmux-spawn-<uuid>.scope` under `app.slice`
  (journal 00:08:20: `Started tmux-spawn-17a54ceb-…scope - tmux child pane 786523 launched by
  process 786522`) — memory used inside tmux counts against nothing, and seconds later an
  OOM kill was reported at `app.slice` level (00:08:21).
- `chrome`: headless instances create `app-com.google.Chrome-<pid>.scope` units under `app.slice`
  (a stream of them 00:08:00–00:08:07), so a runaway browser is outside its pane's limit too.

An OOM in `app.slice` can land on any process there — including processes other panes rely on —
which is exactly the cross-pane blast radius per-pane isolation is supposed to prevent.

## Decision (owner, 2026-09-19)
**(a) Document the hole plainly, (b) offer an opt-in cap.** The hole cannot be closed from the
pane's side: a child asks the user's systemd for a transient scope over D-Bus and gets a *sibling*
of the pane's scope, so Relay has nothing to contain it with. So it is written down where isolation
is described (`docs/ARCHITECTURE.md` section 13) and on the Options page beside the memory limits,
in those words — their memory counts against no pane's limit, and an OOM in `app.slice` can land on
any program there.

The mitigation is opt-in and **off by default**, because it is not per pane: it caps every tmux
server and every Chrome app scope on the machine, whether Relay started it or not. Nothing in the
unit name says which pane, or whether Relay was involved, so machine-wide is all a drop-in on the
unit-name prefix can be — and both the toggle's own text and the docs say so.

## What shipped
- `src/EscapeeCaps.{h,cpp}` (new, `relay-escapees` in CMakeLists.txt): writes and removes
  `~/.config/systemd/user/tmux-spawn-.scope.d/relay.conf` and
  `~/.config/systemd/user/app-com.google.Chrome-.scope.d/relay.conf`, with `MemoryMax=` /
  `MemorySwapMax=` set to the pane agent's own effective limits (`isolation/agent_memory_max`,
  `isolation/agent_swap_max` — "Auto" is the RAM-derived default `src/Isolation.h` computes, 8G on
  this machine), then `systemctl --user daemon-reload`.
- Every file Relay writes carries `# relay-managed: cap-escapees`. A file at one of those paths
  without that line is never written over and never removed: it is left exactly as it is and named
  in the status bar. The `.d` directory is removed only when Relay's file was all that was in it.
- Options › Terminal › Memory limits: an Info row stating the hole, then the toggle **"Cap programs
  that leave their pane (tmux, Chrome)"** (`isolation/cap_escapees`, default off) whose detail line
  names the limit and says it is machine-wide for those two programs, not per pane. Changing the
  agent memory limit while the cap is on rewrites the drop-ins, so the two never disagree.
- `tests/escapeecaps_test.cpp` (ctest case `escapees`), headless against a temporary config root:
  the paths, the text, install/remove, idempotence, and that an unmarked file survives both.

**The prefix mechanism, verified.** `man systemd.unit`: "for a unit name foo-bar-baz.service not
only the regular drop-in directory foo-bar-baz.service.d/ is searched but also both
foo-bar-.service.d/ and foo-.service.d/". So one file covers every `tmux-spawn-<uuid>` and every
`app-com.google.Chrome-<pid>`. Checked live on this machine (systemd 255): with the drop-in
installed, `systemd-run --user --scope --unit=tmux-spawn-relaytest-<pid> -- sleep 5` reported
`MemoryMax=1073741824`, `MemorySwapMax=536870912` and `DropInPaths=…/tmux-spawn-.scope.d/relay.conf`.
The drop-in was removed and `systemctl --user daemon-reload` run again immediately afterwards;
`~/.config/systemd/user/` is back to exactly what it held before
(`docs/qa_evidence/2026-09-19-cap-escapees/NOTES.md`).

## QA checklist
- [ ] Options › Terminal › Memory limits shows the Info row about tmux/Chrome and the new toggle,
      off, with a detail line that says "machine-wide … not per pane".
- [ ] Turning it on creates `~/.config/systemd/user/tmux-spawn-.scope.d/relay.conf` and
      `app-com.google.Chrome-.scope.d/relay.conf`, each with the marker line and the same
      `MemoryMax` the "Agent memory limit" row resolves to; the status bar says what happened.
- [ ] `systemd-run --user --scope --unit=tmux-spawn-qa-$$ -- sleep 5` then
      `systemctl --user show tmux-spawn-qa-<pid>.scope -p MemoryMax -p DropInPaths` shows the cap
      and the file.
- [ ] Turning it off removes both files and their directories, and leaves a hand-written
      `relay.conf` (no marker line) untouched, naming it in the status bar.
- [ ] Changing "Agent memory limit" while the toggle is on rewrites both drop-ins.
- [ ] `ctest -R escapees` passes.
