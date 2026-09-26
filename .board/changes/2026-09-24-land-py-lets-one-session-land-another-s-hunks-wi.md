---
id: WNKN
type: work
status: needs-verification
labels: [bug, land, git]
assignee: agent
implemented_by: anthropic/claude-fable-5-1 via claude-code
session: 987d2a1a-45ad-4dcd-8743-d17c68af8241
rank: zzzzzzzzzzzzzzzzzzzzzr
created: '2026-09-24'
verify: {artifact: code, primary: script, also: [], human: none, sign_off: none, effort: medium, stakes: rework, blast: capability}
source: Claude Code pane, 2026-09-24, analysis of session 3f4a20ad (#234Z)
links: {plans: [], commits: [83a46dd98815, 83218821d6df, 245d2efe4cde], evidence: [docs/qa_evidence/2026-09-25-wnkn-foreign-hunks/], related: [234Z, 6CSN, 76QW], github: null}
---
# land.py lets one session land another's hunks with a single --confirm, and a late begin silently absorbs your own edit

## Issue
write cards for all 6 of your lessons and how to address them, and all 5 of your suggestsions.

## Planning notes
**Defect 1: one `--confirm` lands someone else's hunks.** Land session `234z` began at 19:50:55 claiming `src/Pane.h`. Session `subagent-models` (#6CSN) began at 19:53:47. #234Z then wrote its busy-line hunk (`Relaying · %1… · %2 exits`). At #6CSN's commit, every `Pane.h` hunk was correctly **contested** (it began second) and held. It confirmed the digest wholesale, and `960c0e70` ("Subagents: every model is a choice") landed #234Z's hunk inside #6CSN's commit, half a feature and without its tests. #234Z's own commit later found a conflict and spent ~17 requests and 2.6M tokens mapping hunks by hand. The same shape came up again today: the #SZHQ monitor's first dry-run carried #XQ8F's `RelayWindow.h` ssh-reattach hunks, and only a manual read caught it.

**Defect 2: a late `begin` absorbs your own edit.** `234z` edited `tests/keymap_test.cpp` and `docs/QUEUE-INTERRUPT.md` before claiming them, so the snapshot already held its edit and `commit` would have silently dropped it. It noticed and re-based with `--base main`. Nothing warned it.

**How to address.**
1. **Attribute contested hunks where Relay knows the author.** Relay's session checkpoints record per-file before/after blobs for each turn (`checkpoints.items[].files`). land.py (or a Relay-side check) can ask "did another live session's checkpoint produce this hunk?" If so, the hunk is **foreign**, not merely contested.
2. **Foreign hunks cannot be `--confirm`ed.** They are excluded by default, and landing one takes `--take-foreign <path>:<n> --from <session>`, which is recorded in the commit trailer and posted to that session's card.
3. **The review names the other session per hunk** ("hunk 2: appeared after you began; `234z` edited this region at 19:58"), not just CONTESTED. A blanket confirm is then a visible choice, not an easy default.
4. **Late begin:** Relay's own `edit_file`/`write_file` auto-`begin` a path in the pane's land session before the first write, so a claim can never be late for tool edits. For shell edits, `begin` warns when the path differs from the tip *and* this session's checkpoint shows it wrote the file earlier: "you already edited this; use `--base main`".
5. Test both with two simulated sessions in `tests/test_land.py`.

**Done means.** A replay of the #6CSN/#234Z sequence refuses to land #234Z's hunk under #6CSN without `--take-foreign`. A late `begin` on a file the session already wrote warns and suggests `--base`.

## Done means
A replay of the #6CSN/#234Z sequence in `tests/test_land.py`: the session that began second sees the first pane's hunk tagged FOREIGN with the pane, card and time named, one `--confirm` lands only the committer's own hunks (the foreign hunk stays in the tree, off main), and the foreign hunk reaches main only via `--take-foreign`, recorded in the commit trailer. A claim is never late for the pane's own tool edits (auto-begin from the executor's write path before the first write, and a later manual `begin` adopts that earlier snapshot), and a late `begin` on a file this pane's authorship journal shows it already wrote warns and suggests `--base`. A native pane's `run_command` children see `RELAY_SESSION_TOKEN`.

Failure looks like today: one `--confirm` landing another session's half-feature without its tests, or an edit made before `begin` silently missing from the landed commit.

## Plan
**Goal.** Close both defects from the planning notes: a blanket `--confirm` can no longer land another session's hunks (they become FOREIGN, attributed to the pane and card that made them, excluded by default), and a claim can no longer be late for the pane's own tool edits (auto-begin before the first write, snapshot adoption, a warning when a shell edit slipped in first).

**Findings (re-read 2026-09-25 evening; three of the 04:00 plan's premises were wrong).**
- `scripts/land.py`: `cmd_commit` (~1366) computes per-path hunks (`path_hunks`), marks contested ones (`contested_hunks` ~924, begin-time markers between land sessions), holds contested/stale/`--base` paths and `--confirm <digest>` lands everything held; `--exclude-hunk`/`--only-hunk` select; `--no-verify` refused while held. `cmd_begin` ~775; parser `build_parser` ~2209; registry entries carry `repo, branch, started, updated, contact, claims` and nothing that names a pane.
- **The pane token is already trustworthy per process** — the plan's qputenv worry was misplaced. `Pane::startWorker()` (`src/Pane.h:10716`) builds the worker's `QProcessEnvironment` explicitly with `RELAY_SESSION_TOKEN = m_token` and `RELAY_PANE_ID` ("never qputenv, which leaks between panes"); the `qputenv` at `src/PaneRuntime.cpp:688` is for the pane's shell, spawned right after it, so the shell and every guest CLI under it inherit the right token too (this Claude Code pane's environment holds `RELAY_SESSION_TOKEN` and `RELAY_PANE_ID` for its own pane — checked). The worker reads it from `os.environ` already (`agent.py:648`, scratch keying, #DVV2).
- **The real gap is the opposite one:** `tools.command_env()` (`backend/relay_core/tools.py:1377`) strips **every `RELAY_*`** variable from `run_command` children, so a native pane's `land.py begin` sees *no* token at all, while a guest's shell sees the right one.
- **Checkpoints cannot give diff(before, after).** `CheckpointStore.record_before` (`checkpoints.py:66`) stores the pre-image blob under `<session>.blobs/<sha256>`; `record_after` (76) stores only the new sha256, never the bytes. The 04:00 step 5 ("match the hunk against diff(before, after) from the checkpoint") has no `after` to diff against. Also the store is per workspace-digest session dir, and guest-bridge edits (`guest_bridge.call_tool` ~969 → the executor, no `Agent`) never reach it.
- Both host and bridge writes funnel through `ToolExecutor.execute` for `write_file`/`edit_file` (`tools.py` ~1069–1130; `agent.py:4610` wraps that call with the checkpoint records). One hook there covers both.
- #FYEY step 4 plans an authorship journal at edit time for `--only-hunk --by`. That is the same data this card needs. One mechanism, defined here and consumed by #FYEY: **an authorship journal in the land root**, not a checkpoint scan.
- `tests/test_land.py` already simulates two land sessions with a private `--root` (`LandCase.land`, `age_snapshot`, `digest`) and has the ancestors of both tests: `test_a_hunk_made_after_the_other_session_began_is_contested`, `test_the_session_that_began_second_has_every_hunk_contested`, `test_a_file_edited_before_begin_says_so_instead_of_landing_nothing` (only the solely-before case).

**Steps.**
1. **Token reaches `run_command` children.** `command_env()` keeps `RELAY_SESSION_TOKEN` and `RELAY_PANE_ID` (an allowlist inside the `RELAY_*` strip; they are identity, not secrets). Test in `tests/test_tools.py`. No session-file change: the worker's own environment is per pane already.
2. **Auto-begin before the pane's first tool write**, in `ToolExecutor.execute`'s `write_file`/`edit_file` path (both host and bridge): when the path is inside a git repo whose root has `scripts/land.py` and the pane's token session does not already claim it, run `scripts/land.py begin <token> <path> --contact "pane <RELAY_PANE_ID>[ #card]"` with meta `auto: true`, ~5 s timeout, every failure logged to the session log and never blocking the edit. The snapshot is taken before the write is applied. `<token>` is the pane token from `os.environ`; the card, when the board context holds a claim, goes into the contact.
3. **Authorship journal.** The same hook, after the write, appends one line to `<land root>/authors/<token>.jsonl`: `{ts, repo, path, before, after, pane, card, turn_id}`, and stores both blobs content-addressed under `<land root>/blobs/<sha256>` (the pre-image is the bytes the checkpoint already read; the post-image is what was just written). `land.py gc` drops journal lines and blobs older than `GC_DAYS`. Guest-bridge edits get the same entries; edits made by a guest's own harness tools (this pane's Claude Code `Edit`) or a shell get none and stay CONTESTED.
4. **`begin` adopts an auto-claim.** `cmd_begin` records `token` (from `RELAY_SESSION_TOKEN`) in the session meta and registry. Beginning a path already claimed by an `auto` session with the same token adopts that earlier snapshot and timestamp and marks the auto claim's path as merged into `<me>`, instead of snapshotting the now-edited file. This is the root fix for defect 2.
5. **FOREIGN attribution at commit.** For each held path, read every *other* token's journal lines for this repo and path (skip my token; skip sessions idle past `IDLE_HOURS`), compute the hunks of diff(before blob, after blob) per line, and tag a held hunk FOREIGN when its removed+added line blocks (not line numbers) match one of them — attributed to `pane <id>`, its card and the time. No journal, unreadable blobs, or a re-indented hunk: CONTESTED exactly as today.
6. **Review and gates.** The held-path review names the author per hunk ("FOREIGN — pane 3f4a20ad (#234Z) edited this region at 19:58"). `--confirm` excludes FOREIGN hunks by default; they stay in the working tree and the output says so. Landing one takes `--take-foreign <path>:<n>` (validated against the current attribution), which appends `Relay-take-foreign: <path>:<n> from pane <id>` to the message. `--no-verify` is refused while FOREIGN hunks are held. The digest covers the exclusion and the takes.
7. **Late-begin warning.** `cmd_begin` without an adoptable auto claim, on a path whose working copy differs from the tip **and** whose own-token journal lines predate this begin: warn "you edited this at HH:MM; earlier edits will not land; use `--base main`".
8. **Tests.** `tests/test_land.py`: (a) replay #6CSN/#234Z — journal lines and blobs fabricated in the private root for the first pane's edit; the second-begun session's commit holds with FOREIGN naming that pane; `--confirm` lands only its own hunk (the first pane's edit still in the tree, absent from main); `--take-foreign` lands both with the trailer; (b) auto-claim adoption — `begin` under an `auto` token session, edit, manual `begin <me>`, commit lands the pre-begin edit; (c) the late-begin warning fires with own-token journal lines and not without; (d) own journal lines never mark own hunks foreign; (e) no journal behaves exactly as today. `tests/test_tools.py`: `command_env` passthrough; the executor hook against a fake `scripts/land.py` that records its argv (auto-begin runs once per path, before the write; a failing script does not block the edit; the journal line and both blobs exist afterwards). Update `scripts/land.py --help` and `CLAUDE.md` § "Contested hunks" (FOREIGN, `--take-foreign`, auto-begin, the token).

**Orchestration.** Steps 1–3 are `backend/relay_core/tools.py` plus `tests/test_tools.py`; steps 4–8's land.py half are `scripts/land.py` plus `tests/test_land.py`. Two tracks, no shared files; the land.py track can start on the journal format (step 3's line shape) before the hook lands. #FYEY track B waits for this card's step 3 format and step 4's `token` field.

