# The open task list under the prompt — implementer evidence (card #TKS9)

Implementer evidence, not a QA verdict: the screenshots are prefixed `implementer-` because the
session that wrote the feature took them (`issues/README.md`, "QA evidence").

## How it was produced

    docs/qa_evidence/2026-09-19-open-task-list-under-the-prompt/drive.sh [build-dir] [scene...]

`drive.sh` runs a real `build/relay` under Xvfb against `stub-provider.py`, a loopback-only
OpenAI-compatible endpoint on `127.0.0.1`. Nothing of the owner's is touched: every run gets a
fresh `mktemp -d` sandbox and exports `HOME`, `XDG_CONFIG_HOME`, `XDG_DATA_HOME`, `XDG_CACHE_HOME`,
`XDG_RUNTIME_DIR` and `TMPDIR` into it, with `RELAY_KEYRING=off`, `[isolation] enabled=false` and
`[provider] preset=local:stub` in the sandbox's own `relay.conf`. No provider key is read or needed,
and the harness never attaches to a running Relay.

The stub scripts the turn from the first user message: `tasks3`, `tasks7`, `both7` and `keys7` each
call `update_todos` with a fixed list, then (for the last two) `agent(..., background=true,
todo_id=…)`. Its `SUBTASK` branch sleeps 90 s, so the background agents stay live for the shot.

The seven-task list is the point of the `window` scene: T1–T4 `completed`, **T5 `in_progress`**,
T6–T7 `pending`. The marginal task is then at index 4 of 7, so the window is
`clamp(4 - 2, 0, 7 - 5) = 2` → rows **T3..T7 with T5 third**, and T1/T2 off the top. A marginal task
at index 2 would have given T1..T5 and proved nothing about the window sliding.

## Scenes

Each scene is shot whole as `implementer-<scene>.png` and again as `implementer-<scene>-strip.png`,
a 200 % crop of the bottom 300 px where the composer and the strip are.

| File | What it shows |
|---|---|
| `implementer-none.png` | A fresh pane, no todo list and no subagent: **no strip at all** under the composer — not an empty card. The pane looks exactly as it did before this feature. |
| `implementer-tasks.png` | `tasks3`: three open tasks, no subagents. The strip is full width, the `main` row on top with `idle`, the key hint and `0/3 (3 unfinished)` in its metrics cell, then `◐ T1`, `○ T2`, `○ T3`. No overflow line — the whole list fits. |
| `implementer-window.png` | `tasks7`: seven tasks, T5 in progress. Five rows, **T3..T7, with T5 third of the five**, T3 and T4 drawn as green `✓` (completed tasks of the same list, shown because the window has room), and the overflow line `+2 tasks · Agents… and /tasks in the palette`. T1 and T2 are off the top: the window slid to the marginal task. |
| `implementer-both.png` | `both7`: the same seven tasks and two live background subagents. The rows split at half width with a 1 px rule down the middle, **`general a1` and `general a2` on the left, T3..T7 on the right**, and each pair on one row — a1 beside T5, a2 beside T6 — joined by the violet `✦` connector on the divider. T3, T4 and T7 have no subagent and their left cell is blank. |
| `implementer-keys-handing.png` | `keys7`, caught 0.35 s after the press: `↓` into the strip, `→` into the task column, `↓↓↓↓` to T7 (plain and pending), then `S`. The pane's own status reads **"Handing T7 to a subagent…"** — `Pane`'s `onRunTaskAsSubagent`, which sends `todo_subagent`. |
| `implementer-keys-run.png` | The same press a few seconds later: `✦ general a2 started in the background · T7 · record the evidence` in the terminal, a new `general a2` row paired with T7 on the right, and the `main` row's hint line now teaching the new keys — `↑↓ select · ←→ columns · Enter open · S run as subagent · Esc back`. In this run a2 had already finished, so the pair reads `✓ general a2` / `✓ T7`: the task followed its subagent to completed. |
| `implementer-keys-enter.png` | `↓ → ↓↓` to T5 — the task that *has* a subagent — then `Enter`: **`general a1`'s tab opens** in the subagent pane on the right, rather than the task list. Because that pane is open the strip folds to its one line, which now carries the new tail: `1 subagent running · 1 finished · 2 tasks open · Alt+A to open`. |
| `implementer-keys-prompt.png` | `Esc`: focus is back in the composer (its accent border is lit, the folded strip has no selection). |

`logs-<scene>/` holds that scene's `relay.log` and `worker.log` from the sandbox, which is where
the `update_todos` and `subagent_started` events can be read back. A `relay-<scene>.log` appears
only when the app wrote to stderr; in this run none did.

## What was checked against the design

Everything in the scenes above matched section B of the design on the first run. Nothing was
adjusted to make a screenshot pass, and no defect was filed against the widget.
