---
id: AGNT
type: work
status: needs-verification
labels: [feature, agent, switchboard, ui, architecture]
assignee: claude-code
rank: zzzzzzzzzzx
created: '2026-09-20'
source: Owner, terminal, 2026-09-20, comparing the helper panels with a terminal pane
links: {plans: [], commits: [fe9435b3, 59319d88, f0f9c6b7, 34ff0830, bb1221a2, ece42d07, ca4e5162, de54a761, cbf06cc0, dd104c3b, 24042b5b, 81e889c1, 137f748f, 1265b76f, 7a35a498, 769cc4a4, 694470f8, fe9eea62, '28581709', c4a1625d, 21fbcc5b, bc2db91e, cb0e7d5e, 67837ec9, 6e0179f2, 0df2b685, b8453c97, d52a9d54, db186adc, d0c974aa, 6f0f80a7, 999a8398, 7c51b9dc, b62b9f34, 37eae815, efe55518], evidence: [docs/qa_evidence/2026-09-21-agents-are-consoles], related: [FEJQ, 8YQ9, PBX1, PK5Q, H6VQ, BRD3, R5TC, GH5T, N8VK], github: null}
---
# An agent is the prompt box: one agent surface, one context per setting, and the helper becomes a pane

## Issue

the queue doesn't work like the main terminal, and the thinking bubbles don't work the same way.
why not just make it feature equal with the terminal agent?

— and, when asked whether the helper's panels should stop being a second implementation and simply
be a `Pane`: "yes, do it."

Then, the same evening:

eventually we might think of agents as a more general separable system in relay, and then the
context behind them changes — a terminal, a switchboard card, a project management page, an options
menu, etc.

lets actually do that now. an agent interface is the prompt box. it has a set of options and tools
that vary according to the setting/task, but in general they are shared systems.

more generally, agents are specialized for the given pane context, but the general rule/approach is
that agents have access to all systems and can work across panes and contexts.

## Decisions

