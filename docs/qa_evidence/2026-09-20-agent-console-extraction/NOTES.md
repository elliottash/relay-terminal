# The terminal pane, before and after the agent console's seam (#AGNT step 1, wave 1a)

Card #AGNT step 1 moves the agent out of `Pane`. Its one real risk is stated in the card itself
(Risks 1): **the terminal pane regressing.** This folder is what says it did not.

Wave 1a landed as `cbf06cc0d668`: `src/AgentHost.h` (`relay::agent::Host`, the seam a console is
drawn through), `src/AgentConsole.h` (the console, empty but for its host reference), and `Pane`
gaining the second base, the twenty-two `Host` members and `relay::AgentConsole m_agent{*this}`.
No agent code moved yet, and no public member of `Pane` changed.

## How it was driven

`drive.sh <relay-binary> <out-dir>`, twice, under Xvfb with an isolated `HOME`,
`XDG_CONFIG_HOME`, `XDG_DATA_HOME`, `XDG_RUNTIME_DIR` and `TMPDIR` under a short path,
`RELAY_KEYRING=off` and **no provider account**: the profile points a `local:` endpoint at
`stub-provider.py`, so the pane's agent is that script and answers the same words in the same
chunks every time. One launch, one pane, a fixed 1400x900 window, a fixed shell prompt.

Nine shots, the scenes the step must not change:

| shot | what it holds |
|---|---|
| `01-fresh` | a fresh pane and its prompt box |
| `02-thinking` | a turn with reasoning, settled under its `✦ thought for N s` anchor |
| `03-fold-open` | the same fold unfolded with Alt+R (`agent.thinkingPanel`) |
| `03b-fold-closed` | and folded again |
| `04-toolrow` | a `read_file` call drawing its own `▸` row |
| `05-modelbox` | the model box dropped open with Alt+M (`agent.modelBox`) |
| `06-modelbox-closed` | and dismissed |
| `07-queued` | a second prompt queued behind a running turn, with the queue strip |
| `08-esc` | Esc during that turn |
| `09-after` | the pane when it is all over |

Both runs pass the same six OCR checks (`before/notes.txt`, `after/notes.txt`): the prompt box is
up, the reasoning fold settles under its anchor, Alt+R unfolds it, the tool call draws its own
row, the model box drops open, and the second prompt queues with its strip.

The two binaries are built from clean exports, and the comparison isolates exactly one change:
**before** is `37563ce447ea` (the tip this session started from) and **after** is that same tree
with `git diff cbf06cc0^ cbf06cc0` applied — nothing else that landed on `main` in between.

## What the pixels say

`compare -metric AE`, the full table in `diffs.txt`.

A third run was made — the **before** binary, a second time — as a control, because a pixel
comparison of a live GUI is worth nothing without one. It is in `control/`.

**Whole window**, the control already differs from its own binary by 1 316–1 539 pixels on every
shot from `02` on. The difference is one place: the pane's auto-generated title in the header
(`diff-02-thinking-pane-title.png`, a 249×20 box at +91+46 — "reasoning fold functionality"
against "how reasoning folds work"). Relay titles a pane from its conversation, and that is not
deterministic under this stub. It is therefore not attributable to anything in this change: the
same binary does it to itself.

**Below the pane header** (crop `1400x824+0+76`, which is the terminal, the transcript, the queue
strip and the composer — everything the step touches):

| shot | control | before → after |
|---|---|---|
| `01-fresh` | 0 | 21 310 → **36** once the toast band is masked, see below |
| `02-thinking` | 0 | **0** |
| `03-fold-open` | 36 | 36 (and **0** against the control) |
| `03b-fold-closed` | 0 | **0** |
| `04-toolrow` | 0 | **0** |
| `05-modelbox` | 0 | **0** |
| `06-modelbox-closed` | 0 | **0** |
| `07-queued` | 81 | **0** |
| `08-esc` | 36 | **0** |
| `09-after` | 0 | 36 |

Six of the ten are **pixel-identical**. Every non-zero number is one of two known,
non-deterministic things, and each was read rather than waved away:

- **36 pixels is the caret.** A 2×18 box at +36+824 — the text cursor in the prompt box, blinking,
  on in one shot and off in the other. It appears in the control at the same size (`03-fold-open`,
  `08-esc`), and on `07-queued` and `08-esc` the *after* run matches the *before* run exactly
  while the control does not. 81 pixels on `07-queued` is the caret plus the turn clock's digit.