**Risks.**
- **Coverage is partial by construction**: shell edits and guest-harness edits leave no journal, so their hunks stay CONTESTED and a blanket `--confirm` still lands them. This narrows the failure to sessions that edit outside Relay's tools; the marker mechanism keeps catching the rest as contested.
- The journal grows with every edit; blobs are content-addressed and gc'd with `GC_DAYS`. Bound the per-token file (rotate past a few MB).
- **Shared mechanism with #FYEY** is this review's call: one journal, written here, read by #FYEY's `--by`. Say so if you would rather keep them separate.
- Planning note 2's "posted to that session's card": the trailer plus the printed pane and card replace it; a `--card #ID` thread comment can be added if wanted.
- **Wider scope**: if #234Z's six lessons and five suggestions hold items beyond these two defects, they need their own cards — owner's call.

**Verify.** `python3 -m pytest tests/test_land.py tests/test_tools.py -q` (old cases unchanged, new ones green); `python3 scripts/land.py --help` shows the new flags; then dogfood: from a Relay pane, edit a file with `edit_file`, run `land.py who` and see the auto claim under the pane's token; land this change itself through `begin`/`commit`. Manual replay: two panes, the second edits `src/Pane.h` after the first began; the first pane's `--confirm` must leave the second pane's hunk untouched and name it.

