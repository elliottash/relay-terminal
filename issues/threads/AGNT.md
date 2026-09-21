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
