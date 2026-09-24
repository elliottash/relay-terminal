---
id: RR0G
type: work
status: needs-qa-llm
labels: [change, feature]
component: [gui]
milestone: desktop-alpha
workstream: terminal
assignee: agent
implemented_by: Claude Opus 5 subagent of a Claude Fable 5.1 session, 2026-09-19
rank: zzzzzzza
created: '2026-09-19'
acceptance: every state of the busy line above the prompt box shows a spaced en dash after "Relaying" — the agent turn, a turn blocked on background work, a turn blocked on a question, and a terminal program
source: issues/feature_intake.txt, 2026-09-19
links: {commits: [b81c5a1], evidence: [docs/qa_evidence/2026-09-19-relaying-dash/], github: null, plans: [], related: [4E13, HQ2B, 4X53, 0STR]}
---
# A dash after "Relaying"

## Issue

add a " -- " after "Relaying", i think it should be "Relating -- thinking..." for example, or "Relaying -- running grep..", etc

## Decisions

- 2026-09-19, owner: the dash goes on the line above the prompt box (`PaneBusyLine`), in **all**
  of its spellings — the agent turn, the background wait, the question wait, and the terminal
  program — so the one verb is always separated from what is being done.
- 2026-09-19, agent: a spaced **en dash** (U+2013) rather than two hyphens. ` -- ` is the
  typewriter spelling of exactly this dash, and the line already speaks in typeset punctuation:
  it is built out of `·` separators and an ellipsis character, so `--` would have been the only
  ASCII stand-in on the row.
- 2026-09-19, owner: the pane header's own state word ("Relaying…" beside the glyph) is a
  different surface and is not this card — it is removed altogether by #0STR.

## Tasks

- [x] `tickTurnClock`: `"Relaying – %1… · %2 s%3 · %4"` — the turn, the background wait and the question wait all go through it <!-- t:d1 -->
- [x] `refreshBusyLine`: `"Relaying – waiting for %1…"` (subagents, no turn of its own) and `"Relaying – %1…"` (a terminal program) <!-- t:d2 -->
- [x] Every comment that quotes the line updated: `src/Pane.h` (six places), `src/PaneStatus.h` <!-- t:d3 -->
- [x] Docs updated: `docs/ARCHITECTURE.md` (the live-states paragraph and the turn-clock paragraph), `docs/AGENT-SESSIONS-PROTOCOL.md` (the question caption) <!-- t:d4 -->
- [x] Live capture of all four spellings under Xvfb with an isolated HOME/XDG/TMPDIR and `RELAY_KEYRING=off` <!-- t:d5 -->

## As built

`src/Pane.h`. The line has exactly two builders and both now carry the dash:

- `Pane::tickTurnClock()` — `"Relaying – %1… · %2 s%3 · %4"`, where `%1` is the live tool call's
  gerund, `"thinking"` between calls, `"waiting for 2 subagents"` when the turn is blocked on
  background work (#V7QD, #KP4M) or `"waiting for your answer"` when it is blocked on an `ask_user`
  card (#MQ9C). The same string is `m_turnClockText`, which `pane_state` carries to a paired phone,
  so the phone shows the dash without a protocol change.
- `Pane::refreshBusyLine()` — `"Relaying – waiting for %1…"` for subagents still running with no
  turn of the pane's own, and `"Relaying – %1…"` for a program that owns the terminal.

Nothing else moved: the colour still says whose work it is, the row is still left-aligned with the
prompt text and in the normal weight (#HQ2B), and the middle elision is unchanged. The header's
`stateLabel(State::Working)` — `"Relaying…"`, the glyph's tooltip — is deliberately untouched here.

The anchor comment `// The "Relaying…" line when no turn` is left as it is: `tests/test_questions.py`
splits `tickTurnClock` on it.

## QA checklist

- [ ] An agent turn shows `Relaying – thinking… · N s · step 1/256 · Esc stops` in violet
- [ ] A terminal program shows `Relaying – <program>…` in blue
- [ ] A turn that leaves a subagent running shows `Relaying – waiting for 1 subagent…` in violet
- [ ] A turn blocked on an `ask_user` card shows `Relaying – waiting for your answer… · N s · … · Esc skips it` in amber
- [ ] An idle pane still shows no line at all
- [ ] A narrow pane still elides the line from the middle, dash and all
- [x] Implementer evidence: `docs/qa_evidence/2026-09-19-relaying-dash/` (README, drive.sh, stub-provider.py, four scenes at 100 % and 200 %, per-scene relay/worker logs)
