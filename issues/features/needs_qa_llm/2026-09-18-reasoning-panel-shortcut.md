---
id: R8QM
type: work
status: needs-qa-llm
labels: [feature]
component: [gui]
milestone: desktop-alpha
workstream: agent
assignee: agent
implemented_by: Claude Opus 5 (Claude Code, subagent), 2026-09-18
rank: b
created: '2026-09-18'
acceptance: 'Alt+R shows and hides this pane''s reasoning panel, reopens the last turn''s reasoning between turns, and says why when it cannot; the compact panel shows a line of reasoning rather than an empty box; `docs/F-KEYS.md` proposes an F-key scheme without binding one'
source: 'owner, feature intake 2026-09-18: "need a keyboard shortcut for showing / hiding the reasoning traces, maybe an F# key -- we can think about, how can we assign some things to F keys that are thematically / UX-coherent. or alt+R."'
links: {plans: [], commits: [a0d3920, 0d5ad4d], evidence: ['docs/qa_evidence/2026-09-18-reasoning-panel-shortcut/'], related: [N3WD], github: null}
---
# Alt+R shows and hides the reasoning, and an F-key scheme is proposed

## Change

**The key.** `agent.thinkingPanel`, bound to Alt+R, toggles this pane's reasoning panel:

| State | Alt+R |
|---|---|
| Panel up | Hides it, the same state the × sets, with a toast naming the key that brings it back |
| Turn running, panel dismissed | Brings it back |
| Between turns | Reopens the last turn's reasoning, headed "Thought · last turn · <model>" |
| Pane too short to draw it | "This pane is too short for the reasoning panel · make it taller" |
| Nothing streamed yet | "No reasoning yet in this pane", or that Show thinking is off |

Reopening the last turn was chosen over an inert key: the text is still in the view
(`endThinking()` only hides the frame), so there is something honest to show, and a key that
answers only during the seconds a model is thinking reads as broken the rest of the session. The
header says which turn it is. A new turn takes the panel back (`m_thinkingHeld = false`), and
`placeThinking()` still owns the show/hide decision, so the "too short to draw" rule from card
N3WD is reported rather than bypassed.

Alt+R is unbound in the Relay defaults and in all four presets (the only Alt+letter is Alt+F);
Ctrl+Shift+R is `pane.restartShell`, so there is no chord twin. The action also gets a palette row
("Reasoning panel", Agent section), since the Actions tab is built from palette items.

**A fix carried with it.** The compact panel showed no reasoning at all: it assumed 30px of chrome
where it spends 49 (header row with its two buttons, margins, document margin), so it came out
64px tall with a 15px viewport for a 17px line — a header over an empty box. `thinkingChrome()`
measures it instead, and compact is 81px with two lines visible.

**The proposal.** `docs/F-KEYS.md`, linked from `docs/README.md`, answers the second half of the
request without binding anything: the principle (an F-key toggles what is on screen, a Ctrl chord
acts), what an F-key costs here (under the default `program_keys: "shift-only"` they always act,
so each is taken from mc, nano and htop permanently), what is taken today, four candidates with
what each displaces, which it would not assign, and the precondition that F-keys should respect
`program_keys` before any of it ships.

## QA checklist

1. **While a turn runs.** Alt+R hides the panel and the terminal grows; Alt+R again brings it back.
2. **Between turns.** After a turn finishes, Alt+R reopens that turn's reasoning under a "Thought ·
   last turn" header; a new turn replaces it with a live one.
3. **Refusals speak.** In a pane too short for the panel, and in a fresh pane that has never
   streamed reasoning, Alt+R says why rather than doing nothing.
4. **Compact panel.** The collapsed panel shows an actual line of reasoning, not just its header.
   Check at a larger UI font too.
5. **Discoverable.** The Actions tab lists "Reasoning panel" with Alt+R beside it.
6. **No conflicts.** Alt+R does nothing unexpected in each of the four presets, and while vim owns
   the terminal it still works from the prompt box.
7. **Proposal only.** No F-key is newly bound: F2, F3, F4 and F6 still do whatever they did.

## Known gaps

- The scheme in `docs/F-KEYS.md` is unimplemented on purpose, and its own precondition (F-keys
  honouring `program_keys`) is not done.
- Alt+R reports that Show thinking is off rather than turning it on: that setting is global and the
  key is per pane.
