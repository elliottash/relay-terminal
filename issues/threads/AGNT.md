<!-- relay:entry 20260921T004500Z-a1 author=claude-code kind=event -->
### Claude Code · 2026-09-21 00:45
filed this card from the owner's words in the terminal, 2026-09-20, and claimed it for planning.
Related: #FEJQ (the helper system), #8YQ9 (the page agent), #PBX1 (every prompt box is the pane's),
#PK5Q (every model box is the pane's), #H6VQ (the queue in every panel), #BRD3 (the helper's model
box), #N8VK (the pane's queue), #GH5T (the helper on a guest Main), #R5TC (panes talk to each
other).

<!-- relay:entry 20260921T004600Z-b2 author=claude-code kind=decision -->
### Claude Code · 2026-09-21 00:46
The owner's words this card is built on, in the order he said them.

On the helper's panels:

> "the queue doesn't work like the main terminal, and the thinking bubbles don't work the same way.
> why not just make it feature equal with the terminal agent?"

and, asked whether a helper should simply be a `Pane`: **"yes, do it."**

Then, widening it:

> "eventually we might think of agents as a more general separable system in relay, and then the
> context behind them changes — a terminal, a switchboard card, a project management page, an
> options menu, etc."

> "lets actually do that now. an agent interface is the prompt box. it has a set of options and
> tools that vary according to the setting/task, but in general they are shared systems."

> "more generally, agents are specialized for the given pane context, but the general rule/approach
> is that agents have access to all systems and can work across panes and contexts."

So the card is not "a Pane with a helper mode". It is the seam: an **agent surface**
(`relay::AgentConsole` — the prompt box and everything that belongs to it) and a **context**
(`relay::agent::Context` — what the agent is about). The terminal is one context; the board, a card,
Options/Actions and Sessions are four more; a project-management page later is one file.

<!-- relay:entry 20260921T004700Z-c3 author=claude-code kind=plan -->
### Claude Code · 2026-09-21 00:47
Plan written onto the card. What I decided, and why, so a subagent does not have to re-derive it:

1. **`Context` has no `tools()` method.** The owner's third sentence rules out a per-pane allowlist:
   a fence is what stopped the Sessions helper opening a pane until #H6VQ. The gates stay the two he
   already chose — the `settable` / `agent_safe` markers and the Options › Agent toggle (#FEJQ
   decisions 1–3). The plan names, one by one, what withholds tools by pane in the backend today and
   marks each as a real constraint (no board → no `board_*`; a guest cannot run Relay's tools) or a
   fence to remove (`ChatScope`'s no-shell rule, `activity_tools` attached only to a pane agent,
   `track_requests`/`todo_tool`/`completion_check` off, `_deferred_groups` special-casing `helper`).
   It also names the accident: a board-less helper falls through `Agent.tools()`'s inference
   (`agent.py:979`) and silently gets the *full* executor.

2. **Every context keeps a vterm host as its transcript surface.** It costs one emulator per console
   and it buys the ANSI transcript, the OSC 8 folds, the theme repaint, the scrollback and the
   phone's screen stream for nothing. The shell is a separate question from the surface: the pty is
   started by one call, `m_backend->startProgram` (`src/Pane.h:9690`/`:9704`), at the end of
   `startTerminal` — so "a transcript with no shell" is a split inside one function, not a new
   rendering path. Marked in Risks as where the seam is deliberately thin.

3. **The extraction is a move, not a rewrite**, done by a `scripts/split-agent-console.py` modelled
   on `scripts/split-main.py`: content anchors, brace matching, `--check` rebuilds the original. Five
   waves, each its own commit through `land.py`'s build gate, and `Pane` keeps every public member as
   an inline forwarder so `RelayWindow.h` and every existing pane test compile unchanged. That
   property is what makes the step reviewable.

4. **The de-inlining `CLAUDE.md` warns about is step 10, last, and droppable.** After the move the
   code lives in a file this workstream owns, so a `.cpp` and a `relay-agentconsole` library touch
   nobody else's edits — and everything the owner asked for works without it.

5. **The one-line fix that stops six things breaking at once:** `RelayWindow::panesIn` recurses
   through a `ToolPane`'s whole subtree (`src/RelayWindow.h:6932`) where `leavesIn` stops at the leaf
   (`:6857`). An embedded console would therefore be found by `allPanes()` and get **published to the
   phone** by `syncAlwaysOnShares` `:7631` and `syncTabShares` `:7604`, break the close-dialog count,
   make the first real terminal default to Flash, churn the scrollback store, and take `set_board`
   meant for terminal agents. Step 5 gives `panesIn` the same early-out.

6. **Restore needs nothing.** `serializeNode` returns at the `ToolPane` branch without descending
   (`:7296`) and `isUsableNode` knows only eight node kinds (`src/WindowState.cpp:154-192`), so a
   nested console is never written and never rebuilt — the host recreates it. The conversation
   persists where #FEJQ already put it, keyed by the tab's `tab_id`.

7. **The convergence is mostly deletion.** There is no helper binary: a `BoardWorker` *is*
   `backend/worker.py` with `agent_role: "switchboard"` (`src/BoardWorker.cpp:119`,
   `src/RelayWindow.h:6607`), and it already builds a full pane agent at `worker.py:305` that is
   never asked anything. `board_chat*` and `PageAgent` retire; what has to be added is small
   (a session id on `configure`, a named tool scope, a `surface` echo on `ask`, the brief in the
   system prompt, a `readonly` flag, and `screen` renamed off the occupied `context`).

Eleven steps, disjoint file sets, four waves. Only steps 1 and 3 open `src/Pane.h`; only step 5
opens `src/RelayWindow.h`; steps 1, 2, 4 and 8 can run at once.

<!-- relay:entry 20260921T004800Z-d4 author=claude-code kind=question -->
### Claude Code · 2026-09-21 00:48
Five for the owner. **Each carries a recommendation the plan already assumes, so execution is not
blocked** — a different answer changes only the step named.

1. **One conversation across the four helper surfaces, or one per surface?** Today they share one
   and each panel filters by `pane`; with a real transcript that filtering becomes visible gaps.
   *Recommendation: keep one conversation and draw all of it in every helper console* — it is what
   "one conversation" means, it deletes the `pane`-tagging machinery and the class of bug #H6VQ was,
   and a filter is one predicate to add back. (Steps 4 and 7.)
2. **On a card, is Discuss still a turn written to the card thread?** *Recommendation: yes — keep the
   thread writes with their `model=`/`turn=` provenance, and give the card a real conversation behind
   them.* The thread is the record (POLICY rules 2, 4, 7). Plan/Execute/Verify stay the action row.
   (Step 6.)
3. **Does a helper agent get the shell and the file-write tools?** §19.18 says no; his new rule says
   a context specialises without fencing; and a board-less helper already gets them by accident.
   *Recommendation: yes, with the workspace it has, and never for a card's Plan turn.* (Step 4.)
4. **Should a helper console be shareable to a phone?** *Recommendation: not in this card* — the
   phone has no board UI at all, so the row would open onto nothing. Its own card if he wants it.
5. **Is step 10 (the `.cpp` + library + its own tests) worth doing?** *Recommendation: do it last,
   and drop it without regret if anything above runs long.*

<!-- relay:entry 20260921T005435Z-pw author=claude-code kind=progress -->
**Step 8 landed — `option:` and `session:` are link kinds of the transcript** (`59319d88`).

`relay::links::Kind` now has `Option` and `Session` beside `Path`, `Url` and `Card`
(`src/OutputLinks.h:28`), so an answer from *any* agent that names a setting or a saved
conversation is clickable in a pane's transcript, not only inside the helper panel's
`QTextBrowser` (the lambda at `src/HelperChat.cpp:585-617`, which step 9 deletes).

The grammar is the one the backend's answers already use, kept byte-for-byte: `option:<section>/<row>`
and `session:<id>`, each with or without an authority (`option://…`). Recognition is a new *first*
stage of `candidates()` — first because `option://sec/row` is a URL to the URL regex and would be
claimed whole. The scheme must open a word, the way `#ID` must, so nothing is claimed inside a URL
(`https://relay.test/docs/option:a/b` stays one URL link) and `options:` is not the scheme. A row id
keeps its own slashes (`option:models/provider/glm-coding` is one row, `RelayWindow.h:2472`): the
split is at the first slash, as the helper's handler split it. A section alone (`option:agent`) is a
link too. They need no probe and no board — the answer spelled the thing out — so they resolve in
any pane and in either `Mode`; the *host* decides whether it can show it, which is
`Context::resolveLink`'s job.

Public API added (`src/OutputLinks.h`):

```cpp
QString optionTarget(const QString &section, const QString &row);
bool    optionOf(const QString &target, QString *section, QString *row);
QString sessionTarget(const QString &id);
QString sessionIdOf(const QString &target);
```

They travel as `relay://option/<section>[/<row>]` and `relay://session/<id>` for the same reason a
card does: the engine carries one string per link (`engine/view/TerminalView.cpp:1870`), so the kind
has to survive inside it.

**What step 5 must wire**, in `Pane::openOutputTarget` (`src/Pane.h:2628`) / the window's routing:
beside the `relay://card/` branch, `optionOf(target, &section, &row)` →
`openSettingsPane(Mode::Options, section)` + `SettingsPane::revealOption(section, row)`, and
`sessionIdOf(target)` → `openSessions(QString(), id)`. Those are exactly the two bodies
`RelayWindow.h:6714-6724` already has for `onOpenOption`/`onOpenSession`, so step 5 moves them
rather than writing them.

Tests: `ctest --test-dir build -R '^outputlinks$'` — 42 slots, all pass. Six new ones cover both
spellings of each scheme, a row id with slashes, a section on its own, a line sharing card + option
+ path, prose mode, the non-matches (`options:`, a bare word, `option:` with nothing to name,
mid-token, inside a URL) and the target round trips.

One thing left for another session, because it is in `engine/` and outside this step's file set:
the link **context menu** (`engine/view/TerminalView.cpp:2765-2780`) still has only a card branch
and an else branch that reads the target as a file path, so a right-click on an `option:`/`session:`
link offers "Open <last path segment>", "Open with default application" and "Copy path". Left click,
Ctrl+click, hover and the keyboard walk (#GWXM) are all correct — they go through
`linkActivated(link.target, …)`. Worth a two-line branch there when step 5 lands the routing.

<!-- relay:entry 20260921T005702Z-q9 author=claude-code kind=progress -->
Step 2 landed: `relay::agent::Context`, `ContextSpec`, `Action` and `TurnRecord`
(de54a76144a2, `src/AgentContext.{h,cpp}`, `tests/agentcontext_test.cpp`, `relay-agentcontext` +
its test target in `CMakeLists.txt`). Steps 3, 6 and 7 can code against it without changing it.

**The header is where the rule lives.** It quotes the owner's two sentences and then says what a
context is *not*: not a tool whitelist — **no `tools()` method, ever**, with the #H6VQ fence and the
two gates that stay (the `settable` / `agent_safe` markers, the Options › Agent toggle) named in the
paragraph that refuses it — and not a widget and not a worker.

**The interface, as written.** `Context` is the card's, unchanged: `spec()`, `actions()`,
`resolveLink(const relay::links::Target &)`, `turnFinished(const TurnRecord &)`, `placeholder()`.
One addition the card did not name and steps 6 and 7 would otherwise have had to add: a
`std::function<void()> onChanged` the console sets and the context calls through `changed()` when
anything `spec()` or `actions()` would now answer differently has moved — a card going busy and its
Execute becoming "Executing (a1b2c3d4)", Options swapping mode. There is no signal because the
library is QtCore-only and a context is not a QObject.

**`Action`** is `{key, letter, label, tooltip, leaves, enabled, run}` with `fullLabel()` →
`"Check (k)"`, plus free functions `withUniqueLetters()` (a duplicate letter, a multi-character
letter or a letter another action claimed first is cleared — the *letter* is refused, never the
action, so one context's button can never make another's disappear), `actionForLetter()` (case
insensitive, and a disabled action answers nothing — the key can do no more than the mouse) and
`labelWithoutKey()` (the same rule `CardDetail::fitButtons` uses today, so step 6 can drop its
static copy). `leaves` is the accent-outline flag for the two that hand the card to a pane.

**`ContextSpec` is the only part that crosses to the worker**, and both halves are golden-tested so
step 4's Python is checked against one written-down shape rather than against the GUI:

- `toJson()` — `configure`'s `context` block: `name`, `surface`, `agent_role`, `workspace`,
  `scope`, `shell`, `routing` always; `persist {scope, key}` only when there is a key (no key means
  no store, which is what 30.7 says a `configure` with no tab means); `brief {key, title}` only
  when there is one. `surface` falls back to `name`, so a host with one console per context sets
  nothing.
- `askFields()` — what rides on each `ask`: `surface` always, `screen` when there is one (trimmed,
  cut at 2000 characters as 30.7 already cuts `board_chat`'s), `readonly` only when true. Neither
  `screen` nor `readonly` is in the `context` block: they are the turn's, not the context's.
- `persistId()` — `scope` and `key` joined with a unit separator, so a board-less tab keyed
  ("", tab) cannot resolve to the conversation the same tab inside a project keeps, and no pair of
  (scope, key) can spell another pair's string. `operator==` is the rest of 30.7's move test: a
  model swap leaves the conversation where it is, a new tab does not.

`TurnRecord` carries what `board_protocol._card_answer` writes today — id, surface, prompt, answer,
`model`, `sessionId`/`turnId` (`turnRef()` → `"<session>/<turn>"`), `mode`, `outcome`, `readonly` —
so `CardContext::turnFinished` can append a Discuss turn to `issues/threads/<ID>.md` with its
provenance (owner decision 2).

**The library edge goes the way the card asked.** `relay-agentcontext` is QtCore-only and links
`relay-outputlinks` (for `links::Target`); `relay-board`, `relay-settings` and `relay-conversations`
link *it*, never the other way, so a pane library can supply a context without pulling in a window.

Tests: `ctest --test-dir build -R '^agentcontext$'` — 20 cases, all passing, and the same run inside
land.py's verify build of the exact tree that went on main. Only my two `CMakeLists.txt` hunks were
landed (`--only-hunk`); step 1's source-list hunk was left in the working tree untouched.

One note for step 4: the `context` block carries `surface` beside `name` (`name` is the kind —
"terminal", "card", "options" — and `surface` is *this console's* id, unique within a worker, so a
tab with two cards open on one worker can be told apart on events). If the worker would rather take
the surface only off the `ask`, it can ignore the field; `fromJson` ignores unknown keys the same
way, so neither side breaks when the other adds one.

<!-- relay:entry 20260921T011240Z-e7 author=claude-code kind=progress -->
Step 1 wave 1a has landed, and the measurement says waves 1b-1e are not a pure move.

**Landed**

- `cbf06cc0d668` — the seam. `src/AgentHost.h` is `relay::agent::Host`: the thirty `m_backend`
  touches of the agent block grouped and named (`writeTerminal` / `columns` / `atLineStart`, the
  four fold calls, `viewportAtBottom` / `scrollToBottom`, `screenText` / `cursorPosition`,
  `sendText` / `paste` / `shellPid` / `foregroundProcessId` / `terminalMode`), plus `status` /
  `toast` / `hint` and `bubbleRoom` / `setBubbleHeight`. `src/AgentConsole.h` is the console,
  empty but for its host reference. `Pane` gains the second base: nine members it already had
  were spelled the way the interface names them and became overrides in place, thirteen more are
  written once above the "agent sessions UI" banner, and it owns
  `relay::AgentConsole m_agent{*this}`. No public member of `Pane` changed, no behaviour changed,
  and no agent code has moved yet.
- `dd104c3ba3cd` — `scripts/split-agent-console.py` (the mover, `--check`, `--index`,
  `--coupling`) and the live evidence in
  `docs/qa_evidence/2026-09-20-agent-console-extraction/`.

**The terminal pane is unchanged, and this is what says so.** The pane driven live under Xvfb
through nine scenes — a fresh pane, a turn with reasoning folded and unfolded with Alt+R, a
`read_file` tool row, the model box, a second prompt queued behind a running turn with its strip,
Esc during that turn — on a clean export of the tip this started from and on that same tree with
wave 1a's diff applied (so nothing else that landed on `main` is in the comparison), plus a third
control run of the *before* binary against itself. Below the pane header, six of the ten shots
are pixel-identical (`compare -metric AE` = 0) and every non-zero number is read: 36 pixels is
the blinking caret, a 2x18 box the control shows too, and 21 310 on the first shot is the startup
quota toast the screenshot caught in one run and missed in the others. The pane's
auto-generated title differs between two runs of the *same* binary (1 316–1 539 px), which is
why the whole-window numbers are read below the header. Tests: the pane's suites unchanged and
green before and after — `queuenav queuesubmit continueturn input slash markdown calllines
internalsledger toollabel panestate panes agentinternals turntranscript outputlinks requests
transcriptreplay`, 16/16.

**The finding, and what it needs from you.** The plan costed the seam as "the 4 631-line agent
block touches `m_backend` exactly 30 times". That is true, and it is not the whole cost: the
block also reads `Pane`'s own fields and calls its own members directly.
`split-agent-console.py --coupling` over wave 1b's regions (101 members, 1 395 lines) reports
**33 fields that would travel with the cut, 66 that cross it, and 52 members of `Pane` called
from inside it that are not on `Host`** — `m_editor` (181 uses left behind), `m_backend` (131),
`m_agentBusy` (71), `m_login` (67), `m_cwd` (59), `m_token` (40), `m_workspace` (32). Narrowing
does not rescue it: the 137-line prose / `printInline` slice alone leaves 8 fields and 11 members
on the other side. Both measurements are committed
(`coupling-wave-1b.txt`, `coupling-printinline.txt`).

So a pure move needs `Host` to grow from ~22 calls to sixty-odd — and a good part of what the
block reads from `Pane` is not what the console is *drawn on* at all. `workspace`, `cwd`, the
routing, the agent role, the persist key are what the agent is *about*, which is
`relay::agent::Context` and step 3. Pushing them through `Host` would make every embedded host —
BoardPane, SettingsPane, Conversations — answer `loginAtPrompt()` and `foregroundCommandLine()`,
which is the opposite of what Risks 3 chose.

That is a change to this card's own seam, so it is not taken here. Waves 1b–1e are declared and
empty in the tool, with their anchors kept as the record of what each was to move. **The step
that unblocks them is step 3** (`AgentConsole` takes a `Context`; `Pane` supplies
`TerminalContext`), which this session holds and which waits on step 2. The order in the plan —
1b–1e before 3 — should be read the other way round: the context has to exist before the block
can be cut, or the cut has to carry the context through the host.

<!-- relay:entry 20260921T012110Z-8w author=agent kind=progress model=claude-opus-5 turn=agnt-worker/step-4 -->
Step 4 landed: **the worker speaks one protocol.** A helper worker is `backend/worker.py`
with a `context` block; `board_chat.PageAgent` and the four `board_chat*` messages are retired.

Commits (oldest first):

- `f0f9c6b7` — `relay_core/agent_context.py` + `tests/test_agent_context.py`: the `context` block
  of `configure`, field for field with step 2's `relay::agent::ContextSpec`; the console briefs
  and the helper-session file layout (#FEJQ's `3ddd2193`, untouched) move in with it.
- `34ff0830` — the **named tool scope** (`Agent(tool_scope=…)` — `pane`/`console`/`card`) resolved
  in one place, `ChatScope` → `ConsoleScope` with every fence down, the brief in the system prompt
  (`set_agent_context`), and `Agent.set_readonly` for the survey's turn.
- `bb1221a2` — `ask {surface, screen, readonly}`, `configure {context}` read by `worker.py`,
  `queue_move {item, to}` and `queue_ack`; every event of a turn carries its `surface`.
- `ece42d07` — `PageAgent` and `board_chat*` retire; the survey becomes a `readonly` turn of the
  console's own conversation; card turns gain `agent_started`/`agent_finished` and
  `surface: "card:<ID>"`; "say what you are doing" moves to `Agent`, for every agent; protocol
  §19.18 and §30.7 rewritten and a new §33 written.
- `ca4e5162` — the dead `turn_started` tag removed from its last two homes.

What Steps 3, 5, 6 and 7 code against, verbatim:

`configure {"context": {"name", "agent_role", "workspace", "persist": {"scope", "key"},
"brief": {"key", "title", "screen"}, "scope", "shell", "routing"}}` — `name` is one of
terminal|switchboard|card|options|actions|sessions and is required; `scope` is pane|console|card
and defaults from the name; `routing` is auto|agent; `shell` is a bool; `persist.scope` is
""|pane|helper and a scope without a key is refused. `configured` echoes the block back with the
scope the worker settled on.

`ask {"surface": string ≤64 one line, "screen": string cut at 2000, "readonly": bool}`. `surface`
rides on `queued`, on every `queue_changed` row, on `agent_started`/`agent_finished` and on every
event of the turn. `screen` reaches the model as an "On screen now:" line above the prompt and
stays out of the queue preview and the request ledger. A pane sends none of the three and no
event grows a field.

Tool scope table — constraint vs fence, in §33.3. Kept: no board → no `board_*`; a guest harness
cannot run Relay's tools (#GH5T stands untouched); a card's Plan turn writes only its own
`## Plan` (19.20). Removed: the console's missing shell and file tools (owner decision 3),
`session_info`/`activity` being pane-only, `track_requests`/`todo_tool`/`completion_check` forced
off, and `_deferred_groups`' `helper` special case — which is now one line: only a pane defers.
`board_sections` and `board_claim` stay off a console's list, but as constraints (a cleanup's
preview; a terminal pane to claim for), not fences.

Tests: `tests/test_agent_context.py` (new, 21), `tests/test_queue.py` (+8), `tests/test_board_chat.py`
rewritten to 32 including an end-to-end NDJSON drive of `worker.py` as a console on a loopback
stub, `tests/test_board_turns.py` (+2). 1,664 backend cases run green but for three failures that
are already on main and not this work's: the two #8EJ4 ones in `test_board_protocol`, and
`test_the_board_policy_block_stays_tiered`, which was failing at `fdb662d6` (#Z4HR) on a clean
export before any of these commits.

Left for the GUI steps: the current helper panels stop working until steps 5–7 land, by design —
an old GUI sending `board_chat` now gets one sentence naming `ask`/`surface`/`context` rather
than a traceback.

<!-- relay:entry 20260921T013929Z-rq author=claude-code kind=progress -->
Step 3 has landed. Waves 1b–1e are the wrong cut, and here are the numbers.

**Landed**

- `24042b5b` — `AgentConsole` takes a `Context`, and `Pane::TerminalContext` is the first one.
  `setContext`, `spec()`, `configureBlock()`, `askFields()`, `actions()`, `resolveLink()`,
  `turnFinished()`, `placeholder()`, `onContextChanged`. The terminal context carries what the
  coupling list showed crossing the cut that is the agent's *subject*: workspace, role, routing,
  the named tool scope, and where the conversation is kept. Wired at the four funnels that
  already exist — the context is set before the worker starts, `withSessionFields` inserts the
  block, `startAgentEntry` merges `askFields()`, `agent_finished` builds a `TurnRecord` — and
  `openOutputTarget` offers an activated link to the context first. The bubble question moved
  behind `Host` (`bubbleRow`, `bubbleSpan`, `roomForBubble`, `showBubble`, `hideBubble` join
  `bubbleRoom` and `setBubbleHeight`), with a new `overlayArea()` for the four terminal-anchored
  overlays; the `@`/`#` pickers and the help sheet are deliberately not in it, because they
  anchor on the composer and travel with it.
- `81e889c1` — **and that first landing was wrong.** `persist.scope` went out as the workspace
  path and `routing` as the pane's input mode. Both are closed sets on the wire (`PERSIST_SCOPES`
  is `""`/`pane`/`helper`, `ROUTINGS` is `auto`/`agent`), so `ContextSpec.from_json` raised on
  every `configure`, the worker never built an agent, and the pane came up with a filled model
  box that did nothing when you pressed Enter. The build was green and all seventeen suites were
  green. **The live drive is what caught it** — six of six checks failed on that binary and
  passed on the tip before it. `persist.scope` is now `"pane"` (which store, not which path;
  `store()` only redirects for `"helper"`, so nothing about where a pane's session lives moved)
  and `routing` is `"auto"` flat.
- `137f748f` — the evidence, and `--closure`.

**The gate.** Re-run, seven of the ten shots are pixel-identical across the whole window. The
three that differ are 36, 117 and 36 pixels: the blinking caret's 2×20 box twice, and the turn
clock reading "Relaying · thinking… · 8 s" against "9 s". Suites: the same seventeen, green.

**And now the finding you asked me to stop for.**

`scripts/split-agent-console.py --closure` is new because hand-drawn line ranges cannot answer
"is the cut wrong or merely large" — a member that looks like it crosses usually just sits
outside the range somebody typed. It starts from the owner's own "agent sessions UI" banner and
grows the set to a fixed point, adding anything whose every remaining mention is already inside
it, which can only make the set bigger and the seam smaller. It stops at **228 members, 4 031
lines, with a minimum seam of 284 names — 157 fields and 127 members**. That is the floor,
whatever order the waves are run in. Cutting the whole agent block in one go agrees: 165 fields
and 167 members left on the other side, with only 18 calls already carried by `Host`.

The number that decides it is **`m_editor`: 109 uses of the composer stay in `Pane` even when the
entire agent block moves**. They are not scattered — they are one thing, the routing decision in
and around `requestRoute`: read the typed text, try a slash command, then an alias, then a skill,
then choose the shell, the foreground program, an ssh login, or the agent.

So the prompt box is not a part of the agent block that can be lifted out of it. It is what both
halves are made of. Waves 1b–1e as drawn — move the agent out, leave the terminal holding the
composer — are **the wrong cut**, not merely a large one: they would leave a 284-name seam
through the middle of one widget, and every one of those names would have to become a `Host`
call, a `ContextSpec` field or a console accessor before a single line could move.

The cut that does work is the owner's sentence read literally: *an agent interface is the prompt
box*. The console owns the composer, the queue, the transcript and the worker; the terminal
becomes **one routing of what is typed in it**, and `Pane` keeps the shell, the pty, the login,
the program control and the chrome. That inverts the card's "Pane hosts an AgentConsole" into
something closer to "the console hosts the routing, and a terminal is one of its destinations",
and it changes steps 5, 6 and 7 with it. It is a decision about the card's shape and it is
yours, so it is written down here with the numbers rather than taken.

What is already usable either way, whichever you decide: both seams exist and are tested
(`relay::agent::Host`, 30 calls; `relay::agent::Context`, and `agentcontext` green), the terminal
pane is measurably unchanged, `--coupling` and `--closure` re-measure any proposed cut in
seconds, and the live drive catches a wire-contract break that the build and the suites do not.

<!-- relay:entry 20260921T014050Z-rv author=agent kind=decision pane=switchboard -->
Decision (orchestrator, on the owner's sentence "an agent interface is the prompt box", 2026-09-20): the `--closure` measurement (228 members / 4 031 lines, a floor of 284 names on the seam, 109 uses of the composer left behind by any cut) says the agent block cannot be lifted out of `Pane` around the composer — the box is what both halves are made of. So the cut inverts: **`Pane` is the agent console**, the terminal is one routing of what is typed in it (`TerminalContext` adds the shell, `requestRoute`, login/native), and a helper is the same `Pane` with a non-terminal context — no shell started, routing locked to agent, the vterm kept as the transcript surface. Step 1's remaining waves and Step 10 are dropped; `AgentConsole.h` stays only as the thin context-facing surface (or folds back). Steps 5–7 embed a no-shell `Pane` rather than a separate console class. Risk 2 is answered by measurement rather than by moving 4 000 lines. Both seams (Host, Context) are landed and tested (cbf06cc0, 24042b5b, 81e889c1); the terminal pane is pixel-identical.

<!-- relay:entry 20260921T024032Z-5p author=claude-code kind=progress -->
The inversion is built: a pane with no shell is the whole console. Here is the API Steps 5-7 embed.

**Landed** — `7a35a498` (the console mode), `769cc4a4` (the live evidence, the tool's retirement,
a C++20 warning of mine).

`relay::AgentConsole` is gone, folded into `Pane`: a console holding a member called a console was
the confusing half of the old shape. `relay::agent::Context` and `relay::agent::Host` stay.

## The API, for Step 5's briefing

**Construction.** `Pane(workspace, cwd, cleanShell, engineCore, context)` — one constructor, the
context last and defaulted to `nullptr`. The context is the **host's**: it must outlive the pane,
and `~Pane` clears its `onChanged`. Passing none is a terminal pane, which is every pane written
before this card. There is no second start form: `hasShell()` is `spec().shell`, settled once per
context, and it is the single answer everything keys off.

What a `shell: false` context gets, and none of it is a second rendering path:

- `startTerminal` returns before the pty. Everything above that line — the engine, the theme, the
  fold layer, the link probe, the card lookup — the console keeps.
- The poll timers never start (four wakeups a second per console, the cost of four per tab).
- `shellPid()` and `foregroundPid()` answer 0, so the guest bridge, the line discipline, "take
  control" and the ask's `foreground_program` are inert through that one answer.
- `setNative(true)` is refused, and the five-second "shell integration did not initialize"
  watchdog does not run.
- `inlineReady()` is true, so the transcript goes to the vterm and not to the fallback panel.
- The routing is locked to `agent` and the mode chip is hidden. `applyContextRouting` only ever
  *restricts*: a terminal context leaves it having changed nothing.

**The composer and the context.** The action row is inside the composer frame, above the busy
line, left-aligned, buttons only, each wearing its letter via `Action::fullLabel()`;
`withUniqueLetters` refuses a letter two actions claim rather than answering it twice, and
`runActionLetter(letter)` is the keyboard half. The button's `objectName` is the action's `key`
and it carries `fullLabel` and `leaves` as properties, so `src/Theme.cpp` and the tests can key
off them. `rebuildActionRow()` runs on every `Context::changed()` — build the list fresh in
`actions()`; do not cache widgets. The placeholder is `Context::placeholder()` with "…" under it;
a terminal pane keeps RichEditor's own ladder untouched.

**What a host calls:** `setContext(Context *)`, `context()`, `contextSpec()`, `hasShell()`,
`widget()` (it is the pane), `focusComposer()`, `draftInComposer(text)` (replaces the draft;
`insertInComposer(text)` still appends at the cursor), `composerText()`, `collapsed()` /
`setCollapsed(bool)` for the "? Helper Agent (Alt+Q)" fold — the row stays the host's, the pane
only needs show/hide and focus, and it re-focuses the composer on expand — and `workerContext()`,
what the worker echoed back on `configured {context}`, read rather than guessed.

**What a host must not do**

1. **Do not put a console in a window's leaf lists.** `Pane` registers itself nowhere on
   construction — `consolemode` asserts it is an ordinary child and not a top-level window — so
   the only way one can be walked into is a parent that is walked into. **`RelayWindow::panesIn`
   must stop at a `ToolPane`, the way `leavesIn` already does.** That single line is Step 5's, and
   without it a console is published to the phone, counted in the close dialog, makes the first
   real terminal default to Flash, and churns the scrollback prune.
2. **Do not construct one from a pane library.** `Pane` exists only inside the `relay` executable's
   translation unit. `RelayWindow` makes it and hands it over as a `QWidget *` plus the handles
   above — the `relay::PaneView` pattern.
3. **Do not free the context before the pane**, and do not set `Context::onChanged` yourself: the
   pane owns that callback while it holds the context.
4. **Give a `switchboard`-named context a board.** A board console with no board is refused in one
   sentence by the worker; that is one of the real constraints the card kept, not a bug. Options,
   Actions and Sessions are the board-less surfaces.
5. **`persist.scope` is a wire enum** — `""`, `pane` or `helper` — not a path, and `routing` is
   `auto` or `agent`, not the pane's input mode. Both are validated and both raised on every
   `configure` when I got them wrong in 24042b5b.

## Gates

18 suites green, including the new `consolemode` — nine cases against real `Pane`s, among them
"a terminal pane is unchanged". The terminal pixel diff of `7a35a498^` against `7a35a498`: eight
of ten shots identical below the pane header, the other two the blinking caret and the startup
quota toast. The live console drive: six checks, all passing, including the queue strip with a
second prompt behind a running turn.

The live gate earned its place again: it found three terminal-only paths a console walked into
that neither the build nor the suites saw — the native-mode watchdog hiding the whole prompt box
after five seconds, `inlineReady()` never becoming true so every printed line queued for ever, and
a non-zero `shellPid()` from an emulator with no program.

`--check` is retired with the mover: nothing was moved, and `src/AgentConsole.h` is gone.
`scripts/split-agent-console.py` keeps `--index`, `--coupling` and `--closure`, and says in its
first paragraph why the waves were abandoned.
