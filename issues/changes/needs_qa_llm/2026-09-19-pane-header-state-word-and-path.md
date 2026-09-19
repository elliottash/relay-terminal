---
id: 0STR
type: work
status: needs-qa-llm
labels: [change]
component: [gui]
milestone: desktop-alpha
workstream: terminal
assignee: agent
implemented_by: "Claude Opus 5 subagent of a Claude Fable 5.1 session, 2026-09-19"
rank: zzzzzzzb
created: '2026-09-19'
acceptance: a pane header carries the state glyph, the title, the chips and the bare terminal path — no live state word, no "TERMINAL"/"AGENT WORKSPACE" labels, and no path at all when the title is already the folder's name; the glyph's tooltip still says the state and the header tooltip still has both paths
source: 'issues/feature_intake.txt, 2026-09-19'
links: {commits: [], evidence: [docs/qa_evidence/2026-09-19-header-state-word-and-path/], github: null, plans: [], related: [V8KT, XM0T, YMSR, D03W, RR0G, 4E13]}
---
# The pane header loses the state word, and the right-hand label becomes just the path

## Issue

clean up the headers of tabs and panes. they are busy

## Decisions

All 2026-09-19, owner, in the planning conversation:

- **The live state's word goes.** `PaneStateWord` — "Relaying…", "Command running", "Subagents
  working" beside the glyph — is removed, not hidden. The state glyph stays, keeps blinking, and
  its tooltip keeps spelling the state out; the full sentence lives on the busy line above the
  prompt box (#4E13, #RR0G), which has room for the action as well as the state. The widget's rung
  in the header's give-way ladder goes with it rather than staying as a form nothing ever asks for.
- **The right-hand label is the path.** `TERMINAL  ~/project     │     AGENT WORKSPACE  ~/x`
  becomes `~/project`. The words and the agent-workspace path move into the header tooltip, in a
  sentence, so nothing is lost — it has only stopped being said across the top of every pane.
- **A title that is the folder's name is not repeated.** The automatic title a model writes is very
  often the folder name, and "project … ~/project" says one thing twice. The title stays on the
  left, the path label is not shown, and the tooltip still has the full path.
- **The tabs are not part of this.** The owner did not pick the tab cleanup; the tab bar is
  untouched.
- **Everything else in the header stays**: the ssh chip, the phone chip, the subagent badge, the
  usage chip and the ⓘ ⊞ ⇱ × buttons.

## Tasks

- [x] `PaneStateWord` removed from `src/PaneChrome.h` — the class, the member, its row slot, its measure and its fit <!-- t:h1 -->
- [x] The word's rung removed from `relay::panes::headerFit`: `HeaderWants::stateWord/stateWordShort`, `WordForm`, `HeaderFit::word/wordPx`, and the ladder renumbered 1–5 <!-- t:h2 -->
- [x] `relay::panestatus::stateLabelShort` removed with its only caller; `stateLabel` still feeds the glyph's tooltip <!-- t:h3 -->
- [x] `tests/panelayout_test.cpp`: the word's two rungs taken out of the ladder tests, the other rungs kept and still tested end to end <!-- t:h4 -->
- [x] `tests/panestatus_test.cpp`: the short-form test replaced by one that every state still has a tooltip label <!-- t:h5 -->
- [x] `m_cwdText` is the bare path; `headerTooltip()` gains the sentence with both paths <!-- t:h6 -->
- [x] The path is not shown when the title is the last segment of the terminal's directory <!-- t:h7 -->
- [x] `docs/ARCHITECTURE.md`: the pane anatomy, the give-way ladder, the live-states paragraph and the two places that put something "beside the state's word" <!-- t:h8 -->
- [x] Before/after captured live under Xvfb, six scenes <!-- t:h9 -->

## As built

**The word.** `src/PaneChrome.h` loses the `PaneStateWord` class, the `m_word` member, its
`insertWidget(1, …)` slot (the badge and chips move up one), its branch in `measureHeader()` and
its line in `applyHeaderFit()`. `setStatus()` also loses the `updateHeader()` it did on every
change of liveness: that existed because the word appearing took room from the title, and the
glyph is one fixed size in every state.

**The ladder.** `src/PaneLayout.{h,cpp}`: `HeaderWants` loses `stateWord` and `stateWordShort`,
`HeaderFit` loses `word` and `wordPx`, and `enum class WordForm` is gone. The remaining rungs are
unchanged in behaviour and renumbered 1 directory, 2 title, 3 ssh chip, 4 usage chip, 5 nothing
else. The one arithmetic change is that the title is now served against `room - usageFull -
sshFull` rather than `… - wordFull`, which is what "the word is not on the ladder" means.

**The path.** `Pane::updatePaths()` sets `m_cwdText = tilde(m_cwd)` — the `width() >= 1000` branch
that produced the two-path line is gone. `Pane::updateHeader()` asks the ladder for a directory
width of 0 when the title repeats the folder: the test is `shown.trimmed().compare(folder,
Qt::CaseInsensitive) == 0` against `QFileInfo(m_cwd).fileName()`, where `shown` is the title, or
the folder's own name when there is no title. It is the *last segment* only and an exact match, so
"project notes" is a different name and keeps its path. `headerTooltip()` gained one sentence,
which reads "The terminal and the agent both work in <path>." when the two are the same.

**What the ladder was told is still what it decides on**: `wants.directory` is 0 or the label's
full natural width, never what the label happens to be showing, so the header cannot flicker
between two rungs (the rule the ladder's own comment sets out).

## QA checklist

- [ ] A live pane (agent turn, command, subagents) shows the blinking glyph and **no word** beside it; the glyph's tooltip still names the state
- [ ] The right-hand label of a terminal pane is the path alone, with no "TERMINAL" or "AGENT WORKSPACE"
- [ ] A pane whose title is the folder's name shows no path; one with any other title shows it
- [ ] The header tooltip names both paths in a sentence, and says which is the agent's workspace
- [ ] A pane with an ssh session, a phone share, a subagent badge and the usage chip still shows all of them, and they still give way in the decided order as the pane narrows
- [ ] The tab bar is unchanged
- [x] `ctest`: panes (the header ladder), panestatus, panestate, striplayout, editor, pulsepaint, paneusage all pass on the tree that landed
- [x] Implementer evidence: `docs/qa_evidence/2026-09-19-header-state-word-and-path/` (README, drive.sh, stub-provider.py, six scenes before and after at 200 %, per-scene relay/worker logs)
