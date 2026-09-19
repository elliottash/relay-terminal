---
id: T7QM
type: work
status: needs-qa-llm
labels: [change]
component: [gui, worker]
milestone: desktop-alpha
workstream: terminal
assignee: agent
implemented_by: "Oz agent session in Warp, 2026-09-19"
rank: zzzzzzzm
created: '2026-09-19'
acceptance: a tab is named by its place — the hand-set /rename-tab name if there is one, else the repo name of the attached project or of the active pane's repository, else the folder of the active pane's directory (home shows as ~); no worker is ever asked for a tab label; pane titles, header tooltips, the #JN7X chip, the pane-count and usage suffixes, and the offline joined-titles label of tool-only tabs are unchanged
source: 'owner, in a Warp session, 2026-09-19'
links: {commits: [], evidence: [docs/qa_evidence/2026-09-19-tab-title-repo-name/], github: null, plans: [], related: [0STR, JN7X]}
---
# The tab title is the repo name of its project, else the folder of the active pane

## Issue

change the tab title to be the repo name of the associated project, otherwise the folder of the active pane.

## Decisions

All 2026-09-19, owner, in the planning conversation:

- **A tab is named by its place.** In order: a hand-set `/rename-tab` name, the repo name of the
  tab's attached project (`#JN7X`), the repo name of the active pane's own directory, and the
  folder of that directory — `~` at home. The label follows the active pane's `cd` at once.
- **No model is asked for a tab label anymore, on any tab.** The worker's tab-label machinery —
  the `tab_label` request, the `LABEL_SYSTEM` prompt, the joined-titles merge of
  `related_text`/`join` — is removed; §18.3 of the protocol is now a GUI-side note, and a tab
  label needs no provider at all.
- **A tab label is the opposite of a pane title.** Pane titles stay per-pane, model-written and
  shown in the pane headers and the tab tooltip; the tab label only names where the tab is.
- **Tool-only tabs keep the offline join.** A tab with no terminal pane has no directory to
  follow, so it keeps its label built GUI-side from its panes' titles — offline, no request.

## Tasks

- [x] `relay::panetitles::placeTitle(cwd, project)` — the pure rule, with its tests <!-- t:t1 -->
- [x] `placeTabTitle`/`tabLabelFor` in `RelayWindow.h`; the tab-judgement machinery (`refreshTabJudgement`, `m_tabKey` and friends, the `onTabLabel` hookup) removed <!-- t:t2 -->
- [x] `Pane` loses `requestTabLabel`/`onTabLabel` and the `tab_label` event branch; `session_title` kept <!-- t:t3 -->
- [x] Worker loses `tab_label`: `session_protocol.py` type and handler, `titles.py` label path, `wire.py` forwarding <!-- t:t4 -->
- [x] Tests: `panetitles_test.cpp` gains `namesAPlace()`; `test_titles.py` loses its four tab-label tests and `label_calls` <!-- t:t5 -->
- [x] Docs: protocol §18/§18.3, `ARCHITECTURE.md` tab-label bullet and source-map rows, `VALIDATION.md` counts, `RELAY-FREE-HANDOFF.md` <!-- t:t6 -->
- [x] Live evidence under Xvfb, six scenes, no provider anywhere <!-- t:t7 -->

## As built

**The rule.** `placeTitle(cwd, project)` in `src/PaneTitles.cpp` is a pair of pure steps: a
project name is `QFileInfo(project).fileName()` (the path itself as fallback), and without a
project the label is `~` at home, else `QFileInfo(cwd).fileName()` (the path itself as fallback);
an empty `cwd` gives an empty label. The same-work rule that used to shape a label from the
pane's own title is gone — that is a GUI-side choice now, not a worker behaviour.

**The tab.** `RelayWindow.h` gets `placeTabTitle(page)`: the attached `tabProject(page)` goes
through `nameFor`, else the active pane (`m_lastActive`, falling back to the first `Pane` among
the leaves) contributes `placeTitle(pane->cwd(), candidateFor(cwd))` — `candidateFor` walks up
from the pane's directory to the folder with an `issues/board.yaml`. `tabLabelFor` then reads, in
order: the manual name, the place title, `"Relay"` when a tab has no titles to show, and the
offline `join` of the panes' titles for tool-only tabs. `refreshTabJudgement`,
`applyTabJudgement`, `m_tabKey`/`m_tabRelated`/`m_tabPhrase`/`m_tabLabelRequests`/
`m_tabLabelSerial`, their `updateTitles` call and the `onTabLabel` hookup are gone, and
`renameTab`/`forgetTab` no longer tilt any label machinery.

**The pane and the worker.** `src/Pane.h` loses `requestTabLabel`, `onTabLabel` and the
`tab_label` event branch; the `session_title` branch stays. `backend/relay_core/session_protocol.py`
loses the `tab_label` TYPES entry and its `_tab_label` handler; `titles.py` loses `label()`,
`related_text`, `join`, `distinct`, `LABEL_SYSTEM`, `STOPWORDS` and `_words`; `remote/wire.py`
loses the `FORWARDED_EVENTS` entry. `MAX_TOKENS` and `SUMMARY_MAX_TOKENS` stay — pane titles and
session summaries still go to the model.

**Docs.** `AGENT-SESSIONS-PROTOCOL.md` §18 is retitled "Pane title and session summary" (v1.8)
and §18.3 becomes "Tab labels (GUI side, no protocol since 2026-09-19)". `ARCHITECTURE.md`'s
tab-label bullet now says what a tab label is (the opposite of a pane title: it names where the
tab is), with the `src/PaneTitles.*` row updated and the backend row reading `titles`
(pane titles and session summaries). `VALIDATION.md`'s counts move with the tests
(`test_titles.py` 14→10, Python total 3350→3346; `panetitles` 6→7). `RELAY-FREE-HANDOFF.md`
drops "tab labels" from the Lite chores.

## QA checklist

- [ ] A tab over a repository shows the repo's name, from any subdirectory of it; `cd` elsewhere and the label follows at once, with no visible round trip
- [ ] A tab outside any project shows the folder of the active pane's directory; home shows `~`
- [ ] `/rename-tab` still wins until cleared, and clearing the field hands the tab back to its place
- [ ] Pane titles, the header tooltip, the #JN7X chip, the pane-count and usage suffixes are unchanged
- [ ] With no provider configured at all, every tab has its label immediately
- [x] `scripts/test.sh`: 3409 tests OK; `ctest`: 204 passed, 2 failed — both foreign (`buttonfit` is another session's in-flight `Theme.h` work, `backend-and-bash` is the documented ctest TIMEOUT-120 issue, clean via test.sh); `relay-titles-tests` built and run clean through `scripts/relay-build --target`
- [x] Implementer evidence: `docs/qa_evidence/2026-09-19-tab-title-repo-name/` (notes, drive.sh, relay-stderr.log, six scenes, OCR read-back)
