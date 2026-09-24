---
id: SFP6
type: work
status: planned
labels: [bug, agent-tools]
rank: zzzzzzzzi
created: '2026-09-19'
source: pane 1, 2026-09-19
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# edit_file's over-size refusal reuses read_file wording and gives no way out

## Issue
is this a bug? (in the tool call)

▸ edit src/BoardPane.cpp ✗ · File exceeds the 128 KiB preview/read …

## Decisions
- A deliberate, by-design refusal (a guard's `ValueError`, e.g. edit_file's over-size) keeps the ✗ but renders in dimmed/neutral ink, never red. Red stays strictly for "failed" per the colour table; amber stays for waiting-on-a-person. Backend marks guards' deliberate `ValueError`s as `refused: true` so the GUI can pick the ink. Owner, 2026-09-19: "i agree that non-actionable "errors" like this one should have the x but not in red. plan that". Split out to its own card for planning; #SFP6 keeps tracking the message wording.

## Done means
Calling edit_file (or read_file) on a file over 128 KiB still refuses — the cap itself is intended — but the error text now says what to do instead: use `run_command` (sed/python) to read or change part of the file. The message no longer reads like a read_file preview error when it came from edit_file, and the same wording appears on the ssh (`host`) path. Failure looks like: an over-size edit_file refusal that names no alternative, or wording that mentions "preview/read" only.

## Plan
**Goal.** Make the over-128-KiB refusal from the file tools name the way out, per the edit_file card's own principle "Refusals say what to do instead". The cap stays; only the message changes.

**Findings.** The message "File exceeds the 128 KiB preview/read limit." is raised in exactly two places in `backend/relay_core/tools.py`:

- `Workspace.read_bytes` (tools.py:476) — shared by `read_file` (tools.py:1074), `edit_file`'s prepare (tools.py:738, via `self.workspace.read_bytes(path)`) and `write_file`'s prepare for its diff (tools.py:744). When edit_file on a big file refuses, this read-flavoured wording is what surfaces verbatim.
- `_as_text` (tools.py:1257) — the same guard for files read over ssh (callers at tools.py:861 and :877).

`read_file` cannot read an over-size file even with `from_line`/`to_line` (the guard fires before any range handling), so `run_command` is the only alternative in every case — one shared reworded message covers read, edit and write-diff alike. `_edited`'s other errors (tools.py:994-1019) already follow the target style, e.g. "Use write_file to create a file or replace one in full."

**Steps.**

1. Reword both raise sites (tools.py:476 and tools.py:1257) to the same new text, e.g. `"File exceeds the 128 KiB limit of Relay's file tools. Use run_command (sed, python) to read or change part of it."` — keeps "128 KiB" so the existing regex in `tests/test_ssh_remote.py:364` still matches, drops the read-only "preview/read" framing, and names the alternative.
2. Add a test in `tests/test_tools.py`: `edit_file` against a file just over `MAX_FILE` (131072 bytes) raises `ValueError` whose message mentions `run_command`; same for `read_file`. If a size-guard test already exists there, extend it rather than adding a parallel one.
3. Leave "File is too large or binary." (tools.py:479) and "The edited file would exceed the 128 KiB limit." (tools.py:1017) alone — out of this card's scope (binary content, and a result-size guard whose alternative is a smaller edit).

**Risks.** Message text is user- and model-facing; keep it one sentence in the style of the neighbouring `_edited` errors. No behaviour change beyond the string, so blast radius is the two raise sites and their tests. Rendering of this refusal (dimmed ✗, `refused: true`) is **not** part of this card — that is #25XG.

**Verify.** `python3 -m pytest tests/test_tools.py tests/test_ssh_remote.py -q` (targeted, not the full suite). Manual check: in a pane, ask the agent to edit a >128 KiB file (e.g. `src/BoardPane.cpp`, ~200 KB) and confirm the ✗ message names `run_command` as the way out.
