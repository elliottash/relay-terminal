# Codex guest fixtures

Data for `tests/test_guest_codex.py`, taken from real Codex runs on a developer machine and then
trimmed. Nothing in the suite reads the live `~/.codex`, so these files are how the tests state
what Codex writes instead of hoping it is installed.

## `sessions/YYYY/MM/DD/rollout-*.jsonl`

One JSON object per line, in Codex's own order, under the directory layout Codex uses. Each file
is a real rollout with the bulky parts that the tail never reads (base instructions, permission
profiles, message bodies) trimmed, so a fixture stays readable while every record type and every
field the tail reads is exactly as it was written.

* `2026/09/18/rollout-…-01a0b785-bca3-7b22-aa25-4c4c444a276d.jsonl` — a whole turn:
  `session_meta` (cwd, session id), `task_started`, a `response_item`, `turn_context` (the model),
  a `token_usage_record` and a `token_count` (the numbers the chips show), then `task_complete`.
  Its mtime is the oldest of the three.
* `2026/09/19/rollout-…-01a0b7c1-aaaa-7bbb-8ccc-ddddeeeeffff.jsonl` — the same session cut off
  mid-turn: `task_started` with no completion after it, which is what a tail sees while Codex is
  working. Newer than the first.
* `2026/09/19/rollout-…-01a0b7c2-bbbb-7ccc-8ddd-eeeeffff0000.jsonl` — `session_meta` only, for
  `/home/elliott/repos/sweet-street`. Newest of the three, and the reason a pane's `cwd` has to
  decide which rollout it follows.

Tests copy these into a temporary directory and set the mtimes themselves, so a file's order in
the list never depends on when the checkout happened.

## `config.toml`

A copy of a real `~/.codex/config.toml`: `model`, `[projects."…"]` tables with quoted paths,
`[tui.model_availability_nux]`, `[marketplaces.codex-warp]`, `[plugins."warp@codex-warp"]` — and
no `[tui]` table of its own, which is the case Relay has to create one for.

## `config-with-comments.toml`

Written by hand in the same shape, for the things a config can hold that the copy above does not:
comments (including one after a value and one containing a `#`), a multi-line array, an inline
table, a table that is not last in the file, and a user's own `[tui] notification_condition` —
the value Relay has to replace and put back.
