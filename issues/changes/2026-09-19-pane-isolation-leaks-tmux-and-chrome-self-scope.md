---
id: Y4RX
type: work
status: in-progress
labels: [bug, isolation]
implemented_by: claude-opus-4-5
rank: zzzzzt
created: '2026-09-19'
source: pane 1, 2026-09-19
links: {plans: [], commits: [], evidence: [], related: [], github: null}
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