- **21 310 pixels on `01-fresh` is the startup quota toast** — "Automatic agent turns: 0 used of
  50 · 500 steps / 2000 tool calls per turn · request audit off" — which is up for a moment after
  launch and which the screenshot caught in the after run and missed in both before runs
  (`diff-01-fresh-startup-toast.png`). Mask its band (`rectangle 700,750 1400,800`) and the shot
  comes back to the caret's 36 pixels, with the control at 0.

Nothing in the transcript, the folds, the tool rows, the queue strip, the model box or the
composer differs by a pixel.

## Also here

- `coupling-wave-1b.txt` — `scripts/split-agent-console.py --coupling 5119-6018 12416-13273`,
  what the card's wave 1b would actually cost: 33 fields travel with the cut, **66 cross it**,
  and **52 members of `Pane` are called from inside it that are not on `Host`**. The heaviest are
  `m_editor` (181 uses left behind), `m_backend` (131), `m_agentBusy` (71), `m_login` (67),
  `m_cwd` (59), `m_token` (40), `m_workspace` (32).
- `coupling-printinline.txt` — the same for the 137-line prose / `printInline` slice, to show that
  narrowing the cut does not rescue it: 8 fields and 11 members still cross.

Those two files are why waves 1b–1e are declared and empty rather than half-done; the tool's
docstring says what they change about the card's seam, and the thread entry on #AGNT asks for it.

---

# Step 3: the console takes a Context (`24042b5b` + `81e889c1`)

The same nine-scene drive, on `24042b5b^` and on the landed tree, in `step3-before/` and
`step3-after/`; the numbers are in `diffs-step3.txt`.

**Seven of the ten shots are pixel-identical across the whole window**, header included. The
three that differ are 36, 117 and 36 pixels, and each was read: `02-thinking` and `08-esc` are a
2×18 box at +36+817, the blinking caret; `07-queued` is a 174×42 box at +36+793, the turn clock
reading "Relaying · thinking… · 8 s" against "9 s", plus the caret
(`step3-diff-07-queued-turn-clock.png`).

## The gate earned its keep

The first landing of step 3 was **wrong**, and neither the build nor the seventeen suites said
so. `Pane::TerminalContext` sent `persist.scope` as the workspace path and `routing` as the
pane's input mode. Both are closed sets on the wire — `PERSIST_SCOPES` is `""`, `pane`, `helper`
and `ROUTINGS` is `auto`, `agent` (`backend/relay_core/agent_context.py`) — so
`ContextSpec.from_json` raised on every `configure`, the worker never built an agent, and the
pane came up with a filled model box that did nothing when you pressed Enter.

The drive failed six of its six checks on that binary and passed all six on the tip before it,
which is how it was found. `81e889c1` sends `persist.scope: "pane"` (which store, not which path
— `ContextSpec.store()` only redirects for `"helper"`, so a pane's session stays in
`relay/sessions/` and nothing moves) and `routing: "auto"` flat (the surface's answer to "may a
typed line be a command or a prompt", not the pane's input mode, which picks where *this* line
goes). Re-run, all six pass and the pixels are above.

## And what the re-measurement says about waves 1b–1e

`coupling-whole-agent-block.txt` and `closure-agent-block.txt`, both reproducible from
`scripts/split-agent-console.py`.

Cutting the **whole** agent block at once — 326 members, 6 129 lines — leaves **165 fields and
167 members** of `Pane` on the other side (18 calls are already on `Host`). `--closure`, which
starts from the owner's own "agent sessions UI" banner and grows it to a fixed point by adding
anything whose every remaining mention is inside the set, stops at 228 members / 4 031 lines with
a **minimum seam of 284 names** — 157 fields, 127 members. That is the floor, whatever order the
waves run in.

The single most informative number is `m_editor`: **109 uses of the composer stay in `Pane`**
even when the entire agent block moves. Reading them (`src/Pane.h`, `requestRoute` and its
neighbours) they are one thing — the routing decision: read the typed text, try a slash command,
an alias, a skill, then choose the shell, the foreground program, an ssh login, or the agent.

So the prompt box is not a part of the agent block that can be lifted out of it. It is the thing
both halves are made of, which is the owner's sentence read literally: *an agent interface is the
prompt box*. A cut that moves the agent out and leaves the terminal holding the composer is
therefore the wrong cut, not merely a large one — and the cut that is right inverts the card's
host relationship, because the console would own the box and the terminal would become one
routing of what is typed in it. That is a decision about the card's shape, so it is written down
here and in `issues/threads/AGNT.md` rather than taken.
