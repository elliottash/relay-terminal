---
id: PPR4
type: work
status: needs-verification
labels: [bug, performance]
assignee: claude-code
rank: m6
created: '2026-09-20'
source: 'Claude Code in the owner''s terminal, 2026-09-20 — found by the #PF4K profilers'
links: {plans: [], commits: [9702e504, b8e91fe3, b42c24f7, 5a76d136], evidence: [docs/qa_evidence/2026-09-20-perf-profile/, docs/qa_evidence/2026-09-20-perf-fixes/toolout/], related: [PF4K, 6W0Z], github: null}
---
# Tool output is sent to the GUI and thrown away; per-turn GUI cost grows with the conversation

## Issue
deploy opus subagents to profile and find performance issues and improvements. build it on sphinxpad as well to see how performs there

## Findings
Detail: [docs/qa_evidence/2026-09-20-perf-profile/transcript/FINDINGS.md](../../docs/qa_evidence/2026-09-20-perf-profile/transcript/FINDINGS.md), findings 2, 3 and 5. Stub provider, spark and sphinxpad.

1. `tool_output` (40.5 %) and `tool_result` (40.9 %) are 81 % of worker → GUI bytes, ~66 KB per tool call in the test. In the default configuration the GUI's only use is `m_toolLines += text.count('\n')` (`src/Pane.h:9699`); `result.output` is never read, and folds fetch their text separately. `QJsonDocument::fromJson` is 7.1 % of GUI cycles; about 1 s of a 6.8 s tool-heavy turn.
2. GUI cost per turn grows: 65 ms at turn 25 → 86 ms at turn 225 of a 300-turn conversation. `resolveFoldAnchors` runs `hyperlinkRuns` — a full-scrollback cell walk whose own comment says it is for resize/trim/clear — on every block close and on a 250 ms heartbeat; `FoldLayer::retainAnchored` uses `QVector::contains` (O(folds²)) and calls `rebuildAnchors()` per anchor.
3. One 65–81 ms event-loop stall at the start of each turn. Otherwise p99 is 2 ms over 35k iterations.

## Plan
1. A `set_agent_options` flag so the worker sends a line count unless "show tool output" is on. This is a protocol change: `docs/AGENT-SESSIONS-PROTOCOL.md` and the remote/phone view need to agree.
2. `QSet` in `retainAnchored`, one `rebuildAnchors()` per batch, incremental `hyperlinkRuns`, and no heartbeat while no fold is expanded.
Not measured: the Activity pane's `setThinking` re-renders the whole reasoning block (up to 400 KB of markdown) at 4 Hz — read in the code, but the pane could not be opened deterministically from the harness.

## Done
Evidence and the commands: [docs/qa_evidence/2026-09-20-perf-fixes/toolout/](../../docs/qa_evidence/2026-09-20-perf-fixes/toolout/README.md). Measured on spark against a clean export of each side, three alternating rounds for the tool-heavy scenario because five other #PF4K agents were on the box.

**1. `stream_tool_output` (protocol 23.10), `9702e504`.** `configure` / `set_agent_options` take a boolean, default **true** — today's behaviour exactly, so an old GUI and a phone behind one are unaffected. False sends `{lines, bytes, partial, counted}` in place of `tool_output {text}` and turns `result.output` / `content` / `screen` into `<field>_lines` / `<field>_bytes`. The label, `ms`, `diff`, `exit_code` and `error` are untouched (the label is built before the event is trimmed) and `tool_output_get` still answers with the whole of it. The GUI asks for it while nothing here reads the text — `showToolOutput() || sharedWithPhone()`: the phone prints a running call's output straight from the event (`app/app.js`) and has no fold to fetch it with, while the Activity pane, the call line and both folds want the count. Worker→GUI bytes for a tool-heavy turn **1 642 498 → 316 587 (−80.7 %)**, per tool call **66 829 B → 551 B (−99.2 %)**, GUI CPU for the 200-call scenario **6.95 s → 5.69 s (−18.1 %, 34.7 → 28.4 ms per call)**.

**2. The fold walks, `b8e91fe3`** (#6W0Z, the engine implementer): `retainAnchored` on a `QSet`, one `rebuildAnchors()` per batch, the paint path's per-row directory lookup, and the dirty-row region mapped through the fold layer.

**3. The 65–81 ms hitch at the start of a turn** is the model catalog. Finding 5 measured the gap between two `ppoll()`s, so the stack it could take was always the event loop; a probe that samples *inside* the gap puts 75 % of the samples in `models::shown()` → `curation::isShown()` → `shownKeys()` → **a `QSettings` construction per catalog entry**, reached from `Pane::refreshPickers()` on `changed()`. On an OpenRouter catalog that is a few hundred constructions, each re-stating the whole XDG search path, in one event-loop iteration. Fixed by reading the curated list once for the whole walk (`src/ModelCatalog.cpp`, out in `b42c24f7` — the model-picker session landed that file while this was in review and carried the hunk with it); the stacks are in `stall.txt`.

## QA checklist

1. **Nothing looks different.** With a provider key, ask the agent to run something with a lot of output (`seq 1 5000`, a build). The call is still one line that counts its output live — the count ticks up while it runs and the final line says the same number of lines it used to. Fold it open: the whole output is there.
2. **The stream still comes back.** Options › General › **Show tool output** on, ask for the same command: the output prints under the line as it arrives, exactly as before. Toggle it **during** a long-running command: the text starts appearing within a chunk or two, without a New chat. Toggle it off again mid-command: the text stops and the line goes back to counting, and the final count is still right.
3. **A second pane follows.** Two agent panes, both running a noisy command. Toggle Show tool output with pane A focused, then look at pane B: B's output comes back too (Options only tells the active pane, so the pane asks again when the shape that arrives is not the shape it needs).
4. **The Activity pane.** Ctrl+Shift+I (Activity) open, run a noisy command: the row counts lines live as it did, and clicking it still opens the full output.
5. **A phone still sees tool output.** Share a pane to a paired phone or browser and run a noisy command with Show tool output *off*: the phone's transcript must still show the output under the running call. Stop sharing, run it again: the desktop is unchanged. Start sharing *while* a command runs: the phone starts getting output.
6. **Guests.** A pane on `claude` or `codex` (Options › Models): a long tool call's output still appears where it did — under the line with Show tool output on, counted without it.
7. **Subagents.** Ask for a background subagent that runs something noisy and open its tab: its transcript is unaffected (subagent payloads are never trimmed).
8. **An old worker.** Nothing to do here beyond: after any of the above, `~/.local/share/relay/logs` must have no repeated `set_agent_options` (the pane asks once per change, not once per chunk).
9. **The turn-start hitch.** Start a turn in a pane with a large model catalog (an OpenRouter key, Options › Models showing many rows): the first frame of the turn should not visibly drop. Then check Options › Models still lists exactly the models it did, and that un-checking one still hides only that one.
