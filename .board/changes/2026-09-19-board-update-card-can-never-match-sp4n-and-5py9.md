---
id: E6XC
type: work
status: planned
labels: [bug, switchboard]
rank: zzzzzzzzr
created: '2026-09-19'
source: board cleanup pass, 2026-09-19
links: {plans: [], commits: [], evidence: [], related: [], github: null}
---
# board_update_card can never match #SP4N and #5PY9: their file hashes are not 64 characters

## Issue
Found during the 2026-09-19 board cleanup: board_read returns a file hash that is not 64 characters for two cards, and board_update_card then refuses every base_hash for them, so their labels can never be fixed. Measured: #5PY9 (issues/features/2026-09-19-new-convo-had-incorrect-context-remaining.md) returns a 65-character hash — f386d3142e609b673383a78bab3ddbd7ad05d738184448fc605cbefb4a076fc56 — stable across two reads, and the tool answers "base_hash must be the 64-character hash" for the 65-char form and "does not have the base_hash you sent" for 64-char truncations of it. #SP4N (issues/changes/needs_qa_llm/2026-09-18-settings-as-a-full-pane.md) returns a 63-character hash — a005a8f73dea61b114bb1765c0d6634ac4e712cbedc355c9a56443a0e163024 — refused with the same "must be the 64-character" error. Both cards are stuck without labels: 5PY9 should be bug,gui; SP4N should gain feature beside settings,gui,ux,palette.

## Done means
- #5PY9 carries labels `bug,gui` and #SP4N carries `feature` beside `settings,gui,ux,palette` — the label fixes the bug blocked on 2026-09-19.
- `board_read` returns a 64-character hex `hash` for every card on the board and `board_update_card` accepts it; a regression test asserts this so a non-64 hash can never ship again.
- The cause of the 2026-09-19 63/65-character readings is identified and recorded on the card: either a code path (with the fixing commit named) or an agent transcription error, with the evidence either way.
- Failure looks like: any card whose `board_read` hash is not 64 hex chars, or either card still missing its labels after this lands.

## Plan
## Goal
Unstick #5PY9 and #SP4N (their labels), pin down why `board_read` produced non-64-character hashes on 2026-09-19, and add a regression test so a malformed hash is caught by CI rather than by a cleanup pass.

## Findings
- The hash pipeline is `board_read` → `backend/relay_core/board_tools.py:2141` (`"hash": B.file_hash(card.path)`) → `backend/relay_core/board.py:1312` `file_hash()`, which is `hashlib.sha256(path.read_bytes()).hexdigest() if path.exists() else ""`. A sha256 hexdigest is **always exactly 64 chars**, so the current code cannot return 63 or 65.
- `board_update_card` validates at `board_tools.py:2416-2417` (`len(base_hash) != 64` → "base_hash must be the 64-character `hash` returned by board_read.") and compares at `:2423-2428`.
- **Re-checked today (2026-09-24) via board_read:** #SP4N returns `a005a8f73dea61b114bb1765c0d6634ac4e712cbedc355c9a56443a0e163024a` — 64 chars, and it is the card's measured 63-char string with the final `a` restored. The 2026-09-19 reading was the true hash with its last character dropped. #5PY9's file has changed since (it is now in needs-verification), so its 65-char reading cannot be re-derived, but it is the same off-by-one-at-the-tail shape.
- This points at transcription/transport, not hashing: either the glm-5.3 agent that ran the cleanup miscopied the hash between `board_read` and `board_update_card` (and read its own copy back as "stable across two reads"), or a since-fixed bug in the 2026-09-19 tool-result path truncated/padded long strings. The `git log` of `board.py`/`board_tools.py` around 2026-09-19 will say which.
- Both cards are still missing the labels the cleanup pass wanted: #5PY9's front matter has **no `labels`** (should be `bug,gui`); #SP4N has `settings,gui,ux,palette` and should gain `feature`.

## Steps
1. `git log --oneline --since=2026-09-18 --until=2026-09-21 -- backend/relay_core/board.py backend/relay_core/board_tools.py` and inspect any commit touching `file_hash` or the board_read result assembly. If a real bug existed and was fixed, record the fixing sha on this card. If nothing relevant landed, conclude agent transcription error and record that with the SP4N evidence above.
2. Fix the labels the bug blocked (this is the user-facing point of the card): `board_update_card` #5PY9 `fields: {labels: [bug, gui]}`; `board_update_card` #SP4N `fields: {labels: [settings, gui, ux, palette, feature]}`. Hashes confirmed 64-char today, so these should just work — if either refuses, that *is* the repro and step 1 restarts with a live case.
3. Add a regression test in the board tools test file (find it with `rg -l board_update_card tests/ backend/`): create a card in a temp board, `board_read` it, assert `re.fullmatch(r"[0-9a-f]{64}", hash)`, then `board_update_card` with that exact hash and assert success. Plus a sweep: for every `.md` under a populated temp `issues/` tree (or the real one, read-only), `file_hash` matches the same pattern.
4. If step 1 finds the bug was never in code, also make the failure mode self-diagnosing: in `board_tools.py:2417`, include the received length and first/last 4 chars in the error ("got 65 chars 'f386…fc56'") so a future miscopy is distinguishable from a code bug in one round-trip. Skip this if step 1 finds a real code path — fix that instead.

## Risks
- The 5PY9 65-char reading can no longer be reproduced from its file (the card moved on); the conclusion for it rests on the SP4N pattern. That is acceptable: the guard test in step 3 covers both shapes going forward.
- Step 4 changes an error message; grep `tests/` for any test asserting the current wording before editing it.
- No owner decision needed.

## Verify
- The new test(s) pass: `python3 -m pytest <board tools test file> -k hash`.
- `board_read` on #5PY9 and #SP4N after step 2 shows the new labels.
- Full board sweep: a one-off `python3 -c` over `issues/**/*.md` asserting every `file_hash` is 64 hex chars, output pasted into the card thread as evidence.
- Existing board tests still pass (`python3 -m pytest tests/ -k board`).
