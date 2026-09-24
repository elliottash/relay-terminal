# M5FZ — staging notes

## What is staged

`stage.sh` (no arguments, safe to run twice — it resets everything under `/tmp/claude-1000/tryit/m5fz`)
leaves a Relay running on the private display `:96`:

- a seeded git repo at `/tmp/claude-1000/tryit/m5fz/seed` (real files, so the subagent's
  `read_file` / `run_command` land on real results);
- an isolated profile (`HOME`, `XDG_*` under the fixture root) with `RELAY_KEYRING=off`;
- the scripted model at `mock_model.py`, bound to `127.0.0.1:8731` only — it speaks the hosted
  session's `challenge`/`register`/`quota` endpoints (granting a token to any proof) and the
  OpenAI-style chat the app speaks. `RELAY_HOSTED_URL` points the app at it
  (`hosted.endpoint_for` follows the override for a canonical config), so the pane's model — the
  free tier — never leaves the machine and never needs a key. A minimal `PATH` keeps the
  claude/codex CLI guest rows out of the catalog, so the pane starts on the scripted model.
- one finished **background subagent, "notes QA sweep"**: its transcript contains a landed
  `read_file notes.md` and a landed `run_command grep -c . notes.md` plus its final report. That
  is exactly the state the bug was seen in: the row sits in the subagents strip, and the first
  click opens its tab on the `subagent_transcript` snapshot.

The prompt is submitted by the script (OCR-located clicks on the first-run dialog's *Not now*
and on the input line, then real XTEST typing — the recorded agent-turn pattern from
`2026-09-17-agent-drives-programs`; the drive socket cannot type into a pane, and synthetic
`--window` keys are ignored by the input line).

## The two layers of the pass

- `tests/subagents_test.cpp::transcriptSnapshotOpensOnToolRows` feeds the view the exact snapshot
  shape the worker now emits (`transcript_items`, verified live against the SubagentManager in
  `tests/test_subagents.py::TranscriptItemsTests`) and asserts the folded row, the absent raw
  JSON, and the fold still holding it.
- `ai-pass.sh` plays the whole staged situation: it re-runs `stage.sh`, clicks the strip row by
  OCR, and asserts on the screenshot's OCR that the subagent tab opened over the pane, its status
  counts "2 tools", and no `json.dumps` tool result is visible anywhere on screen. The tab's
  transcript strip is compact and scrolled to its tail on a fresh 1600x1000 layout, so the folded
  rows themselves sit just above the visible fold — scrolling the strip (wheel up over it) shows
  them; whether that first paint *looks* right is the person's judgement, which is what this
  card waits on.

## Staging facts worth keeping

- `open-socket` under `$XDG_RUNTIME_DIR/relay/` is a **file naming the socket's address**, not a
  socket; wait for it with `[ -s ]`.
- `RELAY_HOSTED_URL` must be `127.0.0.1`: plain HTTP is only allowed to a loopback model server,
  and `127.0.0.2` is not recognised as loopback by that check.
- A custom provider at a loopback URL shows "no key yet" and is **unselectable** in the model
  picker (`customproviders.row` reports `has_stored_key: False` for the loopback "local" key
  source) — noted on the card as a separate gap.
- Relay Free stays rank 1 while python3-cryptography imports (`hosted.available()` never touches
  the network), which is why the mock masquerades as the hosted gateway instead.
- SSE tool-call deltas need an `index` on each entry or the worker logs "Invalid tool-call
  index." and retries.