## Decisions
- 2026-09-25, owner: **one authorship journal shared with #FYEY** ("3 yes"): written here from `ToolExecutor.execute` into `<land root>/authors/<token>.jsonl` with content-addressed blobs; #FYEY's `--only-hunk --by` reads it.
- 2026-09-25, owner: **`--take-foreign` also notes the foreign hunk's card** ("4 yes"). Written by land.py through the board's file fallback (append a note to `.board/threads/<ID>.md`, card from the journal entry's `card`), so it works from guest panes and shells as well as through the `land_try` tool. Step 6 extended.

## Execution Summary
Two commits, both landed through `scripts/land.py` by Opus subagents of pane 987d2a1a, plus the evidence commit.

- **`83a46dd9` backend** (`backend/relay_core/tools.py`, four lines in `agent.py`): `command_env()` now passes `RELAY_SESSION_TOKEN` and `RELAY_PANE_ID` through to `run_command` children (an allowlist inside the `RELAY_*` strip). `ToolExecutor.execute`'s disk-write path runs, before a pane's first write of a path in a repo that has `scripts/land.py`, `land.py begin <token> <path> --auto --contact "pane <id>[ #card]"` (5 s timeout, every failure logged, never blocking the edit), and after the write appends `{ts, repo, path, before, after, pane, token, card, turn_id}` to `<land root>/authors/<token>.jsonl` with both blobs under `<land root>/blobs/<sha256>` (rotates past 4 MB). The card comes from `board.claimed`.
- **`83218821` land.py** (`scripts/land.py`, `tests/test_land.py`, CLAUDE.md § Contested hunks): `begin --auto` and a `token` field in meta and registry; a manual `begin` under the same token adopts a live auto claim's snapshot and timestamp (`adopted_from`) and retires the auto claim; at commit, every held hunk is matched by content blocks against `split_hunks(before, after)` of other tokens' journal lines and tagged **FOREIGN — pane <id> (#card) edited this region at HH:MM**; `--confirm` lands with FOREIGN hunks excluded (they stay in the working tree, snapshot advanced as `--exclude-hunk` does); `--take-foreign PATH:N` re-includes one, appends `Relay-take-foreign: PATH:N from pane <id>` to the message and notes the foreign hunk's card thread (`.board/threads/<ID>.md`, best effort); `--no-verify` stays refused while anything is held; the digest covers exclusions and takes; the late-begin warning names the time of the earlier edit and suggests `--base main`; `gc` prunes journal lines and blobs older than `GC_DAYS`. `--help` documents all of it.

Evidence: `docs/qa_evidence/2026-09-25-wnkn-foreign-hunks/README.md` (tests run on a clean `git archive` export, never in the checkout). Known limits are on the README and unchanged from the plan's Risks: guest-harness and shell edits leave no journal and stay CONTESTED; editor-buffer writes bypass the executor's disk path; running workers pick up the hook at their next restart.

## Tests
- `tests/test_land.py` — 100 passed (class `Foreign`: the #6CSN/#234Z replay, `--take-foreign` with trailer and card note, auto-claim adoption, late-begin warning, own-journal never foreign, missing blob stays CONTESTED; `Gc`: journal and blob pruning)
- `tests/test_tools.py` — 48 passed (`LandAuthorship`: auto-begin once per path before the write, failing or slow script never blocks, journal line and blobs; `command_env` passthrough)
- `manual: docs/qa_evidence/2026-09-25-wnkn-foreign-hunks/README.md`