- **The agent interface is the prompt box** (owner, 2026-09-20: "an agent interface is the prompt
  box. it has a set of options and tools that vary according to the setting/task, but in general
  they are shared systems"). So the composer frame and its chips, the queue strip, the transcript
  with its thinking bubbles, tool rows and folds, the Activity hook, prompt history, voice, the
  model picker, the hints, Esc/Stop, the worker connection and the persisted conversation are **one
  unit** — `relay::AgentConsole` — and not a feature of the terminal that other surfaces copy.

- **A context is what the agent is about, and it specialises but does not fence** (owner,
  2026-09-20: "agents are specialized for the given pane context, but the general rule/approach is
  that agents have access to all systems and can work across panes and contexts"). A context
  supplies a brief, defaults, an action row of things that need no typing, link resolvers, a
  persistence key and where a finished turn's output goes. It carries **no tool whitelist**. The
  Sessions helper may change an option; the Options helper may open a card; a terminal agent may
  reorganise the board. The only gates are the two that already exist and are the owner's:
  `settable` / `agent_safe` markers on the catalog rows, and the Options › Agent toggle "Agents
  may change options and run actions" (#FEJQ decisions 1–3).

- **The helper stops being a second implementation.** `HelperChatPanel` renders Markdown into a
  rebuilt `QTextBrowser`, prints thinking as a truncated inline tail and never draws a tool row at
  all (`src/HelperChat.cpp:1199-1292`, `:245-272`, `:2343-2347`); its queue is the worker's
  `board_chat_queue_*` FIFO with remove and reorder and nothing else. A pane's queue has steers,
  escalation, withdrawal and the request ledger (protocol §12.5). That gap is the Issue, and
  closing it by copying features into a second widget is what this card refuses to do.

- **The terminal is one context, not the base case.** `Pane` keeps the shell, the routing
  (`auto`/terminal/agent), the workspace and the cwd chips, and hosts an `AgentConsole` with a
  `TerminalContext`. Afterwards a terminal pane must behave as it does today — that is the thing
  not to break.

- **Every new surface is one file.** A project-management page, a diff view or anything else the
  owner names later supplies a `Context` and gets the whole prompt box: the queue, the bubbles,
  the tool rows, Activity, history, voice, the model picker, slash commands.

## Plan

### Goal

Split the agent out of the terminal. One **agent surface** — `relay::AgentConsole`, the prompt box
and everything that belongs to it — and one **context** per setting, a small interface that says
what the agent is about. `Pane` becomes the surface's first host, with a `TerminalContext`; the
Switchboard board, a card, Options/Actions and Sessions become four more contexts, and the helper's
panels, its worker protocol and its model box are retired. Nothing in a terminal pane changes.

### The principle (owner, 2026-09-20)

> "an agent interface is the prompt box. it has a set of options and tools that vary according to
> the setting/task, but in general they are shared systems."

> "agents are specialized for the given pane context, but the general rule/approach is that agents
> have access to all systems and can work across panes and contexts."

Read together those two sentences fix the seam:

- **What is shared is everything about *how* you talk to an agent.** One composer, one queue, one
  transcript, one model box, one history, one voice chip, one Esc. A behaviour added to the prompt
  box appears in every context at once, which is the whole reason for the card.
- **What varies is what the agent is *about*.** The brief in front of the turn, the defaults (which
  role, which routing, whether there is a shell), the action row of things that need no typing, how
  a link in the answer resolves, where the conversation is kept, and what happens to a finished
  turn's output.
- **What must not vary is the tool set.** A per-pane allowlist would re-create the fence the owner
  just took down: today's Sessions helper could not open a pane until #H6VQ, because "opening a
  pane" had been marked unsafe. The context interface therefore has no `tools()` method, and the
  backend attaches the same tool set to every agent. Three things still withhold tools, and they
  are constraints, not fences (below).

### The two interfaces

**`relay::AgentConsole` — the agent surface** (`src/AgentConsole.h`, later `+ .cpp`). Everything the
prompt box is: the composer frame, the chip row (cwd/work/context/quota/effort/model/mic/cancel),
the busy line, the queue strip and its delegate, prompt history, slash commands, the `@` and `#`
pickers, the ask, voice, the transcript writer (`printInline` → `MarkdownAnsi` → `WordWrap` →
the surface), the tool-call rows and OSC 8 folds, the reasoning fold, the Activity ledger, the turn
panes, the recap, the model/effort pickers, the worker process and every message on it.

It reaches whatever it is drawn on through **`relay::agent::Host`** (`src/AgentHost.h`), a
pure-virtual with one implementation per host. The calls are already few and already named — the
4 631-line agent block of `src/Pane.h` (`:2908-7539`) touches `m_backend` exactly 30 times, and
they group into:

| Host call | Today, in `Pane` |
|---|---|
| `writeTerminal(bytes)` / `columns()` / `atLineStart()` | `Pane.h:12425`, `:5157`, `:5619` |
| `foldExpanded/setFoldExpanded/setFoldContent/toggleFold(uri)` | `:5126`, `:5204`, `:5743`, `:5925` |
| `viewportAtBottom()` / `scrollToBottom()` | `:5556`, `:5565` |
| `screenText()` / `cursorPosition()` | `:5219`, `:5217` |
| `sendText(text)` / `paste()` | `:6427`, `:3887` |
| `shellPid()` / `foregroundProcessId()` / `terminalMode()` | `:6422`, `:6424`, `:6472` |
| `status(text)` / `toast(text, ms)` / `hint(id, text)` | `Pane::status`, `::toast`, `::hint` |
| `bubbleRoom()` / `setBubbleHeight(w, px)` | `:5529`, `:5539` — the one place an embedded host answers differently |

**`relay::agent::Context` — what the agent is about** (`src/AgentContext.h`, QtCore only so the
pane libraries can link it without the app). Deliberately small, and deliberately **without a
`tools()` method**:

```cpp
struct ContextSpec {            // the data half: it rides on `configure`
    QString name;               // "terminal" | "switchboard" | "card" | "options" | "actions" | "sessions"
    QString agentRole;          // "main" | "switchboard" | …  (protocol 13.1)
    QString workspace;          // empty when the setting has none
    QString persistScope, persistKey;   // where the conversation is kept (§30.7's (workspace, tab))
    QJsonObject brief;          // {key, title, screen} — the paragraph, and "On screen now:" (§30.7)
    bool shell = false;         // the terminal context is the only one that spawns one
    QString routing;            // "auto" (terminal) | "agent" (everything else)
};

class Context {                 // the behaviour half: the host supplies it
  public:
    virtual ~Context() = default;
    virtual ContextSpec spec() const = 0;
    virtual QList<Action> actions() const { return {}; }   // the no-typing row above the box
    virtual bool resolveLink(const relay::links::Target &) { return false; }
    virtual void turnFinished(const TurnRecord &) {}       // e.g. a card Discuss turn → the thread
    virtual QString placeholder() const = 0;
};
```

An `Action` is `{key, letter, label, tooltip, run}` — exactly what `PBX1` settled for the action
row ("actions the agent can take that dont require typing… left-aligned… every action has a
letter"), so `AgentConsole` builds that row from `Context::actions()` and `HelperChatPanel::
addToolWidget` / `adoptActionButton` (`src/HelperChat.cpp:906`, `:934`) and `boardCardActions`
retire into it.

### The contexts, and what each supplies

| Context | Role | Shell | Persist key | Action row | Links it resolves | Turn output |
|---|---|---|---|---|---|---|
| **Terminal** (`src/Pane.h`) | `main`/`flash`/`local` | yes | the pane's own session (`m_scrollbackId`) | — | path, url, `#ID` | the transcript |
| **Switchboard board** (`src/BoardPane.cpp`) | `switchboard` | no | (workspace, tab) | Check (k), Clean up (u), Tests, Profile | `card:`, `option:`, `session:`, path | the transcript; the survey offer |
| **Card** (`src/BoardPane.cpp`) | `switchboard` | no | (workspace, card id) | Plan (p), Execute (x), Verify (v) | as the board | **also appended to `issues/threads/<ID>.md`** |
| **Options / Actions** (`src/SettingsPane.cpp`) | `switchboard` | no | (workspace, tab) | — | `option:` reveals the row | the transcript |
| **Sessions** (`src/Conversations.cpp`) | `switchboard` | no | (workspace, tab) | — | `session:` reveals/opens | the transcript |

A sixth — the project-management page the owner named — is one more file implementing `Context`,
and nothing else.

### Findings — the surface (GUI)

**`Pane` is one class of 16 560 lines and the agent is a demarcated but scattered part of it.**
`class Pane final : public QWidget` — `src/Pane.h:430`, ctor `:458` (`workspace, cwd, cleanShell,
engineCore` — no window, no tab, no id; it mints `m_token` at `:467`). `buildUi()` `:3995` lays one
`QVBoxLayout`: header `:3999-4070`, terminal host `:4071`, queue strip `:4243-4249`, program
transcript `:4234`, composer `:4076-4334`, subagents `:4335`, jobs `:4336`. The agent's own region
is banner-marked at `:2908-7539` ("agent sessions UI"), but `placeQueueStrip` `:15404`,
`rebuildQueueStrip` `:15280`, `printInline` `:12630`, `handle()` `:9928`, `requestRoute` `:9722`,
`handleComposerKey` `:13394` and the member block `:16150-16560` are outside it. So the extraction
is a **set of anchored members**, not one brace-matched block — which is exactly the shape
`scripts/split-main.py` was written for (content anchors, brace matching, `--check` rebuilds the
original).

**Nothing in `Pane` declares `Q_OBJECT`** (`src/RelayWindow.h:1529` relies on it), so the move needs
no moc change, and `Pane` is `final`, so the helper cannot subclass it — composition is the only
option and is the one wanted.

**There is no agent-only mode today.** `grep agentOnly|agent_only|noShell|paneKind` finds nothing.
`m_native` (`:16203`, `setNative()` `:15750`) hides the *composer*, not the shell; `HideReason`
(`:457`) is about the prompt box. The shell is spawned by `startTerminal(cleanShell)` `:9521`,
called unconditionally from the ctor `:473`; the backend widget is created at `:9585-9588` and the
**pty only at `:9690` / `:9704`** (`m_backend->startProgram`). So "a transcript surface with no
shell" is a split inside one function, not a new rendering path: the helper keeps the vterm host and
the whole ANSI/fold/OSC 8 transcript, and simply never calls `startProgram`.

**`configure` already has the shape a context needs.** `withSessionFields(request)` `:4677` is the
one funnel; it already inserts `agent_role` `:4717`, `board` (via `onBoardSettings`) `:4723` and
`app` (via `onAppCatalog`) `:4727`. A `context` block is one more line there.

**A `Pane` cannot be constructed by the pane libraries.** `relay-board`, `relay-settings` and
`relay-conversations` are static libraries (`CMakeLists.txt:69`, `:134`, `:268`); `Pane` lives only
in the `relay` executable's single translation unit (`CMakeLists.txt:508-510`). So the embedding
hosts **must not** `new` a console: `RelayWindow` makes it and hands it over as an opaque
`QWidget *` plus a small handle, exactly as `relay::PaneView` (`src/PaneView.h:10`) already lets a
`ToolPane` chrome a view it knows nothing about.

**Embedding hazard: `panesIn` recurses through a `ToolPane`'s whole subtree.**
`RelayWindow::isLeaf` `:6857` is `Pane* || ToolPane*`, and `leavesIn` `:6859-6870` stops at the
leaf — so navigation, tab titles, chrome, the printed pane count and the closed-pane list are all
safe. But `panesIn` `:6922-6934` falls through to
`root->findChildren<QWidget*>(…, FindDirectChildrenOnly)` at **`:6932`**, so a console nested under
`BoardView` (today `new BoardChatPanel(m_listPane)`, `src/BoardPane.cpp:4443`) **would** be returned
by `allPanes()`. That breaks six things at once:

| Site | What breaks |
|---|---|
| `syncAlwaysOnShares` `:7631`, `syncTabShares` `:7604` | the helper is **published to the phone** as its own inbox row |
| `closeEvent` `:1007-1010` vs `confirmClose` `:9448` | the two walkers disagree; every close prompts |
| `createPane` `:6958` | `!allPanes().isEmpty()` makes the **first real terminal default to Flash** |
| `savePaneScrollbacks` `:529` vs `windowstate::scrollbackIds` `src/WindowState.cpp:340-368` | write-then-prune churn every save |
| `paneWithToken` `:585` / `findPaneByToken` `:5812` → `revealPane` `:594` | a notification resolves to a non-leaf and `setActiveLeaf` is handed one |
| `repointTabPanes` `:6324` | the helper gets `set_board` meant for terminal agents |

The cheapest fix is the one `leavesIn` already uses: stop `panesIn` descending at a `ToolPane`.

**Restore is safe by omission, and persistence must ride the tab.** `serializeNode`
`src/RelayWindow.h:7272` returns at the `Pane` branch `:7273` and at the `ToolPane` branch `:7296`
without descending, and `windowstate::isUsableNode` (`src/WindowState.cpp:154-192`) knows only
`split|pane|board|settings|testsuites|subagents|explorer|preview|plan`. So a nested console is never
written and never rebuilt — the host recreates it. `ToolPane::node()` (`src/PaneChrome.h:497-540`)
already returns `{}` for Turn, Diff, Info and Sessions. The conversation therefore persists where
#FEJQ already put it: keyed by the tab's `tab_id` (`src/WindowState.h:97-103`, written
`src/RelayWindow.h:7424-7430`), under `$XDG_DATA_HOME/relay/helper-sessions/` (§30.7).

**Sizing: an embedded console is short, and `Pane` measures against its own height.**
`bubbleRoom()` `:5529` subtracts header + composer + two terminal lines from `height()`, and
`placeQueueStrip` `:15409` hides the strip outright when `roomForBubble(0)` is false — so in a
320 px panel **the queue strip would silently never appear**, which is half the Issue. Eight
`place*` overlays are positioned against `m_terminalHost` with hard minimums
(`placeTakeControl` `:15416` `max(240,…)`, `toggleHelpPopup` `:9270` `max(360,…)`,
`placeGuestBar` `:2446`, `placeRequestsPanel` `:9342`, `placeSubagentsPanel` `:8978`,
`placeToast` `:11073`, `placeAtPopup` `:14368`, `placeCardPopup` `:14278`). `m_editor->
setAutoHeight(1, 8)` `:4193` lets a long draft squeeze the transcript to nothing, where
`HelperChat.h:28-33` deliberately caps its log at ~40 % of the panel. `updateTranscriptHeight()`
`:12530` takes 40 % as well. This is the real embedding work, and it is all in `bubbleRoom()` and
the `place*` family — which is why they are `Host` calls.

**The phone reads none of this today.** No `board_chat`, no `helper`, no `chat: true` anywhere in
`remote/`, `app/` or `src/Remote*`; `board_chat*` is in neither `FORWARDED_EVENTS`
(`remote/wire.py:277-287`) nor `WITHHELD_EVENTS` (`:344-364`), so it is dropped by the allow-list
default. `docs/REMOTE-AND-MULTIPLAYER-DESIGN.md:139-163` has no helper row, and `:438-458` records
that the phone renders the agent's answer out of the *screen* stream because Relay prints it into
the pane's terminal. So a helper that becomes a `Pane` printing into a vterm is, if anything, the
shape the phone already understands — the only risk is publishing it by accident (above).

**What the helper has that a pane does not**, and where each goes:

| Today | File | Becomes |
|---|---|---|
| Check / Clean up / Tests / Profile | `HelperChatPanel::addToolWidget` `src/HelperChat.cpp:906`, `adoptActionButton` `:934` | `Context::actions()` and one action row in `AgentConsole` |
| Plan (p) / Execute (x) / Verify (v) | `boardCardActions`, `src/BoardPane.cpp` | `CardContext::actions()` |
| the findings list (Check) | `showFindings` `src/HelperChat.cpp:1559-1638` | stays a board widget **above** the console; a click still drafts `board::fixRequest` (`src/HelperChat.h:74`) into the composer |
| the survey offer | `showSurvey` `:1649-1858`, `applyImport` `:1962` | stays a board widget above the console (it is an import form, not a turn) |
| `option:` / `session:` / `card:` links | one lambda, `src/HelperChat.cpp:585-617` | `relay::links::Kind` gains `Option` and `Session` (`src/OutputLinks.h:29`), resolved through `Context::resolveLink` |
| the collapsed "? Helper Agent (Alt+Q)" row | `m_askRow` `:445-468`, `updateAskRow` `:820` | a fold the **host** owns (`SettingsPane`, `SessionManager`); the console is created on first expand |
| the panel-side queue | `m_queue` `src/HelperChat.h:325`, `rebuildQueue` `:1460` | retired — the pane's §12 queue strip, which is what the Issue asks for |
| the model box | `relay::helpermodel` `src/HelperModelBox.h:30-72` | retired into `Pane::refreshPickers` `src/Pane.h:11514` / `relay::modelrows` (#PK5Q already made the rows one builder) |

### Findings — the worker

**There is no helper binary. A helper worker *is* `backend/worker.py`.** `relay::BoardWorker::
startProcess` runs `<data>/backend/worker.py` (`src/BoardWorker.cpp:119`) and
`RelayWindow::startBoardWorker` configures it with `agent_role: "switchboard"`
(`src/RelayWindow.h:6607`). Every pane message — `ask`, `cancel`, `queue_*`, `sessions`,
`session_info`, `recap_request`, `set_model`, `set_agent_role`, `presets` — is already reachable on
its stdin. **The convergence is mostly deletion, not new protocol.**

**A helper worker already builds a full pane agent and never asks it anything.** `worker.py:305`
constructs `Agent(config, workspace, turns.agent_emit, …)`; the GUI never sends `ask` to a
`BoardWorker`, so that agent exists only for `board_protocol._build_page_agent`
(`board_protocol.py:693-762`) to copy `config`, `roles`, `executor.skills`, `executor.policy` and
`instructions` off it (`:707-759`). The second agent — `board_chat.PageAgent`
(`board_chat.py:362`) — is the one that answers, driven by its own thread (`:583-616`) rather than
by `TurnSupervisor` (`backend/relay_core/queue.py:421-461`).

**`agent_role` selects a provider and nothing else.** `worker.py:236` validates it, `:265-268`
applies the #GH5T guest rule (`roles.leave_guest`, `roles.py:903`), `:283` swaps the config. It does
not change the tool set, the prompt, the persistence or which code path runs the turn
(`roles.py:67-127`: `switchboard` is tier `main` like `subagent`, with no tools and no prompt of its
own).

**The two queues, side by side** (§12 is the *request ledger*; the pane queue is
`docs/QUEUE-INTERRUPT.md:40-60`):

| | pane (`queue.py`) | helper (`board_chat.py`) |
|---|---|---|
| submit | `ask {id, text, when: now\|queue\|interrupt\|steer, context?, attachments?, cards?, skills?}` | `board_chat {id, text, pane?, model?, context?, survey?}` |
| turn boundary | `agent_started {id, turn_id}` … `agent_finished {id, outcome}` | none — the panel infers it (`src/HelperChat.cpp:2312-2325`) |
| queue view | `queue_changed {running, paused, items[], steering[]}` `queue.py:407` | `board_chat_state {pane, chat{…queue[], history[]}}` `board_chat.py:434` |
| cap | 32 (`queue.py:26`) | 20 (`board_chat.py:51`) |
| item id | `uuid4().hex`, **is** the turn id (`queue.py:445`) | `c1, c2…` counter (`board_chat.py:418`) |
| reorder | ✗ | ✓ `board_chat_queue_move` |
| steer / interrupt / escalate / withdraw | ✓ (`queue.py:260-358`) | **✗** |
| pause, resume, auto-pause on error | ✓ (`queue.py:457`) | **✗** |
| request ledger, todos, completion check | ✓ | **✗** — `track_requests=False, todo_tool=False, completion_check=False` (`board_protocol.py:749`) |
| subagents | ✓ (`worker.py:335`) | **✗** |
| `session_info` / `activity` tools | ✓ (`worker.py:333`) | **✗** — "the helper worker's agents never get them" (`worker.py:330-331`) |

That table is the Issue, itemised.

**What withholds tools by pane today — which are constraints, which are fences.** The owner's rule
("agents have access to all systems and can work across panes and contexts") makes this the list to
act on:

*Real constraints, kept:*
- **No board → no `board_*` tools** (`board_protocol.py:710-716`): there is nothing to act on, and
  a `switchboard` ask in a board-less tab is already answered in a sentence
  (`NO_BOARD_CHAT_ERROR`, `board_protocol.py:94`).
- **A guest harness cannot run Relay's tools** (#GH5T, #4NXH), so the helper never starts one
  (`worker.py:265-268`). Unchanged.
- **A card's Plan turn may only write `## Plan`** (`board_tools.py:1527-1530`). That is the stage
  machine of §19.20, not a per-pane fence.

*Fences to remove:*
- **`ChatScope` gives the helper no shell and no file writes** (`board_tools.py:919-951`; §19.18:
  "No shell, no file writes: code is a card's Execute"). Under the new rule an agent in Options may
  act across contexts; whether the *helper* keeps this particular limit is the owner's
  (decision 3 below).
- **`activity_tools.attach` is pane-agent-only** (`worker.py:330-333`) — every agent should be able
  to answer "why was that turn slow" about itself.
- **`track_requests` / `todo_tool` / `completion_check` off** (`board_protocol.py:749`) — this is
  precisely "not feature equal".
- **`_deferred_groups` returns `()` for `helper=True`** (`agent.py:845-871`), so a helper never
  gets §12.13's `load_tools`.

*And one accident, either way a bug:* `Agent.tools()` branches on
`getattr(self.board, "card_scope", None)` (`agent.py:979`), so a **board-less** helper takes the
*pane* branch and silently gets the full executor — shell, `write_file`, `edit_file` — while a
boarded one gets read-only tools. The scope must be named, not inferred.

**What a helper worker still needs to be an ordinary pane worker** (each is small):

1. **A session id on `configure`.** Persistence is chosen inside `_build_page_agent`
   (`board_protocol.py:730`, `board_chat.helper_dir` / `helper_session_id`, `board_chat.py:183-198`);
   `configure` has `session_dir` but no field for the id. Either add `session_id`, or keep `tab` and
   have `worker.py` call `agent.adopt_session(helper_session_id(tab))` (`agent.py:3526`).
2. **A named tool scope**, so `Agent.tools()` stops inferring one (above).
3. **`helper: true`**, or derive it from `agent_role == HELPER_ROLE` (`agent.py:861`).
4. **The brief.** Today it is prefixed to the *prompt text* per turn
   (`board_chat.pane_prompt`, `:225-236`) with a once-per-pane rule (`:491-495`). As a pane worker
   it becomes part of `configure`'s context block and the system prompt, re-sent when the context
   changes — which is also what makes a brief visible to `session_info`.
5. **`surface` routing on events**, if one worker keeps serving several consoles (owner decision 1).
6. **A name for the on-screen hint.** `ask` already has a `context` field, but it is the
   program/terminal context object (`queue.validate_context`, `agent.py:1742`); §30.7's `context`
   is a 2 000-character string. Rename the new one `screen`.
7. **A read-only turn flag** for the survey (`board_chat.py:508`).

**What a card turn needs that a plain turn does not** (`board_protocol.py:1893-1946`, `:774-798`):
the card id and mode on the request; the owner's words appended to the thread **before** the model
sees them (`:1916`); the answer appended **after**, tool fragments stripped, with `model=` and
`turn=<session>/<turn>` provenance (`:785-786`); a stage advance on both ends (`:1922`, `:791`); a
re-seed when the card file's hash moves (`:1932`); a per-mode brief (`board_turns.py:64`); one turn
per card rather than one per worker (`board_turns.py:143`); `board_cancel {card}` (`:1948`); and an
LRU of six conversations (`board_turns.py:44`). Items 1–4 and 6–7 are genuinely card-specific and
survive as a `CardContext`; 5, 8, 9 and 10 are the pane's own mechanisms under other names.

**Dead and duplicated names to clean up on the way:** `turn_started` is tagged by
`board_chat.py:63` and `board_turns.py:51` and consumed by `src/HelperChat.cpp:2352`, but **nothing
emits it**. `chat` is overloaded — `true` on turn events, an *object* on `board`,
`board_chat_started`, `board_chat_state`, `board_chat_cancelled`. `board_chat_queue_remove` /
`_move` answer with no request id (`board_protocol.py:1684-1691`), unlike the pane's `queue_remove`.
`PageAgent.ask`'s docstring disagrees with its return value (`board_chat.py:441-460`).

### Steps

One subagent per step. The file sets are disjoint; where two steps share a file they are the same
session's and are numbered in order. Every step lands through
`python3 scripts/land.py begin <me> <its files>` … `commit`, builds through `scripts/relay-build`,
and runs only its own tests (WARP.md). **Nobody but step 1/3 opens `src/Pane.h`; nobody but step 5
opens `src/RelayWindow.h`.**

Order: **A** = 1, 2, 4, 8 in parallel · **B** = 3, 5 · **C** = 6, 7 in parallel · **D** = 9, 11 ·
**E** = 10.

---

**Step 1 — Extract the agent console out of `Pane`. The one step that touches `src/Pane.h`.**
*Files:* `src/Pane.h`, new `src/AgentConsole.h`, new `src/AgentHost.h`, new
`scripts/split-agent-console.py`, `CMakeLists.txt` (the `relay` target's source list only,
`:508-519`).
*Do not touch:* anything else, and in particular not `src/RelayWindow.h`.

This is a **move, not a rewrite**, done the way `scripts/split-main.py` did it: every region is
found by an exact content anchor, never a line number; each member is brace-matched with an
awareness of strings and comments; `--check` rebuilds the original `Pane.h` from the two new files
and exits non-zero if one line was dropped or duplicated. The only text that changes is the
receiver: `m_backend->x()` becomes `host().x()`. Write the tool first and let it do the moving —
the file is edited by other sessions and a hand-move cannot be proved complete.

`relay::agent::Host` (`src/AgentHost.h`) is the pure-virtual in the table above; `Pane` implements
it. `Pane` keeps **every public member it has today** as a one-line inline forwarder to
`m_agent`, so `src/RelayWindow.h` and every existing test compile unchanged — that is the property
that makes the step reviewable.

Five waves, **each its own commit through `land.py`'s build gate**, so `main` never holds a
half-moved class:

| wave | what moves | anchors |
|---|---|---|
| 1a | `AgentHost.h`, the empty `AgentConsole`, `Pane` implements `Host` and owns `AgentConsole m_agent{*this}` | — |
| 1b | transcript writer, inks, ANSI/markdown/wrap, block gaps, tool-call rows, OSC 8 folds, the reasoning fold, the Activity ledger, the turn panes | `:5071-6000`, `:12365-12870` |
| 1c | composer frame, chip row, busy line, voice, model/effort pickers, slash / `@` / `#` popups, hints, prompt history | `:4076-4334`, `:4342-4460`, `:6543-6790`, `:11346-11600`, `:13394-13700` |
| 1d | queue entries, steers, the queue strip and `QueueRowDelegate` | `:167-221`, `:8555-8760`, `:13129-13400`, `:15257-15420` |
| 1e | the worker process, `withSessionFields`, `handle()`, `handleSessionEvent`, the ask, recap, `session_info` | `:3098-3500`, `:4677-4790`, `:6881-7539`, `:9427-9520`, `:9714-10543` |

*Tests:* the pane's existing suites, **unchanged and still passing** — that is the verification.
`ctest --test-dir build -R '^queuenav$|^queuesubmit$|^continueturn$|^input$|^slash$|^markdown$|^calllines$|^internalsledger$|^toollabel$|^panestate$'`, plus a build of `relay` itself after every wave.
*Live:* Xvfb side-by-side before/after on the same isolated `XDG_CONFIG_HOME` — a turn with thinking,
a tool call with its fold, a queued second prompt, a steer and its ×, Esc, `/model`, the mic, a
`#ID` and a file link. Screenshots of each wave under the evidence folder.

---

**Step 2 — `relay::agent::Context`, `ContextSpec` and `Action`.** *(parallel with step 1)*
*Files:* new `src/AgentContext.h`, `src/AgentContext.cpp`, new `tests/agentcontext_test.cpp`,
`CMakeLists.txt` (a `relay-agentcontext` library and its test target — QtCore only, so
`relay-board`, `relay-settings` and `relay-conversations` can link it without pulling in a window).
*Do not touch:* `src/Pane.h`, `src/RelayWindow.h`.
The interface as written above, plus `ContextSpec::toJson()` / `fromJson()` — the exact bytes of
`configure`'s `context` block, so the C++ and the Python are tested against one shape. Also the
`Action` struct and the rules `PBX1` settled: left-aligned, buttons only, one free letter each,
`fullLabel` for the narrow case. No `tools()` method, ever: the header says why in a sentence.
*Tests:* `ctest --test-dir build -R '^agentcontext$'` — round-trip, a spec with no workspace, a
duplicate action letter refused, the narrow-row label shed.

---

**Step 3 — `AgentConsole` takes a `Context`; `Pane` supplies `TerminalContext`.**
*Files:* `src/AgentConsole.h`, `src/Pane.h`. *(the same session as step 1, after step 2 lands)*
`AgentConsole` gains `setContext(Context *)`, builds the action row from `Context::actions()`,
routes an activated link through `Context::resolveLink` before its own handling, puts
`spec().toJson()` into `withSessionFields` as the `context` block, and calls
`Context::turnFinished` on `done`. `TerminalContext` — shell `true`, routing `auto`, role from
`m_agentRole`, persist key `m_scrollbackId`, no actions, the existing link behaviour — is what
`Pane` hands it, so the terminal keeps every byte of today's behaviour.
Also in this step, because they are `Host` calls and only `Pane` knows the answer today:
`bubbleRoom()` (`:5529`), `setBubbleHeight`, and the eight `place*` overlays (`:15416`, `:9270`,
`:2446`, `:9342`, `:8978`, `:11073`, `:14368`, `:14278`) move behind `Host` so an embedded host can
answer differently — **this is what stops the queue strip silently vanishing in a 320 px panel.**
*Tests:* the pane suites again, plus a new `agentcontext` case that a terminal spec round-trips into
the configure block.

---

**Step 4 — The worker speaks one protocol.** *(parallel with step 1)*
*Files:* `backend/worker.py`, `backend/relay_core/board_chat.py`,
`backend/relay_core/board_protocol.py`, `backend/relay_core/board_turns.py`,
`backend/relay_core/board_tools.py`, `backend/relay_core/agent.py` (the `tools()` branch at `:979`
and `_deferred_groups` at `:845`), `backend/relay_core/roles.py`,
`docs/AGENT-SESSIONS-PROTOCOL.md`, `tests/test_board_chat.py`, `tests/test_board_protocol.py`,
`tests/test_board_turns.py`, `tests/test_app_tools.py`, new `tests/test_agent_context.py`.
*Do not touch:* any `src/` file.

1. `configure` gains `context {name, agent_role, workspace, persist{scope,key}, brief{key,title},
   scope, shell, routing}` (§1, new §31). `worker.py` reads it, calls
   `agent.adopt_session(...)` from `persist`, sets the named tool scope, sets `helper` from the role,
   and puts the brief in the system prompt rather than in front of every prompt.
2. `ask` gains `surface` (echoed on every event of that turn) and `screen` (the ≤2 000-character
   on-screen hint — **not** `context`, which is already the program-context object).
3. **`PageAgent` is deleted.** Its behaviours move to where the pane already has them: the queue to
   `TurnSupervisor` (which gains `move`, since the helper has reorder and the pane does not), the
   `chat.history` redraw to the ordinary session file, the "said nothing after an app call"
   fallback (`board_chat.py:677-692`) to `Agent` for every agent, the survey's read-only turn to a
   `readonly` flag on `ask`.
4. `board_chat`, `board_chat_cancel`, `board_chat_queue_remove`, `board_chat_queue_move`,
   `board_chat_started`, `board_chat_queued`, `board_chat_state`, `board_chat_cancelled` and the
   `chat: true` tag are **retired**; §19.18 becomes "the board is a context" and §30.7 keeps only
   what is still true (one worker per tab, the tab key, the guest rule, worker-death recovery).
   `board_survey` stays — it is an import form, not a turn.
5. `CardTurns` keeps the card-specific half (thread write before and after, stage advance, seed
   hash, per-mode brief, per-card cancel, the LRU) and loses its queue and its own event tagging to
   the pane's.
6. The fences listed in Findings come down: `activity_tools` attach on every agent, the scope is
   named rather than inferred, `track_requests` / `todo_tool` / `completion_check` follow the
   ordinary options, `_deferred_groups` stops special-casing `helper`.
7. Delete the dead `turn_started` tag; give the queue ops a reply with their request id.
*Tests:* `python3 -m unittest tests.test_board_chat tests.test_board_protocol tests.test_board_turns
tests.test_app_tools tests.test_agent_context` — with `RELAY_KEYRING=off`.

---

**Step 5 — The window makes consoles, and stops making helper panels.**
*Files:* `src/RelayWindow.h`, `tests/boardworkspace_test.cpp`.
*Do not touch:* `src/Pane.h`, `src/BoardPane.*`, `src/SettingsPane.*`, `src/Conversations.*`.

1. `AgentConsole *createAgentConsole(const ContextSpec &, QWidget *leaf)` beside `createPane`
   (`:6942`): builds the console, wires the same callbacks `createPane` wires (status, open path,
   open card, hints, the model box, the app catalog, the board block), and attaches it to the tab's
   worker. The pane libraries never construct one — they are handed a `QWidget *` and a handle,
   the `relay::PaneView` pattern (`src/PaneView.h:10`).
2. **`panesIn` stops descending into a `ToolPane`** (`:6932`), the way `leavesIn` already does
   (`:6857`). This one line is what keeps an embedded console out of `syncAlwaysOnShares` `:7631`,
   `syncTabShares` `:7604`, the close-dialog count `:1007`, the Flash default `:6958`, the
   scrollback prune `:529`, `paneWithToken` `:585` and `repointTabPanes` `:6324`.
3. `wireHelperPanel` (`:1305-1400`), `listenToHelper` (`:6455`), `sendToHelper` (`:6464`),
   `deliverToHelperPanels` (`:6500`), `m_helperListeners`, `m_helperModels`, `pickHelperModel`,
   `helperModelState`, `helperSlashCommand`, `openHelperModelBox`, `focusedHelperModelBox` go.
   `focusHelperOfActiveLeaf` (`:1557`) stays and focuses the console. `helperWorker`,
   `startBoardWorker`, `helperWorkerGone` and `m_boardWorkers` stay — the worker is unchanged.
   The five non-panel `sendToHelper` callers (Test suites `:4951`, Profile `:5058`/`:5099`, the
   board writes `:5168`/`:5207`/`:5269`/`:5300`/`:5360`, signals `:3289`) keep a plain
   `helperWorker(page, true)->send(...)`.
4. `startBoardWorker` (`:6595`) sends the `context` block for each console rather than a bare
   `agent_role`.
*Tests:* `ctest --test-dir build -R '^boardworkspace$'` — adapt the three cases that read
`RelayWindow.h` as text (`:275`, `:298`, `:316`), and add one that `panesIn` does not descend into a
`ToolPane`.

---

**Step 6 — The board and the card become contexts.**
*Files:* `src/BoardPane.h`, `src/BoardPane.cpp`, `tests/boardmodel_test.cpp`.
`BoardContext` and `CardContext` (in `BoardPane.cpp`, implementing `relay::agent::Context`).
`buildChatPanel` (`:4441-4534`) becomes `buildConsole`: the same place in the column
(`layout->addWidget(console, 0)` at `:4528`, deliberately outside the splitter — `:4061-4064` says
why), the same `syncChatVisible` rule (`:6105`), the Check / Clean up / Tests / Profile buttons
supplied as `BoardContext::actions()` instead of `addToolWidget`. The findings list
(`showFindings`) and the survey offer move out of the panel into board-owned widgets **above** the
console, keeping their object names and their draft-into-the-composer behaviour
(`board::fixRequest`, which moves to `src/BoardPane.h`).
`CardDetail`'s reply frame (`:1867-1922`) is replaced by a console pointed at the card;
`boardCardActions` (`:1815-1866`) becomes `CardContext::actions()` — Plan (p), Execute (x),
Verify (v), unchanged; `CardContext::turnFinished` is what writes a Discuss turn to the thread
(owner decision 2 below). `onReply` (`:4125-4157`) and the card-turn event routing (`:5633-5690`)
go with it.
*Tests:* `ctest --test-dir build -R '^board$|^boardpane$|^buttonfit$'` — the 23 helper cases at
`tests/boardmodel_test.cpp:3174-4761` are rewritten against the console, and the two
`static_cast<relay::HelperChatPanel *>` at `:3825` and `:3923` are the first thing to fix.

---

**Step 7 — Options, Actions and Sessions become contexts.**
*Files:* `src/SettingsPane.h`, `src/SettingsPane.cpp`, `src/Conversations.h`,
`src/Conversations.cpp`, `tests/settingspane_test.cpp`, `tests/conversations_test.cpp`.
The collapsed "? Helper Agent (Alt+Q)" row becomes the **host's** — `m_askRow` and the fold move out
of the panel into `SettingsPane` and `SessionManager`, keeping `boardChatAskRow` / `boardChatAsk` /
`boardChatBody` as object names and the `"Helper Agent (%1)"` wording
(`src/HelperChat.cpp:447-468`, `:798-832`). **The console is created on first expand**, which is
also what keeps a tab that nobody asks anything from paying for one (§30.7, owner decision 5).
`SettingsPane::setMode` (`:427-439`) swaps the context, not a pane string. The `onHelper*` blocks
(`src/SettingsPane.h:257-296`, `src/Conversations.h:244-276`) collapse to one
`setAgentConsole(QWidget *, ConsoleHandle *)` plus `focusHelper` / `helperDraft`.
*Tests:* `ctest --test-dir build -R '^settings$|^conversations$'` — adapt
`theHelperPanelFollowsTheModeAndAsksAsTheOptionsPane`
(`tests/settingspane_test.cpp:1094-1178`) and
`theHelperPanelSitsAtTheBottomCollapsedAndAsksAsTheSessionsPane`
(`tests/conversations_test.cpp:1454-1529`).

---

**Step 8 — `option:` and `session:` become link kinds.** *(parallel with step 1)*
*Files:* `src/OutputLinks.h`, `src/OutputLinks.cpp`, `tests/outputlinks_test.cpp`.
`relay::links::Kind` (`:29`) gains `Option` and `Session` beside `Path`, `Url` and `Card`, with the
two spellings `HelperChat.cpp:585-617` already accepts (`option:sec/row` and `option://sec/row`).
Nothing else moves: the *resolving* is `Context::resolveLink`, written in steps 6 and 7.
*Tests:* `ctest --test-dir build -R '^outputlinks$'`.

---

**Step 9 — Retirement.**
*Files:* delete `src/HelperChat.h`, `src/HelperChat.cpp`, `src/HelperModelBox.h`,
`src/HelperModelBox.cpp`, `src/BoardChat.h`, `tests/helpermodelbox_test.cpp`; edit
`CMakeLists.txt` (`relay-helperchat` `:52-65`, its mention in `relay-board` `:75`/`:83`,
`relay-settings` `:138`, `relay-conversations` `:272`, the test target `:602-605`),
`src/Theme.cpp` (the `boardChat*` block `:673-776` keeps the rules whose object names survive and
loses the rest), `src/AskRow.h:13` (a comment pointing at `src/BoardChat.h`).
Run last, when nothing references them: `grep -rn "HelperChatPanel\|HelperModelBox\|BoardChat"
src tests CMakeLists.txt` must come back empty.

---

**Step 10 — `AgentConsole` gets a `.cpp`, a library and its own tests.** *(last)*
*Files:* `src/AgentConsole.h`, new `src/AgentConsole.cpp`, new `tests/agentconsole_test.cpp`,
`CMakeLists.txt`.
Every library the console needs is already one — `relay-editor`, `relay-prompthistory`,
`relay-modelrows`, `relay-voice`, `relay-markdown`, `relay-calllines`, `relay-toollabel`,
`relay-internals`, `relay-queuenav`, `relay-queuesubmit`, `relay-slash`, `relay-input`,
`relay-outputlinks`, `relay-requests`, `relay-backends` — so `relay-agentconsole` is a link edge,
not a new dependency. This is the de-inlining `CLAUDE.md` warns about, and it is safe **here and
only here**: after step 1 the code lives in a file this workstream owns, so moving it out of the
header touches nobody else's edits. It is also the step that may be dropped without losing
anything the owner asked for.

---

**Step 11 — Docs and hints.**
*Files:* `docs/ARCHITECTURE.md` (§10a "The helper system" `:1621-1690` becomes "Agents and
contexts"; a source-map entry `:3029`), `docs/SWITCHBOARD-DESIGN.md` §4,
`docs/REMOTE-AND-MULTIPLAYER-DESIGN.md` (one line saying a console prints into a terminal host, so
the phone path is unchanged), `WARP.md`'s shortcut-hint rule applied to the new fast paths.
`docs/AGENT-SESSIONS-PROTOCOL.md` belongs to step 4, not here.

### Risks

1. **The terminal pane regressing is the one real risk.** Step 1 moves 4 600 lines of the most-used
   code in the app. Three things hold it: the move is done by a tool whose `--check` rebuilds the
   original from the new files, `Pane`'s public API does not change so every existing test compiles
   and runs unmodified, and each of the five waves goes through `land.py`'s build gate, which
   compiles the exact tree it is about to put on `main` (CLAUDE.md). If a wave cannot be made to
   pass the pane's own suites, it is abandoned rather than patched — the anchors are the record of
   what it was trying to move.
2. **`src/Pane.h` is the hot file and this holds it for a day.** Only one session may take step 1,
   it must `begin` immediately before each wave rather than holding a snapshot (the
   land-py-claim-late rule), and it must say so in the thread so other sessions route around it. The
   file gets *smaller*, which is the mitigation that lasts.
3. **Where the seam is deliberately thin.** `relay::agent::Host` is a terminal-shaped interface —
   `writeTerminal`, `columns`, folds, `screenText` — because every context keeps a vterm host as its
   transcript surface. That is a deliberate trade: it costs one emulator per console and it buys the
   whole ANSI/fold/OSC 8 transcript, the theme repaint, the scrollback and the phone's screen stream
   for nothing. A context that one day wants a different surface (a rich-text document, a canvas)
   implements `Host` instead of `Pane` and touches nothing else — but nobody should pretend the
   interface is surface-agnostic today. `Context` is thin in the other direction on purpose: no
   `tools()`, no widget, no worker.
4. **Four consoles per tab means four emulators per tab.** Today the four helper panels share one
   `QTextBrowser` each and one worker. After this they share one worker and have a vterm each,
   created **on first expand** (step 7) so a tab nobody asks anything pays nothing. Measure it in
   the live check (`relay::usage::Meter` is already per pane) and report the number.
5. **Object names are API.** `boardChat*` is asserted in `tests/boardmodel_test.cpp`,
   `tests/settingspane_test.cpp`, `tests/conversations_test.cpp` and keyed by `src/Theme.cpp:673-776`
   (`src/HelperChat.h:328-331` says so explicitly). Keep the names that describe a surviving widget
   (`boardChatAskRow`, `boardChatAsk`, `boardChatBody`) and retire the rest in step 9 with their
   assertions, in the same commit.
6. **Two casts break at runtime, not compile time.**
   `static_cast<relay::HelperChatPanel *>(findChild("boardChatPanel"))` at
   `tests/boardmodel_test.cpp:3825` and `:3923`. Step 6 fixes them first, before anything else in
   that file.
7. **The phone.** It reads nothing of the helper today and must go on reading nothing by accident:
   the `panesIn` fix in step 5 is the whole mitigation, and the live check must include
   "Options › Remote on, a tab with a Switchboard and an expanded helper — the phone's inbox shows
   the terminal panes and nothing else".
8. **#GH5T stands.** The helper never runs on a guest harness and never starts one
   (`worker.py:265-268`); a `guest:` row in the console's model box is still refused with
   `guest_harness_provider.helper_refusal` (#PK5Q). Nothing in this card changes that, and the live
   check must prove it with the owner's own Main.

### Decisions the owner still has to make

Each is written with the recommendation the plan assumes, so a subagent can start; a different
answer changes only the step named.

1. **One conversation across the four helper surfaces, or one per surface?** #FEJQ settled "the
   conversation is one whatever the pane", and today the panels filter by `pane` so each draws only
   its own turns. With a real transcript surface that filtering becomes visible: the Options console
   would show gaps where the board's turns were.
   **Recommendation: keep one conversation, and draw *all* of it in every helper console.** It is
   what "one conversation" means, it deletes the `pane`-tagging machinery and with it the whole
   class of bug #H6VQ was (a panel that drops an event addressed to somebody else and then waits
   forever), and a filter is one predicate to add back if he dislikes it. Changes step 4 item 2 and
   step 7 only. *If he wants one conversation per surface instead*, `Context::persistKey` already
   carries it and nothing else moves.
2. **On a card, is Discuss still a turn written to the card thread?** Today it is: the owner's
   words are appended before the model sees them and the answer after
   (`board_protocol.py:1916`, `:774-798`), which is what makes `issues/threads/<ID>.md` the record
   (POLICY rules 2, 4 and 7 — the thread is append-only and is what a verifier reads).
   **Recommendation: yes — keep the thread writes, and give the card a real conversation behind
   them.** `CardContext::turnFinished` appends the answer exactly as `_card_answer` does now,
   including the `model=` and `turn=` provenance; what the card gains is the queue, the bubbles,
   the tool rows and a history that survives instead of re-seeding on every file-hash change.
   Plan / Execute / Verify stay the action row above the box (#PBX1). Changes step 6.
3. **Does a helper agent get the shell and the file-write tools?** §19.18 says no — "No shell, no
   file writes: code is a card's Execute" — but that predates "agents have access to all systems
   and can work across panes and contexts", and a board-less helper already gets them by accident
   (`agent.py:979`).
   **Recommendation: yes, with the workspace it has, and never for a card's Plan turn.** The reason
   the limit existed was that a board conversation had no business editing code; the reason it
   should go is that the owner has just said a context specialises an agent without fencing it, and
   the Options › Agent toggle plus the `agent_safe` markers are the gates he chose. The card's
   Plan-writes-only check (`board_tools.py:1527`) is a stage rule and stays either way. Changes
   step 4 item 6. *If he says no*, the named scope of step 4 item 2 carries it and the accident is
   still fixed.
4. **Should a helper console be shareable to a phone?** It never has been, and the plan keeps it
   that way (step 5 item 2).
   **Recommendation: not in this card.** The phone has no board UI at all
   (`docs/REMOTE-AND-MULTIPLAYER-DESIGN.md:203` lists it as later), so a helper row in the inbox
   would open onto a screen with nothing behind it. File it as its own card if he wants it.
5. **Is step 10 worth doing?** `AgentConsole` as a real library with its own tests is what "its own
   unit" means, and it is the de-inlining `CLAUDE.md` warns about — though on a file this workstream
   created rather than on `Pane`.
   **Recommendation: do it, last, and drop it without regret if anything above runs long.**
   Everything the owner asked for works with the console as a header in the one translation unit.

### Verify

**Per step**, the tests named in that step and nothing more (WARP.md: the suites are the owner's).

**End to end**, once steps 1–9 have landed:

- `ctest --test-dir build -R '^board$|^boardpane$|^boardworkspace$|^settings$|^conversations$|^agentcontext$|^outputlinks$|^queuenav$|^queuesubmit$|^continueturn$|^input$|^slash$|^markdown$|^calllines$|^internalsledger$|^panestate$|^modelrows$'`
- `RELAY_KEYRING=off python3 -m unittest tests.test_board_chat tests.test_board_protocol
  tests.test_board_turns tests.test_app_tools tests.test_agent_context tests.test_roles`
- **Live under Xvfb**, isolated `XDG_CONFIG_HOME` / `XDG_RUNTIME_DIR` / `TMPDIR` under a short path,
  `RELAY_KEYRING=off`, against a **copy** of a fixture board, evidence in
  `docs/qa_evidence/2026-09-2x-an-agent-is-the-prompt-box/`:
  1. A terminal pane, before and after, side by side: a turn with thinking folded and unfolded, a
     tool call with its OSC 8 fold, a second prompt queued with its row, a steer and its ×, Esc,
     `/model`, the mic, a `#ID` and a file link. **Nothing may differ.**
  2. The Switchboard's console: ask it something long — the answer streams as a terminal transcript
     with a **thinking bubble that folds** and **tool rows**, which is the Issue.
  3. Queue a second prompt on the Switchboard, steer it, withdraw the steer, reorder, Stop. All four
     are things the helper could not do.
  4. Options › Alt+Q: the console appears on first expand, asks, and its answer's `option:` link
     reveals the row. Same in Actions after a mode swap (the conversation continues), and in
     Sessions with a `session:` link.
  5. A card: Discuss in the box, then `p`, `x`, `v` from the action row; the thread file on disk
     holds the owner's words and the answer with their provenance.
  6. Options › Remote on: the phone's inbox lists the terminal panes and **no** helper.
  7. The owner's own Main (Claude Code): the helper falls back per #GH5T and says why in the box's
     tooltip; a `guest:` row in the console's model box is refused in one sentence.
  8. `relay.log`'s `pane_usage` lines for a tab with four helper consoles open, so the cost of
     item 4 in Risks is a number and not a guess.

## QA checklist

- [ ] A real provider, in every console: a rate limit, a refusal, a long reasoning block, a
      tool-call loop. Every turn in the implementer drive is a loopback stub.
- [ ] Options › Remote on, a tab with a Switchboard and an expanded helper: the phone's inbox
      lists the terminal panes and **no** console (Risks 7).
- [ ] A long session's queue in a console under load: steers, escalation, withdrawal, pause and
      the request ledger.
- [ ] The owner's own Main on a guest harness (#GH5T): a `guest:` row in a console's model box is
      still refused in one sentence.
- [ ] `relay.log`'s `pane_usage` for a tab with four consoles open (Risks 4).
- [x] **An agent's option write is announced after all** — closed 2026-09-21 (`efe55518`).
      Nothing swallowed anything: the write reached the setting, the row was marked, and the
      notice was posted. `NotificationsPopup::rebuild` sized the list's viewport at
      `min(6, count) * 56` px, which is what a one-line entry measures, so the entry carrying
      Undo was drawn under the bottom edge — and the earlier marker check read a screenshot in
      which Options had not left its General page. The list is now as tall as its rows measure,
      and an agent's own `app_undo` posts its own notice with a way back (#FEJQ decision 6,
      protocol 30.6). Driven again in
      `docs/qa_evidence/2026-09-21-console-write-undo/` — 19 PASS, 0 FAIL.
- [ ] Ctrl+Shift+Enter on a card, on its own and after a Discuss turn: it did not land in the
      implementer drive and whether that is the page or the drive is not settled.
- [ ] A restart with two tabs on one project: each tab's helper conversation comes back to the
      file it had. The drive read a new file instead; the store is keyed (workspace, tab).
