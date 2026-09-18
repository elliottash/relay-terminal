---
id: JRWQ
type: work
status: needs-qa-llm
component: [gui, worker]
milestone: desktop-alpha
workstream: agent
assignee: implemented by Claude Opus 5 (Claude Code, pane-title worktree), 2026-09-17
rank: zzu
created: '2026-09-17'
acceptance: each pane's header and each tab's label show a short agent-written summary, refreshed as the work changes; double click, /rename and /rename-tab set a name by hand that is never overwritten
source: 'owner in chat, 2026-09-17: "the pane header should be an agent-produced summary of the session." and "(and you should be able to double click on the pane header to set a new title"'
links: {plans: [], commits: [], evidence: [docs/qa_evidence/2026-09-17-pane-title-summary/], related: [], github: null}
---
# The pane header is an agent-written summary of the session

## Today

The header is `TERMINAL  ~/repos/relay-terminal  │  AGENT WORKSPACE  …`. The session "title" in the worker is
just the first 80 characters of the first prompt, used by the resume list and the conversation list.

## Wanted

- A short summary of what this pane is doing ("Fixing pane drag and Ctrl+H sizing"), written by a model, at
  most ~6 words, kept fresh as the work moves on.
- **Double click on the header** to edit it; a title the user typed is theirs and is never overwritten
  (a small "auto" badge or a reset action puts it back under the model's control).
- The directory keeps its place, smaller and to the right; the tooltip keeps both paths.

## Scope

1. Worker: generate the title with a cheap side call (chores role, `sidecall`), after the first turn and then
   only when the work has moved on — e.g. every 5 turns or after a compaction — never on every turn, and never
   when the user has set a title. Emit `session_title {title, source: "model"|"user", session_id}`; store it in
   the session file so the conversation list and resume picker use the same text. Falls back to today's
   first-prompt title when no model is configured or the call fails.
2. GUI: header shows the title, in-place editing on double click (Enter commits, Esc cancels), the directory
   right-aligned and dim, and the tab label follows the title too (elided). Tooltip: full title, terminal
   directory, agent workspace.
3. Protocol: document the event and `set_session_title {title}` in a new subsection.
4. Tests: title refresh cadence, user title never overwritten, restore after resume, and the fallback path.

## Tab labels and manual renaming (owner, 2026-09-17)

Owner: "ditto for the tab name, the agent produces a very short summary of what the panes are doing, if they
are unrelated, separated by a semicolon. manually: /rename, which renames the pane. or /rename-tab, which
renames the tab."

- A tab's label is model-written too: a very short summary of what its panes are doing. Related panes give one
  phrase; unrelated ones are joined with a semicolon ("Fixing pane drag; Release notes"), elided to fit.
- Derived from the pane titles, so it costs no extra call: recomputed when a pane title changes, with a cheap
  "same work or not" check (chores role; plain text comparison when no model is configured).
- `/rename <text>` names this pane, `/rename-tab <text>` names the tab. With no argument they open the same
  in-place editor as a double click (pane header for the pane, the tab itself for the tab).
- A name set by hand is fixed until cleared (`/rename` with no text after a confirm, or "Use automatic name").

## Implemented (2026-09-17)

Everything in Scope plus the tab-label section, with the owner's decisions. Protocol:
`docs/AGENT-SESSIONS-PROTOCOL.md` section 18. Architecture: "Pane titles and tab labels".
Evidence: `docs/qa_evidence/2026-09-17-pane-title-summary/` (`drive.sh`, `stub-provider.py`,
`drive-live.sh`, `cadence.txt`, `implementer-*.png`).

**Worker** — `backend/relay_core/titles.py` holds the rules; the state lives on `Agent`
(`title`, `title_source`, `title_turn`) and the call is started from `session_protocol.py`, off
the protocol thread with its own provider, the way recaps already are. `worker.py` hooks it to
the end of every main turn (`turn_emit` → `SessionCommands.observe`). One no-tools side call on
the **chores** role (Lite tier by default) sends a 12 000-character rendering of the conversation
and asks for `{"title": "..."}`; the reply is stripped of quotes, a `Title:` preamble and a
trailing period and cut to six words / 60 characters. At most one call per pane is in flight.

*Cadence* (`titles.due`): none before the first turn finishes, one right after it, none for the
next four turns, one again five turns later or at the end of the turn after a compaction, and
never once the user has named the pane. A pane rewound below the last title is not re-titled.

*Fallback* — with no model configured, or when the call fails, the title stays today's
first-prompt text (80 characters) and `session_title` carries it with `source: "model"`, which is
the GUI's "still Relay's to change". The cadence still moves on, so a dead provider is not asked
again after every turn.

*Persistence* — `title` and `title_source` are in the session file and the `.meta.json`, so the
conversation list (`conv_index` already reads `title`) and the resume picker show the same text,
and a resumed or loaded session emits `session_title` before its first new turn.

**Protocol** — `session_title {title, source: "user"|"model", session_id}` (event),
`set_session_title {title}` (empty title = "Use automatic name", and a fresh title is written at
once) and `tab_label {titles}` → `{label, related, source}`.

**GUI** — the pane header is a row: the title on the left with a small dim `auto` badge while it
is still the model's, the directory right-aligned, smaller and dim. The tooltip carries the full
title, the terminal directory and the agent workspace. Double click the title for an in-place
editor (Enter commits, Esc cancels, empty goes back to automatic); `/rename <name>` does it
without the mouse and `/rename` with no argument opens the same editor. The hover pane-button row
gives the header back the room it takes, so the directory is never underneath it.

**Tab labels** — `src/PaneTitles.{h,cpp}` (library `relay-titles`). The label is built from the
pane titles a tab already has, so it costs no extra title call: one phrase when the panes are on
the same work, the titles joined with `"; "` when they are not, elided to the tab. The judgement
is a `tab_label` call on the same chores role, asked only when a pane title actually changed, and
answered offline (`relay::titles::relatedText`: every title shares a content word with the first)
until it returns and whenever no model is configured. `/rename-tab <name>` and a double click on
the tab name a tab by hand; that name is fixed until cleared and follows the tab into a new window.

**Shortcut hints** (WARP.md) — renaming by double click shows "Next time: /rename <name> ·
/rename-tab names the tab" and the tab equivalent.

**Tests.** `tests/test_titles.py` (14 tests: the cadence table, tidying a reply, the fallback
title, the offline tab-label rules, and end-to-end through `SessionCommands` — one title after
the first turn and none for the next four, a compaction making one due, a user title never
overwritten and never refreshed, clearing it handing the pane back, the title and its source
coming back with a resumed session and appearing in the resume listing, the fallback when the
side call raises, the chores tab-label judgement and its offline fallback, and a new conversation
dropping the title). `tests/panetitles_test.cpp` (8 slots: cleaning, clipping, de-duplication,
the "same work" rule, joining and shortening). `./scripts/test.sh` 522 tests, `ctest` 17 tests.

**Verified live** (Xvfb, isolated `XDG_CONFIG_HOME`/`XDG_DATA_HOME`):

* `drive.sh` against a loopback stub — the header changes from the directory to "Fixing pane drag"
  after the first turn; four more turns cost **no** further title call (`cadence.txt`); a second
  pane on unrelated work gives the tab "Fixing pane drag; Release notes for 0.…"; double click
  opens the editor and shows the `/rename` hint; the renamed pane loses its `auto` badge;
  `/rename-tab` renames the tab; `/rename` with no argument reopens the editor; a further turn
  leaves both hand-set names alone. Side-call totals for the whole run: 2 title, 3 tab_label.
* `drive-live.sh` against a real provider (pane on GLM-5.3, chores resolving to the Lite tier)
  — real model-written titles "Choosing pane drag drop edge" and "Drafting 0.1 preview release
  notes", and the real tab label joining them with a semicolon.

## QA checklist

1. **A title appears.** Configure a provider and send one prompt. When the turn finishes, the pane
   header changes from the directory name to a short phrase about the work, with a dim `auto`
   badge beside it; the tab label follows. Hover the header: the tooltip has the full title, the
   terminal directory and the agent workspace.
2. **Not on every turn.** Send four more prompts. The title must not change and no title call
   should go out (watch the provider's dashboard, or point the pane at
   `docs/qa_evidence/2026-09-17-pane-title-summary/stub-provider.py` and read `calls.log`). On the
   sixth turn a fresh title is written. `/compact` then one more turn also writes one.
3. **Rename by hand.** Double click the header title: an editor opens with the current name
   selected, and a hint points at `/rename`. Type a name and press Enter — the header shows it, the
   `auto` badge disappears, and no later turn ever overwrites it. Esc while editing changes nothing.
4. **`/rename` and clearing.** `/rename Deploy checklist` renames without the mouse; `/rename` with
   no argument opens the same editor. Clear the field and press Enter: the pane goes back under the
   model, which writes a new title straight away.
5. **Tab labels.** Open a second pane (Ctrl+E) and give it clearly unrelated work. The tab reads
   `First thing; Second thing`, elided. Give the two panes the *same* work instead and the tab
   settles on one phrase. `/rename-tab Two jobs` and a double click on the tab both name the tab,
   and that name survives further turns; move the tab to its own window and the name goes with it.
6. **Resume and the lists.** `/resume` and Ctrl+Shift+O show the model-written title, not the first
   80 characters of the first prompt. Resume one: the header shows its title again before any new
   turn. A conversation you renamed by hand comes back with your name.
7. **Fallback.** Point a pane at an endpoint that fails (or set the chores role to a provider with
   no key) and send a prompt. The header must still fill in — with the first prompt's text — rather
   than stay on the directory, and the failure must not appear as an error toast or stop the turn.
8. **Narrow panes.** Split until a pane is narrow: the title elides with "…", the directory stays
   readable and, while the pane-button row is showing, the directory is not underneath it.
9. **Privacy and cost.** Nothing new leaves the machine except the conversation text the title call
   sends to the pane's own provider on the chores role. Check `~/.local/share/relay/logs/` for a
   title call that logged a prompt (there should be none; only a `title_failed` line with an
   exception name is ever written).

## Known gaps

- The tab's "same work or not" judgement is cached per set of pane titles. Panes whose titles have
  not changed keep the last answer even if the work has drifted; a title refresh re-asks.
- The offline rule (shared content word) is coarse: "Fixing the tests" and "Fixing the docs" count
  as unrelated because "fixing" and "the" are stop words, which is right, but two panes on one job
  that happen to use different vocabulary also count as unrelated until the model answers.
- A hand-set pane name is per session: starting a new conversation in that pane (`/new`) clears it,
  as the title belongs to the session.
- `conversation_rename` (the conversation list's own rename, protocol 14.5) still writes a separate
  `custom_title` in the index. Renaming a pane and renaming its row in the conversation list are
  therefore two different names; they were two different things before this change too.
- Title calls are not shown anywhere in the UI. The only way to count them is the provider's own
  usage page or a loopback stub.
- The live stub run used a loopback endpoint instead of a real provider so that it needs no key and
  no network; `drive-live.sh` is the same run against a real one.
